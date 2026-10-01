#include <math.h>

#include "ai_combat_tuning.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE( bot_ai )

BOOST_AUTO_TEST_SUITE( tuning )

BOOST_AUTO_TEST_CASE( ptk_armor_penalty_tracks_force_lead )
{
	BOOST_CHECK_EQUAL( NewBotAI_AdjustPTKWeightForArmor( 50, 0, 0 ), 50 );
	BOOST_CHECK_EQUAL( NewBotAI_AdjustPTKWeightForArmor( 50, 25, -5 ), -10 );
	BOOST_CHECK_EQUAL( NewBotAI_AdjustPTKWeightForArmor( 50, 25, 10 ), 15 );
	BOOST_CHECK_EQUAL( NewBotAI_AdjustPTKWeightForArmor( 50, 25, 25 ), 60 );
}

BOOST_AUTO_TEST_CASE( drainlock_force_choice_prefers_pull_when_it_matches_or_beats_drain )
{
	BOOST_CHECK_EQUAL(
		NewBotAI_GetDrainlockForceChoice( { 0, 1, 0, 70, 70, 40 } ),
		NEWBOTAI_DRAINLOCK_FORCE_PULL );
	BOOST_CHECK_EQUAL(
		NewBotAI_GetDrainlockForceChoice( { 0, 0, 1, 80, 80, 10 } ),
		NEWBOTAI_DRAINLOCK_FORCE_PULL );
}

BOOST_AUTO_TEST_CASE( drainlock_force_choice_falls_back_to_drain_when_pull_loses )
{
	BOOST_CHECK_EQUAL(
		NewBotAI_GetDrainlockForceChoice( { 0, 1, 0, 90, 60, 40 } ),
		NEWBOTAI_DRAINLOCK_FORCE_DRAIN );
	BOOST_CHECK_EQUAL(
		NewBotAI_GetDrainlockForceChoice( { 0, 0, 1, 90, 60, 40 } ),
		NEWBOTAI_DRAINLOCK_FORCE_DRAIN );
	BOOST_CHECK_EQUAL(
		NewBotAI_GetDrainlockForceChoice( { 0, 0, 1, 90, 80, 10 } ),
		NEWBOTAI_DRAINLOCK_FORCE_DRAIN );
}

BOOST_AUTO_TEST_CASE( saber_throw_immediate_hop_thresholds_match_tuning )
{
	BOOST_CHECK( NewBotAI_ShouldForceImmediateSaberThrowHop( 55.0f, 40.0f, 0, 3.0f ) );
	BOOST_CHECK( !NewBotAI_ShouldForceImmediateSaberThrowHop( 75.0f, 20.0f, 1, 3.0f ) );
	BOOST_CHECK( NewBotAI_ShouldForceImmediateSaberThrowHop( 75.0f, 20.0f, 1, 6.0f ) );
	BOOST_CHECK( !NewBotAI_ShouldForceImmediateSaberThrowHop( 95.0f, 30.0f, 0, 8.0f ) );
}

BOOST_AUTO_TEST_CASE( saber_throw_ptk_bonus_stays_low_until_free_pull_window )
{
	BOOST_CHECK_EQUAL( NewBotAI_GetSaberThrowPTKBonus( 0, 0, 100, 50, 40 ), 10 );
	BOOST_CHECK_EQUAL( NewBotAI_GetSaberThrowPTKBonus( 0, 1, 100, 50, 40 ), 20 );
	BOOST_CHECK_EQUAL( NewBotAI_GetSaberThrowPTKBonus( 1, 0, 100, 60, 20 ), 80 );
	BOOST_CHECK_EQUAL( NewBotAI_GetSaberThrowPTKBonus( 1, 1, 100, 60, 20 ), 120 );
	BOOST_CHECK_EQUAL( NewBotAI_GetSaberThrowPTKBonus( 1, 1, 20, 20, 40 ), 75 );
}

BOOST_AUTO_TEST_CASE( being_pulled_ptk_bonus_stays_heavy_only_in_exposed_window )
{
	BOOST_CHECK_EQUAL( NewBotAI_GetPulledTowardEnemyPTKBonus( 0, 180.0f ), 90 );
	BOOST_CHECK_EQUAL( NewBotAI_GetPulledTowardEnemyPTKBonus( 1, 180.0f ), 140 );
	BOOST_CHECK_EQUAL( NewBotAI_GetPulledTowardEnemyPTKBonus( 1, 240.0f ), 0 );
}

BOOST_AUTO_TEST_CASE( absorb_bait_window_uses_pullkick_spacing_not_immediate_flipkick_spacing )
{
	BOOST_CHECK( NewBotAI_IsAbsorbBaitWindow( 1, 0, 180.0f, 135.0f, 220.0f ) );
	BOOST_CHECK( NewBotAI_IsAbsorbBaitWindow( 0, 1, 200.0f, 135.0f, 220.0f ) );
	BOOST_CHECK( !NewBotAI_IsAbsorbBaitWindow( 1, 0, 120.0f, 135.0f, 220.0f ) );
	BOOST_CHECK( !NewBotAI_IsAbsorbBaitWindow( 0, 0, 180.0f, 135.0f, 220.0f ) );
}

BOOST_AUTO_TEST_CASE( absorb_bias_bonus_scales_with_bias_and_window_state )
{
	BOOST_CHECK_EQUAL( NewBotAI_GetAbsorbBiasBonus( 0, 1, 0, 180.0f, 135.0f, 220.0f ), 30 );
	BOOST_CHECK_EQUAL( NewBotAI_GetAbsorbBiasBonus( 100, 1, 1, 180.0f, 135.0f, 220.0f ), 80 );
	BOOST_CHECK_EQUAL( NewBotAI_GetAbsorbBiasBonus( 50, 0, 0, 180.0f, 135.0f, 220.0f ), 0 );
}

BOOST_AUTO_TEST_CASE( anti_dark_push_and_drain_bonuses_require_advantage_windows )
{
	BOOST_CHECK_EQUAL( NewBotAI_GetAntiDarkPushBonus( 1, 1, 20, 150.0f, 220.0f ), 95 );
	BOOST_CHECK_EQUAL( NewBotAI_GetAntiDarkPushBonus( 1, 0, 20, 150.0f, 220.0f ), 0 );
	BOOST_CHECK_EQUAL( NewBotAI_GetAntiDarkDrainBonus( 1, 1, 15, 1 ), 90 );
	BOOST_CHECK_EQUAL( NewBotAI_GetAntiDarkDrainBonus( 1, 0, 15, 1 ), 0 );
}

BOOST_AUTO_TEST_CASE( login_reminder_cadence_starts_and_repeats_on_interval )
{
	BOOST_CHECK( !NewBotAI_ShouldQueueLoginReminder( 2, 3, 3, 0 ) );
	BOOST_CHECK( NewBotAI_ShouldQueueLoginReminder( 3, 3, 3, 0 ) );
	BOOST_CHECK( !NewBotAI_ShouldQueueLoginReminder( 3, 3, 3, 3 ) );
	BOOST_CHECK( !NewBotAI_ShouldQueueLoginReminder( 4, 3, 3, 0 ) );
	BOOST_CHECK( NewBotAI_ShouldQueueLoginReminder( 6, 3, 3, 3 ) );
	BOOST_CHECK( !NewBotAI_ShouldQueueLoginReminder( 5, 3, 3, 6 ) );
}

BOOST_AUTO_TEST_CASE( login_reminder_state_updates_prevent_repeat_until_next_interval )
{
	int lastPromptDuel = 0;

	BOOST_CHECK( NewBotAI_ShouldQueueLoginReminder( 3, 3, 3, lastPromptDuel ) );
	lastPromptDuel = 3;
	BOOST_CHECK( !NewBotAI_ShouldQueueLoginReminder( 4, 3, 3, lastPromptDuel ) );
	BOOST_CHECK( !NewBotAI_ShouldQueueLoginReminder( 5, 3, 3, lastPromptDuel ) );
	BOOST_CHECK( NewBotAI_ShouldQueueLoginReminder( 6, 3, 3, lastPromptDuel ) );
	lastPromptDuel = 6;
	BOOST_CHECK( !NewBotAI_ShouldQueueLoginReminder( 6, 3, 3, lastPromptDuel ) );
}

BOOST_AUTO_TEST_CASE( immediate_flipkick_contact_widens_yaw_tolerance )
{
	BOOST_CHECK_EQUAL( NewBotAI_GetImmediateFlipkickYawTolerance( 0 ), 35.0f );
	BOOST_CHECK_EQUAL( NewBotAI_GetImmediateFlipkickYawTolerance( 1 ), 60.0f );
}

BOOST_AUTO_TEST_CASE( speed_attack_abort_respects_grip_and_drain_traps )
{
	BOOST_CHECK( NewBotAI_ShouldAbortSpeedAttack( 1, 0, 0, 80, 20, 200.0f, 256.0f ) );
	BOOST_CHECK( NewBotAI_ShouldAbortSpeedAttack( 0, 1, 0, 80, 20, 200.0f, 256.0f ) );
	BOOST_CHECK( NewBotAI_ShouldAbortSpeedAttack( 0, 0, 1, 35, 0, 200.0f, 256.0f ) );
	BOOST_CHECK( !NewBotAI_ShouldAbortSpeedAttack( 0, 0, 1, 60, 20, 300.0f, 256.0f ) );
	BOOST_CHECK( !NewBotAI_ShouldAbortSpeedAttack( 0, 0, 0, 80, 20, 200.0f, 256.0f ) );
}

BOOST_AUTO_TEST_CASE( speed_finisher_window_requires_true_kill_pressure )
{
	BOOST_CHECK( NewBotAI_IsSpeedFinisherWindow( 30, 0 ) );
	BOOST_CHECK( NewBotAI_IsSpeedFinisherWindow( 34, 15 ) );
	BOOST_CHECK( !NewBotAI_IsSpeedFinisherWindow( 45, 4 ) );
	BOOST_CHECK( !NewBotAI_IsSpeedFinisherWindow( 40, 10 ) );
	BOOST_CHECK( !NewBotAI_IsSpeedFinisherWindow( 30, 20 ) );
}

BOOST_AUTO_TEST_CASE( speed_resource_gate_uses_total_health_lead_with_armor )
{
	BOOST_CHECK( NewBotAI_PassesSpeedAttackResourceLeadGate( 80, 30, 35, 40, 0.5f, 90, 60 ) );
	BOOST_CHECK( NewBotAI_PassesSpeedAttackResourceLeadGate( 65, 35, 35, 40, 0.5f, 90, 60 ) );
	BOOST_CHECK( NewBotAI_PassesSpeedAttackResourceLeadGate( 70, 1, 35, 11, 0.5f, 85, 70 ) ); // total 71, lead 25, force lead 15
	BOOST_CHECK( !NewBotAI_PassesSpeedAttackResourceLeadGate( 80, 0, 35, 40, 0.5f, 90, 60 ) );
	BOOST_CHECK( !NewBotAI_PassesSpeedAttackResourceLeadGate( 80, 30, 35, 40, 0.2f, 90, 60 ) );
	BOOST_CHECK( !NewBotAI_PassesSpeedAttackResourceLeadGate( 80, 30, 35, 40, 0.5f, 60, 50 ) );
	BOOST_CHECK( !NewBotAI_PassesSpeedAttackResourceLeadGate( 65, 5, 35, 40, 0.5f, 90, 60 ) );
	BOOST_CHECK( !NewBotAI_PassesSpeedAttackResourceLeadGate( 70, 0, 35, 10, 0.5f, 85, 70 ) ); // total 70 cutoff
	BOOST_CHECK( !NewBotAI_PassesSpeedAttackResourceLeadGate( 70, 1, 35, 12, 0.5f, 85, 70 ) ); // lead 24 cutoff
	BOOST_CHECK( !NewBotAI_PassesSpeedAttackResourceLeadGate( 70, 1, 35, 11, 0.5f, 84, 70 ) ); // force lead 14 cutoff
}

BOOST_AUTO_TEST_CASE( escape_yaw_override_forces_fixed_turn_rate )
{
	BOOST_CHECK_EQUAL( NewBotAI_GetViewAngleAxisFactor( 0.35f, 1, 1 ), 1.0f );
	BOOST_CHECK_EQUAL( NewBotAI_GetViewAngleAxisFactor( 0.35f, 0, 1 ), 0.35f );
	BOOST_CHECK_EQUAL( NewBotAI_GetViewAngleAxisMaxChange( 90.0f, 0.05f, 1, 1 ), NEWBOTAI_TUNING_ESCAPE_YAW_SPEED * 0.05f );
	BOOST_CHECK_EQUAL( NewBotAI_GetViewAngleAxisMaxChange( 90.0f, 0.05f, 1, 0 ), 90.0f );
}

BOOST_AUTO_TEST_CASE( accidental_special_guard_uses_effective_inputs )
{
	BOOST_CHECK( NewBotAI_ShouldBlockOrthogonalSaberSpecialInput( 1, -1, 0, 1, 0, 1 ) );
	BOOST_CHECK_EQUAL( NewBotAI_GetEffectiveMoveInput( 0, -1 ), -1 );
	BOOST_CHECK( !NewBotAI_ShouldBlockOrthogonalSaberSpecialInput( 1, -1, 1, 1, 0, 1 ) );
	BOOST_CHECK( !NewBotAI_ShouldBlockOrthogonalSaberSpecialInput( 1, 0, 0, 1, 0, 1 ) );
}

BOOST_AUTO_TEST_CASE( jump_attack_gate_covers_post_jump_window )
{
	BOOST_CHECK( NewBotAI_IsJumpAttackSuppressionWindowActive( 1000, 1000, 40 ) );
	BOOST_CHECK( !NewBotAI_IsJumpAttackSuppressionWindowActive( 960, 1000, 40 ) );
	BOOST_CHECK( NewBotAI_IsJumpAttackSuppressionWindowActive( 1040, 1000, 40 ) );
	BOOST_CHECK( !NewBotAI_IsJumpAttackSuppressionWindowActive( 959, 1000, 40 ) );
	BOOST_CHECK( !NewBotAI_IsJumpAttackSuppressionWindowActive( 1041, 1000, 40 ) );
}

BOOST_AUTO_TEST_CASE( jump_attack_gate_clears_primary_and_alt_flags )
{
	int doAttack = 1;
	int doAltAttack = 1;
	NewBotAI_ApplyJumpAttackSuppression( 1, &doAttack, &doAltAttack );
	BOOST_CHECK_EQUAL( doAttack, 0 );
	BOOST_CHECK_EQUAL( doAltAttack, 0 );
}

BOOST_AUTO_TEST_CASE( contextual_strafe_frequency_applies_modifiers_and_clamps )
{
	BOOST_CHECK_EQUAL( NewBotAI_GetContextualStrafeFrequency( 60, 1, 1, 0, 0 ), 100 );
	BOOST_CHECK_EQUAL( NewBotAI_GetContextualStrafeFrequency( 60, 0, 0, 1, 1 ), 0 );
	BOOST_CHECK_EQUAL( NewBotAI_GetContextualStrafeFrequency( 40, 0, 1, 1, 0 ), 35 );
}

BOOST_AUTO_TEST_CASE( pull_skip_for_natural_flipkick_respects_range_and_readiness )
{
	BOOST_CHECK( NewBotAI_ShouldSkipPullForNaturalFlipkick( 1, 1, 1, 1, 260.0f, -1.0f ) );
	BOOST_CHECK( NewBotAI_ShouldSkipPullForNaturalFlipkick( 1, 1, 1, 0, 200.0f, 250.0f ) );
	BOOST_CHECK( !NewBotAI_ShouldSkipPullForNaturalFlipkick( 0, 1, 1, 1, 120.0f, 100.0f ) );
	BOOST_CHECK( !NewBotAI_ShouldSkipPullForNaturalFlipkick( 1, 1, 1, 0, 260.0f, 250.0f ) );
	BOOST_CHECK( !NewBotAI_ShouldSkipPullForNaturalFlipkick( 1, 1, 1, 0, 200.0f, 500.0f ) );
}

BOOST_AUTO_TEST_CASE( duel_request_cooldown_is_mode_and_participant_specific )
{
	BOOST_CHECK_EQUAL( NewBotAI_GetDuelRequestCooldownMs( 7000, 120000, 1, 1, 1 ), 120000 );
	BOOST_CHECK_EQUAL( NewBotAI_GetDuelRequestCooldownMs( 7000, 120000, 0, 1, 1 ), 7000 );
	BOOST_CHECK_EQUAL( NewBotAI_GetDuelRequestCooldownMs( 7000, 120000, 1, 0, 1 ), 7000 );
}

BOOST_AUTO_TEST_CASE( bot_saber_loss_guard_respects_cvar )
{
	BOOST_CHECK( NewBotAI_ShouldIgnoreBotSaberLoss( 1, 1 ) );
	BOOST_CHECK( !NewBotAI_ShouldIgnoreBotSaberLoss( 1, 0 ) );
	BOOST_CHECK( !NewBotAI_ShouldIgnoreBotSaberLoss( 0, 1 ) );
}

BOOST_AUTO_TEST_CASE( fan_wobble_activation_respects_fan_attack_delay_and_axes )
{
	BOOST_CHECK( !NewBotAI_ShouldApplyFanWobble( 0, 1, 200, 120, 6.0f, 2.0f, 2.0f ) );
	BOOST_CHECK( !NewBotAI_ShouldApplyFanWobble( 1, 0, 200, 120, 6.0f, 2.0f, 2.0f ) );
	BOOST_CHECK( !NewBotAI_ShouldApplyFanWobble( 1, 1, 100, 120, 6.0f, 2.0f, 2.0f ) );
	BOOST_CHECK( NewBotAI_ShouldApplyFanWobble( 1, 1, 120, 120, 6.0f, 2.0f, 2.0f ) );
	BOOST_CHECK( !NewBotAI_ShouldApplyFanWobble( 1, 1, 200, 120, 0.0f, 0.0f, 2.0f ) );
	BOOST_CHECK( !NewBotAI_ShouldApplyFanWobble( 1, 1, 200, 120, 6.0f, 2.0f, 0.0f ) );
	BOOST_CHECK( NewBotAI_ShouldApplyFanWobble( 1, 1, 200, 120, 6.0f, 0.0f, 2.0f ) );
	BOOST_CHECK( NewBotAI_ShouldApplyFanWobble( 1, 1, 200, 120, 0.0f, 2.0f, 2.0f ) );
}

BOOST_AUTO_TEST_CASE( fan_wobble_offsets_match_phase_and_axis_amplitudes )
{
	float yawOffset = 0.0f;
	float pitchOffset = 0.0f;

	NewBotAI_GetFanWobbleOffsets( 0.0f, 6.0f, 2.0f, 2.0f, &yawOffset, &pitchOffset );
	BOOST_CHECK_CLOSE_FRACTION( yawOffset, 6.0f, 0.0001f );
	BOOST_CHECK_SMALL( pitchOffset, 0.0001f );

	NewBotAI_GetFanWobbleOffsets( 125.0f, 6.0f, 2.0f, 2.0f, &yawOffset, &pitchOffset );
	BOOST_CHECK_SMALL( yawOffset, 0.0001f );
	BOOST_CHECK_CLOSE_FRACTION( pitchOffset, -2.0f, 0.0001f );

	NewBotAI_GetFanWobbleOffsets( 125.0f, 6.0f, 0.0f, 2.0f, &yawOffset, &pitchOffset );
	BOOST_CHECK_SMALL( yawOffset, 0.0001f );
	BOOST_CHECK_SMALL( pitchOffset, 0.0001f );

	NewBotAI_GetFanWobbleOffsets( 300.0f, 6.0f, 2.0f, 2.0f, &yawOffset, &pitchOffset );
	BOOST_CHECK( yawOffset < 0.0f );
}

BOOST_AUTO_TEST_CASE( saber_tactic_mistakes_scale_down_to_zero_at_level_ten )
{
	BOOST_CHECK( NewBotAI_GetSaberTacticMistakeChance( 2, 70 ) >
		NewBotAI_GetSaberTacticMistakeChance( 8, 70 ) );
	BOOST_CHECK_EQUAL( NewBotAI_GetSaberTacticMistakeChance( 10, 100 ), 0 );
}

static newbotai_saber_tactic_context_t MakeSaberDuelContext( float distance )
{
	newbotai_saber_tactic_context_t context = {};
	context.saberOnlyDuel = 1;
	context.skill = 10;
	context.ourTotalHealth = 150;
	context.enemyTotalHealth = 150;
	context.enemyDistance = distance;
	return context;
}

BOOST_AUTO_TEST_CASE( saber_tactic_ranges_match_winner_spacing )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 220.0f );
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_ADVANCE );

	context.enemyDistance = 140.0f;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_STEP_IN );

	context.enemyDistance = 90.0f;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_ATTACK );

	context = MakeSaberDuelContext( 0.0f );
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_ATTACK );
	context.saberOnlyDuel = 0;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_HOLD );
}

BOOST_AUTO_TEST_CASE( saber_tactic_swing_starts_are_limited_to_reach )
{
	BOOST_CHECK( NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_STEP_IN, 120.0f ) );
	BOOST_CHECK( !NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_STEP_IN, 140.0f ) );
	BOOST_CHECK( !NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_ATTACK, 129.0f ) );
	BOOST_CHECK( !NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_ADVANCE, 60.0f ) );
	BOOST_CHECK( !NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_RESET, 60.0f ) );
	BOOST_CHECK( NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_LONG_SWING, 200.0f ) );
	BOOST_CHECK( NewBotAI_SaberTacticHoldsChain( NEWBOTAI_SABER_TACTIC_CHAIN ) );
	BOOST_CHECK( !NewBotAI_SaberTacticHoldsChain( NEWBOTAI_SABER_TACTIC_REPOSITION ) );
}

BOOST_AUTO_TEST_CASE( saber_tactic_continues_chain_after_landed_hit_without_cap )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 90.0f );
	context.selfAttacking = 1;
	context.landedHit = 1;
	context.chainLength = 12;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_CHAIN );

	context.selfAttacking = 0;
	context.enemyDistance = 140.0f;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_STEP_IN );
}

BOOST_AUTO_TEST_CASE( saber_tactic_counters_when_hit_and_retreats_only_when_critical )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 80.0f );
	context.enemyVulnerable = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_COUNTER );

	context.enemyVulnerable = 0;
	context.recentlyHurt = 1;
	context.ourTotalHealth = 60;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_COUNTER );

	context.recentlyHurt = 0;
	context.enemyAttacking = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_COUNTER );

	context.enemyDistance = 150.0f;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_STEP_IN );

	context.enemyDistance = 80.0f;
	context.recentlyHurt = 1;
	context.ourTotalHealth = 25;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_RESET );
}

BOOST_AUTO_TEST_CASE( saber_choice_grade_is_always_correct_at_level_ten )
{
	for ( int roll = 1; roll <= 100; roll++ )
	{
		BOOST_CHECK_EQUAL( NewBotAI_GetSaberChoiceGrade( 10, 100, roll ), NEWBOTAI_SABER_GRADE_CORRECT );
	}
}

BOOST_AUTO_TEST_CASE( saber_choice_grade_tiers_scale_with_skill_and_bias )
{
	int lowErrors = 0, highErrors = 0, lowDeep = 0, highDeep = 0;

	for ( int roll = 1; roll <= 100; roll++ )
	{
		const newbotai_saber_grade_t low = NewBotAI_GetSaberChoiceGrade( 2, 80, roll );
		const newbotai_saber_grade_t high = NewBotAI_GetSaberChoiceGrade( 8, 80, roll );
		lowErrors += ( low != NEWBOTAI_SABER_GRADE_CORRECT );
		highErrors += ( high != NEWBOTAI_SABER_GRADE_CORRECT );
		lowDeep += ( low == NEWBOTAI_SABER_GRADE_BAD || low == NEWBOTAI_SABER_GRADE_MISTAKE );
		highDeep += ( high == NEWBOTAI_SABER_GRADE_BAD || high == NEWBOTAI_SABER_GRADE_MISTAKE );
	}
	BOOST_CHECK( lowErrors > highErrors );
	BOOST_CHECK( lowDeep > highDeep );
	BOOST_CHECK_EQUAL( NewBotAI_GetSaberChoiceGrade( 2, 80, 1 ), NEWBOTAI_SABER_GRADE_MISTAKE );
	BOOST_CHECK( NewBotAI_GetSaberChoiceGrade( 5, 0, 100 ) == NEWBOTAI_SABER_GRADE_CORRECT );
}

BOOST_AUTO_TEST_CASE( saber_choice_grades_map_to_human_outcomes )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 90.0f );

	context.chainLength = 3;
	BOOST_CHECK_EQUAL( NewBotAI_ApplySaberChoiceGrade( context, NEWBOTAI_SABER_TACTIC_CHAIN, NEWBOTAI_SABER_GRADE_GOOD ),
		NEWBOTAI_SABER_TACTIC_REPOSITION );
	context.chainLength = 1;
	BOOST_CHECK_EQUAL( NewBotAI_ApplySaberChoiceGrade( context, NEWBOTAI_SABER_TACTIC_CHAIN, NEWBOTAI_SABER_GRADE_GOOD ),
		NEWBOTAI_SABER_TACTIC_CHAIN );
	BOOST_CHECK_EQUAL( NewBotAI_ApplySaberChoiceGrade( context, NEWBOTAI_SABER_TACTIC_STEP_IN, NEWBOTAI_SABER_GRADE_MEDIOCRE ),
		NEWBOTAI_SABER_TACTIC_STAND_SWING );
	BOOST_CHECK_EQUAL( NewBotAI_ApplySaberChoiceGrade( context, NEWBOTAI_SABER_TACTIC_CHAIN, NEWBOTAI_SABER_GRADE_BAD ),
		NEWBOTAI_SABER_TACTIC_RESET );
	BOOST_CHECK_EQUAL( NewBotAI_ApplySaberChoiceGrade( context, NEWBOTAI_SABER_TACTIC_COUNTER, NEWBOTAI_SABER_GRADE_BAD ),
		NEWBOTAI_SABER_TACTIC_HOLD );
	BOOST_CHECK_EQUAL( NewBotAI_ApplySaberChoiceGrade( context, NEWBOTAI_SABER_TACTIC_ATTACK, NEWBOTAI_SABER_GRADE_MISTAKE ),
		NEWBOTAI_SABER_TACTIC_BACK_SWING );
	context.enemyDistance = 200.0f;
	BOOST_CHECK_EQUAL( NewBotAI_ApplySaberChoiceGrade( context, NEWBOTAI_SABER_TACTIC_ADVANCE, NEWBOTAI_SABER_GRADE_MISTAKE ),
		NEWBOTAI_SABER_TACTIC_LONG_SWING );
	BOOST_CHECK_EQUAL( NewBotAI_ApplySaberChoiceGrade( context, NEWBOTAI_SABER_TACTIC_ADVANCE, NEWBOTAI_SABER_GRADE_CORRECT ),
		NEWBOTAI_SABER_TACTIC_ADVANCE );
}

BOOST_AUTO_TEST_CASE( red_stance_swing_start_and_attack_suppression )
{
	BOOST_CHECK( NewBotAI_ShouldStartRedStanceSwing( 80.0f, 300, 100 ) );
	BOOST_CHECK( NewBotAI_ShouldStartRedStanceSwing( 150.0f, 200, 100 ) );
	BOOST_CHECK( !NewBotAI_ShouldStartRedStanceSwing( 150.0f, 300, 100 ) );
	BOOST_CHECK( !NewBotAI_ShouldStartRedStanceSwing( 80.0f, 0, 20 ) );

	BOOST_CHECK( !NewBotAI_ShouldSuppressSaberAttackForDeficit( -1, 1 ) );
	BOOST_CHECK( NewBotAI_ShouldSuppressSaberAttackForDeficit( -30, 1 ) );
	BOOST_CHECK( !NewBotAI_ShouldSuppressSaberAttackForDeficit( -30, 10 ) );
	BOOST_CHECK( NewBotAI_ShouldSuppressSaberAttackForDeficit( -70, 10 ) );
}

BOOST_AUTO_TEST_CASE( fan_footwork_follows_human_spacing )
{
	BOOST_CHECK( !NewBotAI_IsFanEntrySpacing( 40.0f ) );
	BOOST_CHECK( NewBotAI_IsFanEntrySpacing( 48.0f ) );
	BOOST_CHECK( NewBotAI_IsFanEntrySpacing( 110.0f ) );
	BOOST_CHECK( !NewBotAI_IsFanEntrySpacing( 160.0f ) );

	BOOST_CHECK( !NewBotAI_FanHoldUsesForward( 0, 90.0f ) );
	BOOST_CHECK( NewBotAI_FanHoldUsesForward( 1, 90.0f ) );
	BOOST_CHECK( !NewBotAI_FanHoldUsesForward( 1, 40.0f ) );

	BOOST_CHECK_EQUAL( NewBotAI_ScaleSaberDuelFanBias( 0.0f, 50 ), 0.0f );
	BOOST_CHECK_CLOSE( NewBotAI_ScaleSaberDuelFanBias( 60.0f, 0 ), 60.0f, 0.001f );
	BOOST_CHECK_CLOSE( NewBotAI_ScaleSaberDuelFanBias( 60.0f, -40 ), 36.0f, 0.001f );
	BOOST_CHECK_CLOSE( NewBotAI_ScaleSaberDuelFanBias( 60.0f, -200 ), 21.0f, 0.001f );
	BOOST_CHECK( NewBotAI_ScaleSaberDuelFanBias( 60.0f, -60 ) > 0.0f );
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
