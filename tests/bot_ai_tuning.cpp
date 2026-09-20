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
}

BOOST_AUTO_TEST_CASE( immediate_flipkick_contact_widens_yaw_tolerance )
{
	BOOST_CHECK_EQUAL( NewBotAI_GetImmediateFlipkickYawTolerance( 0 ), 35.0f );
	BOOST_CHECK_EQUAL( NewBotAI_GetImmediateFlipkickYawTolerance( 1 ), 60.0f );
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

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
