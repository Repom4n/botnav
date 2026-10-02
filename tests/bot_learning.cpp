#include "g_bot_learning.h"

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
	// The coarse key keeps force and range only and never collides with a fine key.
	const int coarse = BotLearn_CoarseKey( key );
	BOOST_CHECK( coarse & BOTLEARN_COARSE_KEY_FLAG );
	BOOST_CHECK_EQUAL( coarse & 0x3F, ( key >> 4 ) & 0x3F );
	BOOST_CHECK_EQUAL( BotLearn_CoarseKey( BotLearn_ContextKey( 10, 10, 100, 10, 1, 0, 0 ) ), coarse );
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

BOOST_AUTO_TEST_CASE( weight_bonus_needs_samples_and_is_capped )
{
	BOOST_CHECK_EQUAL( BotLearn_WeightBonus( 50.0f, 3, 6, 1.0f, 10.0f ), 0 );
	BOOST_CHECK_EQUAL( BotLearn_WeightBonus( 50.0f, 20, 6, 0.0f, 10.0f ), 0 );
	BOOST_CHECK_EQUAL( BotLearn_WeightBonus( 50.0f, 20, 6, 1.0f, 10.0f ), BOTLEARN_BONUS_CAP );
	BOOST_CHECK_EQUAL( BotLearn_WeightBonus( -50.0f, 20, 6, 1.0f, 10.0f ), -BOTLEARN_BONUS_CAP );
	BOOST_CHECK_EQUAL( BotLearn_WeightBonus( 5.0f, 20, 6, 1.0f, 7.0f ), 10 );
	// Lower skills lean on the data less and sample more widely.
	BOOST_CHECK( BotLearn_WeightBonus( 5.0f, 20, 6, 1.0f, 1.0f ) < 10 );
	BOOST_CHECK_EQUAL( BotLearn_SampleNoise( 7.0f ), 0 );
	BOOST_CHECK( BotLearn_SampleNoise( 2.0f ) > 0 );
}

BOOST_AUTO_TEST_SUITE_END()
