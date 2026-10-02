#include "g_duel_capture.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(duel_capture)

struct capture_test_record
{
	unsigned long long index;
	int time;
	int priority;
};

static int CaptureTestPriority(const void *record)
{
	return static_cast<const capture_test_record *>(record)->priority;
}

static void *CaptureTestFailedAllocation(void *, size_t)
{
	return NULL;
}

BOOST_AUTO_TEST_CASE(full_95_second_duel_survives_old_128_event_limit)
{
	duel_capture_storage_t storage = {};
	capture_test_record *records = NULL;
	int count = 0;
	for (int time = 0; time <= 95000; time += 100)
	{
		records = static_cast<capture_test_record *>(G_DuelCaptureReserve(records, &count,
			sizeof(*records), &storage, 8192, CaptureTestPriority));
		BOOST_REQUIRE(records);
		records[count++] = { ++storage.total, time, time % 1000 ? 0 : 2 };
	}
	BOOST_CHECK_EQUAL(count, 951);
	BOOST_CHECK_EQUAL(records[count - 1].time, 95000);
	BOOST_CHECK_EQUAL(storage.dropped, 0);
	BOOST_CHECK_EQUAL(storage.compactions, 0);
	free(records);
}

BOOST_AUTO_TEST_CASE(bounded_capture_continues_past_16_bit_indices_and_keeps_late_combat)
{
	duel_capture_storage_t storage = {};
	capture_test_record *records = NULL;
	int count = 0;
	for (int i = 1; i <= 100000; ++i)
	{
		records = static_cast<capture_test_record *>(G_DuelCaptureReserve(records, &count,
			sizeof(*records), &storage, 8192, CaptureTestPriority));
		BOOST_REQUIRE(records);
		records[count++] = { ++storage.total, i * 10, i % 10 ? 0 : 2 };
		BOOST_REQUIRE(count <= 8192);
	}
	BOOST_CHECK_EQUAL(storage.capacity, 8192);
	BOOST_CHECK_EQUAL(storage.total, 100000);
	BOOST_CHECK_EQUAL(storage.total, storage.dropped + count);
	BOOST_CHECK_EQUAL(records[0].index, 1);
	BOOST_CHECK_EQUAL(records[count - 1].index, 100000);
	BOOST_CHECK_EQUAL(records[count - 1].priority, 2);
	BOOST_CHECK(storage.compactions > 0);
	for (int i = 1; i < count; ++i)
	{
		BOOST_REQUIRE(records[i].index > records[i - 1].index);
		BOOST_REQUIRE(records[i].time > records[i - 1].time);
	}
	free(records);
}

BOOST_AUTO_TEST_CASE(compaction_prioritizes_damage_and_keeps_recent_records)
{
	capture_test_record records[16] = {};
	duel_capture_storage_t storage = {};
	for (int i = 0; i < 16; ++i)
		records[i] = { static_cast<unsigned long long>(i + 1), i * 100, i == 3 || i == 7 ? 2 : 0 };
	int count = G_DuelCaptureCompact(records, 16, sizeof(records[0]), &storage, CaptureTestPriority);
	BOOST_CHECK_EQUAL(count, 8);
	BOOST_CHECK_EQUAL(records[0].index, 1);
	BOOST_CHECK_EQUAL(storage.dropped, 8);
	BOOST_CHECK_EQUAL(storage.criticalDropped, 0);
	BOOST_CHECK_EQUAL(records[1].index, 4);
	BOOST_CHECK_EQUAL(records[2].index, 8);
	for (int i = 4; i < count; ++i)
		BOOST_CHECK_EQUAL(records[i].index, static_cast<unsigned long long>(i + 9));
}

BOOST_AUTO_TEST_CASE(all_critical_overflow_is_explicit_and_time_sampled)
{
	capture_test_record records[16] = {};
	duel_capture_storage_t storage = {};
	for (int i = 0; i < 16; ++i)
		records[i] = { static_cast<unsigned long long>(i + 1), i * 100, 2 };
	int count = G_DuelCaptureCompact(records, 16, sizeof(records[0]), &storage, CaptureTestPriority);
	BOOST_CHECK_EQUAL(count, 8);
	BOOST_CHECK_EQUAL(storage.criticalDropped, 8);
	BOOST_CHECK_EQUAL(records[0].index, 1);
	BOOST_CHECK(records[1].index < 8);
	BOOST_CHECK(records[2].index >= 8);
	BOOST_CHECK_EQUAL(records[count - 1].index, 16);
}

BOOST_AUTO_TEST_CASE(allocation_failure_keeps_existing_buffer_and_allows_late_append)
{
	duel_capture_storage_t storage = {};
	int count = 0;
	void *records = G_DuelCaptureReserveWithAllocator(NULL, &count, sizeof(capture_test_record),
		&storage, 8192, CaptureTestPriority, CaptureTestFailedAllocation);
	BOOST_CHECK(!records);
	BOOST_CHECK_EQUAL(storage.capacity, 0);
	BOOST_CHECK_EQUAL(storage.allocationFailures, 1);
	capture_test_record existing[128] = {};
	for (int i = 0; i < 128; ++i)
		existing[i] = { static_cast<unsigned long long>(i + 1), i, 2 };
	storage.capacity = count = 128;
	records = G_DuelCaptureReserveWithAllocator(existing, &count, sizeof(existing[0]),
		&storage, 8192, CaptureTestPriority, CaptureTestFailedAllocation);
	BOOST_CHECK(records == existing);
	BOOST_CHECK_EQUAL(count, 64);
	BOOST_CHECK_EQUAL(storage.capacity, 128);
	BOOST_CHECK_EQUAL(storage.allocationFailures, 2);
	existing[count++] = { 129, 128, 2 };
	BOOST_CHECK_EQUAL(existing[count - 1].index, 129);
}

BOOST_AUTO_TEST_CASE(outcomes_use_original_indices_after_compaction_without_16_bit_wrap)
{
	duel_capture_outcome_t outcome = {};
	G_DuelCaptureAccumulateOutcome(&outcome, 70000, 70010, 1000, 2200,
		70002, 1100, DUEL_CAPTURE_OUTCOME_DEALT, 40, 0);
	G_DuelCaptureAccumulateOutcome(&outcome, 70000, 70010, 1000, 2200,
		70009, 1200, DUEL_CAPTURE_OUTCOME_TAKEN, 10, 0);
	BOOST_CHECK_EQUAL(outcome.dealt, 40);
	BOOST_CHECK_EQUAL(outcome.taken, 10);
}

BOOST_AUTO_TEST_CASE(missing_records_make_outcomes_uncertain_not_false_misses)
{
	BOOST_CHECK(!G_DuelCaptureHasIndexGap(65535, 65536));
	BOOST_CHECK(!G_DuelCaptureHasIndexGap(70000, 70001));
	BOOST_CHECK(G_DuelCaptureHasIndexGap(70000, 70002));
	BOOST_CHECK(G_DuelCaptureHasIndexGap(70000, 65535));
}

BOOST_AUTO_TEST_CASE(any_record_loss_disables_outcome_and_sequence_ranking)
{
	duel_capture_storage_t storage = {};
	BOOST_CHECK(G_DuelCaptureCanRankOutcomes(&storage));
	storage.dropped = 1;
	BOOST_CHECK(!G_DuelCaptureCanRankOutcomes(&storage));
	storage.criticalDropped = 1;
	BOOST_CHECK(!G_DuelCaptureCanRankOutcomes(&storage));
	storage.dropped = 0;
	BOOST_CHECK(!G_DuelCaptureCanRankOutcomes(&storage));
	storage.criticalDropped = 0;
	storage.allocationFailures = 1;
	BOOST_CHECK(G_DuelCaptureCanRankOutcomes(&storage));
}

BOOST_AUTO_TEST_CASE(long_capture_keeps_aggregates_and_tail_but_never_coaches_from_lost_damage)
{
	duel_capture_storage_t storage = {};
	capture_test_record *records = NULL;
	int count = 0, totalDamageDealt = 0;
	duel_capture_outcome_t favorable = { 60, 0, 1 };
	char goodSequence[32] = "clean_counter";
	char badSequence[32] = "forced_entry";
	BOOST_CHECK_EQUAL(G_DuelCaptureRankedOutcomeQuality(&storage, &favorable), "correct");
	G_DuelCaptureSuppressSequenceRanking(&storage, goodSequence, badSequence);
	BOOST_CHECK_EQUAL(goodSequence, "clean_counter");
	BOOST_CHECK_EQUAL(badSequence, "forced_entry");
	for (int i = 1; i <= 20000; ++i)
	{
		records = static_cast<capture_test_record *>(G_DuelCaptureReserve(records, &count,
			sizeof(*records), &storage, 8192, CaptureTestPriority));
		BOOST_REQUIRE(records);
		records[count++] = { ++storage.total, i * 10, 2 };
		totalDamageDealt += 3;
	}
	BOOST_CHECK_EQUAL(totalDamageDealt, 60000);
	BOOST_CHECK_EQUAL(storage.total, storage.dropped + count);
	BOOST_CHECK_EQUAL(storage.dropped, storage.criticalDropped);
	BOOST_CHECK(storage.criticalDropped > 0);
	BOOST_CHECK_EQUAL(records[count - 1].index, 20000);
	BOOST_CHECK_EQUAL(records[count - 1].time, 200000);
	BOOST_CHECK_EQUAL(G_DuelCaptureRankedOutcomeQuality(&storage, &favorable), "unknown");
	G_DuelCaptureSuppressSequenceRanking(&storage, goodSequence, badSequence);
	BOOST_CHECK_EQUAL(goodSequence, "");
	BOOST_CHECK_EQUAL(badSequence, "");
	free(records);
}

BOOST_AUTO_TEST_CASE(finishing_transfers_ownership_without_freeing_events_before_persistence)
{
	struct test_runtime
	{
		capture_test_record *records;
		duel_capture_storage_t storage;
		int active;
		int count;
	};
	test_runtime source = {}, copy = {};
	source.active = 1;
	source.records = static_cast<capture_test_record *>(G_DuelCaptureReserve(NULL, &source.count,
		sizeof(capture_test_record), &source.storage, 8192, CaptureTestPriority));
	BOOST_REQUIRE(source.records);
	source.records[source.count++] = { 1, 95000, 2 };
	source.storage.total = 1;
	capture_test_record *owned = source.records;
	G_DuelCaptureMoveRuntime(&copy, &source, sizeof(source));
	BOOST_CHECK(!source.records);
	BOOST_CHECK_EQUAL(source.count, 0);
	BOOST_CHECK_EQUAL(source.active, 0);
	BOOST_CHECK_EQUAL(source.storage.capacity, 0);
	BOOST_CHECK(copy.records == owned);
	G_DuelCaptureClearRuntime(&source, sizeof(source), source.records);
	BOOST_CHECK_EQUAL(copy.count, 1);
	BOOST_CHECK_EQUAL(copy.records[0].time, 95000);
	BOOST_CHECK_EQUAL(copy.storage.total, 1);
	// A new capture on the cleared slot cannot invalidate the persistence copy.
	source.records = static_cast<capture_test_record *>(G_DuelCaptureReserve(NULL, &source.count,
		sizeof(capture_test_record), &source.storage, 8192, CaptureTestPriority));
	BOOST_REQUIRE(source.records);
	BOOST_CHECK(source.records != copy.records);
	G_DuelCaptureClearRuntime(&source, sizeof(source), source.records);
	BOOST_CHECK_EQUAL(copy.records[0].index, 1);
	G_DuelCaptureClearRuntime(&copy, sizeof(copy), copy.records);
	BOOST_CHECK(!copy.records);
	G_DuelCaptureClearRuntime(&copy, sizeof(copy), copy.records);
	BOOST_CHECK_EQUAL(copy.storage.capacity, 0);
}

BOOST_AUTO_TEST_CASE(small_capacity_and_failed_growth_do_not_overrun_or_compact_tiny_buffers)
{
	for (int maximum = 1; maximum <= 3; ++maximum)
	{
		duel_capture_storage_t storage = {};
		int count = 0;
		capture_test_record *records = static_cast<capture_test_record *>(G_DuelCaptureReserve(NULL,
			&count, sizeof(*records), &storage, maximum, CaptureTestPriority));
		BOOST_REQUIRE(records);
		BOOST_CHECK_EQUAL(storage.capacity, maximum);
		for (int i = 0; i < maximum; ++i)
			records[count++] = { static_cast<unsigned long long>(i + 1), i, 2 };
		void *unchanged = G_DuelCaptureReserve(records, &count, sizeof(*records),
			&storage, maximum, CaptureTestPriority);
		BOOST_CHECK(unchanged == records);
		BOOST_CHECK_EQUAL(count, maximum);
		BOOST_CHECK_EQUAL(storage.compactions, 0);
		unchanged = G_DuelCaptureReserveWithAllocator(records, &count, sizeof(*records),
			&storage, 8192, CaptureTestPriority, CaptureTestFailedAllocation);
		BOOST_CHECK(unchanged == records);
		BOOST_CHECK_EQUAL(count, maximum);
		BOOST_CHECK_EQUAL(storage.allocationFailures, 1);
		BOOST_CHECK_EQUAL(storage.compactions, 0);
		BOOST_CHECK_EQUAL(records[count - 1].index, static_cast<unsigned long long>(maximum));
		free(records);
	}
	duel_capture_storage_t storage = {};
	int count = 0;
	BOOST_CHECK(!G_DuelCaptureReserve(NULL, &count, sizeof(capture_test_record),
		&storage, 0, CaptureTestPriority));
	BOOST_CHECK_EQUAL(storage.capacity, 0);
	BOOST_CHECK(!G_DuelCaptureReserve(NULL, &count, sizeof(capture_test_record),
		&storage, -1, CaptureTestPriority));
	BOOST_CHECK_EQUAL(storage.capacity, 0);
}

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
