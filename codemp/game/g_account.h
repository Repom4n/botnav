#ifndef G_ACCOUNT_H
#define G_ACCOUNT_H

#include "sqlite3.h"
#include <stdlib.h>
#include <string.h>

static inline void G_ReleaseTrackedPersistSnapshots(void *job, size_t jobSize,
	void *winnerEvents, void *loserEvents)
{
	free(winnerEvents);
	free(loserEvents);
	memset(job, 0, jobSize);
}

static inline int G_TrackedPersistQueueSlot(int head, int count, int capacity)
{
	if (count < 0 || count >= capacity || head < 0 || head >= capacity)
		return -1;
	return (head + count) % capacity;
}

/* A blocked step must yield immediately, never spin or release the queued job. */
static inline int G_DrainTrackedPersistJob(void *job, int (*step)(void *),
	int *stage, int *cursor, sqlite3_int64 *summaryId, int maxSteps,
	int (*milliseconds)(void), int budgetMs)
{
	const int start = milliseconds ? milliseconds() : 0;
	int i;
	for (i = 0; i < maxSteps; i++)
	{
		const int savedStage = *stage;
		const int savedCursor = *cursor;
		const sqlite3_int64 savedSummaryId = *summaryId;
		if (milliseconds && milliseconds() - start >= budgetMs)
			return 0;
		if (!step(job))
			return 1;
		if (*stage == savedStage && *cursor == savedCursor && *summaryId == savedSummaryId)
			return 0;
	}
	return 0;
}

static inline int G_TrackedPersistRetryable(int status)
{
	const int primary = status & 0xff;
	return primary == SQLITE_BUSY || primary == SQLITE_LOCKED;
}

static inline int G_BeginTrackedPersistTransaction(sqlite3 *db)
{
	if (!sqlite3_get_autocommit(db))
		return SQLITE_BUSY;
	return sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL);
}

static inline int G_FinishTrackedPersistTransaction(sqlite3 *db, int status,
	int *stage, int *cursor, sqlite3_int64 *summaryId,
	int savedStage, int savedCursor, sqlite3_int64 savedSummaryId)
{
	if (status == SQLITE_OK)
		status = sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
	if (status != SQLITE_OK)
	{
		const int rollback = sqlite3_get_autocommit(db) ? SQLITE_OK :
			sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
		*stage = savedStage;
		*cursor = savedCursor;
		*summaryId = savedSummaryId;
		if (rollback != SQLITE_OK)
			return SQLITE_ERROR;
	}
	return status;
}

static const char *const g_trackedDuelTableNames[] = {
	"LocalDuelTrackSummary",
	"LocalDuelTrackParticipant",
	"LocalDuelTrackEvent",
	"LocalDuelTrackGeometry",
	"LocalDuelTrackAggregate"
};

static inline int G_BackfillLearningEvidenceSessions(sqlite3 *db)
{
	return sqlite3_exec(db,
		"UPDATE LocalBotLearnedEvidence SET session_id = COALESCE("
		"(SELECT NULLIF(e.session_id, '') FROM LocalDuelTrackEvent e "
		"WHERE e.summary_id = LocalBotLearnedEvidence.summary_id "
		"AND e.participant_key = LocalBotLearnedEvidence.participant_key "
		"AND e.session_id IS NOT NULL AND e.session_id <> '' LIMIT 1), "
		"'legacy:' || summary_id) WHERE session_id IS NULL OR session_id = ''",
		NULL, NULL, NULL);
}

static inline int G_AccountTableExists(sqlite3 *db, const char *name)
{
	sqlite3_stmt *stmt = NULL;
	int found = 0;
	if (sqlite3_prepare_v2(db, "SELECT 1 FROM main.sqlite_master WHERE type='table' AND name=?",
		-1, &stmt, NULL) == SQLITE_OK)
	{
		sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
		found = sqlite3_step(stmt) == SQLITE_ROW;
	}
	sqlite3_finalize(stmt);
	return found;
}

/* Keep the reset whitelist and its transaction shared with the SQLite regression tests. */
static inline int G_ResetTrackingTables(sqlite3 *db, int learning, int *clearedRows)
{
	static const char *const learnedTables[] = {
		"LocalBotLearnedSequence", "LocalBotLearnedEvidence"
	};
	const char *const *tables = learning ? learnedTables : g_trackedDuelTableNames;
	const int count = learning ? 2 : (int)(sizeof(g_trackedDuelTableNames) / sizeof(g_trackedDuelTableNames[0]));
	char sql[160];
	int result, i, rows = 0;
	*clearedRows = 0;
	if (!sqlite3_get_autocommit(db))
		return SQLITE_BUSY;
	result = sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL);
	if (result != SQLITE_OK)
		return result;
	/* Preserve provenance before summary/event IDs can be reused by a duel reset. */
	if (!learning)
		result = G_BackfillLearningEvidenceSessions(db);
	for (i = 0; result == SQLITE_OK && i < count; i++)
	{
		sqlite3_snprintf(sizeof(sql), sql, "DELETE FROM main.%s", tables[i]);
		result = sqlite3_exec(db, sql, NULL, NULL, NULL);
		if (result == SQLITE_OK)
			rows += sqlite3_changes(db);
	}
	if (result == SQLITE_OK && !learning && G_AccountTableExists(db, "LocalDuelTrackImport"))
		result = sqlite3_exec(db, "DELETE FROM main.LocalDuelTrackImport", NULL, NULL, NULL);
	if (result == SQLITE_OK && G_AccountTableExists(db, "sqlite_sequence"))
	{
		for (i = 0; result == SQLITE_OK && i < count; i++)
		{
			sqlite3_snprintf(sizeof(sql), sql, "DELETE FROM main.sqlite_sequence WHERE name='%s'", tables[i]);
			result = sqlite3_exec(db, sql, NULL, NULL, NULL);
		}
	}
	if (result == SQLITE_OK)
		result = sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
	if (result != SQLITE_OK)
		sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
	else
		*clearedRows = rows;
	return result;
}

#endif
