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

BOOST_AUTO_TEST_CASE( saber_duels_always_use_saber_duel_path )
{
	BOOST_CHECK( NewBotAI_UsesSaberDuelPath( 1, 0, 1, 1 ) );
	BOOST_CHECK( NewBotAI_UsesSaberDuelPath( 1, 0, 0, 1 ) );
	BOOST_CHECK( NewBotAI_UsesSaberDuelPath( 0, 163837, 0, 1 ) );
	BOOST_CHECK( NewBotAI_UsesSaberDuelPath( 0, 163839, 0, 1 ) );
	BOOST_CHECK( !NewBotAI_UsesSaberDuelPath( 0, 163837, 1, 1 ) );
	BOOST_CHECK( !NewBotAI_UsesSaberDuelPath( 0, 163837, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_UsesSaberDuelPath( 0, 0, 0, 1 ) );
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

BOOST_AUTO_TEST_CASE( saber_tactic_continues_safe_hits_but_bounds_exposed_pressure )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 90.0f );
	context.selfAttacking = 1;
	context.landedHit = 1;
	context.chainLength = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_CHAIN );
	context.chainLength = 12;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_CHAIN );
	context.landedHit = 0;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_RESET );
	context.landedHit = 1;
	context.chainLength = 2;
	context.enemyAttacking = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_RESET );

	context.enemyAttacking = 0;
	context.chainLength = 0;
	context.selfAttacking = 0;
	context.enemyDistance = 140.0f;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_STEP_IN );
}

BOOST_AUTO_TEST_CASE( saber_tactic_counters_recovery_not_every_incoming_swing )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 80.0f );
	context.enemyVulnerable = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_COUNTER );

	context.enemyVulnerable = 0;
	context.recentlyHurt = 1;
	context.ourTotalHealth = 60;
	context.counterReady = 1;
	context.enemyRecovering = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_COUNTER );

	context.recentlyHurt = 0;
	context.enemyRecovering = 0;
	context.enemyAttacking = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_REPOSITION );

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
		NEWBOTAI_SABER_TACTIC_REPOSITION );
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

BOOST_AUTO_TEST_CASE( human_technique_bias_weights_families_not_primary_attack_permission )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 90.0f );
	int simple = 0, complex = 0;
	for ( int roll = 1; roll <= 100; roll++ )
	{
		BOOST_CHECK_EQUAL( NewBotAI_SelectSaberFamily( context, 0, roll ), NEWBOTAI_SABER_BASIC );
		simple += NewBotAI_SelectSaberFamily( context, 25, roll ) != NEWBOTAI_SABER_BASIC;
		complex += NewBotAI_SelectSaberFamily( context, 75, roll ) != NEWBOTAI_SABER_BASIC;
		const newbotai_saber_command_t command = NewBotAI_PlanSaberCommand( context,
			NEWBOTAI_SABER_TACTIC_ATTACK, NewBotAI_SelectSaberFamily( context, 0, roll ),
			0, 1, 1, 1, 1, 0 );
		BOOST_CHECK_EQUAL( command.attack, 1 );
	}
	BOOST_CHECK_EQUAL( simple, 25 );
	BOOST_CHECK_EQUAL( complex, 75 );
	context.enemyTotalHealth = 30;
	context.landedHit = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberFamily( context, 100, 100 ), NEWBOTAI_SABER_FINISH );
	context.enemyTotalHealth = 150;
	context.recentlyHurt = 1;
	context.counterReady = 1;
	context.enemyRecovering = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberFamily( context, 100, 1 ), NEWBOTAI_SABER_COUNTER_ENTRY );
	context.enemyRecovering = 0;
	context.enemyAttacking = 1;
	context.ourTotalHealth = 100;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberFamily( context, 100, 1 ), NEWBOTAI_SABER_BURST );
}

BOOST_AUTO_TEST_CASE( human_saber_sequence_uses_engine_accepted_attacks_only )
{
	BOOST_CHECK( NewBotAI_SaberMoveAccepted( 10, 9, 1 ) );
	BOOST_CHECK( !NewBotAI_SaberMoveAccepted( 10, 10, 1 ) );
	BOOST_CHECK( !NewBotAI_SaberMoveAccepted( 11, 10, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberBurstComplete( NEWBOTAI_SABER_BURST, 1, 1 ) );
	BOOST_CHECK( NewBotAI_SaberBurstComplete( NEWBOTAI_SABER_BURST, 2, 1 ) );
	BOOST_CHECK( !NewBotAI_SaberBurstComplete( NEWBOTAI_SABER_BURST, 12, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberBurstComplete( NEWBOTAI_SABER_HORIZONTAL, 12, 1 ) );
	BOOST_CHECK( !NewBotAI_SaberBurstComplete( NEWBOTAI_SABER_FINISH, 12, 1 ) );
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 90.0f );
	const newbotai_saber_command_t first = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_ATTACK, NEWBOTAI_SABER_DIAGONAL_VERTICAL, 0, -1, 1, 1, 1, 0 );
	const newbotai_saber_command_t second = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_CHAIN, NEWBOTAI_SABER_DIAGONAL_VERTICAL, 1, 1, 1, 1, 1, 0 );
	const newbotai_saber_command_t finisher = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_CHAIN, NEWBOTAI_SABER_DIAGONAL_VERTICAL, 2, -1, 1, 1, 1, 0 );
	BOOST_CHECK_EQUAL( first.forward, 1 );
	BOOST_CHECK_EQUAL( first.right, -1 );
	BOOST_CHECK_EQUAL( second.forward, 1 );
	BOOST_CHECK_EQUAL( second.right, 0 );
	BOOST_CHECK_EQUAL( finisher.forward, 0 );
	BOOST_CHECK_EQUAL( finisher.right, -1 );
	BOOST_CHECK_EQUAL( finisher.attack, 1 );
	const newbotai_saber_command_t running = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_CHAIN, NEWBOTAI_SABER_HORIZONTAL, 1, 1, 1, 0, 1, 0 );
	BOOST_CHECK_EQUAL( running.forward, 1 );
	BOOST_CHECK_EQUAL( NewBotAI_PlanSaberCommand( context, NEWBOTAI_SABER_TACTIC_ATTACK,
		NEWBOTAI_SABER_HORIZONTAL, 0, 1, 1, 1, 1, 0 ).forward, 0 );
}

BOOST_AUTO_TEST_CASE( human_saber_mix_is_deliberate_and_horizontal_sweeps_remain_primary )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 90.0f );
	int horizontal = 0, mixed = 0;
	for ( int roll = 1; roll <= 100; roll++ )
		BOOST_CHECK_EQUAL( NewBotAI_SelectSaberFamily( context, 100, roll ), NEWBOTAI_SABER_HORIZONTAL );
	context.enemyRecovering = 1;
	for ( int roll = 1; roll <= 100; roll++ )
	{
		const newbotai_saber_family_t family = NewBotAI_SelectSaberFamily( context, 100, roll );
		horizontal += family == NEWBOTAI_SABER_HORIZONTAL;
		mixed += family == NEWBOTAI_SABER_DIAGONAL_VERTICAL;
	}
	BOOST_CHECK_EQUAL( horizontal, 90 );
	BOOST_CHECK_EQUAL( mixed, 10 );
	for ( int family : { NEWBOTAI_SABER_BASIC, NEWBOTAI_SABER_DIAGONAL_VERTICAL } )
	{
		int horizontalStarts = 0, diagonalStarts = 0, verticalStarts = 0;
		for ( int stage = 0; stage < 3; stage++ )
		{
			// Stage is an accepted-attack count, never a timer/held-input counter.
			for ( int preparationFrame = 0; preparationFrame < 5; preparationFrame++ )
			{
				const newbotai_saber_command_t command = NewBotAI_PlanSaberCommand( context,
					NEWBOTAI_SABER_TACTIC_ATTACK, (newbotai_saber_family_t)family,
					stage, -1, 1, 1, 1, 0 );
				BOOST_CHECK_EQUAL( command.attack, 1 );
				if (!preparationFrame)
				{
					horizontalStarts += command.forward == 0 && command.right == -1;
					diagonalStarts += command.forward == 1 && command.right == -1;
					verticalStarts += command.forward == 1 && command.right == 0;
				}
				int forward, right;
				NewBotAI_SaberSelectionInputs((newbotai_saber_family_t)family, stage, -1, &forward, &right);
				BOOST_CHECK_EQUAL( command.forward, forward );
				BOOST_CHECK_EQUAL( command.right, right );
			}
		}
		BOOST_CHECK_EQUAL( horizontalStarts, 1 );
		BOOST_CHECK_EQUAL( diagonalStarts, 1 );
		BOOST_CHECK_EQUAL( verticalStarts, 1 );
	}
	newbotai_saber_command_t vertical = { 1, 0, 0, 1 };
	NewBotAI_SaberGuardSelection( &vertical, 1, 1, 0 );
	BOOST_CHECK_EQUAL( vertical.attack, 1 );
	BOOST_CHECK_EQUAL( vertical.forward, 1 );
	newbotai_saber_command_t suppressedDiagonal = { 1, 0, 0, 1 };
	NewBotAI_SaberGuardSelection( &suppressedDiagonal, 1, 1, -1 );
	BOOST_CHECK_EQUAL( suppressedDiagonal.attack, 0 );
}

BOOST_AUTO_TEST_CASE( slow_red_start_and_transition_keep_lateral_selection_until_active )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 90.0f );
	context.selfAttacking = 1; // Also true during engine starts and transitions.
	for ( int remaining : { 1200, 800, 300, 99, 0 } )
	{
		const newbotai_saber_yaw_phase_t phase =
			NewBotAI_SaberYawPhase( 0, 1, remaining, 1200 );
		BOOST_CHECK_EQUAL( phase, NEWBOTAI_SABER_YAW_PREPARE );
		for ( int family : { NEWBOTAI_SABER_HORIZONTAL, NEWBOTAI_SABER_COUNTER_ENTRY,
			NEWBOTAI_SABER_FINISH, NEWBOTAI_SABER_BURST } )
		{
			const newbotai_saber_command_t command = NewBotAI_PlanSaberCommand( context,
				NEWBOTAI_SABER_TACTIC_CHAIN, (newbotai_saber_family_t)family, 1, -1,
				1, phase != NEWBOTAI_SABER_YAW_ACTIVE, 1, 0 );
			BOOST_CHECK_EQUAL( command.forward, 0 );
			BOOST_CHECK_EQUAL( command.right, -1 );
			BOOST_CHECK_EQUAL( command.attack, 1 );
		}
	}
	BOOST_CHECK_EQUAL( NewBotAI_SaberYawPhase( 1, 0, 1000, 1000 ), NEWBOTAI_SABER_YAW_PREPARE );
	BOOST_CHECK_EQUAL( NewBotAI_SaberYawPhase( 1, 0, 600, 1000 ), NEWBOTAI_SABER_YAW_ACTIVE );
	BOOST_CHECK_EQUAL( NewBotAI_SaberYawPhase( 1, 0, 150, 1000 ), NEWBOTAI_SABER_YAW_NEXT );
	BOOST_CHECK_EQUAL( NewBotAI_SaberYawPhase( 1, 0, 0, 1000 ), NEWBOTAI_SABER_YAW_NEXT );
	BOOST_CHECK_EQUAL( NewBotAI_SaberYawPhase( 0, 0, 500, 800 ), NEWBOTAI_SABER_YAW_RECOVER );
	// A held expired animation never retains forward pressure at selection.
	const newbotai_saber_command_t next = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_CHAIN, NEWBOTAI_SABER_HORIZONTAL, 1, -1, 1,
		NewBotAI_SaberYawPhase( 1, 0, 0, 1000 ) != NEWBOTAI_SABER_YAW_ACTIVE, 1, 0 );
	BOOST_CHECK_EQUAL( next.forward, 0 );
	BOOST_CHECK( NewBotAI_SaberCanApplyPressure( NEWBOTAI_SABER_YAW_ACTIVE, 1, 400 ) );
	BOOST_CHECK( !NewBotAI_SaberCanApplyPressure( NEWBOTAI_SABER_YAW_ACTIVE, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberCanApplyPressure( NEWBOTAI_SABER_YAW_ACTIVE, 0, 400 ) );
	BOOST_CHECK( !NewBotAI_SaberCanApplyPressure( NEWBOTAI_SABER_YAW_PREPARE, 1, 400 ) );
	BOOST_CHECK( !NewBotAI_SaberCanApplyPressure( NEWBOTAI_SABER_YAW_NEXT, -1, 50 ) );
}

BOOST_AUTO_TEST_CASE( actual_horizontal_move_drives_sweep_sign_and_next_preparation )
{
	// Model the engine accepting L2R even if the request was R2L.
	const int accepted = NewBotAI_SaberHorizontalDirection( 5, 5, 8 );
	BOOST_CHECK_EQUAL( accepted, 1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberNextHorizontalDirection( accepted, -1 ), -1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberNextHorizontalDirection( -1, -1 ), 1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberNextHorizontalDirection( 0, -1 ), -1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberHorizontalDirection( 8, 5, 8 ), -1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberHorizontalDirection( 4, 5, 8 ), 0 );
	for ( int direction : { -1, 1 } )
	{
		const float amplitude = NEWBOTAI_SABER_SWEEP_DEGREES;
		BOOST_CHECK_CLOSE( NewBotAI_SaberYawOffset( NEWBOTAI_SABER_YAW_PREPARE,
			0.0f, direction, -direction, 1 ), direction * amplitude, 0.001f );
		BOOST_CHECK_CLOSE( NewBotAI_SaberYawOffset( NEWBOTAI_SABER_YAW_ACTIVE,
			0.15f, direction, -direction, 1 ), direction * amplitude, 0.001f );
		BOOST_CHECK_CLOSE( NewBotAI_SaberYawOffset( NEWBOTAI_SABER_YAW_ACTIVE,
			0.8f, direction, -direction, 1 ), -direction * amplitude, 0.001f );
		BOOST_CHECK_CLOSE( NewBotAI_SaberYawOffset( NEWBOTAI_SABER_YAW_NEXT,
			1.0f, direction, -direction, 1 ), -direction * amplitude, 0.001f );
		BOOST_CHECK_EQUAL( NewBotAI_SaberYawOffset( NEWBOTAI_SABER_YAW_RECOVER,
			1.0f, direction, -direction, 1 ), 0.0f );
		BOOST_CHECK_EQUAL( NewBotAI_SaberYawOffset( NEWBOTAI_SABER_YAW_ACTIVE,
			0.5f, direction, -direction, 0 ), 0.0f );
	}
}

BOOST_AUTO_TEST_CASE( coordinated_saber_yaw_wraps_and_bounds_final_command_rate )
{
	BOOST_CHECK_CLOSE( NewBotAI_SaberApplyYawOffset( 359.0f, 2.0f ), 1.0f, 0.001f );
	BOOST_CHECK_CLOSE( NewBotAI_SaberApplyYawOffset( 1.0f, -2.0f ), 359.0f, 0.001f );
	BOOST_CHECK_CLOSE( NewBotAI_SaberStepYawOffset( 0.0f, 18.0f, 10 ), 2.4f, 0.001f );
	BOOST_CHECK_CLOSE( NewBotAI_SaberStepYawOffset( 18.0f, -18.0f, 1000 ), -6.0f, 0.001f );
	BOOST_CHECK_EQUAL( NewBotAI_SaberStepYawOffset( 10.0f, 18.0f, -1 ), 10.0f );
	BOOST_CHECK_EQUAL( NewBotAI_SaberStepYawOffset( 0.0f, 180.0f, 100 ), 18.0f );
	// Detach last command's offset before normal base aim, then reapply exactly once.
	float yaw = 359.0f;
	for ( int frame = 0; frame < 20; frame++ )
	{
		yaw = NewBotAI_SaberApplyYawOffset( yaw, 18.0f );
		BOOST_CHECK_CLOSE( yaw, 17.0f, 0.001f );
		yaw = NewBotAI_SaberApplyYawOffset( yaw, -18.0f );
		BOOST_CHECK_CLOSE( yaw, 359.0f, 0.001f );
	}
	// Moving the base aim still tracks a moving enemy throughout a held sweep.
	BOOST_CHECK_CLOSE( NewBotAI_SaberApplyYawOffset( 10.0f, 18.0f ), 28.0f, 0.001f );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAnimationProgress( 500, 0 ), 0.0f );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAnimationProgress( 1500, 1000 ), 0.0f );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAnimationProgress( -20, 1000 ), 1.0f );
}

BOOST_AUTO_TEST_CASE( saber_command_ownership_replaces_conflicts_and_preserves_force_actions )
{
	const int movement = 1 | 2 | 4 | 8;
	const int attack = 16;
	const int altAttack = 32;
	const int force = 64;
	const int use = 128;
	BOOST_CHECK_EQUAL( NewBotAI_SaberOwnedActionFlags( movement | altAttack | force | use,
		movement | attack | altAttack, 4 | attack ), force | use | 4 | attack );
	BOOST_CHECK_EQUAL( NewBotAI_SaberOwnedActionFlags( movement | attack | force | use,
		movement | attack | altAttack, 0 ), force | use );
	// Losing ownership removes only our queued primary attack, not a legal
	// force/navigation owner's movement, alternate attack, force or use command.
	BOOST_CHECK_EQUAL( NewBotAI_SaberOwnedActionFlags( movement | attack | altAttack | force | use,
		attack, 0 ), movement | altAttack | force | use );
	BOOST_CHECK_EQUAL( NewBotAI_SaberDuelActionFlags( altAttack | attack | force, altAttack, 1 ), attack | force );
	BOOST_CHECK_EQUAL( NewBotAI_SaberDuelActionFlags( altAttack | force, altAttack, 0 ), altAttack | force );
	BOOST_CHECK( NewBotAI_SaberCanOwnInputs( 1, 1, 0, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberCanOwnInputs( 0, 1, 0, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberCanOwnInputs( 1, 0, 0, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberCanOwnInputs( 1, 1, 1, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberCanOwnInputs( 1, 1, 0, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberCanOwnInputs( 1, 1, 0, 0, 1 ) );
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 90.0f );
	newbotai_saber_command_t command = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_ATTACK, NEWBOTAI_SABER_HORIZONTAL, 0, 1, 1, 1, 1, 0 );
	NewBotAI_SaberSuppressStrafe( &command, 1 );
	BOOST_CHECK_EQUAL( command.right, 0 );
	BOOST_CHECK_EQUAL( command.attack, 1 );
	// If navigation or strafe suppression removes lateral input, do not silently
	// turn the horizontal request into an engine-selected vertical/diagonal.
	NewBotAI_SaberGuardSelection( &command, 1, 0, 1 );
	BOOST_CHECK_EQUAL( command.attack, 0 );
	command = { 0, -1, 0, 1 };
	NewBotAI_SaberGuardSelection( &command, 1, 0, -1 );
	BOOST_CHECK_EQUAL( command.forward, 0 );
	BOOST_CHECK_EQUAL( command.right, -1 );
	BOOST_CHECK_EQUAL( command.attack, 1 );
	command.forward = 1;
	NewBotAI_SaberGuardSelection( &command, 0, 0, -1 );
	BOOST_CHECK_EQUAL( command.forward, 1 );
}

BOOST_AUTO_TEST_CASE( saber_no_strafe_gate_keeps_deliberate_basic_fallback_without_changing_committed_start )
{
	for ( int family : { NEWBOTAI_SABER_BASIC, NEWBOTAI_SABER_HORIZONTAL, NEWBOTAI_SABER_DIAGONAL_VERTICAL } )
	{
		int forward, right;
		NewBotAI_SaberSelectionInputs((newbotai_saber_family_t)family, 0, -1, &forward, &right);
		NewBotAI_SaberNoStrafeFallback( 1, 0, &forward, &right );
		BOOST_CHECK_EQUAL( forward, 1 );
		BOOST_CHECK_EQUAL( right, 0 );
		newbotai_saber_command_t command = { forward, right, 0, 1 };
		NewBotAI_SaberGuardSelection( &command, 1, forward, right );
		BOOST_CHECK_EQUAL( command.attack, 1 );
	}
	int forward = 0, right = 1;
	NewBotAI_SaberNoStrafeFallback( 1, 1, &forward, &right );
	BOOST_CHECK_EQUAL( forward, 0 );
	BOOST_CHECK_EQUAL( right, 1 );
	// The engine's committed start/transition takes precedence over a requested
	// family stage, without counting it as an accepted attack.
	BOOST_CHECK( NewBotAI_SaberPreparationInputs( 6, &forward, &right ) );
	BOOST_CHECK_EQUAL( forward, 1 );
	BOOST_CHECK_EQUAL( right, 0 );
	BOOST_CHECK( NewBotAI_SaberPreparationInputs( 1, &forward, &right ) );
	BOOST_CHECK_EQUAL( forward, 0 );
	BOOST_CHECK_EQUAL( right, 1 );
	BOOST_CHECK( NewBotAI_SaberPreparationInputs( 4, &forward, &right ) );
	BOOST_CHECK_EQUAL( forward, 0 );
	BOOST_CHECK_EQUAL( right, -1 );
	BOOST_CHECK( !NewBotAI_SaberPreparationInputs( 7, &forward, &right ) );
	BOOST_CHECK( !NewBotAI_SaberPreparationInputs( -1, &forward, &right ) );
	BOOST_CHECK( !NewBotAI_SaberMoveAccepted( 6, 5, 0 ) );
	BOOST_CHECK( NewBotAI_SaberOrdinaryAttackSafe( 0, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberOrdinaryAttackSafe( 1, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberOrdinaryAttackSafe( 0, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberOrdinaryAttackSafe( 0, 0, 1 ) );
}

BOOST_AUTO_TEST_CASE( saber_final_command_boundary_replaces_stale_pressure_through_engine_phases )
{
	newbotai_saber_final_context_t finalContext = { 1, 0, 1, 1, 1, 0, 1 };
	newbotai_saber_command_t queued = { 1, -1, 0, 1 };
	for ( int remaining : { 1500, 800, 99 } )
	{
		const newbotai_saber_yaw_phase_t phase = NewBotAI_SaberYawPhase( 0, 1, remaining, 1500 );
		finalContext.selecting = !NewBotAI_SaberCanApplyPressure( phase, 0, remaining );
		BOOST_REQUIRE( NewBotAI_SaberPreparationInputs( 1,
			&finalContext.selectedForward, &finalContext.selectedRight ) );
		const newbotai_saber_command_t command = NewBotAI_FinalizeSaberCommand( queued, finalContext );
		BOOST_CHECK_EQUAL( command.forward, 0 );
		BOOST_CHECK_EQUAL( command.right, 1 );
		BOOST_CHECK_EQUAL( command.attack, 1 );
		BOOST_CHECK( !NewBotAI_SaberMoveAccepted( 63, 1, 0 ) );
	}
	// The actual L2R acceptance, not its requested direction or start timer, advances us.
	BOOST_REQUIRE( NewBotAI_SaberMoveAccepted( 5, 63, 1 ) );
	const int actual = NewBotAI_SaberHorizontalDirection( 5, 5, 8 );
	const int next = NewBotAI_SaberNextHorizontalDirection( actual, 1 );
	finalContext.pressureRight = next;
	finalContext.selecting = !NewBotAI_SaberCanApplyPressure(
		NewBotAI_SaberYawPhase( 1, 0, 600, 1200 ), 1, 600 );
	newbotai_saber_command_t active = NewBotAI_FinalizeSaberCommand( queued, finalContext );
	BOOST_CHECK_EQUAL( active.forward, 1 );
	BOOST_CHECK_EQUAL( active.right, -1 );
	BOOST_CHECK_EQUAL( active.attack, 1 );
	float offset = NEWBOTAI_SABER_SWEEP_DEGREES;
	const float targetOffset = NewBotAI_SaberYawOffset( NEWBOTAI_SABER_YAW_ACTIVE,
		0.5f, actual, next, 1 );
	const float nextOffset = NewBotAI_SaberStepYawOffset( offset, targetOffset, 16 );
	BOOST_CHECK_LE( fabs(nextOffset - offset), NEWBOTAI_SABER_SWEEP_RATE * 0.016f + 0.001f );
	BOOST_CHECK_CLOSE( NewBotAI_SaberApplyYawOffset( 355.0f, nextOffset ),
		NewBotAI_SaberApplyYawOffset( 350.0f, nextOffset ) + 5.0f, 0.001f );
	finalContext.selecting = !NewBotAI_SaberCanApplyPressure(
		NewBotAI_SaberYawPhase( 1, 0, 0, 1200 ), 1, 0 );
	NewBotAI_SaberSelectionInputs( NEWBOTAI_SABER_HORIZONTAL, 1, next,
		&finalContext.selectedForward, &finalContext.selectedRight );
	const newbotai_saber_command_t boundary = NewBotAI_FinalizeSaberCommand( active, finalContext );
	BOOST_CHECK_EQUAL( boundary.forward, 0 );
	BOOST_CHECK_EQUAL( boundary.right, -1 );
	BOOST_CHECK_EQUAL( boundary.attack, 1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberYawOffset( NEWBOTAI_SABER_YAW_NEXT, 1.0f, actual, next, 1 ),
		-NEWBOTAI_SABER_SWEEP_DEGREES );
	BOOST_REQUIRE( NewBotAI_SaberPreparationInputs( 4,
		&finalContext.selectedForward, &finalContext.selectedRight ) );
	const newbotai_saber_command_t transition = NewBotAI_FinalizeSaberCommand( active, finalContext );
	BOOST_CHECK_EQUAL( transition.forward, 0 );
	BOOST_CHECK_EQUAL( transition.right, -1 );
	BOOST_CHECK( !NewBotAI_SaberMoveAccepted( 80, 5, 0 ) );
	BOOST_CHECK_EQUAL( NewBotAI_SaberYawOffset( NEWBOTAI_SABER_YAW_RECOVER, 1.0f, actual, next, 1 ), 0.0f );
}

BOOST_AUTO_TEST_CASE( saber_final_command_boundary_preserves_mix_and_gated_fallback_but_not_unsafe_attacks )
{
	newbotai_saber_final_context_t finalContext = { 1, 0, 1, 1, -1, 0, 1 };
	for ( int family : { NEWBOTAI_SABER_BASIC, NEWBOTAI_SABER_DIAGONAL_VERTICAL } )
		for ( int stage = 0; stage < 3; stage++ )
		{
			NewBotAI_SaberSelectionInputs((newbotai_saber_family_t)family, stage, -1,
				&finalContext.selectedForward, &finalContext.selectedRight );
			const newbotai_saber_command_t command = NewBotAI_FinalizeSaberCommand(
				{ 1, 1, 0, 1 }, finalContext );
			BOOST_CHECK_EQUAL( command.forward, finalContext.selectedForward );
			BOOST_CHECK_EQUAL( command.right, finalContext.selectedRight );
			BOOST_CHECK_EQUAL( command.attack, 1 );
		}
	finalContext.selectedForward = 0;
	finalContext.selectedRight = 1;
	finalContext.strafeSuppressed = 1;
	NewBotAI_SaberNoStrafeFallback( 1, 0,
		&finalContext.selectedForward, &finalContext.selectedRight );
	const newbotai_saber_command_t fallback = NewBotAI_FinalizeSaberCommand( { 0, 1, 0, 1 }, finalContext );
	BOOST_CHECK_EQUAL( fallback.forward, 1 );
	BOOST_CHECK_EQUAL( fallback.right, 0 );
	BOOST_CHECK_EQUAL( fallback.attack, 1 );
	finalContext.attackSafe = 0; // jump/knockdown/duck/rising/DFA guard at the live boundary
	const newbotai_saber_command_t unsafe = NewBotAI_FinalizeSaberCommand( fallback, finalContext );
	BOOST_CHECK_EQUAL( unsafe.attack, 0 );
	finalContext.attackSafe = 1;
	const newbotai_saber_command_t vetoed = NewBotAI_FinalizeSaberCommand( { 0, 0, 0, 1 }, finalContext );
	BOOST_CHECK_EQUAL( vetoed.attack, 0 ); // a collision veto is not a fallback permission
	const int primary = 16, force = 64, use = 128, movement = 1 | 2 | 4 | 8;
	BOOST_CHECK_EQUAL( NewBotAI_SaberOwnedActionFlags( primary | force | use | movement,
		primary | movement, unsafe.attack ? primary : 0 ), force | use );
	BOOST_CHECK_EQUAL( NewBotAI_SaberOwnedActionFlags( primary | force | use | movement,
		primary, 0 ), force | use | movement );
}

BOOST_AUTO_TEST_CASE( saber_final_live_movement_veto_runs_after_pressure_refresh_and_preserves_safe_escape )
{
	newbotai_saber_final_context_t finalContext = { 0, 0, 1, 1, -1, 0, 1 };
	// The think-time lateral plan was safe; the final active phase adds forward
	// pressure and flips side. A live trace of that new command can veto it.
	newbotai_saber_command_t command = NewBotAI_FinalizeSaberCommand( { 0, 1, 0, 1 }, finalContext );
	BOOST_CHECK_EQUAL( command.forward, 1 );
	BOOST_CHECK_EQUAL( command.right, -1 );
	NewBotAI_SaberApplyMovementSafety( &command, 0 );
	BOOST_CHECK_EQUAL( command.forward, 0 );
	BOOST_CHECK_EQUAL( command.right, 0 );
	BOOST_CHECK_EQUAL( command.attack, 0 );
	BOOST_CHECK_EQUAL( command.jump, 0 );
	// No fallback attack may be selected from the now-stationary input.
	const int attack = 16, movement = 1 | 2 | 4 | 8, force = 64;
	BOOST_CHECK_EQUAL( NewBotAI_SaberOwnedActionFlags( attack | movement | force,
		attack | movement, command.attack ? attack : 0 ), force );
	for ( int side : { -1, 1 } )
	{
		command = { -1, side, 1, 0 };
		NewBotAI_SaberApplyMovementSafety( &command, 1 );
		BOOST_CHECK_EQUAL( command.forward, -1 );
		BOOST_CHECK_EQUAL( command.right, side );
		BOOST_CHECK_EQUAL( command.jump, 1 );
		NewBotAI_SaberApplyMovementSafety( &command, 0 );
		BOOST_CHECK_EQUAL( command.forward, 0 );
		BOOST_CHECK_EQUAL( command.right, 0 );
		BOOST_CHECK_EQUAL( command.jump, 0 );
		BOOST_CHECK_EQUAL( command.attack, 0 );
	}
	// Deliberate vertical fallback survives only a safe final path, never a veto.
	command = { 1, 0, 0, 1 };
	NewBotAI_SaberApplyMovementSafety( &command, 1 );
	BOOST_CHECK_EQUAL( command.attack, 1 );
	NewBotAI_SaberApplyMovementSafety( &command, 0 );
	BOOST_CHECK_EQUAL( command.attack, 0 );
}

BOOST_AUTO_TEST_CASE( saber_airborne_footwork_and_bounded_escape_have_deliberate_reentry )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 80.0f );
	const newbotai_saber_command_t airborne = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_ATTACK, NEWBOTAI_SABER_HORIZONTAL, 0, -1, 0, 1, 1, 0 );
	BOOST_CHECK_EQUAL( airborne.attack, 0 );
	BOOST_CHECK_EQUAL( airborne.forward, -1 );
	BOOST_CHECK_EQUAL( airborne.right, -1 );
	const int exitUntil = 1000 + NEWBOTAI_SABER_EXIT_MS;
	const int reentryUntil = exitUntil + NEWBOTAI_SABER_REENTRY_MS;
	BOOST_CHECK_EQUAL( NewBotAI_SaberEscapePhase( 1000, exitUntil, reentryUntil, 0, 0 ), -1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberEscapePhase( exitUntil, exitUntil, reentryUntil, 0, 0 ), 1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberEscapePhase( reentryUntil, exitUntil, reentryUntil, 0, 0 ), 0 );
	const int airExitUntil = 1000 + NEWBOTAI_SABER_AIR_EXIT_MAX_MS;
	BOOST_CHECK_EQUAL( NewBotAI_SaberEscapePhase( exitUntil + 100, exitUntil, reentryUntil, 1, airExitUntil ), -1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberEscapePhase( airExitUntil, exitUntil, airExitUntil + 650, 1, airExitUntil ), 1 );
	const newbotai_saber_command_t airborneExit = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_RESET, NEWBOTAI_SABER_BURST, 2, 1, 0, 1, 1,
		NewBotAI_SaberEscapePhase( exitUntil + 100, exitUntil, reentryUntil, 1, airExitUntil ) );
	BOOST_CHECK_EQUAL( airborneExit.forward, -1 );
	BOOST_CHECK_EQUAL( airborneExit.right, 1 );
	BOOST_CHECK_EQUAL( airborneExit.attack, 0 );
	context.enemyDistance = 220.0f;
	BOOST_CHECK_EQUAL( NewBotAI_PlanSaberCommand( context, NEWBOTAI_SABER_TACTIC_RESET,
		NEWBOTAI_SABER_BURST, 2, 1, 0, 1, 1, -1 ).forward, -1 );
	context.enemyDistance = 80.0f;
	const newbotai_saber_command_t retreat = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_RESET, NEWBOTAI_SABER_BURST, 2, 1, 0, 1, 1, -1 );
	BOOST_CHECK_EQUAL( retreat.forward, -1 );
	BOOST_CHECK_EQUAL( retreat.right, 1 );
	BOOST_CHECK_EQUAL( retreat.attack, 0 );
	context.ourTotalHealth = 25; // Re-entry does not wait for healing.
	context.enemyDistance = 140.0f;
	const newbotai_saber_command_t reentry = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_RESET, NEWBOTAI_SABER_BURST, 0, 1, 1, 1, 1, 1 );
	BOOST_CHECK_EQUAL( reentry.forward, 1 );
	BOOST_CHECK_EQUAL( reentry.attack, 0 );
	context.enemyDistance = 90.0f;
	BOOST_CHECK_EQUAL( NewBotAI_PlanSaberCommand( context, NEWBOTAI_SABER_TACTIC_RESET,
		NEWBOTAI_SABER_BASIC, 0, 1, 1, 1, 1, 1 ).attack, 1 );
	const newbotai_saber_command_t airborneReentry = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_RESET, NEWBOTAI_SABER_BASIC, 0, 1, 0, 1, 1, 1 );
	BOOST_CHECK_EQUAL( airborneReentry.forward, 1 );
	BOOST_CHECK_EQUAL( airborneReentry.attack, 0 );
	context.recentlyHurt = 1;
	const newbotai_saber_command_t unsafeReentry = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_RESET, NEWBOTAI_SABER_BASIC, 0, 1, 1, 1, 1, 1 );
	BOOST_CHECK_EQUAL( unsafeReentry.forward, -1 );
	BOOST_CHECK_EQUAL( unsafeReentry.attack, 0 );
	BOOST_CHECK( NewBotAI_SaberCanEscapeJump( 1, 1, 1, 1, 1, 1 ) );
	for ( int guard = 0; guard < 6; guard++ )
	{
		int ready[6] = { 1, 1, 1, 1, 1, 1 };
		ready[guard] = 0;
		BOOST_CHECK( !NewBotAI_SaberCanEscapeJump( ready[0], ready[1], ready[2],
			ready[3], ready[4], ready[5] ) );
	}
}

BOOST_AUTO_TEST_CASE( saber_grades_do_not_freeze_approach_or_override_necessary_escape )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 140.0f );
	BOOST_CHECK_EQUAL( NewBotAI_ApplySaberChoiceGrade( context, NEWBOTAI_SABER_TACTIC_STEP_IN,
		NEWBOTAI_SABER_GRADE_MEDIOCRE ), NEWBOTAI_SABER_TACTIC_STEP_IN );
	BOOST_CHECK_EQUAL( NewBotAI_ApplySaberChoiceGrade( context, NEWBOTAI_SABER_TACTIC_ADVANCE,
		NEWBOTAI_SABER_GRADE_BAD ), NEWBOTAI_SABER_TACTIC_STEP_IN );
	context.enemyDistance = 80.0f;
	context.selfBlocked = 1;
	context.skill = 2;
	context.mistakeBias = 100;
	context.mistakeRoll = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_RESET );
	BOOST_CHECK_EQUAL( NewBotAI_PlanSaberCommand( context, NEWBOTAI_SABER_TACTIC_ATTACK,
		NEWBOTAI_SABER_BASIC, 0, 1, 1, 1, 0, 0 ).attack, 0 );
}

BOOST_AUTO_TEST_CASE( general_saber_combat_shares_policy_and_respects_counter_reaction )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 90.0f );
	context.saberOnlyDuel = 0;
	context.saberCombat = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_ATTACK );
	context.recentlyHurt = 1;
	context.enemyRecovering = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_REPOSITION );
	context.counterReady = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_COUNTER );
	context.enemyAttacking = 1;
	context.enemyRecovering = 0;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_REPOSITION );
	context.chainLength = 2;
	context.landedHit = 1;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_RESET );
}

BOOST_AUTO_TEST_CASE( saber_throw_anticipation_excludes_impossible_duel_throws_and_basic_starts )
{
	BOOST_CHECK( NewBotAI_ShouldAnticipateSaberThrow( 0, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_ShouldAnticipateSaberThrow( 1, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_ShouldAnticipateSaberThrow( 1, 0, 1 ) );
	BOOST_CHECK( !NewBotAI_ShouldAnticipateSaberThrow( 0, 0, 1 ) );
	// In-flight sabers bypass anticipation and still use the actual trajectory threat test.
	BOOST_CHECK( !NewBotAI_ShouldAnticipateSaberThrow( 1, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_ShouldAnticipateSaberThrow( 0, 1, 0 ) );
}

BOOST_AUTO_TEST_CASE( saber_primary_legality_accepts_partial_staff_dual_holster )
{
	BOOST_CHECK( NewBotAI_SaberPrimaryBladeAvailable( 0 ) );
	BOOST_CHECK( NewBotAI_SaberPrimaryBladeAvailable( 1 ) );
	BOOST_CHECK( !NewBotAI_SaberPrimaryBladeAvailable( 2 ) );
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 90.0f );
	const newbotai_saber_command_t partial = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_ATTACK, NEWBOTAI_SABER_BASIC, 0, 1, 1, 1,
		NewBotAI_SaberPrimaryBladeAvailable( 1 ), 0 );
	const newbotai_saber_command_t full = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_ATTACK, NEWBOTAI_SABER_BASIC, 0, 1, 1, 1,
		NewBotAI_SaberPrimaryBladeAvailable( 2 ), 0 );
	BOOST_CHECK_EQUAL( partial.attack, 1 );
	BOOST_CHECK_EQUAL( full.attack, 0 );
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
