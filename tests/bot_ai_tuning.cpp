#include <math.h>

#include "ai_combat_tuning.h"
#include "ai_strafejump.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE( bot_ai )

BOOST_AUTO_TEST_SUITE( tuning )

BOOST_AUTO_TEST_CASE( strafejump_state_machine_has_release_edges )
{
	BOOST_CHECK_EQUAL( BotSFJ_NextPhase( BOT_SFJ_PHASE_PREPARE, 1, 1, 1, 1, 0 ),
		BOT_SFJ_PHASE_PREPARE );
	BOOST_CHECK_EQUAL( BotSFJ_NextPhase( BOT_SFJ_PHASE_PREPARE, 1, 1, 1, 1, 16 ),
		BOT_SFJ_PHASE_TAKEOFF );
	BOOST_CHECK( BotSFJ_JumpPressed( BOT_SFJ_PHASE_TAKEOFF, 1 ) );
	BOOST_CHECK_EQUAL( BotSFJ_NextPhase( BOT_SFJ_PHASE_TAKEOFF, 1, 1, 1, 0, 16 ),
		BOT_SFJ_PHASE_AIR );
	BOOST_CHECK( !BotSFJ_JumpPressed( BOT_SFJ_PHASE_AIR, 0 ) );
	BOOST_CHECK_EQUAL( BotSFJ_NextPhase( BOT_SFJ_PHASE_AIR, 1, 1, 1, 1, 300 ),
		BOT_SFJ_PHASE_LANDING );
	BOOST_CHECK( !BotSFJ_JumpPressed( BOT_SFJ_PHASE_LANDING, 1 ) );
	BOOST_CHECK_EQUAL( BotSFJ_NextPhase( BOT_SFJ_PHASE_LANDING, 1, 1, 1, 1, 16 ),
		BOT_SFJ_PHASE_REJUMP );
}

BOOST_AUTO_TEST_CASE( strafejump_disable_and_conflict_drop_ownership )
{
	BOOST_CHECK( BotSFJ_CanOwnInput( 1, 1, 1, BOT_SFJ_PHASE_AIR ) );
	BOOST_CHECK( !BotSFJ_CanOwnInput( 0, 1, 1, BOT_SFJ_PHASE_AIR ) );
	BOOST_CHECK( !BotSFJ_CanOwnInput( 1, 1, 0, BOT_SFJ_PHASE_AIR ) );
	BOOST_CHECK_EQUAL( BotSFJ_NextPhase( BOT_SFJ_PHASE_AIR, 0, 1, 1, 0, 30 ),
		BOT_SFJ_PHASE_OFF );
	BOOST_CHECK_EQUAL( BotSFJ_NextPhase( BOT_SFJ_PHASE_AIR, 1, 0, 1, 0, 30 ),
		BOT_SFJ_PHASE_ABORT );
}

BOOST_AUTO_TEST_CASE( strafejump_intent_freshness_is_bounded )
{
	BOOST_CHECK( BotSFJ_IntentIsFresh( 1200, 1000 ) );
	BOOST_CHECK( !BotSFJ_IntentIsFresh( 1251, 1000 ) );
	BOOST_CHECK( !BotSFJ_IntentIsFresh( 999, 1000 ) );
}

BOOST_AUTO_TEST_CASE( strafejump_pursuit_uses_start_stop_hysteresis )
{
	BOOST_CHECK( BotSFJ_UpdatePursuitLatch( 0, 540.0f, 500.0f, 200 ) );
	BOOST_CHECK( BotSFJ_UpdatePursuitLatch( 1, 548.0f, 540.0f, 200 ) );
	BOOST_CHECK( !BotSFJ_UpdatePursuitLatch( 1, 550.0f, 548.0f, 200 ) );
	BOOST_CHECK( !BotSFJ_UpdatePursuitLatch( 1, 380.0f, 360.0f, 200 ) );
}

BOOST_AUTO_TEST_CASE( strafejump_yaw_uses_command_dt_and_speed )
{
	const float lowSpeedYaw = BotSFJ_CommandYaw( 0.0f, 20.0f, 0.0f, 250.0f, 1.0f, 0.016f, 1 );
	const float highSpeedShort = BotSFJ_CommandYaw( 0.0f, 600.0f, 0.0f, 250.0f, 1.0f, 0.008f, 1 );
	const float highSpeedLong = BotSFJ_CommandYaw( 0.0f, 600.0f, 0.0f, 250.0f, 1.0f, 0.05f, 1 );

	BOOST_CHECK_SMALL( lowSpeedYaw, 0.001f );
	BOOST_CHECK( highSpeedShort < highSpeedLong );
	BOOST_CHECK( isfinite( BotSFJ_CommandYaw( 90.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.016f, -1 ) ) );
	BOOST_CHECK_EQUAL( BotSFJ_SelectSide( 30.0f, 500.0f, 0.0f, -1 ), 1 );
	BOOST_CHECK_EQUAL( BotSFJ_SelectSide( 330.0f, 500.0f, 0.0f, 1 ), -1 );
}

BOOST_AUTO_TEST_CASE( strafejump_launch_matches_jka_force_jump_substep )
{
	BOOST_CHECK_EQUAL( BotSFJ_JKALaunchVelocity( 0, 0.0f, 1 ), 225.0f );
	BOOST_CHECK_EQUAL( BotSFJ_JKALaunchVelocity( 1, 0.0f, 1 ), 267.0f );
	BOOST_CHECK_EQUAL( BotSFJ_JKALaunchVelocity( 2, 0.0f, 1 ), 284.0f );
	BOOST_CHECK_EQUAL( BotSFJ_JKALaunchVelocity( 3, 0.0f, 1 ), 309.0f );
	BOOST_CHECK_EQUAL( BotSFJ_JKALaunchVelocity( 3, 0.0f, 0 ), 225.0f );
	BOOST_CHECK_EQUAL( BotSFJ_JKALaunchVelocity( 4, 0.0f, 1 ), 225.0f );
}

BOOST_AUTO_TEST_CASE( strafejump_launch_prediction_accounts_for_command_slices )
{
	float shortHeight, shortVelocity, longHeight, longVelocity;

	BotSFJ_PredictReleasedVertical( 3, 1, 1, 800.0f, 8, 8,
		&shortHeight, &shortVelocity );
	BotSFJ_PredictReleasedVertical( 3, 1, 1, 800.0f, 32, 8,
		&longHeight, &longVelocity );
	BOOST_CHECK( longHeight > shortHeight );
	BOOST_CHECK( longVelocity < shortVelocity );
	BOOST_CHECK_EQUAL( BotSFJ_PmoveSliceMsec( 50, 1, 8, 0 ), 8 );
	BOOST_CHECK_EQUAL( BotSFJ_PmoveSliceMsec( 50, 0, 8, 0 ), 50 );
	BOOST_CHECK_EQUAL( BotSFJ_PmoveSliceMsec( 80, 0, 8, 0 ), 66 );
	BOOST_CHECK_EQUAL( BotSFJ_PmoveSliceMsec( 50, 0, 8, 1 ), 8 );
}

BOOST_AUTO_TEST_CASE( strafejump_effective_movement_honors_direction_overrides )
{
	float x, y;

	BOOST_CHECK( BotSFJ_EffectiveMovement( 1.0f, 0.0f, 400.0f, 90.0f,
		0, 0, 0, 0, &x, &y ) > 0.0f );
	BOOST_CHECK_CLOSE_FRACTION( x, 1.0f, 0.0001f );
	BOOST_CHECK_SMALL( y, 0.0001f );
	BOOST_CHECK( BotSFJ_EffectiveMovement( 0.0f, 0.0f, 0.0f, 90.0f,
		1, 127, 0, 0, &x, &y ) > 0.0f );
	BOOST_CHECK_SMALL( x, 0.0001f );
	BOOST_CHECK_CLOSE_FRACTION( y, 1.0f, 0.0001f );
	BOOST_CHECK_SMALL( BotSFJ_EffectiveMovement( 1.0f, 0.0f, 0.0f, 0.0f,
		0, 0, 0, 0, &x, &y ), 0.0001f );
}

BOOST_AUTO_TEST_CASE( strafejump_landing_cannot_bypass_route_endpoint )
{
	BOOST_CHECK( BotSFJ_LandingWithinCorridor( 0.0f, 0.0f, 100.0f, 0.0f,
		80.0f, 20.0f, 24.0f, 0.0f ) );
	BOOST_CHECK( !BotSFJ_LandingWithinCorridor( 0.0f, 0.0f, 100.0f, 0.0f,
		101.0f, 0.0f, 24.0f, 0.0f ) );
	BOOST_CHECK( !BotSFJ_LandingWithinCorridor( 0.0f, 0.0f, 100.0f, 0.0f,
		80.0f, 25.0f, 24.0f, 0.0f ) );
}

BOOST_AUTO_TEST_CASE( strafejump_random_use_does_not_mask_deliberate_use )
{
	BOOST_CHECK( !BotSFJ_UseIsConflict( 0, 1, 1 ) );
	BOOST_CHECK( BotSFJ_UseIsConflict( 0, 1, 0 ) );
	BOOST_CHECK( BotSFJ_UseIsConflict( 1, 0, 0 ) );
}

BOOST_AUTO_TEST_CASE( strafejump_route_safety_rejects_each_hazard_class )
{
	BOOST_CHECK( BotSFJ_RouteSafetyAllows( 1, 1, 1, 0.8f, 0.9f ) );
	BOOST_CHECK( !BotSFJ_RouteSafetyAllows( 0, 1, 1, 0.8f, 0.9f ) );
	BOOST_CHECK( !BotSFJ_RouteSafetyAllows( 1, 0, 1, 0.8f, 0.9f ) );
	BOOST_CHECK( !BotSFJ_RouteSafetyAllows( 1, 1, 0, 0.8f, 0.9f ) );
	BOOST_CHECK( !BotSFJ_RouteSafetyAllows( 1, 1, 1, 0.6f, 0.9f ) );
	BOOST_CHECK( !BotSFJ_RouteSafetyAllows( 1, 1, 1, 0.8f, 0.7f ) );
}

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

	context.enemyDistance = 80.0f;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_ATTACK );

	context.enemyDistance = 90.0f;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_STEP_IN );

	context = MakeSaberDuelContext( 0.0f );
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_ATTACK );
	context.saberOnlyDuel = 0;
	BOOST_CHECK_EQUAL( NewBotAI_SelectSaberTactic( context ), NEWBOTAI_SABER_TACTIC_HOLD );
}

BOOST_AUTO_TEST_CASE( saber_tactic_swing_starts_are_limited_to_reach )
{
	BOOST_CHECK( NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_STEP_IN, 80.0f ) );
	// No 85-100u dead zone: step in and swing together.
	BOOST_CHECK( NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_STEP_IN, 90.0f ) );
	BOOST_CHECK( NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_STEP_IN, 100.0f ) );
	BOOST_CHECK( !NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_STEP_IN, 105.0f ) );
	BOOST_CHECK( !NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_ATTACK, 129.0f ) );
	BOOST_CHECK( !NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_ADVANCE, 60.0f ) );
	BOOST_CHECK( !NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_RESET, 60.0f ) );
	BOOST_CHECK( NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_LONG_SWING, 200.0f ) );
	BOOST_CHECK( NewBotAI_SaberTacticHoldsChain( NEWBOTAI_SABER_TACTIC_CHAIN ) );
	BOOST_CHECK( !NewBotAI_SaberTacticHoldsChain( NEWBOTAI_SABER_TACTIC_REPOSITION ) );
}

BOOST_AUTO_TEST_CASE( saber_tactic_continues_safe_hits_but_bounds_exposed_pressure )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 80.0f );
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
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 80.0f );

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
	// Backpedal swings are a low-skill mistake only (humans: 0% of swing starts).
	BOOST_CHECK_EQUAL( NewBotAI_ApplySaberChoiceGrade( context, NEWBOTAI_SABER_TACTIC_ATTACK, NEWBOTAI_SABER_GRADE_MISTAKE ),
		NEWBOTAI_SABER_TACTIC_STAND_SWING );
	context.skill = 4;
	BOOST_CHECK_EQUAL( NewBotAI_ApplySaberChoiceGrade( context, NEWBOTAI_SABER_TACTIC_ATTACK, NEWBOTAI_SABER_GRADE_MISTAKE ),
		NEWBOTAI_SABER_TACTIC_BACK_SWING );
	context.skill = 10;
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
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 80.0f );
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
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 80.0f );
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
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 80.0f );
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
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 80.0f );
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
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 80.0f );
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
	context.enemyDistance = 80.0f;
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
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 80.0f );
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
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 80.0f );
	const newbotai_saber_command_t partial = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_ATTACK, NEWBOTAI_SABER_BASIC, 0, 1, 1, 1,
		NewBotAI_SaberPrimaryBladeAvailable( 1 ), 0 );
	const newbotai_saber_command_t full = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_ATTACK, NEWBOTAI_SABER_BASIC, 0, 1, 1, 1,
		NewBotAI_SaberPrimaryBladeAvailable( 2 ), 0 );
	BOOST_CHECK_EQUAL( partial.attack, 1 );
	BOOST_CHECK_EQUAL( full.attack, 0 );
}

BOOST_AUTO_TEST_CASE( fan_dwell_links_immediately_at_high_skill )
{
	BOOST_CHECK_EQUAL( NewBotAI_GetFanDwellMs( 250, 7.0f, 0.0f ), 0 );
	BOOST_CHECK_EQUAL( NewBotAI_GetFanDwellMs( 250, 10.0f, 50.0f ), 0 );
	BOOST_CHECK_EQUAL( NewBotAI_GetFanDwellMs( 250, 1.0f, 0.0f ), 250 );
	BOOST_CHECK( NewBotAI_GetFanDwellMs( 250, 4.0f, 0.0f ) < 250 );
	BOOST_CHECK( NewBotAI_GetFanDwellMs( 250, 4.0f, 100.0f ) < NewBotAI_GetFanDwellMs( 250, 4.0f, 0.0f ) );
}

BOOST_AUTO_TEST_CASE( fan_sweep_tracks_human_arc_and_crosses_target_mid_swing )
{
	const float highArc = NewBotAI_GetFanSweepHalfArc( 7.0f, 100.0f, 60.0f );
	const float lowArc = NewBotAI_GetFanSweepHalfArc( 2.0f, 0.0f, 60.0f );

	BOOST_CHECK( highArc >= 55.0f && highArc <= NEWBOTAI_FAN_SWEEP_MAX_HALF_ARC );
	BOOST_CHECK( lowArc < highArc );
	BOOST_CHECK( NewBotAI_GetFanSweepHalfArc( 7.0f, 100.0f, 200.0f ) < highArc );
	BOOST_CHECK( NewBotAI_GetFanSweepHalfArc( 10.0f, 100.0f, 0.0f ) <= NEWBOTAI_FAN_SWEEP_MAX_HALF_ARC );

	// L2R (+1) starts on the left (+yaw), crosses the target at 150ms and ends on the right.
	BOOST_CHECK_CLOSE( NewBotAI_GetFanSweepOffset( 60.0f, 1, 0, 300 ), 60.0f, 0.01f );
	BOOST_CHECK_SMALL( NewBotAI_GetFanSweepOffset( 60.0f, 1, 150, 300 ), 0.01f );
	BOOST_CHECK_CLOSE( NewBotAI_GetFanSweepOffset( 60.0f, 1, 300, 300 ), -60.0f, 0.01f );
	BOOST_CHECK_CLOSE( NewBotAI_GetFanSweepOffset( 60.0f, -1, 0, 300 ), -60.0f, 0.01f );
	BOOST_CHECK_SMALL( NewBotAI_GetFanSweepOffset( 60.0f, 0, 0, 300 ), 0.01f );
}

BOOST_AUTO_TEST_CASE( fan_chain_runs_past_three_seconds_only_while_hitting )
{
	BOOST_CHECK( NewBotAI_FanChainMayContinue( 2500, -1 ) );
	BOOST_CHECK( !NewBotAI_FanChainMayContinue( 3500, -1 ) );
	BOOST_CHECK( !NewBotAI_FanChainMayContinue( 3500, 900 ) );
	BOOST_CHECK( NewBotAI_FanChainMayContinue( 3500, 300 ) );
	BOOST_CHECK( !NewBotAI_FanChainMayContinue( 6500, 100 ) );
}

BOOST_AUTO_TEST_CASE( saber_planner_sweep_scales_from_conservative_envelope )
{
	const float amp = NewBotAI_GetSaberSweepAmplitude( 8.0f, 50.0f );

	BOOST_CHECK( amp > NEWBOTAI_SABER_SWEEP_DEGREES );
	// Human envelope: ~60-70 degrees swept per swing at 200-450 deg/s.
	BOOST_CHECK( 2.0f * NewBotAI_GetSaberSweepAmplitude( 10.0f, 100.0f ) <= 70.0f );
	BOOST_CHECK( 2.0f * NewBotAI_GetSaberSweepAmplitude( 7.0f, 100.0f ) >= 60.0f );
	BOOST_CHECK( NewBotAI_GetSaberSweepAmplitude( 2.0f, 0.0f ) < amp );
	BOOST_CHECK( NewBotAI_GetSaberSweepRate( amp ) >= NEWBOTAI_SABER_SWEEP_MIN_RATE );
	BOOST_CHECK( NewBotAI_GetSaberSweepRate( amp ) <= NEWBOTAI_SABER_SWEEP_MAX_RATE );
	BOOST_CHECK_CLOSE( NewBotAI_GetSaberSweepRateScaled( 1.0f, 0.0f ), NEWBOTAI_SABER_SWEEP_MIN_RATE, 0.01f );
	BOOST_CHECK_CLOSE( NewBotAI_GetSaberSweepRateScaled( 10.0f, 100.0f ), NEWBOTAI_SABER_SWEEP_MAX_RATE, 0.01f );
	// Each swing starts ~20 degrees off the target on its starting side...
	BOOST_CHECK_CLOSE( NewBotAI_SaberYawOffsetScaled( NEWBOTAI_SABER_YAW_PREPARE, 0.0f, 1, 1, 1, amp ),
		NEWBOTAI_SABER_SWING_START_OFFSET, 0.01f );
	BOOST_CHECK_CLOSE( NewBotAI_SaberYawOffsetScaled( NEWBOTAI_SABER_YAW_PREPARE, 0.0f, 0, -1, 1, amp ),
		-NEWBOTAI_SABER_SWING_START_OFFSET, 0.01f );
	// ...and sweeps the full 2*amp arc across the target.
	BOOST_CHECK_CLOSE( NewBotAI_SaberYawOffsetScaled( NEWBOTAI_SABER_YAW_ACTIVE, 0.8f, 1, 1, 1, amp ),
		NEWBOTAI_SABER_SWING_START_OFFSET - 2.0f * amp, 0.01f );
	BOOST_CHECK_SMALL( NewBotAI_SaberYawOffsetScaled( NEWBOTAI_SABER_YAW_PREPARE, 0.0f, 1, 1, 0, amp ), 0.01f );
	BOOST_CHECK_CLOSE( NewBotAI_SaberStepYawOffsetScaled( 0.0f, 100.0f, 1000, amp, 1000.0f ), 2.0f * amp, 0.01f );
	BOOST_CHECK_CLOSE( NewBotAI_SaberStepYawOffsetScaled( 0.0f, 50.0f, 10, amp, 1000.0f ), 10.0f, 0.01f );
}

BOOST_AUTO_TEST_CASE( swing_footing_uses_predicted_peak_range )
{
	// 90u away closing at 300u/s -> 45u at the swing peak.
	const float predicted = NewBotAI_PredictRange2D( 90.0f, 0.0f, -300.0f, 0.0f, NEWBOTAI_SWING_PEAK_LEAD_MS );

	BOOST_CHECK_CLOSE( predicted, 45.0f, 0.01f );
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingFooting( 90.0f, predicted, 300.0f, 0, 0 ), NEWBOTAI_SWING_FOOTING_START );
	// 70-100u only starts while closing faster than ~150u/s; otherwise keep stepping in.
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingFooting( 90.0f, 90.0f, 0.0f, 0, 0 ), NEWBOTAI_SWING_FOOTING_STEP_IN );
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingFooting( 110.0f, 90.0f, 200.0f, 0, 0 ), NEWBOTAI_SWING_FOOTING_START );
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingFooting( 68.0f, 68.0f, 0.0f, 0, 0 ), NEWBOTAI_SWING_FOOTING_START );
	// Beyond ~130u: bait/step in even when the fast close predicts reach.
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingFooting( 140.0f, 60.0f, 500.0f, 0, 0 ), NEWBOTAI_SWING_FOOTING_STEP_IN );
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingFooting( 90.0f, 90.0f, -120.0f, 0, 0 ), NEWBOTAI_SWING_FOOTING_STEP_IN );
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingFooting( 120.0f, 120.0f, 0.0f, 0, 0 ), NEWBOTAI_SWING_FOOTING_STEP_IN );
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingFooting( 66.0f, 66.0f, 0.0f, 0, 1 ), NEWBOTAI_SWING_FOOTING_START );
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingFooting( 80.0f, 80.0f, 0.0f, 1, 0 ), NEWBOTAI_SWING_FOOTING_START );
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingFooting( 110.0f, 50.0f, -120.0f, 0, 0 ), NEWBOTAI_SWING_FOOTING_HOLD );
}

BOOST_AUTO_TEST_CASE( saber_planner_fresh_swing_follows_start_window )
{
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 90.0f );

	// Unknown closing speed keeps the old range-only behaviour.
	BOOST_CHECK( NewBotAI_SaberFreshSwingWindowAllows( context, NEWBOTAI_SABER_TACTIC_STEP_IN ) );
	context.closingKnown = 1;
	context.currentDistance = 105.0f;
	context.closingSpeed = 40.0f;
	BOOST_CHECK( !NewBotAI_SaberFreshSwingWindowAllows( context, NEWBOTAI_SABER_TACTIC_STEP_IN ) );
	BOOST_CHECK( !NewBotAI_SaberFreshSwingWindowAllows( context, NEWBOTAI_SABER_TACTIC_ATTACK ) );
	// Chains and counters are not fresh starts.
	BOOST_CHECK( NewBotAI_SaberFreshSwingWindowAllows( context, NEWBOTAI_SABER_TACTIC_CHAIN ) );
	BOOST_CHECK( NewBotAI_SaberFreshSwingWindowAllows( context, NEWBOTAI_SABER_TACTIC_COUNTER ) );
	context.closingSpeed = 200.0f;
	BOOST_CHECK( NewBotAI_SaberFreshSwingWindowAllows( context, NEWBOTAI_SABER_TACTIC_STEP_IN ) );
	context.currentDistance = 140.0f;
	BOOST_CHECK( !NewBotAI_SaberFreshSwingWindowAllows( context, NEWBOTAI_SABER_TACTIC_STEP_IN ) );
	context.currentDistance = 80.0f;
	context.enemyDistance = 60.0f;
	context.closingSpeed = 0.0f;
	BOOST_CHECK( NewBotAI_SaberFreshSwingWindowAllows( context, NEWBOTAI_SABER_TACTIC_ATTACK ) );
}

BOOST_AUTO_TEST_CASE( fan_stances_alternate_horizontals_and_keep_t2b_for_finisher )
{
	for ( int family : { NEWBOTAI_SABER_BASIC, NEWBOTAI_SABER_DIAGONAL_VERTICAL, NEWBOTAI_SABER_HORIZONTAL } )
		for ( int stage = 0; stage < 6; stage++ )
		{
			int forward, right;
			NewBotAI_SaberSelectionInputs( (newbotai_saber_family_t)family, stage, -1, &forward, &right );
			NewBotAI_SaberFanStanceInputs( 1, (newbotai_saber_family_t)family, stage, -1, &forward, &right );
			// Never a T2B (forward only) outside the finisher.
			BOOST_CHECK( !(forward > 0 && right == 0) );
		}
	int forward = 0, right = -1;
	NewBotAI_SaberFanStanceInputs( 1, NEWBOTAI_SABER_FINISH, 0, -1, &forward, &right );
	BOOST_CHECK_EQUAL( forward, 0 );
	BOOST_CHECK_EQUAL( right, -1 );
	NewBotAI_SaberFanStanceInputs( 1, NEWBOTAI_SABER_FINISH, NEWBOTAI_SABER_FAN_FINISH_STAGE, -1, &forward, &right );
	BOOST_CHECK_EQUAL( forward, 1 );
	BOOST_CHECK_EQUAL( right, 0 );
	// Other stances are unchanged.
	forward = 1; right = 0;
	NewBotAI_SaberFanStanceInputs( 0, NEWBOTAI_SABER_BASIC, 1, -1, &forward, &right );
	BOOST_CHECK_EQUAL( forward, 1 );
	BOOST_CHECK_EQUAL( right, 0 );
	// jundon: R2L opens ~54%, L2R->R2L links ~2/3 as often as R2L->L2R.
	BOOST_CHECK_EQUAL( NewBotAI_FanStartDirection( 54 ), -1 );
	BOOST_CHECK_EQUAL( NewBotAI_FanStartDirection( 55 ), 1 );
	BOOST_CHECK_EQUAL( NewBotAI_FanLinkChance( -1 ), 100 );
	BOOST_CHECK_EQUAL( NewBotAI_FanLinkChance( 1 ), 66 );
}

BOOST_AUTO_TEST_CASE( swing_end_sidestep_and_enemy_swing_reactions )
{
	// Unproductive, unchained swing return: sidestep back.
	BOOST_CHECK( NewBotAI_ShouldSidestepAfterSwing( 1, 0, -1, 0 ) );
	BOOST_CHECK( NewBotAI_ShouldSidestepAfterSwing( 1, 0, 900, 0 ) );
	// Landing hits, chaining, escaping or not in a return: keep the current footwork.
	BOOST_CHECK( !NewBotAI_ShouldSidestepAfterSwing( 1, 0, 300, 0 ) );
	BOOST_CHECK( !NewBotAI_ShouldSidestepAfterSwing( 1, 1, -1, 0 ) );
	BOOST_CHECK( !NewBotAI_ShouldSidestepAfterSwing( 1, 0, -1, 1 ) );
	BOOST_CHECK( !NewBotAI_ShouldSidestepAfterSwing( 0, 0, -1, 0 ) );

	int advances = 0;
	for ( int roll = 1; roll <= 100; roll++ )
		advances += NewBotAI_EnemySwingAdvances( roll );
	BOOST_CHECK_EQUAL( advances, NEWBOTAI_ENEMY_SWING_ADVANCE_PERCENT );

	BOOST_CHECK( !NewBotAI_CounterSwingReady( 50, 80.0f, 0 ) );
	BOOST_CHECK( NewBotAI_CounterSwingReady( 100, 80.0f, 0 ) );
	BOOST_CHECK( NewBotAI_CounterSwingReady( 300, 90.0f, 0 ) );
	BOOST_CHECK( !NewBotAI_CounterSwingReady( 400, 80.0f, 0 ) );
	BOOST_CHECK( !NewBotAI_CounterSwingReady( 120, 95.0f, 0 ) );
	BOOST_CHECK( !NewBotAI_CounterSwingReady( 120, 80.0f, 1 ) );
}

BOOST_AUTO_TEST_CASE( swing_dodge_prefers_lateral_or_jump_over_backpedal )
{
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingDodgeChoice( 200.0f, 1, 1, 0, 50, 50 ), NEWBOTAI_SWING_DODGE_NONE );
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingDodgeChoice( 100.0f, 1, 1, 0, 50, 50 ), NEWBOTAI_SWING_DODGE_LATERAL );
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingDodgeChoice( 100.0f, 1, 1, 0, 50, 10 ), NEWBOTAI_SWING_DODGE_JUMP );
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingDodgeChoice( 100.0f, 0, 1, 0, 50, 10 ), NEWBOTAI_SWING_DODGE_LATERAL );
	BOOST_CHECK_EQUAL( NewBotAI_GetSwingDodgeChoice( 100.0f, 1, 1, 30, 20, 50 ), NEWBOTAI_SWING_DODGE_BACKPEDAL );
	BOOST_CHECK( NewBotAI_AllowAirborneSwingStart( 1, 0, 50 ) );
	BOOST_CHECK( !NewBotAI_AllowAirborneSwingStart( 0, 0, 1 ) );
	BOOST_CHECK( NewBotAI_AllowAirborneSwingStart( 0, 30, 10 ) );
}

BOOST_AUTO_TEST_CASE( combo_gaps_match_jundon_at_high_skill )
{
	BOOST_CHECK_EQUAL( NewBotAI_GetComboGapMs( NEWBOTAI_COMBO_PULL_KICK, 7.0f, 90, 20 ), 0 );
	BOOST_CHECK_EQUAL( NewBotAI_GetComboGapMs( NEWBOTAI_COMBO_THROW_PULL, 8.0f, 0, 0 ), 66 );
	BOOST_CHECK_EQUAL( NewBotAI_GetComboGapMs( NEWBOTAI_COMBO_GRIP_THROW, 10.0f, 0, 0 ), 100 );
	BOOST_CHECK_EQUAL( NewBotAI_GetComboGapMs( NEWBOTAI_COMBO_DRAIN_FOLLOWUP, 7.0f, 0, 0 ), 66 );
	BOOST_CHECK_EQUAL( NewBotAI_GetComboGapMs( NEWBOTAI_COMBO_PULL_KICK, 6.0f, 0, 0 ), 100 );
	BOOST_CHECK_EQUAL( NewBotAI_GetComboGapMs( NEWBOTAI_COMBO_PULL_KICK, 1.0f, 0, 0 ), 250 );
	BOOST_CHECK( NewBotAI_GetComboGapMs( NEWBOTAI_COMBO_PULL_KICK, 3.0f, 90, 20 ) >
		NewBotAI_GetComboGapMs( NEWBOTAI_COMBO_PULL_KICK, 3.0f, 0, 0 ) );
}

BOOST_AUTO_TEST_CASE( reaction_bonuses_follow_observed_counters )
{
	BOOST_CHECK( NewBotAI_GetReactionBonus( BOTLEARN_TOK_DRAIN, BOTLEARN_TOK_PULL ) >
		NewBotAI_GetReactionBonus( BOTLEARN_TOK_DRAIN, BOTLEARN_TOK_THROW ) );
	BOOST_CHECK( NewBotAI_GetReactionBonus( BOTLEARN_TOK_DRAIN, BOTLEARN_TOK_PUSH ) < 0 );
	BOOST_CHECK( NewBotAI_GetReactionBonus( BOTLEARN_TOK_GRIP, BOTLEARN_TOK_PUSH ) < 0 );
	BOOST_CHECK( NewBotAI_GetReactionBonus( BOTLEARN_TOK_THROW, BOTLEARN_TOK_THROW ) > 0 );
	BOOST_CHECK( NewBotAI_GetReactionBonus( BOTLEARN_TOK_THROW, BOTLEARN_TOK_JUMP ) < 0 );
	BOOST_CHECK( NewBotAI_GetReactionBonus( BOTLEARN_TOK_PULL, BOTLEARN_TOK_DRAIN ) < 0 );
	BOOST_CHECK( NewBotAI_GetReactionBonus( BOTLEARN_TOK_PUSH, BOTLEARN_TOK_DRAIN ) > 0 );
	BOOST_CHECK_EQUAL( NewBotAI_GetReactionBonus( BOTLEARN_TOK_IDLE, BOTLEARN_TOK_PULL ), 0 );
	BOOST_CHECK( NewBotAI_GetForceEconomyBonus( BOTLEARN_TOK_PULL, 65 ) > 0 );
	BOOST_CHECK( NewBotAI_GetForceEconomyBonus( BOTLEARN_TOK_DRAIN, 18 ) > 0 );
	BOOST_CHECK( NewBotAI_GetForceEconomyBonus( BOTLEARN_TOK_DRAIN, 80 ) < 0 );
}

BOOST_AUTO_TEST_CASE( saber_throw_situation_matches_human_hit_rates )
{
	BOOST_CHECK( NewBotAI_GetSaberThrowSituationBonus( 40, 200.0f, 0, 0, 0, 1 ) > 0 );
	BOOST_CHECK( NewBotAI_GetSaberThrowSituationBonus( 90, 200.0f, 1, 0, 0, 0 ) < 0 );
	BOOST_CHECK( NewBotAI_GetSaberThrowSituationBonus( 60, 200.0f, 1, 1, 0, 0 ) >= 0 );
	BOOST_CHECK( NewBotAI_GetSaberThrowSituationBonus( 60, 300.0f, 0, 0, 1, 0 ) < 0 );
	BOOST_CHECK( NewBotAI_GetSaberThrowSituationBonus( 90, 200.0f, 0, 0, 0, 0 ) <
		NewBotAI_GetSaberThrowSituationBonus( 40, 200.0f, 0, 0, 0, 0 ) );
}

BOOST_AUTO_TEST_CASE( saber_range_buffer_holds_reach_decision )
{
	const float threshold = NEWBOTAI_SABER_STEP_IN_SWING_RANGE;
	const float band = NEWBOTAI_SABER_RANGE_HYSTERESIS;

	// Outside: the range is used as is.
	BOOST_CHECK_CLOSE( NewBotAI_SaberRangeWithHysteresis( threshold + 5.0f, threshold, band, 0 ),
		threshold + 5.0f, 0.001f );
	// Already inside: small drifts past the threshold stay inside.
	BOOST_CHECK_CLOSE( NewBotAI_SaberRangeWithHysteresis( threshold + 5.0f, threshold, band, 1 ),
		threshold, 0.001f );
	BOOST_CHECK_CLOSE( NewBotAI_SaberRangeWithHysteresis( threshold + band + 1.0f, threshold, band, 1 ),
		threshold + band + 1.0f, 0.001f );
	BOOST_CHECK_CLOSE( NewBotAI_SaberRangeWithHysteresis( 60.0f, threshold, band, 1 ), 60.0f, 0.001f );
	BOOST_CHECK( NewBotAI_SaberTacticAllowsSwingStart( NEWBOTAI_SABER_TACTIC_STEP_IN,
		NewBotAI_SaberRangeWithHysteresis( threshold + 8.0f, threshold, band, 1 ) ) );
}

BOOST_AUTO_TEST_CASE( saber_grade_is_held_through_the_swing )
{
	BOOST_CHECK( NewBotAI_SaberGradeShouldReroll( 1000, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberGradeShouldReroll( 1300, 1000, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberGradeShouldReroll( 1550, 1000, 1 ) );
	BOOST_CHECK( NewBotAI_SaberGradeShouldReroll( 1550, 1000, 0 ) );
	BOOST_CHECK( NewBotAI_SaberGradeShouldReroll( 1000 + NEWBOTAI_SABER_GRADE_HOLD_MAX_MS, 1000, 1 ) );
}

BOOST_AUTO_TEST_CASE( saber_attacks_commit_forward_and_handover_keeps_intent )
{
	BOOST_CHECK_EQUAL( NewBotAI_SaberAttackForward( NEWBOTAI_SABER_TACTIC_ATTACK, 70.0f ), 1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAttackForward( NEWBOTAI_SABER_TACTIC_ATTACK, 40.0f ), 0 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAttackForward( NEWBOTAI_SABER_TACTIC_STAND_SWING, 70.0f ), 1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAttackForward( NEWBOTAI_SABER_TACTIC_STAND_SWING, 50.0f ), 0 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAttackForward( NEWBOTAI_SABER_TACTIC_BACK_SWING, 70.0f ), -1 );
	newbotai_saber_tactic_context_t context = MakeSaberDuelContext( 95.0f );
	const newbotai_saber_command_t stepIn = NewBotAI_PlanSaberCommand( context,
		NEWBOTAI_SABER_TACTIC_STEP_IN, NEWBOTAI_SABER_HORIZONTAL, 1, 1, 1, 0, 1, 0 );
	BOOST_CHECK_EQUAL( stepIn.attack, 1 );
	BOOST_CHECK_EQUAL( stepIn.forward, 1 );

	BOOST_CHECK_EQUAL( NewBotAI_SaberHandoverForward( 1, -1, 100, 0 ), 1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberHandoverForward( 1, 0, 100, 0 ), 1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberHandoverForward( 1, -1, 100, 1 ), -1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberHandoverForward( 1, -1, NEWBOTAI_SABER_HANDOVER_MS + 1, 0 ), -1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberHandoverForward( -1, 1, 100, 0 ), 1 );

	BOOST_CHECK_EQUAL( NewBotAI_SaberSwingStartForward( 8, 1, -1, 0 ), 1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberSwingStartForward( 8, 1, -1, 1 ), -1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberSwingStartForward( 8, 0, -1, 0 ), -1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberSwingStartForward( 4, 1, -1, 0 ), -1 );
}

BOOST_AUTO_TEST_CASE( saber_advance_through_swing_phases )
{
	// Walk in through windup / apex / cooldown when out of reach.
	BOOST_CHECK_EQUAL( NewBotAI_SaberAdvanceForward( NEWBOTAI_SABER_PHASE_WINDUP, 90.0f, 0, 0, 300, 0, 0, 0 ), 1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAdvanceForward( NEWBOTAI_SABER_PHASE_APEX, 80.0f, -1, 0, 200, 0, 0, 0 ), 1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAdvanceForward( NEWBOTAI_SABER_PHASE_COOLDOWN, 75.0f, 0, 0, 200, 0, 0, 0 ), 1 );
	// Already on top of the target: keep the planned input.
	BOOST_CHECK_EQUAL( NewBotAI_SaberAdvanceForward( NEWBOTAI_SABER_PHASE_APEX, 44.0f, 0, 0, 200, 0, 0, 0 ), 0 );
	// Hand the stick back before the chained swing direction is read.
	BOOST_CHECK_EQUAL( NewBotAI_SaberAdvanceForward( NEWBOTAI_SABER_PHASE_COOLDOWN, 90.0f, 0, 1, NEWBOTAI_SABER_ADVANCE_RELINK_MS, 0, 0, 0 ), 0 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAdvanceForward( NEWBOTAI_SABER_PHASE_COOLDOWN, 90.0f, 0, 1, NEWBOTAI_SABER_ADVANCE_RELINK_MS + 1, 0, 0, 0 ), 1 );
	// Swing-start frame and deliberate escapes are never overridden.
	BOOST_CHECK_EQUAL( NewBotAI_SaberAdvanceForward( NEWBOTAI_SABER_PHASE_IDLE, 120.0f, 0, 0, 0, 1, 0, 0 ), 0 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAdvanceForward( NEWBOTAI_SABER_PHASE_WINDUP, 120.0f, -1, 0, 300, 0, 0, 1 ), -1 );
	// Between swings: close to swing range, but keep a dodge back from an incoming swing.
	BOOST_CHECK_EQUAL( NewBotAI_SaberAdvanceForward( NEWBOTAI_SABER_PHASE_IDLE, 150.0f, 0, 0, 0, 0, 0, 0 ), 1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAdvanceForward( NEWBOTAI_SABER_PHASE_IDLE, 150.0f, -1, 0, 0, 0, 1, 0 ), -1 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAdvanceForward( NEWBOTAI_SABER_PHASE_IDLE, 60.0f, 0, 0, 0, 0, 0, 0 ), 0 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberAdvanceForward( NEWBOTAI_SABER_PHASE_IDLE, NEWBOTAI_SABER_ADVANCE_MAX_RANGE + 1.0f, 0, 0, 0, 0, 0, 0 ), 0 );
}

BOOST_AUTO_TEST_CASE( fan_chain_drives_on_while_landing_hits )
{
	BOOST_CHECK_EQUAL( NewBotAI_GetFanLinkDwellMs( 200, 300 ), 0 );
	BOOST_CHECK_EQUAL( NewBotAI_GetFanLinkDwellMs( 200, NEWBOTAI_FAN_CHAIN_HIT_WINDOW_MS + 1 ), 200 );
	BOOST_CHECK_EQUAL( NewBotAI_GetFanLinkDwellMs( 200, -1 ), 200 );
	BOOST_CHECK( NewBotAI_FanChainIsLanding( 0 ) );
	BOOST_CHECK( !NewBotAI_FanChainIsLanding( -1 ) );
}

BOOST_AUTO_TEST_CASE( approach_aim_is_held_off_centre )
{
	BOOST_CHECK_CLOSE( NewBotAI_GetApproachAimOffset( 1.0f, 150.0f, 1 ), NEWBOTAI_APPROACH_AIM_MIN_OFFSET, 0.01f );
	BOOST_CHECK_CLOSE( NewBotAI_GetApproachAimOffset( 10.0f, 150.0f, -1 ), -NEWBOTAI_APPROACH_AIM_MAX_OFFSET, 0.01f );
	BOOST_CHECK_SMALL( NewBotAI_GetApproachAimOffset( 10.0f, 40.0f, 1 ), 0.01f );
	BOOST_CHECK_SMALL( NewBotAI_GetApproachAimOffset( 10.0f, 600.0f, 1 ), 0.01f );
}

BOOST_AUTO_TEST_CASE( saber_throw_is_held_through_first_pass )
{
	// Always held for the minimum hold.
	BOOST_CHECK( NewBotAI_SaberThrowHoldProtected( 300, 1, 0 ) );
	// Held past the minimum while still flying toward the target...
	BOOST_CHECK( NewBotAI_SaberThrowHoldProtected( 900, 0, 1 ) );
	// ...but not once it passed, not when heading elsewhere, and never past the cap.
	BOOST_CHECK( !NewBotAI_SaberThrowHoldProtected( 900, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowHoldProtected( 900, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowHoldProtected( NEWBOTAI_THROW_MAX_PASS_HOLD_MS, 0, 1 ) );
	// Early-release rules wait for the pass; drain-lock and lethal danger do not.
	BOOST_CHECK( !NewBotAI_SaberThrowMayRelease( 400, 0, 1, 0, 0 ) );
	BOOST_CHECK( NewBotAI_SaberThrowMayRelease( 400, 0, 1, 1, 0 ) );
	BOOST_CHECK( NewBotAI_SaberThrowMayRelease( 400, 0, 1, 0, 1 ) );
	BOOST_CHECK( NewBotAI_SaberThrowMayRelease( 800, 1, 0, 0, 0 ) );
}

BOOST_AUTO_TEST_CASE( saber_throw_trails_then_switches_side_after_pass )
{
	const float farTrail = NewBotAI_GetSaberThrowAimOffset( 1, 0, 500.0f );
	const float nearTrail = NewBotAI_GetSaberThrowAimOffset( 1, 0, 0.0f );

	// Trails behind the target's motion by 5-10 degrees...
	BOOST_CHECK_CLOSE( farTrail, -NEWBOTAI_THROW_TRAIL_MIN_DEG, 0.01f );
	BOOST_CHECK_CLOSE( nearTrail, -NEWBOTAI_THROW_TRAIL_MAX_DEG, 0.01f );
	BOOST_CHECK_CLOSE( NewBotAI_GetSaberThrowAimOffset( -1, 0, 500.0f ), NEWBOTAI_THROW_TRAIL_MIN_DEG, 0.01f );
	// ...then switches ~9 degrees to the other side once the saber passed.
	BOOST_CHECK_CLOSE( NewBotAI_GetSaberThrowAimOffset( 1, 1, 200.0f ), NEWBOTAI_THROW_PASS_SWITCH_DEG, 0.01f );
	BOOST_CHECK_CLOSE( NewBotAI_GetSaberThrowAimOffset( -1, 1, 200.0f ), -NEWBOTAI_THROW_PASS_SWITCH_DEG, 0.01f );
	BOOST_CHECK_SMALL( NewBotAI_GetSaberThrowAimOffset( 0, 1, 200.0f ), 0.01f );
}

BOOST_AUTO_TEST_CASE( learning_score_is_net_damage_first_relative_to_source )
{
	// Humans win ~80% of tracked duels: a human row at that rate is neutral, not positive.
	BOOST_CHECK_SMALL( BotLearn_ScoreRelative( 50.0f, BotLearn_ExcessWins( 50.0f, 40.0f, 0.8f ), 0.0f ), 0.001f );
	BOOST_CHECK( BotLearn_ScoreRelative( 50.0f, BotLearn_ExcessWins( 50.0f, 40.0f, 0.8f ), 300.0f ) > 0.0f );
	// A bot row is down-weighted, so the same raw outcome moves the score less.
	const float w = BotLearn_SourceWeight( BOTLEARN_SOURCE_BOT );
	BOOST_CHECK( BotLearn_ScoreRelative( 20.0f * w, 0.0f, -200.0f * w ) >
		BotLearn_ScoreRelative( 20.0f, 0.0f, -200.0f ) );
}

BOOST_AUTO_TEST_CASE( high_skill_mistakes_are_small_and_vanish_at_ten )
{
	BOOST_CHECK( NewBotAI_GetSaberTacticMistakeChance( 7, 30 ) <= 3 );
	BOOST_CHECK( NewBotAI_GetSaberTacticMistakeChance( 7, 100 ) <= 10 );
	BOOST_CHECK_EQUAL( NewBotAI_GetSaberTacticMistakeChance( 10, 100 ), 0 );
	BOOST_CHECK( NewBotAI_GetSaberTacticMistakeChance( 5, 30 ) > NewBotAI_GetSaberTacticMistakeChance( 7, 30 ) );
}

BOOST_AUTO_TEST_CASE( broken_parry_reentry_waits_for_engine_and_enemy_followup )
{
	BOOST_CHECK( !NewBotAI_SaberDefenseReady( 1200, 1250, 1180, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberDefenseReady( 1250, 1250, 1180, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberDefenseReady( 1250, 1250, 1180, 0, 1 ) );
	BOOST_CHECK( !NewBotAI_SaberDefenseReady( 1300, 1250, 1380, 0, 0 ) );
	BOOST_CHECK( NewBotAI_SaberDefenseReady( 1380, 1250, 1380, 0, 0 ) );
	// Ordinary parry/bounce does not create a broken-parry recovery deadline.
	BOOST_CHECK( NewBotAI_SaberDefenseReady( 1000, 0, 0, 0, 0 ) );
}

BOOST_AUTO_TEST_CASE( saber_defense_categories_do_not_conflate_bounce_or_normal_recovery )
{
	BOOST_CHECK_EQUAL( NewBotAI_SaberDefenseCategory( 0, 0, 0 ), NEWBOTAI_SABER_DEFENSE_NONE );
	BOOST_CHECK_EQUAL( NewBotAI_SaberDefenseCategory( 0, 0, 1 ), NEWBOTAI_SABER_DEFENSE_PARRY );
	BOOST_CHECK_EQUAL( NewBotAI_SaberDefenseCategory( 0, 1, 1 ), NEWBOTAI_SABER_DEFENSE_BOUNCE );
	BOOST_CHECK_EQUAL( NewBotAI_SaberDefenseCategory( 1, 1, 1 ), NEWBOTAI_SABER_DEFENSE_BROKEN );
	BOOST_CHECK_EQUAL( NewBotAI_SaberDefenseState( 1, 1, 1, 1, 0 ), NEWBOTAI_SABER_DEFENSE_KNOCKDOWN );
	BOOST_CHECK_EQUAL( NewBotAI_SaberDefenseState( 1, 1, 1, 1, 1 ), NEWBOTAI_SABER_DEFENSE_LOST );
	BOOST_CHECK_EQUAL( NewBotAI_SaberDefenseState( 0, 0, 0, 0, 1 ), NEWBOTAI_SABER_DEFENSE_LOST );
	// A normal saber return is tracked separately; it is not a defensive block.
	BOOST_CHECK_EQUAL( NewBotAI_SaberDefenseCategory( 0, 0, 0 ), NEWBOTAI_SABER_DEFENSE_NONE );
}

BOOST_AUTO_TEST_CASE( broken_defense_followup_ends_after_leaving_reach_not_distant_swings )
{
	BOOST_CHECK( NewBotAI_SaberDefenseFollowupThreat( 1, 90.0f, 200.0f ) );
	BOOST_CHECK( NewBotAI_SaberDefenseFollowupThreat( 1, 240.0f, 100.0f ) );
	BOOST_CHECK( NewBotAI_SaberDefenseFollowupThreat( 1, 150.0f, 200.0f ) );
	BOOST_CHECK( !NewBotAI_SaberDefenseFollowupThreat( 1, 240.0f, 200.0f ) );
	BOOST_CHECK( !NewBotAI_SaberDefenseFollowupThreat( 0, 90.0f, 50.0f ) );
	int followupUntil = 1180;
	for (int now = 1000; now <= 1500; now += 50)
	{
		if (NewBotAI_SaberDefenseFollowupThreat( 1, 400.0f, 400.0f ))
			followupUntil = now + NEWBOTAI_SABER_FOLLOWUP_CLEAR_MS;
	}
	BOOST_CHECK( NewBotAI_SaberDefenseReady( 1500, 1250, followupUntil, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberDefenseReady( 1500, 1250, followupUntil, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberDefenseReady( 1500, 1250, followupUntil, 0, 100 ) );
}

BOOST_AUTO_TEST_CASE( saber_footing_world_direction_survives_final_aim_rotation )
{
	float x, y;
	NewBotAI_SaberWorldDirection( 0.0f, -1, 1, &x, &y );
	BOOST_CHECK_CLOSE( x, -sqrtf( 0.5f ), 0.001f );
	BOOST_CHECK_CLOSE( y, -sqrtf( 0.5f ), 0.001f );
	// With the view turned 90 degrees, the same world lane becomes back-left.
	float forward, right;
	NewBotAI_ProjectWorldMovement( x, y, 90.0f, &forward, &right );
	BOOST_CHECK_CLOSE( forward, -sqrtf( 0.5f ), 0.001f );
	BOOST_CHECK_CLOSE( right, -sqrtf( 0.5f ), 0.001f );
	float oppositeX, oppositeY;
	NewBotAI_SaberWorldDirection( 90.0f, -1, -1, &oppositeX, &oppositeY );
	BOOST_CHECK_CLOSE( oppositeX, x, 0.001f );
	BOOST_CHECK_CLOSE( oppositeY, y, 0.001f );
	// Projection retains analog proportions, not just the signs of movement keys.
	NewBotAI_ProjectWorldMovement( 1.0f, 0.0f, 20.0f, &forward, &right );
	BOOST_CHECK_CLOSE( forward, cosf( 20.0f * 0.017453292519943295f ), 0.001f );
	BOOST_CHECK_CLOSE( right, sinf( 20.0f * 0.017453292519943295f ), 0.001f );
	BOOST_CHECK_LT( right, forward * 0.5f );
	NewBotAI_SaberWorldDirection( 359.0f, 0, 0, &x, &y );
	BOOST_CHECK_SMALL( x, 0.001f );
	BOOST_CHECK_SMALL( y, 0.001f );
}

BOOST_AUTO_TEST_CASE( saber_footing_is_held_until_a_phase_or_safety_boundary )
{
	BOOST_CHECK( !NewBotAI_SaberFootingNeedsUpdate( 1, 1, 1, 10, 10, 2, 2 ) );
	BOOST_CHECK( NewBotAI_SaberFootingNeedsUpdate( 1, 1, 2, 10, 10, 2, 2 ) );
	BOOST_CHECK( NewBotAI_SaberFootingNeedsUpdate( 1, 1, 1, 10, 11, 2, 2 ) );
	BOOST_CHECK( NewBotAI_SaberFootingNeedsUpdate( 1, 1, 1, 10, 10, 2, 3 ) );
	BOOST_CHECK( NewBotAI_SaberFootingNeedsUpdate( 0, 1, 1, 10, 10, 2, 2 ) );
}

BOOST_AUTO_TEST_CASE( saber_learning_records_only_committed_accepted_attacks )
{
	BOOST_CHECK( NewBotAI_SaberCommittedLearningDecision( 1, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberCommittedLearningDecision( 0, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberCommittedLearningDecision( 1, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberCommittedLearningDecision( 1, 1, 1 ) );
}

BOOST_AUTO_TEST_CASE( saber_poke_counteryaw_is_phase_aware_and_bounded )
{
	BOOST_CHECK_SMALL( NewBotAI_SaberPokeCounterYaw( NEWBOTAI_SABER_YAW_PREPARE, 0.475f, 1 ), 0.001f );
	BOOST_CHECK_SMALL( NewBotAI_SaberPokeCounterYaw( NEWBOTAI_SABER_YAW_RECOVER, 0.475f, 1 ), 0.001f );
	BOOST_CHECK_SMALL( NewBotAI_SaberPokeCounterYaw( NEWBOTAI_SABER_YAW_NEXT, 0.9f, 1 ), 0.001f );
	BOOST_CHECK_CLOSE( NewBotAI_SaberPokeCounterYaw( NEWBOTAI_SABER_YAW_ACTIVE, 0.475f, 1 ), 6.0f, 0.001f );
	BOOST_CHECK_CLOSE( NewBotAI_SaberPokeCounterYaw( NEWBOTAI_SABER_YAW_ACTIVE, 0.475f, -1 ), -6.0f, 0.001f );
	BOOST_CHECK_SMALL( NewBotAI_SaberPokeCounterYaw( NEWBOTAI_SABER_YAW_ACTIVE, 0.475f, 0 ), 0.001f );
	for (int direction : { -1, 1 })
	{
		BOOST_CHECK_LT(
			NewBotAI_SaberPokeCounterYaw( NEWBOTAI_SABER_YAW_ACTIVE, 0.7f, direction ) *
			NewBotAI_SaberYawOffset( NEWBOTAI_SABER_YAW_ACTIVE, 0.7f, direction, direction, 1 ), 0.0f );
	}
	BOOST_CHECK_SMALL( NewBotAI_SaberPokeCounterYaw( NEWBOTAI_SABER_YAW_ACTIVE, 1.0f, 1 ), 0.001f );
}

BOOST_AUTO_TEST_CASE( saber_active_wiggle_obeys_delay_axes_and_caps )
{
	float yaw, pitch;
	for (int phase = NEWBOTAI_SABER_YAW_PREPARE; phase <= NEWBOTAI_SABER_YAW_NEXT; ++phase)
	{
		if (phase == NEWBOTAI_SABER_YAW_ACTIVE) continue;
		NewBotAI_GetSaberActiveWiggle( (newbotai_saber_yaw_phase_t)phase, 500, 0, 45.0f, 20.0f, 20.0f, &yaw, &pitch );
		BOOST_CHECK_SMALL( yaw, 0.001f );
		BOOST_CHECK_SMALL( pitch, 0.001f );
	}
	NewBotAI_GetSaberActiveWiggle( NEWBOTAI_SABER_YAW_ACTIVE, 119, 120, 45.0f, 20.0f, 2.0f, &yaw, &pitch );
	BOOST_CHECK_SMALL( yaw, 0.001f );
	BOOST_CHECK_SMALL( pitch, 0.001f );
	for (int elapsed = 120; elapsed <= 1000; elapsed += 17)
	{
		NewBotAI_GetSaberActiveWiggle( NEWBOTAI_SABER_YAW_ACTIVE, elapsed, 120, 45.0f, 20.0f, 25.0f, &yaw, &pitch );
		BOOST_CHECK_LE( fabsf( yaw ), 8.0f );
		BOOST_CHECK_LE( fabsf( pitch ), 4.0f );
	}
	NewBotAI_GetSaberActiveWiggle( NEWBOTAI_SABER_YAW_ACTIVE, 200, 0, 8.0f, 4.0f, 0.0f, &yaw, &pitch );
	BOOST_CHECK_SMALL( yaw, 0.001f );
	BOOST_CHECK_SMALL( pitch, 0.001f );
	NewBotAI_GetSaberActiveWiggle( NEWBOTAI_SABER_YAW_ACTIVE, 200, 0, -8.0f, -4.0f, 2.0f, &yaw, &pitch );
	BOOST_CHECK_SMALL( yaw, 0.001f );
	BOOST_CHECK_SMALL( pitch, 0.001f );
}

BOOST_AUTO_TEST_CASE( saber_throw_phase_uses_target_facing_and_engine_cadence )
{
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowSteerCadence( 1 ), 0 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowSteerCadence( 2 ), 400 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowSteerCadence( 3 ), 100 );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowNextPhase( NEWBOTAI_THROW_LAUNCH, 399, 400, 100.0f ), NEWBOTAI_THROW_LAUNCH );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowNextPhase( NEWBOTAI_THROW_LAUNCH, 400, 400, 100.0f ), NEWBOTAI_THROW_BYPASS );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowNextPhase( NEWBOTAI_THROW_BYPASS, 500, 100, 24.0f ), NEWBOTAI_THROW_BYPASS );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowNextPhase( NEWBOTAI_THROW_BYPASS, 500, 100, 0.0f ), NEWBOTAI_THROW_REAR );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowNextPhase( NEWBOTAI_THROW_REAR, 500, 100, -49.0f ), NEWBOTAI_THROW_CUT_THROUGH );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowNextPhase( NEWBOTAI_THROW_RECALL, 500, 100, -100.0f ), NEWBOTAI_THROW_RECALL );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowNextPhase( NEWBOTAI_THROW_LAUNCH, 500, 0, -100.0f ), NEWBOTAI_THROW_LAUNCH );
}

BOOST_AUTO_TEST_CASE( saber_throw_bypasses_ready_defense_but_intercepts_exposed_targets )
{
	BOOST_CHECK( NewBotAI_SaberThrowTargetGuarded( 1, 1, 0, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowTargetGuarded( 0, 1, 0, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowTargetGuarded( 1, 0, 0, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowTargetGuarded( 1, 1, 1, 0, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowTargetGuarded( 1, 1, 0, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowTargetGuarded( 1, 1, 0, 0, 1 ) );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowTargetPhase( NEWBOTAI_THROW_LAUNCH, 1, 1 ), NEWBOTAI_THROW_BYPASS );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowTargetPhase( NEWBOTAI_THROW_LAUNCH, 0, 1 ), NEWBOTAI_THROW_CUT_THROUGH );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowTargetPhase( NEWBOTAI_THROW_REAR, 0, 1 ), NEWBOTAI_THROW_CUT_THROUGH );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowTargetPhase( NEWBOTAI_THROW_REAR, 1, 1 ), NEWBOTAI_THROW_REAR );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowTargetPhase( NEWBOTAI_THROW_RECALL, 0, 1 ), NEWBOTAI_THROW_RECALL );
	BOOST_CHECK_EQUAL( NewBotAI_SaberThrowTargetPhase( NEWBOTAI_THROW_LAUNCH, 1, 0 ), NEWBOTAI_THROW_LAUNCH );
}

BOOST_AUTO_TEST_CASE( saber_throw_overhead_fallback_preserves_rear_placement_and_cut_through )
{
	float rear, side, height;
	for (int lane : { -1, 1 })
	{
		NewBotAI_SaberThrowLaneOffsets( NEWBOTAI_THROW_BYPASS, lane, 0, &rear, &side, &height );
		BOOST_CHECK_EQUAL( rear, -48.0f );
		BOOST_CHECK_EQUAL( side, lane * 112.0f );
		BOOST_CHECK_SMALL( height, 0.001f );
		NewBotAI_SaberThrowLaneOffsets( NEWBOTAI_THROW_BYPASS, lane, 1, &rear, &side, &height );
		BOOST_CHECK_EQUAL( rear, -48.0f );
		BOOST_CHECK_EQUAL( side, lane * 32.0f );
		BOOST_CHECK_EQUAL( height, 128.0f );
		NewBotAI_SaberThrowLaneOffsets( NEWBOTAI_THROW_REAR, lane, 1, &rear, &side, &height );
		BOOST_CHECK_EQUAL( rear, -128.0f );
		BOOST_CHECK_EQUAL( side, lane * 32.0f );
		BOOST_CHECK_EQUAL( height, 96.0f );
		// Once behind, redirect through the opponent instead of steering farther away.
		NewBotAI_SaberThrowLaneOffsets( NEWBOTAI_THROW_CUT_THROUGH, lane, 1, &rear, &side, &height );
		BOOST_CHECK_SMALL( rear, 0.001f );
		BOOST_CHECK_SMALL( side, 0.001f );
		BOOST_CHECK_SMALL( height, 0.001f );
	}
	// Overhead candidates still obey the same solid/obstruction checks as side lanes.
	BOOST_CHECK( !NewBotAI_SaberThrowTraceSafe( 1, 0, 0, 0, NEWBOTAI_THROW_BYPASS ) );
	BOOST_CHECK( !NewBotAI_SaberThrowTraceSafe( 0, 0, 1, 0, NEWBOTAI_THROW_REAR ) );
}

BOOST_AUTO_TEST_CASE( saber_throw_routing_hold_survives_lateral_motion_but_is_bounded )
{
	// A planned detour can point away from the target after the legacy first-pass hold.
	BOOST_CHECK( !NewBotAI_SaberThrowHoldProtected( 800, 1, 0 ) );
	BOOST_CHECK( NewBotAI_SaberThrowRoutingHold( 2, NEWBOTAI_THROW_BYPASS, 800 ) );
	BOOST_CHECK( NewBotAI_SaberThrowRoutingHold( 3, NEWBOTAI_THROW_REAR, 800 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowRoutingHold( 1, NEWBOTAI_THROW_BYPASS, 600 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowRoutingHold( 2, NEWBOTAI_THROW_BYPASS, 1800 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowRoutingHold( 3, NEWBOTAI_THROW_REAR, 1500 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowRoutingHold( 3, NEWBOTAI_THROW_REAR, -1 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowRoutingHold( 3, NEWBOTAI_THROW_LAUNCH, 800 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowRoutingHold( 3, NEWBOTAI_THROW_CUT_THROUGH, 800 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowRoutingHold( 3, NEWBOTAI_THROW_RECALL, 800 ) );
}

BOOST_AUTO_TEST_CASE( saber_throw_traces_reject_obstructions_and_front_bypass )
{
	BOOST_CHECK( NewBotAI_SaberThrowTraceSafe( 0, 0, 0, 0, NEWBOTAI_THROW_BYPASS ) );
	BOOST_CHECK( !NewBotAI_SaberThrowTraceSafe( 1, 0, 0, 0, NEWBOTAI_THROW_BYPASS ) );
	BOOST_CHECK( !NewBotAI_SaberThrowTraceSafe( 0, 1, 0, 0, NEWBOTAI_THROW_REAR ) );
	BOOST_CHECK( !NewBotAI_SaberThrowTraceSafe( 0, 0, 1, 0, NEWBOTAI_THROW_CUT_THROUGH ) );
	BOOST_CHECK( !NewBotAI_SaberThrowTraceSafe( 0, 0, 1, 1, NEWBOTAI_THROW_BYPASS ) );
	BOOST_CHECK( !NewBotAI_SaberThrowTraceSafe( 0, 0, 1, 1, NEWBOTAI_THROW_REAR ) );
	BOOST_CHECK( NewBotAI_SaberThrowTraceSafe( 0, 0, 1, 1, NEWBOTAI_THROW_CUT_THROUGH ) );
	BOOST_CHECK( NewBotAI_SaberThrowTraceSafe( 0, 0, 1, 1, NEWBOTAI_THROW_LAUNCH ) );
	BOOST_CHECK( !NewBotAI_SaberThrowTraceSafe( 0, 0, 0, 0, NEWBOTAI_THROW_RECALL ) );
}

BOOST_AUTO_TEST_CASE( saber_throw_recall_request_is_level_bounded_and_safety_sensitive )
{
	BOOST_CHECK( !NewBotAI_SaberThrowRecallDue( 1, 749, 0, 0, 1 ) );
	BOOST_CHECK( NewBotAI_SaberThrowRecallDue( 1, 750, 0, 0, 1 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowRecallDue( 2, 1799, 0, 0, 1 ) );
	BOOST_CHECK( NewBotAI_SaberThrowRecallDue( 2, 1800, 0, 0, 1 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowRecallDue( 3, 1499, 0, 0, 1 ) );
	BOOST_CHECK( NewBotAI_SaberThrowRecallDue( 3, 1500, 0, 0, 1 ) );
	BOOST_CHECK( NewBotAI_SaberThrowRecallDue( 3, 50, 1, 0, 1 ) );
	BOOST_CHECK( NewBotAI_SaberThrowRecallDue( 3, 50, 0, 1, 1 ) );
	BOOST_CHECK( NewBotAI_SaberThrowRecallDue( 3, 50, 0, 0, 0 ) );
}

BOOST_AUTO_TEST_CASE( saber_throw_final_hold_keeps_minimum_and_drainlock_polarity )
{
	BOOST_CHECK( NewBotAI_SaberThrowFinalHoldProtected( 50, 1, 0, 0, 0, 0, 1, 0 ) );
	BOOST_CHECK( NewBotAI_SaberThrowFinalHoldProtected( 599, 1, 0, 0, 0, 0, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowFinalHoldProtected( 50, 0, 1, 1, 0, 0, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowFinalHoldProtected( 50, 0, 1, 0, 1, 0, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowFinalHoldProtected( 50, 0, 1, 0, 0, 1, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowFinalHoldProtected( 50, 0, 1, 0, 0, 0, 0, 0 ) );
	BOOST_CHECK( NewBotAI_SaberThrowFinalHoldProtected( 600, 0, 1, 0, 0, 0, 1, 0 ) );
	BOOST_CHECK( !NewBotAI_SaberThrowFinalHoldProtected( 600, 1, 1, 0, 0, 0, 1, 0 ) );
}

BOOST_AUTO_TEST_CASE( saber_throw_hard_recall_overrides_protected_hold )
{
	const int levelOneDeadline = NewBotAI_SaberThrowRecallDue( 1, 800, 0, 0, 1 );
	BOOST_CHECK( NewBotAI_SaberThrowHoldProtected( 800, 0, 1 ) );
	BOOST_CHECK( levelOneDeadline );
	BOOST_CHECK( !NewBotAI_SaberThrowFinalHoldProtected( 800, 0, 1, 0, 0, 0, 1, levelOneDeadline ) );
	// The minimum hold still protects policy recalls, never an unsafe traced route.
	BOOST_CHECK( NewBotAI_SaberThrowHoldProtected( 550, 0, 1 ) );
	const int unsafeRoute = !NewBotAI_SaberThrowTraceSafe( 0, 0, 1, 0, NEWBOTAI_THROW_BYPASS );
	BOOST_CHECK( !NewBotAI_SaberThrowFinalHoldProtected( 550, 0, 1, 0, 0, 0, 1, unsafeRoute ) );
	// Lost/invalid targets are hard recalls even while the old heading is positive.
	BOOST_CHECK( !NewBotAI_SaberThrowFinalHoldProtected( 550, 0, 1, 0, 0, 0, 1, 1 ) );
	BOOST_CHECK( NewBotAI_SaberThrowFinalHoldProtected( 550, 0, 1, 0, 0, 0, 1, 0 ) );
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
