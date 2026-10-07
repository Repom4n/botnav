#include <string>
#include "g_bot_learning.h"
#include "g_account.h"
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

	struct LearningDatabase
	{
		sqlite3 *db = NULL;
		LearningDatabase(const char *path = ":memory:")
		{
			BOOST_REQUIRE_EQUAL(sqlite3_open_v2(path, &db,
				SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI, NULL), SQLITE_OK);
			Execute("CREATE TABLE LocalBotLearnedSequence(samples INTEGER);"
				"INSERT INTO LocalBotLearnedSequence VALUES(42);"
				"CREATE TABLE LocalBotLearnedEvidence(id INTEGER PRIMARY KEY, "
				"summary_id INTEGER, participant_key TEXT, session_id TEXT DEFAULT '');"
				"INSERT INTO LocalBotLearnedEvidence(summary_id,participant_key) VALUES(1,'human');"
				"CREATE TABLE LocalDuelTrackImport(source TEXT);"
				"INSERT INTO LocalDuelTrackImport VALUES('legacy');"
				"CREATE TABLE LocalAccount(id INTEGER PRIMARY KEY AUTOINCREMENT, value INTEGER);"
				"INSERT INTO LocalAccount VALUES(1,123);");
			for (const char *table : g_trackedDuelTableNames)
				Execute("CREATE TABLE " + std::string(table) +
					"(id INTEGER PRIMARY KEY, summary_id INTEGER, participant_key TEXT, session_id TEXT);"
					"INSERT INTO " + table + " VALUES(1,1,'human','old-session');");
		}
		~LearningDatabase()
		{
			BOOST_CHECK_EQUAL(sqlite3_close(db), SQLITE_OK);
		}
		void Execute(const std::string &sql)
		{
			BOOST_REQUIRE_EQUAL(sqlite3_exec(db, sql.c_str(), NULL, NULL, NULL), SQLITE_OK);
		}
		int Scalar(const char *sql)
		{
			sqlite3_stmt *stmt = NULL;
			BOOST_REQUIRE_EQUAL(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), SQLITE_OK);
			BOOST_REQUIRE_EQUAL(sqlite3_step(stmt), SQLITE_ROW);
			const int value = sqlite3_column_int(stmt, 0);
			BOOST_REQUIRE_EQUAL(sqlite3_finalize(stmt), SQLITE_OK);
			return value;
		}
	};

	struct DrainJob
	{
		int stage = 0, cursor = 0, attempts = 0, completed = 0;
		sqlite3_int64 summaryId = 0;
		sqlite3 *db = NULL;
	};

	int ProgressDrainStep(void *data)
	{
		DrainJob *job = static_cast<DrainJob *>(data);
		job->attempts++;
		job->cursor++;
		return 1;
	}

	int SQLiteDrainStep(void *data)
	{
		DrainJob *job = static_cast<DrainJob *>(data);
		job->attempts++;
		const int status = G_BeginTrackedPersistTransaction(job->db);
		if (G_TrackedPersistRetryable(status))
			return 1;
		BOOST_REQUIRE_EQUAL(status, SQLITE_OK);
		BOOST_REQUIRE_EQUAL(sqlite3_exec(job->db, "INSERT INTO LocalDuelTrackSummary(id) VALUES(2)",
			NULL, NULL, NULL), SQLITE_OK);
		job->stage = 1;
		job->summaryId = 2;
		BOOST_REQUIRE_EQUAL(G_FinishTrackedPersistTransaction(job->db, SQLITE_OK,
			&job->stage, &job->cursor, &job->summaryId, 0, 0, 0), SQLITE_OK);
		job->completed = 1;
		return 0;
	}

	int drainTestClock;
	int DrainTestMilliseconds()
	{
		return drainTestClock++;
	}
}

BOOST_AUTO_TEST_SUITE( bot_learning )

BOOST_AUTO_TEST_CASE(duel_reset_preserves_learning_and_distinguishes_reused_summary_ids)
{
	LearningDatabase database;
	int rows = -1;
	BOOST_REQUIRE_EQUAL(G_ResetTrackingTables(database.db, 0, &rows), SQLITE_OK);
	database.Execute("VACUUM;");
	BOOST_CHECK_EQUAL(rows, 5);
	for (const char *table : g_trackedDuelTableNames)
		BOOST_CHECK_EQUAL(database.Scalar(("SELECT COUNT(*) FROM " + std::string(table)).c_str()), 0);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT samples FROM LocalBotLearnedSequence"), 42);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalBotLearnedEvidence WHERE id=1 "
		"AND summary_id=1 AND session_id='old-session'"), 1);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalDuelTrackImport"), 0);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT value FROM LocalAccount"), 123);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT seq FROM sqlite_sequence WHERE name='LocalAccount'"), 1);
	database.Execute("INSERT INTO LocalDuelTrackSummary VALUES(1,1,'human','new-session');"
		"INSERT INTO LocalDuelTrackEvent VALUES(1,1,'human','new-session');"
		"INSERT INTO LocalBotLearnedEvidence(summary_id,participant_key,session_id) "
		"VALUES(1,'human','new-session');");
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(DISTINCT id) FROM LocalBotLearnedEvidence"), 2);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(DISTINCT session_id) FROM LocalBotLearnedEvidence"), 2);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT id FROM LocalBotLearnedEvidence WHERE session_id='new-session'"), 2);
	BOOST_REQUIRE_EQUAL(G_ResetTrackingTables(database.db, 0, &rows), SQLITE_OK);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(DISTINCT session_id) FROM LocalBotLearnedEvidence"), 2);
}

BOOST_AUTO_TEST_CASE(legacy_learning_evidence_gets_stable_provenance_without_events)
{
	LearningDatabase database;
	database.Execute("DELETE FROM LocalDuelTrackEvent;"
		"INSERT INTO LocalBotLearnedEvidence(summary_id,participant_key) VALUES(2,'other');");
	int rows;
	BOOST_REQUIRE_EQUAL(G_ResetTrackingTables(database.db, 0, &rows), SQLITE_OK);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalBotLearnedEvidence "
		"WHERE session_id='legacy:' || summary_id"), 2);
	BOOST_REQUIRE_EQUAL(G_ResetTrackingTables(database.db, 0, &rows), SQLITE_OK);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(DISTINCT session_id) FROM LocalBotLearnedEvidence"), 2);
}

BOOST_AUTO_TEST_CASE(explicit_learning_reset_clears_only_learning_tables)
{
	LearningDatabase database;
	int rows = -1;
	BOOST_REQUIRE_EQUAL(G_ResetTrackingTables(database.db, 1, &rows), SQLITE_OK);
	BOOST_CHECK_EQUAL(rows, 2);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalBotLearnedSequence"), 0);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalBotLearnedEvidence"), 0);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT value FROM LocalAccount"), 123);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalDuelTrackImport"), 1);
	for (const char *table : g_trackedDuelTableNames)
		BOOST_CHECK_EQUAL(database.Scalar(("SELECT COUNT(*) FROM " + std::string(table)).c_str()), 1);
	BOOST_REQUIRE_EQUAL(G_ResetTrackingTables(database.db, 1, &rows), SQLITE_OK);
	BOOST_CHECK_EQUAL(rows, 0);
}

BOOST_AUTO_TEST_CASE(learning_reset_rolls_back_both_tables_on_failure)
{
	LearningDatabase database;
	database.Execute("CREATE TRIGGER deny_evidence_reset BEFORE DELETE ON LocalBotLearnedEvidence "
		"BEGIN SELECT RAISE(ABORT,'test failure'); END;");
	int rows = -1;
	BOOST_CHECK_NE(G_ResetTrackingTables(database.db, 1, &rows), SQLITE_OK);
	BOOST_CHECK_EQUAL(rows, 0);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT samples FROM LocalBotLearnedSequence"), 42);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalBotLearnedEvidence"), 1);
	BOOST_CHECK(sqlite3_get_autocommit(database.db));
}

BOOST_AUTO_TEST_CASE(duel_reset_rolls_back_tracking_and_provenance_on_failure)
{
	LearningDatabase database;
	database.Execute("CREATE TRIGGER deny_event_reset BEFORE DELETE ON LocalDuelTrackEvent "
		"BEGIN SELECT RAISE(ABORT,'test failure'); END;");
	int rows = -1;
	BOOST_CHECK_NE(G_ResetTrackingTables(database.db, 0, &rows), SQLITE_OK);
	BOOST_CHECK_EQUAL(rows, 0);
	for (const char *table : g_trackedDuelTableNames)
		BOOST_CHECK_EQUAL(database.Scalar(("SELECT COUNT(*) FROM " + std::string(table)).c_str()), 1);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalBotLearnedEvidence WHERE session_id=''"), 1);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT samples FROM LocalBotLearnedSequence"), 42);
}

BOOST_AUTO_TEST_CASE(reset_refuses_to_interfere_with_an_existing_transaction)
{
	LearningDatabase database;
	database.Execute("BEGIN IMMEDIATE; UPDATE LocalBotLearnedSequence SET samples=43;");
	int rows = -1;
	BOOST_CHECK_EQUAL(G_ResetTrackingTables(database.db, 1, &rows), SQLITE_BUSY);
	BOOST_CHECK_EQUAL(rows, 0);
	BOOST_CHECK(!sqlite3_get_autocommit(database.db));
	BOOST_CHECK_EQUAL(database.Scalar("SELECT samples FROM LocalBotLearnedSequence"), 43);
	database.Execute("ROLLBACK;");
	BOOST_CHECK_EQUAL(database.Scalar("SELECT samples FROM LocalBotLearnedSequence"), 42);
}

BOOST_AUTO_TEST_CASE(learning_reset_handles_a_writer_lock_without_changing_data)
{
	sqlite3 *writer = NULL, *reset = NULL;
	const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI;
	const char *uri = "file:learning-reset-lock?mode=memory&cache=shared";
	BOOST_REQUIRE_EQUAL(sqlite3_open_v2(uri, &writer, flags, NULL), SQLITE_OK);
	BOOST_REQUIRE_EQUAL(sqlite3_open_v2(uri, &reset, flags, NULL), SQLITE_OK);
	BOOST_REQUIRE_EQUAL(sqlite3_exec(writer,
		"CREATE TABLE LocalBotLearnedSequence(samples INTEGER);"
		"CREATE TABLE LocalBotLearnedEvidence(id INTEGER PRIMARY KEY);"
		"INSERT INTO LocalBotLearnedSequence VALUES(42);"
		"INSERT INTO LocalBotLearnedEvidence VALUES(1); BEGIN IMMEDIATE;",
		NULL, NULL, NULL), SQLITE_OK);
	int rows = -1;
	const int result = G_ResetTrackingTables(reset, 1, &rows);
	BOOST_CHECK(result == SQLITE_BUSY || result == SQLITE_LOCKED);
	BOOST_CHECK_EQUAL(rows, 0);
	BOOST_CHECK(sqlite3_get_autocommit(reset));
	BOOST_REQUIRE_EQUAL(sqlite3_exec(writer, "COMMIT", NULL, NULL, NULL), SQLITE_OK);
	BOOST_REQUIRE_EQUAL(G_ResetTrackingTables(reset, 1, &rows), SQLITE_OK);
	BOOST_CHECK_EQUAL(rows, 2);
	BOOST_CHECK_EQUAL(sqlite3_close(reset), SQLITE_OK);
	BOOST_CHECK_EQUAL(sqlite3_close(writer), SQLITE_OK);
}

BOOST_AUTO_TEST_CASE(pending_save_acquires_writer_lock_before_advancing_progress)
{
	const char *uri = "file:pending-save-writer?mode=memory&cache=shared";
	LearningDatabase database(uri);
	sqlite3 *writer = NULL;
	BOOST_REQUIRE_EQUAL(sqlite3_open_v2(uri, &writer,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI, NULL), SQLITE_OK);
	BOOST_REQUIRE_EQUAL(sqlite3_exec(writer, "BEGIN IMMEDIATE", NULL, NULL, NULL), SQLITE_OK);
	const int status = G_BeginTrackedPersistTransaction(database.db);
	BOOST_CHECK(G_TrackedPersistRetryable(status));
	BOOST_CHECK(sqlite3_get_autocommit(database.db));
	BOOST_REQUIRE_EQUAL(sqlite3_exec(writer, "COMMIT", NULL, NULL, NULL), SQLITE_OK);
	int stage = 1, cursor = 0;
	sqlite3_int64 summaryId = 2;
	BOOST_REQUIRE_EQUAL(G_BeginTrackedPersistTransaction(database.db), SQLITE_OK);
	database.Execute("INSERT INTO LocalDuelTrackSummary(id) VALUES(2);");
	BOOST_REQUIRE_EQUAL(G_FinishTrackedPersistTransaction(database.db, SQLITE_OK,
		&stage, &cursor, &summaryId, 0, 0, 0), SQLITE_OK);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalDuelTrackSummary WHERE id=2"), 1);
	BOOST_CHECK_EQUAL(stage, 1);
	BOOST_CHECK_EQUAL(sqlite3_close(writer), SQLITE_OK);
}

BOOST_AUTO_TEST_CASE(pending_save_later_lock_failure_rolls_back_and_restores_retry_progress)
{
	const char *uri = "file:pending-save-stage?mode=memory&cache=shared";
	LearningDatabase database(uri);
	sqlite3 *reader = NULL;
	BOOST_REQUIRE_EQUAL(sqlite3_open_v2(uri, &reader,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI, NULL), SQLITE_OK);
	BOOST_REQUIRE_EQUAL(sqlite3_exec(reader, "BEGIN; SELECT * FROM LocalBotLearnedEvidence",
		NULL, NULL, NULL), SQLITE_OK);
	BOOST_REQUIRE_EQUAL(G_BeginTrackedPersistTransaction(database.db), SQLITE_OK);
	database.Execute("INSERT INTO LocalDuelTrackSummary(id) VALUES(2);");
	int stage = 2, cursor = 200;
	sqlite3_int64 summaryId = 2;
	const int status = sqlite3_exec(database.db,
		"INSERT INTO LocalBotLearnedEvidence(summary_id,participant_key,session_id) "
		"VALUES(2,'human','retry-session')", NULL, NULL, NULL);
	BOOST_REQUIRE(G_TrackedPersistRetryable(status));
	BOOST_CHECK(G_TrackedPersistRetryable(G_FinishTrackedPersistTransaction(database.db, status,
		&stage, &cursor, &summaryId, 0, 0, 0)));
	BOOST_CHECK_EQUAL(stage, 0);
	BOOST_CHECK_EQUAL(cursor, 0);
	BOOST_CHECK_EQUAL(summaryId, 0);
	BOOST_CHECK(sqlite3_get_autocommit(database.db));
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalDuelTrackSummary WHERE id=2"), 0);
	BOOST_REQUIRE_EQUAL(sqlite3_exec(reader, "COMMIT", NULL, NULL, NULL), SQLITE_OK);
	BOOST_REQUIRE_EQUAL(G_BeginTrackedPersistTransaction(database.db), SQLITE_OK);
	database.Execute("INSERT INTO LocalDuelTrackSummary(id) VALUES(2);"
		"INSERT INTO LocalBotLearnedEvidence(summary_id,participant_key,session_id) "
		"VALUES(2,'human','retry-session');");
	stage = 2;
	cursor = 200;
	summaryId = 2;
	BOOST_REQUIRE_EQUAL(G_FinishTrackedPersistTransaction(database.db, SQLITE_OK,
		&stage, &cursor, &summaryId, 0, 0, 0), SQLITE_OK);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalDuelTrackSummary WHERE id=2"), 1);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalBotLearnedEvidence WHERE summary_id=2"), 1);
	BOOST_CHECK_EQUAL(stage, 2);
	BOOST_CHECK_EQUAL(cursor, 200);
	BOOST_CHECK_EQUAL(summaryId, 2);
	BOOST_CHECK_EQUAL(sqlite3_close(reader), SQLITE_OK);
}

BOOST_AUTO_TEST_CASE(pending_save_recognizes_extended_retryable_status_codes)
{
	BOOST_CHECK(G_TrackedPersistRetryable(SQLITE_BUSY | (1 << 8)));
	BOOST_CHECK(G_TrackedPersistRetryable(SQLITE_LOCKED | (1 << 8)));
	BOOST_CHECK(!G_TrackedPersistRetryable(SQLITE_CONSTRAINT));
}

BOOST_AUTO_TEST_CASE(blocked_drain_yields_after_one_attempt_and_retains_job_for_retry)
{
	const char *uri = "file:pending-save-drain?mode=memory&cache=shared";
	LearningDatabase database(uri);
	sqlite3 *writer = NULL;
	BOOST_REQUIRE_EQUAL(sqlite3_open_v2(uri, &writer,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI, NULL), SQLITE_OK);
	BOOST_REQUIRE_EQUAL(sqlite3_exec(writer, "BEGIN IMMEDIATE", NULL, NULL, NULL), SQLITE_OK);
	DrainJob job;
	job.db = database.db;
	BOOST_CHECK_EQUAL(G_DrainTrackedPersistJob(&job, SQLiteDrainStep,
		&job.stage, &job.cursor, &job.summaryId, 128, NULL, 0), 0);
	BOOST_CHECK_EQUAL(job.attempts, 1);
	BOOST_CHECK_EQUAL(job.completed, 0);
	BOOST_CHECK_EQUAL(job.stage, 0);
	BOOST_CHECK_EQUAL(job.cursor, 0);
	BOOST_CHECK_EQUAL(job.summaryId, 0);
	BOOST_REQUIRE_EQUAL(sqlite3_exec(writer, "COMMIT", NULL, NULL, NULL), SQLITE_OK);
	BOOST_CHECK_EQUAL(G_DrainTrackedPersistJob(&job, SQLiteDrainStep,
		&job.stage, &job.cursor, &job.summaryId, 128, NULL, 0), 1);
	BOOST_CHECK_EQUAL(job.attempts, 2);
	BOOST_CHECK_EQUAL(job.completed, 1);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalDuelTrackSummary WHERE id=2"), 1);
	BOOST_CHECK_EQUAL(sqlite3_close(writer), SQLITE_OK);
}

BOOST_AUTO_TEST_CASE(drain_step_budget_is_bounded_even_when_job_keeps_progressing)
{
	DrainJob job;
	BOOST_CHECK_EQUAL(G_DrainTrackedPersistJob(&job, ProgressDrainStep,
		&job.stage, &job.cursor, &job.summaryId, 4, NULL, 0), 0);
	BOOST_CHECK_EQUAL(job.attempts, 4);
	BOOST_CHECK_EQUAL(job.cursor, 4);
	BOOST_CHECK_EQUAL(job.completed, 0);
}

BOOST_AUTO_TEST_CASE(drain_wall_clock_budget_retains_unfinished_work)
{
	DrainJob job;
	drainTestClock = 0;
	BOOST_CHECK_EQUAL(G_DrainTrackedPersistJob(&job, ProgressDrainStep,
		&job.stage, &job.cursor, &job.summaryId, 128, DrainTestMilliseconds, 3), 0);
	BOOST_CHECK_EQUAL(job.attempts, 2);
	BOOST_CHECK_EQUAL(job.cursor, 2);
	BOOST_CHECK_EQUAL(job.completed, 0);
}

BOOST_AUTO_TEST_CASE(full_queue_never_reuses_an_existing_job_slot)
{
	BOOST_CHECK_EQUAL(G_TrackedPersistQueueSlot(3, 8, 8), -1);
	BOOST_CHECK_EQUAL(G_TrackedPersistQueueSlot(3, 9, 8), -1);
	BOOST_CHECK_EQUAL(G_TrackedPersistQueueSlot(3, 7, 8), 2);
	BOOST_CHECK_EQUAL(G_TrackedPersistQueueSlot(3, 0, 8), 3);
	BOOST_CHECK_EQUAL(G_TrackedPersistQueueSlot(3, 0, 0), -1);
}

BOOST_AUTO_TEST_CASE(terminal_snapshot_cleanup_releases_both_buffers_without_changing_learning)
{
	LearningDatabase database;
	struct Snapshot {
		int stage;
		char *winnerEvents, *loserEvents;
	} job = { 5, static_cast<char *>(malloc(64)), static_cast<char *>(malloc(128)) };
	BOOST_REQUIRE(job.winnerEvents != NULL);
	BOOST_REQUIRE(job.loserEvents != NULL);
	memset(job.winnerEvents, 1, 64);
	memset(job.loserEvents, 2, 128);
	G_ReleaseTrackedPersistSnapshots(&job, sizeof(job), job.winnerEvents, job.loserEvents);
	BOOST_CHECK_EQUAL(job.stage, 0);
	BOOST_CHECK(job.winnerEvents == NULL);
	BOOST_CHECK(job.loserEvents == NULL);
	G_ReleaseTrackedPersistSnapshots(&job, sizeof(job), job.winnerEvents, job.loserEvents);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT samples FROM LocalBotLearnedSequence"), 42);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalBotLearnedEvidence"), 1);
	BOOST_CHECK_EQUAL(database.Scalar("SELECT COUNT(*) FROM LocalDuelTrackSummary"), 1);
}

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

BOOST_AUTO_TEST_CASE(baked_baseline_keeps_last_learned_saber_and_force_preferences)
{
	const int base = BotLearn_ContextKey(150, 150, 100, 100, 0, 1, 1);
	const int saber = BotLearn_ContextFooting(BotLearn_ContextSafety(base, 0, 0, 0, 0, 0, 0, 0),
		BOTLEARN_FOOTING_ADVANCE);
	const int force = BotLearn_ContextFooting(BotLearn_ContextSafety(base, 1, 0, 0, 0, 0, 0, 0),
		BOTLEARN_FOOTING_ADVANCE);

	// Saber-only: opening fan chains (idle -> swing -> swing) were the strongest human result.
	BOOST_CHECK_GT(BotLearn_BaselineBonus(saber, BOTLEARN_TOK_IDLE, BOTLEARN_TOK_SWING,
		BOTLEARN_BASELINE_FOLLOW_ANY, 1.0f, 7.0f), 10);
	BOOST_CHECK_GT(BotLearn_BaselineBonus(saber, BOTLEARN_TOK_IDLE, BOTLEARN_TOK_SWING,
		BOTLEARN_TOK_SWING, 1.0f, 7.0f), 0);
	// Full force: throw -> pull pays off; draining after a pull or swinging into a throw does not.
	BOOST_CHECK_GT(BotLearn_BaselineBonus(force, BOTLEARN_TOK_IDLE, BOTLEARN_TOK_THROW,
		BOTLEARN_TOK_PULL, 1.0f, 7.0f), 0);
	BOOST_CHECK_LT(BotLearn_BaselineBonus(force, BOTLEARN_TOK_PULL, BOTLEARN_TOK_DRAIN,
		BOTLEARN_BASELINE_FOLLOW_ANY, 1.0f, 7.0f), 0);
	BOOST_CHECK_LT(BotLearn_BaselineBonus(force, BOTLEARN_TOK_THROW, BOTLEARN_TOK_SWING,
		BOTLEARN_TOK_SWING, 1.0f, 7.0f), 0);
	// Modes never mix and unknown sequences stay neutral.
	BOOST_CHECK_EQUAL(BotLearn_BaselineBonus(saber, BOTLEARN_TOK_IDLE, BOTLEARN_TOK_THROW,
		BOTLEARN_TOK_PULL, 1.0f, 7.0f), 0);
	BOOST_CHECK_EQUAL(BotLearn_BaselineBonus(force, BOTLEARN_TOK_KICK, BOTLEARN_TOK_ROLL,
		BOTLEARN_BASELINE_FOLLOW_ANY, 1.0f, 7.0f), 0);
	// Strength 0 disables it, lower skills lean on it less, and it never exceeds the cap.
	BOOST_CHECK_EQUAL(BotLearn_BaselineBonus(saber, BOTLEARN_TOK_IDLE, BOTLEARN_TOK_SWING,
		BOTLEARN_BASELINE_FOLLOW_ANY, 0.0f, 7.0f), 0);
	BOOST_CHECK_LT(BotLearn_BaselineBonus(saber, BOTLEARN_TOK_IDLE, BOTLEARN_TOK_SWING,
		BOTLEARN_BASELINE_FOLLOW_ANY, 1.0f, 1.0f),
		BotLearn_BaselineBonus(saber, BOTLEARN_TOK_IDLE, BOTLEARN_TOK_SWING,
		BOTLEARN_BASELINE_FOLLOW_ANY, 1.0f, 7.0f));
	BOOST_CHECK_LE(BotLearn_BaselineBonus(saber, BOTLEARN_TOK_IDLE, BOTLEARN_TOK_SWING,
		BOTLEARN_BASELINE_FOLLOW_ANY, 10.0f, 10.0f), BOTLEARN_BONUS_CAP);
}

BOOST_AUTO_TEST_CASE(baked_baseline_respects_safety_dimensions)
{
	const int base = BotLearn_ContextKey(150, 150, 100, 100, 0, 1, 1);
	const int arcade = BotLearn_ContextSafety(base, 2, 0, 0, 0, 0, 0, 0);
	const int broken = BotLearn_ContextSafety(base, 0, BOTLEARN_DEFENSE_BROKEN, 0, 0, 0, 0, 0);
	const int knocked = BotLearn_ContextSafety(base, 0, BOTLEARN_DEFENSE_KNOCKDOWN, 0, 0, 0, 0, 0);
	const int recovering = BotLearn_ContextSafety(base, 0, 0, 0, 1, 0, 0, 0);
	const int airborne = BotLearn_ContextSafety(base, 0, 0, 0, 0, 0, 1, 0);
	const int parry = BotLearn_ContextSafety(base, 0, BOTLEARN_DEFENSE_PARRY, 0, 0, 0, 0, 1);

	BOOST_CHECK(!BotLearn_BaselineContextAllowed(base));
	BOOST_CHECK(!BotLearn_BaselineContextAllowed(arcade));
	BOOST_CHECK(!BotLearn_BaselineContextAllowed(broken));
	BOOST_CHECK(!BotLearn_BaselineContextAllowed(knocked));
	BOOST_CHECK(!BotLearn_BaselineContextAllowed(recovering));
	BOOST_CHECK(!BotLearn_BaselineContextAllowed(airborne));
	BOOST_CHECK(BotLearn_BaselineContextAllowed(parry));
	BOOST_CHECK_EQUAL(BotLearn_BaselineBonus(broken, BOTLEARN_TOK_IDLE, BOTLEARN_TOK_SWING,
		BOTLEARN_BASELINE_FOLLOW_ANY, 1.0f, 7.0f), 0);
}

BOOST_AUTO_TEST_SUITE_END()
