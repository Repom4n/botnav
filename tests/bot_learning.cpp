#include <string>
#include "g_bot_learning.h"
#include "ai_combat_tuning.h"

#include <boost/test/unit_test.hpp>

namespace
{
	botlearn_event_t MakeEvent( int time, int actor, int token, int damage = 0 )
	{
		botlearn_event_t ev = {};
		ev.time = time;
		ev.actor = actor;
		ev.token = token;
		ev.damage = damage;
		ev.selfHealthArmor = 150;
		ev.enemyHealthArmor = 150;
		ev.selfForce = 100;
		ev.enemyForce = 100;
		ev.rangeBucket = 1;
		return ev;
	}
}

BOOST_AUTO_TEST_SUITE( bot_learning )

BOOST_AUTO_TEST_CASE( wall_escape_tokens_are_appended_responses )
{
	BOOST_CHECK_EQUAL( (int)BOTLEARN_TOK_KNOCKDOWN, 10 );
	BOOST_CHECK_EQUAL( (int)BOTLEARN_TOK_WALLRUN, 11 );
	BOOST_CHECK_EQUAL( (int)BOTLEARN_TOK_HOP, 13 );
	BOOST_CHECK_EQUAL( std::string( BotLearn_TokenName( BOTLEARN_TOK_ROLL ) ), "roll" );
	BOOST_CHECK( BotLearn_IsResponseToken( BOTLEARN_TOK_WALLRUN ) );
	BOOST_CHECK( BotLearn_IsResponseToken( BOTLEARN_TOK_HOP ) );
	BOOST_CHECK( BotLearn_IsResponseToken( BOTLEARN_TOK_JUMP ) );
	BOOST_CHECK( !BotLearn_IsResponseToken( BOTLEARN_TOK_KNOCKDOWN ) );
	BOOST_CHECK( !BotLearn_IsResponseToken( BOTLEARN_TOK_COUNT ) );
}

BOOST_AUTO_TEST_CASE( buckets_and_context_keys )
{
	BOOST_CHECK_EQUAL( BotLearn_HealthArmorBucket( 40 ), 0 );
	BOOST_CHECK_EQUAL( BotLearn_HealthArmorBucket( 200 ), 3 );
	BOOST_CHECK_EQUAL( BotLearn_ForceBucket( 10 ), 0 );
	BOOST_CHECK_EQUAL( BotLearn_ForceBucket( 100 ), 3 );
	BOOST_CHECK_EQUAL( BotLearn_RangeBucket( 100.0f ), 0 );
	BOOST_CHECK_EQUAL( BotLearn_RangeBucket( 200.0f ), 1 );
	BOOST_CHECK_EQUAL( BotLearn_RangeBucket( 500.0f ), 2 );

	const int key = BotLearn_ContextKey( 150, 30, 100, 10, 1, 4, 2 );
	BOOST_CHECK_EQUAL( key & 3, 3 );
	BOOST_CHECK_EQUAL( ( key >> 2 ) & 3, 0 );
	BOOST_CHECK_EQUAL( ( key >> 4 ) & 3, 3 );
	BOOST_CHECK_EQUAL( ( key >> 6 ) & 3, 0 );
	BOOST_CHECK_EQUAL( ( key >> 8 ) & 3, 1 );
	// The coarse key drops health but preserves force, range and stance.
	const int coarse = BotLearn_CoarseKey( key );
	BOOST_CHECK( coarse & BOTLEARN_COARSE_KEY_FLAG );
	BOOST_CHECK_EQUAL( coarse & 0x3F, ( key >> 4 ) & 0x3F );
	BOOST_CHECK_EQUAL( BotLearn_CoarseKey( BotLearn_ContextKey( 10, 10, 100, 10, 1, 4, 2 ) ), coarse );
	BOOST_CHECK_NE( BotLearn_CoarseKey( BotLearn_ContextKey( 10, 10, 100, 10, 1, 0, 0 ) ), coarse );
}

BOOST_AUTO_TEST_CASE( skill_bands_split_humans_and_bot_levels )
{
	BOOST_CHECK_EQUAL( BotLearn_SkillBand( 0, 9 ), 0 );
	BOOST_CHECK_EQUAL( BotLearn_SkillBand( 1, 2 ), 1 );
	BOOST_CHECK_EQUAL( BotLearn_SkillBand( 1, 5 ), 2 );
	BOOST_CHECK_EQUAL( BotLearn_SkillBand( 1, 7 ), 3 );
	BOOST_CHECK_EQUAL( BotLearn_SkillBand( 1, 10 ), 4 );
}

BOOST_AUTO_TEST_CASE( collapse_merges_held_powers_but_keeps_swings )
{
	botlearn_event_t events[] = {
		MakeEvent( 0, 1, BOTLEARN_TOK_DRAIN ),
		MakeEvent( 100, 1, BOTLEARN_TOK_DRAIN ),
		MakeEvent( 300, 1, BOTLEARN_TOK_DRAIN ),
		MakeEvent( 350, 0, BOTLEARN_TOK_SWING ),
		MakeEvent( 500, 0, BOTLEARN_TOK_SWING ),
		MakeEvent( 1200, 1, BOTLEARN_TOK_DRAIN ),
	};
	const int count = BotLearn_CollapseRepeats( events, 6 );
	BOOST_CHECK_EQUAL( count, 4 );
	BOOST_CHECK_EQUAL( events[0].token, BOTLEARN_TOK_DRAIN );
	BOOST_CHECK_EQUAL( events[1].token, BOTLEARN_TOK_SWING );
	BOOST_CHECK_EQUAL( events[2].token, BOTLEARN_TOK_SWING );
	BOOST_CHECK_EQUAL( events[3].time, 1200 );
}

BOOST_AUTO_TEST_CASE( extracts_response_followups_and_outcome )
{
	// Opponent drains, we pull 66ms later and kick, then deal 30 and take 10.
	botlearn_event_t events[] = {
		MakeEvent( 1000, 1, BOTLEARN_TOK_DRAIN ),
		MakeEvent( 1066, 0, BOTLEARN_TOK_PULL ),
		MakeEvent( 1300, 0, BOTLEARN_TOK_KICK ),
		MakeEvent( 1400, 1, BOTLEARN_TOK_KNOCKDOWN ),
		MakeEvent( 1500, 0, BOTLEARN_TOK_NONE, 30 ),
		MakeEvent( 1900, 1, BOTLEARN_TOK_NONE, 10 ),
		MakeEvent( 4000, 1, BOTLEARN_TOK_NONE, 50 ),
	};
	botlearn_sequence_t out[8];
	const int n = BotLearn_ExtractSequences( events, 7, 1, out, 8 );

	BOOST_REQUIRE_EQUAL( n, 1 );
	BOOST_CHECK_EQUAL( out[0].stimulus, BOTLEARN_TOK_DRAIN );
	BOOST_CHECK_EQUAL( out[0].response, BOTLEARN_TOK_PULL );
	BOOST_CHECK_EQUAL( out[0].follow1, BOTLEARN_TOK_KICK );
	BOOST_CHECK_EQUAL( out[0].follow2, BOTLEARN_TOK_NONE );
	BOOST_CHECK_EQUAL( out[0].responseDelayMs, 66 );
	// Only damage inside the 2s outcome window counts.
	BOOST_CHECK_EQUAL( out[0].netDamage, 20 );
	BOOST_CHECK_EQUAL( out[0].won, 1 );
}

BOOST_AUTO_TEST_CASE( own_initiative_is_idle_stimulus )
{
	botlearn_event_t events[] = {
		MakeEvent( 0, 1, BOTLEARN_TOK_PUSH ),
		MakeEvent( 2000, 0, BOTLEARN_TOK_THROW ),
		MakeEvent( 2066, 0, BOTLEARN_TOK_PULL ),
	};
	botlearn_sequence_t out[4];
	const int n = BotLearn_ExtractSequences( events, 3, 0, out, 4 );

	// The pull is a follow-up of our own throw, not a separate response.
	BOOST_REQUIRE_EQUAL( n, 1 );
	BOOST_CHECK_EQUAL( out[0].stimulus, BOTLEARN_TOK_IDLE );
	BOOST_CHECK_EQUAL( out[0].response, BOTLEARN_TOK_THROW );
	BOOST_CHECK_EQUAL( out[0].follow1, BOTLEARN_TOK_PULL );
	BOOST_CHECK_EQUAL( out[0].won, 0 );
}

BOOST_AUTO_TEST_CASE( score_is_confidence_weighted )
{
	BOOST_CHECK_EQUAL( BotLearn_Score( 0, 0, 0 ), 0.0f );
	// Same average, more samples -> score closer to the raw average.
	const float few = BotLearn_Score( 2, 2, 40 );
	const float many = BotLearn_Score( 40, 40, 800 );
	BOOST_CHECK( many > few );
	BOOST_CHECK( BotLearn_Score( 40, 0, -800 ) < 0.0f );
}

BOOST_AUTO_TEST_CASE( score_is_net_damage_first_and_win_rate_relative_to_source )
{
	// A human row that won at the usual human rate scores on net damage alone.
	const float humanBaseline = BotLearn_ScoreRelative( 40.0f,
		BotLearn_ExcessWins( 40.0f, 32.0f, 0.8f ), 0.0f );
	BOOST_CHECK_SMALL( humanBaseline, 0.001f );
	// Winning 80% against a 50% baseline would have looked strongly positive.
	BOOST_CHECK( BotLearn_Score( 40, 32, 0 ) > 1.0f );
	// Net damage dominates the win term: losing damage with a slightly better win rate is negative.
	BOOST_CHECK( BotLearn_ScoreRelative( 40.0f, BotLearn_ExcessWins( 40.0f, 34.0f, 0.8f ), -400.0f ) < 0.0f );
	BOOST_CHECK_CLOSE( BotLearn_SourceWeight( BOTLEARN_SOURCE_HUMAN ), 1.0f, 0.001f );
	BOOST_CHECK( BotLearn_SourceWeight( BOTLEARN_SOURCE_BOT ) < 0.5f );
}

BOOST_AUTO_TEST_CASE( weight_bonus_needs_many_samples_to_reach_cap )
{
	// Same score: a thin context stays well under the cap a well-sampled one reaches.
	BOOST_CHECK( BotLearn_WeightBonus( 20.0f, 6, 6, 1.0f, 10.0f ) < BOTLEARN_BONUS_CAP / 2 );
	BOOST_CHECK_EQUAL( BotLearn_WeightBonus( 20.0f, 200, 6, 1.0f, 10.0f ), BOTLEARN_BONUS_CAP );
	BOOST_CHECK( BotLearn_WeightBonus( 20.0f, 60, 6, 1.0f, 10.0f ) >
		BotLearn_WeightBonus( 20.0f, 10, 6, 1.0f, 10.0f ) );
}

BOOST_AUTO_TEST_CASE( weight_bonus_needs_samples_and_is_capped )
{
	BOOST_CHECK_EQUAL( BotLearn_WeightBonus( 50.0f, 3, 6, 1.0f, 10.0f ), 0 );
	BOOST_CHECK_EQUAL( BotLearn_WeightBonus( 50.0f, 20, 6, 0.0f, 10.0f ), 0 );
	BOOST_CHECK_EQUAL( BotLearn_WeightBonus( 50.0f, 20, 6, 1.0f, 10.0f ), BOTLEARN_BONUS_CAP );
	BOOST_CHECK_EQUAL( BotLearn_WeightBonus( -50.0f, 20, 6, 1.0f, 10.0f ), -BOTLEARN_BONUS_CAP );
	// 20 samples is half confidence (BOTLEARN_CONFIDENCE_SAMPLES).
	BOOST_CHECK_EQUAL( BotLearn_WeightBonus( 5.0f, 20, 6, 1.0f, 7.0f ), 5 );
	// Lower skills lean on the data less and sample more widely.
	BOOST_CHECK( BotLearn_WeightBonus( 5.0f, 20, 6, 1.0f, 1.0f ) < 5 );
	BOOST_CHECK_EQUAL( BotLearn_SampleNoise( 7.0f ), 0 );
	BOOST_CHECK( BotLearn_SampleNoise( 2.0f ) > 0 );
}


BOOST_AUTO_TEST_CASE( similar_contexts_pool_neighbouring_buckets )
{
	const int key = BotLearn_ContextKey( 100, 60, 60, 30, 0, 2, 2 );

	BOOST_CHECK_EQUAL( BotLearn_NeighborKey( key, 0, 0, 0, 0 ), key );
	// One HP+armor step up for self lands on the 126+ bucket context.
	BOOST_CHECK_EQUAL( BotLearn_NeighborKey( key, 1, 0, 0, 0 ), BotLearn_ContextKey( 150, 60, 60, 30, 0, 2, 2 ) );
	// Leaving the bucket range is rejected; coarse keys are never neighbours.
	BOOST_CHECK_EQUAL( BotLearn_NeighborKey( BotLearn_ContextKey( 20, 60, 60, 30, 0, 2, 2 ), -1, 0, 0, 0 ), -1 );
	BOOST_CHECK_EQUAL( BotLearn_NeighborKey( BotLearn_CoarseKey( key ), 0, 0, 0, 0 ), -1 );
	// Range and stance bits are untouched.
	BOOST_CHECK_EQUAL( BotLearn_NeighborKey( key, 0, 1, -1, 1 ) & 0x3F00, key & 0x3F00 );
	BOOST_CHECK_CLOSE( BotLearn_NeighborWeight( 0 ), 1.0f, 0.001f );
	BOOST_CHECK_CLOSE( BotLearn_NeighborWeight( 1 ), 0.5f, 0.001f );
	BOOST_CHECK_CLOSE( BotLearn_NeighborWeight( 2 ), 0.25f, 0.001f );
	BOOST_CHECK_EQUAL( BotLearn_NeighborWeight( 3 ), 0.0f );
}

BOOST_AUTO_TEST_CASE( safety_dimensions_survive_all_cache_lookup_keys )
{
	const int base = BotLearn_ContextKey(100, 100, 60, 60, 1, 2, 2);
	const int saber = BotLearn_ContextSafety(base, 0, 0, 0, 0, 0, 0, 0);
	const int force = BotLearn_ContextSafety(base, 1, 0, 0, 0, 0, 0, 0);
	const int brokenEnemy = BotLearn_ContextSafety(base, 1, 0, BOTLEARN_DEFENSE_BROKEN, 0, 0, 0, 0);
	const int returningEnemy = BotLearn_ContextSafety(base, 1, 0, 0, 0, 1, 0, 0);
	const int airborne = BotLearn_ContextSafety(base, 1, 0, 0, 0, 0, 1, 0);
	BOOST_CHECK_NE(saber, force);
	BOOST_CHECK_NE(BotLearn_CoarseKey(saber), BotLearn_CoarseKey(force));
	BOOST_CHECK_NE(BotLearn_CoarseKey(force), BotLearn_CoarseKey(brokenEnemy));
	BOOST_CHECK_NE(BotLearn_CoarseKey(force), BotLearn_CoarseKey(returningEnemy));
	BOOST_CHECK_NE(BotLearn_CoarseKey(force), BotLearn_CoarseKey(airborne));
	BOOST_CHECK_EQUAL(BotLearn_NeighborKey(brokenEnemy, -1, 1, 0, 0) &
		BOTLEARN_CONTEXT_SAFETY_MASK, brokenEnemy & BOTLEARN_CONTEXT_SAFETY_MASK);
	BOOST_CHECK(saber & BOTLEARN_CONTEXT_VERSION_FLAG);
	BOOST_CHECK_NE(BotLearn_CoarseKey(base), BotLearn_CoarseKey(saber));
}

BOOST_AUTO_TEST_CASE( extracts_verified_context_and_action_window_provenance )
{
	botlearn_event_t events[] = {
		MakeEvent(1000, 1, BOTLEARN_TOK_THROW),
		MakeEvent(1200, 0, BOTLEARN_TOK_SWING),
		MakeEvent(1300, 0, BOTLEARN_TOK_NONE, 40),
	};
	events[1].mode = 1;
	events[1].enemyDefense = BOTLEARN_DEFENSE_LOST;
	events[1].enemyRecovery = 1;
	events[1].selfAir = 1;
	events[1].actionIndex = 70001;
	events[1].selfFooting = BOTLEARN_FOOTING_RETREAT;
	botlearn_sequence_t sequences[4] = {};
	BOOST_REQUIRE_EQUAL(BotLearn_ExtractSequences(events, 3, 1, sequences, 4), 1);
	BOOST_CHECK_EQUAL((sequences[0].contextKey >> 16) & 3, 1);
	BOOST_CHECK_EQUAL((sequences[0].contextKey >> 21) & 7, BOTLEARN_DEFENSE_LOST);
	BOOST_CHECK_EQUAL((sequences[0].contextKey >> 25) & 1, 1);
	BOOST_CHECK_EQUAL((sequences[0].contextKey >> 26) & 1, 1);
	BOOST_CHECK_EQUAL(sequences[0].actionIndex, 70001);
	BOOST_CHECK_EQUAL(sequences[0].startTime, 1200);
	BOOST_CHECK_EQUAL(sequences[0].endTime, 3200);
	BOOST_CHECK_EQUAL(sequences[0].netDamage, 40);
	BOOST_CHECK_EQUAL(sequences[0].selfFooting, BOTLEARN_FOOTING_RETREAT);
	BOOST_CHECK_EQUAL(BotLearn_FootingFromKey(sequences[0].contextKey), BOTLEARN_FOOTING_RETREAT);
}

BOOST_AUTO_TEST_CASE( commentary_and_decision_rows_are_not_combat_responses )
{
	botlearn_event_t events[] = {
		MakeEvent(1000, 0, BOTLEARN_TOK_NONE),
		MakeEvent(1100, 1, BOTLEARN_TOK_NONE),
	};
	botlearn_sequence_t sequences[4] = {};
	BOOST_CHECK_EQUAL(BotLearn_ExtractSequences(events, 2, 1, sequences, 4), 0);
	BOOST_CHECK_EQUAL(BotLearn_Score(0, 1, 100), 0.0f);
}

BOOST_AUTO_TEST_CASE(saber_defense_states_are_distinct_from_normal_swing_recovery)
{
	BOOST_CHECK_EQUAL(BotLearn_DefenseState(0, 0, 0, 0, 0), BOTLEARN_DEFENSE_NONE);
	BOOST_CHECK_EQUAL(BotLearn_DefenseState(0, 0, 0, 0, 1), BOTLEARN_DEFENSE_PARRY);
	BOOST_CHECK_EQUAL(BotLearn_DefenseState(0, 0, 0, 1, 1), BOTLEARN_DEFENSE_BOUNCE);
	BOOST_CHECK_EQUAL(BotLearn_DefenseState(0, 0, 1, 1, 1), BOTLEARN_DEFENSE_BROKEN);
	BOOST_CHECK_EQUAL(BotLearn_DefenseState(0, 1, 1, 1, 1), BOTLEARN_DEFENSE_KNOCKDOWN);
	BOOST_CHECK_EQUAL(BotLearn_DefenseState(1, 1, 1, 1, 1), BOTLEARN_DEFENSE_LOST);
	const int base = BotLearn_ContextKey(100, 100, 60, 60, 1, 2, 2);
	for (int self = 0; self <= BOTLEARN_DEFENSE_UNKNOWN; self++)
		for (int enemy = 0; enemy <= BOTLEARN_DEFENSE_UNKNOWN; enemy++)
		{
			const int key = BotLearn_ContextSafety(base, 0, self, enemy, 0, 0, 0, 0);
			BOOST_CHECK_EQUAL((BotLearn_CoarseKey(key) >> 18) & 7, self);
			BOOST_CHECK_EQUAL((BotLearn_CoarseKey(key) >> 21) & 7, enemy);
			BOOST_CHECK_EQUAL(BotLearn_NeighborKey(key, -1, 1, 0, 0) &
				BOTLEARN_CONTEXT_SAFETY_MASK, key & BOTLEARN_CONTEXT_SAFETY_MASK);
		}
	const int returning = BotLearn_ContextSafety(base, 0, 0, 0, 1, 0, 0, 0);
	const int broken = BotLearn_ContextSafety(base, 0, BOTLEARN_DEFENSE_BROKEN, 0, 0, 0, 0, 0);
	BOOST_CHECK_NE(BotLearn_CoarseKey(returning), BotLearn_CoarseKey(broken));
}

BOOST_AUTO_TEST_CASE(broken_defense_coaching_does_not_infer_break_from_ordinary_parry)
{
	BOOST_CHECK(BotLearn_IsBrokenOrBouncedDefense(BOTLEARN_DEFENSE_BROKEN));
	BOOST_CHECK(BotLearn_IsBrokenOrBouncedDefense(BOTLEARN_DEFENSE_BOUNCE));
	BOOST_CHECK(!BotLearn_IsBrokenOrBouncedDefense(BOTLEARN_DEFENSE_NONE));
	BOOST_CHECK(!BotLearn_IsBrokenOrBouncedDefense(BOTLEARN_DEFENSE_PARRY));
	BOOST_CHECK(!BotLearn_IsBrokenOrBouncedDefense(BOTLEARN_DEFENSE_KNOCKDOWN));
	BOOST_CHECK(!BotLearn_IsBrokenOrBouncedDefense(BOTLEARN_DEFENSE_LOST));
	BOOST_CHECK(!BotLearn_IsBrokenOrBouncedDefense(BOTLEARN_DEFENSE_UNKNOWN));
}

BOOST_AUTO_TEST_CASE(physical_saber_loss_requires_every_validated_drop_signal)
{
	BOOST_CHECK(BotLearn_ValidatedDroppedBlade(1, 1, 1, 1, 1, 1, 1));
	for (int absent = 0; absent < 7; absent++)
	{
		int signal[7] = { 1, 1, 1, 1, 1, 1, 1 };
		signal[absent] = 0;
		BOOST_CHECK(!BotLearn_ValidatedDroppedBlade(signal[0], signal[1], signal[2], signal[3],
			signal[4], signal[5], signal[6]));
	}
}

BOOST_AUTO_TEST_CASE(capture_defense_categories_match_the_combat_classifier)
{
	for (int flags = 0; flags < 32; flags++)
	{
		const int broken = flags & 1, bounce = flags & 2, parry = flags & 4;
		const int knockedDown = flags & 8, lost = flags & 16;
		BOOST_CHECK_EQUAL(BotLearn_DefenseState(lost, knockedDown, broken, bounce, parry),
			NewBotAI_SaberDefenseState(broken, bounce, parry, knockedDown, lost));
	}
}

BOOST_AUTO_TEST_CASE(grounded_footing_uses_own_horizontal_velocity_toward_the_enemy)
{
	BOOST_CHECK_EQUAL(BotLearn_FootingCategory(0, 0, 100, 0, 1), BOTLEARN_FOOTING_STILL);
	BOOST_CHECK_EQUAL(BotLearn_FootingCategory(39, 0, 100, 0, 1), BOTLEARN_FOOTING_STILL);
	BOOST_CHECK_EQUAL(BotLearn_FootingCategory(40, 0, 100, 0, 1), BOTLEARN_FOOTING_ADVANCE);
	BOOST_CHECK_EQUAL(BotLearn_FootingCategory(-40, 0, 100, 0, 1), BOTLEARN_FOOTING_RETREAT);
	BOOST_CHECK_EQUAL(BotLearn_FootingCategory(0, 100, 100, 0, 1), BOTLEARN_FOOTING_LATERAL);
	BOOST_CHECK_EQUAL(BotLearn_FootingCategory(100, 0, -100, 0, 1), BOTLEARN_FOOTING_RETREAT);
	BOOST_CHECK_EQUAL(BotLearn_FootingCategory(70, 70, 100, 100, 1), BOTLEARN_FOOTING_ADVANCE);
	BOOST_CHECK_EQUAL(BotLearn_FootingCategory(70, -70, 100, 100, 1), BOTLEARN_FOOTING_LATERAL);
	BOOST_CHECK_EQUAL(BotLearn_FootingCategory(100, 0, 100, 0, 0), BOTLEARN_FOOTING_STILL);
}

BOOST_AUTO_TEST_CASE(footing_never_mixes_across_exact_neighbor_or_coarse_contexts)
{
	const int base = BotLearn_ContextSafety(BotLearn_ContextKey(100, 100, 60, 60, 1, 2, 2),
		1, BOTLEARN_DEFENSE_PARRY, BOTLEARN_DEFENSE_BROKEN, 0, 1, 0, 0);
	int keys[4];
	for (int footing = 0; footing < 4; footing++)
	{
		keys[footing] = BotLearn_ContextFooting(base, footing);
		BOOST_CHECK(keys[footing] > 0);
		BOOST_CHECK_EQUAL(BotLearn_FootingFromKey(keys[footing]), footing);
		BOOST_CHECK_EQUAL(BotLearn_FootingFromKey(BotLearn_CoarseKey(keys[footing])), footing);
		BOOST_CHECK_EQUAL(BotLearn_FootingFromKey(BotLearn_NeighborKey(keys[footing], -1, 1, 0, 0)), footing);
		BOOST_CHECK_EQUAL(keys[footing] & 0x3C00, base & 0x3C00);
		BOOST_CHECK(keys[footing] & BOTLEARN_FOOTING_CONTEXT_FLAG);
		BOOST_CHECK_NE(BotLearn_CoarseKey(keys[footing]), BotLearn_CoarseKey(base));
		for (int previous = 0; previous < footing; previous++)
		{
			BOOST_CHECK_NE(keys[footing], keys[previous]);
			BOOST_CHECK_NE(BotLearn_CoarseKey(keys[footing]), BotLearn_CoarseKey(keys[previous]));
			BOOST_CHECK_NE(BotLearn_NeighborKey(keys[footing], -1, 1, 0, 0),
				BotLearn_NeighborKey(keys[previous], -1, 1, 0, 0));
		}
	}
	BOOST_CHECK_EQUAL(BotLearn_ContextFooting(base, -1), -1);
	BOOST_CHECK_EQUAL(BotLearn_ContextFooting(base, 4), -1);
	BOOST_CHECK_EQUAL(BotLearn_FootingFromKey(base), -1);
	botlearn_event_t unknown = MakeEvent(1000, 0, BOTLEARN_TOK_SWING);
	unknown.selfFooting = -1;
	botlearn_sequence_t sequence = {};
	BOOST_CHECK_EQUAL(BotLearn_ExtractSequences(&unknown, 1, 1, &sequence, 1), 0);
}

BOOST_AUTO_TEST_SUITE_END()
