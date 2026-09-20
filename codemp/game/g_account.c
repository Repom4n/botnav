#include "g_local.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"
#include "ai_combat_tuning.h"

#define _USE_CURL 0

#if _USE_CURL
#include "curl/curl.h"
#include "curl/easy.h"
#endif

static char LOCAL_DB_PATH[MAX_OSPATH];
#define BOT_DUEL_RANKED_LIMIT_PER_LEVEL 5
#define BOT_DUEL_LEVEL_MIN 1
#define BOT_DUEL_LEVEL_MAX 10
#define TRACKED_DUEL_MAX_EVENTS 128
#define TRACKED_DUEL_TUTORIAL_MAX_MESSAGES 3
#define TRACKED_DUEL_TUTORIAL_COOLDOWN_MS 7000
#define TRACKED_DUEL_TUTORIAL_FAST_COOLDOWN_MS 2500
#define TRACKED_DUEL_TUTORIAL_MIN_DELAY_MS 1500
#define TRACKED_DUEL_TUTORIAL_MAX_DELAY_MS 2500
#define TRACKED_DUEL_LOW_FORCE_THRESHOLD 25
#define TRACKED_DUEL_ADVICE_BASIC_WINDOW 5
#define TRACKED_DUEL_ADVICE_MIN_OBSERVATIONS 3
#define TRACKED_DUEL_ADVICE_REPEAT_THRESHOLD 2
#define TRACKED_DUEL_PATTERN_MIN_EVENTS 10
#define TRACKED_DUEL_PATTERN_MIN_DURATION_MS 12000
#define TRACKED_DUEL_SKILL_MIN_WINRATE_DUELS 5
#define TRACKED_DUEL_SKILL_HIGH_WINRATE 55
#define TRACKED_DUEL_SKILL_LOW_WINRATE 35
#define TRACKED_DUEL_ADVICE_LOGIN_START 3
#define TRACKED_DUEL_ADVICE_LOGIN_INTERVAL 3
#define LOCAL_ARCADE_SCORE_ORDER "score DESC, end_time DESC"
//#define GLOBAL_DB_PATH sv_globalDBPath.string
//#define MAX_TMP_RACELOG_SIZE 80 * 1024

void G_ErrorPrint( const char *fmt, int s );
void G_Say(gentity_t *ent, gentity_t *target, int mode, const char *chatText);
extern qboolean BG_InKnockDown(int anim);

#define CALL_SQLITE(f) {                                        \
        int i;                                                  \
        i = sqlite3_ ## f;                                      \
        if (i != SQLITE_OK) {                                   \
            fprintf (stderr, "%s failed with status %d: %s\n",  \
                     #f, i, sqlite3_errmsg (db));               \
        }                                                       \
    }                                                           \

#define CALL_SQLITE_EXPECT(f,x) {                               \
        int i;                                                  \
        i = sqlite3_ ## f;                                      \
        if (i != SQLITE_ ## x) {                                \
            fprintf (stderr, "%s failed with status %d: %s\n",  \
                     #f, i, sqlite3_errmsg (db));               \
        }                                                       \
    }

typedef enum
{
	DUEL_TRACK_ID_BOT = 0,
	DUEL_TRACK_ID_LOGIN = 1,
	DUEL_TRACK_ID_IP = 2
} duel_track_identity_kind_t;

typedef enum
{
	DUEL_TRACK_POWER_PUSH = 0,
	DUEL_TRACK_POWER_PULL,
	DUEL_TRACK_POWER_GRIP,
	DUEL_TRACK_POWER_DRAIN,
	DUEL_TRACK_POWER_RAGE,
	DUEL_TRACK_POWER_ABSORB,
	DUEL_TRACK_POWER_PROTECT,
	DUEL_TRACK_POWER_HEAL,
	DUEL_TRACK_POWER_SPEED,
	DUEL_TRACK_POWER_SEEING,
	DUEL_TRACK_POWER_UNKNOWN,
	DUEL_TRACK_POWER_COUNT
} duel_track_power_t;

typedef enum
{
	DUEL_TRACK_STATE_NEUTRAL = 0,
	DUEL_TRACK_STATE_ADVANTAGE,
	DUEL_TRACK_STATE_DISADVANTAGE,
	DUEL_TRACK_STATE_PANIC,
	DUEL_TRACK_STATE_FINISHING
} duel_track_state_t;

typedef struct
{
	int relTime;
	int amount;
	unsigned short eventIndex;
	unsigned char eventType;
	unsigned char power;
	unsigned char state;
	unsigned char rangeBucket;
	unsigned char hasGeometry;
	vec3_t selfOrigin;
	vec3_t enemyOrigin;
	vec3_t selfVelocity;
	vec3_t enemyVelocity;
	float selfYaw;
	float enemyYaw;
	char note[32];
} tracked_duel_event_t;

typedef struct
{
	qboolean active;
	int duelType;
	int duelStartTime;
	int opponentClientNum;
	int eventCount;
	int lastForce;
	int lastHealthArmor;
	int lastSelectedPower;
	int lastPowersActive;
	int lastRangeBucket;
	int lastAirborne;
	int lastKnockdown;
	int lastGripCripple;
	int totalForceSpent;
	int totalForceRegen;
	int forceSpentByPower[DUEL_TRACK_POWER_COUNT];
	int spentByState[DUEL_TRACK_STATE_FINISHING + 1];
	int lowForceWindows;
	int gripCrippleEvents;
	int saberThrowPunishes;
	int knockdownEvents;
	int lateDefenseSpends;
	int lowestForce;
	int endingForce;
	int endingHP;
	int endingArmor;
	int side;
	int opponentSide;
	int identityKind;
	int didDieLowForce;
	char identityKey[64];
	char identityLabel[MAX_NETNAME];
	char opponentKey[64];
	char opponentLabel[MAX_NETNAME];
	char openingTactic[32];
	char primaryIssue[32];
	tracked_duel_event_t events[TRACKED_DUEL_MAX_EVENTS];
} tracked_duel_runtime_t;

typedef struct
{
	int targetClientNum;
	int nextSendTime;
	int cooldownMs;
	int queuedCount;
	int nextMessageIndex;
	int immediateIndex;
	qboolean publicBroadcast;
	char messages[TRACKED_DUEL_TUTORIAL_MAX_MESSAGES][MAX_SAY_TEXT];
} bot_tutorial_queue_t;

typedef enum
{
	DUEL_TRACK_ISSUE_LOW_FORCE = 0,
	DUEL_TRACK_ISSUE_GRIP_CONTROL,
	DUEL_TRACK_ISSUE_SABER_THROW,
	DUEL_TRACK_ISSUE_KNOCKDOWN,
	DUEL_TRACK_ISSUE_LATE_DEFENSE,
	DUEL_TRACK_ISSUE_FORCED_ENTRIES,
	DUEL_TRACK_ISSUE_LINEAR_ENTRIES,
	DUEL_TRACK_ISSUE_COUNT
} duel_track_issue_t;

typedef struct
{
	qboolean active;
	int identityKind;
	int duelsSeen;
	int historyDuels;
	qboolean historyLoaded;
	char identityKey[64];
	int sessionIssueCounts[DUEL_TRACK_ISSUE_COUNT];
	int historyIssueCounts[DUEL_TRACK_ISSUE_COUNT];
	int historyWins;
	int sessionWins;
	int adviceRotation;
	unsigned int lastAdviceHash;
	int lastLoginPromptDuel;
} duel_advice_session_state_t;

typedef enum
{
	DUEL_TRACK_SKILL_BEGINNER = 0,
	DUEL_TRACK_SKILL_INTERMEDIATE,
	DUEL_TRACK_SKILL_ADVANCED
} duel_track_skill_band_t;

static tracked_duel_runtime_t g_trackedDuels[MAX_CLIENTS];
static bot_tutorial_queue_t g_botTutorialQueues[MAX_CLIENTS];
static duel_advice_session_state_t g_duelAdviceSessions[MAX_CLIENTS];
static qboolean g_duelTrackingSchemaReady = qfalse;
static char g_duelTrackingSchemaPath[MAX_OSPATH];

static void G_EnsureLocalArcadeSchema(sqlite3 *db);
static qboolean G_DoesTrackedDuelTableExist(sqlite3 *db, const char *tableName);
static qboolean G_OpenTrackedLocalDB(sqlite3 **dbOut, char *resolvedPath, int resolvedPathSize);
static void G_QueueBotTutorialMessage(int botClientNum, int targetClientNum, const char *message);
static void G_EnsureLocalDuelTrackingSchema(sqlite3 *db)
{
	sqlite3_stmt *stmt = NULL;
	char *sql;
	int s;

	sql = "CREATE TABLE IF NOT EXISTS LocalDuelTrackSummary("
		"id INTEGER PRIMARY KEY, start_time UNSIGNED INTEGER, end_time UNSIGNED INTEGER, duration UNSIGNED INTEGER, "
		"type UNSIGNED TINYINT, mapname VARCHAR(64), winner_key VARCHAR(64), winner_label VARCHAR(36), "
		"winner_kind UNSIGNED TINYINT, winner_side UNSIGNED TINYINT, loser_key VARCHAR(64), loser_label VARCHAR(36), "
		"loser_kind UNSIGNED TINYINT, loser_side UNSIGNED TINYINT, draw UNSIGNED TINYINT DEFAULT 0, "
		"winner_opening VARCHAR(32), loser_opening VARCHAR(32))";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (LocalDuelTrackSummary)", s);
	CALL_SQLITE(finalize(stmt));

	sql = "CREATE TABLE IF NOT EXISTS LocalDuelTrackParticipant("
		"id INTEGER PRIMARY KEY, summary_id INTEGER, participant_key VARCHAR(64), participant_label VARCHAR(36), "
		"participant_kind UNSIGNED TINYINT, opponent_key VARCHAR(64), won UNSIGNED TINYINT, side UNSIGNED TINYINT, "
		"opponent_side UNSIGNED TINYINT, matchup UNSIGNED TINYINT, total_force_spent UNSIGNED INTEGER, "
		"total_force_regen UNSIGNED INTEGER, ending_force SMALLINT, ending_hp SMALLINT, ending_armor SMALLINT, "
		"low_force_windows UNSIGNED SMALLINT, grip_cripple_events UNSIGNED SMALLINT, saber_throw_punishes UNSIGNED SMALLINT, "
		"knockdown_events UNSIGNED SMALLINT, late_defense_spends UNSIGNED SMALLINT, opening_tactic VARCHAR(32), "
		"primary_issue VARCHAR(32), spent_neutral UNSIGNED INTEGER, spent_advantage UNSIGNED INTEGER, "
		"spent_disadvantage UNSIGNED INTEGER, spent_panic UNSIGNED INTEGER, spent_finishing UNSIGNED INTEGER, "
		"force_push UNSIGNED INTEGER, force_pull UNSIGNED INTEGER, force_grip UNSIGNED INTEGER, force_drain UNSIGNED INTEGER, "
		"force_rage UNSIGNED INTEGER, force_absorb UNSIGNED INTEGER, force_protect UNSIGNED INTEGER, force_heal UNSIGNED INTEGER, "
		"force_speed UNSIGNED INTEGER, force_seeing UNSIGNED INTEGER, force_unknown UNSIGNED INTEGER)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (LocalDuelTrackParticipant)", s);
	CALL_SQLITE(finalize(stmt));

	sql = "CREATE TABLE IF NOT EXISTS LocalDuelTrackEvent("
		"id INTEGER PRIMARY KEY, summary_id INTEGER, participant_key VARCHAR(64), opponent_key VARCHAR(64), "
		"rel_time UNSIGNED INTEGER, event_index UNSIGNED SMALLINT, event_type VARCHAR(16), power UNSIGNED TINYINT, "
		"amount SMALLINT, state UNSIGNED TINYINT, range_bucket UNSIGNED TINYINT, note VARCHAR(32))";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (LocalDuelTrackEvent)", s);
	CALL_SQLITE(finalize(stmt));

	sql = "CREATE TABLE IF NOT EXISTS LocalDuelTrackGeometry("
		"id INTEGER PRIMARY KEY, summary_id INTEGER, participant_key VARCHAR(64), opponent_key VARCHAR(64), "
		"rel_time UNSIGNED INTEGER, event_index UNSIGNED SMALLINT, "
		"self_x REAL, self_y REAL, self_z REAL, enemy_x REAL, enemy_y REAL, enemy_z REAL, "
		"self_vx REAL, self_vy REAL, self_vz REAL, enemy_vx REAL, enemy_vy REAL, enemy_vz REAL, "
		"self_yaw REAL, enemy_yaw REAL)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (LocalDuelTrackGeometry)", s);
	CALL_SQLITE(finalize(stmt));

	sql = "CREATE TABLE IF NOT EXISTS LocalDuelTrackAggregate("
		"participant_key VARCHAR(64), participant_kind UNSIGNED TINYINT, side UNSIGNED TINYINT, matchup UNSIGNED TINYINT, "
		"duels UNSIGNED INTEGER, wins UNSIGNED INTEGER, losses UNSIGNED INTEGER, total_force_spent UNSIGNED INTEGER, "
		"total_force_regen UNSIGNED INTEGER, low_force_deaths UNSIGNED INTEGER, grip_cripples UNSIGNED INTEGER, "
		"saber_throw_punishes UNSIGNED INTEGER, force_push UNSIGNED INTEGER, force_pull UNSIGNED INTEGER, "
		"force_grip UNSIGNED INTEGER, force_drain UNSIGNED INTEGER, force_rage UNSIGNED INTEGER, force_absorb UNSIGNED INTEGER, "
		"force_protect UNSIGNED INTEGER, force_heal UNSIGNED INTEGER, force_speed UNSIGNED INTEGER, force_seeing UNSIGNED INTEGER, "
		"force_unknown UNSIGNED INTEGER, PRIMARY KEY(participant_key, participant_kind, side, matchup))";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (LocalDuelTrackAggregate)", s);
	CALL_SQLITE(finalize(stmt));
	g_duelTrackingSchemaReady = qtrue;
	Q_strncpyz(g_duelTrackingSchemaPath, LOCAL_DB_PATH, sizeof(g_duelTrackingSchemaPath));
}

static void G_ClearTrackedDuelRuntime(int clientNum)
{
	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
		return;

	memset(&g_trackedDuels[clientNum], 0, sizeof(g_trackedDuels[clientNum]));
}

static void G_ClearBotTutorialQueue(int clientNum)
{
	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
		return;

	memset(&g_botTutorialQueues[clientNum], 0, sizeof(g_botTutorialQueues[clientNum]));
	g_botTutorialQueues[clientNum].targetClientNum = -1;
	g_botTutorialQueues[clientNum].immediateIndex = -1;
	g_botTutorialQueues[clientNum].cooldownMs = TRACKED_DUEL_TUTORIAL_COOLDOWN_MS;
}

static qboolean G_IsTrackedDuelCollectionEnabled(void)
{
	return bot_dueltracking.integer ? qtrue : qfalse;
}

static qboolean G_IsTrackedGeometryEnabled(void)
{
	return (bot_dueltracking_geometry.integer > 0) ? qtrue : qfalse;
}

static qboolean G_ShouldCaptureTrackedGeometryEvent(int eventType)
{
	if (!G_IsTrackedGeometryEnabled())
		return qfalse;
	if (bot_dueltracking_geometry.integer >= 2)
		return qtrue;
	return (eventType == 2 || eventType == 3 || eventType == 4 || eventType == 5) ? qtrue : qfalse;
}

static qboolean G_IsTrackedDuelEligible(gentity_t *first, gentity_t *second)
{
	if (!first || !second)
		return qfalse;
	if ((first->r.svFlags & SVF_BOT) && (second->r.svFlags & SVF_BOT))
		return qfalse;
	return qtrue;
}

static void G_ClearDuelAdviceSession(int clientNum)
{
	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
		return;

	memset(&g_duelAdviceSessions[clientNum], 0, sizeof(g_duelAdviceSessions[clientNum]));
}

void G_ClearTrackedDuelClientState(int clientNum)
{
	G_ClearTrackedDuelRuntime(clientNum);
	G_ClearBotTutorialQueue(clientNum);
	G_ClearDuelAdviceSession(clientNum);
}

static unsigned int G_HashTrackedIdentityString(const char *value)
{
	unsigned int hash = 2166136261u;

	if (!value)
		return 0;

	while (*value)
	{
		hash ^= (unsigned char)*value++;
		hash *= 16777619u;
	}

	return hash;
}

static void G_SetBotTutorialInitialDelay(bot_tutorial_queue_t *queue)
{
	if (!queue || queue->queuedCount <= queue->nextMessageIndex)
	{
		return;
	}

	queue->nextSendTime = level.time + Q_irand(TRACKED_DUEL_TUTORIAL_MIN_DELAY_MS, TRACKED_DUEL_TUTORIAL_MAX_DELAY_MS);
}

static void G_QueueRotatingTutorialMessage(int botClientNum, int targetClientNum,
	const char **messages, int messageCount, int preferredIndex, duel_advice_session_state_t *session)
{
	int index;
	unsigned int hash = 0;

	if (!messages || messageCount <= 0)
	{
		return;
	}

	index = preferredIndex;
	if (index < 0)
	{
		index = 0;
	}
	index %= messageCount;

	if (session)
	{
		index = (index + (session->adviceRotation % messageCount)) % messageCount;
		session->adviceRotation++;
		hash = G_HashTrackedIdentityString(messages[index]);

		if (messageCount > 1 && hash == session->lastAdviceHash)
		{
			index = (index + 1) % messageCount;
			hash = G_HashTrackedIdentityString(messages[index]);
		}
		session->lastAdviceHash = hash;
	}

	G_QueueBotTutorialMessage(botClientNum, targetClientNum, messages[index]);
}

static void G_GetTrackingIPKey(gentity_t *ent, char *out, int outSize)
{
	char ip[NET_ADDRSTRMAXLEN];
	char *colon;
	char *closingBracket;
	int colonCount = 0;

	if (!out || outSize < 1)
		return;

	out[0] = '\0';
	if (!ent || !ent->client)
		return;

	Q_strncpyz(ip, ent->client->sess.IP, sizeof(ip));
	if (ip[0] == '[')
	{
		closingBracket = strchr(ip, ']');
		if (closingBracket)
		{
			*closingBracket = '\0';
			memmove(ip, ip + 1, strlen(ip + 1) + 1);
		}
	}
	else
	{
		for (colon = ip; *colon; colon++)
		{
			if (*colon == ':')
				colonCount++;
		}
		if (colonCount == 1)
		{
			colon = strchr(ip, ':');
			if (colon)
				*colon = '\0';
		}
	}
	if (ip[0])
		Com_sprintf(out, outSize, "iphash:%08x", G_HashTrackedIdentityString(ip));
}

static void G_NormalizeTrackedIdentityComponent(const char *in, char *out, int outSize)
{
	int i;

	if (!out || outSize < 1)
		return;

	out[0] = '\0';
	if (!in)
		return;

	Q_strncpyz(out, in, outSize);
	for (i = 0; out[i]; i++)
		out[i] = tolower((unsigned char)out[i]);
}

static qboolean G_GetDuelTrackingIdentity(gentity_t *ent, char *key, int keySize, char *label, int labelSize, int *kind)
{
	char normalized[128];

	if (!ent || !ent->client || !key || keySize < 1)
		return qfalse;

	key[0] = '\0';
	if (label && labelSize > 0)
		Q_strncpyz(label, ent->client->pers.netname, labelSize);
	if (kind)
		*kind = DUEL_TRACK_ID_IP;

	if (ent->r.svFlags & SVF_BOT)
	{
		char userinfo[MAX_INFO_STRING];
		char personality[MAX_QPATH];

		if (kind)
			*kind = DUEL_TRACK_ID_BOT;
		trap->GetUserinfo(ent->s.number, userinfo, sizeof(userinfo));
		Q_strncpyz(personality, Info_ValueForKey(userinfo, "personality"), sizeof(personality));
		G_NormalizeTrackedIdentityComponent(personality, normalized, sizeof(normalized));
		if (normalized[0])
			Com_sprintf(key, keySize, "bot:%s", normalized);
		else
			Com_sprintf(key, keySize, "botclient:%d", ent->s.number);
		return qtrue;
	}
	if (ent->client->pers.userName[0])
	{
		if (kind)
			*kind = DUEL_TRACK_ID_LOGIN;
		G_NormalizeTrackedIdentityComponent(ent->client->pers.userName, normalized, sizeof(normalized));
		Com_sprintf(key, keySize, "user:%s", normalized);
		if (label && labelSize > 0)
			Q_strncpyz(label, ent->client->pers.userName, labelSize);
		return qtrue;
	}

	G_GetTrackingIPKey(ent, key, keySize);
	return (key[0]) ? qtrue : qfalse;
}

static int G_GetTrackedParticipantSide(gentity_t *ent)
{
	if (!ent || !ent->client)
		return 0;
	if (ent->client->ps.fd.forceSide == FORCE_LIGHTSIDE)
		return FORCE_LIGHTSIDE;
	if (ent->client->ps.fd.forceSide == FORCE_DARKSIDE)
		return FORCE_DARKSIDE;
	return 0;
}

static int G_GetTrackedMatchup(int side, int opponentSide)
{
	if (side == FORCE_LIGHTSIDE && opponentSide == FORCE_LIGHTSIDE)
		return 1;
	if ((side == FORCE_LIGHTSIDE && opponentSide == FORCE_DARKSIDE) ||
		(side == FORCE_DARKSIDE && opponentSide == FORCE_LIGHTSIDE))
		return 2;
	if (side == FORCE_DARKSIDE && opponentSide == FORCE_DARKSIDE)
		return 3;
	return 0;
}

static duel_track_issue_t G_MapPrimaryIssueToTrackedIssue(const char *primaryIssue)
{
	if (!primaryIssue || !primaryIssue[0])
		return DUEL_TRACK_ISSUE_COUNT;
	if (!Q_stricmp(primaryIssue, "low_force"))
		return DUEL_TRACK_ISSUE_LOW_FORCE;
	if (!Q_stricmp(primaryIssue, "grip_control"))
		return DUEL_TRACK_ISSUE_GRIP_CONTROL;
	if (!Q_stricmp(primaryIssue, "saber_throw"))
		return DUEL_TRACK_ISSUE_SABER_THROW;
	if (!Q_stricmp(primaryIssue, "knockdown"))
		return DUEL_TRACK_ISSUE_KNOCKDOWN;
	if (!Q_stricmp(primaryIssue, "late_defense"))
		return DUEL_TRACK_ISSUE_LATE_DEFENSE;
	if (!Q_stricmp(primaryIssue, "forced_entries"))
		return DUEL_TRACK_ISSUE_FORCED_ENTRIES;
	if (!Q_stricmp(primaryIssue, "linear_entries"))
		return DUEL_TRACK_ISSUE_LINEAR_ENTRIES;
	return DUEL_TRACK_ISSUE_COUNT;
}

static void G_LoadTrackedAdviceHistory(duel_advice_session_state_t *session)
{
	sqlite3 *db;
	sqlite3_stmt *stmt = NULL;
	char *sql;
	int s;

	if (!session || session->historyLoaded || !session->identityKey[0] || session->identityKind != DUEL_TRACK_ID_LOGIN)
		return;

	if (!G_OpenTrackedLocalDB(&db, NULL, 0))
	{
		session->historyLoaded = qtrue;
		return;
	}
	if (!G_DoesTrackedDuelTableExist(db, "LocalDuelTrackParticipant"))
	{
		CALL_SQLITE(close(db));
		session->historyLoaded = qtrue;
		return;
	}

	sql = "SELECT COUNT(*), "
		"COALESCE(SUM(CASE WHEN won=1 THEN 1 ELSE 0 END), 0), "
		"COALESCE(SUM(CASE WHEN primary_issue='low_force' THEN 1 ELSE 0 END), 0), "
		"COALESCE(SUM(CASE WHEN primary_issue='grip_control' THEN 1 ELSE 0 END), 0), "
		"COALESCE(SUM(CASE WHEN primary_issue='saber_throw' THEN 1 ELSE 0 END), 0), "
		"COALESCE(SUM(CASE WHEN primary_issue='knockdown' THEN 1 ELSE 0 END), 0), "
		"COALESCE(SUM(CASE WHEN primary_issue='late_defense' THEN 1 ELSE 0 END), 0), "
		"COALESCE(SUM(CASE WHEN primary_issue='forced_entries' THEN 1 ELSE 0 END), 0), "
		"COALESCE(SUM(CASE WHEN primary_issue='linear_entries' THEN 1 ELSE 0 END), 0) "
		"FROM LocalDuelTrackParticipant WHERE participant_key=? AND participant_kind=?";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_text(stmt, 1, session->identityKey, -1, SQLITE_TRANSIENT));
	CALL_SQLITE(bind_int(stmt, 2, session->identityKind));
	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW)
	{
		session->historyDuels = sqlite3_column_int(stmt, 0);
		session->historyWins = sqlite3_column_int(stmt, 1);
		session->historyIssueCounts[DUEL_TRACK_ISSUE_LOW_FORCE] = sqlite3_column_int(stmt, 2);
		session->historyIssueCounts[DUEL_TRACK_ISSUE_GRIP_CONTROL] = sqlite3_column_int(stmt, 3);
		session->historyIssueCounts[DUEL_TRACK_ISSUE_SABER_THROW] = sqlite3_column_int(stmt, 4);
		session->historyIssueCounts[DUEL_TRACK_ISSUE_KNOCKDOWN] = sqlite3_column_int(stmt, 5);
		session->historyIssueCounts[DUEL_TRACK_ISSUE_LATE_DEFENSE] = sqlite3_column_int(stmt, 6);
		session->historyIssueCounts[DUEL_TRACK_ISSUE_FORCED_ENTRIES] = sqlite3_column_int(stmt, 7);
		session->historyIssueCounts[DUEL_TRACK_ISSUE_LINEAR_ENTRIES] = sqlite3_column_int(stmt, 8);
	}
	else if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Select Failed (G_LoadTrackedAdviceHistory)", s);
	}
	CALL_SQLITE(finalize(stmt));
	CALL_SQLITE(close(db));
	session->historyLoaded = qtrue;
}

static duel_advice_session_state_t *G_GetTrackedAdviceSession(gentity_t *ent, tracked_duel_runtime_t *runtime)
{
	duel_advice_session_state_t *session;

	if (!ent || !ent->client || !runtime || ent->s.number < 0 || ent->s.number >= MAX_CLIENTS)
		return NULL;

	session = &g_duelAdviceSessions[ent->s.number];
	if (!session->active ||
		session->identityKind != runtime->identityKind ||
		Q_stricmp(session->identityKey, runtime->identityKey))
	{
		memset(session, 0, sizeof(*session));
		session->active = qtrue;
		session->identityKind = runtime->identityKind;
		Q_strncpyz(session->identityKey, runtime->identityKey, sizeof(session->identityKey));
	}
	return session;
}

static void G_RecordTrackedSessionOutcome(gentity_t *ent, tracked_duel_runtime_t *runtime, qboolean won)
{
	duel_advice_session_state_t *session;

	if (!ent || !ent->client || (ent->r.svFlags & SVF_BOT) || !runtime)
		return;

	session = G_GetTrackedAdviceSession(ent, runtime);
	if (!session)
		return;

	session->duelsSeen++;
	if (won)
		session->sessionWins++;
}

static qboolean G_TrackedPatternConfidenceHigh(const tracked_duel_runtime_t *runtime, const duel_advice_session_state_t *session, duel_track_issue_t issue)
{
	int issueConfidence;
	int observedDuels;
	int duelDuration;

	if (!runtime || !session || issue < 0 || issue >= DUEL_TRACK_ISSUE_COUNT)
		return qfalse;

	issueConfidence = session->sessionIssueCounts[issue] + session->historyIssueCounts[issue];
	observedDuels = session->duelsSeen + session->historyDuels;
	duelDuration = runtime->eventCount > 0 ? runtime->events[runtime->eventCount - 1].relTime : 0;

	if (runtime->eventCount < TRACKED_DUEL_PATTERN_MIN_EVENTS)
		return qfalse;
	if (duelDuration < TRACKED_DUEL_PATTERN_MIN_DURATION_MS)
		return qfalse;
	if (observedDuels < TRACKED_DUEL_ADVICE_MIN_OBSERVATIONS || issueConfidence < TRACKED_DUEL_ADVICE_REPEAT_THRESHOLD)
		return qfalse;

	return qtrue;
}

static duel_track_skill_band_t G_GetTrackedSkillBand(const tracked_duel_runtime_t *runtime, const duel_advice_session_state_t *session, int extraDuels, int extraWins)
{
	int score = 0;
	int observedDuels = 0;

	if (runtime)
	{
		if (runtime->totalForceSpent >= 80)
			score++;
		if (runtime->spentByState[DUEL_TRACK_STATE_ADVANTAGE] >= 20)
			score++;
		if (runtime->lowForceWindows <= 1)
			score++;
		if (runtime->lateDefenseSpends <= 0)
			score++;
		if (runtime->didDieLowForce)
			score -= 2;
		if (runtime->lowForceWindows >= 2)
			score--;
		if (runtime->lateDefenseSpends >= 2)
			score--;
		if (runtime->gripCrippleEvents >= 2 || runtime->knockdownEvents >= 2)
			score--;
	}

	if (session)
	{
		observedDuels = session->duelsSeen + session->historyDuels + extraDuels;
		if (observedDuels >= TRACKED_DUEL_SKILL_MIN_WINRATE_DUELS)
		{
			const int totalWins = session->historyWins + session->sessionWins + extraWins;
			const int winRate = (totalWins * 100) / observedDuels;
			if (winRate >= TRACKED_DUEL_SKILL_HIGH_WINRATE)
				score += 2;
			else if (winRate <= TRACKED_DUEL_SKILL_LOW_WINRATE)
				score -= 2;
		}
	}

	if ((observedDuels < TRACKED_DUEL_ADVICE_MIN_OBSERVATIONS && score <= 1) || score <= 0)
		return DUEL_TRACK_SKILL_BEGINNER;
	if (score >= 4)
		return DUEL_TRACK_SKILL_ADVANCED;
	return DUEL_TRACK_SKILL_INTERMEDIATE;
}

static qboolean G_TrackedAdviceIsSpecificAllowed(const tracked_duel_runtime_t *runtime, duel_advice_session_state_t *session, duel_track_issue_t issue)
{
	int observedDuels;
	int issueConfidence;

	if (!session || issue >= DUEL_TRACK_ISSUE_COUNT)
		return qfalse;
	observedDuels = session->duelsSeen + session->historyDuels;
	issueConfidence = session->sessionIssueCounts[issue] + session->historyIssueCounts[issue];
	if (observedDuels < TRACKED_DUEL_ADVICE_MIN_OBSERVATIONS)
		return qfalse;
	if (issueConfidence < TRACKED_DUEL_ADVICE_REPEAT_THRESHOLD)
		return qfalse;
	if (!G_TrackedPatternConfidenceHigh(runtime, session, issue))
		return qfalse;
	return qtrue;
}

static const char *G_GetTrackedEventTypeName(int eventType)
{
	switch (eventType)
	{
	case 0: return "force";
	case 1: return "regen";
	case 2: return "damage";
	case 3: return "range";
	case 4: return "air";
	case 5: return "knockdown";
	default: return "note";
	}
}

static int G_GetTrackedRangeBucket(gentity_t *ent, gentity_t *other)
{
	vec3_t diff;
	float dist;

	if (!ent || !other || !ent->client || !other->client)
		return 0;

	VectorSubtract(ent->client->ps.origin, other->client->ps.origin, diff);
	dist = VectorLength(diff);
	if (dist < 128.0f)
		return 1;
	if (dist < 384.0f)
		return 2;
	return 3;
}

static duel_track_power_t G_MapForcePowerToTrackedPower(int forcePower)
{
	switch (forcePower)
	{
	case FP_PUSH: return DUEL_TRACK_POWER_PUSH;
	case FP_PULL: return DUEL_TRACK_POWER_PULL;
	case FP_GRIP: return DUEL_TRACK_POWER_GRIP;
	case FP_DRAIN: return DUEL_TRACK_POWER_DRAIN;
	case FP_RAGE: return DUEL_TRACK_POWER_RAGE;
	case FP_ABSORB: return DUEL_TRACK_POWER_ABSORB;
	case FP_PROTECT: return DUEL_TRACK_POWER_PROTECT;
	case FP_HEAL: return DUEL_TRACK_POWER_HEAL;
	case FP_SPEED: return DUEL_TRACK_POWER_SPEED;
	case FP_SEE: return DUEL_TRACK_POWER_SEEING;
	default: return DUEL_TRACK_POWER_UNKNOWN;
	}
}

static int G_InferTrackedForceState(gentity_t *ent, gentity_t *other)
{
	int ourTotal;
	int theirTotal;
	int ourForce;
	int theirForce;

	if (!ent || !other || !ent->client || !other->client)
		return DUEL_TRACK_STATE_NEUTRAL;

	ourTotal = ent->health + ent->client->ps.stats[STAT_ARMOR];
	theirTotal = other->health + other->client->ps.stats[STAT_ARMOR];
	ourForce = ent->client->ps.fd.forcePower;
	theirForce = other->client->ps.fd.forcePower;

	if (ent->health <= 30 || ourForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD)
		return DUEL_TRACK_STATE_PANIC;
	if (other->health <= 30 || theirForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD)
		return DUEL_TRACK_STATE_FINISHING;
	if (ourTotal >= theirTotal + 20 && ourForce >= theirForce + 20)
		return DUEL_TRACK_STATE_ADVANTAGE;
	if (theirTotal >= ourTotal + 20 || theirForce >= ourForce + 20)
		return DUEL_TRACK_STATE_DISADVANTAGE;
	return DUEL_TRACK_STATE_NEUTRAL;
}

static void G_SetTrackedOpeningIfEmpty(tracked_duel_runtime_t *runtime, duel_track_power_t power, const char *fallback)
{
	static const char *powerNames[DUEL_TRACK_POWER_COUNT] = {
		"push", "pull", "grip", "drain", "rage", "absorb", "protect", "heal", "speed", "seeing", "unknown"
	};

	if (!runtime || runtime->openingTactic[0])
		return;

	if (power >= 0 && power < DUEL_TRACK_POWER_COUNT)
		Q_strncpyz(runtime->openingTactic, powerNames[power], sizeof(runtime->openingTactic));
	else if (fallback)
		Q_strncpyz(runtime->openingTactic, fallback, sizeof(runtime->openingTactic));
}

static void G_AddTrackedDuelEvent(tracked_duel_runtime_t *runtime, int eventType, int relTime, int amount, duel_track_power_t power, int state, int rangeBucket, const char *note, gentity_t *self, gentity_t *enemy)
{
	tracked_duel_event_t *event;

	if (!runtime || !runtime->active || runtime->eventCount >= TRACKED_DUEL_MAX_EVENTS)
		return;

	event = &runtime->events[runtime->eventCount++];
	memset(event, 0, sizeof(*event));
	event->relTime = relTime;
	event->amount = amount;
	event->eventIndex = runtime->eventCount;
	event->eventType = (unsigned char)eventType;
	event->power = (unsigned char)power;
	event->state = (unsigned char)state;
	event->rangeBucket = (unsigned char)rangeBucket;
	if (G_ShouldCaptureTrackedGeometryEvent(eventType) && self && enemy && self->client && enemy->client)
	{
		VectorCopy(self->client->ps.origin, event->selfOrigin);
		VectorCopy(enemy->client->ps.origin, event->enemyOrigin);
		VectorCopy(self->client->ps.velocity, event->selfVelocity);
		VectorCopy(enemy->client->ps.velocity, event->enemyVelocity);
		event->selfYaw = self->client->ps.viewangles[YAW];
		event->enemyYaw = enemy->client->ps.viewangles[YAW];
		event->hasGeometry = 1;
	}
	if (note)
		Q_strncpyz(event->note, note, sizeof(event->note));
}

static void G_SetTrackedPrimaryIssue(tracked_duel_runtime_t *runtime, qboolean lowForceFinish)
{
	if (!runtime)
		return;

	if (lowForceFinish || runtime->spentByState[DUEL_TRACK_STATE_PANIC] >= 25)
		Q_strncpyz(runtime->primaryIssue, "low_force", sizeof(runtime->primaryIssue));
	else if (runtime->gripCrippleEvents >= 2)
		Q_strncpyz(runtime->primaryIssue, "grip_control", sizeof(runtime->primaryIssue));
	else if (runtime->saberThrowPunishes >= 2)
		Q_strncpyz(runtime->primaryIssue, "saber_throw", sizeof(runtime->primaryIssue));
	else if (runtime->knockdownEvents >= 2)
		Q_strncpyz(runtime->primaryIssue, "knockdown", sizeof(runtime->primaryIssue));
	else if (runtime->lateDefenseSpends >= 2)
		Q_strncpyz(runtime->primaryIssue, "late_defense", sizeof(runtime->primaryIssue));
	else if (runtime->opponentSide == FORCE_LIGHTSIDE)
		Q_strncpyz(runtime->primaryIssue, "forced_entries", sizeof(runtime->primaryIssue));
	else
		Q_strncpyz(runtime->primaryIssue, "linear_entries", sizeof(runtime->primaryIssue));
}

static void G_QueueBotTutorialMessage(int botClientNum, int targetClientNum, const char *message)
{
	bot_tutorial_queue_t *queue;
	int i;

	if (!message || !message[0] || botClientNum < 0 || botClientNum >= MAX_CLIENTS ||
		targetClientNum < 0 || targetClientNum >= MAX_CLIENTS)
		return;

	queue = &g_botTutorialQueues[botClientNum];
	if (queue->queuedCount > queue->nextMessageIndex && queue->targetClientNum != targetClientNum)
		return;
	for (i = queue->nextMessageIndex; i < queue->queuedCount; i++)
	{
		if (!Q_stricmp(queue->messages[i], message))
			return;
	}
	if (queue->queuedCount >= TRACKED_DUEL_TUTORIAL_MAX_MESSAGES)
		return;

	if (queue->queuedCount > queue->nextMessageIndex && queue->immediateIndex < 0)
		queue->immediateIndex = queue->queuedCount;
	queue->targetClientNum = targetClientNum;
	Q_strncpyz(queue->messages[queue->queuedCount], message, sizeof(queue->messages[queue->queuedCount]));
	queue->queuedCount++;
	if (!queue->nextSendTime)
		queue->nextSendTime = level.time;
}

static int G_GetTrackedIssueConfidence(const duel_advice_session_state_t *session, duel_track_issue_t issue)
{
	if (!session || issue < 0 || issue >= DUEL_TRACK_ISSUE_COUNT)
		return 0;
	return session->sessionIssueCounts[issue] + session->historyIssueCounts[issue];
}

static qboolean G_IsTrackedIntermediateCandidate(const tracked_duel_runtime_t *runtime)
{
	if (!runtime)
		return qfalse;
	if (runtime->totalForceSpent >= 80 && runtime->lowForceWindows <= 1 && runtime->lateDefenseSpends <= 0)
		return qtrue;
	if (runtime->spentByState[DUEL_TRACK_STATE_ADVANTAGE] >= 20)
		return qtrue;
	return qfalse;
}

static void G_QueueManualBasicsAdvice(int botClientNum, int targetClientNum, int duelIndex, duel_advice_session_state_t *session)
{
	int slot;
	static const char *manualBasics[] = {
		"Quick base: GK is Grip Kick. Keep your grip and throw binds clean so your reactions stay smooth.",
		"PK means Pull Kick. Good pull windows are after movement commits, knockdowns, or saber recovery frames.",
		"Core economy: pull recovers faster and gives more repeats than push in a grip window, so budget force around that.",
		"PTK means Pull-Throw-Kick. Mix PTK pressure with saber pressure and GK threat so entries stay harder to read.",
		"Movement first: strafe-jump on approach and avoid long straight lines into your opponent’s crosshair.",
		"Simple defense tip: if the punish lane is still live, stay down briefly instead of panic-standing into damage."
	};

	slot = duelIndex;
	if (slot < 0)
		slot = 0;
	G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, manualBasics,
		(int)(sizeof(manualBasics) / sizeof(manualBasics[0])), slot, session);
}

static void G_QueueManualMetaAdvice(int botClientNum, int targetClientNum, int rotation, duel_advice_session_state_t *session)
{
	static const char *manualMeta[] = {
		"Meta read: win initiative first—bait a response, then spend force into the lane they just exposed.",
		"Meta read: rotate your entry timing every exchange so they can’t lock onto one rhythm.",
		"Meta read: keep escape force reserved, then convert advantage with short, controlled checks.",
		"Meta read: spacing and camera control come before hard commits like grip or deep pull chains.",
		"Meta read: hide your panic moments; keep movement quality high so low-health tells stay less obvious.",
		"Meta read: if they copy your last option, change lane immediately and punish the copycat habit."
	};
	int slot = rotation;
	if (slot < 0)
		slot = 0;
	G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, manualMeta,
		(int)(sizeof(manualMeta) / sizeof(manualMeta[0])), slot, session);
}

static void G_QueueManualIntermediateAdvice(int botClientNum, int targetClientNum, int rotation, duel_advice_session_state_t *session)
{
	static const char *manualIntermediate[] = {
		"Intermediate: pull pressure into throw feints, then convert only when recovery is actually exposed.",
		"Intermediate: advantage is tempo—spend in bursts, reset, then re-enter off lateral movement.",
		"Intermediate: vary anti-grip exits (delay, down-state, mixed direction) so break timing stays hard to solve.",
		"Intermediate: use side kick as a spacing interrupt, but respect the force-regen pause while airborne.",
		"Intermediate: against repeated saber throws, track return path and punish the recall window, not the launch."
	};
	int slot = rotation;
	if (slot < 0)
		slot = 0;
	G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, manualIntermediate,
		(int)(sizeof(manualIntermediate) / sizeof(manualIntermediate[0])), slot, session);
}

static void G_QueueManualGenericAdvice(int botClientNum, int targetClientNum, tracked_duel_runtime_t *runtime, qboolean loggedIn, duel_advice_session_state_t *session)
{
	int rotation;
	static const char *manualGeneric[] = {
		"Manual tempo: keep the offense mix balanced between PTK/pull-throw, saber pressure, and GK.",
		"Drain discipline: tap drain instead of panic holding; cleaner force endings help efficiency.",
		"Knockdown discipline: staying flat can deny free flipkick follow-ups; stand when danger actually clears.",
		"Toss defense: track blade path with crosshair and contest return timing, not just launch timing.",
		"GK control: vary kick types and angle changes so your breakout timing can’t be pre-read.",
		"Entry strategy: use movement gap and strafe pressure before committing force.",
		"Anti-drain set: rotate answers so opponents can’t farm one repeated anti-drain response.",
		"Map tactics: route knowledge matters—know chase paths, hide paths, and force ranges before hard commits.",
		"Advanced spacing: close only when your camera and footwork keep their snap options constrained.",
		"Advanced offense: short saber checks can set up safer pull/grip conversions than raw force-first entries."
	};

	rotation = (session ? session->duelsSeen : 0) + (runtime ? runtime->eventCount : 0);
	if (rotation < 0)
		rotation = 0;
	G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, manualGeneric,
		(int)(sizeof(manualGeneric) / sizeof(manualGeneric[0])), rotation, session);

	if (loggedIn && session && session->historyDuels > 0)
	{
		static const char *historyBlend[] = {
			"I’m factoring your history now, so repeated habits will matter more than one noisy round.",
			"History loaded—coaching will track your recurring patterns, not just one duel snapshot."
		};
		G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, historyBlend,
			(int)(sizeof(historyBlend) / sizeof(historyBlend[0])), rotation, session);
	}
	else
	{
		static const char *sessionCoaching[] = {
			"Session coaching is live—keep fundamentals clean and I’ll scale the advice with you.",
			"Good discipline this session will unlock tighter matchup-specific tips."
		};
		G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, sessionCoaching,
			(int)(sizeof(sessionCoaching) / sizeof(sessionCoaching[0])), rotation, session);
	}
}

static void G_QueueManualIssueAdvice(int botClientNum, int targetClientNum, duel_track_issue_t issue, duel_advice_session_state_t *session)
{
	switch (issue)
	{
	case DUEL_TRACK_ISSUE_LOW_FORCE:
	{
		static const char *messages[] = {
			"I’ve noticed your force crashes late in exchanges. Keep reserve force for exits before re-engaging.",
			"I’m seeing force economy leaks under pressure. Short drain taps and calmer spend should help."
		};
		G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, messages,
			(int)(sizeof(messages) / sizeof(messages[0])), G_GetTrackedIssueConfidence(session, issue), session);
		break;
	}
	case DUEL_TRACK_ISSUE_GRIP_CONTROL:
	{
		static const char *messages[] = {
			"I’ve noticed their grip control is getting clean reads. Randomize breakout timing and direction.",
			"I’m seeing predictable grip lanes. Mix down-state delay, pull breaks, and off-angle exits."
		};
		G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, messages,
			(int)(sizeof(messages) / sizeof(messages[0])), G_GetTrackedIssueConfidence(session, issue), session);
		break;
	}
	case DUEL_TRACK_ISSUE_SABER_THROW:
	{
		static const char *messages[] = {
			"I’ve noticed saber-throw punishes landing too often. Read path early, then hit the return window.",
			"I’m seeing late toss defense. Keep sticky crosshair and deny easy entry lanes."
		};
		G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, messages,
			(int)(sizeof(messages) / sizeof(messages[0])), G_GetTrackedIssueConfidence(session, issue), session);
		break;
	}
	case DUEL_TRACK_ISSUE_KNOCKDOWN:
	{
		static const char *messages[] = {
			"I’ve noticed knockdown follow-ups costing you. Recover with safer exits before full reset.",
			"I’m seeing punishable getups. Stay down through danger, then stand into movement, not into panic."
		};
		G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, messages,
			(int)(sizeof(messages) / sizeof(messages[0])), G_GetTrackedIssueConfidence(session, issue), session);
		break;
	}
	case DUEL_TRACK_ISSUE_LATE_DEFENSE:
	{
		static const char *messages[] = {
			"I’ve noticed your defense turns on late. Pre-activate before the collapse frame.",
			"I’m seeing last-second reactions repeat. Stabilize early, then counter with controlled trades."
		};
		G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, messages,
			(int)(sizeof(messages) / sizeof(messages[0])), G_GetTrackedIssueConfidence(session, issue), session);
		break;
	}
	case DUEL_TRACK_ISSUE_FORCED_ENTRIES:
	{
		static const char *messages[] = {
			"I’ve noticed forced entries into ready defense. Feint first, then convert on reaction.",
			"I’m seeing readable approach timing. Add lateral delay to desync their counter window."
		};
		G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, messages,
			(int)(sizeof(messages) / sizeof(messages[0])), G_GetTrackedIssueConfidence(session, issue), session);
		break;
	}
	case DUEL_TRACK_ISSUE_LINEAR_ENTRIES:
	{
		static const char *messages[] = {
			"I’ve noticed linear entries getting read. Rotate angles and vary your lane order.",
			"I’m seeing repeated straight-line pressure. Shift to strafe-led entries with staged PTK threat."
		};
		G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, messages,
			(int)(sizeof(messages) / sizeof(messages[0])), G_GetTrackedIssueConfidence(session, issue), session);
		break;
	}
	default:
		break;
	}
}

static void G_MaybeQueueTrackedLoginAdvice(int botClientNum, int targetClientNum, duel_advice_session_state_t *session, qboolean loggedIn)
{
	static const char *loginAdvice[] = {
		"Save the progression: /login or /register keeps your duel history and coaching data between sessions.",
		"Want this coaching and your progress to persist? Use /login after the round so the bot can keep your history.",
		"Account nudge: /login lets the bot remember your repeat patterns and saves progress across reconnects."
	};

	if (!session || loggedIn)
	{
		return;
	}

	if (!NewBotAI_ShouldQueueLoginReminder(
		session->duelsSeen,
		TRACKED_DUEL_ADVICE_LOGIN_START,
		TRACKED_DUEL_ADVICE_LOGIN_INTERVAL,
		session->lastLoginPromptDuel))
	{
		return;
	}

	G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, loginAdvice,
		(int)(sizeof(loginAdvice) / sizeof(loginAdvice[0])), session->duelsSeen, session);
	session->lastLoginPromptDuel = session->duelsSeen;
}

static void G_MaybeQueueBotTutorial(tracked_duel_runtime_t *loserRuntime, gentity_t *winner, gentity_t *loser)
{
	int botClientNum;
	duel_advice_session_state_t *session;
	duel_track_issue_t issue;
	duel_track_skill_band_t skillBand;
	qboolean loggedIn;
	qboolean basicsWindow;
	qboolean lowSignalDuel;
	qboolean allowSpecificIssue;
	int duelDuration;

	if (!bot_tutorial.integer || bot_nochat.integer || !loserRuntime || !winner || !loser ||
		!winner->client || !loser->client)
		return;
	if (!(winner->r.svFlags & SVF_BOT) || (loser->r.svFlags & SVF_BOT))
		return;

	botClientNum = winner->s.number;
	session = G_GetTrackedAdviceSession(loser, loserRuntime);
	if (!session)
		return;
	loggedIn = (loserRuntime->identityKind == DUEL_TRACK_ID_LOGIN) ? qtrue : qfalse;
	if (loggedIn)
		G_LoadTrackedAdviceHistory(session);
	issue = G_MapPrimaryIssueToTrackedIssue(loserRuntime->primaryIssue);
	if (issue < DUEL_TRACK_ISSUE_COUNT)
		session->sessionIssueCounts[issue]++;
	duelDuration = (loserRuntime->eventCount > 0) ? loserRuntime->events[loserRuntime->eventCount - 1].relTime : 0;
	lowSignalDuel = (loserRuntime->eventCount < TRACKED_DUEL_PATTERN_MIN_EVENTS ||
		duelDuration < TRACKED_DUEL_PATTERN_MIN_DURATION_MS) ? qtrue : qfalse;
	allowSpecificIssue = (issue < DUEL_TRACK_ISSUE_COUNT && G_TrackedAdviceIsSpecificAllowed(loserRuntime, session, issue)) ? qtrue : qfalse;
	skillBand = G_GetTrackedSkillBand(loserRuntime, session, 1, 0);
	if (allowSpecificIssue && session->sessionIssueCounts[issue] >= 2)
		g_botTutorialQueues[botClientNum].cooldownMs = TRACKED_DUEL_TUTORIAL_FAST_COOLDOWN_MS;
	else
		g_botTutorialQueues[botClientNum].cooldownMs = TRACKED_DUEL_TUTORIAL_COOLDOWN_MS;

	basicsWindow = (skillBand == DUEL_TRACK_SKILL_BEGINNER) &&
		((!loggedIn || (session->historyDuels <= 0)) ||
			(session->duelsSeen <= TRACKED_DUEL_ADVICE_BASIC_WINDOW));
	if (basicsWindow)
	{
		const int queuedCountBefore = g_botTutorialQueues[botClientNum].queuedCount;
		const qboolean queueWasEmpty = (queuedCountBefore <= g_botTutorialQueues[botClientNum].nextMessageIndex);

		if (session->duelsSeen <= 1)
		G_QueueManualBasicsAdvice(botClientNum, loser->s.number, session->duelsSeen - 1, session);
		else
		{
		G_QueueManualMetaAdvice(botClientNum, loser->s.number, session->duelsSeen, session);
		}
		G_MaybeQueueTrackedLoginAdvice(botClientNum, loser->s.number, session, loggedIn);
		if (g_botTutorialQueues[botClientNum].queuedCount > queuedCountBefore)
		{
			g_botTutorialQueues[botClientNum].publicBroadcast = (bot_tutorial.integer >= 2) ? qtrue : qfalse;
			if (queueWasEmpty)
				G_SetBotTutorialInitialDelay(&g_botTutorialQueues[botClientNum]);
		}
		return;
	}

	{
		const int queuedCountBefore = g_botTutorialQueues[botClientNum].queuedCount;
		const qboolean queueWasEmpty = (queuedCountBefore <= g_botTutorialQueues[botClientNum].nextMessageIndex);

		if (lowSignalDuel)
		{
			G_QueueManualGenericAdvice(botClientNum, loser->s.number, loserRuntime, loggedIn, session);
		}
		else if (allowSpecificIssue)
		{
			G_QueueManualIssueAdvice(botClientNum, loser->s.number, issue, session);
		}
		else if (skillBand == DUEL_TRACK_SKILL_ADVANCED)
		{
			G_QueueManualMetaAdvice(botClientNum, loser->s.number, session->duelsSeen + loserRuntime->eventCount, session);
		}
		else if (skillBand == DUEL_TRACK_SKILL_INTERMEDIATE || G_IsTrackedIntermediateCandidate(loserRuntime))
		{
			G_QueueManualIntermediateAdvice(botClientNum, loser->s.number, session->duelsSeen + loserRuntime->eventCount, session);
		}
		else
		{
			G_QueueManualBasicsAdvice(botClientNum, loser->s.number, session->duelsSeen, session);
		}

		G_MaybeQueueTrackedLoginAdvice(botClientNum, loser->s.number, session, loggedIn);

		if ((loserRuntime->spentByState[DUEL_TRACK_STATE_PANIC] >= 20 || loserRuntime->lateDefenseSpends >= 1) &&
			G_TrackedAdviceIsSpecificAllowed(loserRuntime, session, DUEL_TRACK_ISSUE_LATE_DEFENSE))
		{
			static const char *lateDefenseFollowups[] = {
				"I’ve noticed a second leak: defense still comes online too late in pressure windows.",
				"Another note: set anti-drain structure earlier so you are not reacting at collapse point."
			};
			G_QueueRotatingTutorialMessage(botClientNum, loser->s.number, lateDefenseFollowups,
				(int)(sizeof(lateDefenseFollowups) / sizeof(lateDefenseFollowups[0])), session->duelsSeen, session);
		}

		if (g_botTutorialQueues[botClientNum].queuedCount > queuedCountBefore)
		{
			g_botTutorialQueues[botClientNum].publicBroadcast = (bot_tutorial.integer >= 2) ? qtrue : qfalse;
			if (queueWasEmpty)
				G_SetBotTutorialInitialDelay(&g_botTutorialQueues[botClientNum]);
		}
	}
}

static void G_ProcessBotTutorialQueue(gentity_t *ent)
{
	bot_tutorial_queue_t *queue;
	gentity_t *target;
	int cooldown;
	int sayMode;

	if (!ent || !ent->client || !(ent->r.svFlags & SVF_BOT))
		return;

	queue = &g_botTutorialQueues[ent->s.number];
	if (queue->queuedCount <= 0 || queue->nextMessageIndex >= queue->queuedCount)
		return;
	if (!bot_tutorial.integer || bot_nochat.integer || queue->nextSendTime > level.time)
		return;
	if (queue->targetClientNum < 0 || queue->targetClientNum >= MAX_CLIENTS)
	{
		queue->publicBroadcast = qfalse;
		G_ClearBotTutorialQueue(ent->s.number);
		return;
	}

	target = &g_entities[queue->targetClientNum];
	if (!target->inuse || !target->client || target->client->pers.connected != CON_CONNECTED)
	{
		queue->publicBroadcast = qfalse;
		G_ClearBotTutorialQueue(ent->s.number);
		return;
	}

	sayMode = queue->publicBroadcast ? SAY_ALL : SAY_TELL;
	G_Say(ent, (sayMode == SAY_ALL) ? NULL : target, sayMode, queue->messages[queue->nextMessageIndex]);
	queue->nextMessageIndex++;
	if (queue->nextMessageIndex >= queue->queuedCount)
	{
		queue->publicBroadcast = qfalse;
		G_ClearBotTutorialQueue(ent->s.number);
	}
	else if (queue->immediateIndex >= 0 && queue->nextMessageIndex >= queue->immediateIndex)
	{
		queue->nextSendTime = level.time;
		queue->immediateIndex = -1;
	}
	else
	{
		cooldown = queue->cooldownMs > 0 ? queue->cooldownMs : TRACKED_DUEL_TUTORIAL_COOLDOWN_MS;
		queue->nextSendTime = level.time + cooldown;
	}
}

void G_QueueArcadeBotTutorial(gentity_t *speaker, gentity_t *listener, int roundNumber, qboolean betweenRounds)
{
	int rotation;
	bot_tutorial_queue_t *queue;

	if (bot_tutorial.integer < 2 || bot_nochat.integer || !speaker || !listener ||
		!speaker->client || !listener->client)
	{
		return;
	}
	if (!(speaker->r.svFlags & SVF_BOT) || (listener->r.svFlags & SVF_BOT))
	{
		return;
	}
	if (speaker->s.number < 0 || speaker->s.number >= MAX_CLIENTS)
	{
		return;
	}

	queue = &g_botTutorialQueues[speaker->s.number];
	if (queue->queuedCount > queue->nextMessageIndex)
	{
		return;
	}

	G_ClearBotTutorialQueue(speaker->s.number);
	queue = &g_botTutorialQueues[speaker->s.number];

	rotation = roundNumber;
	if (rotation < 0)
	{
		rotation = 0;
	}

	if (betweenRounds)
	{
		if (rotation <= 2)
		{
			G_QueueManualBasicsAdvice(speaker->s.number, listener->s.number, rotation, NULL);
		}
		else if ((rotation % 2) == 0)
		{
			G_QueueManualMetaAdvice(speaker->s.number, listener->s.number, rotation, NULL);
		}
		else
		{
			G_QueueManualIntermediateAdvice(speaker->s.number, listener->s.number, rotation, NULL);
		}
	}
	else
	{
		G_QueueManualGenericAdvice(speaker->s.number, listener->s.number, NULL, qfalse, NULL);
	}

	if (queue->queuedCount > queue->nextMessageIndex)
	{
		queue->publicBroadcast = qtrue;
		G_SetBotTutorialInitialDelay(queue);
	}
}

static void G_InitTrackedDuelRuntimeForClient(gentity_t *ent, gentity_t *opponent, int duelType)
{
	tracked_duel_runtime_t *runtime;

	if (!ent || !ent->client || !opponent || !opponent->client)
		return;

	runtime = &g_trackedDuels[ent->s.number];
	memset(runtime, 0, sizeof(*runtime));
	runtime->active = qtrue;
	runtime->duelType = duelType;
	runtime->duelStartTime = level.time;
	runtime->opponentClientNum = opponent->s.number;
	runtime->lastForce = ent->client->ps.fd.forcePower;
	runtime->lowestForce = ent->client->ps.fd.forcePower;
	runtime->lastHealthArmor = ent->health + ent->client->ps.stats[STAT_ARMOR];
	runtime->lastSelectedPower = ent->client->ps.fd.forcePowerSelected;
	runtime->lastPowersActive = ent->client->ps.fd.forcePowersActive;
	runtime->lastRangeBucket = G_GetTrackedRangeBucket(ent, opponent);
	runtime->lastAirborne = (ent->client->ps.groundEntityNum == ENTITYNUM_NONE) ? 1 : 0;
	runtime->lastKnockdown = BG_InKnockDown(ent->client->ps.legsAnim) ? 1 : 0;
	runtime->lastGripCripple = ent->client->ps.fd.forceGripCripple ? 1 : 0;
	runtime->side = G_GetTrackedParticipantSide(ent);
	runtime->opponentSide = G_GetTrackedParticipantSide(opponent);
	G_GetDuelTrackingIdentity(ent, runtime->identityKey, sizeof(runtime->identityKey), runtime->identityLabel, sizeof(runtime->identityLabel), &runtime->identityKind);
	G_GetDuelTrackingIdentity(opponent, runtime->opponentKey, sizeof(runtime->opponentKey), runtime->opponentLabel, sizeof(runtime->opponentLabel), NULL);
}

static duel_track_power_t G_InferTrackedPowerSpend(gentity_t *ent, tracked_duel_runtime_t *runtime)
{
	static const int sustainedPowers[] = { FP_GRIP, FP_DRAIN, FP_ABSORB, FP_PROTECT, FP_SPEED, FP_SEE, FP_RAGE };
	int i;
	duel_track_power_t mappedPower;

	if (!ent || !ent->client || !runtime)
		return DUEL_TRACK_POWER_UNKNOWN;

	mappedPower = G_MapForcePowerToTrackedPower(ent->client->ps.fd.forcePowerSelected);
	if (mappedPower != DUEL_TRACK_POWER_UNKNOWN)
		return mappedPower;

	for (i = 0; i < (int)(sizeof(sustainedPowers) / sizeof(sustainedPowers[0])); i++)
	{
		if (ent->client->ps.fd.forcePowersActive & (1 << sustainedPowers[i]))
			return G_MapForcePowerToTrackedPower(sustainedPowers[i]);
	}

	return G_MapForcePowerToTrackedPower(runtime->lastSelectedPower);
}

static void G_InsertTrackedParticipant(sqlite3 *db, sqlite3_int64 summaryId, tracked_duel_runtime_t *runtime, int won)
{
	sqlite3_stmt *stmt = NULL;
	char *sql;
	int s;
	int matchup;

	if (!runtime)
		return;

	matchup = G_GetTrackedMatchup(runtime->side, runtime->opponentSide);
	sql = "INSERT INTO LocalDuelTrackParticipant(summary_id, participant_key, participant_label, participant_kind, opponent_key, won, side, opponent_side, matchup, total_force_spent, total_force_regen, ending_force, ending_hp, ending_armor, low_force_windows, grip_cripple_events, saber_throw_punishes, knockdown_events, late_defense_spends, opening_tactic, primary_issue, spent_neutral, spent_advantage, spent_disadvantage, spent_panic, spent_finishing, force_push, force_pull, force_grip, force_drain, force_rage, force_absorb, force_protect, force_heal, force_speed, force_seeing, force_unknown) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_int64(stmt, 1, summaryId));
	CALL_SQLITE(bind_text(stmt, 2, runtime->identityKey, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 3, runtime->identityLabel, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 4, runtime->identityKind));
	CALL_SQLITE(bind_text(stmt, 5, runtime->opponentKey, -1, SQLITE_STATIC));
	if (won < 0)
	{
		CALL_SQLITE(bind_null(stmt, 6));
	}
	else
	{
		CALL_SQLITE(bind_int(stmt, 6, won ? 1 : 0));
	}
	CALL_SQLITE(bind_int(stmt, 7, runtime->side));
	CALL_SQLITE(bind_int(stmt, 8, runtime->opponentSide));
	CALL_SQLITE(bind_int(stmt, 9, matchup));
	CALL_SQLITE(bind_int(stmt, 10, runtime->totalForceSpent));
	CALL_SQLITE(bind_int(stmt, 11, runtime->totalForceRegen));
	CALL_SQLITE(bind_int(stmt, 12, runtime->endingForce));
	CALL_SQLITE(bind_int(stmt, 13, runtime->endingHP));
	CALL_SQLITE(bind_int(stmt, 14, runtime->endingArmor));
	CALL_SQLITE(bind_int(stmt, 15, runtime->lowForceWindows));
	CALL_SQLITE(bind_int(stmt, 16, runtime->gripCrippleEvents));
	CALL_SQLITE(bind_int(stmt, 17, runtime->saberThrowPunishes));
	CALL_SQLITE(bind_int(stmt, 18, runtime->knockdownEvents));
	CALL_SQLITE(bind_int(stmt, 19, runtime->lateDefenseSpends));
	CALL_SQLITE(bind_text(stmt, 20, runtime->openingTactic, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 21, runtime->primaryIssue, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 22, runtime->spentByState[DUEL_TRACK_STATE_NEUTRAL]));
	CALL_SQLITE(bind_int(stmt, 23, runtime->spentByState[DUEL_TRACK_STATE_ADVANTAGE]));
	CALL_SQLITE(bind_int(stmt, 24, runtime->spentByState[DUEL_TRACK_STATE_DISADVANTAGE]));
	CALL_SQLITE(bind_int(stmt, 25, runtime->spentByState[DUEL_TRACK_STATE_PANIC]));
	CALL_SQLITE(bind_int(stmt, 26, runtime->spentByState[DUEL_TRACK_STATE_FINISHING]));
	CALL_SQLITE(bind_int(stmt, 27, runtime->forceSpentByPower[DUEL_TRACK_POWER_PUSH]));
	CALL_SQLITE(bind_int(stmt, 28, runtime->forceSpentByPower[DUEL_TRACK_POWER_PULL]));
	CALL_SQLITE(bind_int(stmt, 29, runtime->forceSpentByPower[DUEL_TRACK_POWER_GRIP]));
	CALL_SQLITE(bind_int(stmt, 30, runtime->forceSpentByPower[DUEL_TRACK_POWER_DRAIN]));
	CALL_SQLITE(bind_int(stmt, 31, runtime->forceSpentByPower[DUEL_TRACK_POWER_RAGE]));
	CALL_SQLITE(bind_int(stmt, 32, runtime->forceSpentByPower[DUEL_TRACK_POWER_ABSORB]));
	CALL_SQLITE(bind_int(stmt, 33, runtime->forceSpentByPower[DUEL_TRACK_POWER_PROTECT]));
	CALL_SQLITE(bind_int(stmt, 34, runtime->forceSpentByPower[DUEL_TRACK_POWER_HEAL]));
	CALL_SQLITE(bind_int(stmt, 35, runtime->forceSpentByPower[DUEL_TRACK_POWER_SPEED]));
	CALL_SQLITE(bind_int(stmt, 36, runtime->forceSpentByPower[DUEL_TRACK_POWER_SEEING]));
	CALL_SQLITE(bind_int(stmt, 37, runtime->forceSpentByPower[DUEL_TRACK_POWER_UNKNOWN]));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Insert Failed (LocalDuelTrackParticipant)", s);
	CALL_SQLITE(finalize(stmt));
}

static void G_InsertTrackedEvents(sqlite3 *db, sqlite3_int64 summaryId, tracked_duel_runtime_t *runtime)
{
	sqlite3_stmt *stmt = NULL;
	sqlite3_stmt *geomStmt = NULL;
	char *sql;
	int i;
	int s;
	qboolean captureGeometry = qfalse;
	qboolean hasAnyGeometry = qfalse;
	qboolean insertFailed = qfalse;

	if (!runtime || runtime->eventCount <= 0)
		return;

	sql = "INSERT INTO LocalDuelTrackEvent(summary_id, participant_key, opponent_key, rel_time, event_index, event_type, power, amount, state, range_bucket, note) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	captureGeometry = G_IsTrackedGeometryEnabled();
	if (captureGeometry)
	{
		for (i = 0; i < runtime->eventCount; i++)
		{
			if (runtime->events[i].hasGeometry)
			{
				hasAnyGeometry = qtrue;
				break;
			}
		}
	}
	if (captureGeometry && hasAnyGeometry)
	{
		sql = "INSERT INTO LocalDuelTrackGeometry(summary_id, participant_key, opponent_key, rel_time, event_index, self_x, self_y, self_z, enemy_x, enemy_y, enemy_z, self_vx, self_vy, self_vz, enemy_vx, enemy_vy, enemy_vz, self_yaw, enemy_yaw) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &geomStmt, NULL));
	}
	CALL_SQLITE(exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL));
	for (i = 0; i < runtime->eventCount; i++)
	{
		tracked_duel_event_t *event = &runtime->events[i];
		CALL_SQLITE(bind_int64(stmt, 1, summaryId));
		CALL_SQLITE(bind_text(stmt, 2, runtime->identityKey, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_text(stmt, 3, runtime->opponentKey, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_int(stmt, 4, event->relTime));
		CALL_SQLITE(bind_int(stmt, 5, event->eventIndex));
		CALL_SQLITE(bind_text(stmt, 6, G_GetTrackedEventTypeName(event->eventType), -1, SQLITE_STATIC));
		CALL_SQLITE(bind_int(stmt, 7, event->power));
		CALL_SQLITE(bind_int(stmt, 8, event->amount));
		CALL_SQLITE(bind_int(stmt, 9, event->state));
		CALL_SQLITE(bind_int(stmt, 10, event->rangeBucket));
		CALL_SQLITE(bind_text(stmt, 11, event->note, -1, SQLITE_STATIC));
		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE)
		{
			G_ErrorPrint("ERROR: SQL Insert Failed (LocalDuelTrackEvent)", s);
			insertFailed = qtrue;
			break;
		}
		CALL_SQLITE(reset(stmt));
		CALL_SQLITE(clear_bindings(stmt));
		if (captureGeometry && hasAnyGeometry && event->hasGeometry)
		{
			CALL_SQLITE(bind_int64(geomStmt, 1, summaryId));
			CALL_SQLITE(bind_text(geomStmt, 2, runtime->identityKey, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_text(geomStmt, 3, runtime->opponentKey, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_int(geomStmt, 4, event->relTime));
			CALL_SQLITE(bind_int(geomStmt, 5, event->eventIndex));
			CALL_SQLITE(bind_double(geomStmt, 6, event->selfOrigin[0]));
			CALL_SQLITE(bind_double(geomStmt, 7, event->selfOrigin[1]));
			CALL_SQLITE(bind_double(geomStmt, 8, event->selfOrigin[2]));
			CALL_SQLITE(bind_double(geomStmt, 9, event->enemyOrigin[0]));
			CALL_SQLITE(bind_double(geomStmt, 10, event->enemyOrigin[1]));
			CALL_SQLITE(bind_double(geomStmt, 11, event->enemyOrigin[2]));
			CALL_SQLITE(bind_double(geomStmt, 12, event->selfVelocity[0]));
			CALL_SQLITE(bind_double(geomStmt, 13, event->selfVelocity[1]));
			CALL_SQLITE(bind_double(geomStmt, 14, event->selfVelocity[2]));
			CALL_SQLITE(bind_double(geomStmt, 15, event->enemyVelocity[0]));
			CALL_SQLITE(bind_double(geomStmt, 16, event->enemyVelocity[1]));
			CALL_SQLITE(bind_double(geomStmt, 17, event->enemyVelocity[2]));
			CALL_SQLITE(bind_double(geomStmt, 18, event->selfYaw));
			CALL_SQLITE(bind_double(geomStmt, 19, event->enemyYaw));
			s = sqlite3_step(geomStmt);
			if (s != SQLITE_DONE)
			{
				G_ErrorPrint("ERROR: SQL Insert Failed (LocalDuelTrackGeometry)", s);
				insertFailed = qtrue;
				break;
			}
			CALL_SQLITE(reset(geomStmt));
			CALL_SQLITE(clear_bindings(geomStmt));
		}
	}
	if (insertFailed)
	{
		CALL_SQLITE(exec(db, "ROLLBACK", NULL, NULL, NULL));
	}
	else
	{
		CALL_SQLITE(exec(db, "COMMIT", NULL, NULL, NULL));
	}
	CALL_SQLITE(finalize(stmt));
	if (captureGeometry && hasAnyGeometry)
		CALL_SQLITE(finalize(geomStmt));
}

static void G_UpdateTrackedAggregate(sqlite3 *db, tracked_duel_runtime_t *runtime, qboolean won, qboolean draw)
{
	sqlite3_stmt *stmt = NULL;
	char *sql;
	int s;
	int matchup;

	if (!runtime)
		return;

	matchup = G_GetTrackedMatchup(runtime->side, runtime->opponentSide);
	sql = "INSERT OR IGNORE INTO LocalDuelTrackAggregate(participant_key, participant_kind, side, matchup, duels, wins, losses, total_force_spent, total_force_regen, low_force_deaths, grip_cripples, saber_throw_punishes, force_push, force_pull, force_grip, force_drain, force_rage, force_absorb, force_protect, force_heal, force_speed, force_seeing, force_unknown) VALUES (?, ?, ?, ?, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_text(stmt, 1, runtime->identityKey, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 2, runtime->identityKind));
	CALL_SQLITE(bind_int(stmt, 3, runtime->side));
	CALL_SQLITE(bind_int(stmt, 4, matchup));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Insert Failed (LocalDuelTrackAggregate init)", s);
	CALL_SQLITE(finalize(stmt));

	sql = "UPDATE LocalDuelTrackAggregate SET duels = duels + 1, wins = wins + ?, losses = losses + ?, total_force_spent = total_force_spent + ?, total_force_regen = total_force_regen + ?, low_force_deaths = low_force_deaths + ?, grip_cripples = grip_cripples + ?, saber_throw_punishes = saber_throw_punishes + ?, force_push = force_push + ?, force_pull = force_pull + ?, force_grip = force_grip + ?, force_drain = force_drain + ?, force_rage = force_rage + ?, force_absorb = force_absorb + ?, force_protect = force_protect + ?, force_heal = force_heal + ?, force_speed = force_speed + ?, force_seeing = force_seeing + ?, force_unknown = force_unknown + ? WHERE participant_key = ? AND participant_kind = ? AND side = ? AND matchup = ?";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_int(stmt, 1, (won && !draw) ? 1 : 0));
	CALL_SQLITE(bind_int(stmt, 2, (!won && !draw) ? 1 : 0));
	CALL_SQLITE(bind_int(stmt, 3, runtime->totalForceSpent));
	CALL_SQLITE(bind_int(stmt, 4, runtime->totalForceRegen));
	CALL_SQLITE(bind_int(stmt, 5, runtime->didDieLowForce));
	CALL_SQLITE(bind_int(stmt, 6, runtime->gripCrippleEvents));
	CALL_SQLITE(bind_int(stmt, 7, runtime->saberThrowPunishes));
	CALL_SQLITE(bind_int(stmt, 8, runtime->forceSpentByPower[DUEL_TRACK_POWER_PUSH]));
	CALL_SQLITE(bind_int(stmt, 9, runtime->forceSpentByPower[DUEL_TRACK_POWER_PULL]));
	CALL_SQLITE(bind_int(stmt, 10, runtime->forceSpentByPower[DUEL_TRACK_POWER_GRIP]));
	CALL_SQLITE(bind_int(stmt, 11, runtime->forceSpentByPower[DUEL_TRACK_POWER_DRAIN]));
	CALL_SQLITE(bind_int(stmt, 12, runtime->forceSpentByPower[DUEL_TRACK_POWER_RAGE]));
	CALL_SQLITE(bind_int(stmt, 13, runtime->forceSpentByPower[DUEL_TRACK_POWER_ABSORB]));
	CALL_SQLITE(bind_int(stmt, 14, runtime->forceSpentByPower[DUEL_TRACK_POWER_PROTECT]));
	CALL_SQLITE(bind_int(stmt, 15, runtime->forceSpentByPower[DUEL_TRACK_POWER_HEAL]));
	CALL_SQLITE(bind_int(stmt, 16, runtime->forceSpentByPower[DUEL_TRACK_POWER_SPEED]));
	CALL_SQLITE(bind_int(stmt, 17, runtime->forceSpentByPower[DUEL_TRACK_POWER_SEEING]));
	CALL_SQLITE(bind_int(stmt, 18, runtime->forceSpentByPower[DUEL_TRACK_POWER_UNKNOWN]));
	CALL_SQLITE(bind_text(stmt, 19, runtime->identityKey, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 20, runtime->identityKind));
	CALL_SQLITE(bind_int(stmt, 21, runtime->side));
	CALL_SQLITE(bind_int(stmt, 22, matchup));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Update Failed (LocalDuelTrackAggregate update)", s);
	CALL_SQLITE(finalize(stmt));
}

static void G_PersistTrackedDuel(tracked_duel_runtime_t *winnerRuntime, tracked_duel_runtime_t *loserRuntime, int duelType, qboolean draw)
{
	sqlite3 *db;
	sqlite3_stmt *stmt = NULL;
	char *sql;
	int s;
	sqlite3_int64 summaryId;
	time_t rawtime;
	const int duration = (winnerRuntime && winnerRuntime->duelStartTime > 0) ? (level.time - winnerRuntime->duelStartTime) : 0;
	const int durationSeconds = duration / 1000;
	int endTimestamp;
	int startTimestamp;
	tracked_duel_runtime_t *summaryFirst = winnerRuntime;
	tracked_duel_runtime_t *summarySecond = loserRuntime;

	if (!winnerRuntime || !loserRuntime)
		return;

	if (draw && Q_stricmp(summaryFirst->identityKey, summarySecond->identityKey) > 0)
	{
		summaryFirst = loserRuntime;
		summarySecond = winnerRuntime;
	}

	time(&rawtime);
	endTimestamp = (int)rawtime;
	startTimestamp = endTimestamp - durationSeconds;

	CALL_SQLITE(open(LOCAL_DB_PATH, &db));
	G_EnsureLocalArcadeSchema(db);
	G_EnsureLocalDuelTrackingSchema(db);

	sql = "INSERT INTO LocalDuelTrackSummary(start_time, end_time, duration, type, mapname, winner_key, winner_label, winner_kind, winner_side, loser_key, loser_label, loser_kind, loser_side, draw, winner_opening, loser_opening) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_int(stmt, 1, startTimestamp));
	CALL_SQLITE(bind_int(stmt, 2, endTimestamp));
	CALL_SQLITE(bind_int(stmt, 3, durationSeconds));
	CALL_SQLITE(bind_int(stmt, 4, duelType));
	CALL_SQLITE(bind_text(stmt, 5, level.rawmapname, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 6, summaryFirst->identityKey, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 7, summaryFirst->identityLabel, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 8, summaryFirst->identityKind));
	CALL_SQLITE(bind_int(stmt, 9, summaryFirst->side));
	CALL_SQLITE(bind_text(stmt, 10, summarySecond->identityKey, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 11, summarySecond->identityLabel, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 12, summarySecond->identityKind));
	CALL_SQLITE(bind_int(stmt, 13, summarySecond->side));
	CALL_SQLITE(bind_int(stmt, 14, draw ? 1 : 0));
	CALL_SQLITE(bind_text(stmt, 15, summaryFirst->openingTactic, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 16, summarySecond->openingTactic, -1, SQLITE_STATIC));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Insert Failed (LocalDuelTrackSummary)", s);
	CALL_SQLITE(finalize(stmt));
	summaryId = sqlite3_last_insert_rowid(db);

	G_InsertTrackedParticipant(db, summaryId, winnerRuntime, draw ? -1 : 1);
	G_InsertTrackedParticipant(db, summaryId, loserRuntime, draw ? -1 : 0);
	G_InsertTrackedEvents(db, summaryId, winnerRuntime);
	G_InsertTrackedEvents(db, summaryId, loserRuntime);
	G_UpdateTrackedAggregate(db, winnerRuntime, qtrue, draw);
	G_UpdateTrackedAggregate(db, loserRuntime, qfalse, draw);

	CALL_SQLITE(close(db));
}

void G_StartTrackedDuel(gentity_t *first, gentity_t *second, int duelType)
{
	if (!G_IsTrackedDuelCollectionEnabled() || !first || !second || !first->client || !second->client)
		return;
	if (!G_IsTrackedDuelEligible(first, second))
		return;

	G_InitTrackedDuelRuntimeForClient(first, second, duelType);
	G_InitTrackedDuelRuntimeForClient(second, first, duelType);
}

void G_UpdateTrackedDuelFrame(gentity_t *ent)
{
	tracked_duel_runtime_t *runtime;
	gentity_t *opponent;
	int curForce, curHealthArmor, forceDelta, healthDelta;
	int curRangeBucket, airborne, knockedDown, state;
	duel_track_power_t power;

	if (!ent || !ent->client)
		return;

	if (ent->r.svFlags & SVF_BOT)
		G_ProcessBotTutorialQueue(ent);
	if (!G_IsTrackedDuelCollectionEnabled())
		return;

	runtime = &g_trackedDuels[ent->s.number];
	if (!runtime->active)
		return;
	if (runtime->opponentClientNum < 0 || runtime->opponentClientNum >= MAX_CLIENTS)
	{
		G_ClearTrackedDuelRuntime(ent->s.number);
		return;
	}

	opponent = &g_entities[runtime->opponentClientNum];
	if (!opponent->inuse || !opponent->client)
	{
		G_ClearTrackedDuelRuntime(ent->s.number);
		return;
	}

	curForce = ent->client->ps.fd.forcePower;
	curHealthArmor = ent->health + ent->client->ps.stats[STAT_ARMOR];
	curRangeBucket = G_GetTrackedRangeBucket(ent, opponent);
	airborne = (ent->client->ps.groundEntityNum == ENTITYNUM_NONE) ? 1 : 0;
	knockedDown = BG_InKnockDown(ent->client->ps.legsAnim) ? 1 : 0;
	state = G_InferTrackedForceState(ent, opponent);

	forceDelta = curForce - runtime->lastForce;
	if (forceDelta < 0)
	{
		int spent = -forceDelta;
		power = G_InferTrackedPowerSpend(ent, runtime);
		runtime->totalForceSpent += spent;
		runtime->forceSpentByPower[power] += spent;
		runtime->spentByState[state] += spent;
		if (state == DUEL_TRACK_STATE_PANIC && curForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD)
			runtime->lowForceWindows++;
		if ((power == DUEL_TRACK_POWER_ABSORB || power == DUEL_TRACK_POWER_PROTECT) &&
			(ent->health <= 45 || state == DUEL_TRACK_STATE_PANIC || state == DUEL_TRACK_STATE_DISADVANTAGE))
			runtime->lateDefenseSpends++;
		G_SetTrackedOpeningIfEmpty(runtime, power, NULL);
		G_AddTrackedDuelEvent(runtime, 0, level.time - runtime->duelStartTime, spent, power, state, curRangeBucket, NULL, ent, opponent);
	}
	else if (forceDelta > 0)
	{
		runtime->totalForceRegen += forceDelta;
		G_AddTrackedDuelEvent(runtime, 1, level.time - runtime->duelStartTime, forceDelta, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, NULL, ent, opponent);
	}

	healthDelta = curHealthArmor - runtime->lastHealthArmor;
	if (healthDelta < 0)
	{
		int taken = -healthDelta;
		const char *note = NULL;
		if (opponent->client->ps.saberInFlight)
		{
			runtime->saberThrowPunishes++;
			note = "saberthrow";
			G_SetTrackedOpeningIfEmpty(runtime, DUEL_TRACK_POWER_UNKNOWN, "saberthrow");
		}
		G_AddTrackedDuelEvent(runtime, 2, level.time - runtime->duelStartTime, taken, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, note, ent, opponent);
	}

	if (ent->client->ps.fd.forceGripCripple && !runtime->lastGripCripple)
		runtime->gripCrippleEvents++;

	if (curRangeBucket != runtime->lastRangeBucket)
		G_AddTrackedDuelEvent(runtime, 3, level.time - runtime->duelStartTime, curRangeBucket, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, NULL, ent, opponent);

	if (airborne != runtime->lastAirborne)
		G_AddTrackedDuelEvent(runtime, 4, level.time - runtime->duelStartTime, airborne, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, airborne ? "airborne" : "landed", ent, opponent);

	if (knockedDown && !runtime->lastKnockdown)
	{
		runtime->knockdownEvents++;
		G_AddTrackedDuelEvent(runtime, 5, level.time - runtime->duelStartTime, 1, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, "knockdown", ent, opponent);
	}

	if (curForce < runtime->lowestForce)
		runtime->lowestForce = curForce;

	runtime->lastForce = curForce;
	runtime->lastHealthArmor = curHealthArmor;
	runtime->lastSelectedPower = ent->client->ps.fd.forcePowerSelected;
	runtime->lastPowersActive = ent->client->ps.fd.forcePowersActive;
	runtime->lastRangeBucket = curRangeBucket;
	runtime->lastAirborne = airborne;
	runtime->lastKnockdown = knockedDown;
	runtime->lastGripCripple = ent->client->ps.fd.forceGripCripple ? 1 : 0;
}

void G_FinishTrackedDuel(gentity_t *winner, gentity_t *loser, int duelType, qboolean draw)
{
	tracked_duel_runtime_t winnerRuntime;
	tracked_duel_runtime_t loserRuntime;
	tracked_duel_runtime_t *winnerSlot;
	tracked_duel_runtime_t *loserSlot;
	qboolean winnerLowForceFinish;
	qboolean loserLowForceFinish;

	if (!winner || !loser || !winner->client || !loser->client)
		return;

	winnerSlot = &g_trackedDuels[winner->s.number];
	loserSlot = &g_trackedDuels[loser->s.number];
	if (!winnerSlot->active || !loserSlot->active)
		return;
	if (winnerSlot->opponentClientNum != loser->s.number || loserSlot->opponentClientNum != winner->s.number)
		return;

	winnerSlot->endingForce = winner->client->ps.fd.forcePower;
	winnerSlot->endingHP = winner->health;
	winnerSlot->endingArmor = winner->client->ps.stats[STAT_ARMOR];
	loserSlot->endingForce = loser->client->ps.fd.forcePower;
	loserSlot->endingHP = loser->health;
	loserSlot->endingArmor = loser->client->ps.stats[STAT_ARMOR];
	winnerLowForceFinish = (winnerSlot->endingForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD || winnerSlot->lowestForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD) ? qtrue : qfalse;
	loserLowForceFinish = (loserSlot->endingForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD || loserSlot->lowestForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD) ? qtrue : qfalse;
	loserSlot->didDieLowForce = draw ? 0 : (loserLowForceFinish ? 1 : 0);
	G_SetTrackedPrimaryIssue(winnerSlot, winnerLowForceFinish);
	G_SetTrackedPrimaryIssue(loserSlot, loserLowForceFinish);

	memcpy(&winnerRuntime, winnerSlot, sizeof(winnerRuntime));
	memcpy(&loserRuntime, loserSlot, sizeof(loserRuntime));
	G_ClearTrackedDuelRuntime(winner->s.number);
	G_ClearTrackedDuelRuntime(loser->s.number);

	G_PersistTrackedDuel(&winnerRuntime, &loserRuntime, duelType, draw);
	if (!draw)
		G_MaybeQueueBotTutorial(&loserRuntime, winner, loser);
	G_RecordTrackedSessionOutcome(winner, &winnerRuntime, draw ? qfalse : qtrue);
	G_RecordTrackedSessionOutcome(loser, &loserRuntime, qfalse);
}

void G_ClearTrackedDuelIfMismatched(gentity_t *ent, gentity_t *opponent)
{
	tracked_duel_runtime_t *runtime;
	tracked_duel_runtime_t *otherRuntime;

	if (!ent || !ent->client)
		return;

	runtime = &g_trackedDuels[ent->s.number];
	if (!runtime->active)
		return;

	if (!opponent || !opponent->client)
	{
		G_ClearTrackedDuelRuntime(ent->s.number);
		return;
	}

	otherRuntime = &g_trackedDuels[opponent->s.number];
	if (runtime->opponentClientNum == opponent->s.number &&
		(!otherRuntime->active || otherRuntime->opponentClientNum == ent->s.number))
	{
		return;
	}

	G_ClearTrackedDuelRuntime(ent->s.number);
	if (otherRuntime->active && otherRuntime->opponentClientNum == ent->s.number)
		G_ClearTrackedDuelRuntime(opponent->s.number);
}

static void G_EnsureLocalArcadeSchema(sqlite3 *db)
{
	sqlite3_stmt *stmt = NULL;
	char *sql;
	int s;
	qboolean hasMapname = qfalse;

	sql = "CREATE TABLE IF NOT EXISTS LocalArcade(id INTEGER PRIMARY KEY, username VARCHAR(16), mapname VARCHAR(64), score UNSIGNED INTEGER, level UNSIGNED SMALLINT, kills UNSIGNED SMALLINT, end_time UNSIGNED INTEGER)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (InitGameAccountStuff arcade)", s);
	CALL_SQLITE(finalize(stmt));
	stmt = NULL;

	sql = "PRAGMA table_info(LocalArcade)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	while ((s = sqlite3_step(stmt)) == SQLITE_ROW)
	{
		const unsigned char *columnName = sqlite3_column_text(stmt, 1);
		if (columnName && !Q_stricmp((const char *)columnName, "mapname"))
		{
			hasMapname = qtrue;
			break;
		}
	}

	if (s != SQLITE_DONE && s != SQLITE_ROW)
		G_ErrorPrint("ERROR: SQL Select Failed (LocalArcade schema)", s);
	CALL_SQLITE(finalize(stmt));
	stmt = NULL;

	if (!hasMapname)
	{
		sql = "ALTER TABLE LocalArcade ADD COLUMN mapname VARCHAR(64) DEFAULT ''";
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE)
			G_ErrorPrint("ERROR: SQL Alter Failed (LocalArcade mapname)", s);
		CALL_SQLITE(finalize(stmt));
	}
}

#if 0
typedef struct RaceRecord_s {
	char				username[16];
	char				coursename[40];
	unsigned int		duration_ms;
	unsigned short		topspeed;
	unsigned short		average;
	unsigned short		style; //only needs to be 3 bits	
	char				end_time[64]; //Why a char?
	unsigned int		end_timeInt;
} RaceRecord_t;

RaceRecord_t	HighScores[32][MV_NUMSTYLES][10];//32 courses, 9 styles, 10 spots on highscore list

typedef struct PersonalBests_s {
	char				username[16];
	char				coursename[40];
	unsigned int		duration_ms;
	unsigned short		style; //only needs to be 3 bits	
} PersonalBests_t;

PersonalBests_t	PersonalBests[9][50];//9 styles, 50 cached spots
#endif

#if 0
typedef struct UserStats_s {
	char				username[16];
	unsigned short		kills;
	unsigned short		deaths;
	unsigned short		suicides;
	unsigned short		captures;
	unsigned short		returns;
} UserStats_t;

UserStats_t	UserStats[256];//256 max logged in users per map :/
#endif

/*
typedef struct UserAccount_s {
	char			username[16];
	char			password[16];
	int				lastIP;
} UserAccount_t;
*/

/*
typedef struct PlayerID_s {
	char			name[MAX_NETNAME];
	unsigned int	ip[NET_ADDRSTRMAXLEN];
	int				guid[33];
} PlayerID_t;
*/

void getDateTime(int time, char * timeStr, size_t timeStrSize) {
	time_t	timeGMT;
	//time -= 60*60*5; //EST timezone -5? 
	timeGMT = (time_t)time;
	strftime( timeStr, timeStrSize, "%m/%d/%y %I:%M %p", localtime( &timeGMT ) );
}

unsigned int ip_to_int (const char * ip) {
    unsigned v = 0;
    int i;
    const char * start;

    start = ip;
    for (i = 0; i < 4; i++) {
        char c;
        int n = 0;
        while (1) {
            c = * start;
            start++;
            if (c >= '0' && c <= '9') {
                n *= 10;
                n += c - '0';
            }
            else if ((i < 3 && c == '.') || i == 3)
                break;
            else
                return 0;
        }
        if (n >= 256)
            return 0;
        v *= 256;
        v += n;
    }
    return v;
}

void G_ErrorPrint( const char *fmt, int s ) {
	trap->SendServerCommand( -1, va("print \"%s %i\n\"", fmt, s) );
	G_SecurityLogPrintf(fmt);
}

/*
static void CleanStrin(char &string) {
	
	int i = 0;
	//while (string[i]) {
		//string[i] = tolower(string[i]);
		i++;
	//}
	//Q_CleanStr(string);
}
*/

/*
void DebugWriteToDB(char *entrypoint) {
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	int s;
	char username[16], password[16];

	Q_strncpyz(username, "test", sizeof(username));
	Q_strncpyz(password, "test", sizeof(password));

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));

	sql = "UPDATE LocalAccount SET password = ? WHERE username = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));

	s = sqlite3_step(stmt);

	if (s != SQLITE_DONE)
		G_SecurityLogPrintf( "ERROR: Could not write to database with error %i, entrypoint %s\n", s, entrypoint );

	CALL_SQLITE (finalize(stmt));
	CALL_SQLITE (close(db));
}
*/

int CheckUserExists(char *username) { //make this accept existing DB so we dont have to open/close it
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	int row = 0, s;

	//load fixme replace this with simple select count

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
	sql = "SELECT id FROM LocalAccount WHERE username = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));
	
    while (1) {
        s = sqlite3_step(stmt);
        if (s == SQLITE_ROW) {
            row++;
        }
        else if (s == SQLITE_DONE)
            break;
        else {
			G_ErrorPrint("ERROR: SQL Select Failed (CheckUserExists)", s);
			break;
        }
    }

	CALL_SQLITE (finalize(stmt));
	CALL_SQLITE (close(db));

	//DebugWriteToDB("CheckUserExists");

	if (row == 0) {
		return 0;
	}
	if (row == 1) { //Only 1 matching accound
		return 1;
	}

	trap->Print("ERROR: Multiple accounts with same accountname!\n");
	return 0;

}
void G_AddPlayerLog(char *name, char *strIP, char *guid) {
	fileHandle_t f;
	char string[128], string2[128], buf[80*1024], cleanName[MAX_NETNAME];
	char *p = NULL;
	int	fLen = 0;
	char*	pch;
	qboolean unique = qtrue;

	p = strchr(strIP, ':');
	if (p) //loda - fix ip sometimes not printing
		*p = 0;

	if (!Q_stricmp(strIP, "") && !Q_stricmp(guid, "NOGUID")) //No IP or GUID info to be gained.. so forget it
		return;

	Q_strncpyz(cleanName, name, sizeof(cleanName));

	Q_strlwr(cleanName);
	Q_CleanStr(cleanName);

	if (!Q_stricmp(name, "padawan")) //loda fixme, also ignore Padawan[0] etc..
		return;
	if (!Q_stricmp(name, "padawan[0]")) //loda fixme, also ignore Padawan[0] etc..
		return;
	if (!Q_stricmp(name, "padawan[1]")) //loda fixme, also ignore Padawan[0] etc..
		return;

	Com_sprintf(string, sizeof(string), "%s;%s;%s", cleanName, strIP, guid); //Store ip as char

	fLen = trap->FS_Open(PLAYER_LOG, &f, FS_READ);
	if (!f) {
		Com_Printf ("ERROR: Couldn't load player logfile %s\n", PLAYER_LOG);
		return;
	}
	
	if (fLen >= 80*1024) {
		trap->FS_Close(f);
		Com_Printf ("ERROR: Couldn't load player logfile %s, file is too large\n", PLAYER_LOG);
		return;
	}

	trap->FS_Read(buf, fLen, f);
	buf[fLen] = 0;
	trap->FS_Close(f);

	pch = strtok (buf,"\n");

	while (pch != NULL) {
		if (!Q_stricmp(string, pch)) {
			unique = qfalse;
			break;
		}
    	pch = strtok (NULL, "\n");
	}

	if (unique) {
		Com_sprintf(string2, sizeof(string2), "%s\n", string);
		trap->FS_Write(string2, strlen(string2), level.playerLog );
	}

	//If line does not already exist, write it.

	//ftell, fseek

	//Check to see if IP already exists
		//If so, check if username is same
			//If not, update username (or add to it?)
	
		//If not, check if GUID exists
			//If yes, update IP of guid, check to see if name is same
				//If no, update username (or add to it)

			//If not (no IP or guid), add entry.

	//Somehow make this sorted..
}

qboolean G_GetDuelParticipantName(gentity_t *ent, char *name, int nameSize) {
	char userinfo[MAX_INFO_STRING];
	int level;

	if (!name || nameSize < 1) {
		return qfalse;
	}

	name[0] = '\0';

	if (!ent || !ent->client) {
		return qfalse;
	}

	if (ent->client->pers.userName[0]) {
		Q_strncpyz(name, ent->client->pers.userName, nameSize);
		return qtrue;
	}

	if (!g_eloRanking.integer || !g_newBotAI.integer || !(ent->r.svFlags & SVF_BOT)) {
		return qfalse;
	}

	trap->GetUserinfo(ent->s.number, userinfo, sizeof(userinfo));
	level = atoi(Info_ValueForKey(userinfo, "skill"));
	if (level < 1 || level > 10) {
		return qfalse;
	}

	Com_sprintf(name, nameSize, "botlvl%i", level);
	return qtrue;
}

#if _ELORANKING	

int GetDuelCount(char *username, int type, int end_time, sqlite3 * db) {
    char * sql;
    sqlite3_stmt * stmt;
	int s;
	int count = 0;

	//Get Current Count, Get last duel id, get new duel id
	sql = "SELECT COUNT(*) FROM LocalDuel WHERE type = ? AND (winner = ? OR loser = ?) AND end_time < ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_int (stmt, 1, type));
	CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_text (stmt, 3, username, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_int (stmt, 4, end_time));

	s = sqlite3_step(stmt);

	if (s == SQLITE_ROW) {
		count = sqlite3_column_int(stmt, 0);
	}
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (GetDuelCount)", s);
	}

	CALL_SQLITE (finalize(stmt));

	return count;
}

float GetDuelElo( char *username, int type, int end_time, sqlite3 * db) {
	float elo = -999.0f;
    char * sql;
    sqlite3_stmt * stmt;
	int s;

	sql = "SELECT winner_elo AS elo, end_time FROM LocalDuel where type = ? AND winner = ? AND end_time < ? "
		"UNION ALL SELECT loser_elo AS elo, end_time FROM LocalDuel where type = ? AND loser = ? AND end_time < ? "
		"ORDER BY end_time DESC LIMIT 1";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_int (stmt, 1, type));
	CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_int (stmt, 3, end_time));
	CALL_SQLITE (bind_int (stmt, 4, type));
	CALL_SQLITE (bind_text (stmt, 5, username, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_int (stmt, 6, end_time));
	
	s = sqlite3_step(stmt);

	if (s == SQLITE_ROW) {
		elo = sqlite3_column_double(stmt, 0);
	}
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (GetDuelElo)", s);
	}

	if (elo == -999.0f) {//This needs to just be done to check if its null
		elo = 1000; //Elo not found, give them initial value
	}

	CALL_SQLITE (finalize(stmt));

	//Com_Printf("Getting duel elo %.2f %s %i %i\n", elo, username, type, end_time);

	return elo;
}

void UpdatePlayerRating(char *username, int type, qboolean winner, float newElo, float odds, int id, sqlite3 * db) {
    char * sql;
    sqlite3_stmt * stmt;
	int s;

	if (id) {//We are doing a rebuild of everything so we cant trust end_time, also we can optimize it by using the known id to update
		if (winner)
			sql = "UPDATE LocalDuel SET winner_elo = ?, odds = ? WHERE id = ?";
		else
			sql = "UPDATE LocalDuel SET loser_elo = ?, odds = ? WHERE id = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_double (stmt, 1, newElo));
		CALL_SQLITE (bind_double (stmt, 2, odds));
		CALL_SQLITE (bind_int (stmt, 3, id));
	}
	else {
		if (winner)
			sql = "UPDATE LocalDuel SET winner_elo = ?, odds = ? WHERE type = ? AND winner = ? AND end_time = (SELECT MAX(end_time) FROM LocalDuel WHERE type = ? and winner = ?)";
		else
			sql = "UPDATE LocalDuel SET loser_elo = ?, odds = ? WHERE type = ? AND loser = ? AND end_time = (SELECT MAX(end_time) FROM LocalDuel WHERE type = ? and loser = ?)";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_double (stmt, 1, newElo));
		CALL_SQLITE (bind_double (stmt, 2, odds));
		CALL_SQLITE (bind_int (stmt, 3, type));
		CALL_SQLITE (bind_text (stmt, 4, username, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 5, type));
		CALL_SQLITE (bind_text (stmt, 6, username, -1, SQLITE_STATIC));
	}

	s = sqlite3_step(stmt);

	if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Update Failed (UpdatePlayerRating)", s);
	}

	CALL_SQLITE (finalize(stmt));
}

int GetEloKValue(int numDuels) { //Also take rank into account
	int k1 = g_eloKValue1.integer;
	int k2 = g_eloKValue2.integer;
	int k3 = g_eloKValue3.integer;

	int p = g_eloProvisionalCutoff.integer;
	int n = g_eloNewUserCutoff.integer;

	if (k1 < 1)
		k1 = 1;
	if (k2 < 1)
		k2 = 1;
	if (k3 < 1)
		k3 = 1;
	if (p < 0)
		p = 0;
	if (n < 0)
		n = 0;

	if (numDuels <= n) //BRAND NEW PLAYER
		return k1;
	if (numDuels <= p) //PROVISIONAL
		return k2;
	return k3;
}

static int G_ParseBotLevelName(const char *name)
{
	int level = 0;
	const char *suffix;
	const char *p;

	if (!name || strncmp(name, "botlvl", 6))
	{
		return 0;
	}

	suffix = name + 6;
	if (!suffix[0])
	{
		return 0;
	}
	for (p = suffix; *p; p++)
	{
		if (*p < '0' || *p > '9')
		{
			return 0;
		}
	}

	level = atoi(suffix);
	if (level < BOT_DUEL_LEVEL_MIN || level > BOT_DUEL_LEVEL_MAX)
	{
		return 0;
	}

	return level;
}

static int G_GetRankedBotVsBotLevelDuelsToday(int botLevel, int end_time, sqlite3 *db)
{
	char *sql;
	sqlite3_stmt *stmt;
	int s;
	int count = 0;
	sqlite3_int64 dayStart;
	sqlite3_int64 dayEnd;
	sqlite3_int64 endTime64;
	char botLevelName[16];

	if (!db || botLevel < BOT_DUEL_LEVEL_MIN || botLevel > BOT_DUEL_LEVEL_MAX)
	{
		return 0;
	}

	// end_time is a Unix epoch timestamp (UTC seconds from time()).
	endTime64 = (sqlite3_int64)end_time;
	dayStart = endTime64 - (endTime64 % 86400);
	dayEnd = dayStart + 86400;
	Com_sprintf(botLevelName, sizeof(botLevelName), "botlvl%i", botLevel);

	sql = "SELECT COUNT(*) FROM LocalDuel "
		"WHERE end_time >= ? AND end_time < ? "
		"AND winner_elo > -998 AND loser_elo > -998 "
		"AND winner LIKE 'botlvl%' AND loser LIKE 'botlvl%' "
		"AND (winner = ? OR loser = ?)";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_int64 (stmt, 1, dayStart));
	CALL_SQLITE (bind_int64 (stmt, 2, dayEnd));
	CALL_SQLITE (bind_text (stmt, 3, botLevelName, -1, SQLITE_TRANSIENT));
	CALL_SQLITE (bind_text (stmt, 4, botLevelName, -1, SQLITE_TRANSIENT));

	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW) {
		count = sqlite3_column_int(stmt, 0);
	}
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (G_GetRankedBotVsBotLevelDuelsToday)", s);
	}

	CALL_SQLITE (finalize(stmt));
	return count;
}

static qboolean G_ShouldRankBotVsBotDuel(const char *winner, const char *loser, int end_time, sqlite3 *db)
{
	int winnerLevel;
	int loserLevel;
	int winnerDailyCount;
	int loserDailyCount;

	winnerLevel = G_ParseBotLevelName(winner);
	loserLevel = G_ParseBotLevelName(loser);
	// Duel result names come from G_GetDuelParticipantName: bots without account names are
	// normalized to botlvlN so rank throttling can treat bot levels as a shared bucket.

	if (!winnerLevel || !loserLevel)
	{
		return qtrue;
	}

	winnerDailyCount = G_GetRankedBotVsBotLevelDuelsToday(winnerLevel, end_time, db);
	loserDailyCount = (loserLevel == winnerLevel) ? winnerDailyCount :
		G_GetRankedBotVsBotLevelDuelsToday(loserLevel, end_time, db);

	if (loserLevel == winnerLevel)
	{
		if (winnerDailyCount >= BOT_DUEL_RANKED_LIMIT_PER_LEVEL)
		{
			return qfalse;
		}
	}
	else if (winnerDailyCount >= BOT_DUEL_RANKED_LIMIT_PER_LEVEL ||
		loserDailyCount >= BOT_DUEL_RANKED_LIMIT_PER_LEVEL)
	{
		return qfalse;
	}

	return qtrue;
}

static void G_AddDuelToDBWithHandle(sqlite3 *db, char *winner, char *loser, int type, int duration, int winner_hp, int winner_shield, int end_time)
{
	char *sql;
	sqlite3_stmt *stmt;
	int s;

	sql = "INSERT INTO LocalDuel(winner, loser, duration, type, winner_hp, winner_shield, end_time, winner_elo, loser_elo, odds) VALUES (?, ?, ?, ?, ?, ?, ?, -999, -999, 0)";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, winner, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_text (stmt, 2, loser, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_int (stmt, 3, duration));
	CALL_SQLITE (bind_int (stmt, 4, type));
	CALL_SQLITE (bind_int (stmt, 5, winner_hp));
	CALL_SQLITE (bind_int (stmt, 6, winner_shield));
	CALL_SQLITE (bind_int (stmt, 7, end_time));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Insert Failed (G_AddDuelToDB)", s);
	}

	CALL_SQLITE (finalize(stmt));
}

void G_AddDuelToDB(char *winner, char *loser, int type, int duration, int winner_hp, int winner_shield, int end_time) {
	sqlite3 * db;

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
	G_AddDuelToDBWithHandle(db, winner, loser, type, duration, winner_hp, winner_shield, end_time);

	CALL_SQLITE (close(db));
}

void G_AddDuelElo(char *winner, char *loser, int type, int duration, int winner_hp, int winner_shield, int id, int end_time, sqlite3 *db) { //id and end_time are passed through if its a /rebuildElo 
	int winnerDuelCount, loserDuelCount, winnerType, loserType, winnerK, loserK;
	float expectedScoreWinner, expectedScoreLoser, WA, LA, loserElo, winnerElo, newWinnerElo, newLoserElo;
	const int NEWUSER = 0, PROVISIONAL = 1, NORMAL = 2;

	int newUserCutoff = g_eloNewUserCutoff.integer;
	int provisionalCutoff = g_eloProvisionalCutoff.integer;
	float provisionalChangeBig = g_eloProvisionalChangeBig.value;
	float provisionalChangeSmall = g_eloProvisionalChangeSmall.value;

	if (newUserCutoff < 0)
		newUserCutoff = 0;
	if (provisionalCutoff < 0)
		provisionalCutoff = 0;
	if (provisionalChangeBig < 0.1f)
		provisionalChangeBig = 0.1f;
	if (provisionalChangeSmall < 0.1f)
		provisionalChangeSmall = 0.1f;

	winnerDuelCount = GetDuelCount(winner, type, end_time, db);
	loserDuelCount = GetDuelCount(loser, type, end_time, db);

	if (winnerDuelCount < 0) //Error i guess
		return;
	if (loserDuelCount < 0)
		return;

	if (winnerDuelCount <= newUserCutoff)
		winnerType = NEWUSER;
	else if (winnerDuelCount <= provisionalCutoff)
		winnerType = PROVISIONAL;
	else
		winnerType = NORMAL;

	if (loserDuelCount <= newUserCutoff)
		loserType = NEWUSER;
	else if (loserDuelCount <= provisionalCutoff)
		loserType = PROVISIONAL;
	else
		loserType = NORMAL;

	if (winnerType == NEWUSER)
		winnerElo = 1000; //always have newusers kept at 1k elo until they get enough duels?
	else 
		winnerElo = GetDuelElo(winner, type, end_time, db);

	if (winnerElo == -999.0f) //Error i guess
		return; 

	if (loserType == NEWUSER)
		loserElo = 1000; //loda fixme
	else
		loserElo = GetDuelElo(loser, type, end_time, db);

	if (loserElo == -999.0f) //Error i guess
		return; 
		
	winnerK = GetEloKValue(winnerDuelCount);
	loserK = GetEloKValue(loserDuelCount);

	if (winnerType == PROVISIONAL) {
		if (loserType == PROVISIONAL || loserType == NEWUSER) //Loser is also provisional
			winnerK *= provisionalChangeSmall;
		else 
			winnerK *= provisionalChangeBig;
	}

	if (loserType == PROVISIONAL) { //PROVISIONAL, calculate loser rank
		if (winnerType == PROVISIONAL || winnerType == NEWUSER) //Winner is also provisional
			loserK *= provisionalChangeSmall;
		else 
			loserK *= provisionalChangeBig;
	}

	WA = pow(10, winnerElo / 400.0f);
	LA = pow(10, loserElo / 400.0f);

	expectedScoreWinner = WA / (WA + LA);
	expectedScoreLoser = 1 - expectedScoreWinner;
	//Round to.. 5th digit? or..
	//expectedScoreLoser = LA / (LA + WA); //This is just 1 - expected score winner..?

	//if (winnerType == PROVISIONAL || winnerType == NORMAL) //Nvm about this.. rank their first duels i guess.
		newWinnerElo = winnerElo + winnerK * (1 - expectedScoreWinner);

	//if (loserType == PROVISIONAL || loserType == NORMAL)
		newLoserElo = loserElo + loserK * (0 - expectedScoreLoser);

	if (!id) { //We are not doing a rebuild, so add the duel here after we get the needed info
		 G_AddDuelToDB(winner, loser, type, duration, winner_hp, winner_shield, end_time);
	}

	if (newWinnerElo != winnerElo) //Update winner elo
		UpdatePlayerRating(winner, type, qtrue, newWinnerElo, expectedScoreWinner, id, db);

	if (newLoserElo != loserElo) //Update loser elo
		UpdatePlayerRating(loser, type, qfalse, newLoserElo, expectedScoreLoser, id, db);

	//Com_Printf("Adding duel: odds = %.2f, %s [%.2f -> %.2f (c=%i) (t=%i)] > %s [%.2f -> %.2f (c=%i) (t=%i)] {%i}\n", 
		//expectedScoreWinner, winner, winnerElo, newWinnerElo, winnerDuelCount, winnerType, loser, loserElo, newLoserElo, loserDuelCount, loserType, type);
}

void SV_RebuildElo_f() {
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	int s;
	int time1 = trap->Milliseconds();

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));

	sql = "UPDATE LocalDuel SET winner_elo = -999, loser_elo = -999, odds = 0";//Save rank into row - use null
    //sql = "DELETE FROM DuelRanks";
    CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Update Failed (SV_RebuildElo_f 1)", s);
	}
	CALL_SQLITE (finalize(stmt));

	sql = "SELECT winner, loser, type, id, end_time from LocalDuel ORDER BY end_time ASC";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	
    while (1) {
        s = sqlite3_step(stmt);
        if (s == SQLITE_ROW) {
			G_AddDuelElo((char*)sqlite3_column_text(stmt, 0), (char*)sqlite3_column_text(stmt, 1), sqlite3_column_int(stmt, 2), 0, 0, 0, sqlite3_column_int(stmt, 3), sqlite3_column_int(stmt, 4), db);
        }
        else if (s == SQLITE_DONE) {
            break;
		}
        else {
			G_ErrorPrint("ERROR: SQL Select Failed (SV_RebuildElo_f 2)", s);
			break;
        }
    }
	
	CALL_SQLITE (finalize(stmt));
	CALL_SQLITE (close(db));

	Com_Printf("Duel ranks cleared in %i ms.\n", trap->Milliseconds() - time1);
}

void SV_BotEloReset_f(void) {
	char input[32];
	char botName[16];
	sqlite3 *db;
	char *sql;
	sqlite3_stmt *stmt;
	int botLevel;
	int s;
	int winnerRows = 0;
	int loserRows = 0;

	if (trap->Argc() != 2) {
		trap->Print("Usage: bot_eloreset <botlvl1-10|1-10>\n");
		return;
	}

	trap->Argv(1, input, sizeof(input));
	Q_strlwr(input);
	Q_CleanStr(input);

	if (input[0] >= '0' && input[0] <= '9') {
		botLevel = atoi(input);
		Com_sprintf(botName, sizeof(botName), "botlvl%i", botLevel);
	}
	else {
		Q_strncpyz(botName, input, sizeof(botName));
	}

	botLevel = G_ParseBotLevelName(botName);
	if (!botLevel) {
		trap->Print("Usage: bot_eloreset <botlvl1-10|1-10>\n");
		return;
	}

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));

	sql = "UPDATE LocalDuel SET winner_elo = 1000 WHERE winner = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, botName, -1, SQLITE_TRANSIENT));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Update Failed (SV_BotEloReset_f winner)", s);
	}
	else {
		winnerRows = sqlite3_changes(db);
	}
	CALL_SQLITE (finalize(stmt));

	sql = "UPDATE LocalDuel SET loser_elo = 1000 WHERE loser = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, botName, -1, SQLITE_TRANSIENT));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Update Failed (SV_BotEloReset_f loser)", s);
	}
	else {
		loserRows = sqlite3_changes(db);
	}
	CALL_SQLITE (finalize(stmt));

	CALL_SQLITE (close(db));

	trap->Print("bot_eloreset: reset %s ELO to 1000 in %i winner rows and %i loser rows.\n", botName, winnerRows, loserRows);
}

int DuelTypeToInteger(char *style) {
	Q_strlwr(style);
	Q_CleanStr(style);

	if (!Q_stricmp(style, "saber") || !Q_stricmp(style, "nf"))
		return 0;
	if (!Q_stricmp(style, "force") || !Q_stricmp(style, "fullforce") || !Q_stricmp(style, "ff"))
		return 1;
	if (!Q_stricmp(style, "melee") || !Q_stricmp(style, "fists") || !Q_stricmp(style, "fisticuffs"))
		return 4;
	if (!Q_stricmp(style, "pistol"))
		return 6;
	if (!Q_stricmp(style, "blaster") || !Q_stricmp(style, "e11"))
		return 7;
	if (!Q_stricmp(style, "sniper") || !Q_stricmp(style, "disruptor"))
		return 8;
	if (!Q_stricmp(style, "bowcaster") || !Q_stricmp(style, "crossbow"))
		return 9;
	if (!Q_stricmp(style, "repeater"))
		return 10;
	if (!Q_stricmp(style, "demp2"))
		return 11;
	if (!Q_stricmp(style, "flechette") || !Q_stricmp(style, "shotgun"))
		return 12;
	if (!Q_stricmp(style, "rocket") || !Q_stricmp(style, "rocket launcher") || !Q_stricmp(style, "rl"))
		return 13;
	if (!Q_stricmp(style, "thermal") || !Q_stricmp(style, "grenade"))
		return 14;
	if (!Q_stricmp(style, "tripmine"))
		return 15;
	if (!Q_stricmp(style, "detpack"))
		return 16;
	if (!Q_stricmp(style, "concussion") || !Q_stricmp(style, "conc"))
		return 17;
	if (!Q_stricmp(style, "bryar pistol") || !Q_stricmp(style, "bryar"))
		return 18;
	if (!Q_stricmp(style, "stun baton") || !Q_stricmp(style, "stun"))
		return 19;
	if (!Q_stricmp(style, "all"))
		return 20;
	if (!Q_stricmp(style, "arcade"))
		return 21;
	return -1;
}

void IntegerToDuelType(int type, char *typeString, size_t typeStringSize) {
	switch(type) {
		case 0: Q_strncpyz(typeString, "saber", typeStringSize); break;
		case 1: Q_strncpyz(typeString, "force", typeStringSize); break;
		case 4:	Q_strncpyz(typeString, "melee", typeStringSize);	break;
		case 6:	Q_strncpyz(typeString, "pistol", typeStringSize); break;
		case 7:	Q_strncpyz(typeString, "blaster", typeStringSize); break;
		case 8:	Q_strncpyz(typeString, "sniper", typeStringSize); break;
		case 9:	Q_strncpyz(typeString, "bowcaster", typeStringSize); break;
		case 10: Q_strncpyz(typeString, "repeater", typeStringSize); break;
		case 11: Q_strncpyz(typeString, "demp2", typeStringSize); break;
		case 12: Q_strncpyz(typeString, "flechette", typeStringSize); break;
		case 13: Q_strncpyz(typeString, "rocket", typeStringSize); break;
		case 14: Q_strncpyz(typeString, "thermal", typeStringSize); break;
		case 15: Q_strncpyz(typeString, "trip mine", typeStringSize); break;
		case 16: Q_strncpyz(typeString, "det pack", typeStringSize); break;
		case 17: Q_strncpyz(typeString, "concussion", typeStringSize); break;
		case 18: Q_strncpyz(typeString, "bryar pistol", typeStringSize); break;
		case 19: Q_strncpyz(typeString, "stun baton", typeStringSize); break;
		case 20: Q_strncpyz(typeString, "all weapons", typeStringSize); break;
		case 21: Q_strncpyz(typeString, "arcade", typeStringSize); break;
		default: Q_strncpyz(typeString, "ERROR", typeStringSize); break;
	}
}

void Cmd_DuelTop10_f(gentity_t *ent) {
	char username[32], typeString[32], inputString[32];
	const int args = trap->Argc();
	int type = -1, page = -1, start = 0, input, i, minimumCount = g_eloMinimumDuels.integer;

	if (args <= 1 || args > 3) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /top <dueltype> <page>\n\"");
		return;
	}

	for (i = 1; i < args; i++) {
		trap->Argv(i, inputString, sizeof(inputString));
		if (type == -1) {
			input = DuelTypeToInteger(inputString);
			if (input != -1) {
				type = input;
				continue;
			}
		}
		if (page == -1) {
			input = atoi(inputString);
			if (input > 0) {
				page = input;
				continue;
			}
		}
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /top <dueltype> <page>\n\"");
		return;
	}

	if (type == -1) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /top <dueltype> <page>\n\"");
		return;
	}

	IntegerToDuelType(type, typeString, sizeof(typeString));
	if (page < 1)
		page = 1;
	if (page > 1000)
		page = 1000;
	start = (page - 1) * 10;

	if (minimumCount < 0)
		minimumCount = 0;

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int rank, count, TS, s, row = 1;
		char msg[1024-128] = {0};

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));

		if (type == 21)
		{
			sql = "WITH has_map(map_exists) AS (SELECT EXISTS(SELECT 1 FROM LocalArcade WHERE mapname = ?)) "
				"SELECT username, score, level, kills FROM LocalArcade, has_map "
				"WHERE (has_map.map_exists = 1 AND LocalArcade.mapname = ?) "
				"OR (has_map.map_exists = 0 AND (LocalArcade.mapname = '' OR LocalArcade.mapname IS NULL)) "
				"ORDER BY " LOCAL_ARCADE_SCORE_ORDER " LIMIT ?, 10";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_text (stmt, 1, level.rawmapname, -1, SQLITE_STATIC));
			CALL_SQLITE (bind_text (stmt, 2, level.rawmapname, -1, SQLITE_STATIC));
			CALL_SQLITE (bind_int (stmt, 3, start));

			trap->SendServerCommand(ent-g_entities, va("print \"Topscore results for arcade on %s:\n ^5#   Username           Score      Level  Kills\n\"", level.rawmapname));
			while (1) {
				s = sqlite3_step(stmt);
				if (s == SQLITE_ROW) {
					char *tmpMsg = NULL;
					int score = 0, levelReached = 0, kills = 0;

					Q_strncpyz(username, (char*)sqlite3_column_text(stmt, 0), sizeof(username));
					score = sqlite3_column_int(stmt, 1);
					levelReached = sqlite3_column_int(stmt, 2);
					kills = sqlite3_column_int(stmt, 3);

					tmpMsg = va("^5%-3i ^3%-18s ^3%-10i ^3%-6i %i\n", start+row, username, score, levelReached, kills);
					if (strlen(msg) + strlen(tmpMsg) >= sizeof(msg)) {
						trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));
						msg[0] = '\0';
					}
					Q_strcat(msg, sizeof(msg), tmpMsg);
					row++;
				}
				else if (s == SQLITE_DONE) {
					trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));
					break;
				}
				else {
					G_ErrorPrint("ERROR: SQL Select Failed (Cmd_DuelTop10_f arcade)", s);
					break;
				}
			}
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}

		//We dont need to select from loser since we know a users highscore will always be from a winning duel.  And we can ignore users who have never won a duel(?)
		//How to get count?
		//sql = "SELECT winner, winner_elo, 100, 100 FROM (SELECT winner, winner_elo, odds, end_time FROM LocalDuel WHERE type = ? ORDER BY end_time ASC) GROUP BY winner ORDER BY winner_elo DESC LIMIT 10";
		sql = "SELECT D1.username, elo, 100-ROUND(100*(win_ts + loss_ts)/(win_count+loss_count), 0) AS TS, win_count+loss_count AS count "
				"FROM ((SELECT username, type, elo FROM ((SELECT winner AS username, type, ROUND(winner_elo,0) AS elo, end_time FROM LocalDuel WHERE type = ? "
				"UNION ALL SELECT loser AS username, type, ROUND(loser_elo,0) AS elo, end_time FROM LocalDuel WHERE type = ? ORDER BY end_time ASC)) GROUP BY username ORDER BY elo DESC) AS D1 "
				"INNER JOIN (SELECT winner AS username2, COUNT(*) AS win_count, SUM(odds) AS win_ts FROM LocalDuel WHERE type = ? GROUP BY username2) AS D2 "
				"ON D1.username = D2.username2) "
				"INNER JOIN (SELECT loser AS username3, COUNT(*) AS loss_count, SUM(1-odds) AS loss_ts FROM LocalDuel WHERE type = ? GROUP BY username3) AS D3 "
				"ON D1.username = D3.username3 ORDER BY elo desc LIMIT ?, 10";

		//loda fixme
		/*
		sql = "SELECT username, rank, count, TSSUM \
			FROM DuelRanks WHERE type = ? AND count > ? \
			GROUP BY username ORDER BY rank DESC LIMIT 10";
		*/

		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_int (stmt, 1, type));
		CALL_SQLITE (bind_int (stmt, 2, type));
		CALL_SQLITE (bind_int (stmt, 3, type));
		CALL_SQLITE (bind_int (stmt, 4, type));
		CALL_SQLITE (bind_int (stmt, 5, start));

		//CALL_SQLITE (bind_int (stmt, 2, type));
		//CALL_SQLITE (bind_int (stmt, 3, minimumCount));

		trap->SendServerCommand(ent-g_entities, va("print \"Topscore results for %s duels:\n    ^5Username           Skill        TS        Count\n\"", typeString));
	
		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char *tmpMsg = NULL;

				Q_strncpyz(username, (char*)sqlite3_column_text(stmt, 0), sizeof(username));
				rank = sqlite3_column_int(stmt, 1);
				count = sqlite3_column_int(stmt, 2);
				TS = sqlite3_column_int(stmt, 3);

				tmpMsg = va("^5%2i^3: ^3%-18s ^3%-12i ^3%-9i %i\n", start+row, username, rank, count, TS);
				if (strlen(msg) + strlen(tmpMsg) >= sizeof( msg)) {
					trap->SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
					msg[0] = '\0';
				}
				Q_strcat(msg, sizeof(msg), tmpMsg);
				row++;
			}
			else if (s == SQLITE_DONE) {
				trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));
				break;
			}
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_DuelTop10_f)", s);
				break;
			}
		}

		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
	}
}
#endif

void G_AddDuel(char *winner, char *loser, int start_time, int type, int winner_hp, int winner_shield) {
	sqlite3 * db;
	time_t	rawtime;
	char	string[256] = {0};
	const int duration = start_time ? (level.time - start_time) : 0;

	time( &rawtime );
	localtime( &rawtime );

	Com_sprintf(string, sizeof(string), "%s;%s;%i;%i;%i;%i;%i\n", winner, loser, duration, type, winner_hp, winner_shield, rawtime);

	if (level.duelLog)
		trap->FS_Write(string, strlen(string), level.duelLog ); //Always write to text file, this file is remade every mapchange and its contents are put to database.

	//Might want to make this log to file, and have that sent to db on map change.  But whatever.. duel finishes are not as frequent as race course finishes usually.

#if _ELORANKING	
	if (g_eloRanking.integer) {
		CALL_SQLITE (open (LOCAL_DB_PATH, & db));
		{
			const qboolean shouldRankDuel = G_ShouldRankBotVsBotDuel(winner, loser, rawtime, db);
			if (shouldRankDuel)
			{
				G_AddDuelElo(winner, loser, type, duration, winner_hp, winner_shield, 0, rawtime, db);
			}

			else
			{
				G_AddDuelToDBWithHandle(db, winner, loser, type, duration, winner_hp, winner_shield, rawtime);
			}
		}
		CALL_SQLITE (close(db));
	}
#endif

}

void G_AddArcadeScore(const char *username, const char *mapname, int score, int levelReached, int kills, int end_time)
{
	sqlite3 *db;
	sqlite3_stmt *stmt;
	char *sql;
	int s;

	if (!username || !username[0] || !mapname || !mapname[0])
	{
		return;
	}

	CALL_SQLITE(open(LOCAL_DB_PATH, &db));
	sql = "INSERT INTO LocalArcade(username, mapname, score, level, kills, end_time) VALUES (?, ?, ?, ?, ?, ?)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT));
	CALL_SQLITE(bind_text(stmt, 2, mapname, -1, SQLITE_TRANSIENT));
	CALL_SQLITE(bind_int(stmt, 3, score));
	CALL_SQLITE(bind_int(stmt, 4, levelReached));
	CALL_SQLITE(bind_int(stmt, 5, kills));
	CALL_SQLITE(bind_int(stmt, 6, end_time));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Insert Failed (G_AddArcadeScore)", s);
	}
	CALL_SQLITE(finalize(stmt));
	CALL_SQLITE(close(db));
}

qboolean G_GetArcadeTopScore(const char *mapname, int *scoreOut, char *usernameOut, int usernameOutSize)
{
	sqlite3 *db;
	sqlite3_stmt *stmt;
	char *sql;
	int s;
	qboolean found = qfalse;

	if (scoreOut)
	{
		*scoreOut = 0;
	}
	if (usernameOut && usernameOutSize > 0)
	{
		usernameOut[0] = '\0';
	}
	if (!mapname || !mapname[0])
	{
		return qfalse;
	}

	CALL_SQLITE(open(LOCAL_DB_PATH, &db));
	sql = "WITH has_map(map_exists) AS (SELECT EXISTS(SELECT 1 FROM LocalArcade WHERE mapname = ?)) "
		"SELECT username, score FROM LocalArcade, has_map "
		"WHERE (has_map.map_exists = 1 AND LocalArcade.mapname = ?) "
		"OR (has_map.map_exists = 0 AND (LocalArcade.mapname = '' OR LocalArcade.mapname IS NULL)) "
		"ORDER BY " LOCAL_ARCADE_SCORE_ORDER " LIMIT 1";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_text(stmt, 1, mapname, -1, SQLITE_TRANSIENT));
	CALL_SQLITE(bind_text(stmt, 2, mapname, -1, SQLITE_TRANSIENT));
	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW)
	{
		if (scoreOut)
		{
			*scoreOut = sqlite3_column_int(stmt, 1);
		}
		if (usernameOut && usernameOutSize > 0)
		{
			const unsigned char *name = sqlite3_column_text(stmt, 0);
			Q_strncpyz(usernameOut, name ? (const char *)name : "", usernameOutSize);
		}
		found = qtrue;
	}
	else if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Select Failed (G_GetArcadeTopScore)", s);
	}
	CALL_SQLITE(finalize(stmt));
	CALL_SQLITE(close(db));
	return found;
}

#if 0
void G_TestAddDuel() {
	char winner[32], loser[32], type[32];
	int time1;

	if (trap->Argc() != 4) {
		Com_Printf("Usage: /addDuel winner loser type\n");
		return;
	}

	trap->Argv(1, winner, sizeof(winner));
	trap->Argv(2, loser, sizeof(loser));
	trap->Argv(3, type, sizeof(type));

	time1 = trap->Milliseconds();

	G_AddDuel(winner, loser, level.time-1000, atoi(type), 420, 420);

	Com_Printf("Adding duel elo, took %i ms\n", trap->Milliseconds() - time1);
}
#endif

#if !_NEWRACERANKING
void G_AddToDBFromFile(void) { //loda fixme, we can filter out the slower times from file before we add them??.. keep a record idk..?
	fileHandle_t f;	
	int		fLen = 0, args = 1, s = 0, row = 0, i, j; //MAX_FILESIZE = 4096
	char	buf[MAX_TMP_RACELOG_SIZE] = {0}, empty[8] = {0};//eh
	char*	pch;
	sqlite3 * db;
	char* sql;
	sqlite3_stmt * stmt;
	RaceRecord_t	TempRaceRecord[1024] = {0}; //Max races per map to count i gues..? should this be zeroed?
	qboolean good = qfalse, foundFaster = qfalse;

	fLen = trap->FS_Open(TEMP_RACE_LOG, &f, FS_READ);

	if (!f) {
		trap->Print("ERROR: Couldn't load defrag data from %s\n", TEMP_RACE_LOG);
		return;
	}
	if (fLen >= MAX_TMP_RACELOG_SIZE) {
		trap->FS_Close(f);
		trap->Print("ERROR: Couldn't load defrag data from %s, file is too large\n", TEMP_RACE_LOG);
		return;
	}

	trap->FS_Read(buf, fLen, f);
	buf[fLen] = 0;
	trap->FS_Close(f);
	
	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
	sql = "INSERT INTO LocalRun (username, coursename, duration_ms, topspeed, average, style, end_time) VALUES (?, ?, ?, ?, ?, ?, ?)";	 //loda fixme, make multiple?

	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));

	//Todo: make TempRaceRecord an array of structs instead, maybe like 32 long idk, and build a query to insert 32 at a time or something.. instead of 1 by 1
	pch = strtok (buf,";\n");
	while (pch != NULL)
	{
		if ((args % 7) == 1)
			Q_strncpyz(TempRaceRecord[row].username, pch, sizeof(TempRaceRecord[row].username));
		else if ((args % 7) == 2)
			Q_strncpyz(TempRaceRecord[row].coursename, pch, sizeof(TempRaceRecord[row].coursename));
		else if ((args % 7) == 3)
			TempRaceRecord[row].duration_ms = atoi(pch);
		else if ((args % 7) == 4)
			TempRaceRecord[row].topspeed = atoi(pch);
		else if ((args % 7) == 5)
			TempRaceRecord[row].average = atoi(pch);
		else if ((args % 7) == 6)
			TempRaceRecord[row].style = atoi(pch);
		else if ((args % 7) == 0) {
			TempRaceRecord[row].end_timeInt = atoi(pch);

			if (row >= 1024) {
				trap->Print("ERROR: Too many entries in %s! Could not add to database.\n", TEMP_RACE_LOG);
				return;
			}
			row++;
		}
    	pch = strtok (NULL, ";\n");
		args++;
	}

	for (i=0;i<1024;i++) {
		//This is the time we are looking at [i]

		//See if we can find any faster times

		for (j=0;j<1024;j++) {	
			foundFaster = qfalse;

			if (!Q_stricmp(TempRaceRecord[j].username, TempRaceRecord[i].username) &&
				!Q_stricmp(TempRaceRecord[j].coursename, TempRaceRecord[i].coursename) &&
				TempRaceRecord[j].style == TempRaceRecord[i].style)
			{
				if (TempRaceRecord[j].duration_ms < TempRaceRecord[i].duration_ms) { //we found a faster time
					foundFaster = qtrue;
					//trap->Print("Found a faster time!\n");
					break;
				}
			}
			if (!TempRaceRecord[j].username[0])
				break;
		}
		if (!TempRaceRecord[i].username[0])//see what happens if we move this up here..
			break;

		if (TempRaceRecord[i].end_timeInt <= 0)
			break;

		if (!foundFaster) {
			const int place = i;//The fuck is this.. shut the compiler up

			//Debug this..
			//G_SecurityLogPrintf( "ADDING RACE TIME TO DB WITH PLACE %i: %s, %s, %u, %u, %u, %u, %u \n", 
				//place, TempRaceRecord[place].username, TempRaceRecord[place].coursename, TempRaceRecord[place].duration_ms, TempRaceRecord[place].topspeed, TempRaceRecord[place].average, TempRaceRecord[place].style, TempRaceRecord[place].end_timeInt);

			CALL_SQLITE (bind_text (stmt, 1, TempRaceRecord[place].username, -1, SQLITE_STATIC));
			CALL_SQLITE (bind_text (stmt, 2, TempRaceRecord[place].coursename, -1, SQLITE_STATIC));
			CALL_SQLITE (bind_int64 (stmt, 3, TempRaceRecord[place].duration_ms));
			CALL_SQLITE (bind_int64 (stmt, 4, TempRaceRecord[place].topspeed));
			CALL_SQLITE (bind_int64 (stmt, 5, TempRaceRecord[place].average));
			CALL_SQLITE (bind_int64 (stmt, 6, TempRaceRecord[place].style));
			CALL_SQLITE (bind_int64 (stmt, 7, TempRaceRecord[place].end_timeInt));

			//CALL_SQLITE_EXPECT (step (stmt), DONE);
			s = sqlite3_step(stmt);

			CALL_SQLITE (reset (stmt));
			CALL_SQLITE (clear_bindings (stmt));

			//AddRunToWebServer(TempRaceRecord[place]);
		}
	}

	//s = sqlite3_step(stmt); //this duplicates last one..? LODA FIXME, this inserts empty row

	if (s == SQLITE_DONE) {
		good = qtrue;
	}

	CALL_SQLITE (finalize(stmt));
	CALL_SQLITE (close(db));	

	if (good) { //dont delete tmp file if mysql database is not responding 
		trap->FS_Open(TEMP_RACE_LOG, &f, FS_WRITE);
		trap->FS_Write( empty, strlen( empty ), level.tempRaceLog );
		trap->FS_Close(f);
		trap->Print("Loaded previous map racetimes from %s.\n", TEMP_RACE_LOG);
	}
	else 
		trap->Print("Unable to insert previous map racetimes into database.\n");

	//DebugWriteToDB("G_AddToDBFromFile");
}
#endif

//ntity_t *G_SoundTempEntity( vec3_t origin, int event, int channel );
#if 0
void PlayActualGlobalSound2(char * sound) { //loda fixme, just go through each client and play it on them..?
	gentity_t	*te;
	vec3_t temp = {0, 0, 0};

	te = G_SoundTempEntity( temp, EV_GENERAL_SOUND, EV_GLOBAL_SOUND );
	te->s.eventParm = G_SoundIndex(sound);
	//te->s.saberEntityNum = channel;
	te->s.eFlags = EF_SOUNDTRACKER;
	te->
	r.svFlags |= SVF_BROADCAST;
}
#endif
void PlayActualGlobalSound(int soundindex) {
	gentity_t *player;
	int i;

	//G_AddEvent(ent, EV_GLOBAL_SOUND, soundindex); //need to svf_broadcast firsT? and what ent to use ??

	for (i=0; i<MAX_CLIENTS; i++) {//Build a list of clients
		if (!g_entities[i].inuse)
			continue;
		player = &g_entities[i];
		G_Sound(player, CHAN_AUTO, soundindex);
	}
}

#if _RACECACHE
void WriteToTmpRaceLog(char *string, size_t stringSize) {
	int fLen = 0;
	fileHandle_t f;

	if (level.tempRaceLog) //Lets try only writing to temp file if we know its a highscore
		trap->FS_Write(string, stringSize, level.tempRaceLog ); //Always write to text file, this file is remade every mapchange and its contents are put to database.

	fLen = trap->FS_Open(TEMP_RACE_LOG, &f, FS_READ);

	if (!f) {
		trap->Print("ERROR: Couldn't read defrag data from %s\n", TEMP_RACE_LOG);
		return;
	}	

	if (fLen >= (MAX_TMP_RACELOG_SIZE - ((int)stringSize * 2))) { //Just to be safe..
		trap->FS_Close(f);
		trap->SendServerCommand(-1, va("print \"WARNING: %s file is too large, reloading map to clear it.\n\"", TEMP_RACE_LOG));
		trap->SendConsoleCommand( EXEC_APPEND, "map_restart 0\n" );
		return;
	}
}
#endif

void IntegerToRaceName(int style, char *styleString, size_t styleStringSize) {
	switch(style) {
		case 0: Q_strncpyz(styleString, "siege", styleStringSize); break;
		case 1: Q_strncpyz(styleString, "jka", styleStringSize); break;
		case 2:	Q_strncpyz(styleString, "qw", styleStringSize);	break;
		case 3:	Q_strncpyz(styleString, "cpm", styleStringSize); break;
		case 4:	Q_strncpyz(styleString, "q3", styleStringSize); break;
		case 5:	Q_strncpyz(styleString, "pjk", styleStringSize); break;
		case 6:	Q_strncpyz(styleString, "wsw", styleStringSize); break;
		case 7:	Q_strncpyz(styleString, "rjq3", styleStringSize); break;
		case 8:	Q_strncpyz(styleString, "rjcpm", styleStringSize); break;
		case 9:	Q_strncpyz(styleString, "swoop", styleStringSize); break;
		case 10: Q_strncpyz(styleString, "jetpack", styleStringSize); break;
		case 11: Q_strncpyz(styleString, "speed", styleStringSize); break;
#if _SPPHYSICS
		case 12: Q_strncpyz(styleString, "sp", styleStringSize); break;
#endif
		case 13: Q_strncpyz(styleString, "slick", styleStringSize); break;
		case 14: Q_strncpyz(styleString, "botcpm", styleStringSize); break;
#if _COOP
		case 15: Q_strncpyz(styleString, "coop", styleStringSize); break;
#endif
		case 16: Q_strncpyz(styleString, "ocpm", styleStringSize); break;
		case 17: Q_strncpyz(styleString, "tribes", styleStringSize); break;
		case 18: Q_strncpyz(styleString, "surf", styleStringSize); break;
		case 19: Q_strncpyz(styleString, "flow", styleStringSize); break;
		default: Q_strncpyz(styleString, "ERROR", styleStringSize); break;
	}
}

void CleanupLocalRun() { //This should never actually change anything since we insert/update properly, but just to be safe..
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	int s;

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));

	sql = "DELETE FROM LocalRun WHERE id NOT IN (SELECT id FROM (SELECT id, coursename, username, style, season FROM LocalRun ORDER BY duration_ms DESC) AS T GROUP BY T.username, T.coursename, T.style, T.season)";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	//Print to textfile what got deleted since this should never happen? failRaceLog

	s = sqlite3_step(stmt);
	if (s == SQLITE_DONE)
		trap->Print("Cleaned up racetimes\n");
	else 
		G_ErrorPrint("ERROR: SQL Delete Failed (CleanupLocalRun)", s);

	CALL_SQLITE (finalize(stmt));
	CALL_SQLITE (close(db));

	//DebugWriteToDB("CleanupLocalRun");
}

#if 0
void G_GetRaceScore(int id, char *username, char *coursename, int style, int season, int time, sqlite3 * db) { //Need to use transactions here or something
	char * sql;
	sqlite3_stmt * stmt;
	int s, season_count = 0, season_rank = 0, global_count = 0, global_rank = 0, i = 1;

	//This has to be done like, 15k times currently.  15k x 10ms = like 4 minutes :(

	//Combine 1st and 2nd queries?
	//Can we sub query this 1st query - Select global count, then select that, season count from global count? -- not possible cuz we want distinct count (dont double count a user if they have multiple season entries)
	
	//Get season count and global count of races on specified course/style
	sql = "SELECT COUNT(*) FROM LocalRun WHERE coursename = ? AND style = ? AND season = ? "
		"UNION ALL SELECT COUNT(DISTINCT username) FROM LocalRun WHERE coursename = ? AND style = ?";//Select count for that course,style
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, coursename, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_int (stmt, 2, style));
	CALL_SQLITE (bind_int (stmt, 3, season));
	CALL_SQLITE (bind_text (stmt, 4, coursename, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_int (stmt, 5, style));

	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW)
		season_count = sqlite3_column_int(stmt, 0);
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (G_GetRaceScore 1)", s);
	}

	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW)
		global_count = sqlite3_column_int(stmt, 0);
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (G_GetRaceScore 2)", s);
	}
	CALL_SQLITE (finalize(stmt));

	//Get season rank
	sql = "SELECT id FROM LocalRun WHERE coursename = ? AND style = ? AND season = ? ORDER BY duration_ms ASC, end_time ASC"; //assume just one per person to speed this up..
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, coursename, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_int (stmt, 2, style));
	CALL_SQLITE (bind_int (stmt, 3, season));
    while (1) {
        s = sqlite3_step(stmt);
        if (s == SQLITE_ROW) {
			if (id == sqlite3_column_int(stmt, 0)) {
				season_rank = i;
				break;
			}
			i++;
        }
        else if (s == SQLITE_DONE)
            break;
        else {
            G_ErrorPrint("ERROR: SQL Select Failed (G_GetRaceScore 3)", s);
			break;
        }
    }
	CALL_SQLITE (finalize(stmt));

	i = 1; // AH HA ha

	//can we index on duration_ms ?
	//Get global rank - if its a season PB but not a global PB, leave global rank at 0
	sql = "SELECT id, MIN(duration_ms) AS duration FROM LocalRun WHERE coursename = ? AND style = ? GROUP BY username ORDER BY duration ASC, end_time ASC"; 
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, coursename, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_int (stmt, 2, style));
    while (1) {
        s = sqlite3_step(stmt);
        if (s == SQLITE_ROW) {
			if (id == sqlite3_column_int(stmt, 0)) {
				global_rank = i;
				break;
			}
			i++;
        }
        else if (s == SQLITE_DONE)
            break;
        else {
            G_ErrorPrint("ERROR: SQL Select Failed (G_GetRaceScore 4)", s);
			break;
        }
    }
	CALL_SQLITE (finalize(stmt));
	
	//Save rank into row
	sql = "UPDATE LocalRun SET rank = ?, entries = ?, season_rank = ?, season_entries = ?, last_update = ? WHERE id = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_int (stmt, 1, global_rank));
	CALL_SQLITE (bind_int (stmt, 2, global_count));
	CALL_SQLITE (bind_int (stmt, 3, season_rank));
	CALL_SQLITE (bind_int (stmt, 4, season_count));
	CALL_SQLITE (bind_int (stmt, 5, time));
	CALL_SQLITE (bind_int (stmt, 6, id));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Update Failed (G_GetRaceScore 5)", s);
	CALL_SQLITE (finalize(stmt));

}
#endif

void G_GetRaceScore(int id, char *username, char *coursename, int style, int season, int invalid, int season_count, int global_count, int time, sqlite3 * db) { //Need to use transactions here or something
	char * sql;
	sqlite3_stmt * stmt;
	int s, season_rank = 0, global_rank = 0, i = 1;
	int duration = 0, lastDuration = 0;

	//This has to be done like, 15k times currently.  15k x 10ms = like 4 minutes :(

	//Get season rank
	sql = "SELECT id, duration_ms FROM LocalRun WHERE coursename = ? AND style = ? AND season = ? ORDER BY duration_ms ASC, end_time ASC, average DESC"; //assume just one per person to speed this up..
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_text(stmt, 1, coursename, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 2, style));
	CALL_SQLITE(bind_int(stmt, 3, season));
	while (1) {
		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			if (style == MV_COOP_JKA) {
				duration = sqlite3_column_int(stmt, 1);
				if (duration == lastDuration) {
					i--;
				}
				lastDuration = duration;
			}
			if (id == sqlite3_column_int(stmt, 0)) {
				season_rank = i;
				break;
			}
			i++;
		}
		else if (s == SQLITE_DONE)
			break;
		else {
			G_ErrorPrint("ERROR: SQL Select Failed (G_GetRaceScore 3)", s);
			break;
		}
	}
	CALL_SQLITE(finalize(stmt));

	i = 1; // AH HA ha
	lastDuration = 0;

	//can we index on duration_ms ?
	//Get global rank - if its a season PB but not a global PB, leave global rank at 0
	//don't do this if it's invalid?
	if (!invalid) {
		sql = "SELECT id, MIN(duration_ms) AS duration FROM LocalRun WHERE coursename = ? AND style = ? AND invalid = 0 GROUP BY username ORDER BY duration ASC, end_time ASC, average DESC";
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		CALL_SQLITE(bind_text(stmt, 1, coursename, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_int(stmt, 2, style));
		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				if (style == MV_COOP_JKA) {
					duration = sqlite3_column_int(stmt, 1);
					if (duration == lastDuration) {
						i--;
					}
					lastDuration = duration;
				}

				if (id == sqlite3_column_int(stmt, 0)) {
					global_rank = i;
					break;
				}
				i++;
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (G_GetRaceScore 4)", s);
				break;
			}
		}
		CALL_SQLITE(finalize(stmt));
	}

	//Save rank into row
	sql = "UPDATE LocalRun SET rank = ?, entries = ?, season_rank = ?, season_entries = ?, last_update = ? WHERE id = ?";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_int(stmt, 1, global_rank));
	CALL_SQLITE(bind_int(stmt, 2, global_count));
	CALL_SQLITE(bind_int(stmt, 3, season_rank));
	CALL_SQLITE(bind_int(stmt, 4, season_count));
	CALL_SQLITE(bind_int(stmt, 5, time));
	CALL_SQLITE(bind_int(stmt, 6, id));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Update Failed (G_GetRaceScore 5)", s);
	CALL_SQLITE(finalize(stmt));

}

#if 0
void SV_RebuildRaceRanks_f(void) {
	char * sql;
    sqlite3_stmt * stmt;
	sqlite3 * db;
	int s;//, row = 0;
	time_t	rawtime;

	time( &rawtime );
	localtime( &rawtime );

	CleanupLocalRun();//Make sure no duplicate entries

	CALL_SQLITE (open (LOCAL_DB_PATH, & db)); 

	sql = "SELECT id, username, coursename, style, season FROM LocalRun ORDER BY end_time ASC";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
    while (1) {
        s = sqlite3_step(stmt);
        if (s == SQLITE_ROW) {
			G_GetRaceScore(sqlite3_column_int(stmt, 0), (char*)sqlite3_column_text(stmt, 1), (char*)sqlite3_column_text(stmt, 2), sqlite3_column_int(stmt, 3), sqlite3_column_int(stmt, 4), rawtime, db);

        }
        else if (s == SQLITE_DONE)
            break;
        else {
			G_ErrorPrint("ERROR: SQL Select Failed (SV_RebuildRaceRanks_f)", s);
			break;
        }
    }
	CALL_SQLITE (finalize(stmt));

	CALL_SQLITE (close(db));

}
#endif

void SV_RebuildRaceRanks_f(void) {
	char * sql;
	sqlite3_stmt * stmt;
	sqlite3 * db;
	int s;//, row = 0;
	time_t	rawtime;

	time(&rawtime);
	localtime(&rawtime);

	CleanupLocalRun();//Make sure no duplicate entries

	CALL_SQLITE(open(LOCAL_DB_PATH, &db));
	
	//This doesn't have to be ordered at all does it?
	//also select invalid, pass thru to get racescore, and skip global rank calc if its invalid
	sql = "SELECT LR1.id, LR1.username, LR1.coursename, LR1.style, LR1.season, LR1.invalid, LR2.season_count, LR3.global_count FROM "
		"(SELECT id, username, coursename, style, season, invalid FROM LocalRun) AS LR1 "
		"LEFT JOIN "
		"(SELECT coursename, style, season, COUNT(*) AS season_count FROM LocalRun GROUP BY coursename, style, season) AS LR2 "
		"ON LR1.style = LR2.style AND LR1.coursename = LR2.coursename AND LR1.season = LR2.season "
		"LEFT JOIN "
		"(SELECT coursename, style, COUNT(DISTINCT username) AS global_count FROM LocalRun GROUP BY coursename, style) AS LR3 "
		"ON LR1.style = LR3.style AND LR1.coursename = LR3.coursename";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	while (1) {
		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			G_GetRaceScore(sqlite3_column_int(stmt, 0), (char*)sqlite3_column_text(stmt, 1), (char*)sqlite3_column_text(stmt, 2),
				sqlite3_column_int(stmt, 3), sqlite3_column_int(stmt, 4), sqlite3_column_int(stmt, 5), sqlite3_column_int(stmt, 6), sqlite3_column_int(stmt, 7), rawtime, db);

			//G_UpdateUnlocks((char*)sqlite3_column_text(stmt, 1), (char*)sqlite3_column_text(stmt, 2), sqlite3_column_int(stmt, 3), NULL, db);
		}
		else if (s == SQLITE_DONE)
			break;
		else {
			G_ErrorPrint("ERROR: SQL Select Failed (SV_RebuildRaceRanks_f)", s);
			break;
		}
	}
	CALL_SQLITE(finalize(stmt));

	CALL_SQLITE(close(db));

}

void SV_MigrateCheckpoints_f(void) {
	char * sql;
	sqlite3_stmt * stmt;
	sqlite3 * db;
	int s;
	qboolean hasCheckpoints = qfalse;

	CALL_SQLITE(open(LOCAL_DB_PATH, &db));

	sql = "PRAGMA table_info(LocalRun)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	while (1) {
		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			const char *columnName = (const char *)sqlite3_column_text(stmt, 1);
			if (columnName && !Q_stricmp(columnName, "checkpoints")) {
				hasCheckpoints = qtrue;
				break;
			}
		}
		else if (s == SQLITE_DONE) {
			break;
		}
		else {
			G_ErrorPrint("ERROR: SQL Select Failed (SV_MigrateCheckpoints_f)", s);
			break;
		}
	}
	CALL_SQLITE(finalize(stmt));

	if (hasCheckpoints) {
		trap->Print("LocalRun.checkpoints already exists.\n");
		CALL_SQLITE(close(db));
		return;
	}

	sql = "ALTER TABLE LocalRun ADD COLUMN checkpoints TEXT";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s == SQLITE_DONE)
		trap->Print("Added LocalRun.checkpoints column.\n");
	else
		G_ErrorPrint("ERROR: SQL Alter Failed (SV_MigrateCheckpoints_f)", s);
	CALL_SQLITE(finalize(stmt));

	CALL_SQLITE(close(db));
}

static QINLINE int G_GetSeason(void) {
	return 6;
}

static void G_UpdateOurLocalRun(sqlite3 * db, int seasonOldRank_self, int seasonNewRank_self, int globalOldRank_self, int globalNewRank_self, int style_self, char *username_self, char *coursename_self, 
	int duration_ms_self, int topspeed_self, int average_self, int end_time_self, int seasonCount, int globalCount, char *checkpoints_self) {
	char * sql;
	sqlite3_stmt * stmt;
	int s;
	const int season = G_GetSeason();
	char emptyCheckpoints[1] = {0};

	if (!checkpoints_self)
		checkpoints_self = emptyCheckpoints;

	//Get count
	//Insert it +1 (including ourself)

	//If it is our best time of all seasons (if globalNewRank_self), we need to make our other season entries set to rank=0 !!!!
	//This also needs to update lastupdatetime for the website!
	if (globalNewRank_self && globalOldRank_self) { //And globalOldRankSelf ? - We dont want to update other rows if we dont have any other rows
		sql = "UPDATE LocalRun SET rank = 0, last_update = ? WHERE username = ? AND coursename = ? AND style = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_int (stmt, 1, end_time_self));
		CALL_SQLITE (bind_text (stmt, 2, username_self, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 3, coursename_self, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 4, style_self));
		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Update Failed (G_UpdateOurLocalRun 1)", s);
		}
		CALL_SQLITE (finalize(stmt));
	}

	if (seasonOldRank_self == -1) { //First attempt of the season
		sql = "INSERT INTO LocalRun (username, coursename, duration_ms, topspeed, average, style, season, end_time, rank, entries, season_rank, season_entries, last_update, invalid, checkpoints) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?)";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, username_self, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 2, coursename_self, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 3, duration_ms_self));
		CALL_SQLITE (bind_int (stmt, 4, topspeed_self));
		CALL_SQLITE (bind_int (stmt, 5, average_self));
		CALL_SQLITE (bind_int (stmt, 6, style_self));
		CALL_SQLITE (bind_int (stmt, 7, season));
		CALL_SQLITE (bind_int (stmt, 8, end_time_self));
		CALL_SQLITE (bind_int (stmt, 9, globalNewRank_self));
		CALL_SQLITE (bind_int (stmt, 10, globalCount));
		CALL_SQLITE (bind_int (stmt, 11, seasonNewRank_self));
		CALL_SQLITE (bind_int (stmt, 12, seasonCount));
		CALL_SQLITE (bind_int (stmt, 13, end_time_self));
		CALL_SQLITE (bind_text (stmt, 14, checkpoints_self, -1, SQLITE_STATIC));
		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE) {
			char string[1024] = {0};

			Com_sprintf(string, sizeof(string), "%s;%s;%i;%i;%i;%i;%i;%i\n", username_self, coursename_self, duration_ms_self, topspeed_self, average_self, style_self, season, end_time_self);
			trap->FS_Write( string, strlen( string ), level.failRaceLog );

			G_ErrorPrint("ERROR: SQL Insert Failed (G_UpdateOurLocalRun 2)", s);
		}
		CALL_SQLITE (finalize(stmt));
	}
	else {
		sql = "UPDATE LocalRun SET duration_ms = ?, topspeed = ?, average = ?, end_time = ?, rank = ?, season_rank = ?, last_update = ?, invalid = 0, checkpoints = ? WHERE username = ? AND coursename = ? AND style = ? AND season = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_int (stmt, 1, duration_ms_self));
		CALL_SQLITE (bind_int (stmt, 2, topspeed_self));
		CALL_SQLITE (bind_int (stmt, 3, average_self));
		CALL_SQLITE (bind_int (stmt, 4, end_time_self));
		CALL_SQLITE (bind_int (stmt, 5, globalNewRank_self));
		CALL_SQLITE (bind_int (stmt, 6, seasonNewRank_self));
		CALL_SQLITE (bind_int (stmt, 7, end_time_self));
		CALL_SQLITE (bind_text (stmt, 8, checkpoints_self, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 9, username_self, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 10, coursename_self, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 11, style_self));
		CALL_SQLITE (bind_int (stmt, 12, season));
		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE) {
			char string[1024] = {0};

			Com_sprintf(string, sizeof(string), "%s;%s;%i;%i;%i;%i;%i;%i\n", username_self, coursename_self, duration_ms_self, topspeed_self, average_self, style_self, season, end_time_self);
			trap->FS_Write( string, strlen( string ), level.failRaceLog );

			G_ErrorPrint("ERROR: SQL Update Failed (G_UpdateOurLocalRun 3)", s);
		}
		CALL_SQLITE (finalize(stmt));
	}

}

static void G_UpdateOtherLocalRun(sqlite3 * db, int seasonNewRank_self, int seasonOldRank_self, int globalNewRank_self, int globalOldRank_self, int style_self, char *coursename_self, int time) {
	char * sql;
	sqlite3_stmt * stmt;
	int s;
	const int season = G_GetSeason();

	//Problem? someone completeing a first attempt of the season which is also their first global attempt.  This affects the entries of people from other seasons?
	
	if (seasonOldRank_self == -1) //Our first attempt of the season, so do this to THEM
		sql = "UPDATE LocalRun SET season_rank = season_rank + 1, last_update = ? WHERE coursename = ? and style = ? AND season = ? AND season_rank >= ?";
	else
		sql = "UPDATE LocalRun SET season_rank = season_rank + 1, last_update = ? WHERE coursename = ? and style = ? AND season = ? AND season_rank >= ? AND season_rank < ?";

	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_int (stmt, 1, time));
	CALL_SQLITE (bind_text (stmt, 2, coursename_self, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_int (stmt, 3, style_self));
	CALL_SQLITE (bind_int (stmt, 4, season));
	CALL_SQLITE (bind_int (stmt, 5, seasonNewRank_self));

	if (seasonOldRank_self != -1)
		CALL_SQLITE (bind_int (stmt, 6, seasonOldRank_self));

	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Update Failed (G_UpdateOtherLocalRun)", s);
	}
	CALL_SQLITE (finalize(stmt));

	//Should not care about what season we update if its a global pb ?
	if (globalNewRank_self) { //Dont update other peoples global ranks if our run was not a global personal best...
		if (globalOldRank_self == -1) //Our first attempt overall
			sql = "UPDATE LocalRun SET rank = rank + 1, last_update = ? WHERE coursename = ? and style = ? AND rank >= ?";
		else
			sql = "UPDATE LocalRun SET rank = rank + 1, last_update = ? WHERE coursename = ? and style = ? AND rank >= ? AND rank < ?";

		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_int (stmt, 1, time));
		CALL_SQLITE (bind_text (stmt, 2, coursename_self, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 3, style_self));
		CALL_SQLITE (bind_int (stmt, 4, globalNewRank_self));

		if (globalOldRank_self != -1)
			CALL_SQLITE (bind_int (stmt, 5, globalOldRank_self));

		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Update Failed (G_UpdateOtherLocalRun 2)", s);
		}
		CALL_SQLITE (finalize(stmt));
	}

	//loda this can be combined with above query probably
	if (seasonOldRank_self == -1) { //First attempt  
		sql = "UPDATE LocalRun SET season_entries = season_entries + 1, last_update = ? WHERE coursename = ? AND style = ? AND season = ?";
	//+1 count for all, +1 rank only if affected
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_int (stmt, 1, time));
		CALL_SQLITE (bind_text (stmt, 2, coursename_self, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 3, style_self));
		CALL_SQLITE (bind_int (stmt, 4, season));

		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Update Failed (G_UpdateOtherLocalRun 3)", s);
		}
		CALL_SQLITE (finalize(stmt));
	}


	if (globalOldRank_self == -1) { //First attempt  
		sql = "UPDATE LocalRun SET entries = entries + 1, last_update = ? WHERE coursename = ? AND style = ?";
	//+1 count for all, +1 rank only if affected
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_int (stmt, 1, time));
		CALL_SQLITE (bind_text (stmt, 2, coursename_self, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 3, style_self));

		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Update Failed (G_UpdateOtherLocalRun 4)", s);
		}
		CALL_SQLITE (finalize(stmt));
	}

}

void TimeSecToString(int duration_ms, char *timeStr, size_t strSize) {
	if (duration_ms > (60 * 60)) { //thanks, eternal
		int hours, minutes, seconds;
		hours = (int)((duration_ms / (60 * 60))); //wait wut
		minutes = (int)((duration_ms / (60)) % 60);
		seconds = (int)(duration_ms) % 60;
		Com_sprintf(timeStr, strSize, "%i:%02i:%02i", hours, minutes, seconds);
	}
	else if (duration_ms > (60)) {
		int minutes, seconds;
		minutes = (int)((duration_ms / (60)) % 60);
		seconds = (int)(duration_ms) % 60;
		Com_sprintf(timeStr, strSize, "%i:%02i", minutes, seconds);
	}
	else {
		Q_strncpyz(timeStr, va("%i", (duration_ms)), strSize);
	}
}

void TimeToString(int duration_ms, char *timeStr, size_t strSize) {
	if (duration_ms > (60*60*1000)) { //thanks, eternal
		int hours, minutes, seconds, milliseconds; 
		hours = (int)((duration_ms / (1000*60*60))); //wait wut
		minutes = (int)((duration_ms / (1000*60)) % 60);
		seconds = (int)(duration_ms / 1000) % 60;
		milliseconds = duration_ms % 1000; 
		Com_sprintf(timeStr, strSize, "%i:%02i:%02i.%03i", hours, minutes, seconds, milliseconds);
	}
	else if (duration_ms > (60*1000)) {
		int minutes, seconds, milliseconds;
		minutes = (int)((duration_ms / (1000*60)) % 60);
		seconds = (int)(duration_ms / 1000) % 60;
		milliseconds = duration_ms % 1000; 
		Com_sprintf(timeStr, strSize, "%i:%02i.%03i", minutes, seconds, milliseconds);
	}
	else {
		Q_strncpyz(timeStr, va("%.3f", ((float)duration_ms * 0.001)), strSize);
	}
}

void PrintRaceTime(char *username, char *playername, char *message, char *style, int topspeed, int average, char *timeStr, int clientNum, int season_newRank, qboolean spb, int global_newRank, qboolean loggedin, qboolean valid, int season_oldRank, int global_oldRank, float addedScore, int awesomenoise, int worldrecordnoise) {
	int nameColor, color;
	char awardString[28] = {0}, messageStr[64] = {0}, nameStr[32] = {0};

	//Com_Printf("SOldrank %i SNewrank %i GOldrank %i GNewrank %i Addscore %.1f\n", season_oldRank, season_newRank, global_oldRank, global_newRank, addedScore);

	if (topspeed || average) { //weird hack to not play double sound coop
		if (global_newRank == 1) {//WR, Play the sound
			if (worldrecordnoise)
				PlayActualGlobalSound(worldrecordnoise); //Only for simple PB not WR i guess..
			else if (worldrecordnoise != -1) {
				if (!level.wrNoise) {
					level.wrNoise = G_SoundIndex("sound/chars/rosh_boss/misc/victory3"); //Maybe this should be done when df_trigger_finish is spawned cuz its still gonna hitch maybe on first wr of map? idk
				}
				PlayActualGlobalSound(level.wrNoise);
			}
		}
		else if (global_newRank > 0) {//PB
			if (awesomenoise)
				PlayActualGlobalSound(awesomenoise);
			else if (awesomenoise != -1) {
				if (!level.pbNoise) {
					level.pbNoise = G_SoundIndex("sound/chars/rosh/misc/taunt1");
				}
				PlayActualGlobalSound(level.pbNoise);
			}
		}
	}

	nameColor = 7 - (clientNum % 8);//sad hack
	if (nameColor < 2)
		nameColor = 2;
	else if (nameColor > 7 || nameColor == 5)
		nameColor = 7;

	if (valid && loggedin)
		color = 5;
	else if (valid)
		color = 2;
	else
		color = 1;

	if (username)
		Com_sprintf(nameStr, sizeof(nameStr), "%s", username);
	else
		Com_sprintf(nameStr, sizeof(nameStr), "%s", playername);

	if (message)
		Com_sprintf(messageStr, sizeof(messageStr), "^3%-16s^%i completed", message, color);
	else
		Com_sprintf(messageStr, sizeof(messageStr), "^%iCompleted", color);

	if (level.clients[clientNum].ps.stats[STAT_RESTRICTIONS] & JAPRO_RESTRICT_ALLOWTELES) { //print number of teles?
		if (level.clients[clientNum].midRunTeleCount < 1)
			Q_strcat(messageStr, sizeof(messageStr), " (PRO)");
		else
			Q_strcat(messageStr, sizeof(messageStr), va(" with %i TPs & %i CPs", level.clients[clientNum].midRunTeleCount, level.clients[clientNum].midRunTeleMarkCount));
	}

	if (valid) {
		if (global_newRank == 1) { //was 1 when it shouldnt have been.. ?
			Q_strncpyz(awardString, "^5(WR)", sizeof(awardString));
		}
		else if (season_newRank == 1 && global_newRank > 0) {
			Q_strncpyz(awardString, "^5(SR+PB)", sizeof(awardString));
		}
		else if (season_newRank == 1) {
			Q_strncpyz(awardString, "^5(SR)", sizeof(awardString));
		}
		else if (global_newRank > 0) {
			Q_strncpyz(awardString, "^5(PB)", sizeof(awardString));
		}
		else if (season_newRank > 0) {
			Q_strncpyz(awardString, "^5(SPB)", sizeof(awardString));
		}

		if (global_newRank > 0) { //Print global rank increased, global score added
			if (global_newRank != global_oldRank) {//Can be from -1 to #.  What do we do in this case..
				if (global_oldRank > 0)
					Q_strcat(awardString, sizeof(awardString), va(" (%i->%i +%.1f)", global_oldRank, global_newRank, addedScore));
				else
					Q_strcat(awardString, sizeof(awardString), va(" (%i +%.1f)", global_newRank, addedScore));
			}
		}
		else if (season_newRank > 0) {//Print season rank increased, global score added
			if (season_newRank != season_oldRank) {
				if (season_oldRank > 0)
					Q_strcat(awardString, sizeof(awardString), va(" (%i->%i +%.1f)", season_oldRank, season_newRank, addedScore));
				else
					Q_strcat(awardString, sizeof(awardString), va(" (%i +%.1f)", season_newRank, addedScore));
			}
		}

	}

	trap->SendServerCommand( -1, va("print \"%s in ^3%-12s^%i max:^3%-10i^%i avg:^3%-10i^%i style:^3%-10s^%i by ^%i%s %s^7\n\"",
				messageStr, timeStr, color, topspeed, color, average, color, style, color, nameColor, nameStr, awardString));
}

void G_UpdatePlaytime(sqlite3 *db, char *username, int seconds ) {
	char * sql;
	sqlite3_stmt * stmt;
	int s;
	qboolean newDB = qfalse;

	if (!db) {
		CALL_SQLITE (open (LOCAL_DB_PATH, & db)); 
		newDB = qtrue;
	}
	
	sql = "UPDATE LocalAccount SET racetime = racetime + ? WHERE username = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_int (stmt, 1, seconds));
	CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));

	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Update Failed (G_UpdatePlaytime)", s);
	}

	CALL_SQLITE (finalize(stmt));
	if (newDB) {
		CALL_SQLITE (close(db));
	}
}

void G_UpdateUnlocks(char *username, char *coursename, int style, int duration_ms, gclient_t *client, sqlite3 *db) { //Combine with update playtime i think, to reduce queries.  Update playtime is done after course completion..?
	//If its a cumulative award or something, we can check if current race is any of the conditions, then sql check inside to see if all the other conditions are met
	//Or, just make it cumulative when we check ValidateCosmetics, i guess thats better?
	unsigned int unlock = 0;
	unsigned int unlocks = 0;
	int i;

	if (client)
		unlocks = client->pers.unlocks;  	//Unlocks is existing unlocks from client. no need to update if they already have it.

	for (i=0; i<MAX_COSMETIC_UNLOCKS; i++) {
		if (!(unlocks & (1 << cosmeticUnlocks[i].bitvalue)) && cosmeticUnlocks[i].style == style && !Q_stricmp(coursename, cosmeticUnlocks[i].mapname) && (!cosmeticUnlocks[i].duration || (duration_ms < cosmeticUnlocks[i].duration))) {
			unlock |= (1 << cosmeticUnlocks[i].bitvalue);
			//Com_Printf("Unlock found [%s %i %i] %i (%i %s)\n", cosmeticUnlocks[i].mapname, cosmeticUnlocks[i].style, cosmeticUnlocks[i].duration, cosmeticUnlocks[i].bitvalue, style, coursename);
			continue; //Ahh this should be continue, so that one course can give us multiple unlocks?
		}
	}

	if (unlock) {
		char * sql;
		sqlite3_stmt * stmt;
		int s;

		sql = "UPDATE LocalAccount SET unlocks = unlocks | ? WHERE username = ?";
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		CALL_SQLITE(bind_int(stmt, 1, unlock));
		CALL_SQLITE(bind_text(stmt, 2, username, -1, SQLITE_STATIC));

		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Update Failed (G_UpdateUnlocks)", s);
		}

		CALL_SQLITE(finalize(stmt));

		if (client)//Also update in realtime if possible.
			client->pers.unlocks |= unlock;
	}
}

void G_SpawnCosmeticUnlocks(void) {
	fileHandle_t f;
	int		fLen = 0, MAX_FILESIZE = 4096, args = 1, row = 0;  //use max num warps idk
	char	filename[MAX_QPATH+4] = {0}, buf[4096] = {0};//eh
	char*	pch;

	Q_strncpyz(filename, "cosmetics.cfg", sizeof(filename));

	fLen = trap->FS_Open(filename, &f, FS_READ);

	if (!f) {
		//Com_Printf ("Couldn't load cosmetic unlocks from %s\n", filename);
		return;
	}
	if (fLen >= MAX_FILESIZE) {
		trap->FS_Close(f);
		Com_Printf ("Couldn't load cosmetic unlocks from %s, file is too large\n", filename);
		return;
	}

	trap->FS_Read(buf, fLen, f);
	buf[fLen] = 0;
	trap->FS_Close(f);

	pch = strtok (buf,";\n\t");  //loda fixme why is this broken
	while (pch != NULL && row < MAX_COSMETIC_UNLOCKS)
	{
		if ((args % 4) == 1) {
			cosmeticUnlocks[row].bitvalue = atoi(pch);
			cosmeticUnlocks[row].active = qtrue;
		}
		else if ((args % 4) == 2)
			Q_strncpyz(cosmeticUnlocks[row].mapname, pch, sizeof(cosmeticUnlocks[row].mapname));
		else if ((args % 4) == 3)
			cosmeticUnlocks[row].style = atoi(pch);
		else if ((args % 4) == 0) {
			cosmeticUnlocks[row].duration = atoi(pch);
			if (cosmeticUnlocks[row].bitvalue < 32) {
				//trap->Print("Cosmetic unlock added: %i, %s, %i, %i\n", cosmeticUnlocks[row].bitvalue, cosmeticUnlocks[row].mapname, cosmeticUnlocks[row].style, cosmeticUnlocks[row].duration);
				row++;
			}
		}
		pch = strtok (NULL, ";\n\t");
		args++;
	}
	Com_Printf("Loaded cosmetic unlocks from %s\n", filename);
}

void SV_RebuildUnlocks_f(void) {
	sqlite3 * db;
	char * sql;
	sqlite3_stmt * stmt;
	int s;

	//Clear existing
	memset(cosmeticUnlocks, 0, sizeof(cosmeticUnlocks));
	G_SpawnCosmeticUnlocks();//Re Spawn from CFG

	CALL_SQLITE(open(LOCAL_DB_PATH, &db));

	//Set all unlocks to 0 ?
	sql = "UPDATE LocalAccount SET unlocks = 0"; //Only get username for cumulative checks if needed
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Update Failed (SV_RebuildUnlocks_f 1)", s);
	CALL_SQLITE(finalize(stmt));

	sql = "SELECT username, coursename, style, duration_ms FROM LocalRun"; //Only get username for cumulative checks if needed
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	while (1) {
		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			G_UpdateUnlocks((char*)sqlite3_column_text(stmt, 0), (char*)sqlite3_column_text(stmt, 1), sqlite3_column_int(stmt, 2), sqlite3_column_int(stmt, 3), NULL, db);
		}
		else if (s == SQLITE_DONE)
			break;
		else {
			G_ErrorPrint("ERROR: SQL Select Failed (SV_RebuildUnlocks_f 2)", s);
			break;
		}
	}
	CALL_SQLITE(finalize(stmt));

	CALL_SQLITE(close(db));
}

void StripWhitespace(char *s);
void G_AddRaceTime(char *username, char *message, int duration_ms, int style, int topspeed, int average, int clientNum, int awesomenoise, int worldrecordnoise, char *checkpoints) {//should be short.. but have to change elsewhere? is it worth it?
	time_t	rawtime;
	char	string[1024] = {0}, info[1024] = {0}, coursename[40], timeStr[32] = {0}, styleString[32] = {0};
	qboolean seasonPB = qfalse, globalPB = qfalse;//, WR = qfalse;
	sqlite3 * db;
	char * sql;
	sqlite3_stmt * stmt;
	int s;
	int season_oldBest, season_oldRank = 0, season_newRank = -1, global_oldBest, global_oldRank = 0, global_newRank = -1; //Changed newrank to be -1 ??
	float addedScore = 0.0f;
	gclient_t	*cl;
	const int season = G_GetSeason();

	cl = &level.clients[clientNum];

	time(&rawtime);
	localtime(&rawtime);

	trap->GetServerinfo(info, sizeof(info));
	Q_strncpyz(coursename, Info_ValueForKey(info, "mapname"), sizeof(coursename));

	if (message) {// [0]?
		Q_strlwr(message);
		Q_CleanStr(message);
		Q_strcat(coursename, sizeof(coursename), va(" (%s)", message));
	}

	if (average > topspeed) {
		average = topspeed; //need to sample speeds every clientframe.. but how to calculate average if client frames are not evenly spaced.. can use pml.msec ?
	}

	Q_strlwr(coursename);
	Q_CleanStr(coursename);

	IntegerToRaceName(style, styleString, sizeof(styleString));

	Com_sprintf(string, sizeof(string), "%s;%s;%i;%i;%i;%i;%i\n", username, coursename, duration_ms, topspeed, average, style, rawtime);

	if (level.raceLog)
		trap->FS_Write(string, strlen(string), level.raceLog); //Always write to text file races.log

	CALL_SQLITE(open(LOCAL_DB_PATH, &db));

	sql = "SELECT MIN(duration_ms), season_rank FROM LocalRun WHERE username = ? AND coursename = ? AND style = ? AND season = ? "
		"UNION ALL SELECT MIN(duration_ms), rank FROM LocalRun WHERE username = ? AND coursename = ? AND style = ? AND invalid = 0";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 2, coursename, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 3, style));
	CALL_SQLITE(bind_int(stmt, 4, season));
	CALL_SQLITE(bind_text(stmt, 5, username, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 6, coursename, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 7, style));

	s = sqlite3_step(stmt);

	if (s == SQLITE_ROW) {
		season_oldBest = sqlite3_column_int(stmt, 0);
		season_oldRank = sqlite3_column_int(stmt, 1);

		//trap->Print("Oldbest, Duration_ms: %i, %i\n", oldBest, duration_ms);

		if (season_oldBest) {// We found a time in the database
			if (duration_ms < season_oldBest) { //our time we just recorded is faster, so log it			
				seasonPB = qtrue;
			}
		}
		else { //No time found in database, so record the time we just recorded 
			seasonPB = qtrue;
			season_oldRank = -1;
		}
	}
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (G_AddRaceTime 1)", s);
	}

	s = sqlite3_step(stmt);

	if (s == SQLITE_ROW) {
		global_oldBest = sqlite3_column_int(stmt, 0);
		global_oldRank = sqlite3_column_int(stmt, 1);

		//trap->Print("Oldbest, Duration_ms: %i, %i\n", oldBest, duration_ms);

		if (global_oldBest) {// We found a time in the database
			if (duration_ms < global_oldBest) { //our time we just recorded is faster, so log it			
				globalPB = qtrue;
			}
		}
		else { //No time found in database, so record the time we just recorded 
			globalPB = qtrue;
			global_oldRank = -1;
		}
	}
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (G_AddRaceTime 2)", s);
	}
	CALL_SQLITE(finalize(stmt));


	if (seasonPB) {
		int season_oldCount = 0, season_newCount, global_oldCount = 0, global_newCount;
		int i = 1; //1st place is rank 1
		int duration, lastDuration = 0;

		sql = "SELECT COUNT(*) FROM LocalRun WHERE coursename = ? AND style = ? AND season = ? "
			"UNION ALL SELECT COUNT(DISTINCT username) FROM LocalRun WHERE coursename = ? AND style = ? AND invalid = 0"; //entries ?
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		CALL_SQLITE(bind_text(stmt, 1, coursename, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_int(stmt, 2, style));
		CALL_SQLITE(bind_int(stmt, 3, season));
		CALL_SQLITE(bind_text(stmt, 4, coursename, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_int(stmt, 5, style));
		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			season_oldCount = sqlite3_column_int(stmt, 0);
		}
		else if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Select Failed (G_AddRaceTime 3)", s);
		}

		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			global_oldCount = sqlite3_column_int(stmt, 0);
		}
		else if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Select Failed (G_AddRaceTime 4)", s);
		}

		CALL_SQLITE(finalize(stmt));

		//Get season rank
		sql = "SELECT duration_ms FROM LocalRun WHERE coursename = ? AND style = ? AND season = ? ORDER BY duration_ms ASC, end_time ASC, average DESC"; //assume just one per person to speed this up..
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		CALL_SQLITE(bind_text(stmt, 1, coursename, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_int(stmt, 2, style));
		CALL_SQLITE(bind_int(stmt, 3, season));
		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				season_newRank = 0; //Make sure this doesnt reset a set newrank, but it wont since we break after setting
				duration = sqlite3_column_int(stmt, 0);

				if (style == MV_COOP_JKA) { //update this for coop ties
					if (duration == lastDuration) {
						i--;
					}
					lastDuration = duration;
				}
				if (duration_ms < duration) { //We are faster than this time... If we dont find anything newrank stays -1
					season_newRank = i;
					break;
				}
				i++;
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (G_AddRaceTime 5)", s);
				break;
			}
		}
		CALL_SQLITE(finalize(stmt));

		i = 1; //oh no no
		lastDuration = 0;

		//Get global rank, could union this with previous query maybe
		sql = "SELECT MIN(duration_ms) FROM LocalRun WHERE coursename = ? AND style = ? AND invalid = 0 GROUP BY username ORDER BY duration_ms ASC, end_time ASC, average DESC";
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		CALL_SQLITE(bind_text(stmt, 1, coursename, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_int(stmt, 2, style));
		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				global_newRank = 0; //Make sure this doesnt reset a set newrank, but it wont since we break after setting --  what?
				duration = sqlite3_column_int(stmt, 0);

				if (style == MV_COOP_JKA) { //update this for coop ties
					if (duration == lastDuration) {
						i--;
					}
					lastDuration = duration;
				}
				if (duration_ms < duration) { //We are faster than this time... If we dont find anything newrank stays -1
					global_newRank = i;
					break;
				}
				i++;
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (G_AddRaceTime 6)", s);
				break;
			}
		}
		CALL_SQLITE(finalize(stmt));

		if (season_newRank == 0) { //We wern't faster than any times, so set our rank to count (+ 1) ? -- loda checkme
			season_newRank = season_oldCount + 1;
		}
		if (season_newRank == -1) {//We didnt find any times, so we are first -- ?
			season_newRank = 1;
		}

		if (season_oldRank == -1) {
			season_newCount = season_oldCount + 1;
		}
		else {
			season_newCount = season_oldCount;
		}
		//--------------
		if (global_newRank == 0) { //We wern't faster than any times, so set our rank to count (+ 1) ? -- loda checkme
			global_newRank = global_oldCount + 1;
		}
		if (global_newRank == -1) {//We didnt find any times, so we are first -- ?
			global_newRank = 1;
		}

		if (global_oldRank == -1) {
			global_newCount = global_oldCount + 1;
		}
		else {
			global_newCount = global_oldCount;
		}

		//If this isnt our best time of all seasons, set rank to 0 so we wont get it in global queries.
		if (!globalPB) {
			global_newRank = 0;
		}

		if ((season_newRank != season_oldRank || global_newRank != global_oldRank)) { //Do this before messing with out race list rank - does this affect count?
			if (style == MV_COOP_JKA && topspeed == 0 && average == 0) { //dont update others if its coop and we're the booster.. our partner already updated it
			}
			else {
				G_UpdateOtherLocalRun(db, season_newRank, season_oldRank, global_newRank, global_oldRank, style, coursename, rawtime); //Update other spots in race list
			}
		}
		G_UpdateOurLocalRun(db, season_oldRank, season_newRank, global_oldRank, global_newRank, style, username, coursename, duration_ms, topspeed, average, rawtime, season_newCount, global_newCount, checkpoints);//Update our race list

		if (cl->pers.recordingDemo && globalPB) {
			char mapCourse[MAX_QPATH] = { 0 };

			Q_strncpyz(mapCourse, coursename, sizeof(mapCourse));
			StripWhitespace(mapCourse);
			Q_strstrip(mapCourse, "\n\r;:.?*<>|\\/\"", NULL);

			if (cl) {
				cl->pers.stopRecordingTime = level.time + 2000;
				cl->pers.keepDemo = qtrue;
				Com_sprintf(cl->pers.oldDemoName, sizeof(cl->pers.oldDemoName), "%s", cl->pers.userName);
				if (style == MV_SIEGE) //Give siege demos a hidden demoname
					Com_sprintf(cl->pers.demoName, sizeof(cl->pers.demoName), "hidden/%s/%s-%s-%s", cl->pers.userName, cl->pers.userName, mapCourse, styleString); //TODO, change this to %s/%s-%s-%s so its puts in individual players folder
				else
					Com_sprintf(cl->pers.demoName, sizeof(cl->pers.demoName), "%s/%s-%s-%s", cl->pers.userName, cl->pers.userName, mapCourse, styleString); //TODO, change this to %s/%s-%s-%s so its puts in individual players folder
			}
		}

		//For print
		if (global_newRank > 0) {
			addedScore = ((global_newCount / (float)global_newRank) + (global_newCount - global_newRank)) * 0.5f; //Add new score
			if (global_oldRank > 0)
				addedScore -= ((global_oldCount / (float)global_oldRank) + (global_oldCount - global_oldRank)) * 0.5f; //Subtract old score, if there was one
		}
		else if (season_newRank > 0) {
			addedScore = ((season_newCount / (float)season_newRank) + (season_newCount - season_newRank)) * 0.5f;
			if (season_oldRank > 0)
				addedScore -= ((season_oldCount / (float)season_oldRank) + (season_oldCount - season_oldRank)) * 0.5f;
		}

		if (globalPB) {
			G_UpdateUnlocks(username, coursename, style, duration_ms, cl, db);
		}
	}
	//else.. set ranks to 0 for print, nothing to update

	cl->pers.stats.racetime += (duration_ms*0.001f) - cl->afkDuration*0.001f;
	cl->afkDuration = 0;
	if (cl->pers.stats.racetime > 120.0f) { //Avoid spamming the db
		G_UpdatePlaytime(db, username, (int)(cl->pers.stats.racetime + 0.5f));
		cl->pers.stats.racetime = 0.0f;
	}
	
	CALL_SQLITE(close(db));

	TimeToString((int)(duration_ms), timeStr, sizeof(timeStr));
	PrintRaceTime(username, cl->pers.netname, message, styleString, topspeed, average, timeStr, clientNum, season_newRank, seasonPB, global_newRank, qtrue, qtrue, season_oldRank, global_oldRank, addedScore, awesomenoise, worldrecordnoise);
	//DebugWriteToDB("G_AddRaceTime");
}

#if 0
void G_TestAddRace() {
	char username[40], coursename[40], input[16];
	int style, duration_ms, average, topspeed, end_time, oldrank, newrank;

	if (trap->Argc() != 7) {
		Com_Printf ("Usage: /addrace <username> <coursename> <style> <duration> <average> <topspeed>\n");
		return;
	}

	trap->Argv(1, username, sizeof(username));
	trap->Argv(2, coursename, sizeof(coursename));

	trap->Argv(3, input, sizeof(input));
	style = atoi(input);
		
	trap->Argv(4, input, sizeof(input));
	duration_ms = atoi(input);

	trap->Argv(5, input, sizeof(input));
	average = atoi(input);

	trap->Argv(6, input, sizeof(input));
	topspeed = atoi(input);

	trap->Argv(7, input, sizeof(input));
	end_time = atoi(input);

	trap->Argv(8, input, sizeof(input));
	oldrank = atoi(input);

	trap->Argv(9, input, sizeof(input));
	newrank = atoi(input);

	G_AddRaceTime(username, coursename, duration_ms, style, topspeed, average, 0);
	//G_AddNewRaceToDB(username, coursename, style, duration_ms, average, topspeed, end_time, oldrank, newrank, 0);
}

#endif

//So the best way is to probably add every run as soon as its taken and not filter them.
//to cut down on database size, there should be a cleanup on every mapload or.. every week..or...?
//which removes any time not in the top 100 of its category AND more than 1 week old?

void ResetPlayerTimers(gentity_t *ent, qboolean print);//extern ?
void Cmd_ACLogin_f( gentity_t *ent ) { //loda fixme show lastip ? or use lastip somehow
	char username[16], enteredPassword[16], password[16], strIP[NET_ADDRSTRMAXLEN] = {0}, enteredKey[32];
	int key;
	unsigned int ip;
	char *p = NULL;

	if (!ent->client)
		return;

	if (trap->Argc() != 3 && trap->Argc() != 4) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /login <username> <password>\n\"");
		return;
	}

	if (Q_stricmp(ent->client->pers.userName, "")) {
		trap->SendServerCommand(ent-g_entities, "print \"You are already logged in!\n\"");
		return;
	}

	trap->Argv(1, username, sizeof(username));
	trap->Argv(2, enteredPassword, sizeof(password));

	trap->Argv(3, enteredKey, sizeof(enteredKey));
	key = atoi(enteredKey);
	if (key && sv_pluginKey.integer) {
		int mod, add, pluginKey = sv_pluginKey.integer;
		int time = (ent->client->pers.cmd.serverTime + 500) / 1000 * 1000;
		if (sv_pluginKey.integer < 0)
			pluginKey = -pluginKey;
		mod = pluginKey / 1000;
		add = pluginKey % 1000;

		if (mod > 0) {
			//trap->Print("Client logged in with key: %i and time %i correct key is %i\n", key, time, (time % mod) + add);
			if (key == (time % mod) + add) {
				ent->client->pers.validPlugin = qtrue;
				//trap->Print("Valid login\n");
			}
			else {
				trap->SendServerCommand(ent - g_entities, "print \"^1Error authenticating!\n\"");
				ent->client->pers.validPlugin = qfalse;
			}
		}
	}
	else {
		ent->client->pers.validPlugin = qfalse;
	}

	Q_strlwr(username);
	Q_CleanStr(username);

	Q_CleanStr(enteredPassword);

	Q_strncpyz(strIP, ent->client->sess.IP, sizeof(strIP));
	p = strchr(strIP, ':');
	if (p) //loda - fix ip sometimes not printing
		*p = 0;
	ip = ip_to_int(strIP);

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int row = 0, s, count = 0, i;
		gclient_t	*cl;
		unsigned int lastip = 0, unlocks = 0, flags = 0;

		CALL_SQLITE(open(LOCAL_DB_PATH, &db));

		if (ip) {
			sql = "SELECT COUNT(*) FROM LocalAccount WHERE lastip = ?";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_int64(stmt, 1, ip));

			s = sqlite3_step(stmt);

			if (s == SQLITE_ROW)
				count = sqlite3_column_int(stmt, 0);
			else if (s != SQLITE_DONE) {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_ACLogin_f 1)", s);
				CALL_SQLITE(finalize(stmt));
				CALL_SQLITE(close(db));
				return;
			}

			CALL_SQLITE(finalize(stmt));
		}

		sql = "SELECT password, lastip, flags, unlocks FROM LocalAccount WHERE username = ?";
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));

		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				Q_strncpyz(password, (char*)sqlite3_column_text(stmt, 0), sizeof(password));
				lastip = sqlite3_column_int(stmt, 1);
				flags = sqlite3_column_int(stmt, 2);
				unlocks = sqlite3_column_int(stmt, 3);
				row++;
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_ACLogin_f 2)", s);
				break;
			}
		}

		CALL_SQLITE(finalize(stmt));

		if (row == 0) { // No accounts found
			trap->SendServerCommand(ent - g_entities, "print \"Account not found! To make a new account, use the /register command.\n\"");
			CALL_SQLITE(close(db));
			return;
		}
		else if (row > 1) { // More than 1 account found
			trap->Print("WARNING: Multiple accounts with same name!\n");
			CALL_SQLITE(close(db));
			return;
		}

		if (!(flags & JAPRO_ACCOUNTFLAG_TRUSTED) && (count > 0) && lastip && ip && (lastip != ip)) { //IF lastip already tied to account, and lastIP (of attempted login username) does not match current IP, deny.?
			trap->SendServerCommand(ent - g_entities, "print \"Your IP address already belongs to an account. You are only allowed one account.\n\"");
			CALL_SQLITE(close(db));
			return;
		}

		for (i = 0; i < MAX_CLIENTS; i++) {//Build a list of clientsv - use numplayingclients fixme
			if (!g_entities[i].inuse)
				continue;
			cl = &level.clients[i];
			if (!Q_stricmp(username, cl->pers.userName)) {
				trap->SendServerCommand(ent - g_entities, "print \"This account is already logged in!\n\"");
				CALL_SQLITE(close(db));
				return;
			}
		}

		if (enteredPassword[0] && password[0] && !Q_stricmp(enteredPassword, password)) {
			time_t	rawtime;

			time(&rawtime);
			localtime(&rawtime);

			if ((flags & JAPRO_ACCOUNTFLAG_IPLOCK) && lastip && lastip != ip) {
				trap->SendServerCommand(ent - g_entities, "print \"This account is locked to a different IP address.\n\"");
				CALL_SQLITE(close(db));
				return;
			}

			Q_strncpyz(ent->client->pers.userName, username, sizeof(ent->client->pers.userName));
			if (ent->client->sess.raceMode && ent->client->pers.stats.startTime) {
				ResetPlayerTimers(ent, qtrue);
				//trap->SendServerCommand(ent - g_entities, "print \"Login sucessful. Time reset.\n\"");
			}
			else {
				//trap->SendServerCommand(ent - g_entities, "print \"Login sucessful.\n\"");
			}

			if (!ip) //meh
				ip = lastip;

			sql = "UPDATE LocalAccount SET lastip = ?, lastlogin = ? WHERE username = ?";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_int64(stmt, 1, ip));
			CALL_SQLITE(bind_int(stmt, 2, rawtime));
			CALL_SQLITE(bind_text(stmt, 3, username, -1, SQLITE_STATIC));

			s = sqlite3_step(stmt);

			if (s != SQLITE_DONE)
				G_ErrorPrint("ERROR: SQL Update Failed (Cmd_ACLogin_f 3)", s);

			CALL_SQLITE(finalize(stmt));

			ent->client->pers.unlocks = unlocks;

			if ((flags & JAPRO_ACCOUNTFLAG_A_READAMSAY) && !(ent->client->sess.accountFlags & JAPRO_ACCOUNTFLAG_A_READAMSAY))
				trap->SendServerCommand(-1, va("print \"%s^7 (%s) has logged in as an admin\n\"", ent->client->pers.netname, ent->client->pers.userName));
			else
				trap->SendServerCommand(-1, va("print \"%s^7 (%s) has logged in\n\"", ent->client->pers.netname, ent->client->pers.userName));

			for (i=0; i<32; i++) {//Loop this
				if (flags & (1 << i))//Problem, only add to flags, not remove?
					ent->client->sess.accountFlags |= (1 << i);
			}
		}
		else {
			trap->SendServerCommand(ent - g_entities, "print \"Incorrect password!\n\"");
		}
		CALL_SQLITE(close(db));
	}

	//DebugWriteToDB("Cmd_ACLogin_f");
}

void Cmd_ChangePassword_f( gentity_t *ent ) {
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
    int s; //row = 0, s;
	char username[16], enteredPassword[16], newPassword[16], password[16];

	if (trap->Argc() != 4) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /changepassword <username> <password> <newpassword>\n\"");
		return;
	}

	if (!Q_stricmp(ent->client->pers.userName, "")) {
		trap->SendServerCommand(ent-g_entities, "print \"You are not logged in!\n\"");
		return;
	}

	trap->Argv(1, username, sizeof(username));
	trap->Argv(2, enteredPassword, sizeof(enteredPassword));
	trap->Argv(3, newPassword, sizeof(newPassword));

	Q_strlwr(username);
	Q_CleanStr(username);

	if (Q_stricmp(ent->client->pers.userName, username)) {
		trap->SendServerCommand(ent-g_entities, "print \"Incorrect username!\n\"");
		return;
	}

	Q_CleanStr(enteredPassword);
	Q_CleanStr(newPassword);

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
	sql = "SELECT password FROM LocalAccount WHERE username = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, ent->client->pers.userName, -1, SQLITE_STATIC));
	
    while (1) {
        s = sqlite3_step(stmt);
        if (s == SQLITE_ROW) {
			Q_strncpyz(password, (char*)sqlite3_column_text(stmt, 0), sizeof(password));
            //row++;
        }
        else if (s == SQLITE_DONE)
            break;
        else {
            G_ErrorPrint("ERROR: SQL Select Failed (Cmd_ChangePassword_f 1)", s);
			break;
        }
    }

	CALL_SQLITE (finalize(stmt));

	if (enteredPassword[0] && password[0] && !Q_stricmp(enteredPassword, password)) {
		int s;

		sql = "UPDATE LocalAccount SET password = ? WHERE username = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, newPassword, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 2, ent->client->pers.userName, -1, SQLITE_STATIC));
		s = sqlite3_step(stmt);
		if (s == SQLITE_DONE)
			trap->SendServerCommand(ent-g_entities, "print \"Password Changed.\n\""); //loda fixme check if this executed
		else
			G_ErrorPrint("ERROR: SQL Update Failed (Cmd_ChangePassword_f 2)", s);

		CALL_SQLITE (finalize(stmt));
	}
	else {
		trap->SendServerCommand(ent-g_entities, "print \"Incorrect password!\n\"");
	}	
	CALL_SQLITE (close(db));

	//DebugWriteToDB("Cmd_ChangePassword_f");
}

void Svcmd_ChangePass_f(void)
{
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	char username[16], newPassword[16];
	int s;

	if (trap->Argc() != 3) {
		trap->Print( "Usage: /changepassword <username> <newpassword>\n");
		return;
	}

	trap->Argv(1, username, sizeof(username));
	trap->Argv(2, newPassword, sizeof(newPassword));

	Q_strlwr(username);
	Q_CleanStr(username);

	Q_CleanStr(newPassword);

	if (!CheckUserExists(username)) {
		trap->Print( "User does not exist!\n");
		return;
	}

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
	sql = "UPDATE LocalAccount SET password = ? WHERE username = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, newPassword, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
	s = sqlite3_step(stmt);
	if (s == SQLITE_DONE)
			trap->Print( "Password changed.\n");
	else
		G_ErrorPrint("ERROR: SQL Update Failed (Svcmd_ChangePass_f)", s);

	CALL_SQLITE (finalize(stmt));
	CALL_SQLITE (close(db));
}

void Svcmd_ClearIP_f(void)
{
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	char username[16];
	int s;

	if (trap->Argc() != 2) {
		trap->Print( "Usage: /clearIP <username>\n");
		return;
	}

	trap->Argv(1, username, sizeof(username));

	Q_strlwr(username);
	Q_CleanStr(username);

	if (!CheckUserExists(username)) {
		trap->Print( "User does not exist!\n");
		return;
	}

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
	sql = "UPDATE LocalAccount SET lastip = 0 WHERE username = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));
	s = sqlite3_step(stmt);

	if (s == SQLITE_DONE)
		trap->Print( "IP Cleared.\n");
	else
		G_ErrorPrint("ERROR: SQL Update Failed (Svcmd_ClearIP_f)", s);

	CALL_SQLITE (finalize(stmt));
	CALL_SQLITE (close(db));
}

void Svcmd_Register_f(void)
{
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	char username[16], password[16];
	time_t	rawtime;
	int s;

	if (trap->Argc() != 3) {
		trap->Print( "Usage: /register <username> <password>\n");
		return;
	}

	trap->Argv(1, username, sizeof(username));
	trap->Argv(2, password, sizeof(password));

	Q_strlwr(username);
	Q_CleanStr(username);
	Q_strstrip(username, " \n\r;:.?*<>!#$&'()+@=`~{}[]^_|\\/\"", NULL);

	Q_CleanStr(password);

	if (!username[0]) {
		return;
	}
	if (!Q_stricmp(username, "none") || !Q_stricmp(username, "null") || !Q_stricmp(username, "0")) {
		return;
	}
	if (!Q_stricmp(username, password)) {
		trap->Print("Username and password cannot be the same\n");
		return;
	}
	if (CheckUserExists(username)) {
		trap->Print( "User already exists!\n");
		return;
	}

	time( &rawtime );
	localtime( &rawtime );

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
    sql = "INSERT INTO LocalAccount (username, password, kills, deaths, suicides, captures, returns, racetime, created, lastlogin, lastip) VALUES (?, ?, 0, 0, 0, 0, 0, 0, ?, ?, 0)";
    CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
    CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_text (stmt, 2, password, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_int (stmt, 3, rawtime));
	CALL_SQLITE (bind_int (stmt, 4, rawtime));
	s = sqlite3_step(stmt);

	if (s == SQLITE_DONE)
		trap->Print( "Account created.\n");
	else
		G_ErrorPrint("ERROR: SQL Insert Failed (Svcmd_Register_f)", s);

	CALL_SQLITE (finalize(stmt));
	CALL_SQLITE (close(db));
}

void Svcmd_DeleteAccount_f(void)
{
	char username[16], confirm[16];

	if (trap->Argc() != 3) {
		trap->Print( "Usage: /deleteAccount <username> <confirm>\n");
		return;
	}

	trap->Argv(1, username, sizeof(username));
	trap->Argv(2, confirm, sizeof(confirm));

	if (Q_stricmp(confirm, "confirm")) {
		trap->Print( "Usage: /deleteAccount <username> <confirm>\n");
		return;
	}

	Q_strlwr(username);
	Q_CleanStr(username);

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s;

		CALL_SQLITE(open(LOCAL_DB_PATH, &db));

		if (CheckUserExists(username)) {
			sql = "DELETE FROM LocalAccount WHERE username = ?";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));
			s = sqlite3_step(stmt);
			if (s == SQLITE_DONE)
				trap->Print("Account deleted.\n");
			else
				G_ErrorPrint("ERROR: SQL Delete Failed (Svcmd_DeleteAccount_f 1)", s);
			CALL_SQLITE(finalize(stmt));
		}
		else
			trap->Print("User does not exist, deleting highscores for username anyway.\n");

		sql = "DELETE FROM LocalRun WHERE username = ?";
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));

		//Delete from localduel?

		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE)
			G_ErrorPrint("ERROR: SQL Delete Failed (Svcmd_DeleteAccount_f 2)", s);
		CALL_SQLITE(finalize(stmt));

		CALL_SQLITE(close(db));
	}
}

void Svcmd_RenameAccount_f(void)
{
	char username[16], newUsername[16], confirm[16];

	if (trap->Argc() != 4) {
		trap->Print( "Usage: /renameAccount <username> <new username> <confirm>\n");
		return;
	}

	trap->Argv(1, username, sizeof(username));
	trap->Argv(2, newUsername, sizeof(newUsername));
	trap->Argv(3, confirm, sizeof(confirm));

	if (Q_stricmp(confirm, "confirm")) {
		trap->Print( "Usage: /renameAccount <username> <new username> <confirm>\n");
		return;
	}

	Q_strlwr(username);
	Q_CleanStr(username);

	Q_strlwr(newUsername);
	Q_CleanStr(newUsername);
	Q_strstrip(username, " \n\r;:.?*<>!#$&'()+@=`~{}[]^_|\\/\"", NULL);
	Q_strstrip(newUsername, " \n\r;:.?*<>!#$&'()+@=`~{}[]^_|\\/\"", NULL);

	if (!Q_stricmp(newUsername, "none") || !Q_stricmp(newUsername, "null") || !Q_stricmp(newUsername, "0")) {
		return;
	}
	if (CheckUserExists(newUsername)) {
		trap->Print( "This username already exists.  Merging\n"); //Merge?
		//return;
	}

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s;

		CALL_SQLITE(open(LOCAL_DB_PATH, &db));

		if (CheckUserExists(username)) {
			sql = "UPDATE LocalAccount SET username = ? WHERE username = ?";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, newUsername, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_text(stmt, 2, username, -1, SQLITE_STATIC));
			s = sqlite3_step(stmt);
			if (s == SQLITE_DONE)
				trap->Print("Account renamed.\n");
			else
				G_ErrorPrint("ERROR: SQL Update Failed (Svcmd_RenameAccount_f 1)", s);
			CALL_SQLITE(finalize(stmt));
		}
		else
			trap->Print("User does not exist, renaming in races and duels anyway.\n");

		sql = "UPDATE LocalRun SET username = ? WHERE username = ?";
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		CALL_SQLITE(bind_text(stmt, 1, newUsername, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_text(stmt, 2, username, -1, SQLITE_STATIC));

		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE)
			G_ErrorPrint("ERROR: SQL Update Failed (Svcmd_RenameAccount_f 2)", s);

		CALL_SQLITE(finalize(stmt));

		sql = "UPDATE LocalDuel SET winner = ? WHERE winner = ?";
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		CALL_SQLITE(bind_text(stmt, 1, newUsername, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_text(stmt, 2, username, -1, SQLITE_STATIC));

		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE)
			G_ErrorPrint("ERROR: SQL Update Failed (Svcmd_RenameAccount_f 3)", s);

		CALL_SQLITE(finalize(stmt));

		sql = "UPDATE LocalDuel SET loser = ? WHERE loser = ?";
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		CALL_SQLITE(bind_text(stmt, 1, newUsername, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_text(stmt, 2, username, -1, SQLITE_STATIC));

		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE)
			G_ErrorPrint("ERROR: SQL Update Failed (Svcmd_RenameAccount_f 4)", s);

		CALL_SQLITE(finalize(stmt));
		CALL_SQLITE(close(db));

	}
}

void Svcmd_AccountInfo_f(void)
{
	char username[16];

	if (trap->Argc() != 2) {
		trap->Print( "Usage: /accountInfo <username>\n");
		return;
	}

	trap->Argv(1, username, sizeof(username));

	Q_strlwr(username);
	Q_CleanStr(username);

	if (!CheckUserExists(username)) {
		trap->Print( "User does not exist!\n");
		return;
	}

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int lastlogin = 0, created = 0, racetime = 0;
		unsigned int lastip = 0;
		int s;
		char timeStr[64] = { 0 }, buf[MAX_STRING_CHARS - 64] = { 0 };

		CALL_SQLITE(open(LOCAL_DB_PATH, &db));
		sql = "SELECT created, lastlogin, lastip, racetime FROM LocalAccount WHERE username = ?";
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));

		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			created = sqlite3_column_int(stmt, 0);
			lastlogin = sqlite3_column_int(stmt, 1);
			lastip = sqlite3_column_int(stmt, 2);
			racetime = sqlite3_column_int(stmt, 3);
		}
		else if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Select Failed (Svcmd_AccountInfo_f)", s);
			CALL_SQLITE(finalize(stmt));
			CALL_SQLITE(close(db));
			return;
		}

		CALL_SQLITE(finalize(stmt));
		CALL_SQLITE(close(db));

		Q_strncpyz(buf, va("Stats for %s:\n", username), sizeof(buf));
		getDateTime(created, timeStr, sizeof(timeStr));
		Q_strcat(buf, sizeof(buf), va("   ^5Created   : ^2%s\n", timeStr));
		getDateTime(lastlogin, timeStr, sizeof(timeStr));
		Q_strcat(buf, sizeof(buf), va("   ^5Last login: ^2%s\n", timeStr));
		Q_strcat(buf, sizeof(buf), va("   ^5Last IP^3: ^2%u\n", lastip));
		TimeSecToString(racetime, timeStr, sizeof(timeStr));
		Q_strcat(buf, sizeof(buf), va("   ^5Racetime: ^2%s\n", timeStr));

		trap->Print("%s", buf);
	}
}

typedef struct bitInfo_S {
	const char	*string;
} bitInfo_T;

static bitInfo_T accountFlags[] = { 
	{"AmTele"},//0
	{"AmFreeze"},//1
	{"AmTelemark"},//2
	{"AmBan"},//3
	{"AmKick"},//4
	{"NPC"},//5
	{"NoClip"},//6
	{"AmGrantAdmin"},//7
	{"AmMap"},//8
	{"AmPSay"},//9
	{"AmForceTeam"},//10
	{"Amlockteam"},//11
	{"AmVSTR"},//12
	{"See IPs"},//13
	{"AmRename"},//14
	{"AmListMaps"},//15
	{"Whois"},//16
	{"amLookup"},//17
	{"Hide"},//18
	{"See Hiders"},//19
	{"Callvote"},//20
	{"Killvote"},//21
	{"Read AmSay"},//22
	{"IP Lock"},//23
	{"Trusted"},//24
	{"No Race"},//25
	{"No Duel"},//26
	{"All Cosmetics"},//27
	{"Entities"},//28
	{"Database"}//29
};
static const int MAX_ACCOUNT_FLAGS = ARRAY_LEN( accountFlags );

void Svcmd_FlagAccount_f( void ) {
	const int args = trap->Argc();
	char username[16];

	if (args != 2 && args != 3 && args != 4) {
		trap->Print( "Usage: /flagAccount <username> <set (optional)> <flag>\n");
		return;
	}

	trap->Argv(1, username, sizeof(username));
	Q_strlwr(username);
	Q_CleanStr(username);
	
	if (!CheckUserExists(username)) {
		trap->Print( "User does not exist!\n");
		return;
	}

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s;
		int flags;

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));
		sql = "SELECT flags FROM LocalAccount WHERE username = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));
	
		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			flags = sqlite3_column_int(stmt, 0);
		}
		else if (s != SQLITE_DONE){
			G_ErrorPrint("ERROR: SQL Select Failed (Svcmd_FlagAccount_f 1)", s);
			CALL_SQLITE(finalize(stmt));
			CALL_SQLITE(close(db));
			return;
		}
		CALL_SQLITE (finalize(stmt));

		if ( args == 2 ) {
			int i = 0;
			for ( i = 0; i < MAX_ACCOUNT_FLAGS; i++ ) {
				if ( (flags & (1 << i)) ) {
					trap->Print( "%2d [X] %s\n", i, accountFlags[i].string );
				}
				else {
					trap->Print( "%2d [ ] %s\n", i, accountFlags[i].string );
				}
			}
			CALL_SQLITE (close(db));
			return;
		}
		else if (args == 3) {
			char arg[8] = { 0 };
			int index, i;
			//const uint32_t mask = (1 << MAX_ACCOUNT_FLAGS) - 1;
			gclient_t	*cl;

			trap->Argv( 2, arg, sizeof(arg) );
			index = atoi( arg );

			//DM Start: New -1 toggle all options.
			if (index < 0 || index >= MAX_ACCOUNT_FLAGS) {  //Whereas we need to allow -1 now, we must change the limit for this value.
				trap->Print("flagAccount: Invalid range: %i [0-%i]\n", index, MAX_ACCOUNT_FLAGS - 1);
				CALL_SQLITE (close(db));
				return;
			}

			if (flags & (1 << index)) 
				sql = "UPDATE LocalAccount SET flags = flags & ~? WHERE username = ?"; //loda redo this
			else 
				sql = "UPDATE LocalAccount SET flags = flags | ? WHERE username = ?";

			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_int (stmt, 1, (1 << index)));
			CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
			s = sqlite3_step(stmt);
			if (s == SQLITE_DONE) {
				trap->Print( "%s %s^7\n", accountFlags[index].string, ((flags & (1 << index))
					? "^1Disabled" : "^2Enabled") );
			}
			else {
				G_ErrorPrint("ERROR: SQL Update Failed (Svcmd_FlagAccount_f 2)", s);
			}

			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));

			for (i=0;  i<level.numPlayingClients; i++) {
				cl = &level.clients[level.sortedClients[i]];
				if (cl->pers.userName[0] && !Q_stricmp(cl->pers.userName, username)) {
					if (flags & (1 << index)) 
						cl->sess.accountFlags &= ~(1 << index);
					else
						cl->sess.accountFlags |= (1 << index);
					break;
				}
			}
		}
		else if (args == 4) { //set
			char arg[8] = { 0 };
			unsigned int bitmask = 0;
			trap->Argv( 2, arg, sizeof(arg) );
			int i;
			gclient_t	*cl;

			if (Q_stricmp(arg, "set")) {
				trap->Print( "Usage: /flagAccount <username> <set (optional)> <flag>\n");
				return;
			}

			trap->Argv( 3, arg, sizeof(arg) );
			if (atoi(arg))
				bitmask = atoi(arg);
			else if (!Q_stricmp(arg, "j") || !Q_stricmp(arg, "junior"))
				bitmask = g_juniorAdminLevel.integer;
			else if (!Q_stricmp(arg, "f") || !Q_stricmp(arg, "full"))
				bitmask = g_fullAdminLevel.integer;

			sql = "UPDATE LocalAccount SET flags = ? WHERE username = ?";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_int (stmt, 1, bitmask));
			CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
			s = sqlite3_step(stmt);

			if (s == SQLITE_DONE) {
				trap->Print("Account flag set.\n");
			}
			else {
				G_ErrorPrint("ERROR: SQL Update Failed (Svcmd_FlagAccount_f 2)", s);
			}

			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));

			for (i=0;  i<level.numPlayingClients; i++) {
				cl = &level.clients[level.sortedClients[i]];
				if (cl->pers.userName[0] && !Q_stricmp(cl->pers.userName, username)) {
					cl->sess.accountFlags = bitmask;
					break;
				}
			}
		}
	}
}

void Svcmd_ListAdmins_f(void)
{
	if (trap->Argc() != 1) {
		trap->Print("Usage: /listAdmins\n");
		return;
	}

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s;
		//unsigned int flags;
		char adminString[16];
		int row = 1;

		CALL_SQLITE(open(LOCAL_DB_PATH, &db));

		sql = "SELECT username, flags FROM localAccount WHERE flags & 4194304 ORDER BY flags DESC"; //ehh
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));

		Com_Printf("    ^5Username           Admin\n");

		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				//flags = sqlite3_column_int(stmt, 1);
				Q_strncpyz(adminString, "Admin", sizeof(adminString));
				Com_Printf(va("^5%2i^3: ^3%-18s %s^7\n", row, (char*)sqlite3_column_text(stmt, 0), adminString));
				row++;
			}
			else if (s == SQLITE_DONE) {
				break;
			}
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Svcmd_ListAdmins)", s);
				break;
			}
		}
		CALL_SQLITE(finalize(stmt));
		CALL_SQLITE(close(db));
	}
}

void Svcmd_DBInfo_f(void)
{
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	int s, numAccounts = 0, numRaces = 0, numDuels = 0;

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
	sql = "SELECT COUNT(*) FROM LocalAccount";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
    s = sqlite3_step(stmt);
    if (s == SQLITE_ROW)
		numAccounts = sqlite3_column_int(stmt, 0);
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (Svcmd_DBInfo_f 1)", s);
		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
		return;
	}
	CALL_SQLITE (finalize(stmt));

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
	sql = "SELECT COUNT(*) FROM LocalRun";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
    s = sqlite3_step(stmt);
    if (s == SQLITE_ROW)
		numRaces = sqlite3_column_int(stmt, 0);
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (Svcmd_DBInfo_f 2)", s);
		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
		return;
	}
	CALL_SQLITE (finalize(stmt));

	sql = "SELECT COUNT(*) FROM LocalDuel";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
    s = sqlite3_step(stmt);
    if (s == SQLITE_ROW)
		numDuels = sqlite3_column_int(stmt, 0);
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (Svcmd_DBInfo_f 3)", s);
		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
		return;
	}
	CALL_SQLITE (finalize(stmt));

	CALL_SQLITE (close(db));

	trap->Print( "There are %i accounts, %i race records, and %i duels in the database.\n", numAccounts, numRaces, numDuels);
}

static void G_WriteTrackedCSVCell(FILE *out, const char *text)
{
	const char *cursor;

	if (!text)
	{
		fputs("\"\"", out);
		return;
	}

	fputc('"', out);
	for (cursor = text; *cursor; cursor++)
	{
		if (*cursor == '"')
			fputc('"', out);
		fputc(*cursor, out);
	}
	fputc('"', out);
}

static qboolean G_ExportTrackedDuelTableCSV(sqlite3 *db, const char *tableName, const char *outputPath, int *rowsWritten)
{
	sqlite3_stmt *stmt = NULL;
	char sql[256];
	FILE *out;
	int s;
	int i;
	int colCount;
	int localRows = 0;

	if (!db || !tableName || !outputPath)
		return qfalse;

	Com_sprintf(sql, sizeof(sql), "SELECT * FROM %s", tableName);
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	colCount = sqlite3_column_count(stmt);
	if (colCount <= 0)
	{
		CALL_SQLITE(finalize(stmt));
		return qfalse;
	}

	out = fopen(outputPath, "wb");
	if (!out)
	{
		trap->Print("Failed opening export file: %s\n", outputPath);
		CALL_SQLITE(finalize(stmt));
		return qfalse;
	}

	for (i = 0; i < colCount; i++)
	{
		if (i > 0)
			fputc(',', out);
		G_WriteTrackedCSVCell(out, sqlite3_column_name(stmt, i));
	}
	fputc('\n', out);

	while ((s = sqlite3_step(stmt)) == SQLITE_ROW)
	{
		for (i = 0; i < colCount; i++)
		{
			const unsigned char *text;
			if (i > 0)
				fputc(',', out);
			text = sqlite3_column_text(stmt, i);
			G_WriteTrackedCSVCell(out, text ? (const char *)text : "");
		}
		fputc('\n', out);
		localRows++;
	}

	if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Select Failed (G_ExportTrackedDuelTableCSV)", s);
		fclose(out);
		remove(outputPath);
		CALL_SQLITE(finalize(stmt));
		return qfalse;
	}

	fclose(out);
	CALL_SQLITE(finalize(stmt));
	if (rowsWritten)
		*rowsWritten = localRows;
	return qtrue;
}

static void G_SanitizeTrackedExportPrefix(const char *in, char *out, int outSize)
{
	int i;
	int outIndex = 0;

	if (!out || outSize < 1)
		return;

	out[0] = '\0';
	if (!in)
		return;

	for (i = 0; in[i] && outIndex < outSize - 1; i++)
	{
		const unsigned char ch = (const unsigned char)in[i];
		if (isalnum(ch) || ch == '_' || ch == '-')
			out[outIndex++] = (char)tolower(ch);
	}
	out[outIndex] = '\0';
}

static qboolean G_DoesTrackedDuelTableExist(sqlite3 *db, const char *tableName)
{
	sqlite3_stmt *stmt = NULL;
	char *sql;
	int s;
	qboolean exists = qfalse;

	if (!db || !tableName || !tableName[0])
		return qfalse;

	sql = "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_text(stmt, 1, tableName, -1, SQLITE_TRANSIENT));
	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW)
		exists = qtrue;
	else if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Select Failed (G_DoesTrackedDuelTableExist)", s);
	CALL_SQLITE(finalize(stmt));
	return exists;
}

static qboolean G_OpenTrackedLocalDB(sqlite3 **dbOut, char *resolvedPath, int resolvedPathSize)
{
	char fallbackDbPath[MAX_OSPATH];
	char fs_game[MAX_QPATH];
	sqlite3 *db = NULL;

	if (!dbOut)
		return qfalse;
	*dbOut = NULL;
	if (!LOCAL_DB_PATH[0])
		return qfalse;

	trap->Cvar_VariableStringBuffer("fs_game", fs_game, sizeof(fs_game));
	if (!VALIDSTRING(fs_game))
	{
		trap->Cvar_VariableStringBuffer("fs_basegame", fs_game, sizeof(fs_game));
		if (!VALIDSTRING(fs_game))
			Q_strncpyz(fs_game, TAYSTJKGAME, sizeof(fs_game));
	}
	Com_sprintf(fallbackDbPath, sizeof(fallbackDbPath), "%s/data.db", fs_game);

	if (sqlite3_open(LOCAL_DB_PATH, &db) == SQLITE_OK)
	{
		*dbOut = db;
		if (resolvedPath && resolvedPathSize > 0)
			Q_strncpyz(resolvedPath, LOCAL_DB_PATH, resolvedPathSize);
		return qtrue;
	}
	if (db)
		sqlite3_close(db);
	db = NULL;

	if (Q_stricmp(LOCAL_DB_PATH, fallbackDbPath) && sqlite3_open(fallbackDbPath, &db) == SQLITE_OK)
	{
		*dbOut = db;
		if (resolvedPath && resolvedPathSize > 0)
			Q_strncpyz(resolvedPath, fallbackDbPath, resolvedPathSize);
		return qtrue;
	}
	if (db)
		sqlite3_close(db);
	return qfalse;
}

void Svcmd_ExportDuelTrack_f(void)
{
	const char *trackedTables[5];
	int trackedTableCount = 0;
	sqlite3 *db;
	char dbDir[MAX_OSPATH];
	char effectiveDbPath[MAX_OSPATH];
	char timestamp[32];
	char outPath[MAX_OSPATH];
	char optionalPrefix[64];
	char safePrefix[64];
	char *slashPos;
	char *backslashPos;
	time_t rawtime;
	struct tm tmLocal;
	int i;
	int rows;
	int msPart;
	char pathSep;

	optionalPrefix[0] = '\0';
	if (trap->Argc() >= 2)
	{
		char token[64];
		size_t curLen;
		size_t tokenLen;
		size_t needed;
		trap->Argv(1, optionalPrefix, sizeof(optionalPrefix));
		for (i = 2; i < trap->Argc(); i++)
		{
			trap->Argv(i, token, sizeof(token));
			curLen = strlen(optionalPrefix);
			tokenLen = strlen(token);
			needed = tokenLen + ((optionalPrefix[0]) ? 1 : 0);
			if (curLen + needed >= sizeof(optionalPrefix))
				break;
			if (optionalPrefix[0])
				Q_strcat(optionalPrefix, sizeof(optionalPrefix), "_");
			Q_strcat(optionalPrefix, sizeof(optionalPrefix), token);
		}
		G_SanitizeTrackedExportPrefix(optionalPrefix, safePrefix, sizeof(safePrefix));
	}
	else
	{
		safePrefix[0] = '\0';
	}

	if (!LOCAL_DB_PATH[0])
	{
		trap->Print("Duel tracking export unavailable: LOCAL_DB_PATH is not initialized yet.\n");
		return;
	}

	if (!G_OpenTrackedLocalDB(&db, effectiveDbPath, sizeof(effectiveDbPath)))
	{
		trap->Print("exportDuelTrack failed: unable to open local duel database.\n");
		return;
	}
	trackedTables[trackedTableCount++] = "LocalDuelTrackSummary";
	trackedTables[trackedTableCount++] = "LocalDuelTrackParticipant";
	trackedTables[trackedTableCount++] = "LocalDuelTrackEvent";
	if (G_DoesTrackedDuelTableExist(db, "LocalDuelTrackGeometry"))
		trackedTables[trackedTableCount++] = "LocalDuelTrackGeometry";
	trackedTables[trackedTableCount++] = "LocalDuelTrackAggregate";

	Q_strncpyz(dbDir, effectiveDbPath, sizeof(dbDir));
	slashPos = strrchr(dbDir, '/');
	backslashPos = strrchr(dbDir, '\\');
	if (backslashPos && (!slashPos || backslashPos > slashPos))
		slashPos = backslashPos;
	if (slashPos)
		*slashPos = '\0';
	else
		dbDir[0] = '\0';
#if defined(_WIN32)
	pathSep = '\\';
#else
	pathSep = '/';
#endif

	time(&rawtime);
#if defined(_WIN32)
	if (localtime_s(&tmLocal, &rawtime) != 0)
	{
		if (db)
			sqlite3_close(db);
		trap->Print("exportDuelTrack failed: could not format local timestamp.\n");
		return;
	}
#else
	if (!localtime_r(&rawtime, &tmLocal))
	{
		if (db)
			sqlite3_close(db);
		trap->Print("exportDuelTrack failed: could not format local timestamp.\n");
		return;
	}
#endif
	if (!strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tmLocal))
	{
		if (db)
			sqlite3_close(db);
		trap->Print("exportDuelTrack failed: could not format local timestamp.\n");
		return;
	}
	msPart = trap->Milliseconds() % 1000;

	for (i = 0; i < trackedTableCount; i++)
	{
		if (!G_DoesTrackedDuelTableExist(db, trackedTables[i]))
		{
			trap->Print("Skipping %s: table not present in %s\n", trackedTables[i], effectiveDbPath);
			continue;
		}
		if (safePrefix[0] && dbDir[0])
			Com_sprintf(outPath, sizeof(outPath), "%s%c%s_dueltrack_%s_%03d_%s.csv", dbDir, pathSep, safePrefix, timestamp, msPart, trackedTables[i]);
		else if (safePrefix[0])
			Com_sprintf(outPath, sizeof(outPath), "%s_dueltrack_%s_%03d_%s.csv", safePrefix, timestamp, msPart, trackedTables[i]);
		else if (dbDir[0])
			Com_sprintf(outPath, sizeof(outPath), "%s%cdueltrack_%s_%03d_%s.csv", dbDir, pathSep, timestamp, msPart, trackedTables[i]);
		else
			Com_sprintf(outPath, sizeof(outPath), "dueltrack_%s_%03d_%s.csv", timestamp, msPart, trackedTables[i]);
		if (G_ExportTrackedDuelTableCSV(db, trackedTables[i], outPath, &rows))
			trap->Print("Exported %s (%d rows) -> %s\n", trackedTables[i], rows, outPath);
	}

	CALL_SQLITE(close(db));
}

void Svcmd_ClanDelete_f(void) {
	sqlite3 * db;
	char * sql;
    sqlite3_stmt * stmt;
	int s;
	char teamname[16];
	//int flags = 0;
	int count = 0;

	if (trap->Argc() != 2) {
		trap->Print( "Usage: /clanDelete <clan>\n");
		return;
	}

	trap->Argv(1, teamname, sizeof(teamname));

	Q_strlwr(teamname);
	Q_CleanStr(teamname);
	
	CALL_SQLITE (open (LOCAL_DB_PATH, & db));

	sql = "SELECT COUNT(*) FROM LocalTeam WHERE name = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
	
	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW) {
		count = sqlite3_column_int(stmt, 0);
		if (count == 0) {
			trap->Print( "Clan does not exist!\n");
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
	}
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (Svcmd_ClanDelete_f 1)", s);
		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
		return;
	}
	CALL_SQLITE (finalize(stmt));

	sql = "DELETE FROM LocalTeam WHERE name = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
	s = sqlite3_step(stmt);

	if (s == SQLITE_DONE) {
		trap->Print( "Clan deleted.\n");
	}
	else
		G_ErrorPrint("ERROR: SQL Delete Failed (Svcmd_ClanDelete_f 2)", s);

	CALL_SQLITE (finalize(stmt));

	CALL_SQLITE (close(db));
}

void Svcmd_ClanCreate_f(void) {
	sqlite3 * db;
	char * sql;
    sqlite3_stmt * stmt;
	int s;
	char teamname[16];
	//int flags = 0;
	int count = 0;

	if (trap->Argc() != 2) {
		trap->Print( "Usage: /clanCreate <clan>\n");
		return;
	}

	trap->Argv(1, teamname, sizeof(teamname));

	Q_strlwr(teamname);
	Q_CleanStr(teamname);
	
	CALL_SQLITE (open (LOCAL_DB_PATH, & db));

	sql = "SELECT COUNT(*) FROM LocalTeam WHERE name = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
	
	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW) {
		count = sqlite3_column_int(stmt, 0);
		if (count > 0) {
			trap->Print( "Clan already exists!\n");
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
	}
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (Svcmd_ClanCreate_f 1)", s);
		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
		return;
	}
	CALL_SQLITE (finalize(stmt));

	sql = "INSERT INTO LocalTeam (name, flags) VALUES (?, 1)";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
	s = sqlite3_step(stmt);

	if (s == SQLITE_DONE) {
		trap->Print( "Clan created.\n");
	}
	else
		G_ErrorPrint("ERROR: SQL Insert Failed (Svcmd_ClanCreate_f 2)", s);

	CALL_SQLITE (finalize(stmt));

	CALL_SQLITE (close(db));
}

void Svcmd_ClanKick_f(void) {
	sqlite3 * db;
	char * sql;
    sqlite3_stmt * stmt;
	int s;
	char username[16], teamname[16];
	//int flags = 0;
	int count = 0;

	if (trap->Argc() != 3) {
		trap->Print( "Usage: /clanKick <clan> <username>\n");
		return;
	}

	trap->Argv(1, teamname, sizeof(teamname));
	trap->Argv(2, username, sizeof(username));

	Q_strlwr(username);
	Q_CleanStr(username);

	Q_strlwr(teamname);
	Q_CleanStr(teamname);
	
	CALL_SQLITE (open (LOCAL_DB_PATH, & db));

	if (!CheckUserExists(username)) {
		trap->Print( "This user does not exist!\n");
		return;
	}

	sql = "SELECT COUNT(*) FROM LocalTeam WHERE name = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
	
	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW) {
		count = sqlite3_column_int(stmt, 0);
		if (count == 0) {
			trap->Print( "Clan does not exist!\n");
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
	}
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (Svcmd_ClanKick_f 1)", s);
		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
		return;
	}
	CALL_SQLITE (finalize(stmt));

	sql = "SELECT COUNT(*) FROM LocalTeamAccount WHERE team = ? AND account = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
	
	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW) {
		count = sqlite3_column_int(stmt, 0);
		if (count == 0) {
			trap->Print( "User is not in this clan!\n");
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
	}
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (Svcmd_ClanKick_f 2)", s);
		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
		return;
	}
	CALL_SQLITE (finalize(stmt));
	

	sql = "DELETE FROM LocalTeamAccount WHERE team = ? AND account = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
	s = sqlite3_step(stmt);

	if (s == SQLITE_DONE) {
		trap->Print( "User removed from clan.\n");
	}
	else
		G_ErrorPrint("ERROR: SQL Delete Failed (Svcmd_ClanKick_f 3)", s);

	CALL_SQLITE (finalize(stmt));

	CALL_SQLITE (close(db));
}

void Svcmd_ClanJoin_f(void) {
	sqlite3 * db;
	char * sql;
    sqlite3_stmt * stmt;
	int s;
	char username[16], teamname[16];
	//int flags = 0;
	int count = 0;

	if (trap->Argc() != 3) {
		trap->Print( "Usage: /clanJoin <clan> <username>\n");
		return;
	}

	trap->Argv(1, teamname, sizeof(teamname));
	trap->Argv(2, username, sizeof(username));

	Q_strlwr(username);
	Q_CleanStr(username);

	Q_strlwr(teamname);
	Q_CleanStr(teamname);
	
	CALL_SQLITE (open (LOCAL_DB_PATH, & db));

	if (!CheckUserExists(username)) {
		trap->Print( "This user does not exist!\n");
		return;
	}

	sql = "SELECT COUNT(*) FROM LocalTeam WHERE name = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
	
	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW) {
		count = sqlite3_column_int(stmt, 0);
		if (count == 0) {
			trap->Print( "Clan does not exist.\n");
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
	}
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (Svcmd_ClanJoin_f 1)", s);
		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
		return;
	}
	CALL_SQLITE (finalize(stmt));

	sql = "SELECT COUNT(*) FROM LocalTeamAccount WHERE team = ? AND account = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
	
	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW) {
		count = sqlite3_column_int(stmt, 0);
		if (count > 0) {
			trap->Print( "User is already in this clan!\n");
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
	}
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (Svcmd_ClanJoin_f 2)", s);
		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
		return;
	}
	CALL_SQLITE (finalize(stmt));
	

	sql = "INSERT INTO LocalTeamAccount (team, account, flags) VALUES (?, ?, 0)";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
	s = sqlite3_step(stmt);

	if (s == SQLITE_DONE) {
		trap->Print( "User added to clan.\n");
	}
	else
		G_ErrorPrint("ERROR: SQL Insert Failed (Svcmd_ClanJoin_f 3)", s);

	CALL_SQLITE (finalize(stmt));

	CALL_SQLITE (close(db));
}

void Cmd_ACRegister_f( gentity_t *ent ) { //Temporary, until global shit is done
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	char username[16], password[16], strIP[NET_ADDRSTRMAXLEN] = {0};
	char *p = NULL;
	time_t	rawtime;
	int s;
	unsigned int ip;

	if (!g_allowRegistration.integer) {
		trap->SendServerCommand(ent-g_entities, "print \"This server does not allow registration\n\"");
		return;
	}
		
	if (trap->Argc() != 3) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /register <username> <password>\n\"");
		return;
	}

	if (Q_stricmp(ent->client->pers.userName, "")) { //if (ent->client->pers.accountName) { //check cuz its a string if [0] = \0 or w/e
		trap->SendServerCommand(ent-g_entities, "print \"You are already logged in!\n\"");
		return;
	}

	trap->Argv(1, username, sizeof(username));
	trap->Argv(2, password, sizeof(password));

	Q_strlwr(username);
	Q_CleanStr(username);
	Q_strstrip(username, " \n\r;:.?*<>!#$&'()+@=`~{}[]^_|\\/\"", NULL);

	Q_CleanStr(password);

	if (!username[0]) {
		return;
	}
	if (!Q_stricmp(username, "none") || !Q_stricmp(username, "null") || !Q_stricmp(username, "0") || !Q_stricmp(username, "hidden")) {
		return;
	}
	if (!Q_stricmp(username, password)) {
		trap->SendServerCommand(ent-g_entities, "print \"Username and password cannot be the same\n\"");
		return;
	}
	if (CheckUserExists(username)) {
		trap->SendServerCommand(ent-g_entities, "print \"This account name has already been taken!\n\"");
		return;
	}

	time( &rawtime );
	localtime( &rawtime );

	Q_strncpyz(strIP, ent->client->sess.IP, sizeof(strIP));
	p = strchr(strIP, ':');
	if (p) //loda - fix ip sometimes not printing
		*p = 0;
	ip = ip_to_int(strIP);

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));

	if (ip) {
		sql = "SELECT COUNT(*) FROM LocalAccount WHERE lastip = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_int64 (stmt, 1, ip));

		s = sqlite3_step(stmt);

		if (s == SQLITE_ROW) {
			int count;
			count = sqlite3_column_int(stmt, 0);
			if (count > 0) {
				trap->SendServerCommand(ent-g_entities, "print \"Your IP address already belongs to an account. You are only allowed one account.\n\"");
				CALL_SQLITE (finalize(stmt));
				CALL_SQLITE (close(db));
				return;
			}
		}
		else if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Select Failed (Cmd_ACRegister_f 1)", s);
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
		CALL_SQLITE (finalize(stmt));
	}

    sql = "INSERT INTO LocalAccount (username, password, kills, deaths, suicides, captures, returns, racetime, created, lastlogin, lastip, flags, unlocks) VALUES (?, ?, 0, 0, 0, 0, 0, 0, ?, ?, ?, 0, 0)";
    CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
    CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_text (stmt, 2, password, -1, SQLITE_STATIC));
	CALL_SQLITE (bind_int (stmt, 3, rawtime));
	CALL_SQLITE (bind_int (stmt, 4, rawtime));
	CALL_SQLITE (bind_int64 (stmt, 5, ip));
	s = sqlite3_step(stmt);

	if (s == SQLITE_DONE) {
		trap->SendServerCommand(ent-g_entities, "print \"Account created.\n\"");
		Q_strncpyz(ent->client->pers.userName, username, sizeof(ent->client->pers.userName));
	}
	else
		G_ErrorPrint("ERROR: SQL Insert Failed (Cmd_ACRegister_f 2)", s);


	CALL_SQLITE (finalize(stmt));
	CALL_SQLITE (close(db));

	//DebugWriteToDB("Cmd_ACRegister_f");
}

void Cmd_ACLogout_f( gentity_t *ent ) { //If logged in, print logout msg, remove login status.
	if (ent->client->pers.userName[0]) {
		if (ent->client->sess.raceMode && !ent->client->pers.practice && ent->client->pers.stats.startTime) {
			ent->client->pers.stats.racetime += (trap->Milliseconds() - ent->client->pers.stats.startTime)*0.001f - ent->client->afkDuration*0.001f;
			ent->client->afkDuration = 0;
		}
		if (ent->client->pers.stats.racetime >= 1.0f) {
			G_UpdatePlaytime(0, ent->client->pers.userName, (int)(ent->client->pers.stats.racetime+0.5f));
			ent->client->pers.stats.racetime = 0.0f;
		}

		//ent->client->pers.unlocks = 0;
		//ent->client->sess.accountFlags = 0;
		trap->SendServerCommand(-1, va("print \"%s^7 (%s) has logged out\n\"", ent->client->pers.netname, ent->client->pers.userName));
		Q_strncpyz(ent->client->pers.userName, "", sizeof(ent->client->pers.userName));
	}
	else
		trap->SendServerCommand(ent-g_entities, "print \"You are not logged in!\n\"");
}

void Cmd_JoinTeam_f( gentity_t *ent ) {
	char username[16], teamname[16];

	if (g_allowRegistration.integer < 2) { //?
		trap->SendServerCommand(ent-g_entities, "print \"This server does not allow you to join a clan.\n\"");
		return;
	}
		
	if (trap->Argc() != 2) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /clanJoin <name>\n\"");
		return;
	}

	if (!Q_stricmp(ent->client->pers.userName, "")) {
		trap->SendServerCommand(ent-g_entities, "print \"You must be logged in to use this command.\n\"");
		return;
	}

	Q_strncpyz(username, ent->client->pers.userName, sizeof(username));
	trap->Argv(1, teamname, sizeof(teamname));

	Q_strlwr(teamname);
	Q_CleanStr(teamname);
	Q_strstrip(teamname, " \n\r;:.?*<>!#$&'()+@=`~{}[]^_|\\/\"", NULL);

	//get team_id and user_id
	//make sure user is not already in a team?
	//make sure user does not already own a team?
	//make sure team is public or there is invite? (entry in LocalTeamAccount with flag_PENDING)
	//insert into LocalTeamAccount with team_id and user_id

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s;//, row = 0;
		qboolean inviteOnly = qfalse;
		int count = 0;

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));

		sql = "SELECT flags FROM LocalTeam WHERE name = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
	
		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			int flags = sqlite3_column_int(stmt, 0);
			if (flags & JAPRO_TEAMFLAG_PRIVATE) {
				inviteOnly = qtrue;
			}
		}
		else if (s == SQLITE_DONE) {
			trap->SendServerCommand(ent-g_entities, "print \"Clan does not exist!\n\""); //You already own a team
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
		else {
			G_ErrorPrint("ERROR: SQL Select Failed (Cmd_JoinTeam_f 1)", s);
		}
		CALL_SQLITE (finalize(stmt));

		if (inviteOnly) {
			sql = "SELECT flags FROM LocalTeamAccount WHERE team = ? AND account = ?";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
			CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
	
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				int flags = sqlite3_column_int(stmt, 0);
				if (!(flags & JAPRO_ACCOUNTTEAMFLAG_PENDING)) {
					trap->SendServerCommand(ent-g_entities, "print \"You are already in this clan!\n\"");//Already in this clan?
					CALL_SQLITE (finalize(stmt));
					CALL_SQLITE (close(db));
					return;
				}
				else {
					count = 1; //They do have a row
				}
			}
			else if (s == SQLITE_DONE) {
				trap->SendServerCommand(ent-g_entities, "print \"This clan is invite-only!\n\"");//Not yet invited
				CALL_SQLITE (finalize(stmt));
				CALL_SQLITE (close(db));
				return;
			}
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_JoinTeam_f 1)", s);
			}
			CALL_SQLITE (finalize(stmt));
		}
		else {
			sql = "SELECT COUNT(*) FROM LocalTeamAccount WHERE team = ? AND account = ?";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
			CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
	
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				count = sqlite3_column_int(stmt, 0);
				if (count > 0) {
					trap->SendServerCommand(ent-g_entities, "print \"You are already in this clan!\n\""); //You already own a team - optional?
					CALL_SQLITE (finalize(stmt));
					CALL_SQLITE (close(db));
					return;
				}
			}
			else if (s != SQLITE_DONE) {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_JoinTeam_f 2)", s);
				CALL_SQLITE (finalize(stmt));
				CALL_SQLITE (close(db));
				return;
			}
			CALL_SQLITE (finalize(stmt));
		}

		if (count > 0)
			sql = "UPDATE LocalTeamAccount SET flags = 0 WHERE team = ? AND account = ?";
		else 
			sql = "INSERT INTO LocalTeamAccount (team, account, flags) VALUES (?, ?, 0)"; //Replace
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
		s = sqlite3_step(stmt);

		if (s == SQLITE_DONE) {
			trap->SendServerCommand(ent-g_entities, "print \"Clan joined.\n\"");//Not yet invited
		}
		else
			G_ErrorPrint("ERROR: SQL Insert Failed (Cmd_JoinTeam_f 3)", s);

		CALL_SQLITE (finalize(stmt));

		CALL_SQLITE (close(db));
	}

}

void Cmd_LeaveTeam_f( gentity_t *ent ) {
	char username[16], teamname[16];
		
	if (trap->Argc() != 2) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /clanLeave <name>\n\"");
		return;
	}

	if (!Q_stricmp(ent->client->pers.userName, "")) {
		trap->SendServerCommand(ent-g_entities, "print \"You must be logged in to use this command.\n\"");
		return;
	}

	Q_strncpyz(username, ent->client->pers.userName, sizeof(username));
	trap->Argv(1, teamname, sizeof(teamname));

	Q_strlwr(teamname);
	Q_CleanStr(teamname);
	Q_strstrip(teamname, " \n\r;:.?*<>!#$&'()+@=`~{}[]^_|\\/\"", NULL);

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s;//, row = 0;
		int count;

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));

		sql = "SELECT flags FROM LocalTeam WHERE name = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
	
		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
		}
		else if (s == SQLITE_DONE) {
			trap->SendServerCommand(ent-g_entities, "print \"Clan does not exist!\n\""); //You already own a team
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
		else {
			G_ErrorPrint("ERROR: SQL Select Failed (Cmd_JoinTeam_f 1)", s);
		}
		CALL_SQLITE (finalize(stmt));

		sql = "SELECT COUNT(*) FROM LocalTeamAccount WHERE team = ? AND account = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
	
		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			count = sqlite3_column_int(stmt, 0);
			if (count == 0) {
				trap->SendServerCommand(ent-g_entities, "print \"You are not in this clan!\n\""); //You already own a team - optional?
				CALL_SQLITE (finalize(stmt));
				CALL_SQLITE (close(db));
				return;
			}
		}
		else if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Select Failed (Cmd_JoinTeam_f 2)", s);
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
		CALL_SQLITE (finalize(stmt));

		sql = "DELETE FROM LocalTeamAccount WHERE team = ? AND account = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
		s = sqlite3_step(stmt);

		if (s == SQLITE_DONE) {
			trap->SendServerCommand(ent-g_entities, "print \"Clan left.\n\"");
		}
		else
			G_ErrorPrint("ERROR: SQL Delete Failed (Cmd_JoinTeam_f 3)", s);

		CALL_SQLITE (finalize(stmt));

		CALL_SQLITE (close(db));
	}

}

void Cmd_CreateTeam_f( gentity_t *ent ) {
	char username[16], teamname[16];

	if (g_allowRegistration.integer < 3) { //?
		trap->SendServerCommand(ent-g_entities, "print \"This server does not allow team creation.\n\"");
		return;
	}
		
	if (trap->Argc() != 2) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /clanCreate <name>\n\"");
		return;
	}

	if (!Q_stricmp(ent->client->pers.userName, "")) {
		trap->SendServerCommand(ent-g_entities, "print \"You must be logged in to use this command.\n\"");
		return;
	}

	Q_strncpyz(username, ent->client->pers.userName, sizeof(username));
	trap->Argv(1, teamname, sizeof(teamname));

	Q_strlwr(teamname);
	Q_CleanStr(teamname);
	Q_strstrip(teamname, " \n\r;:.?*<>!#$&'()+@=`~{}[]^_|\\/\"", NULL);

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s;//, row = 0;

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));

		sql = "SELECT COUNT(*) FROM LocalTeam WHERE name = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
		s = sqlite3_step(stmt);

		if (s == SQLITE_ROW) {
			int count = sqlite3_column_int(stmt, 0);
			if (count > 0) {
				trap->SendServerCommand(ent-g_entities, "print \"This clan already exists.\n\"");
				CALL_SQLITE (finalize(stmt));
				CALL_SQLITE (close(db));
				return;
			}
		}
		else if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Select Failed (Cmd_CreateTeam_f 1)", s);
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
		CALL_SQLITE (finalize(stmt));

		sql = "SELECT COUNT(*) FROM LocalTeamAccount WHERE account = ?"; //AND FLAGS = OWNER, fixme
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));
		s = sqlite3_step(stmt);

		if (s == SQLITE_ROW) {
			int count;
			count = sqlite3_column_int(stmt, 0);
			if (count > 0) {
				trap->SendServerCommand(ent-g_entities, "print \"You are already in a clan.\n\""); //You already own a team
				CALL_SQLITE (finalize(stmt));
				CALL_SQLITE (close(db));
				return;
			}
		}
		else if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Select Failed (Cmd_CreateTeam_f 2)", s);
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
		CALL_SQLITE (finalize(stmt));

		sql = "INSERT INTO LocalTeam (name, flags) VALUES (?, 0)";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
		s = sqlite3_step(stmt);

		if (s == SQLITE_DONE) {
		}
		else
			G_ErrorPrint("ERROR: SQL Insert Failed (Cmd_CreateTeam_f 4)", s);

		CALL_SQLITE (finalize(stmt));

		sql = "INSERT INTO LocalTeamAccount (team, account, flags) VALUES (?, ?, ?)";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 3, JAPRO_ACCOUNTTEAMFLAG_OWNER)); //1, JAPRO_ACCOUNTTEAMFLAG_OWNER

		s = sqlite3_step(stmt);

		if (s == SQLITE_DONE) {
			trap->SendServerCommand(ent-g_entities, "print \"Clan created.\n\"");
		}
		else
			G_ErrorPrint("ERROR: SQL Insert Failed (Cmd_CreateTeam_f 6)", s);

		CALL_SQLITE (finalize(stmt));

		CALL_SQLITE (close(db));
	}
}


void Cmd_InfoTeam_f( gentity_t *ent ) {
	char teamname[16], pageStr[8];
	int page = 1, start;
	const int args = trap->Argc();

	if (args != 2 && args != 3) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /clanInfo <clan> <page (optional)>\n\"");
		return;
	}

	trap->Argv(1, teamname, sizeof(teamname));
	Q_strlwr(teamname);
	Q_CleanStr(teamname);
	Q_strstrip(teamname, " \n\r;:.?*<>!#$&'()+@=`~{}[]^_|\\/\"", NULL);

	if (trap->Argc() == 3) {
		trap->Argv(2, pageStr, sizeof(pageStr));
		page = atoi(pageStr);

		if (page < 1 || page > 100) {
			trap->SendServerCommand(ent-g_entities, "print \"Usage: /clanInfo <clan> <page (optional)>\n\"");
			return;
		}
	}

	start = (page - 1) * 10;

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s, row = 1;
		char msg[1024-128] = {0}, playername[16] = {0};

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));

		sql = "SELECT username, ROUND(SUM((entries/CAST(rank AS FLOAT) + entries-rank))/2,0) AS score FROM LocalRun WHERE rank != 0 AND username IN (SELECT account FROM LocalTeamAccount WHERE team = ? AND (flags & 2 != 2)) GROUP BY username ORDER BY score DESC LIMIT ?, 10";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 2, start));

		trap->SendServerCommand(ent-g_entities, va("print \"clanInfo %s:\n    ^5Name               Score\n\"", teamname));
	
		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char *tmpMsg = NULL;

				Q_strncpyz(playername, (char*)sqlite3_column_text(stmt, 0), sizeof(playername));

				tmpMsg = va("^5%2i^3: ^3%-18s %i\n", start+row, playername, sqlite3_column_int(stmt, 1));
				if (strlen(msg) + strlen(tmpMsg) >= sizeof( msg)) {
					trap->SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
					msg[0] = '\0';
				}
				Q_strcat(msg, sizeof(msg), tmpMsg);
				row++;
			}
			else if (s == SQLITE_DONE) {
				trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));
				break;
			}
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_InfoTeam_f)", s);
				break;
			}
		}

		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
	}
}

void Cmd_AddMaster_f(gentity_t *ent) {
	char username[16], mastername[16];

	if (trap->Argc() != 2) {
		trap->SendServerCommand(ent - g_entities, "print \"Usage: /master <name (or none)>\n\"");
		return;
	}

	if (!Q_stricmp(ent->client->pers.userName, "")) {
		trap->SendServerCommand(ent - g_entities, "print \"You must be logged in to use this command.\n\"");
		return;
	}

	Q_strncpyz(username, ent->client->pers.userName, sizeof(username));
	trap->Argv(1, mastername, sizeof(mastername));

	Q_strlwr(mastername);
	Q_CleanStr(mastername);
	Q_strstrip(mastername, " \n\r;:.?*<>!#$&'()+@=`~{}[]^_|\\/\"", NULL);

	//get team_id and user_id
	//make sure user is not already in a team?
	//make sure user does not already own a team?
	//make sure team is public or there is invite? (entry in LocalTeamAccount with flag_PENDING)
	//insert into LocalTeamAccount with team_id and user_id

	if (!Q_stricmp(ent->client->pers.userName, mastername)) {
		trap->SendServerCommand(ent - g_entities, "print \"You can not be your own master.\n\"");
		return;
	}
	if (Q_stricmp(mastername, "none") && !CheckUserExists(mastername)) { //if its not none and it doesnt exist
		trap->SendServerCommand(ent - g_entities, "print \"Master does not exist.\n\"");
		return;
	}

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s;

		CALL_SQLITE(open(LOCAL_DB_PATH, &db));

		if (!Q_stricmp(mastername, "none")) {
			sql = "UPDATE LocalAccount SET master = NULL WHERE username = ?";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));
		}
		else {
#if 0
			//Make sure we are not their master
			sql = "SELECT id FROM LocalAccount WHERE master = ? AND username = ?";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_text(stmt, 2, mastername, -1, SQLITE_STATIC));

			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				trap->SendServerCommand(ent - g_entities, "print \"You can not be your own master.\n\"");
				CALL_SQLITE(finalize(stmt));
				CALL_SQLITE(close(db));
				return;
			}
			else if (s != SQLITE_DONE) {
				G_ErrorPrint("ERROR: SQL Update Failed (Cmd_AddMaster_f 1)", s);
			}
			CALL_SQLITE(finalize(stmt));
#endif

			sql = "UPDATE LocalAccount SET master = ? WHERE username = ?";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, mastername, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_text(stmt, 2, username, -1, SQLITE_STATIC));
		}

		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Update Failed (Cmd_AddMaster_f 2)", s);
		}
		else {
			trap->SendServerCommand(ent - g_entities, "print \"Master added.\n\"");
		}
		CALL_SQLITE(finalize(stmt));

		CALL_SQLITE(close(db));
	}

}

void Cmd_ListMasters_f(gentity_t *ent) {
	char pageStr[8];
	int page = 1, start;

	if (trap->Argc() != 1 && trap->Argc() != 2) {
		trap->SendServerCommand(ent - g_entities, "print \"Usage: /masterList <page (optional)>\n\"");
		return;
	}

	if (trap->Argc() == 2) {
		trap->Argv(1, pageStr, sizeof(pageStr));
		page = atoi(pageStr);

		if (page < 1 || page > 100) {
			trap->SendServerCommand(ent - g_entities, "print \"Usage: /masterList <page (optional)>\n\"");
			return;
		}
	}

	start = (page - 1) * 10;

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s, row = 1;
		char msg[1024 - 128] = { 0 }, mastername[16] = { 0 };

		CALL_SQLITE(open(LOCAL_DB_PATH, &db));

		sql = "SELECT T1.master, T1.username FROM "
			"(SELECT master, username FROM LocalAccount WHERE master IS NOT NULL) AS T1 "
			"INNER JOIN(SELECT master, COUNT(*) AS count FROM LocalAccount WHERE master IS NOT NULL GROUP BY master) AS T2 "
			"ON T1.master = T2.master ORDER BY T2.count DESC, T1.master DESC LIMIT ?, 10"; //Order by score - OH BOY! Or by member count?
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		CALL_SQLITE(bind_int(stmt, 1, start));

		trap->SendServerCommand(ent - g_entities, "print \"Masterlist:\n    ^5Name               Padawans\"");

		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char *tmpMsg = NULL;

				if (!Q_stricmp((char*)sqlite3_column_text(stmt, 0), mastername)) { //Same master
					tmpMsg = va("%s ", sqlite3_column_text(stmt, 1)); ///Append Padawan
				}
				else { //New master
					Q_strncpyz(mastername, (char*)sqlite3_column_text(stmt, 0), sizeof(mastername)); //Store new master
					tmpMsg = va("\n^5%2i^3: ^3%-18s %s ", start + row, mastername, sqlite3_column_text(stmt, 1)); ///Append newline master and padawan
					row++;
				}

				//If mastername is equal to previous mastername, strcat 2nd col
				//Else, new col

				if (strlen(msg) + strlen(tmpMsg) >= sizeof(msg)) {
					trap->SendServerCommand(ent - g_entities, va("print \"%s\"", msg));
					msg[0] = '\0';
				}
				Q_strcat(msg, sizeof(msg), tmpMsg);
			}
			else if (s == SQLITE_DONE) {
				break;
			}
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_ListMasters_f)", s);
				break;
			}
		}

		trap->SendServerCommand(ent - g_entities, va("print \"%s\n\"", msg));

		CALL_SQLITE(finalize(stmt));
		CALL_SQLITE(close(db));
	}
}

void Cmd_ListTeam_f( gentity_t *ent ) { //Should i bother to cache player stats in memory? id then have to live update them.. but its doable.. worth it though?
	char pageStr[8];
	int page = 1, start;
	const int args = trap->Argc();

	if (args != 1 && args != 2) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /clanList <page (optional)>\n\"");
		return;
	}

	if (args == 2) {
		trap->Argv(1, pageStr, sizeof(pageStr));
		page = atoi(pageStr);

		if (page < 1 || page > 100) {
			trap->SendServerCommand(ent-g_entities, "print \"Usage: /clanList <page (optional)>\n\"");
			return;
		}
	}

	start = (page - 1) * 10;

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s, row = 1;
		char msg[1024-128] = {0}, teamname[16] = {0};

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));

		sql = "SELECT T.name AS name, TA.count AS count, T.flags AS flags FROM "
				"(SELECT name, flags From LocalTeam) AS T "
				"INNER JOIN "
				"(SELECT team, COUNT(*) AS count From LocalTeamAccount WHERE (flags & 2 != 2) GROUP BY team) AS TA "
				"ON T.name = TA.team ORDER BY count DESC LIMIT ?, 10"; //Order by score - OH BOY! Or by member count?
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_int (stmt, 1, start));

		trap->SendServerCommand(ent-g_entities, "print \"Clanlist:\n    ^5Name               Members\n\"");
	
		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char *tmpMsg = NULL;

				Q_strncpyz(teamname, (char*)sqlite3_column_text(stmt, 0), sizeof(teamname));

				tmpMsg = va("^5%2i^3: ^3%-18s %i\n", start+row, teamname, sqlite3_column_int(stmt, 1)); //Use flags eventually to highlight leader?
				if (strlen(msg) + strlen(tmpMsg) >= sizeof( msg)) {
					trap->SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
					msg[0] = '\0';
				}
				Q_strcat(msg, sizeof(msg), tmpMsg);
				row++;
			}
			else if (s == SQLITE_DONE) {
				trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));
				break;
			}
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_TeamList_f)", s);
				break;
			}
		}

		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
	}
}

void Cmd_InviteTeam_f( gentity_t *ent ) {
	char username[16], teamname[16], invitee[16];
		
	if (trap->Argc() != 3) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /clanInvite <clan> <player>\n\"");
		return;
	}

	if (!Q_stricmp(ent->client->pers.userName, "")) {
		trap->SendServerCommand(ent-g_entities, "print \"You must be logged in to use this command.\n\"");
		return;
	}

	Q_strncpyz(username, ent->client->pers.userName, sizeof(username));
	trap->Argv(1, teamname, sizeof(teamname));
	trap->Argv(2, invitee, sizeof(invitee));

	Q_strlwr(teamname);
	Q_CleanStr(teamname);
	Q_strstrip(teamname, " \n\r;:.?*<>!#$&'()+@=`~{}[]^_|\\/\"", NULL);

	Q_strlwr(invitee);
	Q_CleanStr(invitee);
	Q_strstrip(invitee, " \n\r;:.?*<>!#$&'()+@=`~{}[]^_|\\/\"", NULL);

	//Confirm clan exists
	//Confirm it is private
	//Confirm im clan leader
	//Make sure they are not already in the clan
	//Create invite

	if (!CheckUserExists(invitee)) {
		trap->Print( "User does not exist!\n");
		return;
	}

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s;//, row = 0;
		//qboolean inviteOnly = qfalse;

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));

		sql = "SELECT flags FROM LocalTeam WHERE name = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
	
		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			int flags = sqlite3_column_int(stmt, 0);
			if (!(flags & JAPRO_TEAMFLAG_PRIVATE)) {
				trap->SendServerCommand(ent-g_entities, "print \"This clan is public!\n\""); //You already own a team
				CALL_SQLITE (finalize(stmt));
				CALL_SQLITE (close(db));
				return;
			}
		}
		else if (s == SQLITE_DONE) {
			trap->SendServerCommand(ent-g_entities, "print \"This clan does not exist!\n\""); //You already own a team
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
		else {
			G_ErrorPrint("ERROR: SQL Select Failed (Cmd_JoinTeam_f 1)", s);
		}
		CALL_SQLITE (finalize(stmt));

		sql = "SELECT flags FROM LocalTeamAccount WHERE team = ? AND account = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));

		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			int flags = sqlite3_column_int(stmt, 0);
			if (!(flags & JAPRO_ACCOUNTTEAMFLAG_OWNER)) {
				trap->SendServerCommand(ent-g_entities, "print \"You are not the clan leader!\n\"");//no permission
				CALL_SQLITE (finalize(stmt));
				CALL_SQLITE (close(db));
				return;
			}
		}
		else if (s == SQLITE_DONE) {
			trap->SendServerCommand(ent-g_entities, "print \"You are not the clan leader!\n\"");//not even in the clan - lmao
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
		else {
			G_ErrorPrint("ERROR: SQL Select Failed (Cmd_InviteTeam_f 2)", s);
		}
		CALL_SQLITE (finalize(stmt));


		sql = "SELECT COUNT(*) FROM LocalTeamAccount WHERE team = ? AND account = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 2, invitee, -1, SQLITE_STATIC));

		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			int count = sqlite3_column_int(stmt, 0);
			if (count > 0) {
				trap->SendServerCommand(ent-g_entities, "print \"Recepient is already in the clan!\n\"");
				CALL_SQLITE (finalize(stmt));
				CALL_SQLITE (close(db));
				return;
			}
		}
		else if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Select Failed (Cmd_InviteTeam_f 3)", s);
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
		CALL_SQLITE (finalize(stmt));

		sql = "INSERT INTO LocalTeamAccount (team, account, flags) VALUES (?, ?, ?)";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 2, invitee, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 3, JAPRO_ACCOUNTTEAMFLAG_PENDING));
		s = sqlite3_step(stmt);

		if (s == SQLITE_DONE) {
			trap->SendServerCommand(ent-g_entities, "print \"Invite sent.\n\"");
		}
		else
			G_ErrorPrint("ERROR: SQL Insert Failed (Cmd_InviteTeam_f 4)", s);

		CALL_SQLITE (finalize(stmt));

		CALL_SQLITE (close(db));
	}

}

void Cmd_AdminTeam_f( gentity_t *ent ) {
	char command[16], username[16], teamname[16];
	//Get sub command
		//kick
		//private
		//longname
		//tags
	//Do action

	const int args = trap->Argc();
	if (args < 3 || args > 4) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /clanAdmin <clan> <kick/private/public/longname/tag> <text (optional)>\n\"");
		return; 
	}

	if (!ent->client->pers.userName[0]) {
		trap->SendServerCommand(ent-g_entities, "print \"You must be logged in to use this command.\n\"");
		return;
	}
	Q_strncpyz(username, ent->client->pers.userName, sizeof(username));

	trap->Argv(1, teamname, sizeof(teamname));
	Q_strlwr(teamname);
	Q_CleanStr(teamname);

	trap->Argv(2, command, sizeof(command));
	Q_strlwr(command);
	Q_CleanStr(command);


	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s;

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));

		sql = "SELECT flags FROM LocalTeamAccount WHERE team = ? AND account = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));

		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			int flags = sqlite3_column_int(stmt, 0);
			if (!(flags & JAPRO_ACCOUNTTEAMFLAG_OWNER)) {
				trap->SendServerCommand(ent-g_entities, "print \"You are not the clan leader!\n\"");//no permission
				CALL_SQLITE (finalize(stmt));
				CALL_SQLITE (close(db));
				return;
			}
		}
		else if (s == SQLITE_DONE) {
			trap->SendServerCommand(ent-g_entities, "print \"You are not the clan leader!\n\"");//not even in the clan - lmao
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
		else {
			G_ErrorPrint("ERROR: SQL Select Failed (Cmd_AdminTeam_f 1)", s);
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}
		CALL_SQLITE (finalize(stmt));

		if (!Q_stricmp(command, "kick")) {
			char player[16];

			if (args < 4) {
				trap->SendServerCommand(ent-g_entities, "print \"Usage: /clanAdmin <clan> <kick/private/public/longname/tag> <text (optional)>\n\"");
				CALL_SQLITE (close(db));
				return;
			}

			trap->Argv(3, player, sizeof(player));
			Q_strlwr(player);
			Q_CleanStr(player);
			Q_strstrip(player, "\n\r", NULL);

			sql = "DELETE FROM LocalTeamAccount WHERE team = ? AND account = ?";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
			CALL_SQLITE (bind_text (stmt, 2, player, -1, SQLITE_STATIC));
			s = sqlite3_step(stmt);
			if (s == SQLITE_DONE)
				trap->SendServerCommand(ent-g_entities, "print \"Player removed.\n\"");//eh, maybe check if they were even in the team b4 printing this
			else 
				G_ErrorPrint("ERROR: SQL Delete Failed (Cmd_AdminTeam_f 2)", s);
			CALL_SQLITE (finalize(stmt));

		}
		else if (!Q_stricmp(command, "private")) {
			sql = "UPDATE LocalTeam SET flags = ? WHERE name = ?";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_int (stmt, 1, JAPRO_TEAMFLAG_PRIVATE));
			CALL_SQLITE (bind_text (stmt, 2, teamname, -1, SQLITE_STATIC));
			s = sqlite3_step(stmt);
			if (s == SQLITE_DONE)
				trap->SendServerCommand(ent-g_entities, "print \"Clan made private.\n\"");//eh, maybe check if they were even in the team b4 printing this
			else 
				G_ErrorPrint("ERROR: SQL Delete Failed (Cmd_AdminTeam_f 3)", s);
			CALL_SQLITE (finalize(stmt));
		}
		else if (!Q_stricmp(command, "public")) {
			sql = "UPDATE LocalTeam SET flags = 0 WHERE name = ?";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_text (stmt, 1, teamname, -1, SQLITE_STATIC));
			s = sqlite3_step(stmt);
			if (s == SQLITE_DONE)
				trap->SendServerCommand(ent-g_entities, "print \"Clan made public.\n\"");//eh, maybe check if they were even in the team b4 printing this
			else 
				G_ErrorPrint("ERROR: SQL Delete Failed (Cmd_AdminTeam_f 4)", s);
			CALL_SQLITE (finalize(stmt));
		}
		else if (!Q_stricmp(command, "longname")) {
			char longname[24];

			if (args < 4) {
				trap->SendServerCommand(ent-g_entities, "print \"Usage: /clanAdmin <clan> <kick/private/public/longname/tag> <text (optional)>\n\"");
				CALL_SQLITE (close(db));
				return;
			}

			trap->Argv(3, longname, sizeof(longname));
			//Q_strlwr(longname);
			//Q_CleanStr(longname);
			//Q_strstrip(username, " \n\r;:.?*<>!#$&'()+@=`~{}[]^_|\\/\"", NULL);
			Q_strstrip(longname, "\n\r", NULL);

			sql = "UPDATE LocalTeam SET longname = ? WHERE name = ?";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_text (stmt, 1, longname, -1, SQLITE_STATIC));
			CALL_SQLITE (bind_text (stmt, 2, teamname, -1, SQLITE_STATIC));
			s = sqlite3_step(stmt);
			if (s == SQLITE_DONE)
				trap->SendServerCommand(ent-g_entities, "print \"Longname set.\n\"");//eh, maybe check if they were even in the team b4 printing this
			else 
				G_ErrorPrint("ERROR: SQL Delete Failed (Cmd_AdminTeam_f 5)", s);
			CALL_SQLITE (finalize(stmt));
		}
		else if (!Q_stricmp(command, "tag")) {
			char tags[16];

			if (args < 4) {
				trap->SendServerCommand(ent-g_entities, "print \"Usage: /clanAdmin <clan> <kick/private/public/longname/tag> <text (optional)>\n\"");
				CALL_SQLITE (close(db));
				return;
			}

			trap->Argv(3, tags, sizeof(tags));
			//Q_strlwr(tags);
			//Q_CleanStr(tags);
			Q_strstrip(tags, "\n\r", NULL);

			sql = "UPDATE LocalTeam SET tag = ? WHERE name = ?";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_text (stmt, 1, tags, -1, SQLITE_STATIC));
			CALL_SQLITE (bind_text (stmt, 2, teamname, -1, SQLITE_STATIC));
			s = sqlite3_step(stmt);
			if (s == SQLITE_DONE)
				trap->SendServerCommand(ent-g_entities, "print \"Tag set.\n\"");//eh, maybe check if they were even in the team b4 printing this
			else 
				G_ErrorPrint("ERROR: SQL Delete Failed (Cmd_AdminTeam_f 6)", s);
			CALL_SQLITE (finalize(stmt));
		}
		else {
			trap->SendServerCommand(ent-g_entities, "print \"Usage: /clanAdmin <clan> <kick/private/public/longname/tag> <text (optional)>\n\"");
		}

		CALL_SQLITE (close(db));

	}
}

int StatToInteger(char *type) {
	Q_strlwr(type);
	Q_CleanStr(type);

	//Todo - Do this dynamically

	if (!Q_stricmp(type, "race"))
		return 1;
	if (!Q_stricmp(type, "combat"))
		return 2;
	//if (!Q_stricmp(type, "force"))
	//return 3;
	return -1;
}

void Cmd_AccountStats_f(gentity_t *ent) { //Should i bother to cache player stats in memory? id then have to live update them.. but its doable.. worth it though?
	char inputString[40], username[16];
	const int args = trap->Argc();
	int page = -1, start, type = -1, input, i;

	if (args > 4 || args == 1) {
		trap->SendServerCommand(ent - g_entities, "print \"Usage: /stats <username> <type (optional - example: race/combat)> <page (optional)>\n\"");
		return;
	}

	//Make 1st arg be username I guess.
	trap->Argv(1, username, sizeof(username));
	Q_strlwr(username);
	Q_CleanStr(username);

	for (i = 2; i < args; i++) {
		trap->Argv(i, inputString, sizeof(inputString));
		if (type == -1) {
			input = StatToInteger(inputString);
			if (input != -1) {
				type = input;
				continue;
			}
		}
		if (page == -1) {
			input = atoi(inputString);
			if (input > 0) {
				page = input;
				continue;
			}
		}	
	}

	if (type == -1)	//If no type specified, default to... saber? or generic stats?
		type = 1; //Default to race
	if (page < 1 || page > 100)
		page = 1;

	start = (page - 1) * 10;

	//List their master if they have one
	//List their padawans if they have any

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		char timeStr[64] = { 0 }, dateStr[64] = { 0 }, master[16] = { 0 }, padawans[512] = { 0 }; //idk
		int s, created = 0;
		qboolean printInfo = qfalse;

		CALL_SQLITE(open(LOCAL_DB_PATH, &db));

		sql = "SELECT created, master FROM LocalAccount WHERE username = ? "
			"UNION ALL SELECT username, NULL  from LocalAccount WHERE master = ? LIMIT 32";
		CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
		CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_text(stmt, 2, username, -1, SQLITE_STATIC));

		s = sqlite3_step(stmt);
		if (s == SQLITE_ROW) {
			created = sqlite3_column_int(stmt, 0);
			if ((char*)sqlite3_column_text(stmt, 1))
				Q_strncpyz(master, (char*)sqlite3_column_text(stmt, 1), sizeof(master));
		}
		else if (s == SQLITE_DONE) {
			trap->SendServerCommand(ent - g_entities, "print \"Account not found!\n\"");
			CALL_SQLITE(finalize(stmt));
			CALL_SQLITE(close(db));
			return;
		}
		else if (s == SQLITE_ERROR) {
			G_ErrorPrint("ERROR: SQL Select Failed (Cmd_Stats_f 1)", s);
			CALL_SQLITE(finalize(stmt));
			CALL_SQLITE(close(db));
			return;
		}

		while (1) { //Get padawans
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				Q_strcat(padawans, sizeof(padawans), va("%s ", (char*)sqlite3_column_text(stmt, 0)));
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_Stats_f 1)", s);
				break;
			}
		}
		CALL_SQLITE(finalize(stmt));

		{
			char msg[1024-128] = { 0 };

			if (created > 1) { //Sad hack since some accounts were made before this was recorded
				getDateTime(created, timeStr, sizeof(timeStr));
				Q_strcat(msg, sizeof(msg), va("   ^5Created: ^2%s\n", timeStr));
				printInfo = qtrue;
			}
			if (Q_stricmp(master, "")) {
				Q_strcat(msg, sizeof(msg), va("   ^5Master: ^2%s\n", master));
				printInfo = qtrue;
			}
			if (Q_stricmp(padawans, "")) {
				//Trim end ", " of padawans
				//char *p = NULL;
				//*p = padawans;
				//p[strlen(p) - 2] = 0;
				Q_strcat(msg, sizeof(msg), va("   ^5Padawans: ^2%s\n", padawans));
				printInfo = qtrue;
			}

			if (printInfo) {
				trap->SendServerCommand(ent - g_entities, va("print \"%s\"", msg));
			}
		}

		if (type == 1)
		{
			char styleStr[16] = { 0 }, rankStr[8];
			char msg[1024 - 128] = { 0 };
			int s;
			int row = 1;

			//Race stats
			sql = "SELECT SUM(entries-rank) AS newscore, CAST(SUM(entries/CAST(rank AS FLOAT)) AS INT) AS oldscore, AVG(rank) as rank, AVG((entries - CAST(rank-1 AS float))/entries) AS percentile, SUM(CASE WHEN rank == 1 THEN 1 ELSE 0 END) AS golds, SUM(CASE WHEN rank == 2 THEN 1 ELSE 0 END) AS silvers, SUM(CASE WHEN rank == 3 THEN 1 ELSE 0 END) AS bronzes, COUNT(*) as count FROM LocalRun "
				"WHERE rank != 0 AND style != 14 AND username = ?";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));

			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char raceStats[256] = { 0 };
				int newscore = sqlite3_column_int(stmt, 0);
				int oldscore = sqlite3_column_int(stmt, 1);
				int score = (int)(((newscore + oldscore)*0.5f)+0.5f);

				Q_strncpyz(raceStats, "Race Stats:\n    ^5Score    SPR    Avg. Rank    Percentile    Golds    Silvers    Bronzes    Count\n", sizeof(raceStats));
				Q_strcat(raceStats, sizeof(raceStats), va("    ^3%-8i ^3%-6.2f ^3%-12.2f ^3%-13.2f ^3%-8i ^3%-10i ^3%-10i %i\n", score, sqlite3_column_int(stmt, 7) ? oldscore / (float)sqlite3_column_int(stmt, 7) : 0.0f, sqlite3_column_double(stmt, 2), sqlite3_column_double(stmt, 3), sqlite3_column_int(stmt, 4), sqlite3_column_int(stmt, 5), sqlite3_column_int(stmt, 6), sqlite3_column_int(stmt, 7)));

				trap->SendServerCommand(ent - g_entities, va("print \"%s\"", raceStats));
			}
			else if (s != SQLITE_DONE) {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_Stats_f 2)", s);
			}
			CALL_SQLITE(finalize(stmt));

			//Recent races
			sql = "SELECT coursename, style, rank, season_rank, duration_ms, end_time FROM LocalRun WHERE username = ? ORDER BY end_time DESC LIMIT ?, 10";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_int(stmt, 2, start));

			trap->SendServerCommand(ent - g_entities, "print \"Recent Races:\n    ^5Course                      Style      Rank    Time         Date\n\""); //Color rank yellow for global, normal for season -fixme match race print scheme
			while (1) {
				s = sqlite3_step(stmt);
				if (s == SQLITE_ROW) {
					char *tmpMsg = NULL;
					IntegerToRaceName(sqlite3_column_int(stmt, 1), styleStr, sizeof(styleStr));
					TimeToString(sqlite3_column_int(stmt, 4), timeStr, sizeof(timeStr));
					getDateTime(sqlite3_column_int(stmt, 5), dateStr, sizeof(dateStr));

					//If rank == 0, put "Season rank: season_rank".  Else put "Rank: rank"
					if (sqlite3_column_int(stmt, 2))
						Com_sprintf(rankStr, sizeof(rankStr), "^2%i^7", sqlite3_column_int(stmt, 2));
					else
						Com_sprintf(rankStr, sizeof(rankStr), "^3%i^7", sqlite3_column_int(stmt, 3));

					tmpMsg = va("^5%2i^3: ^3%-27s ^3%-10s ^3%-11s ^3%-12s %s\n", row + start, sqlite3_column_text(stmt, 0), styleStr, rankStr, timeStr, dateStr);
					if (strlen(msg) + strlen(tmpMsg) >= sizeof(msg)) {
						trap->SendServerCommand(ent - g_entities, va("print \"%s\"", msg));
						msg[0] = '\0';
					}
					Q_strcat(msg, sizeof(msg), tmpMsg);
					row++;
				}
				else if (s == SQLITE_DONE)
					break;
				else {
					G_ErrorPrint("ERROR: SQL Select Failed (Cmd_Stats_f 2)", s);
					break;
				}
			}
			trap->SendServerCommand(ent - g_entities, va("print \"%s\"", msg));

			CALL_SQLITE(finalize(stmt));

		}
		else if (type == 2)//All Combat..? break down per style? gametype?
		{
			char opponent[16], result[16], type[16];
			char msg[1024 - 128] = { 0 };
			int s;
			int row = 1;

#if 0
			//Combat stats
			sql = "SELECT D1.username, elo, 100-ROUND(100*(win_ts + loss_ts)/(win_count+loss_count), 0) AS TS, win_count+loss_count AS count "
				"FROM ((SELECT username, type, elo FROM ((SELECT winner AS username, type, ROUND(winner_elo,0) AS elo, end_time FROM LocalDuel WHERE type = ? "
				"UNION ALL SELECT loser AS username, type, ROUND(loser_elo,0) AS elo, end_time FROM LocalDuel WHERE type = ? ORDER BY end_time ASC)) GROUP BY username ORDER BY elo DESC) AS D1 "
				"INNER JOIN (SELECT winner AS username2, COUNT(*) AS win_count, SUM(odds) AS win_ts FROM LocalDuel WHERE type = ? GROUP BY username2) AS D2 "
				"ON D1.username = D2.username2) "
				"INNER JOIN (SELECT loser AS username3, COUNT(*) AS loss_count, SUM(1-odds) AS loss_ts FROM LocalDuel WHERE type = ? GROUP BY username3) AS D3 "
				"ON D1.username = D3.username3 ORDER BY elo desc LIMIT ?, 10";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));

			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char raceStats[256] = { 0 };
				int newscore = sqlite3_column_int(stmt, 0);
				int oldscore = sqlite3_column_int(stmt, 1);
				int score = (int)(((newscore + oldscore)*0.5f) + 0.5f);

				Q_strncpyz(raceStats, "Race Stats:\n    ^5Score    SPR    Avg. Rank    Percentile    Golds    Silvers    Bronzes    Count\n", sizeof(raceStats));
				Q_strcat(raceStats, sizeof(raceStats), va("    ^3%-8i ^3%-6.2f ^3%-12.2f ^3%-13.2f ^3%-8i ^3%-10i ^3%-10i %i\n", score, oldscore / (float)sqlite3_column_int(stmt, 7), sqlite3_column_double(stmt, 2), sqlite3_column_double(stmt, 3), sqlite3_column_int(stmt, 4), sqlite3_column_int(stmt, 5), sqlite3_column_int(stmt, 6), sqlite3_column_int(stmt, 7)));

				trap->SendServerCommand(ent - g_entities, va("print \"%s\"", raceStats));
			}
			else if (s != SQLITE_DONE) {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_Stats_f 2)", s);
			}
			CALL_SQLITE(finalize(stmt));
#endif

			//Recent duels
			sql = "SELECT winner, loser, type, end_time FROM LocalDuel WHERE winner = ? OR loser = ? ORDER BY end_time DESC LIMIT ?, 10";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_text(stmt, 2, username, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_int(stmt, 3, start));

			trap->SendServerCommand(ent - g_entities, "print \"Recent Duels:\n    ^5Opponent         Result   Type        Date\n\"");
			while (1) {
				s = sqlite3_step(stmt);
				if (s == SQLITE_ROW) {
					char *tmpMsg = NULL;
					IntegerToDuelType(sqlite3_column_int(stmt, 2), type, sizeof(type));
					getDateTime(sqlite3_column_int(stmt, 3), dateStr, sizeof(dateStr));

					if (!Q_stricmp((char*)sqlite3_column_text(stmt, 0), username)) { //They are winner
						Com_sprintf(opponent, sizeof(opponent), "^3%s^7", (char*)sqlite3_column_text(stmt, 1));
						Com_sprintf(result, sizeof(result), "^2Win^7");
					}
					else {
						Com_sprintf(opponent, sizeof(opponent), "^3%s^7", (char*)sqlite3_column_text(stmt, 0));
						Com_sprintf(result, sizeof(result), "^1Loss^7");
					}

					tmpMsg = va("^5%2i^3: ^3%-20s ^3%-12s ^3%-11s %s\n", row + start, opponent, result, type, dateStr);
					if (strlen(msg) + strlen(tmpMsg) >= sizeof(msg)) {
						trap->SendServerCommand(ent - g_entities, va("print \"%s\"", msg));
						msg[0] = '\0';
					}
					Q_strcat(msg, sizeof(msg), tmpMsg);
					row++;
				}
				else if (s == SQLITE_DONE)
					break;
				else {
					G_ErrorPrint("ERROR: SQL Select Failed (Cmd_Stats_f 3)", s);
					break;
				}
			}
			trap->SendServerCommand(ent - g_entities, va("print \"%s\"", msg));

			CALL_SQLITE(finalize(stmt));
		}

		CALL_SQLITE(close(db));
	}
	//DebugWriteToDB("Cmd_AccountStats_f");
}

//Search array list to find players row
//If found, update it
//If not found, add a new row at next empty spot
//A new function will read the array on mapchange, and do the querys updates
void G_AddSimpleStat(gentity_t *self, gentity_t *other, int type) {
//Useless feature
#if _STATLOG
	int row;
	char userName[16];

	if (sv_cheats.integer) //Dont record stats if cheats were enabled
		return;
	if (!self)
		return;
	if (!other)
		return;
	if (!self->client)
		return;
	if (!other->client)
		return;
	if (!self->client->pers.userName[0])
		return;
	if (!other->client->pers.userName[0])
		return;
	if (self->client->sess.raceMode)
		return;
	if (other->client->sess.raceMode) //EH?
		return;

	Q_strncpyz(userName, self->client->pers.userName, sizeof(userName));

	for (row = 0; row < 256; row++) { //size of UserStats ?
		if (!UserStats[row].username || !UserStats[row].username[0])
			break;
		if (!Q_stricmp(UserStats[row].username, userName)) { //User found, update his stats in memory, is this check right?
			if (type == 1) //Kills
				UserStats[row].kills++;
			else if (type == 2) //Deaths
				UserStats[row].deaths++;
			else if (type == 3) //Suicides
				UserStats[row].suicides++;
			else if (type == 4) //Captures
				UserStats[row].captures++;
			else if (type == 5) //Returns
				UserStats[row].returns++;
			return;
		}
	}
	Q_strncpyz(UserStats[row].username, userName, sizeof(UserStats[row].username )); //If we are here it means name not found, so add it
	UserStats[row].kills = UserStats[row].deaths = UserStats[row].suicides = UserStats[row].captures = UserStats[row].returns = 0; //I guess set all their shit to 0
	//Add the one type ..
	if (type == 1) //Kills
		UserStats[row].kills++;
	else if (type == 2) //Deaths
		UserStats[row].deaths++;
	else if (type == 3) //Suicides
		UserStats[row].suicides++;
	else if (type == 4) //Captures
		UserStats[row].captures++;
	else if (type == 5) //Returns
		UserStats[row].returns++;

#endif
}

void G_AddSimpleStatsToFile() { //For each item in array.. do an update query?  Called on shutdown game.
	//Useless feature
#if _STATLOG
	//fileHandle_t	f;	
	char	buf[8 * 4096] = {0};
	int		row;

	Q_strncpyz(buf, "", sizeof(buf));

	for (row = 0; row < 256; row++) { //size of UserStats ?

		if (!UserStats[row].username || !UserStats[row].username[0])
			break;

		Q_strcat(buf, sizeof(buf), va("%s;%i;%i;%i;%i;%i\n", UserStats[row].username, UserStats[row].kills, UserStats[row].deaths, UserStats[row].suicides, UserStats[row].captures, UserStats[row].returns));
	}

	trap->FS_Write( buf, strlen( buf ), level.tempStatLog );
	trap->Print("Adding stats to file: %s\n", TEMP_STAT_LOG);

#endif
}

void G_AddSimpleStatsToDB() {
//Useless feature
#if _STATLOG
	fileHandle_t f;	
	int		fLen = 0, args = 1, s; //MAX_FILESIZE = 4096
	char	buf[8 * 1024] = {0}, empty[8] = {0};//eh
	char*	pch;
	sqlite3 * db;
	char * sql;
	sqlite3_stmt * stmt;
	UserStats_t	TempUserStats;
	qboolean good = qfalse;

	fLen = trap->FS_Open(TEMP_STAT_LOG, &f, FS_READ);

	if (!f) {
		trap->Print("ERROR: Couldn't load stat data from %s\n", TEMP_STAT_LOG);
		return;
	}
	if (fLen >= 8*1024) {
		trap->FS_Close(f);
		trap->Print("ERROR: Couldn't load stat data from %s, file is too large\n", TEMP_STAT_LOG);
		return;
	}

	trap->FS_Read(buf, fLen, f);
	buf[fLen] = 0;
	trap->FS_Close(f);
	
	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
	sql = "UPDATE LocalAccount SET "
		"kills = kills + ?, deaths = deaths + ?, suicides = suicides + ?, captures = captures + ?, returns = returns + ? "
		"WHERE username = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));

	//Todo: make TempRaceRecord an array of structs instead, maybe like 32 long idk, and build a query to insert 32 at a time or something.. instead of 1 by 1
	pch = strtok (buf,";\n");
	while (pch != NULL)
	{
		if ((args % 6) == 1)
			Q_strncpyz(TempUserStats.username, pch, sizeof(TempUserStats.username));
		else if ((args % 6) == 2)
			TempUserStats.kills = atoi(pch);
		else if ((args % 6) == 3)
			TempUserStats.deaths = atoi(pch);
		else if ((args % 6) == 4)
			TempUserStats.suicides = atoi(pch);
		else if ((args % 6) == 5)
			TempUserStats.captures = atoi(pch);
		else if ((args % 6) == 0) {
			TempUserStats.returns = atoi(pch);
			//trap->Print("Inserting stat into db: %s, %i, %i, %i, %i, %i\n", TempUserStats.username, TempUserStats.kills, TempUserStats.deaths, TempUserStats.suicides, TempUserStats.captures, TempUserStats.returns);
			CALL_SQLITE (bind_int (stmt, 1, TempUserStats.kills));
			CALL_SQLITE (bind_int (stmt, 2, TempUserStats.deaths));
			CALL_SQLITE (bind_int (stmt, 3, TempUserStats.suicides));
			CALL_SQLITE (bind_int (stmt, 4, TempUserStats.captures));
			CALL_SQLITE (bind_int (stmt, 5, TempUserStats.returns));
			CALL_SQLITE (bind_text (stmt, 6, TempUserStats.username, -1, SQLITE_STATIC));
			CALL_SQLITE_EXPECT (step (stmt), DONE);
			CALL_SQLITE (reset (stmt));
			CALL_SQLITE (clear_bindings (stmt));
		}
    	pch = strtok (NULL, ";\n");
		args++;
	}

	s = sqlite3_step(stmt); //this duplicates last one..?
	if (s == SQLITE_DONE)
		good = qtrue;
	CALL_SQLITE (finalize(stmt));
	CALL_SQLITE (close(db));	

	if (good) { //dont delete tmp file if mysql database is not responding 
		trap->FS_Open(TEMP_STAT_LOG, &f, FS_WRITE); 
		trap->FS_Write( empty, strlen( empty ), level.tempStatLog );
		trap->FS_Close(f);
		trap->Print("Loaded previous map stats from %s.\n", TEMP_STAT_LOG);
	}
	else 
		trap->Print("ERROR: Unable to insert previous map stats into database.\n");

	//DebugWriteToDB("G_AddSimpleStatToDB");
#endif
}

#if 0
void G_AddSimpleStatsToDB2() { //For each item in array.. do an update query?  Called on shutdown game.
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	int row = 0;

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
	sql = "UPDATE LocalAccount SET "
		"kills = kills + ?, deaths = deaths + ?, suicides = suicides + ?, captures = captures + ?, returns = returns + ? "
		"WHERE username = ?";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));

	for (row = 0; row < 256; row++) { //size of UserStats ?
		if (!UserStats[row].username || !UserStats[row].username[0])
			break;

		CALL_SQLITE (bind_int (stmt, 1, UserStats[row].kills));
		CALL_SQLITE (bind_int (stmt, 2, UserStats[row].deaths));
		CALL_SQLITE (bind_int (stmt, 3, UserStats[row].suicides));
		CALL_SQLITE (bind_int (stmt, 4, UserStats[row].captures));
		CALL_SQLITE (bind_int (stmt, 5, UserStats[row].returns));
		CALL_SQLITE (bind_text (stmt, 6, UserStats[row].username, -1, SQLITE_STATIC));
		CALL_SQLITE_EXPECT (step (stmt), DONE);
		CALL_SQLITE (reset (stmt));
		CALL_SQLITE (clear_bindings (stmt));
	}

	CALL_SQLITE (finalize(stmt));
	CALL_SQLITE (close(db));
}
#endif

#if 0
void BuildMapHighscores() { //loda fixme, take prepare,query out of loop
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	int i, mstyle;
	char mapName[40], courseName[40], info[1024] = {0}, dateStr[64] = {0};

	trap->GetServerinfo(info, sizeof(info));
	Q_strncpyz(mapName, Info_ValueForKey( info, "mapname" ), sizeof(mapName));
	Q_strlwr(mapName);
	Q_CleanStr(mapName);

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));

	for (i = 0; i < level.numCourses; i++) { //32 max
		Q_strncpyz(courseName, mapName, sizeof(courseName));
		if (level.courseName[i][0])
			Q_strcat(courseName, sizeof(courseName), va(" (%s)", level.courseName[i]));
		for (mstyle = 0; mstyle < MV_NUMSTYLES; mstyle++) { //9 movement styles. 0-8
			int rank = 0;

			sql = "SELECT LR.id, LR.username, LR.coursename, LR.duration_ms, LR.topspeed, LR.average, LR.style, LR.end_time "  //Place 1
				"FROM (SELECT id, MIN(duration_ms) "
				   "FROM LocalRun "
				   "WHERE coursename = ? AND style = ? "
				   "GROUP by username) " 
				"AS X INNER JOIN LocalRun AS LR ON LR.id = X.id ORDER BY duration_ms, end_time LIMIT 10"; //end_time so in case of tie, first one shows up first?

			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_text (stmt, 1, courseName, -1, SQLITE_STATIC));
			CALL_SQLITE (bind_int (stmt, 2, mstyle));

			while (1) {
				int s;
				s = sqlite3_step (stmt);
				if (s == SQLITE_ROW) {
					char *username; //loda fixme should this be char[]
					char *course;
					unsigned int duration_ms;
					unsigned short topspeed, average;
					unsigned short style;
					unsigned int end_time;
					// int garbage;

					// garbage = sqlite3_column_int(stmt, 0); //cn i just delete this, it seemed to throw off the order..??
					username = (char*)sqlite3_column_text(stmt, 1); 
					course = (char*)sqlite3_column_text(stmt, 2); //again, not needed
					duration_ms = sqlite3_column_int(stmt, 3);
					topspeed = sqlite3_column_int(stmt, 4);
					average = sqlite3_column_int(stmt, 5);
					style = sqlite3_column_int(stmt, 6);
					end_time = sqlite3_column_int(stmt, 7);

					Q_strncpyz(HighScores[i][style][rank].username, username, sizeof(HighScores[i][style][rank].username));
					Q_strncpyz(HighScores[i][style][rank].coursename, course, sizeof(HighScores[i][style][rank].coursename));
					HighScores[i][style][rank].duration_ms = duration_ms;
					HighScores[i][style][rank].topspeed = topspeed;
					HighScores[i][style][rank].average = average;
					HighScores[i][style][rank].style = style;

					getDateTime(end_time, dateStr, sizeof(dateStr));
					Q_strncpyz(HighScores[i][style][rank].end_time, dateStr, sizeof(HighScores[i][style][rank].end_time));
					rank++;
				}
				else if (s == SQLITE_DONE)
					break;
				else {
					fprintf (stderr, "ERROR: SQL Select Failed.\n");//trap print?
					break;
				}
			}
			CALL_SQLITE (finalize(stmt));
		}
	}
	CALL_SQLITE (close(db));

	if (level.numCourses)
		trap->Print("Highscores built for %s\n", mapName);

	//DebugWriteToDB("BuildMapHighscores");
}
#endif

int RaceNameToInteger(char *style) {
	Q_strlwr(style);
	Q_CleanStr(style);

	if (!Q_stricmp(style, "siege"))
		return 0;
	if (!Q_stricmp(style, "jka") || !Q_stricmp(style, "jk3"))
		return 1;
	if (!Q_stricmp(style, "hl2") || !Q_stricmp(style, "hl1") || !Q_stricmp(style, "hl") || !Q_stricmp(style, "qw"))
		return 2;
	if (!Q_stricmp(style, "cpm") || !Q_stricmp(style, "cpma"))
		return 3;
	if (!Q_stricmp(style, "q3"))
		return 4;
	if (!Q_stricmp(style, "pjk"))
		return 5;
	if (!Q_stricmp(style, "wsw") || !Q_stricmp(style, "warsow"))
		return 6;
	if (!Q_stricmp(style, "rjq3"))
		return 7;
	if (!Q_stricmp(style, "rjcpm"))
		return 8;
	if (!Q_stricmp(style, "swoop"))
		return 9;
	if (!Q_stricmp(style, "jetpack") || !Q_stricmp(style, "jet") || !Q_stricmp(style, "detpack"))
		return 10;
	if (!Q_stricmp(style, "speed") || !Q_stricmp(style, "ctf"))
		return 11;
#if _SPPHYSICS
	if (!Q_stricmp(style, "sp") || !Q_stricmp(style, "singleplayer"))
		return 12;
#endif
	if (!Q_stricmp(style, "slick"))
		return 13;
	if (!Q_stricmp(style, "botcpm"))
		return 14;
#if _COOP
	if (!Q_stricmp(style, "coop"))
		return 15;
#endif
	if (!Q_stricmp(style, "ocpm"))
		return 16;
	if (!Q_stricmp(style, "tribes"))
		return 17;
	if (!Q_stricmp(style, "surf"))
		return 18;
	if (!Q_stricmp(style, "flow"))
		return 19;
	return -1;
}

int SeasonToInteger(char *season) {
	Q_strlwr(season);
	Q_CleanStr(season);

	//Todo - Do this dynamically

	if (!Q_stricmp(season, "s1"))
		return 1;
	if (!Q_stricmp(season, "s2"))
		return 2;
	if (!Q_stricmp(season, "s3"))
		return 3;
	if (!Q_stricmp(season, "s4"))
		return 4;
	if (!Q_stricmp(season, "s5"))
		return 5;
	if (!Q_stricmp(season, "s6"))
		return 6;
	return -1;
}

void remove_all_chars(char* str, char c) {
    char *pr = str, *pw = str;
    while (*pr) {
        *pw = *pr++;
        pw += (*pw != c);
    }
    *pw = '\0';
}

#if 0
void Cmd_PersonalBest_f(gentity_t *ent) { //loda fixme bugged, always finds in cache even if not there
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	int s, style, duration_ms = 0, topspeed = 0, average = 0, i, course = -1;
	char username[16], courseName[40], courseNameFull[40], styleString[16], durationStr[32], tempCourseName[40];

	if (trap->Argc() != 4 && trap->Argc() != 5) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /best <username> <full coursename> <style>.  Example: /best user mapname (coursename) style\n\"");
		return;
	}

	trap->Argv(1, username, sizeof(username));

	Q_strlwr(username);
	Q_CleanStr(username);

	if (trap->Argc() == 5) {
		trap->Argv(2, courseNameFull, sizeof(courseNameFull));
		trap->Argv(3, courseName, sizeof(courseName));
		Q_strcat(courseNameFull, sizeof(courseNameFull), va(" %s", courseName));
		trap->Argv(4, styleString, sizeof(styleString));
	}
	else {
		trap->Argv(2, courseNameFull, sizeof(courseNameFull));
		trap->Argv(3, styleString, sizeof(styleString));
	}

	Q_strlwr(styleString);
	Q_CleanStr(styleString);

	style = RaceNameToInteger(styleString);

	if (style < 0) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /best <username> <full coursename> <style>.\n\"");
		return;
	}

	Q_strlwr(username);
	Q_CleanStr(username);
	Q_strlwr(courseNameFull);
	Q_CleanStr(courseNameFull);

	Q_strncpyz(tempCourseName, courseName, sizeof(tempCourseName));
	remove_all_chars(tempCourseName, '(');
	remove_all_chars(tempCourseName, ')');

	for (i = 0; i < level.numCourses; i++) {
		//trap->Print("course, course : %s, %s\n", tempCourseName, level.courseName[i]);
		if (!Q_stricmp(tempCourseName, level.courseName[i])) {
			course = i;
			break;
		}
	}

	if (level.numCourses == 1)
		course = 0;

	if (course != -1) { //Found course on current map, check highscores cache
		for (i = 0; i < 10; i++) { //Search for highscore in highscore cache table
			//trap->Print("Cycling Highscores %s, %s matching with %s, %s\n", HighScores[course][style][i].username, HighScores[course][style][i].coursename , username, courseNameFull);
			if (!Q_stricmp(username, HighScores[course][style][i].username) && !Q_stricmp(courseNameFull, HighScores[course][style][i].coursename)) { //Its us, and right course
				//trap->Print("Found In Highscore Cache\n");
				duration_ms = HighScores[course][style][i].duration_ms;
				topspeed = HighScores[course][style][i].topspeed;
				average = HighScores[course][style][i].average;
				break;
			}
			if (!HighScores[course][style][i].username[0])
				break;
		}
	}

	if (!duration_ms) { //Search for highscore in personalbest cache table
		for (i = 0; i < 50; i++) {
			//trap->Print("Cycling PB %s, %s matching with %s, %s\n", PersonalBests[style][i].username, PersonalBests[style][i].coursename , username, courseNameFull);
			if (!Q_stricmp(username, PersonalBests[style][i].username) && !Q_stricmp(courseNameFull, PersonalBests[style][i].coursename)) { //Its us, and right course
				//trap->Print("Found In My Cache\n");
				duration_ms = PersonalBests[style][i].duration_ms;
				topspeed = 0;//HighScores[course][style][i].topspeed; //Topspeed and average are not yet stored in personalBest cache, so uh.... make them 0 for now...
				average = 0;//HighScores[course][style][i].average;
				break;
			}
			if (!PersonalBests[style][i].username[0])
				break;
		}
	}

	if (!duration_ms) { //Not found in cache, so check db
		CALL_SQLITE (open (LOCAL_DB_PATH, & db));
		sql = "SELECT MIN(duration_ms), topspeed, average FROM LocalRun WHERE username = ? AND coursename = ? AND style = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 2, courseNameFull, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 3, style));

		s = sqlite3_step(stmt);

		if (s == SQLITE_ROW) {
			duration_ms = sqlite3_column_int(stmt, 0);
			topspeed = sqlite3_column_int(stmt, 1);
			average = sqlite3_column_int(stmt, 2);
		}
		else if (s != SQLITE_DONE) {
			fprintf (stderr, "ERROR: SQL Select Failed.\n");//trap print?
			CALL_SQLITE (finalize(stmt));
			CALL_SQLITE (close(db));
			return;
		}

		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
	}

	if (duration_ms >= 60000) { //FIXME, make this use the inttostring function if it tests bugfree
		int minutes, seconds, milliseconds;
		minutes = (int)((duration_ms / (1000*60)) % 60);
		seconds = (int)(duration_ms / 1000) % 60;
		milliseconds = duration_ms % 1000; 
		Com_sprintf(durationStr, sizeof(durationStr), "%i:%02i.%03i", minutes, seconds, milliseconds);//more precision?
	}
	else
		Q_strncpyz(durationStr, va("%.3f", ((float)duration_ms * 0.001)), sizeof(durationStr));

	if (duration_ms) {
		if (!topspeed && !average)
			trap->SendServerCommand( ent-g_entities, va("print \"^5 This players fastest time is ^3%s^5.\n\"", durationStr)); //whatever, probably wont ever get around to fixing this since it fucks with the caching
		else
			trap->SendServerCommand( ent-g_entities, va("print \"^5 This players fastest time is ^3%s^5 with max ^3%i^5 and average ^3%i^5.\n\"", durationStr, topspeed, average));
	}
	else
		trap->SendServerCommand(ent-g_entities, "print \"^5 No results found.\n\"");

	//DebugWriteToDB("Cmd_PersonalBest_f");
}

void Cmd_NotCompleted_f(gentity_t *ent) {
	int i, style, course;
	char styleString[16] = {0}, username[16] = {0};
	char msg[128] = {0};
	qboolean printed, found;

	if (level.numCourses == 0) {
		trap->SendServerCommand(ent-g_entities, "print \"This map does not have any courses.\n\"");
		return;
	}

	if (trap->Argc() > 2) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /notCompleted <username (optional)>\n\"");
		return;
	}

	if (trap->Argc() == 1) { //notcompleted
		if (!ent->client->pers.userName || !ent->client->pers.userName[0]) {
			trap->SendServerCommand(ent-g_entities, "print \"You must be logged in to use this command.\n\"");
			return;
		}
		Q_strncpyz(username, ent->client->pers.userName, sizeof(username));
	}
	else if (trap->Argc() == 2) { //notcompleted user
		char input[16];
		trap->Argv(1, input, sizeof(input));
		Q_strncpyz(username, input, sizeof(username));
	}

	Q_strlwr(username);
	Q_CleanStr(username);

	if (trap->Argc() == 1)
		trap->SendServerCommand(ent-g_entities, "print \"Courses where you are not in the top 10:\n\"");
	else if (trap->Argc() == 2)
		trap->SendServerCommand(ent-g_entities, va("print \"Courses where %s is not in the top 10:\n\"", username));


	for (course=0; course<level.numCourses; course++) { //For each course
		Q_strncpyz(msg, "", sizeof(msg));
		printed = qfalse;
		for (style = 0; style < MV_NUMSTYLES; style++) { //For each style
			found = qfalse;
			for (i=0; i<10; i++) {
				if (HighScores[course][style][i].username && HighScores[course][style][i].username[0] && !Q_stricmp(HighScores[course][style][i].username, username)) {
					found = qtrue;
					break;
				}
				else if (!HighScores[course][style][i].username || (HighScores[course][style][i].username && !HighScores[course][style][i].username[0])) {
					found = qfalse;
					break;
				}
			}

			if (!found) {
				if (!printed) {
					Q_strcat(msg, sizeof(msg), va("\n^3%-12s", level.courseName[course]));
					printed = qtrue;
				}
				else {
					//Q_strcat(msg, sizeof(msg), ":");
				}
				//Q_strcat(msg, sizeof(msg), va("<%i, %i>", found, printed));
				//Q_strcat(msg, sizeof(msg), "-");
				IntegerToRaceName(style, styleString, sizeof(styleString));
				Q_strcat(msg, sizeof(msg), va(" ^5%-6s", styleString));
			}
			else
			{
				if (printed)
					Q_strcat(msg, sizeof(msg), "       ");
			}
		}
		if (printed) {
			Q_strcat(msg, sizeof(msg), "\n");
			trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));
		}
	}
}
#endif

//
int G_AdminAllowed(gentity_t* ent, unsigned int adminCmd, qboolean cheatAllowed, qboolean raceAllowed, char* cmdName);
void Cmd_InvalidateRace_f(gentity_t* ent)
{
	char username[16], coursename[40], styleStr[16], season[8], mode[8];
	int i, style;

	if (trap->Argc() != 6) {
		trap->SendServerCommand(ent - g_entities, "print \"Usage: /flagRecord <username> <coursename> <style> <season> <mode: f, u, d>. Use * in place of space in the coursename.\n\"");
		//flagRecord kane racearena_pro*(dash1) jka
		return;
	}

	if (!G_AdminAllowed(ent, JAPRO_ACCOUNTFLAG_DATABASE, qfalse, qfalse, "flagRecord"))
		return;

	trap->Argv(1, username, sizeof(username));
	trap->Argv(2, coursename, sizeof(coursename));
	trap->Argv(3, styleStr, sizeof(styleStr));
	trap->Argv(4, season, sizeof(season));
	trap->Argv(5, mode, sizeof(mode));

	for (i = 0; i < strlen(coursename); i++) {//Replace / in mapname with _ since we cant have a file named mp/duel1.cfg etc.
		if (coursename[i] == '*')
			coursename[i] = ' ';
	}

	Q_strlwr(username);
	Q_CleanStr(username);
	Q_strlwr(coursename);
	Q_CleanStr(coursename);
	Q_strlwr(styleStr);
	Q_CleanStr(styleStr);
	Q_strlwr(season);
	Q_CleanStr(season);

	style = RaceNameToInteger(styleStr);

	//Com_Printf("%s - %s - %s - %s\n", username, coursename, style, season);

	if (!Q_stricmp(mode, "f")) {
		sqlite3* db;
		char* sql;
		sqlite3_stmt* stmt;
		int s;

		CALL_SQLITE(open(LOCAL_DB_PATH, &db));

		if (!Q_stricmp(season, "-1")) {
			sql = "UPDATE LocalRun SET invalid = 1 WHERE username = ? AND coursename = ? AND style = ? AND season < 5";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_text(stmt, 2, coursename, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_int(stmt, 3, style));
		}
		else {
			sql = "UPDATE LocalRun SET invalid = 1 WHERE username = ? AND coursename = ? AND style = ? AND season = ?";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_text(stmt, 2, coursename, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_int(stmt, 3, style));
			CALL_SQLITE(bind_text(stmt, 4, season, -1, SQLITE_STATIC));
		}

		s = sqlite3_step(stmt);
		if (s == SQLITE_DONE)
			trap->SendServerCommand(ent - g_entities, "print \"Record flagged?\n\"");
		else
			G_ErrorPrint("ERROR: SQL Update Failed (Svcmd_InvalidateRace_f 1)", s);
		CALL_SQLITE(finalize(stmt));

		CALL_SQLITE(close(db));
	}
	else if (!Q_stricmp(mode, "u")) {
		sqlite3* db;
		char* sql;
		sqlite3_stmt* stmt;
		int s;

		CALL_SQLITE(open(LOCAL_DB_PATH, &db));

		if (!Q_stricmp(season, "-1")) {
			sql = "UPDATE LocalRun SET invalid = 0 WHERE username = ? AND coursename = ? AND style = ? AND season < 5";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_text(stmt, 2, coursename, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_int(stmt, 3, style));
		}
		else {
			sql = "UPDATE LocalRun SET invalid = 0 WHERE username = ? AND coursename = ? AND style = ? AND season = ?";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_text(stmt, 2, coursename, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_int(stmt, 3, style));
			CALL_SQLITE(bind_text(stmt, 4, season, -1, SQLITE_STATIC));
		}

		s = sqlite3_step(stmt);
		if (s == SQLITE_DONE)
			trap->SendServerCommand(ent - g_entities, "print \"Record unflagged?\n\"");
		else
			G_ErrorPrint("ERROR: SQL Update Failed (Svcmd_InvalidateRace_f 1)", s);
		CALL_SQLITE(finalize(stmt));

		CALL_SQLITE(close(db));
	}
	else if (!Q_stricmp(mode, "d")) {
		sqlite3* db;
		char* sql;
		sqlite3_stmt* stmt;
		int s;

		CALL_SQLITE(open(LOCAL_DB_PATH, &db));

		if (!Q_stricmp(season, "-1")) {
			sql = "UPDATE LocalRun SET invalid = 2 WHERE username = ? AND coursename = ? AND style = ? AND season < 5";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_text(stmt, 2, coursename, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_int(stmt, 3, style));
		}
		else {
			sql = "UPDATE LocalRun SET invalid = 2 WHERE username = ? AND coursename = ? AND style = ? AND season = ?";
			CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
			CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_text(stmt, 2, coursename, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_int(stmt, 3, style));
			CALL_SQLITE(bind_text(stmt, 4, season, -1, SQLITE_STATIC));
		}

		s = sqlite3_step(stmt);
		if (s == SQLITE_DONE)
			trap->SendServerCommand(ent - g_entities, "print \"Record flagged for deletion?\n\"");
		else
			G_ErrorPrint("ERROR: SQL Update Failed (Svcmd_InvalidateRace_f 1)", s);
		CALL_SQLITE(finalize(stmt));

		CALL_SQLITE(close(db));
	}
}

void Cmd_DFFind_f(gentity_t *ent) {
	int style = -1, season = -1, input, i;
	char inputString[40], inputStyleString[16], username[16];
	char partialCourseName[40] = {0}, fullCourseName[40] = {0};
	const int args = trap->Argc();
	qboolean enteredCourseName = qtrue;

	if (args > 5 || args < 2) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rFind <username> <mapname (optional)> <season (optional - example: s1)> <style (optional)>.  This displays the players best time.\n\"");
		return;
	}

	//Always assume 1st arg is username
	trap->Argv(1, username, sizeof(username));
	Q_strlwr(username);
	Q_CleanStr(username);

	//Get mapname.
	//How to tell if mapname is specified -- If its a map with only 1 course, we have to check. Otherwise we can assume 2nd arg is mapname.
		//It will always be the 2nd arg.
		//If the 2nd arg doesnt match the pattern of style/season/page, we can assume it is mapname.
		//This means we cant search for

	if (args == 2) //rFind <username>
		enteredCourseName = qfalse;
	else {
		trap->Argv(2, inputString, sizeof(inputString));
		//use strtol isntead of atoi maybe - partial coursename can start with number
		if ((RaceNameToInteger(inputString) != -1) || (SeasonToInteger(inputString) != -1)) {//If arg1 is style or season
			enteredCourseName = qfalse; //Use current mapname as coursename
		}
	}

	if (enteredCourseName) {
		trap->Argv(2, partialCourseName, sizeof(partialCourseName)); //Use arg1 as coursename
	}

	if (!enteredCourseName) {
		if (!level.numCourses) {
			trap->SendServerCommand(ent-g_entities, "print \"This map has no courses, you must specify one of the following with /rFind <username> <mapname (optional)> <season (optional - example: s1)> <style (optional)>.\n\"");
			return;
		}
		else if (level.numCourses > 1) { //uhhhh
			trap->SendServerCommand(ent-g_entities, "print \"This map has multiple courses, you must specify one of the following with /rFind <username> <mapname (optional)> <season (optional - example: s1)> <style (optional)>.\n\"");
			for (i = 0; i < level.numCourses; i++) { //32 max
				if (level.courseName[i] && level.courseName[i][0])
					trap->SendServerCommand(ent-g_entities, va("print \"  ^5%i ^7- ^3%s\n\"", i+1, level.courseName[i]));
			}
			return;
		}
	}

	//Go through args 3-x, if we have a specified mapname, or 1-x if we dont
	for (i = (enteredCourseName ? 3 : 2) ; i < args; i++) {
		trap->Argv(i, inputString, sizeof(inputString));
		if (style == -1) {
			input = RaceNameToInteger(inputString);
			if (input != -1) {
				style = input;
				continue;
			}
		}
		if (season == -1) {
			input = SeasonToInteger(inputString);
			if (input != -1) {
				season = input;
				continue;
			}
		}
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rFind <username> <mapname (optional)> <season (optional - example: s1)> <style (optional)>.  This displays the players best time.\n\"");
		return; //Arg doesnt match any expected values so error.
	}

	if (style == -1) //Default to JKA style
		style = 1;
	IntegerToRaceName(style, inputStyleString, sizeof(inputStyleString));
	Q_strcat(inputStyleString, sizeof(inputStyleString), " style");

	Q_strlwr(partialCourseName);
	Q_CleanStr(partialCourseName);

	if (!enteredCourseName) {
		char info[1024] = {0};
		trap->GetServerinfo(info, sizeof(info));
		Q_strncpyz(fullCourseName, Info_ValueForKey( info, "mapname" ), sizeof(fullCourseName));
		Q_strlwr(fullCourseName);
		Q_CleanStr(fullCourseName);
	}

	{ //See if course is found in database and print it then..?
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s;
		char dateStr[64] = {0}, dateStrColored[64] = {0}, timeStr[32], msg[1024-128] = {0};
		time_t	rawtime;

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));

		if (enteredCourseName) { //Course e
			//Com_Printf("doing sql query %s %i\n", courseName, style);
			//sql = "SELECT DISTINCT(coursename) FROM LocalRun WHERE coursename LIKE %?%";
			//sql = "SELECT DISTINCT(coursename) FROM LocalRun WHERE instr(coursename, ?) > 0 LIMIT 1";
			//sql = "SELECT coursename, MAX(entries) FROM LocalRun WHERE instr(coursename, ?) > 0 LIMIT 1";
			//sql = "SELECT DISTINCT(coursename) FROM LocalRun WHERE instr(coursename, ?) > 0 ORDER BY LENGTH(coursename) ASC, entries DESC LIMIT 1";
			sql = "SELECT DISTINCT(coursename) FROM LocalRun WHERE instr(replace(coursename, ' ', ''), ?) > 0 ORDER BY entries DESC LIMIT 1";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_text (stmt, 1, partialCourseName, -1, SQLITE_STATIC));
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				//Check if it actually has text, if not return.  then we can use cheaper (MAX) entries query above //loda fixme
				Q_strncpyz(fullCourseName, (char*)sqlite3_column_text(stmt, 0), sizeof(fullCourseName));
			}
			else if (s == SQLITE_DONE) {
				//Com_Printf("fail 4\n");
				trap->SendServerCommand(ent-g_entities, "print \"Usage: /rFind <username> <mapname (optional)> <season (optional - example: s1)> <style (optional)>.  This displays the players best time.\n\"");
				CALL_SQLITE (finalize(stmt));
				CALL_SQLITE (close(db));
				return;
			}
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_DFFind_f)", s);
				return;
			}
			CALL_SQLITE (finalize(stmt));

		}

		//Problem - crossmap query can return multiple records for same person since the cleanup cmd is only done on mapchange, 
		//fix by grouping by username here? and using min() so it shows right one? who knows if that will work
		//could be cheaper by using where rank != 0 instead of min(duration_ms) but w/e
		if (season == -1)
			sql = "SELECT rank, MIN(duration_ms) AS duration, topspeed, average, end_time FROM LocalRun WHERE username = ? AND coursename = ? AND style = ? GROUP BY username ORDER BY duration ASC, end_time ASC LIMIT 1";
		else 
			sql = "SELECT season_rank, MIN(duration_ms) AS duration, topspeed, average, end_time FROM LocalRun WHERE username = ? AND coursename = ? AND style = ? AND season = ? GROUP BY username ORDER BY duration ASC, end_time ASC LIMIT 1";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_text (stmt, 2, fullCourseName, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 3, style));

		if (season != -1) {
			CALL_SQLITE (bind_int (stmt, 4, season));
		}

		time( &rawtime );
		localtime( &rawtime );

		if (season == -1)
			trap->SendServerCommand(ent-g_entities, va("print \"Best time for %s on %s using %s:\n    ^5Rank     Time         Topspeed    Average      Date\n\"", username, fullCourseName, inputStyleString));
		else
			trap->SendServerCommand(ent-g_entities, va("print \"Best time for %s on %s using %s season %i:\n    ^5Rank     Time         Topspeed    Average      Date\n\"", username, fullCourseName, inputStyleString, season));
		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char *tmpMsg = NULL;
				TimeToString(sqlite3_column_int(stmt, 1), timeStr, sizeof(timeStr));
				getDateTime(sqlite3_column_int(stmt, 4), dateStr, sizeof(dateStr));
				if (rawtime - sqlite3_column_int(stmt, 4) < 60*60*24) { //Today
					Com_sprintf(dateStrColored, sizeof(dateStrColored), "^2%s^7", dateStr);
				}
				else {
					Q_strncpyz(dateStrColored, dateStr, sizeof(dateStrColored));
				}
				tmpMsg = va("    ^3%-8i %-12s %-11i %-12i %s\n", sqlite3_column_int(stmt, 0), timeStr, sqlite3_column_int(stmt, 2), sqlite3_column_int(stmt, 3), dateStrColored);
				if (strlen(msg) + strlen(tmpMsg) >= sizeof( msg)) {
					trap->SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
					msg[0] = '\0';
				}
				Q_strcat(msg, sizeof(msg), tmpMsg);
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_DFFind_f)", s);
				break;
			}
		}
		trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));

		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
	}
}

#if 1//NEWRACERANKING
void Cmd_DFTopRank_f(gentity_t *ent) { //Add season support?
	int style = -1, season = -1, page = -1, start = 0, i, input;
	char styleString[16] = {0}, inputString[32];
	const int args = trap->Argc();

	if (args > 4) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rRank <season (optional - example: s1)> <style (optional)> <page (optional)>.  This displays the rankings for the specified season and style.\n\"");
		return;
	}

	for (i = 1; i < args; i++) {
		trap->Argv(i, inputString, sizeof(inputString));
		if (season == -1) {
			input = SeasonToInteger(inputString);
			if (input != -1) {
				season = input;
				continue;
			}
		}
		if (style == -1) {
			input = RaceNameToInteger(inputString);
			if (input != -1) {
				style = input;
				continue;
			}
		}
		if (page == -1) {
			input = atoi(inputString);
			if (input > 0) {
				page = input;
				continue;
			}
		}
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rRank <season (optional - example: s1)> <style (optional)> <page (optional)>.  This displays the rankings for the specified season and style.\n\"");
		return;
	}

	if (style == -1) {
		Q_strncpyz(styleString, "all styles", sizeof(styleString));
	}
	else {
		IntegerToRaceName(style, styleString, sizeof(styleString));
		Q_strcat(styleString, sizeof(styleString), " style");
	}

	if (page < 1)
		page = 1;
	if (page > 1000)
		page = 1000;
	start = (page - 1) * 10;

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int s, oldscore, newscore, count, golds, silvers, bronzes, row = 1;
		float rank, percentile;
		char msg[1024-128] = {0}, username[40];

		CALL_SQLITE (open (LOCAL_DB_PATH, & db)); //Needs to select only top entry from each person not all seasons
		if (style == -1) {
			if (season == -1) {
				sql = "SELECT username, SUM(entries-rank) AS newscore, CAST(SUM(entries/CAST(rank AS FLOAT)) AS INT) AS oldscore, AVG(rank) as rank, AVG((entries - CAST(rank-1 AS float))/entries) AS percentile, SUM(CASE WHEN rank == 1 THEN 1 ELSE 0 END) AS golds, SUM(CASE WHEN rank == 2 THEN 1 ELSE 0 END) AS silvers, SUM(CASE WHEN rank == 3 THEN 1 ELSE 0 END) AS bronzes, COUNT(*) as count FROM LocalRun "
					"WHERE rank != 0 "
					"GROUP BY username "
					"ORDER BY oldscore+newscore DESC, rank DESC LIMIT ?, 10";
				CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
				CALL_SQLITE (bind_int (stmt, 1, start));
			}
			else {
				sql = "SELECT username, SUM(season_entries-season_rank) AS newscore, CAST(SUM(season_entries/CAST(season_rank AS FLOAT)) AS INT) AS oldscore, AVG(season_rank) as season_rank, AVG((season_entries - CAST(season_rank-1 AS float))/season_entries) AS percentile, SUM(CASE WHEN season_rank == 1 THEN 1 ELSE 0 END) AS golds, SUM(CASE WHEN season_rank == 2 THEN 1 ELSE 0 END) AS silvers, SUM(CASE WHEN season_rank == 3 THEN 1 ELSE 0 END) AS bronzes, COUNT(*) as count FROM LocalRun "
					"WHERE season = ? "
					"GROUP BY username "
					"ORDER BY oldscore+newscore DESC, rank DESC LIMIT ?, 10";
				CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
				CALL_SQLITE (bind_int (stmt, 1, season));
				CALL_SQLITE (bind_int (stmt, 2, start));
			}
		}
		else {
			if (season == -1) {
				sql = "SELECT username, SUM(entries-rank) AS newscore, CAST(SUM(entries/CAST(rank AS FLOAT)) AS INT) AS oldscore, AVG(rank) as rank, AVG((entries - CAST(rank-1 AS float))/entries) AS percentile, SUM(CASE WHEN rank == 1 THEN 1 ELSE 0 END) AS golds, SUM(CASE WHEN rank == 2 THEN 1 ELSE 0 END) AS silvers, SUM(CASE WHEN rank == 3 THEN 1 ELSE 0 END) AS bronzes, COUNT(*) as count FROM LocalRun "
					"WHERE rank != 0 AND style = ? "
					"GROUP BY username "
					"ORDER BY oldscore+newscore DESC, rank DESC LIMIT ?, 10";
				CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
				CALL_SQLITE (bind_int (stmt, 1, style));
				CALL_SQLITE (bind_int (stmt, 2, start));
			}
			else {
				sql = "SELECT username, SUM(season_entries-season_rank) AS newscore, CAST(SUM(season_entries/CAST(season_rank AS FLOAT)) AS INT) AS oldscore, AVG(season_rank) as season_rank, AVG((season_entries - CAST(season_rank-1 AS float))/season_entries) AS percentile, SUM(CASE WHEN season_rank == 1 THEN 1 ELSE 0 END) AS golds, SUM(CASE WHEN season_rank == 2 THEN 1 ELSE 0 END) AS silvers, SUM(CASE WHEN season_rank == 3 THEN 1 ELSE 0 END) AS bronzes, COUNT(*) as count FROM LocalRun "
					"WHERE season = ? AND style = ? "
					"GROUP BY username "
					"ORDER BY oldscore+newscore DESC, rank DESC LIMIT ?, 10";
				CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
				CALL_SQLITE (bind_int (stmt, 1, season));
				CALL_SQLITE (bind_int (stmt, 2, style));
				CALL_SQLITE (bind_int (stmt, 3, start));
			}
		}

		if (season == -1)
			trap->SendServerCommand(ent-g_entities, va("print \"Highscore results for %s:\n    ^5Username           Score     SPR       Avg. Rank   Percentile   Golds   Silvers   Bronzes   Count \n\"", styleString));
		else
			trap->SendServerCommand(ent-g_entities, va("print \"Highscore results for %s season %i:\n    ^5Username           Score     SPR       Avg. Rank   Percentile   Golds   Silvers   Bronzes   Count \n\"", styleString, season));

		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char *tmpMsg = NULL;
				Q_strncpyz(username, (char*)sqlite3_column_text(stmt, 0), sizeof(username));
				newscore = sqlite3_column_int(stmt, 1);
				oldscore = sqlite3_column_int(stmt, 2);
				rank = sqlite3_column_double(stmt, 3);
				percentile = sqlite3_column_double(stmt, 4);
				golds = sqlite3_column_int(stmt, 5);
				silvers = sqlite3_column_int(stmt, 6);
				bronzes = sqlite3_column_int(stmt, 7);
				count = sqlite3_column_int(stmt, 8);

				tmpMsg = va("^5%2i^3: ^3%-18s ^3%-9i ^3%-9.2f ^3%-11.2f ^3%-12.2f ^3%-7i ^3%-9i ^3%-9i %i\n", row+start, username, (int)(1+((oldscore+newscore)*0.5f)), (count ? ((float)oldscore/(float)count) : oldscore), rank, percentile, golds, silvers, bronzes, count);
				if (strlen(msg) + strlen(tmpMsg) >= sizeof( msg)) {
					trap->SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
					msg[0] = '\0';
				}
				Q_strcat(msg, sizeof(msg), tmpMsg);
				row++;
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_DFTopRank_f)", s);
				break;
			}
		}
		trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));

		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));

	}
}
#endif

void Cmd_DFHardest_f(gentity_t *ent) {
	int style = -1, page = -1, start = 0, input, i;
	char inputString[16], inputStyleString[16];
	qboolean currentSeason = qfalse;
	const int args = trap->Argc();

	//Should this also only show results that you don't have a time on?

	if (args > 4) {//5 for 'me'
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rHardest <style (optional)> <current season (optional - example: s) <page (optional)>.  This displays hardest completed courses for the specified style.\n\"");
		return;
	}

	for (i = 1; i < args; i++) {
		trap->Argv(i, inputString, sizeof(inputString));
		if (style == -1) {
			input = RaceNameToInteger(inputString);
			if (input != -1) {
				style = input;
				continue;
			}
		}
		if (!currentSeason) {
			if (!Q_stricmp(inputString, "s")) {
				currentSeason = qtrue;
				continue;
			}
		}
		/*
		if (!me) {
			if (!Q_stricmp(inputString, "me")) {
				me = qtrue;
				continue;
			}
		}
		*/
		if (page == -1) {
			input = atoi(inputString);
			if (input > 0) {
				page = input;
				continue;
			}
		}
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rHardest <style (optional)> <include seasons (optional - example: s) <page (optional)>.  This displays hardest completed courses for the specified style.\n\"");
		return; //Arg doesnt match any expected values so error.
	}

	/*
	if (me) {
		if (!ent->client->pers.userName[0]) { //Not logged in
			trap->SendServerCommand(ent-g_entities, "print \"You must be logged in to use this command.\n\"");
			return;
		}
		Q_strncpyz(username, ent->client->pers.userName, sizeof(username));
		Q_strlwr(username);
		Q_CleanStr(username);
	}
	*/

	if (style == -1) {
		Q_strncpyz(inputStyleString, "all styles", sizeof(inputStyleString));
	}
	else {
		IntegerToRaceName(style, inputStyleString, sizeof(inputStyleString));
		Q_strcat(inputStyleString, sizeof(inputStyleString), " style");
	}

	if (page < 1)
		page = 1;
	if (page > 1000)
		page = 1000;
	start = (page - 1) * 10;

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int row = 1;
		char styleStr[16] = {0}, msg[128] = {0};
		int s;

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));

		if (style == -1) {
			if (currentSeason)
				sql = "SELECT username, coursename, style, season_entries FROM LocalRun WHERE season = (SELECT MAX(season) FROM LocalRun) ORDER BY season_entries ASC, entries ASC LIMIT ?,10";
			else
				sql = "SELECT username, coursename, style, entries FROM LocalRun ORDER BY entries ASC LIMIT ?,10";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_int (stmt, 1, start));
		}
		else {
			if (currentSeason)
				sql = "SELECT username, coursename, style, season_entries FROM LocalRun WHERE season = (SELECT MAX(season) FROM LocalRun) AND style = ? ORDER BY season_entries ASC, entries ASC LIMIT ?,10";
			else
				sql = "SELECT username, coursename, style, entries FROM LocalRun WHERE style = ? ORDER BY entries ASC LIMIT ?,10";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_int (stmt, 1, style));
			CALL_SQLITE (bind_int (stmt, 2, start));
		}

		if (currentSeason)
				trap->SendServerCommand(ent-g_entities, va("print \"Results for %s (current season):\n    ^5Username           Coursename                     Style       Entries\n\"", inputStyleString));
		else
				trap->SendServerCommand(ent-g_entities, va("print \"Results for %s:\n    ^5Username           Coursename                     Style       Entries\n\"", inputStyleString));
		
		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char *tmpMsg = NULL;
				IntegerToRaceName(sqlite3_column_int(stmt, 2), styleStr, sizeof(styleStr));

				tmpMsg = va("^5%2i^3: ^3%-18s ^3%-30s ^3%-11s ^3%-8i\n", start+row, sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1), styleStr, sqlite3_column_int(stmt, 3));
				if (strlen(msg) + strlen(tmpMsg) >= sizeof( msg)) {
					trap->SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
					msg[0] = '\0';
				}
				Q_strcat(msg, sizeof(msg), tmpMsg);
				row++;
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_DFHardest_f)", s);
				break;
			}
		}
		trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));

		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
	}


}

void Cmd_DFCompare_f(gentity_t *ent) {
	int style = -1, page = -1, start = 0, input, i, season = -1;
	char inputString[16], inputStyleString[16], myUsername[16], theirUsername[16];
	const int args = trap->Argc();

	if (!ent->client->pers.userName[0]) {
		trap->SendServerCommand(ent - g_entities, "print \"You must be logged in to use this command.\n\"");
		return;
	}
	Q_strncpyz(myUsername, ent->client->pers.userName, sizeof(myUsername));

	if (args < 2 || args > 5) {
		trap->SendServerCommand(ent - g_entities, "print \"Usage: /rCompare <username> <style (optional)> <current season (optional - example: s) <page (optional)>.  This displays the courses that the specified user has defeated you on.\n\"");
		return;
	}

	//Make 1st arg be username I guess.
	trap->Argv(1, theirUsername, sizeof(theirUsername));
	Q_strlwr(theirUsername);
	Q_CleanStr(theirUsername);

	for (i = 2; i < args; i++) {
		trap->Argv(i, inputString, sizeof(inputString));
		if (style == -1) {
			input = RaceNameToInteger(inputString);
			if (input != -1) {
				style = input;
				continue;
			}
		}
		if (season == -1) {
			input = SeasonToInteger(inputString);
			if (input != -1) {
				season = input;
				continue;
			}
		}
		if (page == -1) {
			input = atoi(inputString);
			if (input > 0) {
				page = input;
				continue;
			}
		}
		trap->SendServerCommand(ent - g_entities, "print \"Usage: /rCompare <username> <style (optional)> <current season (optional - example: s) <page (optional)>.  This displays the courses that the specified user has defeated you on.\n\"");
		return; //Arg doesnt match any expected values so error.
	}

	if (style == -1) {
		Q_strncpyz(inputStyleString, "all styles", sizeof(inputStyleString));
	}
	else {
		IntegerToRaceName(style, inputStyleString, sizeof(inputStyleString));
		Q_strcat(inputStyleString, sizeof(inputStyleString), " style");
	}

	if (page < 1)
		page = 1;
	if (page > 1000)
		page = 1000;
	start = (page - 1) * 10;

	//Com_Printf("Username 1 is %s, Username 2 is %s, Style is %i, season is %i, page is %i\n", myUsername, theirUsername, style, season, page);
	//return;

/*
//Example query to see which courses source has beat kane on
SELECT username, coursename, style, season, MIN(duration_ms) FROM
(SELECT username, coursename, style, season, duration_ms FROM LocalRUN WHERE username = "kane"
UNION ALL
SELECT username, coursename, style, season, duration_ms FROM LocalRUN WHERE username = "source")
WHERE username = "source"
GROUP BY coursename, style, season
*/

	//select a.*,b.* FROM LocalRun a INNER JOIN LocalRun b ON a.coursename=b.coursename AND a.style=b.style WHERE a.duration_ms < b.duration_ms AND a.username = 'loda' AND b.username = 'kane'

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int row = 1;
		char styleStr[16] = { 0 }, msg[128] = { 0 };
		int s;

		CALL_SQLITE(open(LOCAL_DB_PATH, &db));

		//Problem - these queries return races if the other person has not even done that race.  The query is just bad in general..
		if (season == -1) {
			if (style == -1) { //All seasons, all styles
				trap->SendServerCommand(ent - g_entities, va("print \"Results for player %s %s:\n    ^5Coursename                     Style\n\"", theirUsername, inputStyleString));
				sql = "SELECT a.coursename, a.style FROM LocalRun a WHERE a.username = ? AND a.rank != 0 AND EXISTS (SELECT 1 FROM LocalRun b WHERE b.username = ? AND b.coursename = a.coursename AND b.style = a.style AND b.duration_ms > a.duration_ms AND b.rank != 0) ORDER BY a.entries DESC LIMIT ?,10";
					CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
					CALL_SQLITE(bind_text(stmt, 1, theirUsername, -1, SQLITE_STATIC));
					CALL_SQLITE(bind_text(stmt, 2, myUsername, -1, SQLITE_STATIC));
					CALL_SQLITE(bind_int(stmt, 3, start));
			}
			else {//All seasons, specific style
				trap->SendServerCommand(ent - g_entities, va("print \"Results for player %s %s:\n    ^5Coursename\n\"", theirUsername, inputStyleString));
				sql = "SELECT a.coursename FROM LocalRun a WHERE a.username = ? AND a.style = ? AND a.rank != 0 AND EXISTS (SELECT 1 FROM LocalRun b WHERE b.username = ? AND b.coursename = a.coursename AND b.style = a.style AND b.duration_ms > a.duration_ms AND b.rank != 0) ORDER BY a.entries DESC LIMIT ?,10";
					CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
					CALL_SQLITE(bind_text(stmt, 1, theirUsername, -1, SQLITE_STATIC));
					CALL_SQLITE(bind_int(stmt, 2, style));
					CALL_SQLITE(bind_text(stmt, 3, myUsername, -1, SQLITE_STATIC));
					CALL_SQLITE(bind_int(stmt, 4, start));
			}
		}
		else {
			if (style == -1) {//Specific season, all styles
				trap->SendServerCommand(ent - g_entities, va("print \"Results for player %s %s season %i:\n    ^5Coursename                     Style\n\"", theirUsername, inputStyleString, season));
				sql = "SELECT a.coursename, a.style FROM LocalRun a WHERE a.username = ? AND a.season = ? AND EXISTS (SELECT 1 FROM LocalRun b WHERE b.username = ? AND b.coursename = a.coursename AND b.style = a.style AND b.season = a.season AND b.duration_ms > a.duration_ms) ORDER BY a.entries DESC LIMIT ?,10";
					CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
					CALL_SQLITE(bind_text(stmt, 1, theirUsername, -1, SQLITE_STATIC));
					CALL_SQLITE(bind_int(stmt, 2, season));
					CALL_SQLITE(bind_text(stmt, 3, myUsername, -1, SQLITE_STATIC));
					CALL_SQLITE(bind_int(stmt, 4, start));
			}
			else {//Speific season, specific style
				trap->SendServerCommand(ent - g_entities, va("print \"Results for player %s %s season %i:\n    ^5Coursename\n\"", theirUsername, inputStyleString, season));
				sql = "SELECT a.coursename FROM LocalRun a WHERE a.username = ? AND a.style = ? AND a.season = ? AND EXISTS (SELECT 1 FROM LocalRun b WHERE b.username = ? AND b.coursename = a.coursename AND b.style = a.style AND b.season = a.season AND b.duration_ms > a.duration_ms) ORDER BY a.entries DESC LIMIT ?,10";
				CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
				CALL_SQLITE(bind_text(stmt, 1, theirUsername, -1, SQLITE_STATIC));
				CALL_SQLITE(bind_int(stmt, 2, style));
				CALL_SQLITE(bind_int(stmt, 3, season));
				CALL_SQLITE(bind_text(stmt, 4, myUsername, -1, SQLITE_STATIC));
				CALL_SQLITE(bind_int(stmt, 5, start));
			}
		}

		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char *tmpMsg = NULL;

				if (style == -1) {
					IntegerToRaceName(sqlite3_column_int(stmt, 1), styleStr, sizeof(styleStr));
					tmpMsg = va("^5%2i^3: ^3%-30s ^3%s\n", start + row, sqlite3_column_text(stmt, 0), styleStr); //Print username, inputstyle, coursename, returned style
				}
				else
					tmpMsg = va("^5%2i^3: ^3%s\n", start + row, sqlite3_column_text(stmt, 0)); //Print username, inputstyle, coursename

				if (strlen(msg) + strlen(tmpMsg) >= sizeof(msg)) {
					trap->SendServerCommand(ent - g_entities, va("print \"%s\"", msg));
					msg[0] = '\0';
				}
				Q_strcat(msg, sizeof(msg), tmpMsg);
				row++;
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_DFCompare_f)", s);
				break;
			}
		}
		trap->SendServerCommand(ent - g_entities, va("print \"%s\"", msg));

		CALL_SQLITE(finalize(stmt));
		CALL_SQLITE(close(db));
	}


}

void Cmd_DFRecent_f(gentity_t *ent) {
	int style = -1, page = -1, start = 0, input, i;
	char inputString[16], inputStyleString[16];
	qboolean showSeasons = qfalse;
	const int args = trap->Argc();

	if (args > 4) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rLatest <style (optional)> <include seasons (optional - example: s) <page (optional)>.  This displays recent records for the specified style.\n\"");
		return;
	}

	for (i = 1; i < args; i++) {
		trap->Argv(i, inputString, sizeof(inputString));
		if (style == -1) {
			input = RaceNameToInteger(inputString);
			if (input != -1) {
				style = input;
				continue;
			}
		}
		if (!showSeasons) {
			if (!Q_stricmp(inputString, "s")) {
				showSeasons = qtrue;
				continue;
			}
		}
		if (page == -1) {
			input = atoi(inputString);
			if (input > 0) {
				page = input;
				continue;
			}
		}
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rLatest <style (optional)> <include seasons (optional - example: s) <page (optional)>.  This displays recent records for the specified style.\n\"");
		return; //Arg doesnt match any expected values so error.
	}

	if (style == -1) {
		Q_strncpyz(inputStyleString, "all styles", sizeof(inputStyleString));
	}
	else {
		IntegerToRaceName(style, inputStyleString, sizeof(inputStyleString));
		Q_strcat(inputStyleString, sizeof(inputStyleString), " style");
	}

	if (page < 1)
		page = 1;
	if (page > 10000000)
		page = 10000000;
	start = (page - 1) * 10;

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int row = 1;
		char dateStr[64] = {0}, timeStr[32] = {0}, styleStr[16] = {0}, rankStr[16] = {0}, msg[1024 - 128] = { 0 };
		int s;

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));

		if (style == -1) {
			if (showSeasons)
				sql = "SELECT username, coursename, style, rank, duration_ms, end_time, season_rank FROM LocalRun ORDER BY end_time DESC LIMIT ?,10";
			else
				sql = "SELECT username, coursename, style, rank, duration_ms, end_time FROM LocalRun WHERE rank != 0 ORDER BY end_time DESC LIMIT ?,10";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_int (stmt, 1, start));
		}
		else {
			if (showSeasons)
				sql = "SELECT username, coursename, style, rank, duration_ms, end_time, season_rank FROM LocalRun WHERE style = ? ORDER BY end_time DESC LIMIT ?,10";
			else
				sql = "SELECT username, coursename, style, rank, duration_ms, end_time FROM LocalRun WHERE rank != 0 AND style = ? ORDER BY end_time DESC LIMIT ?,10";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_int (stmt, 1, style));
			CALL_SQLITE (bind_int (stmt, 2, start));
		}

		if (showSeasons)
			trap->SendServerCommand(ent-g_entities, va("print \"Recent results for %s style (by season):\n    ^5Username           Coursename                     Style       Rank     Time         Date\n\"", inputStyleString));
		else
			trap->SendServerCommand(ent-g_entities, va("print \"Recent results for %s style:\n    ^5Username           Coursename                     Style       Rank     Time         Date\n\"", inputStyleString));
		
		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char *tmpMsg = NULL;
				TimeToString(sqlite3_column_int(stmt, 4), timeStr, sizeof(timeStr));
				getDateTime(sqlite3_column_int(stmt, 5), dateStr, sizeof(dateStr));
				IntegerToRaceName(sqlite3_column_int(stmt, 2), styleStr, sizeof(styleStr));

				if (showSeasons) {	//If rank == 0, put season rank in yellow, else put rank in green
					if (sqlite3_column_int(stmt, 3))
						Com_sprintf(rankStr, sizeof(rankStr), "^2%i^7", sqlite3_column_int(stmt, 3));
					else
						Com_sprintf(rankStr, sizeof(rankStr), "^3%i^7", sqlite3_column_int(stmt, 6));
				}
				else
					Com_sprintf(rankStr, sizeof(rankStr), "^2%i^7", sqlite3_column_int(stmt, 3));

				tmpMsg = va("^5%2i^3: ^3%-18s ^3%-30s ^3%-11s ^3%-12s ^3%-12s %s\n", start+row, sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1), styleStr, rankStr, timeStr, dateStr);
				if (strlen(msg) + strlen(tmpMsg) >= sizeof(msg)) {
					trap->SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
					msg[0] = '\0';
				}
				Q_strcat(msg, sizeof(msg), tmpMsg);
				row++;
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_DFRecent_f)", s);
				break;
			}
		}
		trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));

		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
	}


}

qboolean atoi_real(const char* string) {
	size_t i;
	for (i = 0; string[i] != '\0'; ++i) {
		if (string[i] < '0' || string[i] > '9') {
			return qfalse;
		}
	}
	return qtrue;
}

void Cmd_DFTop10_f(gentity_t *ent) {
	int style = -1, page = -1, season = -1, start = 0, input, i;
	char inputString[40], inputStyleString[16];
	char partialCourseName[40] = {0}, fullCourseName[40] = {0};
	const int args = trap->Argc();
	qboolean enteredCourseName = qtrue;

	if (args > 5) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rTop <course (if needed)> <style (optional)> <season (optional - example: s1)> <page (optional)>.  This displays the top10 for the specified course.\n\"");
		return;
	}

	//Get mapname.
	//How to tell if mapname is specified -- If its a map with only 1 course, we have to check. Otherwise we can assume 1st arg is mapname.
		//It will always be the first arg.
		//If the first arg doesnt match the pattern of style/season/page, we can assume it is mapname.
		//This means we cant search for

	if (args == 1)
		enteredCourseName = qfalse;
	else {
		trap->Argv(1, inputString, sizeof(inputString));
		//use strtol isntead of atoi maybe - partial coursename can start with number
		if ((RaceNameToInteger(inputString) != -1) || (SeasonToInteger(inputString) != -1) || (atoi_real(inputString))) {//If arg1 is style, or season, or page
			//BUG - atoi(inputstring) returns true for values like "18percent" where it should return false..
			enteredCourseName = qfalse; //Use current mapname as coursename
		}
	}

	if (enteredCourseName) {
		trap->Argv(1, partialCourseName, sizeof(partialCourseName)); //Use arg1 as coursename
	}

	if (!enteredCourseName) {
		if (!level.numCourses) {
			trap->SendServerCommand(ent-g_entities, "print \"This map has no courses, you must specify one of the following with /rTop <coursename> <style (optional)> <season (optional - example: s1)> <page (optional)>.\n\"");
			return;
		}
		else if (level.numCourses > 1) {
			trap->SendServerCommand(ent-g_entities, "print \"This map has multiple courses, you must specify one of the following with /rTop <coursename> <style (optional)> <season (optional - example: s1)> <page (optional)>.\n\"");
			for (i = 0; i < level.numCourses; i++) { //32 max
				if (level.courseName[i] && level.courseName[i][0])
					trap->SendServerCommand(ent-g_entities, va("print \"  ^5%i ^7- ^3%s\n\"", i+1, level.courseName[i]));
			}
			return;
		}
	}

	//Go through args 2-x, if we have a specified mapname, or 1-x if we dont
	for (i = (enteredCourseName ? 2 : 1) ; i < args; i++) {
		trap->Argv(i, inputString, sizeof(inputString));
		if (style == -1) {
			input = RaceNameToInteger(inputString);
			if (input != -1) {
				style = input;
				continue;
			}
		}
		if (season == -1) {
			input = SeasonToInteger(inputString);
			if (input != -1) {
				season = input;
				continue;
			}
		}
		if (page == -1) {
			input = atoi(inputString);
			if (input > 0) {
				page = input;
				continue;
			}
		}
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rTop <course (if needed)> <style (optional)> <season (optional - example: s1)> <page (optional)>.  This displays highscores for the specified course.\n\"");
		return; //Arg doesnt match any expected values so error.
	}

	if (style == -1) //Default to JKA style
		style = 1;
	IntegerToRaceName(style, inputStyleString, sizeof(inputStyleString));
	Q_strcat(inputStyleString, sizeof(inputStyleString), " style");

	if (page < 1)
		page = 1;
	if (page > 1000)
		page = 1000;
	start = (page - 1) * 10;

	Q_strlwr(partialCourseName);
	Q_CleanStr(partialCourseName);
	Q_strstrip(partialCourseName, " ", "");
	Q_strstrip(partialCourseName, "&", " ");

	Com_Printf("Partial coursename is %s\n", partialCourseName);

	if (!enteredCourseName) {
		char info[1024] = {0};
		trap->GetServerinfo(info, sizeof(info));
		Q_strncpyz(fullCourseName, Info_ValueForKey( info, "mapname" ), sizeof(fullCourseName));
		Q_strlwr(fullCourseName);
		Q_CleanStr(fullCourseName);
		Q_strstrip(partialCourseName, " ", "");
	}

	//Com_Printf("Style %i, page %i, season %i, map %s, fullmap %s\n", style, page, season, partialCourseName, fullCourseName);

	{ //See if course is found in database and print it then..?
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int row = 1;
		int s;
		char dateStr[64] = {0}, dateStrColored[64] = {0}, timeStr[32], msg[1024-128] = {0};
		time_t	rawtime;

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));

		if (enteredCourseName) { //Course e
			//sql = "SELECT DISTINCT(coursename) FROM LocalRun WHERE instr(replace(coursename, ' ', ''), ?) > 0 ORDER BY entries DESC LIMIT 1";
			sql = "SELECT DISTINCT(coursename) FROM LocalRun WHERE instr(coursename, ?) > 0 ORDER BY entries DESC LIMIT 1";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_text (stmt, 1, partialCourseName, -1, SQLITE_STATIC));
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				//Check if it actually has text, if not return.  then we can use cheaper (MAX) entries query above //loda fixme
				Q_strncpyz(fullCourseName, (char*)sqlite3_column_text(stmt, 0), sizeof(fullCourseName));
			}
			else if (s == SQLITE_DONE) {
				//Com_Printf("fail 4\n");
				trap->SendServerCommand(ent-g_entities, "print \"Usage: /rTop <course (if needed)> <style (optional)> <season (optional - example: s1)> <page (optional)>.  This displays highscores for the specified course.\n\"");
				CALL_SQLITE (finalize(stmt));
				CALL_SQLITE (close(db));
				return;
			}
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_DFTop10_f)", s);
				return;
			}
			CALL_SQLITE (finalize(stmt));

		}

		//Problem - crossmap query can return multiple records for same person since the cleanup cmd is only done on mapchange, 
		//fix by grouping by username here? and using min() so it shows right one? who knows if that will work
		//could be cheaper by using where rank != 0 instead of min(duration_ms) but w/e
		if (season == -1)
			sql = "SELECT username, MIN(duration_ms) AS duration, topspeed, average, end_time FROM LocalRun WHERE coursename = ? AND style = ? AND invalid = 0 GROUP BY username ORDER BY duration ASC, end_time ASC, average DESC LIMIT ?, 10";
		else 
			sql = "SELECT username, MIN(duration_ms) AS duration, topspeed, average, end_time FROM LocalRun WHERE coursename = ? AND style = ? AND season = ? GROUP BY username ORDER BY duration ASC, end_time ASC, average DESC LIMIT ?, 10";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, fullCourseName, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 2, style));

		if (season == -1) {
			CALL_SQLITE (bind_int (stmt, 3, start));
		}
		else {
			CALL_SQLITE (bind_int (stmt, 3, season));
			CALL_SQLITE (bind_int (stmt, 4, start));
		}

		//Todo, select flagged, if flagged - change color of the time during print.

		time( &rawtime );
		localtime( &rawtime );

		if (season == -1)
			trap->SendServerCommand(ent-g_entities, va("print \"Highscore results for %s using %s:\n    ^5Username           Time         Topspeed    Average      Date\n\"", fullCourseName, inputStyleString));
		else
			trap->SendServerCommand(ent-g_entities, va("print \"Highscore results for %s using %s season %i:\n    ^5Username           Time         Topspeed    Average      Date\n\"", fullCourseName, inputStyleString, season));
		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char *tmpMsg = NULL;
				TimeToString(sqlite3_column_int(stmt, 1), timeStr, sizeof(timeStr));
				getDateTime(sqlite3_column_int(stmt, 4), dateStr, sizeof(dateStr));
				if (rawtime - sqlite3_column_int(stmt, 4) < 60*60*24) { //Today
					Com_sprintf(dateStrColored, sizeof(dateStrColored), "^2%s^7", dateStr);
				}
				else {
					Q_strncpyz(dateStrColored, dateStr, sizeof(dateStrColored));
				}
				//if (sqlite3_column_int(stmt, 5) == 1) //temp, make flagged runs red until we can figure out how to handle them
					//tmpMsg = va("^5%2i^3: ^3%-18s ^1%-12s ^3%-11i ^3%-12i %s\n", row+start, sqlite3_column_text(stmt, 0), timeStr, sqlite3_column_int(stmt, 2), sqlite3_column_int(stmt, 3), dateStrColored);
				//else
					tmpMsg = va("^5%2i^3: ^3%-18s ^3%-12s ^3%-11i ^3%-12i %s\n", row + start, sqlite3_column_text(stmt, 0), timeStr, sqlite3_column_int(stmt, 2), sqlite3_column_int(stmt, 3), dateStrColored);
				if (strlen(msg) + strlen(tmpMsg) >= sizeof( msg)) {
					trap->SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
					msg[0] = '\0';
				}
				Q_strcat(msg, sizeof(msg), tmpMsg);
				row++;
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_DFTop10_f)", s);
				break;
			}
		}
		trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));

		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
	}
}

#if 0
void Cmd_DFTop10_f(gentity_t *ent) {
	const int args = trap->Argc();
	char input1[40], input2[32], input3[32], courseName[40] = {0}, courseNameFull[40] = {0}, msg[1024-128] = {0}, timeStr[32], styleString[16] = {0};
	int i, style = -1, page = 1, start;
	qboolean partialCourseName = qtrue;

	//special case for if only 1 course on map.. aoh no
	//dftop10 (coursename always first) style, season, page can be any order


	if (args == 1) { //Dftop10  - current map JKA, only 1 course on map.  Or if there are multiple courses, display them all.
		if (level.numCourses == 0) { //No course on this map, so error.
			//Com_Printf("fail 1\n");
			trap->SendServerCommand(ent-g_entities, "print \"Usage: /rTop <course (if needed)> <style (optional)> <page (optional)>.  This displays the top10 for the specified course.\n\"");
			return;
		}
		if (level.numCourses > 1) { //
			trap->SendServerCommand(ent-g_entities, "print \"This map has multiple courses, you must specify one of the following with /rTop <coursename> <style (optional)> <page (optional)>.\n\"");
			for (i = 0; i < level.numCourses; i++) { //32 max
				if (level.courseName[i] && level.courseName[i][0])
					trap->SendServerCommand(ent-g_entities, va("print \"  ^5%i ^7- ^3%s\n\"", i+1, level.courseName[i]));
			}
			return;
		}
		else {
			partialCourseName = qfalse;
		}
		style = 1;
	}
	else if (args == 2) {//CPM - current map cpm, only 1 course on map
		trap->Argv(1, input1, sizeof(input1));
		style = RaceNameToInteger(input1);
		//Check if 2nd arg is style or course.

		if (style < 0) { //Invalid style, so its a course intead.
			style = 1;
			Q_strncpyz(courseName, input1, sizeof(courseName));
		}
		else if (level.numCourses == 1) { //What if its a style, then we use course=0 ?
			partialCourseName = qfalse;
		}
	}
	else if (args == 3) { //dftop10 dash1 cpm - search for dash1 exact match(?) in memory, if not then fallback to SQL query.  cpm style.
		//Get 2nd arg as course
		//Get 3rd arg as style
		trap->Argv(1, input1, sizeof(input1));
		trap->Argv(2, input2, sizeof(input2));

		style = RaceNameToInteger(input2);
		if (style < 0) { //Invalid style
			//Com_Printf("fail 2\n");
			trap->SendServerCommand(ent-g_entities, "print \"Usage: /rTop <course (if needed)> <style (optional)> <page (optional)>.  This displays the top10 for the specified course.\n\"");
			return;
		}

		Q_strncpyz(courseName, input1, sizeof(courseName));

	}
	else if (args == 4) { //dftop10 dash1 cpm - search for dash1 exact match(?) in memory, if not then fallback to SQL query.  cpm style.
		//Get 2nd arg as course
		//Get 3rd arg as style
		trap->Argv(1, input1, sizeof(input1));
		trap->Argv(2, input2, sizeof(input2));
		trap->Argv(3, input3, sizeof(input3));

		style = RaceNameToInteger(input2);
		if (style < 0) { //Invalid style
			//Com_Printf("fail 2\n");
			trap->SendServerCommand(ent-g_entities, "print \"Usage: /rTop <course (if needed)> <style (optional)> <page (optional)>.  This displays the top10 for the specified course.\n\"");
			return;
		}
		page = atoi(input3);
		if (page < 1 || page > 100) {
			trap->SendServerCommand(ent-g_entities, "print \"Usage: /rTop <course (if needed)> <style (optional)> <page (optional)>.  This displays the top10 for the specified course.\n\"");
			return;
		}

		Q_strncpyz(courseName, input1, sizeof(courseName));

	}
	else { //Error, print usage
		//Com_Printf("fail 3\n");
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rTop <course (if needed)> <style (optional)> <page (optional)>.  This displays the top10 for the specified course.\n\"");
		return;
	}

	start = (page - 1) * 10;
	
	//At this point we should have a valid style and a potential coursename.
	Q_strlwr(courseName);
	Q_CleanStr(courseName);
	IntegerToRaceName(style, styleString, sizeof(styleString));

	if (!partialCourseName) {
		char info[1024] = {0};
		trap->GetServerinfo(info, sizeof(info));
		Q_strncpyz(courseNameFull, Info_ValueForKey( info, "mapname" ), sizeof(courseNameFull));
		Q_strlwr(courseNameFull);
		Q_CleanStr(courseNameFull);
	}
	else {
		if (!Q_stricmp(courseName, "")) {
			trap->SendServerCommand(ent-g_entities, "print \"Usage: /rTop <course (if needed)> <style (optional)> <page (optional)>.  This displays the top10 for the specified course.\n\"");
			return;
		}
	}

	{ //See if course is found in database and print it then..?
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		int row = 1;
		int s;
		char dateStr[64] = {0}, dateStrColored[64] = {0};
		time_t	rawtime;

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));

		if (partialCourseName) { //Course e
			//Com_Printf("doing sql query %s %i\n", courseName, style);
			//sql = "SELECT DISTINCT(coursename) FROM LocalRun WHERE coursename LIKE %?%";
			//sql = "SELECT DISTINCT(coursename) FROM LocalRun WHERE instr(coursename, ?) > 0 LIMIT 1";
			//sql = "SELECT coursename, MAX(entries) FROM LocalRun WHERE instr(coursename, ?) > 0 LIMIT 1";
			//sql = "SELECT DISTINCT(coursename) FROM LocalRun WHERE instr(coursename, ?) > 0 ORDER BY LENGTH(coursename) ASC, entries DESC LIMIT 1";
			sql = "SELECT DISTINCT(coursename) FROM LocalRun WHERE instr(coursename, ?) > 0 ORDER BY entries DESC LIMIT 1";
			CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
			CALL_SQLITE (bind_text (stmt, 1, courseName, -1, SQLITE_STATIC));
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				//Check if it actually has text, if not return.  then we can use cheaper (MAX) entries query above //loda fixme
				Q_strncpyz(courseNameFull, (char*)sqlite3_column_text(stmt, 0), sizeof(courseNameFull));
			}
			else {
				//Com_Printf("fail 4\n");
				trap->SendServerCommand(ent-g_entities, "print \"Usage: /rTop <course (if needed)> <style (optional)> <page (optional)>.  This displays the top10 for the specified course.\n\"");
				CALL_SQLITE (finalize(stmt));
				CALL_SQLITE (close(db));
				return;
			}
			CALL_SQLITE (finalize(stmt));

		}

		//Problem - crossmap query can return multiple records for same person since the cleanup cmd is only done on mapchange, 
		//fix by grouping by username here? and using min() so it shows right one? who knows if that will work
		//could be cheaper by using where rank != 0 instead of min(duration_ms) but w/e
		sql = "SELECT username, MIN(duration_ms) AS duration, topspeed, average, end_time FROM LocalRun WHERE coursename = ? AND style = ? GROUP BY username ORDER BY duration ASC, end_time ASC LIMIT ?, 10";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_text (stmt, 1, courseNameFull, -1, SQLITE_STATIC));
		CALL_SQLITE (bind_int (stmt, 2, style));
		CALL_SQLITE (bind_int (stmt, 3, start));

		time( &rawtime );
		localtime( &rawtime );

		trap->SendServerCommand(ent-g_entities, va("print \"Highscore results for %s using %s style:\n    ^5Username           Time         Topspeed    Average      Date\n\"", courseNameFull, styleString));
		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char *tmpMsg = NULL;
				TimeToString(sqlite3_column_int(stmt, 1), timeStr, sizeof(timeStr), qfalse);
				getDateTime(sqlite3_column_int(stmt, 4), dateStr, sizeof(dateStr));
				if (rawtime - sqlite3_column_int(stmt, 4) < 60*60*24) { //Today
					Com_sprintf(dateStrColored, sizeof(dateStrColored), "^2%s^7", dateStr);
				}
				else {
					Q_strncpyz(dateStrColored, dateStr, sizeof(dateStrColored));
				}
				tmpMsg = va("^5%2i^3: ^3%-18s ^3%-12s ^3%-11i ^3%-12i %s\n", row+start, sqlite3_column_text(stmt, 0), timeStr, sqlite3_column_int(stmt, 2), sqlite3_column_int(stmt, 3), dateStrColored);
				if (strlen(msg) + strlen(tmpMsg) >= sizeof( msg)) {
					trap->SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
					msg[0] = '\0';
				}
				Q_strcat(msg, sizeof(msg), tmpMsg);
				row++;
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				fprintf (stderr, "ERROR: SQL Select Failed (Cmd_DFTop10_f).\n");//Trap print?
				break;
			}
		}
		trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));

		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
	}
}
#endif

void Cmd_DFTodo_f(gentity_t *ent) {
	const int args = trap->Argc();
	int style = -1, page = -1, start = 0, i, input;
	char styleString[16] = {0}, inputString[32], partialCourseName[40], username[16];
	qboolean enteredCoursename = qfalse;

	if (!ent->client->pers.userName[0]) {
		trap->SendServerCommand(ent-g_entities, "print \"You must be logged in to use this command.\n\"");
		return;
	}
	Q_strncpyz(username, ent->client->pers.userName, sizeof(username));

	if (args > 4) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rWorst <map (optional)> <style (optional)> <page (optional)>\n\"");
		return;
	}

	for (i = 1 ; i < args; i++) {
		trap->Argv(i, inputString, sizeof(inputString));
		if (style == -1) {
			input = RaceNameToInteger(inputString);
			if (input != -1) {
				style = input;
				continue;
			}
		}
		if (page == -1) {
			input = atoi(inputString);
			if (input > 0) {
				page = input;
				continue;
			}
		}
		if (!enteredCoursename) {
			input = SeasonToInteger(inputString);
			Q_strncpyz(partialCourseName, inputString, sizeof(partialCourseName));
			enteredCoursename = qtrue;
			continue;
		}
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rWorst <map (optional)> <style (optional)> <page (optional)>\n\"");
		return; //Arg doesnt match any expected values so error.
	}

	if (style == -1) {
		Q_strncpyz(styleString, "all styles", sizeof(styleString));
	}
	else {
		IntegerToRaceName(style, styleString, sizeof(styleString));
		Q_strcat(styleString, sizeof(styleString), " style");
	}

	if (page < 1)
		page = 1;
	if (page > 1000)
		page = 1000;
	start = (page - 1) * 10;

	Q_strlwr(partialCourseName);
	Q_CleanStr(partialCourseName);

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		char msg[1024-128] = {0}, timeStr[64], dateStr[64], styleStr[16], rankStr[16];
		int s, row = 1;

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));
		if (style == -1) {
			if (enteredCoursename) {
				//sql = "SELECT coursename, style, rank, entries, duration_ms, end_time FROM LocalRun WHERE rank != 0 AND username = ? AND instr(coursename, ?) > 0 ORDER BY (entries - entries / rank) DESC LIMIT ?, 10";
				sql = "SELECT * FROM "
						"(SELECT coursename, style, rank, entries, duration_ms, end_time "
							"FROM LocalRun WHERE rank != 0 AND username = ? AND instr(coursename, ?) > 0 "
						"UNION ALL "
						"SELECT T1.coursename, T1.style, T1.entries AS rank, T1.entries, 0 AS duration_ms, 0 AS end_time "
							"FROM "
								"(SELECT coursename, style, entries FROM LocalRun WHERE instr(coursename, ?) > 0 GROUP BY coursename, style) T1 "
								"LEFT JOIN (SELECT coursename, style FROM LocalRun WHERE username = ? AND instr(coursename, ?) > 0 GROUP BY coursename, style) T2 "
								"ON T1.coursename = T2.coursename AND T1.style = T2.style "
							"WHERE T2.coursename IS NULL OR T2.style IS NULL) "	
					"ORDER BY (entries-((entries/cast(rank as float))+(entries-rank)/2.0)) DESC LIMIT ?, 10";
					CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
					CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_text (stmt, 2, partialCourseName, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_text (stmt, 3, partialCourseName, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_text (stmt, 4, username, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_text (stmt, 5, partialCourseName, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_int (stmt, 6, start));
			}
			else {
				//sql = "SELECT coursename, style, rank, entries, duration_ms, end_time FROM LocalRun WHERE rank != 0 AND username = ? ORDER BY (entries - entries / rank) DESC LIMIT ?, 10";
				sql = "SELECT * FROM "
						"(SELECT coursename, style, rank, entries, duration_ms, end_time "
							"FROM LocalRun WHERE rank != 0 AND username = ? "
						"UNION ALL "
						"SELECT T1.coursename, T1.style, T1.entries AS rank, T1.entries, 0 AS duration_ms, 0 AS end_time "
							"FROM "
								"(SELECT coursename, style, entries FROM LocalRun GROUP BY coursename, style) T1 "
								"LEFT JOIN (SELECT coursename, style FROM LocalRun WHERE username = ? GROUP BY coursename, style) T2 "
								"ON T1.coursename = T2.coursename AND T1.style = T2.style "
							"WHERE T2.coursename IS NULL OR T2.style IS NULL) "	
					"ORDER BY (entries-((entries/cast(rank as float))+(entries-rank)/2.0)) DESC LIMIT ?, 10";
					CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
					CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_text (stmt, 2, username, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_int (stmt, 3, start));
			}	
		}
		else {
			if (enteredCoursename) {
				//sql = "SELECT coursename, style, rank, entries, duration_ms, end_time FROM LocalRun WHERE rank != 0 AND username = ? AND style = ? AND instr(coursename, ?) > 0 ORDER BY (entries - entries / rank) DESC LIMIT ?, 10";
				sql = "SELECT * FROM "
						"(SELECT coursename, style, rank, entries, duration_ms, end_time "
							"FROM LocalRun WHERE rank != 0 AND username = ? AND style = ? AND instr(coursename, ?) > 0 "
						"UNION ALL "
						"SELECT T1.coursename, T1.style, T1.entries AS rank, T1.entries, 0 AS duration_ms, 0 AS end_time "
							"FROM "
								"(SELECT coursename, style, entries FROM LocalRun WHERE style = ? AND instr(coursename, ?) > 0 GROUP BY coursename, style) T1 "
								"LEFT JOIN (SELECT coursename, style FROM LocalRun WHERE username = ? AND style = ? AND instr(coursename, ?) > 0 GROUP BY coursename, style) T2 "
								"ON T1.coursename = T2.coursename AND T1.style = T2.style "
							"WHERE T2.coursename IS NULL OR T2.style IS NULL) "	
					"ORDER BY (entries-((entries/cast(rank as float))+(entries-rank)/2.0)) DESC LIMIT ?, 10";
					CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
					CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_int (stmt, 2, style));
					CALL_SQLITE (bind_text (stmt, 3, partialCourseName, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_int (stmt, 4, style));
					CALL_SQLITE (bind_text (stmt, 5, partialCourseName, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_text (stmt, 6, username, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_int (stmt, 7, style));
					CALL_SQLITE (bind_text (stmt, 8, partialCourseName, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_int (stmt, 9, start));
			}
			else {
				//sql = "SELECT coursename, style, rank, entries, duration_ms, end_time FROM LocalRun WHERE rank != 0 AND username = ? AND style = ? ORDER BY (entries - entries / rank) DESC LIMIT ?, 10";
				sql = "SELECT * FROM "
						"(SELECT coursename, style, rank, entries, duration_ms, end_time "
							"FROM LocalRun WHERE rank != 0 AND username = ? AND style = ?"
						"UNION ALL "
						"SELECT T1.coursename, T1.style, T1.entries AS rank, T1.entries, 0 AS duration_ms, 0 AS end_time "
							"FROM "
								"(SELECT coursename, style, entries FROM LocalRun WHERE style = ? GROUP BY coursename, style) T1 "
								"LEFT JOIN (SELECT coursename, style FROM LocalRun WHERE username = ? AND style = ? GROUP BY coursename, style) T2 "
								"ON T1.coursename = T2.coursename AND T1.style = T2.style "
							"WHERE T2.coursename IS NULL OR T2.style IS NULL) "	
					"ORDER BY (entries-((entries/cast(rank as float))+(entries-rank)/2.0)) DESC LIMIT ?, 10";
					CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
					CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_int (stmt, 2, style));
					CALL_SQLITE (bind_int (stmt, 3, style));
					CALL_SQLITE (bind_text (stmt, 4, username, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_int (stmt, 5, style));
					CALL_SQLITE (bind_int (stmt, 6, start));
			}
		}

		trap->SendServerCommand(ent-g_entities, va("print \"Most improvable scores for %s:\n    ^5Course                      Style      Rank    Entries      Time         Date\n\"", styleString)); //Color rank yellow for global, normal for season -fixme match race print scheme
		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char *tmpMsg = NULL;
				IntegerToRaceName(sqlite3_column_int(stmt, 1), styleStr, sizeof(styleStr));
				if (sqlite3_column_int(stmt, 5)) {
					Q_strncpyz(rankStr, va("%i", sqlite3_column_int(stmt, 2)), sizeof(rankStr));
					TimeToString(sqlite3_column_int(stmt, 4), timeStr, sizeof(timeStr));
					getDateTime(sqlite3_column_int(stmt, 5), dateStr, sizeof(dateStr)); 
				}
				else {
					Q_strncpyz(rankStr, "N/A", sizeof(rankStr));
					Q_strncpyz(timeStr, "N/A", sizeof(timeStr));
					Q_strncpyz(dateStr, "N/A", sizeof(dateStr));
				}

				tmpMsg = va("^5%2i^3: ^3%-27s ^3%-10s ^3%-7s ^3%-12i ^3%-12s %s\n", row+start, sqlite3_column_text(stmt, 0), styleStr, rankStr, sqlite3_column_int(stmt, 3), timeStr, dateStr);
				if (strlen(msg) + strlen(tmpMsg) >= sizeof( msg)) {
					trap->SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
					msg[0] = '\0';
				}
				Q_strcat(msg, sizeof(msg), tmpMsg);
				row++;
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_DFTodo_f)", s);
				break;
			}
		}
		trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));
		CALL_SQLITE (finalize(stmt));

		CALL_SQLITE (close(db));
	}

}

void Cmd_DFPopular_f(gentity_t *ent) {
	const int args = trap->Argc();
	int style = -1, page = -1, season = -1, start = 0, i, input;
	char styleString[16] = {0}, inputString[32], username[16];
	qboolean enteredUsername = qfalse;

	if (args > 5) {
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rPopular <style (optional)> <season (optional - example: s1)> <me (optional)> <page (optional)>\n\"");
		return;
	}

	for (i = 1 ; i < args; i++) {
		trap->Argv(i, inputString, sizeof(inputString));
		if (style == -1) {
			input = RaceNameToInteger(inputString);
			if (input != -1) {
				style = input;
				continue;
			}
		}
		if (season == -1) {
			input = SeasonToInteger(inputString);
			if (input != -1) {
				season = input;
				continue;
			}
		}
		if (page == -1) {
			input = atoi(inputString);
			if (input > 0) {
				page = input;
				continue;
			}
		}
		if (!enteredUsername) {
			if (!Q_stricmp(inputString, "me")) {
				Q_strncpyz(username, ent->client->pers.userName, sizeof(username));
				enteredUsername = qtrue;
				continue;
			}
		}
		trap->SendServerCommand(ent-g_entities, "print \"Usage: /rPopular <style (optional)> <season (optional - example: s1)> <me (optional)> <page (optional)>\n\"");
		return; //Arg doesnt match any expected values so error.
	}

	if (enteredUsername && (!ent->client->pers.userName[0])) {
		trap->SendServerCommand(ent-g_entities, "print \"You must be logged in to use this command.\n\"");
		return;
	}

	if (style == -1) {
		Q_strncpyz(styleString, "all styles", sizeof(styleString));
	}
	else {
		IntegerToRaceName(style, styleString, sizeof(styleString));
		Q_strcat(styleString, sizeof(styleString), " style");
	}

	if (page < 1)
		page = 1;
	if (page > 1000)
		page = 1000;
	start = (page - 1) * 10;

	{
		sqlite3 * db;
		char * sql;
		sqlite3_stmt * stmt;
		char msg[1024-128] = {0}, styleStr[16], entriesStr[16];
		int s, row = 1;

		CALL_SQLITE (open (LOCAL_DB_PATH, & db));
		if (style == -1) {
			if (enteredUsername) {
				if (season == -1) { //User
					sql = "SELECT T1.coursename, T1.style, T1.entries, T1.username "
								"FROM (SELECT coursename, style, entries, username FROM LocalRun WHERE rank = 1 GROUP BY coursename, style) T1 "
									"LEFT JOIN (SELECT coursename, style FROM LocalRun WHERE username = ? GROUP BY coursename, style) T2 "
									"ON T1.coursename = T2.coursename AND T1.style = T2.style "
								"WHERE T2.coursename IS NULL OR T2.style IS NULL "
					"ORDER BY entries DESC LIMIT ?, 10";
					CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
					CALL_SQLITE (bind_text (stmt, 1, username, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_int (stmt, 2, start));
				}
				else { //User, season
					sql = "SELECT T1.coursename, T1.style, T1.season_entries, T1.username "
								"FROM (SELECT coursename, style, season_entries, username FROM LocalRun WHERE season_rank = 1 AND season = ? GROUP BY coursename, style) T1 "
									"LEFT JOIN (SELECT coursename, style FROM LocalRun WHERE season = ? AND username = ? GROUP BY coursename, style) T2 "
									"ON T1.coursename = T2.coursename AND T1.style = T2.style "
								"WHERE T2.coursename IS NULL OR T2.style IS NULL "
					"ORDER BY season_entries DESC LIMIT ?, 10";
					CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
					CALL_SQLITE (bind_int (stmt, 1, season));
					CALL_SQLITE (bind_int (stmt, 2, season));
					CALL_SQLITE (bind_text (stmt, 3, username, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_int (stmt, 4, start));
				}
			}
			else {
				if (season == -1) {
					sql = "SELECT coursename, style, entries, username FROM LocalRun WHERE rank = 1 GROUP BY coursename, style ORDER BY entries DESC LIMIT ?, 10";
					CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
					CALL_SQLITE (bind_int (stmt, 1, start));
				}
				else { //Season
					sql = "SELECT coursename, style, season_entries, username FROM LocalRun WHERE season_rank = 1 AND season = ? GROUP BY coursename, style ORDER BY season_entries DESC LIMIT ?, 10";
					CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
					CALL_SQLITE (bind_int (stmt, 1, season));
					CALL_SQLITE (bind_int (stmt, 2, start));
				}
			}	
		}
		else {
			if (enteredUsername) {
				if (season == -1) { //Style, user
					sql = "SELECT T1.coursename, T1.style, T1.entries, T1.username "
								"FROM (SELECT coursename, style, entries FROM LocalRun WHERE style = ? GROUP BY coursename, style) T1 "
									"LEFT JOIN (SELECT coursename, style FROM LocalRun WHERE style = ? AND username = ? GROUP BY coursename, style) T2 "
									"ON T1.coursename = T2.coursename AND T1.style = T2.style "
								"WHERE T2.coursename IS NULL OR T2.style IS NULL "
					"ORDER BY entries DESC LIMIT ?, 10";
					CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
					CALL_SQLITE (bind_int (stmt, 1, style));
					CALL_SQLITE (bind_int (stmt, 2, style));
					CALL_SQLITE (bind_text (stmt, 3, username, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_int (stmt, 4, start));
				}
				else { //Style, user, season
					sql = "SELECT T1.coursename, T1.style, T1.season_entries, T1.username "
								"FROM (SELECT coursename, style, season_entries, username FROM LocalRun WHERE season_rank = 1 AND style = ? AND season = ? GROUP BY coursename, style) T1 "
									"LEFT JOIN (SELECT coursename, style FROM LocalRun WHERE style = ? AND season = ? AND username = ? GROUP BY coursename, style) T2 "
									"ON T1.coursename = T2.coursename AND T1.style = T2.style "
								"WHERE T2.coursename IS NULL OR T2.style IS NULL "
					"ORDER BY season_entries DESC LIMIT ?, 10";
					CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
					CALL_SQLITE (bind_int (stmt, 1, style));
					CALL_SQLITE (bind_int (stmt, 2, season));
					CALL_SQLITE (bind_int (stmt, 3, style));
					CALL_SQLITE (bind_int (stmt, 4, season));
					CALL_SQLITE (bind_text (stmt, 5, username, -1, SQLITE_STATIC));
					CALL_SQLITE (bind_int (stmt, 6, start));
				}
			}
			else {
				if (season == -1) { //Style
					sql = "SELECT coursename, style, entries, username FROM LocalRun WHERE rank = 1 AND style = ? GROUP BY coursename, style ORDER BY entries DESC LIMIT ?, 10";
					CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
					CALL_SQLITE (bind_int (stmt, 1, style));
					CALL_SQLITE (bind_int (stmt, 2, start));
				}
				else { //Style, season
					sql = "SELECT coursename, style, season_entries, username FROM LocalRun WHERE season_rank = 1 AND style = ? AND season = ? GROUP BY coursename, style ORDER BY season_entries DESC LIMIT ?, 10";
					CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
					CALL_SQLITE (bind_int (stmt, 1, style));
					CALL_SQLITE (bind_int (stmt, 2, season));
					CALL_SQLITE (bind_int (stmt, 3, start));
				}
			}
		}

		if (season == -1)
			trap->SendServerCommand(ent-g_entities, va("print \"Most popular courses for %s:\n    ^5Course                      Style      Entries      Winner\n\"", styleString));
		else
			trap->SendServerCommand(ent-g_entities, va("print \"Most popular courses for %s season %i:\n    ^5Course                      Style      Entries      Winner\n\"", styleString, season));

		while (1) {
			s = sqlite3_step(stmt);
			if (s == SQLITE_ROW) {
				char *tmpMsg = NULL;
				IntegerToRaceName(sqlite3_column_int(stmt, 1), styleStr, sizeof(styleStr));
				Com_sprintf(entriesStr, sizeof(entriesStr), "%i", sqlite3_column_int(stmt, 2));

				tmpMsg = va("^5%2i^3: ^3%-27s ^3%-10s ^3%-12s %s\n", row+start, sqlite3_column_text(stmt, 0), styleStr, entriesStr, sqlite3_column_text(stmt, 3));
				if (strlen(msg) + strlen(tmpMsg) >= sizeof( msg)) {
					trap->SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
					msg[0] = '\0';
				}
				Q_strcat(msg, sizeof(msg), tmpMsg);
				row++;
			}
			else if (s == SQLITE_DONE)
				break;
			else {
				G_ErrorPrint("ERROR: SQL Select Failed (Cmd_DFTodo_f)", s);
				break;
			}
		}
		trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));
		CALL_SQLITE (finalize(stmt));

		CALL_SQLITE (close(db));
	}

}

#if !_NEWRACERANKING
void Cmd_DFRefresh_f(gentity_t *ent) {
	if (ent->client && ent->client->sess.fullAdmin) {//Logged in as full admin
		if (!(g_fullAdminLevel.integer & (1 << A_BUILDHIGHSCORES))) {
			trap->SendServerCommand( ent-g_entities, "print \"You are not authorized to use this command (dfRefresh).\n\"" );
			return;
		}
	}
	else if (ent->client && ent->client->sess.juniorAdmin) {//Logged in as junior admin
		if (!(g_juniorAdminLevel.integer & (1 << A_BUILDHIGHSCORES))) {
			trap->SendServerCommand( ent-g_entities, "print \"You are not authorized to use this command (dfRefresh).\n\"" );
			return;
		}
	}
	else {//Not logged in
		trap->SendServerCommand( ent-g_entities, "print \"You must be logged in to use this command (dfRefresh).\n\"" );
		return;
	}
	G_AddToDBFromFile(); //From file to db
	//BuildMapHighscores(); //From db, built to memory
}
#endif

int JP_ClientNumberFromString(gentity_t *to, const char *s);
void Cmd_ACWhois_f( gentity_t *ent ) { //why does this crash sometimes..? conditional open/close issue??
	int			i;
	char		msg[1024-128] = {0};
	gclient_t	*cl;
	qboolean	whois = qfalse, seeip = qfalse;
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	int s;
	int clientnum = -1;

	if (trap->Argc() == 2) {
		char arg1[16] = { 0 };
		trap->Argv(1, arg1, sizeof(arg1));

		if (!Q_stricmp(arg1, "follow")) {
			if (ent->client->sess.sessionTeam == TEAM_SPECTATOR && (ent->client->ps.pm_flags & PMF_FOLLOW)) {
				clientnum = ent->client->ps.clientNum;
			}
		}
		else {
			clientnum = JP_ClientNumberFromString( ent, arg1 );
		}
	}
	else if (trap->Argc() != 1) {
		trap->SendServerCommand(ent - g_entities, "print \"Usage: whois <follow (optional)>\n\"");
		return;
	}

	if (G_AdminAllowed(ent, JAPRO_ACCOUNTFLAG_A_WHOIS, qfalse, qfalse, NULL))
		whois = qtrue;
	if (G_AdminAllowed(ent, JAPRO_ACCOUNTFLAG_A_SEEIP, qfalse, qfalse, NULL))
		seeip = qtrue;
	
	if (whois) {
		CALL_SQLITE (open (LOCAL_DB_PATH, & db));
		sql = "SELECT username FROM LocalAccount WHERE lastip = ?";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	}

	if (whois && seeip) {
		if (g_raceMode.integer)
			trap->SendServerCommand(ent-g_entities, "print \"^5   Username            IP                Plugin  Admin   Race  Style    Jump  Hidden  Nickname\n\"");
		else
			trap->SendServerCommand(ent-g_entities, "print \"^5   Username            IP                Plugin  Admin   Nickname\n\"");
	}
	else if (whois) {
		if (g_raceMode.integer)
			trap->SendServerCommand(ent-g_entities, "print \"^5   Username            Plugin  Admin   Race  Style    Jump  Hidden  Nickname\n\"");
		else
			trap->SendServerCommand(ent-g_entities, "print \"^5   Username            Plugin  Admin   Nickname\n\"");
	}
	else {
		if (g_raceMode.integer)
			trap->SendServerCommand(ent-g_entities, "print \"^5   Username            Plugin  Race  Style    Jump  Hidden  Nickname\n\"");
		else
			trap->SendServerCommand(ent-g_entities, "print \"^5   Username            Plugin  Nickname\n\"");
	}

	for (i=0; i<MAX_CLIENTS; i++) {//Build a list of clients
		char *tmpMsg = NULL;
		if (!g_entities[i].inuse)
			continue;
		cl = &level.clients[i];
		if (cl->pers.netname[0]) { // && cl->pers.userName[0] ?
			char strNum[12] = {0};
			char strName[MAX_NETNAME] = {0};
			char strUser[20] = {0};
			char strIP[NET_ADDRSTRMAXLEN] = {0};
			char strAdmin[32] = {0};
			char strPlugin[32] = {0};
			char strRace[32] = {0};
			char strHidden[32] = {0};
			char strStyle[32] = {0};
			char jumpLevel[32] = {0};
			char *p = NULL;

			if (clientnum != -1 && (i != clientnum))
				continue;

			Q_strncpyz(strNum, va("^5%2i^3:", i), sizeof(strNum));
			Q_strncpyz(strName, cl->pers.netname, sizeof(strName));
			Com_sprintf(strUser, sizeof(strUser), "^7%s^7", cl->pers.userName);
			Q_strncpyz(strIP, cl->sess.IP, sizeof(strIP));

			if (cl->sess.sessionTeam != TEAM_SPECTATOR)
				Q_strncpyz(jumpLevel, va("%i", cl->ps.fd.forcePowerLevel[FP_LEVITATION]), sizeof(jumpLevel));

			if (whois || seeip) {
				p = strchr(strIP, ':');
				if (p) //loda - fix ip sometimes not printing in amstatus?
					*p = 0;
			}
			if (whois) {
				if (cl->sess.accountFlags == g_juniorAdminLevel.integer)
					Q_strncpyz(strAdmin, "^3Junior^7", sizeof(strAdmin));
				else if (cl->sess.accountFlags == g_fullAdminLevel.integer)
					Q_strncpyz( strAdmin, "^3Full^7", sizeof(strAdmin));
				else if (cl->sess.accountFlags & JAPRO_ACCOUNTFLAG_A_READAMSAY) //Damn this, how do we get it to ignore non admin account flags
					Q_strncpyz(strAdmin, "^3Custom^7", sizeof(strAdmin));
				else
					Q_strncpyz(strAdmin, "^7None^7", sizeof(strAdmin));
			}

			if (g_raceMode.integer) {
				if (cl->sess.sessionTeam == TEAM_SPECTATOR) {
					Q_strncpyz(strStyle, "^7^7", sizeof(strStyle));
					Q_strncpyz(strRace, "^7^7", sizeof(strRace));
					Q_strncpyz(strHidden, "^7^7", sizeof(strHidden));
				}
				else {
					char strStyleName[16] = {0};
					Q_strncpyz(strRace, (cl->sess.raceMode) ? "^2Yes^7" : "^1No^7", sizeof(strRace));
					Q_strncpyz(strHidden, (cl->pers.noFollow) ? "^2Yes^7" : "^1No^7", sizeof(strHidden));

					Q_strncpyz(strStyle, "^7", sizeof(strStyle));
					IntegerToRaceName(cl->ps.stats[STAT_MOVEMENTSTYLE],strStyleName, sizeof(strStyleName));
					Q_strcat(strStyle, sizeof(strStyle), va("%s^7", strStyleName));
				}
			}

			if (g_entities[i].r.svFlags & SVF_BOT)
				Q_strncpyz(strPlugin, "^7Bot^7", sizeof(strPlugin));
			else
				Q_strncpyz(strPlugin, (cl->pers.isJAPRO) ? "^2Yes^7" : "^1No^7", sizeof(strPlugin));

			if (whois) { //No username means not logged in, so check if they have an account tied to their ip
				if (!cl->pers.userName[0]) {
					unsigned int ip;

					ip = ip_to_int(strIP);

					CALL_SQLITE (bind_int64 (stmt, 1, ip));

					s = sqlite3_step(stmt);

					if (s == SQLITE_ROW) {
						if (ip)
							//Q_strncpyz(strUser, (char*)sqlite3_column_text(stmt, 0), sizeof(strUser));
							Com_sprintf(strUser, sizeof(strUser), "^3%s^7", (char*)sqlite3_column_text(stmt, 0));
					}
					else if (s != SQLITE_DONE) {
						G_ErrorPrint("ERROR: SQL Select Failed (Cmd_ACWhois_f)", s);
						CALL_SQLITE (finalize(stmt));
						CALL_SQLITE (close(db));
						return;
					}

					CALL_SQLITE (reset (stmt));
					CALL_SQLITE (clear_bindings (stmt));
				}

				if (seeip) {
					//Admin prints
					if (g_raceMode.integer)
						tmpMsg = va( "%-2s%-24s%-18s%-12s%-12s%-10s%-13s%-6s%-12s%s\n", strNum, strUser, strIP, strPlugin, strAdmin, strRace, strStyle, jumpLevel, strHidden, strName);
					else
						tmpMsg = va( "%-2s%-24s%-18s%-12s%-12s%s\n", strNum, strUser, strIP, strPlugin, strAdmin, strName);
				}
				else {
					//Admin prints
					if (g_raceMode.integer)
						tmpMsg = va( "%-2s%-24s%-12s%-12s%-10s%-13s%-6s%-12s%s\n", strNum, strUser, strPlugin, strAdmin, strRace, strStyle, jumpLevel, strHidden, strName);
					else
						tmpMsg = va( "%-2s%-24s%-12s%-12s%s\n", strNum, strUser, strPlugin, strAdmin, strName);
				}
			}
			else {//Not admin
				if (g_raceMode.integer)
					tmpMsg = va( "%-2s%-24s%-12s%-10s%-13s%-6s%-12s%s\n", strNum, strUser, strPlugin, strRace, strStyle, jumpLevel, strHidden, strName);
				else
					tmpMsg = va( "%-2s%-24s%-12s%s\n", strNum, strUser, strPlugin, strName);
			}
			
			if (strlen(msg) + strlen(tmpMsg) >= sizeof( msg)) {
				trap->SendServerCommand( ent-g_entities, va("print \"%s\"", msg));
				msg[0] = '\0';
			}
			Q_strcat(msg, sizeof(msg), tmpMsg);
		}
	}

	if (whois) {
		CALL_SQLITE (finalize(stmt));
		CALL_SQLITE (close(db));
	}

	trap->SendServerCommand(ent-g_entities, va("print \"%s\"", msg));

	//DebugWriteToDB("Cmd_ACWhois_f");
}

void InitGameAccountStuff( void ) { //Called every mapload , move the create table stuff to something that gets called every srvr start.. eh?
	sqlite3 * db;
    char * sql;
    sqlite3_stmt * stmt;
	int s;

	memset(g_trackedDuels, 0, sizeof(g_trackedDuels));
	memset(g_botTutorialQueues, 0, sizeof(g_botTutorialQueues));
	memset(g_duelAdviceSessions, 0, sizeof(g_duelAdviceSessions));

	//ok build DB file path from fs_game and fs_homepath
	char fs_game[MAX_QPATH];
	char fs_homepath[MAX_OSPATH];
	trap->Cvar_VariableStringBuffer("fs_game", fs_game, sizeof(fs_game));
	if (!VALIDSTRING(fs_game)) {
		trap->Cvar_VariableStringBuffer("fs_basegame", fs_game, sizeof(fs_game));

		if (!VALIDSTRING(fs_game)) {
			Q_strncpyz(fs_game, TAYSTJKGAME, sizeof(fs_game)); //fall back to this i guess
		}
	}

	trap->Cvar_VariableStringBuffer("fs_homepath", fs_homepath, sizeof(fs_homepath));
	if (VALIDSTRING(fs_homepath)) {
		Com_sprintf(LOCAL_DB_PATH, sizeof(LOCAL_DB_PATH), "%s/%s/data.db", fs_homepath, fs_game);
	} else {
		Com_sprintf(LOCAL_DB_PATH, sizeof(LOCAL_DB_PATH), "%s/data.db", fs_game);
	}
	g_duelTrackingSchemaReady = qfalse;
	g_duelTrackingSchemaPath[0] = '\0';

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
	G_EnsureLocalArcadeSchema(db);
	if (bot_dueltracking.integer)
		G_EnsureLocalDuelTrackingSchema(db);

	//sqlite_exec(db, "VACUUM;", 0, 0);
	//index LocalRun on RANK
	//use transactions
	//COUNT_CHANGES off

	//Create localrun index ?

	sql = "CREATE TABLE IF NOT EXISTS LocalAccount(id INTEGER PRIMARY KEY, username VARCHAR(16), password VARCHAR(16), kills UNSIGNED SMALLINT, deaths UNSIGNED SMALLINT, "
		"suicides UNSIGNED SMALLINT, captures UNSIGNED SMALLINT, returns UNSIGNED SMALLINT, racetime UNSIGNED INTEGER, lastlogin UNSIGNED INTEGER, created UNSIGNED INTEGER, lastip UNSIGNED INTEGER, flags UNSIGNED INTEGER, unlocks UNSIGNED INTEGER, master VARCHAR(16))";
    CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (InitGameAccountStuff 1)", s);
	CALL_SQLITE (finalize(stmt));

#if 1//NEWRACERANKING
	sql = "CREATE TABLE IF NOT EXISTS LocalRun(id INTEGER PRIMARY KEY, username VARCHAR(16), coursename VARCHAR(40), duration_ms UNSIGNED INTEGER, topspeed UNSIGNED SMALLINT, "
		"average UNSIGNED SMALLINT, style UNSIGNED TINYINT, season UNSIGNED TINYINT, end_time UNSIGNED INTEGER, rank UNSIGNED SMALLINT, entries UNSIGNED SMALLINT, season_rank UNSIGNED SMALLINT, season_entries UNSIGNED SMALLINT, last_update UNSIGNED INTEGER, invalid UNSIGNED TINYINT, checkpoints TEXT)";
#else
	sql = "CREATE TABLE IF NOT EXISTS LocalRun(id INTEGER PRIMARY KEY, username VARCHAR(16), coursename VARCHAR(40), duration_ms UNSIGNED INTEGER, topspeed UNSIGNED SMALLINT, "
		"average UNSIGNED SMALLINT, style UNSIGNED TINYINT, end_time UNSIGNED INTEGER, checkpoints TEXT)";
#endif
    CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (InitGameAccountStuff 2)", s);
	CALL_SQLITE (finalize(stmt));

	sql = "CREATE TABLE IF NOT EXISTS LocalDuel(id INTEGER PRIMARY KEY, winner VARCHAR(16), loser VARCHAR(16), duration UNSIGNED SMALLINT, "
		"type UNSIGNED TINYINT, winner_hp UNSIGNED TINYINT, winner_shield UNSIGNED TINYINT, end_time UNSIGNED INTEGER, winner_elo DECIMAL(6,2), loser_elo DECIMAL(6,2), odds DECIMAL(9,2))";
    CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (InitGameAccountStuff 3)", s);
	CALL_SQLITE (finalize(stmt));

	G_EnsureLocalArcadeSchema(db);

	sql = "CREATE TABLE IF NOT EXISTS LocalTeam(id INTEGER PRIMARY KEY, name VARCHAR(16), tag VARCHAR(16), longname VARCHAR(24), flags UNSIGNED TINYINT)";
    CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (InitGameAccountStuff 4)", s);
	CALL_SQLITE (finalize(stmt));

	sql = "CREATE TABLE IF NOT EXISTS LocalTeamAccount(id INTEGER PRIMARY KEY, team VARCHAR(16), account VARCHAR(16), flags UNSIGNED TINYINT)";
    CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (InitGameAccountStuff 5)", s);
	CALL_SQLITE (finalize(stmt));

#if _ELORANKING
	/*
	sql = "CREATE TABLE IF NOT EXISTS DuelRanks(id INTEGER PRIMARY KEY, username VARCHAR(16), type UNSIGNED SMALLINT, rank DECIMAL(6,2), TSSUM DECIMAL(9,2), count UNSIGNED INTEGER)"; //We only need like 2 decimal precision here so how do that in sqlite C? --todo
    CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE_EXPECT (step (stmt), DONE);
	CALL_SQLITE (finalize(stmt));
	*/
#endif

#if 0//NEWRACERANKING
	sql = "CREATE TABLE IF NOT EXISTS RaceRanks(id INTEGER PRIMARY KEY, username VARCHAR(16), style UNSIGNED SMALLINT, score DECIMAL(6,2), percentilesum DECIMAL(6,2), ranksum DECIMAL(6,2), golds UNSIGNED SMALLINT, silvers UNSIGNED SMALLINT, bronzes UNSIGNED SMALLINT, count UNSIGNED SMALLINT)"; //We only need like 2 decimal precision here so how do that in sqlite C? --todo
    CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE_EXPECT (step (stmt), DONE);
	CALL_SQLITE (finalize(stmt));
#endif

	sql = "CREATE INDEX IF NOT EXISTS idx_localrun_username ON LocalRun(username)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (InitGameAccountStuff idx_localrun_username)", s);
	CALL_SQLITE(finalize(stmt));

	sql = "CREATE INDEX IF NOT EXISTS idx_localrun_coursename ON LocalRun(coursename)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (InitGameAccountStuff idx_localrun_coursename)", s);
	CALL_SQLITE(finalize(stmt));

	sql = "CREATE INDEX IF NOT EXISTS idx_localrun_course_style ON LocalRun(coursename, style)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (InitGameAccountStuff idx_localrun_course_style)", s);
	CALL_SQLITE(finalize(stmt));

	sql = "CREATE INDEX IF NOT EXISTS idx_localrun_lastupdate ON LocalRun(last_update)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (InitGameAccountStuff idx_localrun_lastupdate)", s);
	CALL_SQLITE(finalize(stmt));

	sql = "CREATE INDEX IF NOT EXISTS idx_localrun_user_course_style ON LocalRun(username, coursename, style)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
		G_ErrorPrint("ERROR: SQL Create Failed (InitGameAccountStuff idx_localrun_user_course_style)", s);
	CALL_SQLITE(finalize(stmt));

	CALL_SQLITE (close(db));

	CleanupLocalRun(); //Deletes useless shit from LocalRun database table
#if !_NEWRACERANKING
	G_AddToDBFromFile(); //Add last maps highscores
#endif
	//BuildMapHighscores();//Build highscores into memory from database

	//DebugWriteToDB("InitGameAccountStuff");
}

void G_SpawnWarpLocationsFromCfg(void) //loda fixme
{
	fileHandle_t f;	
	int		fLen = 0, i, MAX_FILESIZE = 4096, MAX_NUM_WARPS = 72, args = 1, row = 0;  //use max num warps idk
	char	filename[MAX_QPATH+4] = {0}, info[1024] = {0}, buf[4096] = {0};//eh
	char*	pch;

	trap->GetServerinfo(info, sizeof(info));
	Q_strncpyz(filename, Info_ValueForKey(info, "mapname"), sizeof(filename));
	Q_strlwr(filename);//dat linux
	Q_strcat(filename, sizeof(filename), "_warps.cfg");

	for(i = 0; i < strlen(filename); i++) {//Replace / in mapname with _ since we cant have a file named mp/duel1.cfg etc.
		if (filename[i] == '/')
			filename[i] = '_'; 
	} 

	fLen = trap->FS_Open(filename, &f, FS_READ);

	if (!f) {
		//Com_Printf ("Couldn't load tele locations from %s\n", filename);
		return;
	}
	if (fLen >= MAX_FILESIZE) {
		trap->FS_Close(f);
		Com_Printf ("Couldn't load tele locations from %s, file is too large\n", filename);
		return;
	}

	trap->FS_Read(buf, fLen, f);
	buf[fLen] = 0;
	trap->FS_Close(f);

	pch = strtok (buf," \n\t");  //loda fixme why is this broken
	while (pch != NULL && row < MAX_NUM_WARPS)
	{
		if ((args % 5) == 1)
			Q_strncpyz(warpList[row].name, pch, sizeof(warpList[row].name));
		else if ((args % 5) == 2)
			warpList[row].x = atoi(pch);
		else if ((args % 5) == 3)
			warpList[row].y = atoi(pch);
		else if ((args % 5) == 4)
			warpList[row].z = atoi(pch);
		else if ((args % 5) == 0) {
			warpList[row].yaw = atoi(pch);
			//trap->Print("Warp added: %s, <%i, %i, %i, %i>\n", warpList[row].name, warpList[row].x, warpList[row].y, warpList[row].z, warpList[row].yaw);
			row++;
		}
    	pch = strtok (NULL, " \n\t");
		args++;
	}

	Com_Printf ("Loaded warp locations from %s\n", filename);
}


void G_SpawnCapRoutesFromCFG(void) {
	fileHandle_t f;
	int		fLen = 0, routeNum, numRedRoutes = 0, i = 0; //use max num warps idk
	const int MAX_NUM_ITEMS = 100000, MAX_ROUTES_PER_TEAM = 6;
	char	fileName[MAX_QPATH], buf[512 * 1024] = { 0 }, mapname[40], info[1024] = { 0 };//eh
	char*	pch;
	ivec3_t spot = { 0 };


	//float radius;


	trap->GetServerinfo(info, sizeof(info));
	Q_strncpyz(mapname, Info_ValueForKey(info, "mapname"), sizeof(mapname));
	Q_strlwr(mapname);//dat linux

	for (i = 0; i < strlen(mapname); i++) {//Replace / in mapname with _ since we cant have a file named mp/duel1.cfg etc.
		if (mapname[i] == '/')
			mapname[i] = '_';
	}


	//Com_Printf("^5Mapname is %s\n", mapname);

	//Loop through max # of route options
	for (routeNum = 0; routeNum < MAX_ROUTES_PER_TEAM; routeNum++) {
		int args = 1, row = 0;
		Com_sprintf(fileName, sizeof(fileName), "caproutes/%s_b_%i.cfg", mapname, routeNum+1); //mapname
		//Com_Printf("^5Filename blue is %s\n", fileName);
																			

		fLen = trap->FS_Open(fileName, &f, FS_READ);

		if (!f) {
			//Com_Printf("Couldn't load path locations from %s\n", fileName); //not needed?
			break;
		}
		if (fLen >= sizeof(buf)) {
			trap->FS_Close(f);
			Com_Printf("Couldn't load path locations from %s, file is too large\n", fileName);
			continue;
		}

		trap->FS_Read(buf, fLen, f);
		buf[fLen] = 0;
		trap->FS_Close(f);


		pch = strtok(buf, " \n\t");  //loda fixme why is this broken
		while (pch != NULL && row < MAX_NUM_ITEMS)
		{
			if ((args % 3) == 1)
				spot[0] = atoi(pch);
			else if ((args % 3) == 2)
				spot[1] = atoi(pch);
			else if ((args % 3) == 0) {
				spot[2] = atoi(pch);

				blueRouteList[routeNum].pos[row][0] = spot[0];
				blueRouteList[routeNum].pos[row][1] = spot[1];
				blueRouteList[routeNum].pos[row][2] = spot[2];
				//trap->Print("^5Blue Route spot added: route %i row %i [%i %i %i]\n", routeNum, row, blueRouteList[routeNum].pos[row][0], blueRouteList[routeNum].pos[row][1], blueRouteList[routeNum].pos[row][2]);
				row++;
			}
			pch = strtok(NULL, " \n\t");
			args++;

			//Routes named like raindance_1_r.cfg or raindance_2_b.cfg.  6 per team max? or 12 max?  lets do 12 max

			//put them in memory I guess ? If tribes mode ? (latched rquirement ? )
		}
		blueRouteList[routeNum].length = row;

	}

	numRedRoutes = routeNum;

	//Loop through max # of route options
	for (routeNum = 0; routeNum < MAX_ROUTES_PER_TEAM; routeNum++) {
		int args = 1, row = 0;
		Com_sprintf(fileName, sizeof(fileName), "caproutes/%s_r_%i.cfg", mapname, routeNum+1); //mapname


		fLen = trap->FS_Open(fileName, &f, FS_READ);

		if (!f) {
			//Com_Printf("Couldn't load route path from %s\n", fileName); //not needed?
			break;
		}
		if (fLen >= sizeof(buf)) {
			trap->FS_Close(f);
			Com_Printf("Couldn't load route path from %s, file is too large\n", fileName);
			continue;
		}

		trap->FS_Read(buf, fLen, f);
		buf[fLen] = 0;
		trap->FS_Close(f);


		pch = strtok(buf, " \n\t");  //loda fixme why is this broken
		while (pch != NULL && row < MAX_NUM_ITEMS)
		{
			if ((args % 3) == 1)
				spot[0] = atoi(pch);
			else if ((args % 3) == 2)
				spot[1] = atoi(pch);
			else if ((args % 3) == 0) {
				spot[2] = atoi(pch);

				redRouteList[routeNum].pos[row][0] = spot[0];
				redRouteList[routeNum].pos[row][1] = spot[1];
				redRouteList[routeNum].pos[row][2] = spot[2];
				//trap->Print("^5Red Route spot added: route %i row %i [%i %i %i]\n", routeNum, row, spot[0], spot[1], spot[2]);

				row++;
			}
			pch = strtok(NULL, " \n\t");
			args++;

			//Routes named like raindance_1_r.cfg or raindance_2_b.cfg.  6 per team max? or 12 max?  lets do 12 max

			//put them in memory I guess ? If tribes mode ? (latched rquirement ? )

		}
		redRouteList[routeNum].length = row;
	}

		Com_Printf("Loaded %i red cap routes and %i blue cap routes from files\n", numRedRoutes, routeNum);
}

#if 0
void AddRunToWebServer(RaceRecord_t record) 
{ 

	//fetch_response();

	CURL *curl;
	char address[128], data[256], password[64];
	CURLcode res;


	Q_strncpyz(address, sv_webServerPath.string, sizeof(address));
	Q_strncpyz(password, sv_webServerPassword.string, sizeof(password));

	//Case, special chars matter? clean??  Encode coursename / username for html ?


	Q_strncpyz(record.username, "testuser", sizeof(record.username));
	Q_strncpyz(record.coursename, "testcourse", sizeof(record.coursename));
	record.duration_ms = 123456;
	record.topspeed = 835;
	record.average = 652;
	record.style = 3;
	record.end_timeInt = 26246234;


	Com_sprintf(data, sizeof(data), "username=%s&coursename=%s&duration_ms=%i&topspeed=%i&average=%i&style=%i&end_time=%i", 
		record.username, record.coursename, record.duration_ms, record.topspeed, record.average, record.style, record.end_time);

	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();
	if (curl) {
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
		curl_easy_setopt(curl, CURLOPT_URL, address);
		curl_easy_setopt(curl, CURLOPT_POST, 1);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
		res = curl_easy_perform(curl); 
	
		if(res != CURLE_OK) 
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res)); 
		else 
			trap->Print("cURL Worked?\n"); //de fuck izzat

		curl_easy_cleanup(curl);
	}
	else 
		trap->Print("ERROR: Libcurl failed\n"); //de fuck izzat


} 
#endif
