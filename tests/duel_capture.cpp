#include "g_duel_capture.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(duel_capture)

BOOST_AUTO_TEST_CASE(yaw_wrap_and_reversal_are_measured)
{
	duel_capture_swing_t swing = {};
	BOOST_CHECK(G_DuelCaptureSwingStarted(&swing, 1, 4, 100, 179.0f));
	G_DuelCaptureSampleYaw(&swing, -179.0f);
	BOOST_CHECK_EQUAL(swing.sweep, 2.0f);
	G_DuelCaptureSampleYaw(&swing, 170.0f);
	BOOST_CHECK_EQUAL(swing.sweep, -9.0f);
}

BOOST_AUTO_TEST_CASE(only_accepted_moves_start_and_reset_a_swing)
{
	duel_capture_swing_t swing = {};
	BOOST_CHECK(!G_DuelCaptureSwingStarted(&swing, 0, 0, 100, 10.0f));
	BOOST_CHECK(G_DuelCaptureSwingStarted(&swing, 1, 4, 200, 30.0f));
	BOOST_CHECK(!G_DuelCaptureSwingStarted(&swing, 1, 4, 250, 50.0f));
	BOOST_CHECK_EQUAL(swing.sweep, 20.0f);
	BOOST_CHECK_EQUAL(swing.startTime, 200);
	BOOST_CHECK(G_DuelCaptureSwingStarted(&swing, 1, 5, 300, 60.0f));
	BOOST_CHECK_EQUAL(swing.sweep, 0.0f);
	BOOST_CHECK_EQUAL(swing.startTime, 300);
	BOOST_CHECK(!G_DuelCaptureSwingStarted(&swing, 0, 0, 350, 70.0f));
	G_DuelCaptureSampleYaw(&swing, 100.0f);
	BOOST_CHECK_EQUAL(swing.sweep, 10.0f);
	BOOST_CHECK(G_DuelCaptureSwingStarted(&swing, 1, 5, 400, 110.0f));
	BOOST_CHECK_EQUAL(swing.sweep, 0.0f);
}

BOOST_AUTO_TEST_CASE(stationary_view_does_not_invent_a_sweep)
{
	duel_capture_swing_t swing = {};
	G_DuelCaptureSwingStarted(&swing, 1, 4, 100, 90.0f);
	G_DuelCaptureSwingStarted(&swing, 1, 4, 500, 90.0f);
	BOOST_CHECK_EQUAL(swing.sweep, 0.0f);
	G_DuelCaptureSampleYaw(&swing, -90.0f);
	G_DuelCaptureSampleYaw(&swing, -179.0f);
	G_DuelCaptureSampleYaw(&swing, 90.0f);
	BOOST_CHECK_EQUAL(swing.sweep, -360.0f);
}

BOOST_AUTO_TEST_CASE(damage_totals_measure_resources_not_overkill)
{
	BOOST_CHECK_EQUAL(G_DuelCaptureDamageAmount(100, 80, 20), 40);
	BOOST_CHECK_EQUAL(G_DuelCaptureDamageAmount(10, -90, 5), 15);
	BOOST_CHECK_EQUAL(G_DuelCaptureDamageAmount(100, 100, 5), 5);
	BOOST_CHECK_EQUAL(G_DuelCaptureDamageAmount(100, 120, 0), 0);
	BOOST_CHECK_EQUAL(G_DuelCaptureDamageAmount(10, 1, 0), 9);
	BOOST_CHECK_EQUAL(G_DuelCaptureDamageAmount(0, -10, 0), 0);
}

BOOST_AUTO_TEST_CASE(dead_frames_cannot_create_inputs_but_lethal_damage_is_retained)
{
	BOOST_CHECK(G_DuelCaptureCanObserveInputs(100, 0));
	BOOST_CHECK(!G_DuelCaptureCanObserveInputs(0, 0));
	BOOST_CHECK(!G_DuelCaptureCanObserveInputs(-90, 0));
	BOOST_CHECK(!G_DuelCaptureCanObserveInputs(100, 1));
	BOOST_CHECK_EQUAL(G_DuelCaptureDamageAmount(10, -90, 5), 15);
}

BOOST_AUTO_TEST_CASE(chain_links_require_a_previous_accepted_attack)
{
	duel_capture_swing_t swing = {};
	BOOST_CHECK(!G_DuelCaptureSwingLinked(&swing, 1, 4));
	G_DuelCaptureSwingStarted(&swing, 1, 4, 100, 0.0f);
	BOOST_CHECK(!G_DuelCaptureSwingLinked(&swing, 1, 4));
	BOOST_CHECK(G_DuelCaptureSwingLinked(&swing, 1, 5));
	BOOST_CHECK(!G_DuelCaptureSwingLinked(&swing, 0, 5));
	G_DuelCaptureSwingStarted(&swing, 0, 0, 150, 0.0f);
	BOOST_CHECK(!G_DuelCaptureSwingLinked(&swing, 1, 5));
}

BOOST_AUTO_TEST_CASE(chain_continuity_survives_legal_transitions_without_measuring_their_yaw)
{
	duel_capture_swing_t swing = {};
	G_DuelCaptureSwingStartedWithContinuity(&swing, 1, 0, 4, 100, 170.0f);
	BOOST_CHECK(G_DuelCapturePrepareSwingTransition(&swing, 0, 100, 179.0f));
	BOOST_CHECK_EQUAL(swing.sweep, 9.0f);
	G_DuelCaptureSwingStartedWithContinuity(&swing, 0, 1, 100, 150, 179.0f);
	G_DuelCaptureSwingStartedWithContinuity(&swing, 0, 1, 101, 170, -170.0f);
	BOOST_CHECK_EQUAL(swing.active, 0);
	BOOST_CHECK_EQUAL(swing.chainPending, 1);
	BOOST_CHECK_EQUAL(swing.sweep, 9.0f);
	BOOST_CHECK(G_DuelCaptureSwingLinked(&swing, 1, 5));
	BOOST_CHECK(G_DuelCaptureSwingStartedWithContinuity(&swing, 1, 0, 5, 200, -160.0f));
	BOOST_CHECK_EQUAL(swing.sweep, 0.0f);
}

BOOST_AUTO_TEST_CASE(repeated_direction_chains_are_distinct_accepted_swings)
{
	duel_capture_swing_t swing = {};
	G_DuelCaptureSwingStartedWithContinuity(&swing, 1, 0, 4, 100, 10.0f);
	G_DuelCaptureSwingStartedWithContinuity(&swing, 0, 1, 100, 150, 20.0f);
	BOOST_CHECK(G_DuelCaptureSwingLinked(&swing, 1, 4));
	BOOST_CHECK(G_DuelCaptureSwingStartedWithContinuity(&swing, 1, 0, 4, 200, 30.0f));
	BOOST_CHECK_EQUAL(swing.startTime, 200);
	BOOST_CHECK(!G_DuelCaptureSwingLinked(&swing, 1, 4));
}

BOOST_AUTO_TEST_CASE(return_idle_and_interruption_break_chain_continuity)
{
	duel_capture_swing_t swing = {};
	G_DuelCaptureSwingStartedWithContinuity(&swing, 1, 0, 4, 100, 0.0f);
	G_DuelCaptureSwingStartedWithContinuity(&swing, 0, 1, 100, 150, 0.0f);
	G_DuelCaptureSwingStartedWithContinuity(&swing, 0, 0, 200, 170, 0.0f);
	BOOST_CHECK_EQUAL(swing.chainPending, 0);
	BOOST_CHECK(!G_DuelCaptureSwingLinked(&swing, 1, 5));
	G_DuelCaptureSwingStartedWithContinuity(&swing, 1, 0, 5, 200, 0.0f);
	G_DuelCaptureSwingStartedWithContinuity(&swing, 0, 0, 5, 250, 0.0f);
	BOOST_CHECK_EQUAL(swing.active, 0);
	BOOST_CHECK(!G_DuelCaptureSwingLinked(&swing, 1, 5));
	G_DuelCaptureSwingStartedWithContinuity(&swing, 0, 0, 0, 300, 0.0f);
	G_DuelCaptureSwingStartedWithContinuity(&swing, 0, 1, 100, 350, 0.0f);
	BOOST_CHECK(!G_DuelCaptureSwingLinked(&swing, 1, 4));
}

BOOST_AUTO_TEST_CASE(damage_hook_transition_preserves_old_sweep_for_end_event)
{
	duel_capture_swing_t swing = {};
	G_DuelCaptureSwingStarted(&swing, 1, 4, 100, 179.0f);
	G_DuelCaptureSampleYaw(&swing, -179.0f);
	BOOST_CHECK(G_DuelCapturePrepareSwingTransition(&swing, 1, 5, -169.0f));
	BOOST_CHECK_EQUAL(swing.move, 4);
	BOOST_CHECK_EQUAL(swing.sweep, 12.0f);
	BOOST_CHECK_EQUAL(swing.startTime, 100);
	BOOST_CHECK(G_DuelCaptureSwingStarted(&swing, 1, 5, 200, -169.0f));
	BOOST_CHECK_EQUAL(swing.sweep, 0.0f);
	BOOST_CHECK_EQUAL(swing.move, 5);
}

BOOST_AUTO_TEST_CASE(repeated_lethal_callbacks_cannot_count_multiple_kills)
{
	int recorded = 0;
	BOOST_CHECK(!G_DuelCaptureMarkLethal(10, &recorded));
	BOOST_CHECK_EQUAL(recorded, 0);
	BOOST_CHECK(G_DuelCaptureMarkLethal(-5, &recorded));
	BOOST_CHECK_EQUAL(recorded, 1);
	BOOST_CHECK(!G_DuelCaptureMarkLethal(-10, &recorded));
}

BOOST_AUTO_TEST_CASE(arcade_damage_requires_actual_attacker_and_selected_opponent)
{
	BOOST_CHECK(G_DuelCaptureCreditsOpponent(1, 0, 0, 1, 1));
	BOOST_CHECK(!G_DuelCaptureCreditsOpponent(1, 0, 2, 1, 1));
	BOOST_CHECK(!G_DuelCaptureCreditsOpponent(1, 0, -1, 1, 1));
	BOOST_CHECK(!G_DuelCaptureCreditsOpponent(1, 0, 0, 2, 1));
	BOOST_CHECK(!G_DuelCaptureCreditsOpponent(1, 0, 0, 0, 0));
	BOOST_CHECK(!G_DuelCaptureCreditsOpponent(1, 0, 0, 1, -1));
	BOOST_CHECK(!G_DuelCaptureCreditsOpponent(0, 0, 0, 1, 1));
	// Lethal resource loss still belongs to the target selected before its health reached zero.
	BOOST_CHECK_EQUAL(G_DuelCaptureDamageAmount(10, -90, 5), 15);
	BOOST_CHECK(G_DuelCaptureCreditsOpponent(1, 0, 0, 1, 1));
	BOOST_CHECK_EQUAL(G_DuelCaptureDamageAmount(0, 0, 5), 0);
}

BOOST_AUTO_TEST_CASE(outcome_opponent_scope_requires_identity_and_actor)
{
	BOOST_CHECK(G_DuelCaptureSameOpponent("bot:mediumds", 1, "bot:mediumds", 1));
	BOOST_CHECK(!G_DuelCaptureSameOpponent("bot:mediumds", 1, "bot:mediumds", 2));
	BOOST_CHECK(!G_DuelCaptureSameOpponent("bot:mediumds", 1, "bot:hardls", 1));
	BOOST_CHECK(!G_DuelCaptureSameOpponent(NULL, 1, "bot:mediumds", 1));
	BOOST_CHECK(!G_DuelCaptureSameOpponent("", 1, "", 1));
	BOOST_CHECK(!G_DuelCaptureSameOpponent("bot:mediumds", -1, "bot:mediumds", -1));
	BOOST_CHECK(!G_DuelCaptureSameOpponent("bot:mediumds", 1, NULL, 1));
}

BOOST_AUTO_TEST_CASE(arcade_target_changes_do_not_contaminate_local_outcomes)
{
	duel_capture_outcome_t outcome = {};
	if (G_DuelCaptureSameOpponent("bot:mediumds", 1, "bot:mediumds", 1))
	{
		G_DuelCaptureAccumulateOutcome(&outcome, 0, 10, 1000, 2200, 1, 1100, DUEL_CAPTURE_OUTCOME_DEALT, 40, 0);
		G_DuelCaptureAccumulateOutcome(&outcome, 0, 10, 1000, 2200, 2, 1200, DUEL_CAPTURE_OUTCOME_TAKEN, 10, 0);
	}
	if (G_DuelCaptureSameOpponent("bot:mediumds", 1, "bot:hardls", 2))
		G_DuelCaptureAccumulateOutcome(&outcome, 0, 10, 1000, 2200, 3, 1300, DUEL_CAPTURE_OUTCOME_DEALT, 100, 1);
	if (G_DuelCaptureSameOpponent("bot:mediumds", 1, "bot:mediumds", 2))
		G_DuelCaptureAccumulateOutcome(&outcome, 0, 10, 1000, 2200, 4, 1400, DUEL_CAPTURE_OUTCOME_TAKEN, 100, 0);
	BOOST_CHECK_EQUAL(outcome.dealt, 40);
	BOOST_CHECK_EQUAL(outcome.taken, 10);
	BOOST_CHECK_EQUAL(G_DuelCaptureOutcomeQuality(&outcome), "good");
}

BOOST_AUTO_TEST_CASE(outcomes_exclude_previous_attacks_and_expired_windows)
{
	duel_capture_outcome_t outcome = {};
	G_DuelCaptureAccumulateOutcome(&outcome, 5, 10, 1000, 2200, 4, 1000, DUEL_CAPTURE_OUTCOME_DEALT, 80, 1);
	G_DuelCaptureAccumulateOutcome(&outcome, 5, 10, 1000, 2200, 6, 999, DUEL_CAPTURE_OUTCOME_TAKEN, 80, 0);
	G_DuelCaptureAccumulateOutcome(&outcome, 5, 10, 1000, 2200, 10, 2000, DUEL_CAPTURE_OUTCOME_DEALT, 80, 1);
	G_DuelCaptureAccumulateOutcome(&outcome, 5, 10, 1000, 2200, 8, 2201, DUEL_CAPTURE_OUTCOME_DEALT, 80, 1);
	BOOST_CHECK_EQUAL(outcome.dealt, 0);
	BOOST_CHECK_EQUAL(outcome.taken, 0);
	BOOST_CHECK_EQUAL(outcome.killedEnemy, 0);
	G_DuelCaptureAccumulateOutcome(&outcome, 5, 10, 1000, 2200, 6, 1000, DUEL_CAPTURE_OUTCOME_DEALT, 40, 0);
	G_DuelCaptureAccumulateOutcome(&outcome, 5, 10, 1000, 2200, 9, 2200, DUEL_CAPTURE_OUTCOME_TAKEN, 20, 0);
	BOOST_CHECK_EQUAL(outcome.dealt, 40);
	BOOST_CHECK_EQUAL(outcome.taken, 20);
}

BOOST_AUTO_TEST_CASE(success_aliases_do_not_count_the_same_hit_multiple_times)
{
	duel_capture_outcome_t outcome = {};
	G_DuelCaptureAccumulateOutcome(&outcome, 0, 5, 1000, 2200, 1, 1100, DUEL_CAPTURE_OUTCOME_DEALT, 40, 0);
	G_DuelCaptureAccumulateOutcome(&outcome, 0, 5, 1000, 2200, 2, 1100, DUEL_CAPTURE_OUTCOME_OTHER, 40, 0);
	G_DuelCaptureAccumulateOutcome(&outcome, 0, 5, 1000, 2200, 3, 1100, DUEL_CAPTURE_OUTCOME_OTHER, 40, 0);
	BOOST_CHECK_EQUAL(outcome.dealt, 40);
	BOOST_CHECK_EQUAL(outcome.taken, 0);
	BOOST_CHECK_EQUAL(G_DuelCaptureOutcomeQuality(&outcome), "good");
}

BOOST_AUTO_TEST_CASE(outcome_quality_requires_a_favorable_local_trade)
{
	duel_capture_outcome_t outcome = { 60, 100, 0 };
	BOOST_CHECK_EQUAL(G_DuelCaptureOutcomeQuality(&outcome), "mistake");
	outcome = { 60, 50, 0 };
	BOOST_CHECK_EQUAL(G_DuelCaptureOutcomeQuality(&outcome), "correct");
	outcome = { 10, 15, 1 };
	BOOST_CHECK_EQUAL(G_DuelCaptureOutcomeQuality(&outcome), "bad");
	outcome = { 10, 0, 1 };
	BOOST_CHECK_EQUAL(G_DuelCaptureOutcomeQuality(&outcome), "correct");
	outcome = { 20, 20, 0 };
	BOOST_CHECK_EQUAL(G_DuelCaptureOutcomeQuality(&outcome), "mediocre");
	outcome = { 60, 60, 0 };
	BOOST_CHECK_EQUAL(G_DuelCaptureOutcomeQuality(&outcome), "mediocre");
	outcome = { 10, 10, 1 };
	BOOST_CHECK_EQUAL(G_DuelCaptureOutcomeQuality(&outcome), "mediocre");
	outcome = { 0, 0, 0 };
	BOOST_CHECK_EQUAL(G_DuelCaptureOutcomeQuality(&outcome), "mediocre");
}

BOOST_AUTO_TEST_SUITE_END()
