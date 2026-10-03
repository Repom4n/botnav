#include "g_local.h"
#include "g_duel_identity.h"
#include "g_duel_capture.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sqlite3.h"
#include "ai_combat_tuning.h"
#include "g_bot_learning.h"
#include "ai_main.h"
#include "w_saber.h"

extern bot_state_t *botstates[MAX_CLIENTS];

extern qboolean PM_SaberInTransition(int move);

#define _USE_CURL 0

#if _USE_CURL
#include "curl/curl.h"
#include "curl/easy.h"
#endif

static char LOCAL_DB_PATH[MAX_OSPATH];
static char LOCAL_DUELTRACK_DB_PATH[MAX_OSPATH];
#define BOT_DUEL_RANKED_LIMIT_PER_LEVEL 5
#define BOT_DUEL_LEVEL_MIN 1
#define BOT_DUEL_LEVEL_MAX 10
#define BOT_DUEL_SEED_ELO_MIN 800.0f
#define BOT_DUEL_SEED_ELO_MAX 1100.0f
#define BOT_DUEL_SEED_ELO_DEFAULT 1000.0f
//Duel "type" used for arcade rows, matching the arcade leaderboard type in G_AddDuel.
#define TRACKED_ARCADE_DUEL_TYPE 21
#define TRACKED_CAPTURE_VERSION 9
#define TRACKED_FORCE_NOTE_SELECTED_FALLBACK "selected_fallback"
#define TRACKED_AIR_NOTE_JUMP "jump"
#define TRACKED_DUEL_MAX_EVENTS 8192
#define TRACKED_DUEL_TUTORIAL_MAX_MESSAGES 3
#define TRACKED_DUEL_TUTORIAL_COOLDOWN_MS 7000
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
#define TRACKED_DUEL_ADVICE_LOGIN_MIN_DUELS 3
#define TRACKED_DUEL_SCORE_LOW_FORCE_FINISH_BONUS 5
#define TRACKED_DUEL_SCORE_SPENT_PANIC_DIVISOR 15
#define TRACKED_DUEL_SCORE_GRIP_EVENT_WEIGHT 2
#define TRACKED_DUEL_SCORE_SABER_THROW_PUNISH_WEIGHT 2
#define TRACKED_DUEL_SCORE_KNOCKDOWN_EVENT_WEIGHT 2
#define TRACKED_DUEL_SCORE_LATE_DEFENSE_WEIGHT 2
#define TRACKED_DUEL_SCORE_FORCED_ENTRY_SPENT_GAP 30
#define TRACKED_DUEL_SCORE_FORCED_ENTRY_SPENT_BONUS 3
#define TRACKED_DUEL_SCORE_FORCED_ENTRY_NO_CONFIRM_BONUS 2
#define TRACKED_DUEL_SCORE_LINEAR_FALLBACK_BASE 1
#define TRACKED_SEQUENCE_TIMEOUT_MS 1500
//Force regeneration ticks every frame, which previously produced one tracked event per tick
//and dominated the exports. Accumulate regen and emit a single periodic resource sample
//instead; total_force_regen accounting is unchanged.
#define TRACKED_REGEN_SAMPLE_INTERVAL_MS 1000
#define TRACKED_ATTACK_CHAIN_WINDOW_MS 1200
#define TRACKED_COUNTER_WINDOW_MS 1200
#define TRACKED_PUNISH_WINDOW_MS 900
#define TRACKED_FORCE_TO_SABER_WINDOW_MS 1000
#define TRACKED_ARCADE_LEADERBOARD_NAME_CHARS 18
#define LOCAL_ARCADE_SCORE_ORDER "score DESC, end_time DESC"
//#define GLOBAL_DB_PATH sv_globalDBPath.string
//#define MAX_TMP_RACELOG_SIZE 80 * 1024

void G_ErrorPrint( const char *fmt, int s );
void G_Say(gentity_t *ent, gentity_t *target, int mode, const char *chatText);
extern qboolean BG_InKnockDown(int anim);
extern qboolean BG_KickingAnim(int anim);

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

typedef enum
{
	DUEL_TRACK_EVENT_FORCE = 0,
	DUEL_TRACK_EVENT_REGEN,
	DUEL_TRACK_EVENT_DAMAGE,
	DUEL_TRACK_EVENT_RANGE,
	DUEL_TRACK_EVENT_AIR,
	DUEL_TRACK_EVENT_KNOCKDOWN,
	DUEL_TRACK_EVENT_PRESSURE_SUCCESS,
	DUEL_TRACK_EVENT_RESET_SUCCESS,
	DUEL_TRACK_EVENT_OVERCOMMIT,
	DUEL_TRACK_EVENT_ATTACK_START,
	DUEL_TRACK_EVENT_ATTACK_CHAIN,
	DUEL_TRACK_EVENT_COUNTER_SUCCESS,
	DUEL_TRACK_EVENT_PUNISH_SUCCESS,
	DUEL_TRACK_EVENT_SABER_RETURN_PUNISH,
	DUEL_TRACK_EVENT_FORCE_TO_SABER,
	DUEL_TRACK_EVENT_KNOCKDOWN_FOLLOWUP,
	DUEL_TRACK_EVENT_SWING_START,
	DUEL_TRACK_EVENT_SWING_DAMAGE,
	DUEL_TRACK_EVENT_SWING_END,
	DUEL_TRACK_EVENT_DAMAGE_DEALT,
	DUEL_TRACK_EVENT_INPUT_START,
	DUEL_TRACK_EVENT_THROW_START,
	DUEL_TRACK_EVENT_DODGE
} duel_track_event_type_t;

typedef struct
{
	int relTime;
	int amount;
	unsigned long long eventIndex;
	unsigned char eventType;
	unsigned char power;
	unsigned char state;
	unsigned char rangeBucket;
	unsigned char hasGeometry;
	unsigned short sequenceId;
	unsigned short buttons;
	short yawDelta;
	int saberMove;
	int enemySaberMove;
	short selfHealth;
	short selfArmor;
	short selfForce;
	short enemyHealth;
	short enemyArmor;
	short enemyForce;
	vec3_t selfOrigin;
	vec3_t enemyOrigin;
	vec3_t selfVelocity;
	vec3_t enemyVelocity;
	float selfYaw;
	float enemyYaw;
	unsigned char opponentKind;
	int opponentClientNum;
	char opponentKey[64];
	char opponentLabel[MAX_NETNAME];
	char sequenceLabel[32];
	char quality[16];
	char note[32];
	char swingSide[12];
	char preSwingStrafe[12];
	char movementIntent[12];
	short radialSpeed;
	short yawSweep;
	unsigned short attackElapsedMs;
	short throwYawOffset;
	short forwardmove;
	short rightmove;
	short upmove;
	int saberStance;
	int grounded;
	int damageSource;
	char damageAttackerKey[64];
	int controllerOwnsInputs;
	int controllerFamily;
	int controllerEnabled;
	int controllerFanBias;
	int controllerCandidate;
	int controllerMistakeBias;
	float controllerSkill;
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
	int lastButtons;
	int lastSaberMove;
	int lastOpponentHealthArmor;
	int lastAttackTime;
	duel_capture_swing_t swing;
	int lastRightMove;
	int totalDamageTaken;
	int totalDamageDealt;
	int totalKills;
	int counterSuccessEvents;
	int punishSuccessEvents;
	int lastAttackSwingSide;
	int lastAttackStrafeDir;
	int lastDamageTakenTime;
	int lastForceSpendTime;
	int lastThrowTime;
	int lastThrowYawOffset;
	int lastSaberInFlight;
	int currentSequenceId;
	int lastSequenceTime;
	int lastRegenSampleTime;
	int pendingRegenAmount;
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
	//Resources captured the moment this participant died. The duel is only finalised after
	//the respawn path has already restored HP/armor/force, so reading the live playerState in
	//G_FinishTrackedDuel used to report a full force bar for every loser.
	int deathForce;
	int deathHP;
	int deathArmor;
	int hasDeathSnapshot;
	int hasLethalDamage;
	int side;
	int opponentSide;
	int identityKind;
	int didDieLowForce;
	int pendingResetRecovery;
	int punishConfirmEvents;
	int resetSuccessEvents;
	int antiThrowSuccessEvents;
	int overcommitEvents;
	char identityKey[64];
	char identityLabel[MAX_NETNAME];
	//The ELO ladder identity ("botlvlN" / account name), stored alongside the
	//tracking key so tracking rows and LocalDuel rating rows can be joined.
	char eloKey[64];
	char opponentKey[64];
	char opponentLabel[MAX_NETNAME];
	char openingTactic[32];
	char primaryIssue[32];
	char lastBadSequenceLabel[32];
	char lastGoodSequenceLabel[32];
	tracked_duel_event_t *events;
	duel_capture_storage_t capture;
} tracked_duel_runtime_t;

typedef struct
{
	qboolean active;
	int startTime;
	int eventCount;
	int lastForce;
	int lowestForce;
	int lastHealthArmor;
	int lastSelectedPower;
	int lastPowersActive;
	int lastRangeBucket;
	int lastAirborne;
	int lastKnockdown;
	int lastGripCripple;
	int lastButtons;
	int lastSaberMove;
	int lastOpponentClientNum;
	int lastOpponentHealthArmor;
	int lastAttackTime;
	duel_capture_swing_t swing;
	int lastRightMove;
	int lastAttackSwingSide;
	int lastAttackStrafeDir;
	int lastDamageTakenTime;
	int lastForceSpendTime;
	int lastThrowTime;
	int lastThrowYawOffset;
	int lastSaberInFlight;
	int currentSequenceId;
	int lastSequenceTime;
	int lastRegenSampleTime;
	int pendingRegenAmount;
	int totalForceSpent;
	int totalForceRegen;
	int totalDamageTaken;
	int totalDamageDealt;
	int lowForceWindows;
	int hasLethalDamage;
	int knockdownEvents;
	int resetSuccessEvents;
	int counterSuccessEvents;
	int punishSuccessEvents;
	int saberReturnPunishes;
	int killCount;
	int endingForce;
	int endingHP;
	int endingArmor;
	int identityKind;
	char identityKey[64];
	char identityLabel[MAX_NETNAME];
	tracked_duel_event_t *events;
	duel_capture_storage_t capture;
} tracked_arcade_runtime_t;

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
	unsigned int lastOpponentHash;
	int lastIssueAdvised;
	int lastIssueAdviceTime;
	int lastIssueAdviceDuel;
	qboolean loginReminderSent;
} duel_advice_session_state_t;

typedef enum
{
	DUEL_TRACK_SKILL_BEGINNER = 0,
	DUEL_TRACK_SKILL_INTERMEDIATE,
	DUEL_TRACK_SKILL_ADVANCED
} duel_track_skill_band_t;

static tracked_duel_runtime_t g_trackedDuels[MAX_CLIENTS];
static tracked_arcade_runtime_t g_trackedArcadeCombats[MAX_CLIENTS];
static bot_tutorial_queue_t g_botTutorialQueues[MAX_CLIENTS];
static duel_advice_session_state_t g_duelAdviceSessions[MAX_CLIENTS];
static qboolean g_duelTrackingSchemaReady = qfalse;
static char g_duelTrackingSchemaPath[MAX_OSPATH];

static void G_EnsureLocalArcadeSchema(sqlite3 *db);
static qboolean G_DoesTrackedDuelTableExist(sqlite3 *db, const char *tableName);
static qboolean G_OpenTrackedLocalDB(sqlite3 **dbOut, char *resolvedPath, int resolvedPathSize);
static qboolean G_OpenSQLiteFile(const char *path, sqlite3 **dbOut, const char *context);
static qboolean G_OpenLocalAccountDB(sqlite3 **dbOut);
static qboolean G_TrackedTableHasColumn(sqlite3 *db, const char *tableName, const char *columnName);
static void G_QueueBotTutorialMessage(int botClientNum, int targetClientNum, const char *message);

//The CALL_SQLITE macro only writes to stderr, which is invisible on a hosted dedicated
//server. Duel tracking used to fail silently because of that, so route the tracking
//failures through trap->Print where the operator can actually see them.
static void G_TrackedDBError(const char *context, sqlite3 *db, int status)
{
	trap->Print("Duel tracking: %s failed (%i: %s)\n",
		context ? context : "sqlite operation", status,
		db ? sqlite3_errmsg(db) : "no database handle");
}

static const char *const g_trackedDuelTableNames[] = {
	"LocalDuelTrackSummary",
	"LocalDuelTrackParticipant",
	"LocalDuelTrackEvent",
	"LocalDuelTrackGeometry",
	"LocalDuelTrackAggregate"
};
static void G_FormatArcadeLeaderboardName(const char *input, char *output, int outputSize)
{
	char clean[MAX_NETNAME];
	int visibleChars = 0;
	int readIndex = 0;
	int writeIndex = 0;

	if (!output || outputSize <= 0)
		return;

	output[0] = '\0';
	if (!input || !input[0])
	{
		Q_strncpyz(output, "<unknown>", outputSize);
		return;
	}

	Q_strncpyz(clean, input, sizeof(clean));
	Q_CleanStr(clean);
	if (!clean[0])
	{
		Q_strncpyz(output, "<unknown>", outputSize);
		return;
	}

	while (clean[readIndex] && writeIndex < outputSize - 1 &&
		visibleChars < TRACKED_ARCADE_LEADERBOARD_NAME_CHARS)
	{
		output[writeIndex++] = clean[readIndex++];
		visibleChars++;
	}
	output[writeIndex] = '\0';
}

static void G_BuildTrackedExportPath(const char *dbDir, char pathSep, const char *safePrefix,
	const char *suffix, char *outPath, int outPathSize)
{
	if (!outPath || outPathSize < 1 || !suffix || !suffix[0])
		return;

	if (safePrefix && safePrefix[0] && dbDir && dbDir[0])
		Com_sprintf(outPath, outPathSize, "%s%c%s_dueltrack_%s", dbDir, pathSep, safePrefix, suffix);
	else if (safePrefix && safePrefix[0])
		Com_sprintf(outPath, outPathSize, "%s_dueltrack_%s", safePrefix, suffix);
	else if (dbDir && dbDir[0])
		Com_sprintf(outPath, outPathSize, "%s%cdueltrack_%s", dbDir, pathSep, suffix);
	else
		Com_sprintf(outPath, outPathSize, "dueltrack_%s", suffix);
}

static qboolean G_IsAllowedTrackedTableName(const char *tableName)
{
	static const char *const allowedTables[] = {
		"LocalDuelTrackSummary",
		"LocalDuelTrackParticipant",
		"LocalDuelTrackEvent",
		"LocalDuelTrackGeometry",
		"LocalDuelTrackAggregate"
	};
	int i;

	if (!tableName || !tableName[0])
		return qfalse;

	for (i = 0; i < (int)(sizeof(allowedTables) / sizeof(allowedTables[0])); i++)
	{
		if (!Q_stricmp(tableName, allowedTables[i]))
			return qtrue;
	}
	return qfalse;
}

static qboolean G_IsAllowedTrackedColumnName(const char *columnName)
{
	static const char *const allowedColumns[] = {
		"source_context",
		"summary_id",
		"session_id",
		"participant_key",
		"opponent_key",
		"rel_time",
		"event_index",
		"event_type",
		"power",
		"amount",
		"state",
		"range_bucket",
		"draw",
		"winner_opening",
		"loser_opening",
		"participant_label",
		"participant_kind",
		"opponent_side",
		"side",
		"matchup",
		"total_force_spent",
		"total_force_regen",
		"ending_force",
		"ending_hp",
		"ending_armor",
		"low_force_windows",
		"grip_cripple_events",
		"saber_throw_punishes",
		"knockdown_events",
		"late_defense_spends",
		"opening_tactic",
		"primary_issue",
		"spent_neutral",
		"spent_advantage",
		"spent_disadvantage",
		"spent_panic",
		"spent_finishing",
		"force_push",
		"force_pull",
		"force_grip",
		"force_drain",
		"force_rage",
		"force_absorb",
		"force_protect",
		"force_heal",
		"force_speed",
		"force_seeing",
		"force_unknown",
		"sequence_id",
		"buttons",
		"saber_move",
		"enemy_saber_move",
		"yaw_delta",
		"opponent_label",
		"opponent_kind",
		"self_hp",
		"self_armor",
		"self_force",
		"enemy_hp",
		"enemy_armor",
		"enemy_force",
		"sequence_label",
		"quality",
		"note",
		"swing_side",
		"pre_swing_strafe",
		"movement_intent",
		"radial_speed",
		"yaw_sweep",
		"attack_elapsed_ms",
		"throw_yaw_offset",
		"result",
		"arcade_level",
		"total_kills",
		"total_damage_taken",
		"total_damage_dealt",
		"counter_successes",
		"punish_successes",
		"reset_successes",
		"saber_return_punishes",
		"duels",
		"wins",
		"losses",
		"low_force_deaths",
		"grip_cripples",
		"start_time",
		"end_time",
		"duration",
		"mapname",
		"type",
		"winner_key",
		"winner_label",
		"winner_kind",
		"winner_side",
		"loser_key",
		"loser_label",
		"loser_kind",
		"loser_side",
		"won",
		"self_x",
		"self_y",
		"self_z",
		"enemy_x",
		"enemy_y",
		"enemy_z",
		"self_vx",
		"self_vy",
		"self_vz",
		"enemy_vx",
		"enemy_vy",
		"enemy_vz",
		"self_yaw",
		"enemy_yaw",
		"capture_version", "capture_revision",
		"forwardmove", "rightmove", "upmove", "saber_stance", "grounded",
		"damage_source", "damage_attacker_key",
		"controller_owns_inputs", "controller_family", "controller_enabled", "controller_fanbias",
		"controller_candidate", "controller_mistakebias", "controller_skill",
		"event_total", "event_retained", "event_dropped", "event_critical_dropped",
		"event_compactions", "event_allocation_failures"
	};
	const char *p;
	int i;

	if (!columnName || !columnName[0])
		return qfalse;

	//Allow only identifier-safe names here; membership is still enforced by allowlist below.
	for (p = columnName; *p; ++p)
	{
		if (!((*p >= 'a' && *p <= 'z') ||
			(*p >= 'A' && *p <= 'Z') ||
			(*p >= '0' && *p <= '9') ||
			*p == '_'))
		{
			return qfalse;
		}
	}

	for (i = 0; i < (int)(sizeof(allowedColumns) / sizeof(allowedColumns[0])); i++)
	{
		if (!Q_stricmp(columnName, allowedColumns[i]))
			return qtrue;
	}
	return qfalse;
}

static qboolean G_TrackedTableHasColumn(sqlite3 *db, const char *tableName, const char *columnName)
{
	sqlite3_stmt *stmt = NULL;
	char sql[128];
	int s = SQLITE_DONE;
	qboolean found = qfalse;

	if (!G_IsAllowedTrackedTableName(tableName) || !G_IsAllowedTrackedColumnName(columnName))
		return qfalse;

	Com_sprintf(sql, sizeof(sql), "PRAGMA table_info(%s)", tableName);
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	while ((s = sqlite3_step(stmt)) == SQLITE_ROW)
	{
		const unsigned char *name = sqlite3_column_text(stmt, 1);
		if (name && !Q_stricmp((const char *)name, columnName))
		{
			found = qtrue;
			break;
		}
	}
	if (s != SQLITE_DONE && s != SQLITE_ROW)
		G_ErrorPrint("ERROR: SQL Select Failed (tracked table_info)", s);
	CALL_SQLITE(finalize(stmt));
	return found;
}

static void G_EnsureTrackedTableColumn(sqlite3 *db, const char *tableName, const char *columnName, const char *definition)
{
	sqlite3_stmt *stmt = NULL;
	char sql[256];
	int s = SQLITE_ERROR;

	if (!G_IsAllowedTrackedTableName(tableName) || !G_IsAllowedTrackedColumnName(columnName) ||
		!definition || !definition[0])
	{
		return;
	}

	if (G_TrackedTableHasColumn(db, tableName, columnName))
		return;

	Com_sprintf(sql, sizeof(sql), "ALTER TABLE %s ADD COLUMN %s %s", tableName, columnName, definition);
	s = sqlite3_prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL);
	if (s != SQLITE_OK || !stmt)
	{
		G_TrackedDBError(va("ALTER TABLE %s ADD COLUMN %s", tableName, columnName), db, s);
		if (stmt)
			sqlite3_finalize(stmt);
		return;
	}
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Alter Failed (tracked add column)", s);
		G_TrackedDBError(va("ALTER TABLE %s ADD COLUMN %s", tableName, columnName), db, s);
	}
	CALL_SQLITE(finalize(stmt));
}

static void G_EnsureLocalDuelTrackingSchema(sqlite3 *db)
{
	sqlite3_stmt *stmt = NULL;
	char *sql;
	int s = SQLITE_ERROR;

	sql = "CREATE TABLE IF NOT EXISTS LocalDuelTrackSummary("
		"id INTEGER PRIMARY KEY, start_time UNSIGNED INTEGER, end_time UNSIGNED INTEGER, duration UNSIGNED INTEGER, "
		"type UNSIGNED TINYINT, mapname VARCHAR(64), winner_key VARCHAR(64), winner_label VARCHAR(36), "
		"winner_kind UNSIGNED TINYINT, winner_side UNSIGNED TINYINT, loser_key VARCHAR(64), loser_label VARCHAR(36), "
		"loser_kind UNSIGNED TINYINT, loser_side UNSIGNED TINYINT, draw UNSIGNED TINYINT DEFAULT 0, "
		"winner_opening VARCHAR(32), loser_opening VARCHAR(32))";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Create Failed (LocalDuelTrackSummary)", s);
		G_TrackedDBError("CREATE TABLE LocalDuelTrackSummary", db, s);
	}
	CALL_SQLITE(finalize(stmt));

	sql = "CREATE TABLE IF NOT EXISTS LocalDuelTrackParticipant("
		"id INTEGER PRIMARY KEY, summary_id INTEGER, participant_key VARCHAR(64), participant_label VARCHAR(36), "
		"participant_kind UNSIGNED TINYINT, elo_key VARCHAR(64) DEFAULT '', opponent_key VARCHAR(64), won UNSIGNED TINYINT, side UNSIGNED TINYINT, "
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
	{
		G_ErrorPrint("ERROR: SQL Create Failed (LocalDuelTrackParticipant)", s);
		G_TrackedDBError("CREATE TABLE LocalDuelTrackParticipant", db, s);
	}
	CALL_SQLITE(finalize(stmt));

	sql = "CREATE TABLE IF NOT EXISTS LocalDuelTrackEvent("
		"id INTEGER PRIMARY KEY, summary_id INTEGER, participant_key VARCHAR(64), opponent_key VARCHAR(64), "
		"rel_time UNSIGNED INTEGER, event_index UNSIGNED SMALLINT, event_type VARCHAR(16), power UNSIGNED TINYINT, "
		"amount SMALLINT, state UNSIGNED TINYINT, range_bucket UNSIGNED TINYINT, note VARCHAR(32) DEFAULT '', "
		"sequence_id UNSIGNED SMALLINT DEFAULT 0, buttons UNSIGNED SMALLINT DEFAULT 0, "
		"saber_move INTEGER DEFAULT 0, enemy_saber_move INTEGER DEFAULT 0, yaw_delta SMALLINT DEFAULT 0, "
		"opponent_label VARCHAR(36) DEFAULT '', opponent_kind UNSIGNED TINYINT DEFAULT 0, "
		"self_hp SMALLINT DEFAULT 0, self_armor SMALLINT DEFAULT 0, self_force SMALLINT DEFAULT 0, "
		"enemy_hp SMALLINT DEFAULT 0, enemy_armor SMALLINT DEFAULT 0, enemy_force SMALLINT DEFAULT 0, "
		"sequence_label VARCHAR(32) DEFAULT '', quality VARCHAR(16) DEFAULT '', "
		"swing_side VARCHAR(12) DEFAULT '', pre_swing_strafe VARCHAR(12) DEFAULT '', "
		"movement_intent VARCHAR(12) DEFAULT '', radial_speed SMALLINT DEFAULT 0, "
		"yaw_sweep SMALLINT DEFAULT 0, attack_elapsed_ms UNSIGNED SMALLINT DEFAULT 0, "
		"throw_yaw_offset SMALLINT DEFAULT 0)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Create Failed (LocalDuelTrackEvent)", s);
		G_TrackedDBError("CREATE TABLE LocalDuelTrackEvent", db, s);
	}
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
	{
		G_ErrorPrint("ERROR: SQL Create Failed (LocalDuelTrackGeometry)", s);
		G_TrackedDBError("CREATE TABLE LocalDuelTrackGeometry", db, s);
	}
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
	{
		G_ErrorPrint("ERROR: SQL Create Failed (LocalDuelTrackAggregate)", s);
		G_TrackedDBError("CREATE TABLE LocalDuelTrackAggregate", db, s);
	}
	CALL_SQLITE(finalize(stmt));

	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "capture_version", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "capture_revision", "VARCHAR(40) DEFAULT ''");

	//Bot sequence learning: outcome counts per (who, skill band, context, opponent action,
	//response, follow-ups). See g_bot_learning.h for the token/context encoding.
	sql = "CREATE TABLE IF NOT EXISTS LocalBotLearnedSequence("
		"source_kind UNSIGNED TINYINT, skill_band UNSIGNED TINYINT, ctx_key UNSIGNED INTEGER, "
		"stimulus UNSIGNED TINYINT, response UNSIGNED TINYINT, follow1 UNSIGNED TINYINT, follow2 UNSIGNED TINYINT, "
		"samples UNSIGNED INTEGER DEFAULT 0, wins UNSIGNED INTEGER DEFAULT 0, net_damage INTEGER DEFAULT 0, "
		"total_response_ms UNSIGNED INTEGER DEFAULT 0, capture_version UNSIGNED INTEGER DEFAULT 0, "
		"PRIMARY KEY(source_kind, skill_band, ctx_key, stimulus, response, follow1, follow2))";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Create Failed (LocalBotLearnedSequence)", s);
		G_TrackedDBError("CREATE TABLE LocalBotLearnedSequence", db, s);
	}
	CALL_SQLITE(finalize(stmt));

	G_EnsureTrackedTableColumn(db, "LocalDuelTrackSummary", "source_context", "VARCHAR(16) DEFAULT 'duel'");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackSummary", "draw", "UNSIGNED TINYINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackSummary", "winner_opening", "VARCHAR(32) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackSummary", "loser_opening", "VARCHAR(32) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "participant_label", "VARCHAR(36) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "elo_key", "VARCHAR(64) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "participant_kind", "UNSIGNED TINYINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "opponent_side", "UNSIGNED TINYINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "matchup", "UNSIGNED TINYINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "total_force_spent", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "total_force_regen", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "ending_force", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "ending_hp", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "ending_armor", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "low_force_windows", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "grip_cripple_events", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "saber_throw_punishes", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "knockdown_events", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "late_defense_spends", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "opening_tactic", "VARCHAR(32) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "primary_issue", "VARCHAR(32) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "spent_neutral", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "spent_advantage", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "spent_disadvantage", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "spent_panic", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "spent_finishing", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "force_push", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "force_pull", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "force_grip", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "force_drain", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "force_rage", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "force_absorb", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "force_protect", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "force_heal", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "force_speed", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "force_seeing", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "force_unknown", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "summary_id", "INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "participant_key", "VARCHAR(64) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "opponent_key", "VARCHAR(64) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "rel_time", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "event_index", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "event_type", "VARCHAR(16) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "power", "UNSIGNED TINYINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "amount", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "state", "UNSIGNED TINYINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "range_bucket", "UNSIGNED TINYINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "sequence_id", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "buttons", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "saber_move", "INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "enemy_saber_move", "INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "yaw_delta", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "opponent_label", "VARCHAR(36) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "opponent_kind", "UNSIGNED TINYINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "self_hp", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "self_armor", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "self_force", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "enemy_hp", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "enemy_armor", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "enemy_force", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "sequence_label", "VARCHAR(32) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "quality", "VARCHAR(16) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "note", "VARCHAR(32) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "swing_side", "VARCHAR(12) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "pre_swing_strafe", "VARCHAR(12) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "movement_intent", "VARCHAR(12) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "radial_speed", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "yaw_sweep", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "attack_elapsed_ms", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "throw_yaw_offset", "SMALLINT DEFAULT 0");
	/* Unknown provenance stays unknown for migrated/imported sessions. */
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackSummary", "capture_version", "INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "controller_owns_inputs", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "controller_family", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "controller_enabled", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "controller_fanbias", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "controller_candidate", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "controller_mistakebias", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "controller_skill", "REAL DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "event_total", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "event_retained", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "event_dropped", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "event_critical_dropped", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "event_compactions", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "event_allocation_failures", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackSummary", "capture_revision", "VARCHAR(64) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "forwardmove", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "rightmove", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "upmove", "SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "saber_stance", "INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "grounded", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "damage_source", "INTEGER DEFAULT -1");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "damage_attacker_key", "VARCHAR(64) DEFAULT ''");

	//Arcade sessions share this table family. source_context on the summary separates the two,
	//while the columns below carry the arcade-only fields that used to live in LocalArcadeTrack*.
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackSummary", "result", "VARCHAR(24) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackSummary", "arcade_level", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "total_kills", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "total_damage_taken", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "total_damage_dealt", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "counter_successes", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "punish_successes", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackParticipant", "reset_successes", "UNSIGNED SMALLINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "participant_label", "VARCHAR(36) DEFAULT ''");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackEvent", "participant_kind", "UNSIGNED TINYINT DEFAULT 0");

	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "duels", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "participant_kind", "UNSIGNED TINYINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "side", "UNSIGNED TINYINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "matchup", "UNSIGNED TINYINT DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "wins", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "losses", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "total_force_spent", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "total_force_regen", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "low_force_deaths", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "grip_cripples", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "saber_throw_punishes", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "force_push", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "force_pull", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "force_grip", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "force_drain", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "force_rage", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "force_absorb", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "force_protect", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "force_heal", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "force_speed", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "force_seeing", "UNSIGNED INTEGER DEFAULT 0");
	G_EnsureTrackedTableColumn(db, "LocalDuelTrackAggregate", "force_unknown", "UNSIGNED INTEGER DEFAULT 0");

	g_duelTrackingSchemaReady = qtrue;
	Q_strncpyz(g_duelTrackingSchemaPath, LOCAL_DUELTRACK_DB_PATH, sizeof(g_duelTrackingSchemaPath));
}

static void G_ClearTrackedDuelRuntime(int clientNum)
{
	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
		return;

	G_DuelCaptureClearRuntime(&g_trackedDuels[clientNum], sizeof(g_trackedDuels[clientNum]),
		g_trackedDuels[clientNum].events);
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
	switch (eventType)
	{
	case DUEL_TRACK_EVENT_DAMAGE:
	case DUEL_TRACK_EVENT_RANGE:
	case DUEL_TRACK_EVENT_AIR:
	case DUEL_TRACK_EVENT_KNOCKDOWN:
	case DUEL_TRACK_EVENT_ATTACK_START:
	case DUEL_TRACK_EVENT_ATTACK_CHAIN:
	case DUEL_TRACK_EVENT_COUNTER_SUCCESS:
	case DUEL_TRACK_EVENT_PUNISH_SUCCESS:
	case DUEL_TRACK_EVENT_SABER_RETURN_PUNISH:
	case DUEL_TRACK_EVENT_FORCE_TO_SABER:
	case DUEL_TRACK_EVENT_KNOCKDOWN_FOLLOWUP:
	case DUEL_TRACK_EVENT_SWING_START:
	case DUEL_TRACK_EVENT_SWING_DAMAGE:
	case DUEL_TRACK_EVENT_SWING_END:
	case DUEL_TRACK_EVENT_DAMAGE_DEALT:
	case DUEL_TRACK_EVENT_INPUT_START:
	case DUEL_TRACK_EVENT_THROW_START:
	case DUEL_TRACK_EVENT_DODGE:
		return qtrue;
	default:
		return qfalse;
	}
}

static qboolean G_IsTrackedDuelEligible(gentity_t *first, gentity_t *second)
{
	if (!first || !second)
		return qfalse;
	// keep bot-vs-bot duels out of tracked datasets
	if ((first->r.svFlags & SVF_BOT) && (second->r.svFlags & SVF_BOT))
		return qfalse;
	return qtrue;
}

static void G_ClearDuelAdviceSession(int clientNum)
{
	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
		return;

	memset(&g_duelAdviceSessions[clientNum], 0, sizeof(g_duelAdviceSessions[clientNum]));
	g_duelAdviceSessions[clientNum].lastIssueAdvised = -1;
	g_duelAdviceSessions[clientNum].lastIssueAdviceDuel = -1;
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
		session->lastIssueAdvised = -1;
		session->lastIssueAdviceDuel = -1;
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
	case DUEL_TRACK_EVENT_FORCE: return "force";
	case DUEL_TRACK_EVENT_REGEN: return "regen";
	case DUEL_TRACK_EVENT_DAMAGE: return "damage";
	case DUEL_TRACK_EVENT_RANGE: return "range";
	case DUEL_TRACK_EVENT_AIR: return "air";
	case DUEL_TRACK_EVENT_KNOCKDOWN: return "knockdown";
	case DUEL_TRACK_EVENT_PRESSURE_SUCCESS: return "pressure_success";
	case DUEL_TRACK_EVENT_RESET_SUCCESS: return "reset_success";
	case DUEL_TRACK_EVENT_OVERCOMMIT: return "overcommit";
	case DUEL_TRACK_EVENT_ATTACK_START: return "attack_start";
	case DUEL_TRACK_EVENT_ATTACK_CHAIN: return "attack_chain";
	case DUEL_TRACK_EVENT_SWING_START: return "swing_start";
	case DUEL_TRACK_EVENT_SWING_DAMAGE: return "swing_damage";
	case DUEL_TRACK_EVENT_SWING_END: return "swing_end";
	case DUEL_TRACK_EVENT_DAMAGE_DEALT: return "damage_dealt";
	case DUEL_TRACK_EVENT_INPUT_START: return "input_start";
	case DUEL_TRACK_EVENT_THROW_START: return "throw_start";
	case DUEL_TRACK_EVENT_DODGE: return "dodge";
	case DUEL_TRACK_EVENT_COUNTER_SUCCESS: return "counter_success";
	case DUEL_TRACK_EVENT_PUNISH_SUCCESS: return "punish_success";
	case DUEL_TRACK_EVENT_SABER_RETURN_PUNISH: return "saber_return_punish";
	case DUEL_TRACK_EVENT_FORCE_TO_SABER: return "force_to_saber";
	case DUEL_TRACK_EVENT_KNOCKDOWN_FOLLOWUP: return "knockdown_followup";
	default: return "note";
	}
}

static const char *G_GetTrackedEventQualityName(const tracked_duel_event_t *event)
{
	if (!event)
		return "";

	switch (event->eventType)
	{
	case DUEL_TRACK_EVENT_COUNTER_SUCCESS:
	case DUEL_TRACK_EVENT_PUNISH_SUCCESS:
	case DUEL_TRACK_EVENT_SABER_RETURN_PUNISH:
	case DUEL_TRACK_EVENT_KNOCKDOWN_FOLLOWUP:
		return "best";
	case DUEL_TRACK_EVENT_RESET_SUCCESS:
		return "good";
	case DUEL_TRACK_EVENT_OVERCOMMIT:
		return (event->state == DUEL_TRACK_STATE_PANIC || event->state == DUEL_TRACK_STATE_DISADVANTAGE) ? "mistake" : "bad";
	case DUEL_TRACK_EVENT_DAMAGE:
		if (event->state == DUEL_TRACK_STATE_PANIC)
			return "mistake";
		if (event->state == DUEL_TRACK_STATE_DISADVANTAGE)
			return "bad";
		return "mediocre";
	case DUEL_TRACK_EVENT_FORCE:
		if (event->state == DUEL_TRACK_STATE_ADVANTAGE || event->state == DUEL_TRACK_STATE_FINISHING)
			return "good";
		if (event->state == DUEL_TRACK_STATE_PANIC || event->state == DUEL_TRACK_STATE_DISADVANTAGE)
			return (event->amount >= 20) ? "bad" : "mediocre";
		return "mediocre";
	case DUEL_TRACK_EVENT_ATTACK_START:
	case DUEL_TRACK_EVENT_ATTACK_CHAIN:
		if (event->state == DUEL_TRACK_STATE_ADVANTAGE || event->state == DUEL_TRACK_STATE_FINISHING)
			return "good";
		if (event->state == DUEL_TRACK_STATE_PANIC)
			return "bad";
		return "mediocre";
	case DUEL_TRACK_EVENT_INPUT_START:
	case DUEL_TRACK_EVENT_SWING_START:
		return "mediocre";
	default:
		break;
	}

	return "";
}

static int G_GetTrackedSwingSideValue(int saberMove)
{
	return BG_SaberHorizontalSweepDir(saberMove);
}

static qboolean G_IsTrackedSaberContinuityAllowed(const gentity_t *ent)
{
	return ent && ent->client && ent->health > 0 && ent->client->ps.weapon == WP_SABER &&
		!ent->client->ps.saberInFlight && ent->client->ps.forceHandExtend == HANDEXTEND_NONE &&
		!BG_InKnockDown(ent->client->ps.legsAnim);
}

static const char *G_GetTrackedSwingSideName(int swingSide)
{
	if (swingSide < 0)
		return "leftward";
	if (swingSide > 0)
		return "rightward";
	return "";
}

static int G_GetTrackedStrafeDirValue(const usercmd_t *cmd)
{
	if (!cmd)
		return 0;
	if (cmd->rightmove > 0)
		return 1;
	if (cmd->rightmove < 0)
		return -1;
	return 0;
}

static const char *G_GetTrackedStrafeDirName(int strafeDir)
{
	if (strafeDir < 0)
		return "left";
	if (strafeDir > 0)
		return "right";
	return "";
}

static gentity_t *G_GetTrackedOwnSaberEntity(gentity_t *self)
{
	int saberEntNum;

	if (!self || !self->client)
		return NULL;

	saberEntNum = self->client->ps.saberEntityNum;
	if (!saberEntNum && self->client->saberStoredIndex > 0)
		saberEntNum = self->client->saberStoredIndex;
	if (saberEntNum <= 0 || saberEntNum >= ENTITYNUM_MAX_NORMAL)
		return NULL;

	return &g_entities[saberEntNum];
}

static int G_GetTrackedThrowYawOffset(gentity_t *self, gentity_t *enemy)
{
	vec3_t enemyVec;
	gentity_t *saberEnt;
	vec3_t throwDir;
	float releaseYaw;
	float yawToEnemy;

	if (!self || !self->client || !enemy || !enemy->client)
		return 0;

	VectorSubtract(enemy->client->ps.origin, self->client->ps.origin, enemyVec);
	if (VectorLengthSquared(enemyVec) <= 1.0f)
		return 0;

	saberEnt = G_GetTrackedOwnSaberEntity(self);
	if (saberEnt && VectorLengthSquared(saberEnt->s.pos.trDelta) > 1.0f)
	{
		VectorCopy(saberEnt->s.pos.trDelta, throwDir);
		throwDir[2] = 0.0f;
		if (VectorLengthSquared(throwDir) > 1.0f)
			releaseYaw = vectoyaw(throwDir);
		else
			releaseYaw = self->client->ps.viewangles[YAW];
	}
	else
	{
		releaseYaw = self->client->ps.viewangles[YAW];
	}

	yawToEnemy = vectoyaw(enemyVec);
	return (int)AngleNormalize180(AngleSubtract(releaseYaw, yawToEnemy));
}

static void G_FillTrackedMovementContext(tracked_duel_event_t *event,
	const gentity_t *self, const gentity_t *enemy)
{
	vec3_t towardEnemy;
	vec3_t relativeVelocity;
	vec3_t horizontalVelocity;
	float distance;
	float radialSpeed;
	float horizontalSpeed;
	const char *intent = "still";

	if (!event || !self || !self->client || !enemy || !enemy->client)
		return;

	VectorSubtract(enemy->client->ps.origin, self->client->ps.origin, towardEnemy);
	towardEnemy[2] = 0.0f;
	distance = VectorNormalize(towardEnemy);
	if (distance <= 1.0f)
	{
		Q_strncpyz(event->movementIntent, "still", sizeof(event->movementIntent));
		return;
	}

	VectorSubtract(self->client->ps.velocity, enemy->client->ps.velocity, relativeVelocity);
	relativeVelocity[2] = 0.0f;
	radialSpeed = DotProduct(relativeVelocity, towardEnemy);
	event->radialSpeed = (short)Com_Clampi(-32768, 32767, (int)radialSpeed);

	VectorCopy(self->client->ps.velocity, horizontalVelocity);
	horizontalVelocity[2] = 0.0f;
	horizontalSpeed = VectorLength(horizontalVelocity);
	if (radialSpeed >= 40.0f)
		intent = "advance";
	else if (radialSpeed <= -40.0f)
		intent = "retreat";
	else if (horizontalSpeed >= 40.0f)
		intent = "lateral";
	Q_strncpyz(event->movementIntent, intent, sizeof(event->movementIntent));
}

static qboolean G_IsTrackedSaberThrowRelease(gentity_t *ent)
{
	if (!ent || !ent->client)
		return qfalse;
	if (ent->client->ps.weapon != WP_SABER)
		return qfalse;
	if (ent->client->ps.saberEntityState != SES_LEAVING)
		return qfalse;
	if (ent->client->ps.weaponstate != WEAPON_FIRING)
		return qfalse;
	return qtrue;
}

static void G_FillTrackedEventCoachingContext(tracked_duel_event_t *event, int eventType,
	int lastAttackTime, int lastAttackSwingSide, int lastAttackStrafeDir,
	int lastThrowTime, int lastThrowYawOffset, const duel_capture_swing_t *swing,
	gentity_t *self, gentity_t *enemy)
{
	int swingSide;
	int strafeDir;
	int elapsed;
	const qboolean attackContextFresh = (lastAttackTime > 0 &&
		level.time - lastAttackTime <= TRACKED_ATTACK_CHAIN_WINDOW_MS) ? qtrue : qfalse;
	const qboolean swingContextFresh = (swing && (swing->active ||
		(swing->startTime > 0 && level.time - swing->startTime <= TRACKED_ATTACK_CHAIN_WINDOW_MS))) ? qtrue : qfalse;

	if (!event || !self || !self->client)
		return;

	swingSide = G_GetTrackedSwingSideValue(self->client->ps.saberMove);
	if (swingContextFresh)
		swingSide = G_GetTrackedSwingSideValue(swing->move);
	if (!swingSide && attackContextFresh)
	{
		swingSide = lastAttackSwingSide;
	}
	strafeDir = G_GetTrackedStrafeDirValue(&self->client->pers.cmd);
	if (swingContextFresh)
	{
		strafeDir = swing->preStrafe;
	}
	else if (!strafeDir && attackContextFresh)
	{
		strafeDir = lastAttackStrafeDir;
	}

	Q_strncpyz(event->swingSide, G_GetTrackedSwingSideName(swingSide), sizeof(event->swingSide));
	Q_strncpyz(event->preSwingStrafe, G_GetTrackedStrafeDirName(strafeDir), sizeof(event->preSwingStrafe));
	G_FillTrackedMovementContext(event, self, enemy);

	if (swingContextFresh)
	{
		elapsed = level.time - swing->startTime;
		if (elapsed < 0)
			elapsed = 0;
		else if (elapsed > 65535)
			elapsed = 65535;
		event->attackElapsedMs = (unsigned short)elapsed;
		event->yawSweep = (short)Com_Clampi(-32768, 32767, (int)swing->sweep);
	}

	if (lastThrowTime > 0 &&
		level.time - lastThrowTime <= TRACKED_ATTACK_CHAIN_WINDOW_MS &&
		(self->client->ps.saberInFlight ||
		 eventType == DUEL_TRACK_EVENT_DAMAGE ||
		 eventType == DUEL_TRACK_EVENT_PUNISH_SUCCESS ||
		 eventType == DUEL_TRACK_EVENT_SABER_RETURN_PUNISH))
	{
		event->throwYawOffset = (short)lastThrowYawOffset;
	}
	else if (self->client->ps.saberInFlight)
	{
		event->throwYawOffset = (short)G_GetTrackedThrowYawOffset(self, enemy);
	}
}

static void G_SetTrackedSequenceLabel(char *out, int outSize, int eventType, duel_track_power_t power,
	const char *note, const gentity_t *self, const gentity_t *enemy)
{
	const playerState_t *selfPs = (self && self->client) ? &self->client->ps : NULL;
	const playerState_t *enemyPs = (enemy && enemy->client) ? &enemy->client->ps : NULL;
	const qboolean kicking = (selfPs && (BG_KickingAnim(selfPs->legsAnim) || BG_KickingAnim(selfPs->torsoAnim) ||
		(selfPs->saberMove >= LS_KICK_F && selfPs->saberMove <= LS_KICK_L_AIR))) ? qtrue : qfalse;
	const qboolean flipKick = (selfPs && (selfPs->legsAnim == BOTH_WALL_FLIP_BACK1 || selfPs->torsoAnim == BOTH_WALL_FLIP_BACK1)) ? qtrue : qfalse;
	const qboolean saberThrowPunish = ((note && !Q_stricmp(note, "saberthrow")) || eventType == DUEL_TRACK_EVENT_SABER_RETURN_PUNISH) ? qtrue : qfalse;
	const int swingSide = selfPs ? G_GetTrackedSwingSideValue(selfPs->saberMove) : 0;
	const char *swingSuffix = (swingSide < 0) ? "left" : ((swingSide > 0) ? "right" : NULL);

	if (!out || outSize <= 0)
		return;
	out[0] = '\0';

	if (saberThrowPunish)
	{
		Q_strncpyz(out, "saberthrow_punish", outSize);
		return;
	}

	if (eventType == DUEL_TRACK_EVENT_FORCE || eventType == DUEL_TRACK_EVENT_FORCE_TO_SABER)
	{
		switch (power)
		{
		case DUEL_TRACK_POWER_PULL:
			Q_strncpyz(out, (eventType == DUEL_TRACK_EVENT_FORCE_TO_SABER) ? "ptk_convert" : "pull_entry", outSize);
			return;
		case DUEL_TRACK_POWER_GRIP:
			Q_strncpyz(out, "grip_pressure", outSize);
			return;
		case DUEL_TRACK_POWER_DRAIN:
			Q_strncpyz(out, "drainlock_pressure", outSize);
			return;
		case DUEL_TRACK_POWER_PUSH:
			Q_strncpyz(out, "push_reset", outSize);
			return;
		case DUEL_TRACK_POWER_ABSORB:
		case DUEL_TRACK_POWER_PROTECT:
			Q_strncpyz(out, "defense_stabilize", outSize);
			return;
		default:
			break;
		}
	}

	if (flipKick)
	{
		Q_strncpyz(out, "flipkick_followup", outSize);
		return;
	}
	if (kicking)
	{
		Q_strncpyz(out, "kick_followup", outSize);
		return;
	}
	if (eventType == DUEL_TRACK_EVENT_KNOCKDOWN_FOLLOWUP)
	{
		Q_strncpyz(out, "knockdown_followup", outSize);
		return;
	}
	if (eventType == DUEL_TRACK_EVENT_COUNTER_SUCCESS)
	{
		Q_strncpyz(out, "counter_window", outSize);
		return;
	}
	if (eventType == DUEL_TRACK_EVENT_PUNISH_SUCCESS)
	{
		Q_strncpyz(out, "punish_window", outSize);
		return;
	}

	if (selfPs)
	{
		if (selfPs->fd.saberAnimLevel == SS_STAFF)
		{
			if (swingSuffix)
				Com_sprintf(out, outSize, "staff_%s_pressure", swingSuffix);
			else
				Q_strncpyz(out, "staff_pressure", outSize);
			return;
		}
		if (selfPs->fd.saberAnimLevel == SS_MEDIUM)
		{
			if (swingSuffix)
				Com_sprintf(out, outSize, "yellow_%s_pressure", swingSuffix);
			else
				Q_strncpyz(out, "yellow_pressure", outSize);
			return;
		}
		if (selfPs->fd.saberAnimLevel == SS_STRONG)
		{
			if (swingSuffix)
				Com_sprintf(out, outSize, "red_%s_pressure", swingSuffix);
			else
				Q_strncpyz(out, "red_pressure", outSize);
			return;
		}
		if (selfPs->fd.saberAnimLevel == SS_DUAL)
		{
			Q_strncpyz(out, "dual_pressure", outSize);
			return;
		}
	}

	if (enemyPs && enemyPs->fd.forcePowersActive & (1 << FP_DRAIN))
	{
		Q_strncpyz(out, "anti_drain", outSize);
		return;
	}

	Q_strncpyz(out, "neutral_probe", outSize);
}

static void G_FillTrackedEventContext(tracked_duel_event_t *event, gentity_t *self, gentity_t *enemy, qboolean captureGeometry)
{
	if (!event || !self || !self->client)
		return;

	event->controllerEnabled = g_newBotAI.integer;
	event->controllerFanBias = bot_fanbias.integer;
	event->controllerMistakeBias = bot_mistakebias.integer;
	event->controllerSkill = -1.0f;
	event->controllerFamily = -1;
	if ((self->r.svFlags & SVF_BOT) && self->s.number >= 0 && self->s.number < MAX_CLIENTS &&
		botstates[self->s.number] && botstates[self->s.number]->inuse)
	{
		event->controllerOwnsInputs = botstates[self->s.number]->saberTechniqueOwnsInputs;
		event->controllerFamily = botstates[self->s.number]->saberTechniqueFamily;
		event->controllerCandidate = botstates[self->s.number]->saberTechniqueCandidate;
		event->controllerSkill = botstates[self->s.number]->settings.skill;
	}
	event->buttons = (unsigned short)(self->client->pers.cmd.buttons & 0xFFFF);
	event->forwardmove = self->client->pers.cmd.forwardmove;
	event->rightmove = self->client->pers.cmd.rightmove;
	event->upmove = self->client->pers.cmd.upmove;
	event->saberStance = self->client->ps.fd.saberAnimLevel;
	event->grounded = self->client->ps.groundEntityNum != ENTITYNUM_NONE;
	event->damageSource = -1;
	event->opponentClientNum = -1;
	event->saberMove = self->client->ps.saberMove;
	event->selfHealth = (short)self->health;
	event->selfArmor = (short)self->client->ps.stats[STAT_ARMOR];
	event->selfForce = (short)self->client->ps.fd.forcePower;
	event->selfYaw = self->client->ps.viewangles[YAW];
	if (enemy && enemy->client)
	{
		int opponentKind;
		event->opponentClientNum = enemy->s.number;
		event->enemySaberMove = enemy->client->ps.saberMove;
		event->enemyHealth = (short)enemy->health;
		event->enemyArmor = (short)enemy->client->ps.stats[STAT_ARMOR];
		event->enemyForce = (short)enemy->client->ps.fd.forcePower;
		event->enemyYaw = enemy->client->ps.viewangles[YAW];
		event->yawDelta = (short)AngleSubtract(event->selfYaw, event->enemyYaw);
		G_GetDuelTrackingIdentity(enemy, event->opponentKey, sizeof(event->opponentKey),
			event->opponentLabel, sizeof(event->opponentLabel), &opponentKind);
		event->opponentKind = (unsigned char)opponentKind;
		if (captureGeometry)
		{
			VectorCopy(self->client->ps.origin, event->selfOrigin);
			VectorCopy(enemy->client->ps.origin, event->enemyOrigin);
			VectorCopy(self->client->ps.velocity, event->selfVelocity);
			VectorCopy(enemy->client->ps.velocity, event->enemyVelocity);
			event->hasGeometry = 1;
		}
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

static int G_TrackedEventPriority(const void *record)
{
	const tracked_duel_event_t *event = (const tracked_duel_event_t *)record;
	if (event->eventType == DUEL_TRACK_EVENT_DAMAGE || event->eventType == DUEL_TRACK_EVENT_DAMAGE_DEALT ||
		event->eventType == DUEL_TRACK_EVENT_KNOCKDOWN)
		return 2;
	if (event->eventType == DUEL_TRACK_EVENT_RANGE || event->eventType == DUEL_TRACK_EVENT_AIR ||
		event->eventType == DUEL_TRACK_EVENT_REGEN)
		return 0;
	return 1;
}

static tracked_duel_event_t *G_ReserveTrackedEvent(tracked_duel_event_t **events,
	int *count, duel_capture_storage_t *capture, int eventType)
{
	const unsigned long long oldDropped = capture->dropped;
	++capture->total;
	*events = (tracked_duel_event_t *)G_DuelCaptureReserve(*events, count, sizeof(**events),
		capture, TRACKED_DUEL_MAX_EVENTS, G_TrackedEventPriority);
	if (capture->dropped && !oldDropped)
		trap->Print("Duel tracking: bounded capture compacted; outcome/sequence ranking disabled, missing records exposed by index gaps and participant counters.\n");
	if (*count >= capture->capacity)
	{
		++capture->dropped;
		if (eventType == DUEL_TRACK_EVENT_DAMAGE || eventType == DUEL_TRACK_EVENT_DAMAGE_DEALT ||
			eventType == DUEL_TRACK_EVENT_KNOCKDOWN)
			++capture->criticalDropped;
		if (!oldDropped)
			trap->Print("Duel tracking: event allocation failed; outcome/sequence ranking disabled, aggregates continue and missing records are counted.\n");
		return NULL;
	}
	return &(*events)[(*count)++];
}

static void G_AddTrackedDuelEvent(tracked_duel_runtime_t *runtime, int eventType, int relTime, int amount, duel_track_power_t power, int state, int rangeBucket, const char *note, gentity_t *self, gentity_t *enemy)
{
	tracked_duel_event_t *event;
	const qboolean captureGeometry = G_ShouldCaptureTrackedGeometryEvent(eventType);

	if (!runtime || !runtime->active)
		return;

	event = G_ReserveTrackedEvent(&runtime->events, &runtime->eventCount, &runtime->capture, eventType);
	if (!event)
		return;
	memset(event, 0, sizeof(*event));
	event->relTime = relTime;
	event->amount = amount;
	event->eventIndex = runtime->capture.total;
	event->eventType = (unsigned char)eventType;
	event->power = (unsigned char)power;
	event->state = (unsigned char)state;
	event->rangeBucket = (unsigned char)rangeBucket;
	event->sequenceId = (unsigned short)((runtime->currentSequenceId > 0) ? runtime->currentSequenceId : 0);
	G_FillTrackedEventContext(event, self, enemy, captureGeometry);
	G_FillTrackedEventCoachingContext(event, eventType,
		runtime->lastAttackTime,
		runtime->lastAttackSwingSide,
		runtime->lastAttackStrafeDir,
		runtime->lastThrowTime,
		runtime->lastThrowYawOffset,
		&runtime->swing,
		self,
		enemy);
	G_SetTrackedSequenceLabel(event->sequenceLabel, sizeof(event->sequenceLabel), eventType, power, note, self, enemy);
	Q_strncpyz(event->quality, G_GetTrackedEventQualityName(event), sizeof(event->quality));
	if (note)
		Q_strncpyz(event->note, note, sizeof(event->note));
	if (!Q_stricmp(event->quality, "mistake") || !Q_stricmp(event->quality, "bad"))
	{
		Q_strncpyz(runtime->lastBadSequenceLabel, event->sequenceLabel, sizeof(runtime->lastBadSequenceLabel));
	}
	else if (!Q_stricmp(event->quality, "best") || !Q_stricmp(event->quality, "good"))
	{
		Q_strncpyz(runtime->lastGoodSequenceLabel, event->sequenceLabel, sizeof(runtime->lastGoodSequenceLabel));
	}
}

static duel_track_power_t G_InferTrackedPowerSpendArcade(gentity_t *ent, tracked_arcade_runtime_t *runtime)
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
		const int bit = 1 << sustainedPowers[i];
		if ((runtime->lastPowersActive & bit) || (ent->client->ps.fd.forcePowersActive & bit))
		{
			mappedPower = G_MapForcePowerToTrackedPower(sustainedPowers[i]);
			if (mappedPower != DUEL_TRACK_POWER_UNKNOWN)
				return mappedPower;
		}
	}

	mappedPower = G_MapForcePowerToTrackedPower(runtime->lastSelectedPower);
	return mappedPower;
}

static gentity_t *G_GetTrackedArcadePrimaryOpponent(gentity_t *ent)
{
	gentity_t *bestManaged = NULL;
	gentity_t *bestBot = NULL;
	gentity_t *bestFallback = NULL;
	float bestManagedDist = 0.0f;
	float bestBotDist = 0.0f;
	float bestFallbackDist = 0.0f;
	int i;

	if (!ent || !ent->client)
		return NULL;

	for (i = 0; i < MAX_CLIENTS; i++)
	{
		gentity_t *other = &g_entities[i];
		vec3_t diff;
		float distSq;

		if (other == ent || !other->inuse || !other->client ||
			other->client->pers.connected != CON_CONNECTED ||
			other->client->sess.sessionTeam == TEAM_SPECTATOR ||
			other->health < 1)
		{
			continue;
		}

		VectorSubtract(ent->client->ps.origin, other->client->ps.origin, diff);
		distSq = DotProduct(diff, diff);

		if (level.arcadeManagedBot[i])
		{
			if (!bestManaged || distSq < bestManagedDist)
			{
				bestManaged = other;
				bestManagedDist = distSq;
			}
			continue;
		}
		if (other->r.svFlags & SVF_BOT)
		{
			if (!bestBot || distSq < bestBotDist)
			{
				bestBot = other;
				bestBotDist = distSq;
			}
			continue;
		}
		if (level.arcadeParticipant[i] && !level.arcadeEliminated[i])
		{
			if (!bestFallback || distSq < bestFallbackDist)
			{
				bestFallback = other;
				bestFallbackDist = distSq;
			}
		}
	}

	if (bestManaged)
		return bestManaged;
	if (bestBot)
		return bestBot;
	return bestFallback;
}

static int G_GetTrackedCombatHealthArmor(gentity_t *ent)
{
	if (!ent || !ent->client)
		return 0;
	return ent->health + ent->client->ps.stats[STAT_ARMOR];
}

static void G_TouchTrackedSequenceState(int *currentSequenceId, int *lastSequenceTime)
{
	if (!currentSequenceId || !lastSequenceTime)
		return;

	if (*currentSequenceId <= 0 ||
		level.time - *lastSequenceTime > TRACKED_SEQUENCE_TIMEOUT_MS)
	{
		(*currentSequenceId)++;
		if (*currentSequenceId <= 0)
			*currentSequenceId = 1;
	}
	*lastSequenceTime = level.time;
}

static void G_TouchTrackedDuelSequence(tracked_duel_runtime_t *runtime)
{
	if (!runtime)
		return;

	G_TouchTrackedSequenceState(&runtime->currentSequenceId, &runtime->lastSequenceTime);
}

static void G_TouchTrackedArcadeSequence(tracked_arcade_runtime_t *runtime)
{
	if (!runtime)
		return;

	G_TouchTrackedSequenceState(&runtime->currentSequenceId, &runtime->lastSequenceTime);
}

static void G_AddTrackedArcadeEvent(tracked_arcade_runtime_t *runtime, int eventType, int relTime, int amount, duel_track_power_t power, int state, int rangeBucket, const char *note, gentity_t *self, gentity_t *enemy)
{
	tracked_duel_event_t *event;
	const qboolean captureGeometry = G_ShouldCaptureTrackedGeometryEvent(eventType);

	if (!runtime || !runtime->active)
		return;

	event = G_ReserveTrackedEvent(&runtime->events, &runtime->eventCount, &runtime->capture, eventType);
	if (!event)
		return;
	memset(event, 0, sizeof(*event));
	event->relTime = relTime;
	event->amount = amount;
	event->eventIndex = runtime->capture.total;
	event->eventType = (unsigned char)eventType;
	event->power = (unsigned char)power;
	event->state = (unsigned char)state;
	event->rangeBucket = (unsigned char)rangeBucket;
	event->sequenceId = (unsigned short)((runtime->currentSequenceId > 0) ? runtime->currentSequenceId : 0);
	G_FillTrackedEventContext(event, self, enemy, captureGeometry);
	G_FillTrackedEventCoachingContext(event, eventType,
		runtime->lastAttackTime,
		runtime->lastAttackSwingSide,
		runtime->lastAttackStrafeDir,
		runtime->lastThrowTime,
		runtime->lastThrowYawOffset,
		&runtime->swing,
		self,
		enemy);
	G_SetTrackedSequenceLabel(event->sequenceLabel, sizeof(event->sequenceLabel), eventType, power, note, self, enemy);
	Q_strncpyz(event->quality, G_GetTrackedEventQualityName(event), sizeof(event->quality));
	if (note)
		Q_strncpyz(event->note, note, sizeof(event->note));
}

static void G_SetTrackedPrimaryIssue(tracked_duel_runtime_t *runtime, qboolean lowForceFinish, qboolean won)
{
	int lowForceScore;
	int gripScore;
	int saberThrowScore;
	int knockdownScore;
	int lateDefenseScore;
	int forcedEntryScore;
	int linearScore;
	int totalConfirms;
	int bestScore;
	const char *bestIssue;

	if (!runtime)
		return;

	lowForceScore = ((lowForceFinish || runtime->didDieLowForce) ? TRACKED_DUEL_SCORE_LOW_FORCE_FINISH_BONUS : 0) +
		(runtime->spentByState[DUEL_TRACK_STATE_PANIC] / TRACKED_DUEL_SCORE_SPENT_PANIC_DIVISOR) +
		runtime->lowForceWindows;
	//Spending force down to empty on the way to a win is deliberate pressure, not a problem.
	if (won)
		lowForceScore = 0;
	gripScore = runtime->gripCrippleEvents * TRACKED_DUEL_SCORE_GRIP_EVENT_WEIGHT;
	saberThrowScore = (runtime->saberThrowPunishes * TRACKED_DUEL_SCORE_SABER_THROW_PUNISH_WEIGHT) - runtime->antiThrowSuccessEvents;
	knockdownScore = runtime->knockdownEvents * TRACKED_DUEL_SCORE_KNOCKDOWN_EVENT_WEIGHT;
	lateDefenseScore = (runtime->lateDefenseSpends * TRACKED_DUEL_SCORE_LATE_DEFENSE_WEIGHT) + runtime->overcommitEvents;
	totalConfirms = runtime->punishConfirmEvents;
	forcedEntryScore = 0;
	if (runtime->spentByState[DUEL_TRACK_STATE_DISADVANTAGE] >= runtime->spentByState[DUEL_TRACK_STATE_ADVANTAGE] + TRACKED_DUEL_SCORE_FORCED_ENTRY_SPENT_GAP)
	{
		forcedEntryScore += TRACKED_DUEL_SCORE_FORCED_ENTRY_SPENT_BONUS;
	}
	if (runtime->spentByState[DUEL_TRACK_STATE_FINISHING] > runtime->spentByState[DUEL_TRACK_STATE_ADVANTAGE] &&
		totalConfirms <= 0)
	{
		forcedEntryScore += TRACKED_DUEL_SCORE_FORCED_ENTRY_NO_CONFIRM_BONUS;
	}
	linearScore = (totalConfirms <= 0) ? TRACKED_DUEL_SCORE_LINEAR_FALLBACK_BASE : 0;

	bestIssue = "linear_entries";
	bestScore = linearScore;
	if (lowForceScore > bestScore)
	{
		bestScore = lowForceScore;
		bestIssue = "low_force";
	}
	if (gripScore > bestScore)
	{
		bestScore = gripScore;
		bestIssue = "grip_control";
	}
	if (saberThrowScore > bestScore)
	{
		bestScore = saberThrowScore;
		bestIssue = "saber_throw";
	}
	if (knockdownScore > bestScore)
	{
		bestScore = knockdownScore;
		bestIssue = "knockdown";
	}
	if (lateDefenseScore > bestScore)
	{
		bestScore = lateDefenseScore;
		bestIssue = "late_defense";
	}
	if (forcedEntryScore > bestScore)
	{
		bestScore = forcedEntryScore;
		bestIssue = "forced_entries";
	}

	if (bestScore <= 0)
	{
		bestIssue = (runtime->opponentSide == FORCE_LIGHTSIDE) ? "forced_entries" : "linear_entries";
	}
	Q_strncpyz(runtime->primaryIssue, bestIssue, sizeof(runtime->primaryIssue));
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

static qboolean G_ShouldSuppressRepeatedIssueAdvice(const tracked_duel_runtime_t *runtime, duel_advice_session_state_t *session, duel_track_issue_t issue)
{
	unsigned int opponentHash;

	if (!runtime || !session || issue < 0 || issue >= DUEL_TRACK_ISSUE_COUNT)
	{
		return qfalse;
	}
	opponentHash = G_HashTrackedIdentityString(runtime->opponentKey);
	if (session->lastIssueAdvised != issue)
	{
		return qfalse;
	}
	if (session->lastIssueAdviceDuel != session->duelsSeen)
	{
		return qfalse;
	}
	if (session->lastOpponentHash != opponentHash)
	{
		return qfalse;
	}

	return (level.time < session->lastIssueAdviceTime + 20000) ? qtrue : qfalse;
}

static void G_MarkTrackedIssueAdvice(duel_advice_session_state_t *session, const tracked_duel_runtime_t *runtime, duel_track_issue_t issue)
{
	if (!session || !runtime || issue < 0 || issue >= DUEL_TRACK_ISSUE_COUNT)
	{
		return;
	}

	session->lastIssueAdvised = issue;
	session->lastIssueAdviceTime = level.time;
	session->lastIssueAdviceDuel = session->duelsSeen;
	session->lastOpponentHash = G_HashTrackedIdentityString(runtime->opponentKey);
}

static void G_FormatTrackedSequenceLabel(const char *label, char *out, int outSize)
{
	int i, outIndex;
	qboolean capitalizeNext = qtrue;

	if (!out || outSize <= 0)
		return;
	out[0] = '\0';

	if (!label || !label[0])
	{
		Q_strncpyz(out, "sequence", outSize);
		return;
	}

	for (i = 0, outIndex = 0; label[i] && outIndex < outSize - 1; i++)
	{
		char ch = label[i];

		if (ch == '_')
		{
			out[outIndex++] = ' ';
			capitalizeNext = qtrue;
			continue;
		}
		if (capitalizeNext && ch >= 'a' && ch <= 'z')
		{
			ch = (char)(ch - ('a' - 'A'));
		}
		else if (!capitalizeNext && ch >= 'A' && ch <= 'Z')
		{
			ch = (char)(ch + ('a' - 'A'));
		}
		out[outIndex++] = ch;
		capitalizeNext = qfalse;
	}
	out[outIndex] = '\0';
}

static void G_QueueManualBasicsAdvice(int botClientNum, int targetClientNum, int duelIndex, duel_advice_session_state_t *session)
{
	int slot;
	static const char *manualBasics[] = {
		"PK means Pull Kick. Use it after committed movement, knockdowns, or saber recovery windows.",
		"GK means Grip Kick. Keep grip and kick binds clean so timing stays reliable under pressure.",
		"PTK means Pull-Throw-Kick. Threaten space first, then convert only on real recovery windows.",
		"Force management first: preserve exit force before re-committing to pressure.",
		"Lane basics: a lane is your approach angle into threat range; rotate lanes to stay less readable.",
		"After lane basics are stable, layer movement variation and timing changes into your entries."
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
		"Meta tempo: bait first, then spend into the lane they just exposed.",
		"Meta rhythm: rotate entry timing each exchange so they cannot pre-read cadence.",
		"Meta reserve: keep exit force banked, then convert advantage with short checks.",
		"Meta spacing: camera and footwork should be clean before hard grip/pull commits.",
		"Meta composure: hide panic tells by keeping movement quality steady at low HP.",
		"Meta punish: if they copy your last option, change lane immediately and counter."
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
		"Intermediate: shape pull pressure with throw feints, then convert only on exposed recovery.",
		"Intermediate tempo: spend in bursts, reset, then re-enter from lateral movement.",
		"Intermediate anti-grip: vary delay, down-state, and exit direction to break reads.",
		"Intermediate side-kick: use it as spacing control, but respect regen pause until landing.",
		"Intermediate anti-throw: punish recall windows and return path, not just launch timing."
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
		"Balanced offense rule: split pressure across PTK/pull-throw, saber checks, and GK.",
		"Drain discipline: controlled taps beat panic holds and keep force economy efficient.",
		"If punish lane is still live, delay stand-up to deny free follow-up damage.",
		"Anti-throw discipline: track blade return path and punish recall windows.",
		"GK variation wins: rotate kick type, turn angle, and yank timing every attempt.",
		"Entry strategy: use movement gap and strafe pressure before force commitment.",
		"Anti-drain strategy: rotate counters so one repeated answer cannot be farmed.",
		"Map strategy: know chase routes, hide routes, and force ranges before hard commits.",
		"Spacing control: close only when camera and footwork constrain snap answers.",
		"Short saber checks can create safer pull/grip conversions than force-first entries."
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
		"Tip: use /login or /register once to save progress and coaching history between sessions."
	};

	if (!session || loggedIn || session->loginReminderSent ||
		session->duelsSeen < TRACKED_DUEL_ADVICE_LOGIN_MIN_DUELS)
	{
		return;
	}

	G_QueueRotatingTutorialMessage(botClientNum, targetClientNum, loginAdvice,
		(int)(sizeof(loginAdvice) / sizeof(loginAdvice[0])), session->duelsSeen, session);
	session->loginReminderSent = qtrue;
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
	qboolean suppressRepeatedIssueAdvice;
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
	suppressRepeatedIssueAdvice = allowSpecificIssue ? G_ShouldSuppressRepeatedIssueAdvice(loserRuntime, session, issue) : qfalse;
	skillBand = G_GetTrackedSkillBand(loserRuntime, session, 1, 0);
	g_botTutorialQueues[botClientNum].cooldownMs = TRACKED_DUEL_TUTORIAL_COOLDOWN_MS;

	basicsWindow = (skillBand == DUEL_TRACK_SKILL_BEGINNER) &&
		((!loggedIn || (session->historyDuels <= 0)) ||
			(session->duelsSeen <= TRACKED_DUEL_ADVICE_BASIC_WINDOW));
	if (basicsWindow)
	{
		const int queuedCountBefore = g_botTutorialQueues[botClientNum].queuedCount;
		const qboolean queueWasEmpty = (queuedCountBefore <= g_botTutorialQueues[botClientNum].nextMessageIndex);
		const int basicsRotation = (session->duelsSeen > 0) ? (session->duelsSeen - 1) : 0;

		G_QueueManualBasicsAdvice(botClientNum, loser->s.number, basicsRotation, session);
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
		char sequenceLabel[32];
		char sequenceMessage[128];

		if (lowSignalDuel)
		{
			G_QueueManualGenericAdvice(botClientNum, loser->s.number, loserRuntime, loggedIn, session);
		}
		else if (allowSpecificIssue && !suppressRepeatedIssueAdvice)
		{
			G_QueueManualIssueAdvice(botClientNum, loser->s.number, issue, session);
			G_MarkTrackedIssueAdvice(session, loserRuntime, issue);
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
		if (loserRuntime->lastBadSequenceLabel[0])
		{
			G_FormatTrackedSequenceLabel(loserRuntime->lastBadSequenceLabel, sequenceLabel, sizeof(sequenceLabel));
			Com_sprintf(sequenceMessage, sizeof(sequenceMessage),
				"Sequence read: your %s line broke down. Reset or vary the timing before the collapse frame.",
				sequenceLabel);
			G_QueueBotTutorialMessage(botClientNum, loser->s.number, sequenceMessage);
		}
		else if (loserRuntime->lastGoodSequenceLabel[0])
		{
			G_FormatTrackedSequenceLabel(loserRuntime->lastGoodSequenceLabel, sequenceLabel, sizeof(sequenceLabel));
			Com_sprintf(sequenceMessage, sizeof(sequenceMessage),
				"Keep the %s shape—it was one of your cleaner conversions this duel.",
				sequenceLabel);
			G_QueueBotTutorialMessage(botClientNum, loser->s.number, sequenceMessage);
		}
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

	if (bot_tutorial.integer < 1 || bot_nochat.integer || !speaker || !listener ||
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
	if (bot_tutorial.integer == 1)
	{
		// trainer mode at 1 is strict private tell coaching only
		if (betweenRounds || speaker == listener)
		{
			return;
		}
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
		if (rotation <= TRACKED_DUEL_ADVICE_BASIC_WINDOW)
		{
			G_QueueManualBasicsAdvice(speaker->s.number, listener->s.number, rotation, NULL);
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
		if (bot_tutorial.integer >= 2)
		{
			queue->publicBroadcast = qtrue;
		}
		else
		{
			queue->publicBroadcast = qfalse;
		}
		G_SetBotTutorialInitialDelay(queue);
	}
}

static void G_InitTrackedDuelRuntimeForClient(gentity_t *ent, gentity_t *opponent, int duelType)
{
	tracked_duel_runtime_t *runtime;

	if (!ent || !ent->client || !opponent || !opponent->client)
		return;

	runtime = &g_trackedDuels[ent->s.number];
	G_DuelCaptureClearRuntime(runtime, sizeof(*runtime), runtime->events);
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
	runtime->lastButtons = ent->client->pers.cmd.buttons;
	runtime->lastSaberMove = ent->client->ps.saberMove;
	runtime->lastSaberInFlight = ent->client->ps.saberInFlight ? 1 : 0;
	runtime->lastRightMove = ent->client->pers.cmd.rightmove;
	runtime->swing.lastYaw = ent->client->ps.viewangles[YAW];
	runtime->lastOpponentHealthArmor = G_GetTrackedCombatHealthArmor(opponent);
	runtime->pendingResetRecovery = 0;
	runtime->side = G_GetTrackedParticipantSide(ent);
	runtime->opponentSide = G_GetTrackedParticipantSide(opponent);
	G_GetDuelTrackingIdentity(ent, runtime->identityKey, sizeof(runtime->identityKey), runtime->identityLabel, sizeof(runtime->identityLabel), &runtime->identityKind);
	if (!G_GetDuelParticipantName(ent, runtime->eloKey, sizeof(runtime->eloKey)))
		runtime->eloKey[0] = '\0';
	if (!runtime->eloKey[0] && ent->client->ps.duelInProgress && G_IsGuestDuelKey(ent->client->pers.lastUserName))
		Q_strncpyz(runtime->eloKey, ent->client->pers.lastUserName, sizeof(runtime->eloKey));
	G_GetDuelTrackingIdentity(opponent, runtime->opponentKey, sizeof(runtime->opponentKey), runtime->opponentLabel, sizeof(runtime->opponentLabel), NULL);
}

static duel_track_power_t G_InferTrackedPowerSpend(gentity_t *ent, tracked_duel_runtime_t *runtime, qboolean *confirmed)
{
	static const int sustainedPowers[] = { FP_GRIP, FP_DRAIN, FP_ABSORB, FP_PROTECT, FP_SPEED, FP_SEE, FP_RAGE };
	int i;
	duel_track_power_t mappedPower;

	if (confirmed)
		*confirmed = qfalse;
	if (!ent || !ent->client || !runtime)
		return DUEL_TRACK_POWER_UNKNOWN;

	//Record the power actually used, not the selected one: players using direct binds
	//(force_pull, force_drain, ...) rarely change forcePowerSelected, so the selection
	//only identifies the spend as a last resort.
	for (i = 0; i < NUM_FORCE_POWERS; i++)
	{
		const int bit = (1 << i);

		if ((ent->client->ps.fd.forcePowersActive & bit) && !(runtime->lastPowersActive & bit))
		{
			mappedPower = G_MapForcePowerToTrackedPower(i);
			if (mappedPower != DUEL_TRACK_POWER_UNKNOWN)
			{
				if (confirmed)
					*confirmed = qtrue;
				return mappedPower;
			}
		}
	}

	if (ent->client->ps.forceHandExtendTime > level.time)
	{
		if (confirmed && (ent->client->ps.forceHandExtend == HANDEXTEND_FORCEPULL ||
			ent->client->ps.forceHandExtend == HANDEXTEND_FORCEPUSH))
			*confirmed = qtrue;
		if (ent->client->ps.forceHandExtend == HANDEXTEND_FORCEPULL)
			return DUEL_TRACK_POWER_PULL;
		if (ent->client->ps.forceHandExtend == HANDEXTEND_FORCEPUSH)
			return DUEL_TRACK_POWER_PUSH;
	}

	for (i = 0; i < (int)(sizeof(sustainedPowers) / sizeof(sustainedPowers[0])); i++)
	{
		if (ent->client->ps.fd.forcePowersActive & (1 << sustainedPowers[i]))
		{
			if (confirmed)
				*confirmed = qtrue;
			return G_MapForcePowerToTrackedPower(sustainedPowers[i]);
		}
	}

	mappedPower = G_MapForcePowerToTrackedPower(ent->client->ps.fd.forcePowerSelected);
	if (mappedPower != DUEL_TRACK_POWER_UNKNOWN)
		return mappedPower;

	return G_MapForcePowerToTrackedPower(runtime->lastSelectedPower);
}

static qboolean G_InsertTrackedCaptureDiagnostics(sqlite3 *db, sqlite3_int64 summaryId,
	const char *identityKey, const duel_capture_storage_t *capture, int retained)
{
	sqlite3_stmt *stmt = NULL;
	const char *sql = "UPDATE LocalDuelTrackParticipant SET event_total=?, event_retained=?, event_dropped=?, event_critical_dropped=?, event_compactions=?, event_allocation_failures=? WHERE summary_id=? AND participant_key=?";
	int s = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (s == SQLITE_OK)
	{
		sqlite3_bind_int64(stmt, 1, (sqlite3_int64)capture->total);
		sqlite3_bind_int(stmt, 2, retained);
		sqlite3_bind_int64(stmt, 3, (sqlite3_int64)capture->dropped);
		sqlite3_bind_int64(stmt, 4, (sqlite3_int64)capture->criticalDropped);
		sqlite3_bind_int64(stmt, 5, capture->compactions);
		sqlite3_bind_int64(stmt, 6, capture->allocationFailures);
		sqlite3_bind_int64(stmt, 7, summaryId);
		sqlite3_bind_text(stmt, 8, identityKey, -1, SQLITE_STATIC);
		s = sqlite3_step(stmt);
	}
	if (s != SQLITE_DONE)
		G_TrackedDBError("write capture diagnostics", db, s);
	sqlite3_finalize(stmt);
	return s == SQLITE_DONE ? qtrue : qfalse;
}

static qboolean G_InsertTrackedParticipant(sqlite3 *db, sqlite3_int64 summaryId, tracked_duel_runtime_t *runtime, int won)
{
	sqlite3_stmt *stmt = NULL;
	char *sql;
	int s;
	int matchup;

	if (!runtime)
		return qfalse;

	matchup = G_GetTrackedMatchup(runtime->side, runtime->opponentSide);
	sql = "INSERT INTO LocalDuelTrackParticipant(summary_id, participant_key, participant_label, participant_kind, elo_key, opponent_key, won, side, opponent_side, matchup, total_force_spent, total_force_regen, ending_force, ending_hp, ending_armor, low_force_windows, grip_cripple_events, saber_throw_punishes, knockdown_events, late_defense_spends, opening_tactic, primary_issue, spent_neutral, spent_advantage, spent_disadvantage, spent_panic, spent_finishing, force_push, force_pull, force_grip, force_drain, force_rage, force_absorb, force_protect, force_heal, force_speed, force_seeing, force_unknown, total_damage_taken, total_damage_dealt, counter_successes, punish_successes, reset_successes, total_kills) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_int64(stmt, 1, summaryId));
	CALL_SQLITE(bind_text(stmt, 2, runtime->identityKey, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 3, runtime->identityLabel, -1, SQLITE_TRANSIENT));
	CALL_SQLITE(bind_int(stmt, 4, runtime->identityKind));
	CALL_SQLITE(bind_text(stmt, 5, runtime->eloKey, -1, SQLITE_TRANSIENT));
	CALL_SQLITE(bind_text(stmt, 6, runtime->opponentKey, -1, SQLITE_STATIC));
	if (won < 0)
	{
		CALL_SQLITE(bind_null(stmt, 7));
	}
	else
	{
		CALL_SQLITE(bind_int(stmt, 7, won ? 1 : 0));
	}
	CALL_SQLITE(bind_int(stmt, 8, runtime->side));
	CALL_SQLITE(bind_int(stmt, 9, runtime->opponentSide));
	CALL_SQLITE(bind_int(stmt, 10, matchup));
	CALL_SQLITE(bind_int(stmt, 11, runtime->totalForceSpent));
	CALL_SQLITE(bind_int(stmt, 12, runtime->totalForceRegen));
	CALL_SQLITE(bind_int(stmt, 13, runtime->endingForce));
	CALL_SQLITE(bind_int(stmt, 14, runtime->endingHP));
	CALL_SQLITE(bind_int(stmt, 15, runtime->endingArmor));
	CALL_SQLITE(bind_int(stmt, 16, runtime->lowForceWindows));
	CALL_SQLITE(bind_int(stmt, 17, runtime->gripCrippleEvents));
	CALL_SQLITE(bind_int(stmt, 18, runtime->saberThrowPunishes));
	CALL_SQLITE(bind_int(stmt, 19, runtime->knockdownEvents));
	CALL_SQLITE(bind_int(stmt, 20, runtime->lateDefenseSpends));
	CALL_SQLITE(bind_text(stmt, 21, runtime->openingTactic, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 22, runtime->primaryIssue, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 23, runtime->spentByState[DUEL_TRACK_STATE_NEUTRAL]));
	CALL_SQLITE(bind_int(stmt, 24, runtime->spentByState[DUEL_TRACK_STATE_ADVANTAGE]));
	CALL_SQLITE(bind_int(stmt, 25, runtime->spentByState[DUEL_TRACK_STATE_DISADVANTAGE]));
	CALL_SQLITE(bind_int(stmt, 26, runtime->spentByState[DUEL_TRACK_STATE_PANIC]));
	CALL_SQLITE(bind_int(stmt, 27, runtime->spentByState[DUEL_TRACK_STATE_FINISHING]));
	CALL_SQLITE(bind_int(stmt, 28, runtime->forceSpentByPower[DUEL_TRACK_POWER_PUSH]));
	CALL_SQLITE(bind_int(stmt, 29, runtime->forceSpentByPower[DUEL_TRACK_POWER_PULL]));
	CALL_SQLITE(bind_int(stmt, 30, runtime->forceSpentByPower[DUEL_TRACK_POWER_GRIP]));
	CALL_SQLITE(bind_int(stmt, 31, runtime->forceSpentByPower[DUEL_TRACK_POWER_DRAIN]));
	CALL_SQLITE(bind_int(stmt, 32, runtime->forceSpentByPower[DUEL_TRACK_POWER_RAGE]));
	CALL_SQLITE(bind_int(stmt, 33, runtime->forceSpentByPower[DUEL_TRACK_POWER_ABSORB]));
	CALL_SQLITE(bind_int(stmt, 34, runtime->forceSpentByPower[DUEL_TRACK_POWER_PROTECT]));
	CALL_SQLITE(bind_int(stmt, 35, runtime->forceSpentByPower[DUEL_TRACK_POWER_HEAL]));
	CALL_SQLITE(bind_int(stmt, 36, runtime->forceSpentByPower[DUEL_TRACK_POWER_SPEED]));
	CALL_SQLITE(bind_int(stmt, 37, runtime->forceSpentByPower[DUEL_TRACK_POWER_SEEING]));
	CALL_SQLITE(bind_int(stmt, 38, runtime->forceSpentByPower[DUEL_TRACK_POWER_UNKNOWN]));
	CALL_SQLITE(bind_int(stmt, 39, runtime->totalDamageTaken));
	CALL_SQLITE(bind_int(stmt, 40, runtime->totalDamageDealt));
	CALL_SQLITE(bind_int(stmt, 41, runtime->counterSuccessEvents));
	CALL_SQLITE(bind_int(stmt, 42, runtime->punishSuccessEvents));
	CALL_SQLITE(bind_int(stmt, 43, runtime->resetSuccessEvents));
	CALL_SQLITE(bind_int(stmt, 44, runtime->totalKills));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Insert Failed (LocalDuelTrackParticipant)", s);
		G_TrackedDBError("insert LocalDuelTrackParticipant", db, s);
		CALL_SQLITE(finalize(stmt));
		return qfalse;
	}
	CALL_SQLITE(finalize(stmt));
	return G_InsertTrackedCaptureDiagnostics(db, summaryId, runtime->identityKey, &runtime->capture, runtime->eventCount);
}

static qboolean G_InsertTrackedEvents(sqlite3 *db, sqlite3_int64 summaryId, tracked_duel_runtime_t *runtime)
{
	sqlite3_stmt *stmt = NULL;
	sqlite3_stmt *geomStmt = NULL;
	char *sql;
	int i;
	int s;
	qboolean captureGeometry = qfalse;
	qboolean hasAnyGeometry = qfalse;

	if (!runtime || runtime->eventCount <= 0)
		return qtrue;

	sql = "INSERT INTO LocalDuelTrackEvent(summary_id, participant_key, opponent_key, rel_time, event_index, event_type, power, amount, state, range_bucket, sequence_id, buttons, saber_move, enemy_saber_move, yaw_delta, opponent_label, opponent_kind, self_hp, self_armor, self_force, enemy_hp, enemy_armor, enemy_force, sequence_label, quality, note, swing_side, pre_swing_strafe, movement_intent, radial_speed, yaw_sweep, attack_elapsed_ms, throw_yaw_offset, participant_label, participant_kind, forwardmove, rightmove, upmove, saber_stance, grounded, damage_source, damage_attacker_key, controller_owns_inputs, controller_family, controller_enabled, controller_fanbias, controller_candidate, controller_mistakebias, controller_skill) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
	s = sqlite3_prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL);
	if (s != SQLITE_OK || !stmt)
	{
		G_ErrorPrint("ERROR: SQL Prepare Failed (LocalDuelTrackEvent)", s);
		G_TrackedDBError("prepare LocalDuelTrackEvent", db, s);
		if (stmt)
			CALL_SQLITE(finalize(stmt));
		return qfalse;
	}
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
		s = sqlite3_prepare_v2(db, sql, strlen(sql) + 1, &geomStmt, NULL);
		if (s != SQLITE_OK || !geomStmt)
		{
			G_ErrorPrint("ERROR: SQL Prepare Failed (LocalDuelTrackGeometry)", s);
			G_TrackedDBError("prepare LocalDuelTrackGeometry", db, s);
			if (stmt)
			{
				sqlite3_finalize(stmt);
				stmt = NULL;
			}
			if (geomStmt)
			{
				sqlite3_finalize(geomStmt);
				geomStmt = NULL;
			}
			return qfalse;
		}
	}
	for (i = 0; i < runtime->eventCount; i++)
	{
		tracked_duel_event_t *event = &runtime->events[i];
		CALL_SQLITE(bind_int64(stmt, 1, summaryId));
		CALL_SQLITE(bind_text(stmt, 2, runtime->identityKey, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_text(stmt, 3, runtime->opponentKey, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_int(stmt, 4, event->relTime));
		CALL_SQLITE(bind_int64(stmt, 5, (sqlite3_int64)event->eventIndex));
		CALL_SQLITE(bind_text(stmt, 6, G_GetTrackedEventTypeName(event->eventType), -1, SQLITE_STATIC));
		CALL_SQLITE(bind_int(stmt, 7, event->power));
		CALL_SQLITE(bind_int(stmt, 8, event->amount));
		CALL_SQLITE(bind_int(stmt, 9, event->state));
		CALL_SQLITE(bind_int(stmt, 10, event->rangeBucket));
		CALL_SQLITE(bind_int(stmt, 11, event->sequenceId));
		CALL_SQLITE(bind_int(stmt, 12, event->buttons));
		CALL_SQLITE(bind_int(stmt, 13, event->saberMove));
		CALL_SQLITE(bind_int(stmt, 14, event->enemySaberMove));
		CALL_SQLITE(bind_int(stmt, 15, event->yawDelta));
		CALL_SQLITE(bind_text(stmt, 16, event->opponentLabel, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_int(stmt, 17, event->opponentKind));
		CALL_SQLITE(bind_int(stmt, 18, event->selfHealth));
		CALL_SQLITE(bind_int(stmt, 19, event->selfArmor));
		CALL_SQLITE(bind_int(stmt, 20, event->selfForce));
		CALL_SQLITE(bind_int(stmt, 21, event->enemyHealth));
		CALL_SQLITE(bind_int(stmt, 22, event->enemyArmor));
		CALL_SQLITE(bind_int(stmt, 23, event->enemyForce));
		CALL_SQLITE(bind_text(stmt, 24, event->sequenceLabel, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_text(stmt, 25, event->quality, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_text(stmt, 26, event->note, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_text(stmt, 27, event->swingSide, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_text(stmt, 28, event->preSwingStrafe, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_text(stmt, 29, event->movementIntent, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_int(stmt, 30, event->radialSpeed));
		CALL_SQLITE(bind_int(stmt, 31, event->yawSweep));
		CALL_SQLITE(bind_int(stmt, 32, event->attackElapsedMs));
		CALL_SQLITE(bind_int(stmt, 33, event->throwYawOffset));
		CALL_SQLITE(bind_text(stmt, 34, runtime->identityLabel, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_int(stmt, 35, runtime->identityKind));
		CALL_SQLITE(bind_int(stmt, 36, event->forwardmove));
		CALL_SQLITE(bind_int(stmt, 37, event->rightmove));
		CALL_SQLITE(bind_int(stmt, 38, event->upmove));
		CALL_SQLITE(bind_int(stmt, 39, event->saberStance));
		CALL_SQLITE(bind_int(stmt, 40, event->grounded));
		CALL_SQLITE(bind_int(stmt, 41, event->damageSource));
		CALL_SQLITE(bind_int(stmt, 43, event->controllerOwnsInputs));
		CALL_SQLITE(bind_int(stmt, 44, event->controllerFamily));
		CALL_SQLITE(bind_int(stmt, 45, event->controllerEnabled));
		CALL_SQLITE(bind_int(stmt, 46, event->controllerFanBias));
		CALL_SQLITE(bind_int(stmt, 47, event->controllerCandidate));
		CALL_SQLITE(bind_int(stmt, 48, event->controllerMistakeBias));
		CALL_SQLITE(bind_double(stmt, 49, event->controllerSkill));
		CALL_SQLITE(bind_text(stmt, 42, event->damageAttackerKey, -1, SQLITE_TRANSIENT));
		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE)
		{
			G_ErrorPrint("ERROR: SQL Insert Failed (LocalDuelTrackEvent)", s);
			G_TrackedDBError("insert LocalDuelTrackEvent", db, s);
			CALL_SQLITE(finalize(stmt));
			if (geomStmt)
				CALL_SQLITE(finalize(geomStmt));
			return qfalse;
		}
		CALL_SQLITE(reset(stmt));
		CALL_SQLITE(clear_bindings(stmt));
		if (captureGeometry && hasAnyGeometry && event->hasGeometry)
		{
			CALL_SQLITE(bind_int64(geomStmt, 1, summaryId));
			CALL_SQLITE(bind_text(geomStmt, 2, runtime->identityKey, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_text(geomStmt, 3, runtime->opponentKey, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_int(geomStmt, 4, event->relTime));
			CALL_SQLITE(bind_int64(geomStmt, 5, (sqlite3_int64)event->eventIndex));
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
				G_TrackedDBError("insert LocalDuelTrackGeometry", db, s);
				CALL_SQLITE(finalize(stmt));
				if (geomStmt)
					CALL_SQLITE(finalize(geomStmt));
				return qfalse;
			}
			CALL_SQLITE(reset(geomStmt));
			CALL_SQLITE(clear_bindings(geomStmt));
		}
	}
	CALL_SQLITE(finalize(stmt));
	if (geomStmt)
		CALL_SQLITE(finalize(geomStmt));
	return qtrue;
}

static qboolean G_UpdateTrackedAggregate(sqlite3 *db, tracked_duel_runtime_t *runtime, qboolean won, qboolean draw)
{
	sqlite3_stmt *stmt = NULL;
	char *sql;
	int s;
	int matchup;

	if (!runtime)
		return qfalse;

	matchup = G_GetTrackedMatchup(runtime->side, runtime->opponentSide);
	sql = "INSERT OR IGNORE INTO LocalDuelTrackAggregate(participant_key, participant_kind, side, matchup, duels, wins, losses, total_force_spent, total_force_regen, low_force_deaths, grip_cripples, saber_throw_punishes, force_push, force_pull, force_grip, force_drain, force_rage, force_absorb, force_protect, force_heal, force_speed, force_seeing, force_unknown) VALUES (?, ?, ?, ?, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_text(stmt, 1, runtime->identityKey, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 2, runtime->identityKind));
	CALL_SQLITE(bind_int(stmt, 3, runtime->side));
	CALL_SQLITE(bind_int(stmt, 4, matchup));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Insert Failed (LocalDuelTrackAggregate init)", s);
		G_TrackedDBError("insert LocalDuelTrackAggregate init", db, s);
		CALL_SQLITE(finalize(stmt));
		return qfalse;
	}
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
	if (s == SQLITE_DONE)
	{
		//Stamp the capture version/revision that last touched this aggregate row.
		sqlite3_stmt *stampStmt = NULL;
		const char *stampSql = "UPDATE LocalDuelTrackAggregate SET capture_version = ?, capture_revision = ? WHERE participant_key = ? AND participant_kind = ? AND side = ? AND matchup = ?";
		if (sqlite3_prepare_v2(db, stampSql, -1, &stampStmt, NULL) == SQLITE_OK)
		{
			sqlite3_bind_int(stampStmt, 1, TRACKED_CAPTURE_VERSION);
			sqlite3_bind_text(stampStmt, 2, GIT_HASH, -1, SQLITE_STATIC);
			sqlite3_bind_text(stampStmt, 3, runtime->identityKey, -1, SQLITE_STATIC);
			sqlite3_bind_int(stampStmt, 4, runtime->identityKind);
			sqlite3_bind_int(stampStmt, 5, runtime->side);
			sqlite3_bind_int(stampStmt, 6, matchup);
			sqlite3_step(stampStmt);
		}
		sqlite3_finalize(stampStmt);
	}
	if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Update Failed (LocalDuelTrackAggregate update)", s);
		G_TrackedDBError("update LocalDuelTrackAggregate update", db, s);
		CALL_SQLITE(finalize(stmt));
		return qfalse;
	}
	CALL_SQLITE(finalize(stmt));
	return qtrue;
}

// ---------------------------------------------------------------------------------------
// Bot sequence learning: turn a finished duel's two event streams into
// stimulus -> response -> follow-up sequences and accumulate their outcomes.
// ---------------------------------------------------------------------------------------

#define BOTLEARN_MAX_DUEL_SEQUENCES 2048

typedef struct
{
	botlearn_event_t ev;
	int ownerStance;
} botlearn_merge_row_t;

static int G_BotLearnTokenForTrackedEvent(const tracked_duel_event_t *event)
{
	switch (event->eventType)
	{
	case DUEL_TRACK_EVENT_FORCE:
		if (!Q_stricmp(event->note, TRACKED_FORCE_NOTE_SELECTED_FALLBACK))
			return BOTLEARN_TOK_NONE;
		switch (event->power)
		{
		case DUEL_TRACK_POWER_PUSH: return BOTLEARN_TOK_PUSH;
		case DUEL_TRACK_POWER_PULL: return BOTLEARN_TOK_PULL;
		case DUEL_TRACK_POWER_GRIP: return BOTLEARN_TOK_GRIP;
		case DUEL_TRACK_POWER_DRAIN: return BOTLEARN_TOK_DRAIN;
		default: return BOTLEARN_TOK_NONE;
		}
	case DUEL_TRACK_EVENT_THROW_START:
		return BOTLEARN_TOK_THROW;
	case DUEL_TRACK_EVENT_SWING_START:
		return (event->amount >= LS_KICK_F && event->amount <= LS_KICK_L_AIR) ?
			BOTLEARN_TOK_KICK : BOTLEARN_TOK_SWING;
	case DUEL_TRACK_EVENT_AIR:
		return (event->amount && !Q_stricmp(event->note, TRACKED_AIR_NOTE_JUMP)) ? BOTLEARN_TOK_JUMP : BOTLEARN_TOK_NONE;
	case DUEL_TRACK_EVENT_KNOCKDOWN:
		return BOTLEARN_TOK_KNOCKDOWN;
	default:
		return BOTLEARN_TOK_NONE;
	}
}

static int G_BotLearnAppendRows(botlearn_merge_row_t *rows, int count, int maxRows,
	const tracked_duel_runtime_t *runtime, int actor)
{
	int i;

	for (i = 0; i < runtime->eventCount && count < maxRows; i++)
	{
		const tracked_duel_event_t *event = &runtime->events[i];
		const int token = G_BotLearnTokenForTrackedEvent(event);
		const int damage = (event->eventType == DUEL_TRACK_EVENT_DAMAGE_DEALT && event->amount > 0) ? event->amount : 0;
		botlearn_merge_row_t *row;
		const int ownHA = event->selfHealth + event->selfArmor;
		const int otherHA = event->enemyHealth + event->enemyArmor;

		if (token == BOTLEARN_TOK_NONE && !damage)
			continue;
		row = &rows[count++];
		memset(row, 0, sizeof(*row));
		row->ev.time = event->relTime;
		row->ev.actor = actor;
		row->ev.token = token;
		row->ev.damage = damage;
		//Snapshots are stored from the owner's point of view; flip them into the
		//perspective participant's ("self") point of view.
		row->ev.selfHealthArmor = actor ? otherHA : ownHA;
		row->ev.enemyHealthArmor = actor ? ownHA : otherHA;
		row->ev.selfForce = actor ? event->enemyForce : event->selfForce;
		row->ev.enemyForce = actor ? event->selfForce : event->enemyForce;
		row->ev.rangeBucket = (event->rangeBucket > 0) ? event->rangeBucket - 1 : 0;
		if (row->ev.rangeBucket > 2)
			row->ev.rangeBucket = 2;
		row->ownerStance = event->saberStance;
	}
	return count;
}

static int QDECL G_BotLearnCompareRows(const void *a, const void *b)
{
	const botlearn_merge_row_t *ra = (const botlearn_merge_row_t *)a;
	const botlearn_merge_row_t *rb = (const botlearn_merge_row_t *)b;

	if (ra->ev.time != rb->ev.time)
		return ra->ev.time - rb->ev.time;
	//Opponent rows first on ties so a same-frame answer still counts as a response.
	return rb->ev.actor - ra->ev.actor;
}

static int G_BotLearnSkillBand(const tracked_duel_runtime_t *runtime)
{
	int skill = 0;

	if (runtime->identityKind != DUEL_TRACK_ID_BOT)
		return BotLearn_SkillBand(0, 0);
	if (!Q_strncmp(runtime->eloKey, "botlvl", 6))
		skill = atoi(runtime->eloKey + 6);
	else if (runtime->eventCount > 0)
		skill = (int)runtime->events[0].controllerSkill;
	return BotLearn_SkillBand(1, skill);
}

static qboolean G_BotLearnRecordPerspective(sqlite3 *db, const tracked_duel_runtime_t *self,
	const tracked_duel_runtime_t *opponent, qboolean won)
{
	botlearn_merge_row_t *rows;
	botlearn_event_t *events;
	botlearn_sequence_t *sequences;
	sqlite3_stmt *insertStmt = NULL;
	sqlite3_stmt *updateStmt = NULL;
	const int maxRows = self->eventCount + opponent->eventCount;
	const int sourceKind = (self->identityKind == DUEL_TRACK_ID_BOT) ? BOTLEARN_SOURCE_BOT : BOTLEARN_SOURCE_HUMAN;
	const int skillBand = G_BotLearnSkillBand(self);
	int lastStance[2] = { 0, 0 };
	int count = 0, sequenceCount, i;
	qboolean ok = qtrue;

	if (!self->events || !opponent->events || maxRows <= 0)
		return qtrue;

	rows = (botlearn_merge_row_t *)malloc(sizeof(*rows) * maxRows);
	events = (botlearn_event_t *)malloc(sizeof(*events) * maxRows);
	sequences = (botlearn_sequence_t *)malloc(sizeof(*sequences) * BOTLEARN_MAX_DUEL_SEQUENCES);
	if (!rows || !events || !sequences)
	{
		free(rows);
		free(events);
		free(sequences);
		return qtrue;
	}

	count = G_BotLearnAppendRows(rows, count, maxRows, self, 0);
	count = G_BotLearnAppendRows(rows, count, maxRows, opponent, 1);
	qsort(rows, count, sizeof(rows[0]), G_BotLearnCompareRows);
	for (i = 0; i < count; i++)
	{
		lastStance[rows[i].ev.actor] = rows[i].ownerStance;
		events[i] = rows[i].ev;
		events[i].selfStance = lastStance[0];
		events[i].enemyStance = lastStance[1];
	}
	count = BotLearn_CollapseRepeats(events, count);
	sequenceCount = BotLearn_ExtractSequences(events, count, won ? 1 : 0, sequences, BOTLEARN_MAX_DUEL_SEQUENCES);

	if (sequenceCount > 0 &&
		sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO LocalBotLearnedSequence(source_kind, skill_band, ctx_key, stimulus, response, follow1, follow2) VALUES (?, ?, ?, ?, ?, ?, ?)",
			-1, &insertStmt, NULL) == SQLITE_OK &&
		sqlite3_prepare_v2(db, "UPDATE LocalBotLearnedSequence SET samples = samples + 1, wins = wins + ?, net_damage = net_damage + ?, total_response_ms = total_response_ms + ?, capture_version = ? "
			"WHERE source_kind = ? AND skill_band = ? AND ctx_key = ? AND stimulus = ? AND response = ? AND follow1 = ? AND follow2 = ?",
			-1, &updateStmt, NULL) == SQLITE_OK)
	{
		for (i = 0; i < sequenceCount && ok; i++)
		{
			const botlearn_sequence_t *seq = &sequences[i];
			int rc;

			sqlite3_reset(insertStmt);
			sqlite3_bind_int(insertStmt, 1, sourceKind);
			sqlite3_bind_int(insertStmt, 2, skillBand);
			sqlite3_bind_int(insertStmt, 3, seq->contextKey);
			sqlite3_bind_int(insertStmt, 4, seq->stimulus);
			sqlite3_bind_int(insertStmt, 5, seq->response);
			sqlite3_bind_int(insertStmt, 6, seq->follow1);
			sqlite3_bind_int(insertStmt, 7, seq->follow2);
			rc = sqlite3_step(insertStmt);
			if (rc != SQLITE_DONE)
			{
				G_TrackedDBError("insert LocalBotLearnedSequence", db, rc);
				ok = qfalse;
				break;
			}

			sqlite3_reset(updateStmt);
			sqlite3_bind_int(updateStmt, 1, seq->won);
			sqlite3_bind_int(updateStmt, 2, seq->netDamage);
			sqlite3_bind_int(updateStmt, 3, seq->responseDelayMs);
			sqlite3_bind_int(updateStmt, 4, TRACKED_CAPTURE_VERSION);
			sqlite3_bind_int(updateStmt, 5, sourceKind);
			sqlite3_bind_int(updateStmt, 6, skillBand);
			sqlite3_bind_int(updateStmt, 7, seq->contextKey);
			sqlite3_bind_int(updateStmt, 8, seq->stimulus);
			sqlite3_bind_int(updateStmt, 9, seq->response);
			sqlite3_bind_int(updateStmt, 10, seq->follow1);
			sqlite3_bind_int(updateStmt, 11, seq->follow2);
			rc = sqlite3_step(updateStmt);
			if (rc != SQLITE_DONE)
			{
				G_TrackedDBError("update LocalBotLearnedSequence", db, rc);
				ok = qfalse;
			}
		}
	}
	else if (sequenceCount > 0)
	{
		G_TrackedDBError("prepare LocalBotLearnedSequence", db, sqlite3_errcode(db));
		ok = qfalse;
	}
	sqlite3_finalize(insertStmt);
	sqlite3_finalize(updateStmt);
	free(rows);
	free(events);
	free(sequences);
	return ok;
}

// Map load: fold LocalBotLearnedSequence into the in-memory learned weight cache. Wins are
// judged relative to each source's usual win rate and bot rows are down-weighted (or left
// out with bot_learninghumansonly 1).
static void G_BotLearnLoadCache(sqlite3 *db)
{
	sqlite3_stmt *stmt = NULL;
	const char *baselineSql =
		"SELECT source_kind, SUM(samples), SUM(wins) FROM LocalBotLearnedSequence GROUP BY source_kind";
	const char *sql = bot_learninghumansonly.integer ?
		"SELECT source_kind, ctx_key, stimulus, response, follow1, SUM(samples), SUM(wins), SUM(net_damage) FROM LocalBotLearnedSequence "
		"WHERE source_kind = 0 GROUP BY source_kind, ctx_key, stimulus, response, follow1" :
		"SELECT source_kind, ctx_key, stimulus, response, follow1, SUM(samples), SUM(wins), SUM(net_damage) FROM LocalBotLearnedSequence "
		"GROUP BY source_kind, ctx_key, stimulus, response, follow1";
	float baseline[2] = { 0.5f, 0.5f };
	int rows = 0;

	G_BotLearnCacheClear();
	if (!bot_learning.integer || !db)
		return;
	if (sqlite3_prepare_v2(db, baselineSql, -1, &stmt, NULL) == SQLITE_OK)
	{
		while (sqlite3_step(stmt) == SQLITE_ROW)
		{
			const int source = sqlite3_column_int(stmt, 0);
			const double samples = sqlite3_column_double(stmt, 1);
			if (source >= BOTLEARN_SOURCE_HUMAN && source <= BOTLEARN_SOURCE_BOT && samples > 0.0)
				baseline[source] = (float)(sqlite3_column_double(stmt, 2) / samples);
		}
	}
	sqlite3_finalize(stmt);
	stmt = NULL;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
	{
		sqlite3_finalize(stmt);
		return;
	}
	while (sqlite3_step(stmt) == SQLITE_ROW)
	{
		const int source = sqlite3_column_int(stmt, 0) == BOTLEARN_SOURCE_BOT ?
			BOTLEARN_SOURCE_BOT : BOTLEARN_SOURCE_HUMAN;
		const float weight = BotLearn_SourceWeight(source);
		const float samples = (float)sqlite3_column_double(stmt, 5);

		G_BotLearnCacheAdd(sqlite3_column_int(stmt, 1), sqlite3_column_int(stmt, 2),
			sqlite3_column_int(stmt, 3), sqlite3_column_int(stmt, 4), samples * weight,
			BotLearn_ExcessWins(samples, (float)sqlite3_column_double(stmt, 6), baseline[source]) * weight,
			(float)sqlite3_column_double(stmt, 7) * weight);
		rows++;
	}
	sqlite3_finalize(stmt);
	trap->Print("Bot learning: loaded %i learned sequence groups (%i cache entries)%s baseline win human %.2f bot %.2f\n",
		rows, G_BotLearnCacheCount(), bot_learninghumansonly.integer ? " [humans only]" : "",
		baseline[BOTLEARN_SOURCE_HUMAN], baseline[BOTLEARN_SOURCE_BOT]);
	if (bot_learning_debug.integer)
		G_BotLearnDebugPrint(60);
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
	qboolean persistOk = qtrue;

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

	if (!G_OpenTrackedLocalDB(&db, NULL, 0))
		return;
	G_EnsureLocalDuelTrackingSchema(db);
	s = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
	if (s != SQLITE_OK)
	{
		G_ErrorPrint("ERROR: SQL Begin Failed (LocalDuelTrack persist)", s);
		G_TrackedDBError("begin LocalDuelTrack persist", db, s);
		CALL_SQLITE(close(db));
		return;
	}

	sql = "INSERT INTO LocalDuelTrackSummary(source_context, start_time, end_time, duration, type, mapname, winner_key, winner_label, winner_kind, winner_side, loser_key, loser_label, loser_kind, loser_side, draw, winner_opening, loser_opening, capture_version, capture_revision, result) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_text(stmt, 1, "duel", -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 2, startTimestamp));
	CALL_SQLITE(bind_int(stmt, 3, endTimestamp));
	CALL_SQLITE(bind_int(stmt, 4, durationSeconds));
	CALL_SQLITE(bind_int(stmt, 5, duelType));
	CALL_SQLITE(bind_text(stmt, 6, level.rawmapname, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 7, summaryFirst->identityKey, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 8, summaryFirst->identityLabel, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 9, summaryFirst->identityKind));
	CALL_SQLITE(bind_int(stmt, 10, summaryFirst->side));
	CALL_SQLITE(bind_text(stmt, 11, summarySecond->identityKey, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 12, summarySecond->identityLabel, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 13, summarySecond->identityKind));
	CALL_SQLITE(bind_int(stmt, 14, summarySecond->side));
	CALL_SQLITE(bind_int(stmt, 15, draw ? 1 : 0));
	CALL_SQLITE(bind_text(stmt, 16, summaryFirst->openingTactic, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 17, summarySecond->openingTactic, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 18, TRACKED_CAPTURE_VERSION));
	CALL_SQLITE(bind_text(stmt, 19, GIT_HASH, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 20, draw ? "draw" : "duel_complete", -1, SQLITE_STATIC));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Insert Failed (LocalDuelTrackSummary)", s);
		G_TrackedDBError("insert LocalDuelTrackSummary", db, s);
		persistOk = qfalse;
	}
	CALL_SQLITE(finalize(stmt));
	if (persistOk)
	{
		summaryId = sqlite3_last_insert_rowid(db);
		persistOk = G_InsertTrackedParticipant(db, summaryId, winnerRuntime, draw ? -1 : 1);
	}
	if (persistOk)
		persistOk = G_InsertTrackedParticipant(db, summaryId, loserRuntime, draw ? -1 : 0);
	if (persistOk)
		persistOk = G_InsertTrackedEvents(db, summaryId, winnerRuntime);
	if (persistOk)
		persistOk = G_InsertTrackedEvents(db, summaryId, loserRuntime);
	if (persistOk)
		persistOk = G_UpdateTrackedAggregate(db, winnerRuntime, draw ? qfalse : qtrue, draw);
	if (persistOk)
		persistOk = G_UpdateTrackedAggregate(db, loserRuntime, qfalse, draw);
	if (persistOk)
		persistOk = G_BotLearnRecordPerspective(db, winnerRuntime, loserRuntime, draw ? qfalse : qtrue);
	if (persistOk)
		persistOk = G_BotLearnRecordPerspective(db, loserRuntime, winnerRuntime, qfalse);

	s = sqlite3_exec(db, persistOk ? "COMMIT" : "ROLLBACK", NULL, NULL, NULL);
	if (s != SQLITE_OK)
		G_ErrorPrint(persistOk ?
			"ERROR: SQL Commit Failed (LocalDuelTrack persist)" :
			"ERROR: SQL Rollback Failed (LocalDuelTrack persist)", s);

	CALL_SQLITE(close(db));
}

static void G_ClassifyTrackedAttackOutcomes(tracked_duel_event_t *events, int eventCount,
	const duel_capture_storage_t *capture)
{
	int i;

	if (!events)
		return;

	for (i = 0; i < eventCount; i++)
	{
		tracked_duel_event_t *attack = &events[i];
		int j;
		int endIndex = eventCount;
		qboolean incomplete = qfalse;
		const int endTime = attack->relTime + TRACKED_ATTACK_CHAIN_WINDOW_MS;
		duel_capture_outcome_t outcome = { 0, 0, 0 };

		if ((attack->eventType != DUEL_TRACK_EVENT_ATTACK_START &&
			 attack->eventType != DUEL_TRACK_EVENT_INPUT_START &&
			 attack->eventType != DUEL_TRACK_EVENT_ATTACK_CHAIN &&
			 attack->eventType != DUEL_TRACK_EVENT_SWING_START))
		{
			continue;
		}
		if (!G_DuelCaptureCanRankOutcomes(capture))
		{
			Q_strncpyz(attack->quality, G_DuelCaptureRankedOutcomeQuality(capture, &outcome), sizeof(attack->quality));
			continue;
		}

		for (j = i + 1; j < eventCount; j++)
		{
			const tracked_duel_event_t *event = &events[j];
			const qboolean nextAttack = (attack->eventType == DUEL_TRACK_EVENT_SWING_START ||
				attack->eventType == DUEL_TRACK_EVENT_ATTACK_CHAIN) ?
				(event->eventType == DUEL_TRACK_EVENT_SWING_START) :
				(event->eventType == DUEL_TRACK_EVENT_ATTACK_START || event->eventType == DUEL_TRACK_EVENT_INPUT_START);
			if (nextAttack)
			{
				endIndex = j;
				break;
			}
		}
		for (j = i + 1; j < endIndex; j++)
		{
			const tracked_duel_event_t *event = &events[j];
			const int kind = event->eventType == DUEL_TRACK_EVENT_DAMAGE_DEALT ? DUEL_CAPTURE_OUTCOME_DEALT :
				(event->eventType == DUEL_TRACK_EVENT_DAMAGE ? DUEL_CAPTURE_OUTCOME_TAKEN : DUEL_CAPTURE_OUTCOME_OTHER);
			if (G_DuelCaptureHasIndexGap(events[j - 1].eventIndex, event->eventIndex))
				incomplete = qtrue;
			if (event->relTime > endTime)
				break;
			if (!G_DuelCaptureSameOpponent(attack->opponentKey, attack->opponentClientNum,
				event->opponentKey, event->opponentClientNum))
				continue;
			G_DuelCaptureAccumulateOutcome(&outcome, attack->eventIndex,
				endIndex < eventCount ? events[endIndex].eventIndex : capture->total + 1,
				attack->relTime, endTime, event->eventIndex, event->relTime, kind,
				event->amount, !Q_stricmp(event->note, "kill"));
		}
		if (j == endIndex && ((endIndex < eventCount &&
			G_DuelCaptureHasIndexGap(events[endIndex - 1].eventIndex, events[endIndex].eventIndex)) ||
			(endIndex == eventCount && events[eventCount - 1].eventIndex != capture->total)))
			incomplete = qtrue;
		Q_strncpyz(attack->quality, incomplete ? "unknown" : G_DuelCaptureRankedOutcomeQuality(capture, &outcome), sizeof(attack->quality));
	}
}

void G_StartTrackedDuel(gentity_t *first, gentity_t *second, int duelType)
{
	if (!G_IsTrackedDuelCollectionEnabled() || !first || !second || !first->client || !second->client)
		return;
	if (first == second || first->s.number < 0 || first->s.number >= MAX_CLIENTS ||
		second->s.number < 0 || second->s.number >= MAX_CLIENTS)
		return;
	if (!G_IsTrackedDuelEligible(first, second))
		return;

	G_InitTrackedDuelRuntimeForClient(first, second, duelType);
	G_InitTrackedDuelRuntimeForClient(second, first, duelType);
}

// Knockdowns caused by the opponent (push/pull/kick/grip set ps.otherKiller) vs. our own
// (slips, wall hits, failed rolls).
static qboolean G_IsTrackedKnockdownByOpponent(gentity_t *ent, gentity_t *opponent)
{
	if (!ent || !ent->client || !opponent)
		return qfalse;
	return (ent->client->ps.otherKiller == opponent->s.number &&
		ent->client->ps.otherKillerTime > level.time - 2000) ? qtrue : qfalse;
}

// An opponent swing that ended in close range without hurting us is a dodge (or block) for us.
static void G_RecordTrackedDuelDodge(tracked_duel_runtime_t *attackerRuntime, gentity_t *attacker, gentity_t *defender)
{
	tracked_duel_runtime_t *defenderRuntime;
	int state;
	int range;

	if (!attackerRuntime || !attacker || !defender || !defender->client ||
		defender->s.number < 0 || defender->s.number >= MAX_CLIENTS)
		return;
	defenderRuntime = &g_trackedDuels[defender->s.number];
	if (!defenderRuntime->active || defenderRuntime->opponentClientNum != attacker->s.number)
		return;
	range = G_GetTrackedRangeBucket(defender, attacker);
	if (range != 1 || defenderRuntime->lastDamageTakenTime >= attackerRuntime->swing.startTime)
		return;
	state = G_InferTrackedForceState(defender, attacker);
	G_AddTrackedDuelEvent(defenderRuntime, DUEL_TRACK_EVENT_DODGE, level.time - defenderRuntime->duelStartTime,
		attackerRuntime->swing.move, DUEL_TRACK_POWER_UNKNOWN, state, range,
		(defender->client->ps.groundEntityNum == ENTITYNUM_NONE) ? "air" :
		(defender->client->ps.saberBlocked != BLOCKED_NONE ? "blocked" : "evaded"), defender, attacker);
}

void G_UpdateTrackedDuelFrame(gentity_t *ent)
{
	tracked_duel_runtime_t *runtime;
	gentity_t *opponent;
	int curForce, curHealthArmor, forceDelta;
	int curRangeBucket, airborne, knockedDown, state, opponentHealthArmor, attackButtons;
	const int attackMask = BUTTON_ATTACK | BUTTON_ALT_ATTACK;
	duel_track_power_t power;
	qboolean linkedSwing;

	if (!ent || !ent->client)
		return;

	if (ent->r.svFlags & SVF_BOT)
		G_ProcessBotTutorialQueue(ent);
	if (!G_IsTrackedDuelCollectionEnabled())
		return;

	runtime = &g_trackedDuels[ent->s.number];
	if (!runtime->active || !G_DuelCaptureCanObserveInputs(ent->health, runtime->hasDeathSnapshot))
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
	opponentHealthArmor = G_GetTrackedCombatHealthArmor(opponent);
	attackButtons = ent->client->pers.cmd.buttons & attackMask;
	linkedSwing = G_DuelCaptureSwingLinked(&runtime->swing,
		G_IsTrackedSaberContinuityAllowed(ent) && BG_SaberInAttack(ent->client->ps.saberMove),
		ent->client->ps.saberMove) ? qtrue : qfalse;
	if (runtime->swing.active &&
		(!G_IsTrackedSaberContinuityAllowed(ent) || !BG_SaberInAttack(ent->client->ps.saberMove) ||
		runtime->swing.move != ent->client->ps.saberMove))
	{
		G_DuelCaptureSampleYaw(&runtime->swing, ent->client->ps.viewangles[YAW]);
		G_AddTrackedDuelEvent(runtime, DUEL_TRACK_EVENT_SWING_END, level.time - runtime->duelStartTime,
			runtime->swing.move, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, "completed", ent, opponent);
		G_RecordTrackedDuelDodge(runtime, ent, opponent);
	}
	if (G_DuelCaptureSwingStartedWithContinuity(&runtime->swing,
		G_IsTrackedSaberContinuityAllowed(ent) && BG_SaberInAttack(ent->client->ps.saberMove),
		G_IsTrackedSaberContinuityAllowed(ent) && PM_SaberInTransition(ent->client->ps.saberMove),
		ent->client->ps.saberMove, level.time, ent->client->ps.viewangles[YAW]))
	{
		G_TouchTrackedDuelSequence(runtime);
		runtime->lastAttackTime = level.time;
		runtime->lastAttackSwingSide = G_GetTrackedSwingSideValue(ent->client->ps.saberMove);
		runtime->swing.preStrafe = (runtime->lastRightMove > 0) - (runtime->lastRightMove < 0);
		G_AddTrackedDuelEvent(runtime, DUEL_TRACK_EVENT_SWING_START, level.time - runtime->duelStartTime,
			ent->client->ps.saberMove, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket,
			"accepted", ent, opponent);
		if (linkedSwing)
			G_AddTrackedDuelEvent(runtime, DUEL_TRACK_EVENT_ATTACK_CHAIN, level.time - runtime->duelStartTime,
				ent->client->ps.saberMove, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, "accepted_link", ent, opponent);
	}
	if (ent->client->ps.saberInFlight &&
		!runtime->lastSaberInFlight &&
		G_IsTrackedSaberThrowRelease(ent))
	{
		runtime->lastThrowTime = level.time;
		runtime->lastThrowYawOffset = G_GetTrackedThrowYawOffset(ent, opponent);
		G_TouchTrackedDuelSequence(runtime);
		G_AddTrackedDuelEvent(runtime, DUEL_TRACK_EVENT_THROW_START, level.time - runtime->duelStartTime,
			curForce, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, "release", ent, opponent);
	}
	if ((state == DUEL_TRACK_STATE_PANIC || state == DUEL_TRACK_STATE_DISADVANTAGE) &&
		curForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD)
	{
		runtime->pendingResetRecovery = 1;
	}

	if (attackButtons && !(runtime->lastButtons & attackMask))
	{
		G_TouchTrackedDuelSequence(runtime);
		runtime->lastAttackTime = level.time;
		runtime->lastAttackSwingSide = G_GetTrackedSwingSideValue(ent->client->ps.saberMove);
		runtime->lastAttackStrafeDir = G_GetTrackedStrafeDirValue(&ent->client->pers.cmd);
		G_AddTrackedDuelEvent(runtime, DUEL_TRACK_EVENT_INPUT_START, level.time - runtime->duelStartTime,
			attackButtons, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket,
			(attackButtons & BUTTON_ALT_ATTACK) ? "alt" : "attack", ent, opponent);
		if (runtime->lastForceSpendTime > 0 &&
			level.time - runtime->lastForceSpendTime <= TRACKED_FORCE_TO_SABER_WINDOW_MS)
		{
			G_AddTrackedDuelEvent(runtime, DUEL_TRACK_EVENT_FORCE_TO_SABER, level.time - runtime->duelStartTime,
				ent->client->ps.saberMove, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket,
				"force_to_saber", ent, opponent);
		}
	}
	forceDelta = curForce - runtime->lastForce;
	if (forceDelta < 0)
	{
		int spent = -forceDelta;
		qboolean powerConfirmed;
		power = G_InferTrackedPowerSpend(ent, runtime, &powerConfirmed);
		runtime->lastForceSpendTime = level.time;
		runtime->totalForceSpent += spent;
		runtime->forceSpentByPower[power] += spent;
		runtime->spentByState[state] += spent;
		G_TouchTrackedDuelSequence(runtime);
		if (spent >= 20 && (state == DUEL_TRACK_STATE_PANIC || state == DUEL_TRACK_STATE_DISADVANTAGE))
		{
			runtime->overcommitEvents++;
			G_AddTrackedDuelEvent(runtime, DUEL_TRACK_EVENT_OVERCOMMIT, level.time - runtime->duelStartTime, spent, power, state, curRangeBucket, "overcommit", ent, opponent);
		}
		if (state == DUEL_TRACK_STATE_PANIC && curForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD)
			runtime->lowForceWindows++;
		if ((power == DUEL_TRACK_POWER_ABSORB || power == DUEL_TRACK_POWER_PROTECT) &&
			(ent->health <= 45 || state == DUEL_TRACK_STATE_PANIC || state == DUEL_TRACK_STATE_DISADVANTAGE))
			runtime->lateDefenseSpends++;
		G_SetTrackedOpeningIfEmpty(runtime, power, NULL);
		// Force lost to an enemy drain or a saber throw falls back to the selected power;
		// mark it so it is not mistaken for an action the player chose.
		G_AddTrackedDuelEvent(runtime, DUEL_TRACK_EVENT_FORCE, level.time - runtime->duelStartTime, spent, power, state, curRangeBucket,
			powerConfirmed ? NULL : TRACKED_FORCE_NOTE_SELECTED_FALLBACK, ent, opponent);
	}
	else if (forceDelta > 0)
	{
		runtime->totalForceRegen += forceDelta;
		runtime->pendingRegenAmount += forceDelta;
		if (level.time - runtime->lastRegenSampleTime >= TRACKED_REGEN_SAMPLE_INTERVAL_MS)
		{
			G_AddTrackedDuelEvent(runtime, DUEL_TRACK_EVENT_REGEN, level.time - runtime->duelStartTime, runtime->pendingRegenAmount, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, NULL, ent, opponent);
			runtime->pendingRegenAmount = 0;
			runtime->lastRegenSampleTime = level.time;
		}
		if (runtime->pendingResetRecovery &&
			runtime->lastForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD &&
			curForce > TRACKED_DUEL_LOW_FORCE_THRESHOLD)
		{
			runtime->resetSuccessEvents++;
			runtime->pendingResetRecovery = 0;
			G_AddTrackedDuelEvent(runtime, DUEL_TRACK_EVENT_RESET_SUCCESS, level.time - runtime->duelStartTime, forceDelta, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, "reset", ent, opponent);
		}
	}

	if (ent->client->ps.fd.forceGripCripple && !runtime->lastGripCripple)
		runtime->gripCrippleEvents++;

	if (curRangeBucket != runtime->lastRangeBucket)
		G_AddTrackedDuelEvent(runtime, DUEL_TRACK_EVENT_RANGE, level.time - runtime->duelStartTime, curRangeBucket, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, NULL, ent, opponent);

	if (airborne != runtime->lastAirborne)
	{
		const char *airNote = "landed";
		if (airborne)
			airNote = (ent->client->pers.cmd.upmove > 0 && ent->client->ps.velocity[2] > 0.0f && !knockedDown) ?
				TRACKED_AIR_NOTE_JUMP : "airborne";
		G_AddTrackedDuelEvent(runtime, DUEL_TRACK_EVENT_AIR, level.time - runtime->duelStartTime, airborne, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, airNote, ent, opponent);
	}

	if (knockedDown && !runtime->lastKnockdown)
	{
		const qboolean byOpponent = G_IsTrackedKnockdownByOpponent(ent, opponent);
		runtime->knockdownEvents++;
		G_AddTrackedDuelEvent(runtime, DUEL_TRACK_EVENT_KNOCKDOWN, level.time - runtime->duelStartTime, 1, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket,
			byOpponent ? "knockdown_by_opponent" : "knockdown_self", ent, opponent);
		if (byOpponent && runtime->eventCount &&
			runtime->events[runtime->eventCount - 1].eventType == DUEL_TRACK_EVENT_KNOCKDOWN)
			G_GetDuelTrackingIdentity(opponent, runtime->events[runtime->eventCount - 1].damageAttackerKey,
				sizeof(runtime->events[runtime->eventCount - 1].damageAttackerKey), NULL, 0, NULL);
	}

	if (curForce < runtime->lowestForce)
		runtime->lowestForce = curForce;

	runtime->lastForce = curForce;
	runtime->lastHealthArmor = curHealthArmor;
	runtime->lastSelectedPower = ent->client->ps.fd.forcePowerSelected;
	runtime->lastSaberInFlight = ent->client->ps.saberInFlight ? 1 : 0;
	runtime->lastPowersActive = ent->client->ps.fd.forcePowersActive;
	runtime->lastRangeBucket = curRangeBucket;
	runtime->lastAirborne = airborne;
	runtime->lastKnockdown = knockedDown;
	runtime->lastGripCripple = ent->client->ps.fd.forceGripCripple ? 1 : 0;
	runtime->lastButtons = ent->client->pers.cmd.buttons;
	runtime->lastRightMove = ent->client->pers.cmd.rightmove;
	runtime->lastSaberMove = ent->client->ps.saberMove;
	runtime->lastOpponentHealthArmor = opponentHealthArmor;
}

static void G_RecordTrackedArcadeDamage(gentity_t *target, gentity_t *attacker, int amount, int mod)
{
	tracked_arcade_runtime_t *runtime;
	gentity_t *opponent;
	tracked_duel_event_t *event;
	unsigned long long firstEvent;
	int eventSlot;
	int state;
	int range;
	qboolean linkedSwing;
	qboolean endedSwing;
	const qboolean trackedAttacker = (attacker && attacker->client && attacker->s.number >= 0 &&
		attacker->s.number < MAX_CLIENTS) ? qtrue : qfalse;

	runtime = &g_trackedArcadeCombats[target->s.number];
	if (runtime->active && !runtime->hasLethalDamage)
	{
		opponent = trackedAttacker && attacker != target ? attacker : G_GetTrackedArcadePrimaryOpponent(target);
		G_DuelCaptureMarkLethal(target->health, &runtime->hasLethalDamage);
		runtime->totalDamageTaken += amount;
		runtime->lastDamageTakenTime = level.time;
		G_DuelCaptureSampleYaw(&runtime->swing, target->client->ps.viewangles[YAW]);
		firstEvent = runtime->capture.total;
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_DAMAGE, level.time - runtime->startTime,
			amount, DUEL_TRACK_POWER_UNKNOWN, G_InferTrackedForceState(target, opponent),
			G_GetTrackedRangeBucket(target, opponent),
			(mod == MOD_SABER && attacker && attacker->client && attacker->client->ps.saberInFlight) ? "saberthrow" : NULL,
			target, opponent);
		if (runtime->eventCount && runtime->events[runtime->eventCount - 1].eventIndex > firstEvent)
		{
			event = &runtime->events[runtime->eventCount - 1];
			event->damageSource = mod;
			if (trackedAttacker)
				G_GetDuelTrackingIdentity(attacker, event->damageAttackerKey,
					sizeof(event->damageAttackerKey), NULL, 0, NULL);
		}
		if (runtime->hasLethalDamage && runtime->swing.active)
		{
			G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_SWING_END, level.time - runtime->startTime,
				runtime->swing.move, DUEL_TRACK_POWER_UNKNOWN, G_InferTrackedForceState(target, opponent),
				G_GetTrackedRangeBucket(target, opponent), "session_end", target, opponent);
			runtime->swing.active = 0;
			runtime->swing.chainPending = 0;
		}
	}

	if (!attacker || !attacker->client || attacker->s.number < 0 || attacker->s.number >= MAX_CLIENTS)
		return;
	runtime = &g_trackedArcadeCombats[attacker->s.number];
	/* Use the selected target snapshot: selecting again after a lethal hit would skip the corpse. */
	if (!G_DuelCaptureCreditsOpponent(runtime->active, attacker->s.number, attacker->s.number,
		target->s.number, runtime->lastOpponentClientNum))
		return;
	runtime->totalDamageDealt += amount;
	G_TouchTrackedArcadeSequence(runtime);
	state = G_InferTrackedForceState(attacker, target);
	range = G_GetTrackedRangeBucket(attacker, target);
	firstEvent = runtime->capture.total;
	endedSwing = G_DuelCapturePrepareSwingTransition(&runtime->swing,
		G_IsTrackedSaberContinuityAllowed(attacker) && BG_SaberInAttack(attacker->client->ps.saberMove),
		attacker->client->ps.saberMove, attacker->client->ps.viewangles[YAW]) ? qtrue : qfalse;
	if (endedSwing)
	{
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_SWING_END, level.time - runtime->startTime,
			runtime->swing.move, DUEL_TRACK_POWER_UNKNOWN, state, range, "completed", attacker, target);
		runtime->swing.active = 0;
	}
	if (!G_IsTrackedSaberContinuityAllowed(attacker) || !BG_SaberInAttack(attacker->client->ps.saberMove))
		G_DuelCaptureSwingStartedWithContinuity(&runtime->swing, 0,
			G_IsTrackedSaberContinuityAllowed(attacker) && PM_SaberInTransition(attacker->client->ps.saberMove),
			attacker->client->ps.saberMove, level.time, attacker->client->ps.viewangles[YAW]);
	linkedSwing = G_DuelCaptureSwingLinked(&runtime->swing,
		G_IsTrackedSaberContinuityAllowed(attacker) && BG_SaberInAttack(attacker->client->ps.saberMove),
		attacker->client->ps.saberMove) ? qtrue : qfalse;
	if (mod == MOD_SABER && !runtime->hasLethalDamage &&
		G_IsTrackedSaberContinuityAllowed(attacker) && BG_SaberInAttack(attacker->client->ps.saberMove))
	{
		if (G_DuelCaptureSwingStartedWithContinuity(&runtime->swing, 1, 0, attacker->client->ps.saberMove,
			level.time, attacker->client->ps.viewangles[YAW]))
		{
			runtime->lastAttackTime = level.time;
			runtime->lastAttackSwingSide = G_GetTrackedSwingSideValue(attacker->client->ps.saberMove);
			runtime->swing.preStrafe = (runtime->lastRightMove > 0) - (runtime->lastRightMove < 0);
			G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_SWING_START, level.time - runtime->startTime,
				attacker->client->ps.saberMove, DUEL_TRACK_POWER_UNKNOWN, state, range, "accepted", attacker, target);
			if (linkedSwing)
				G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_ATTACK_CHAIN, level.time - runtime->startTime,
					attacker->client->ps.saberMove, DUEL_TRACK_POWER_UNKNOWN, state, range, "accepted_link", attacker, target);
		}
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_SWING_DAMAGE, level.time - runtime->startTime,
			amount, DUEL_TRACK_POWER_UNKNOWN, state, range, "confirmed", attacker, target);
	}
	G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_DAMAGE_DEALT, level.time - runtime->startTime,
		amount, DUEL_TRACK_POWER_UNKNOWN, state, range, target->health <= 0 ? "kill" : "hit", attacker, target);
	if (runtime->lastDamageTakenTime > 0 && level.time - runtime->lastDamageTakenTime <= TRACKED_COUNTER_WINDOW_MS)
	{
		runtime->counterSuccessEvents++;
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_COUNTER_SUCCESS, level.time - runtime->startTime,
			amount, DUEL_TRACK_POWER_UNKNOWN, state, range, "counter", attacker, target);
	}
	else if (runtime->swing.startTime > 0 && level.time - runtime->swing.startTime <= TRACKED_PUNISH_WINDOW_MS)
	{
		runtime->punishSuccessEvents++;
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_PUNISH_SUCCESS, level.time - runtime->startTime,
			amount, DUEL_TRACK_POWER_UNKNOWN, state, range, "punish", attacker, target);
	}
	if (target->client->ps.saberInFlight)
	{
		runtime->saberReturnPunishes++;
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_SABER_RETURN_PUNISH, level.time - runtime->startTime,
			amount, DUEL_TRACK_POWER_UNKNOWN, state, range, "return", attacker, target);
	}
	if (BG_InKnockDown(target->client->ps.legsAnim))
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_KNOCKDOWN_FOLLOWUP, level.time - runtime->startTime,
			amount, DUEL_TRACK_POWER_UNKNOWN, state, range, "followup", attacker, target);
	for (eventSlot = 0; eventSlot < runtime->eventCount; eventSlot++)
	{
		event = &runtime->events[eventSlot];
		if (event->eventIndex <= firstEvent)
			continue;
		if (event->eventType == DUEL_TRACK_EVENT_SWING_START || event->eventType == DUEL_TRACK_EVENT_ATTACK_CHAIN ||
			event->eventType == DUEL_TRACK_EVENT_SWING_END)
			continue;
		event->damageSource = mod;
		Q_strncpyz(event->damageAttackerKey, runtime->identityKey, sizeof(event->damageAttackerKey));
	}
}

void G_TrackedDuelRecordDamage(gentity_t *target, gentity_t *attacker, int amount, int mod)
{
	tracked_duel_runtime_t *victim;
	tracked_duel_runtime_t *source;
	tracked_duel_event_t *event;
	gentity_t *opponent;
	unsigned long long firstEvent;
	int eventSlot;
	int state;
	int range;
	qboolean linkedSwing;
	qboolean lethal;
	qboolean endedSwing;

	if (!G_IsTrackedDuelCollectionEnabled() || !target || !target->client ||
		target->s.number < 0 || target->s.number >= MAX_CLIENTS || amount <= 0)
		return;
	G_RecordTrackedArcadeDamage(target, attacker, amount, mod);
	victim = &g_trackedDuels[target->s.number];
	if (!victim->active || victim->hasDeathSnapshot || victim->hasLethalDamage ||
		victim->opponentClientNum < 0 || victim->opponentClientNum >= MAX_CLIENTS)
		return;
	lethal = G_DuelCaptureMarkLethal(target->health, &victim->hasLethalDamage) ? qtrue : qfalse;
	opponent = &g_entities[victim->opponentClientNum];
	victim->totalDamageTaken += amount;
	victim->lastDamageTakenTime = level.time;
	G_DuelCaptureSampleYaw(&victim->swing, target->client->ps.viewangles[YAW]);
	firstEvent = victim->capture.total;
	G_AddTrackedDuelEvent(victim, DUEL_TRACK_EVENT_DAMAGE, level.time - victim->duelStartTime,
		amount, DUEL_TRACK_POWER_UNKNOWN, G_InferTrackedForceState(target, opponent),
		G_GetTrackedRangeBucket(target, opponent), NULL, target, opponent);
	if (victim->eventCount && victim->events[victim->eventCount - 1].eventIndex > firstEvent)
	{
		event = &victim->events[victim->eventCount - 1];
		event->damageSource = mod;
		if (attacker && attacker->client && attacker->s.number >= 0 && attacker->s.number < MAX_CLIENTS)
			G_GetDuelTrackingIdentity(attacker, event->damageAttackerKey,
				sizeof(event->damageAttackerKey), NULL, 0, NULL);
	}
	if (mod == MOD_SABER && attacker && attacker->client && attacker->client->ps.saberInFlight)
	{
		victim->saberThrowPunishes++;
		G_SetTrackedOpeningIfEmpty(victim, DUEL_TRACK_POWER_UNKNOWN, "saberthrow");
		if (victim->eventCount && victim->events[victim->eventCount - 1].eventIndex > firstEvent)
			Q_strncpyz(victim->events[victim->eventCount - 1].note, "saberthrow",
				sizeof(victim->events[victim->eventCount - 1].note));
	}

	if (attacker != opponent || !attacker->client)
		return;
	source = &g_trackedDuels[attacker->s.number];
	if (!source->active || source->opponentClientNum != target->s.number)
		return;
	source->totalDamageDealt += amount;
	if (lethal)
		source->totalKills++;
	G_TouchTrackedDuelSequence(source);
	state = G_InferTrackedForceState(attacker, target);
	range = G_GetTrackedRangeBucket(attacker, target);
	firstEvent = source->capture.total;
	endedSwing = G_DuelCapturePrepareSwingTransition(&source->swing,
		G_IsTrackedSaberContinuityAllowed(attacker) && BG_SaberInAttack(attacker->client->ps.saberMove),
		attacker->client->ps.saberMove, attacker->client->ps.viewangles[YAW]) ? qtrue : qfalse;
	if (endedSwing)
	{
		G_AddTrackedDuelEvent(source, DUEL_TRACK_EVENT_SWING_END, level.time - source->duelStartTime,
			source->swing.move, DUEL_TRACK_POWER_UNKNOWN, state, range, "completed", attacker, target);
		source->swing.active = 0;
	}
	if (!G_IsTrackedSaberContinuityAllowed(attacker) || !BG_SaberInAttack(attacker->client->ps.saberMove))
		G_DuelCaptureSwingStartedWithContinuity(&source->swing, 0,
			G_IsTrackedSaberContinuityAllowed(attacker) && PM_SaberInTransition(attacker->client->ps.saberMove),
			attacker->client->ps.saberMove, level.time, attacker->client->ps.viewangles[YAW]);
	linkedSwing = G_DuelCaptureSwingLinked(&source->swing,
		G_IsTrackedSaberContinuityAllowed(attacker) && BG_SaberInAttack(attacker->client->ps.saberMove),
		attacker->client->ps.saberMove) ? qtrue : qfalse;
	/* A saber hit is not a damaging swing when the blade is in flight. */
	if (mod == MOD_SABER && !source->hasDeathSnapshot &&
		G_IsTrackedSaberContinuityAllowed(attacker) && BG_SaberInAttack(attacker->client->ps.saberMove))
	{
		if (G_DuelCaptureSwingStartedWithContinuity(&source->swing, 1, 0, attacker->client->ps.saberMove,
			level.time, attacker->client->ps.viewangles[YAW]))
		{
			source->lastAttackTime = level.time;
			source->lastAttackSwingSide = G_GetTrackedSwingSideValue(attacker->client->ps.saberMove);
			source->swing.preStrafe = (source->lastRightMove > 0) - (source->lastRightMove < 0);
			G_AddTrackedDuelEvent(source, DUEL_TRACK_EVENT_SWING_START, level.time - source->duelStartTime,
				attacker->client->ps.saberMove, DUEL_TRACK_POWER_UNKNOWN, state, range, "accepted", attacker, target);
			if (linkedSwing)
				G_AddTrackedDuelEvent(source, DUEL_TRACK_EVENT_ATTACK_CHAIN, level.time - source->duelStartTime,
					attacker->client->ps.saberMove, DUEL_TRACK_POWER_UNKNOWN, state, range, "accepted_link", attacker, target);
		}
		G_AddTrackedDuelEvent(source, DUEL_TRACK_EVENT_SWING_DAMAGE, level.time - source->duelStartTime,
			amount, DUEL_TRACK_POWER_UNKNOWN, state, range, "confirmed", attacker, target);
	}
	G_AddTrackedDuelEvent(source, DUEL_TRACK_EVENT_DAMAGE_DEALT, level.time - source->duelStartTime,
		amount, DUEL_TRACK_POWER_UNKNOWN, state, range, lethal ? "kill" : "hit", attacker, target);
	source->punishConfirmEvents++;
	if (source->lastDamageTakenTime > 0 &&
		level.time - source->lastDamageTakenTime <= TRACKED_COUNTER_WINDOW_MS)
	{
		source->counterSuccessEvents++;
		G_AddTrackedDuelEvent(source, DUEL_TRACK_EVENT_COUNTER_SUCCESS, level.time - source->duelStartTime,
			amount, DUEL_TRACK_POWER_UNKNOWN, state, range, "counter", attacker, target);
	}
	else if (source->swing.startTime > 0 &&
		level.time - source->swing.startTime <= TRACKED_PUNISH_WINDOW_MS)
	{
		source->punishSuccessEvents++;
		G_AddTrackedDuelEvent(source, DUEL_TRACK_EVENT_PUNISH_SUCCESS, level.time - source->duelStartTime,
			amount, DUEL_TRACK_POWER_UNKNOWN, state, range, "punish", attacker, target);
	}
	if (target->client->ps.saberInFlight)
	{
		source->antiThrowSuccessEvents++;
		G_AddTrackedDuelEvent(source, DUEL_TRACK_EVENT_SABER_RETURN_PUNISH, level.time - source->duelStartTime,
			amount, DUEL_TRACK_POWER_UNKNOWN, state, range, "return", attacker, target);
	}
	if (BG_InKnockDown(target->client->ps.legsAnim))
		G_AddTrackedDuelEvent(source, DUEL_TRACK_EVENT_KNOCKDOWN_FOLLOWUP, level.time - source->duelStartTime,
			amount, DUEL_TRACK_POWER_UNKNOWN, state, range, "followup", attacker, target);
	for (eventSlot = 0; eventSlot < source->eventCount; eventSlot++)
	{
		event = &source->events[eventSlot];
		if (event->eventIndex <= firstEvent)
			continue;
		if (event->eventType == DUEL_TRACK_EVENT_SWING_START || event->eventType == DUEL_TRACK_EVENT_ATTACK_CHAIN ||
			event->eventType == DUEL_TRACK_EVENT_SWING_END)
			continue;
		event->damageSource = mod;
		Q_strncpyz(event->damageAttackerKey, source->identityKey, sizeof(event->damageAttackerKey));
	}
}

static void G_CloseTrackedDuelSwing(tracked_duel_runtime_t *slot, gentity_t *ent, gentity_t *opponent)
{
	if (!slot || !slot->swing.active || !ent || !ent->client)
		return;
	G_DuelCaptureSampleYaw(&slot->swing, ent->client->ps.viewangles[YAW]);
	G_AddTrackedDuelEvent(slot, DUEL_TRACK_EVENT_SWING_END, level.time - slot->duelStartTime,
		slot->swing.move, DUEL_TRACK_POWER_UNKNOWN, G_InferTrackedForceState(ent, opponent),
		G_GetTrackedRangeBucket(ent, opponent), "session_end", ent, opponent);
	slot->swing.active = 0;
	slot->swing.chainPending = 0;
}

//Prefer the death snapshot: the respawn path may have already restored live resources.
static void G_ApplyTrackedDuelEndingResources(tracked_duel_runtime_t *slot, gentity_t *ent)
{
	if (!slot || !ent || !ent->client)
		return;

	if (slot->hasDeathSnapshot)
	{
		slot->endingForce = slot->deathForce;
		slot->endingHP = slot->deathHP;
		slot->endingArmor = slot->deathArmor;
		return;
	}

	slot->endingForce = ent->client->ps.fd.forcePower;
	slot->endingHP = ent->health;
	slot->endingArmor = ent->client->ps.stats[STAT_ARMOR];
}

//Called from player_die before any respawn bookkeeping runs.
void G_TrackedDuelRecordDeath(gentity_t *self)
{
	tracked_duel_runtime_t *slot;

	if (!self || !self->client || self->s.number < 0 || self->s.number >= MAX_CLIENTS)
		return;

	slot = &g_trackedDuels[self->s.number];
	if (!slot->active || slot->hasDeathSnapshot)
		return;

	slot->deathForce = self->client->ps.fd.forcePower;
	slot->deathHP = self->health;
	slot->deathArmor = self->client->ps.stats[STAT_ARMOR];
	G_CloseTrackedDuelSwing(slot, self,
		(slot->opponentClientNum >= 0 && slot->opponentClientNum < MAX_CLIENTS) ?
		&g_entities[slot->opponentClientNum] : NULL);
	slot->hasDeathSnapshot = 1;
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
	if (winner == loser || winner->s.number < 0 || winner->s.number >= MAX_CLIENTS ||
		loser->s.number < 0 || loser->s.number >= MAX_CLIENTS)
		return;

	winnerSlot = &g_trackedDuels[winner->s.number];
	loserSlot = &g_trackedDuels[loser->s.number];
	if (!winnerSlot->active || !loserSlot->active)
		return;
	if (winnerSlot->opponentClientNum != loser->s.number || loserSlot->opponentClientNum != winner->s.number)
		return;

	G_CloseTrackedDuelSwing(winnerSlot, winner, loser);
	G_CloseTrackedDuelSwing(loserSlot, loser, winner);
	G_ApplyTrackedDuelEndingResources(winnerSlot, winner);
	G_ApplyTrackedDuelEndingResources(loserSlot, loser);
	winnerLowForceFinish = (winnerSlot->endingForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD || winnerSlot->lowestForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD) ? qtrue : qfalse;
	loserLowForceFinish = (loserSlot->endingForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD || loserSlot->lowestForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD) ? qtrue : qfalse;
	loserSlot->didDieLowForce = draw ? 0 : (loserLowForceFinish ? 1 : 0);
	G_SetTrackedPrimaryIssue(winnerSlot, winnerLowForceFinish, draw ? qfalse : qtrue);
	G_SetTrackedPrimaryIssue(loserSlot, loserLowForceFinish, qfalse);
	G_ClassifyTrackedAttackOutcomes(winnerSlot->events, winnerSlot->eventCount, &winnerSlot->capture);
	G_ClassifyTrackedAttackOutcomes(loserSlot->events, loserSlot->eventCount, &loserSlot->capture);
	G_DuelCaptureSuppressSequenceRanking(&winnerSlot->capture,
		winnerSlot->lastGoodSequenceLabel, winnerSlot->lastBadSequenceLabel);
	G_DuelCaptureSuppressSequenceRanking(&loserSlot->capture,
		loserSlot->lastGoodSequenceLabel, loserSlot->lastBadSequenceLabel);

	G_DuelCaptureMoveRuntime(&winnerRuntime, winnerSlot, sizeof(winnerRuntime));
	G_DuelCaptureMoveRuntime(&loserRuntime, loserSlot, sizeof(loserRuntime));

	G_PersistTrackedDuel(&winnerRuntime, &loserRuntime, duelType, draw);
	if (!draw)
		G_MaybeQueueBotTutorial(&loserRuntime, winner, loser);
	G_RecordTrackedSessionOutcome(winner, &winnerRuntime, draw ? qfalse : qtrue);
	G_RecordTrackedSessionOutcome(loser, &loserRuntime, qfalse);
	free(winnerRuntime.events);
	free(loserRuntime.events);
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

static qboolean G_InsertTrackedArcadeEvents(sqlite3 *db, sqlite3_int64 summaryId, tracked_arcade_runtime_t *runtime)
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
		return qtrue;

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
	sql = "INSERT INTO LocalDuelTrackEvent(summary_id, participant_key, participant_label, participant_kind, opponent_key, opponent_label, opponent_kind, rel_time, event_index, sequence_id, event_type, power, amount, state, range_bucket, buttons, saber_move, enemy_saber_move, yaw_delta, self_hp, self_armor, self_force, enemy_hp, enemy_armor, enemy_force, sequence_label, quality, note, swing_side, pre_swing_strafe, movement_intent, radial_speed, yaw_sweep, attack_elapsed_ms, throw_yaw_offset, forwardmove, rightmove, upmove, saber_stance, grounded, damage_source, damage_attacker_key, controller_owns_inputs, controller_family, controller_enabled, controller_fanbias, controller_candidate, controller_mistakebias, controller_skill) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
	s = sqlite3_prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL);
	if (s != SQLITE_OK || !stmt)
	{
		G_ErrorPrint("ERROR: SQL Prepare Failed (LocalDuelTrackEvent)", s);
		G_TrackedDBError("prepare LocalDuelTrackEvent", db, s);
		if (stmt)
			CALL_SQLITE(finalize(stmt));
		return qfalse;
	}
	if (captureGeometry && hasAnyGeometry)
	{
		sql = "INSERT INTO LocalDuelTrackGeometry(summary_id, participant_key, opponent_key, rel_time, event_index, self_x, self_y, self_z, enemy_x, enemy_y, enemy_z, self_vx, self_vy, self_vz, enemy_vx, enemy_vy, enemy_vz, self_yaw, enemy_yaw) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
		s = sqlite3_prepare_v2(db, sql, strlen(sql) + 1, &geomStmt, NULL);
		if (s != SQLITE_OK || !geomStmt)
		{
			G_ErrorPrint("ERROR: SQL Prepare Failed (LocalDuelTrackGeometry)", s);
			G_TrackedDBError("prepare LocalDuelTrackGeometry", db, s);
			if (stmt)
			{
				sqlite3_finalize(stmt);
				stmt = NULL;
			}
			if (geomStmt)
			{
				sqlite3_finalize(geomStmt);
				geomStmt = NULL;
			}
			return qfalse;
		}
	}

	for (i = 0; i < runtime->eventCount; i++)
	{
		tracked_duel_event_t *event = &runtime->events[i];
		CALL_SQLITE(bind_int64(stmt, 1, summaryId));
		CALL_SQLITE(bind_text(stmt, 2, runtime->identityKey, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_text(stmt, 3, runtime->identityLabel, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_int(stmt, 4, runtime->identityKind));
		CALL_SQLITE(bind_text(stmt, 5, event->opponentKey, -1, SQLITE_STATIC));
		CALL_SQLITE(bind_text(stmt, 6, event->opponentLabel, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_int(stmt, 7, event->opponentKind));
		CALL_SQLITE(bind_int(stmt, 8, event->relTime));
		CALL_SQLITE(bind_int64(stmt, 9, (sqlite3_int64)event->eventIndex));
		CALL_SQLITE(bind_int(stmt, 10, event->sequenceId));
		CALL_SQLITE(bind_text(stmt, 11, G_GetTrackedEventTypeName(event->eventType), -1, SQLITE_STATIC));
		CALL_SQLITE(bind_int(stmt, 12, event->power));
		CALL_SQLITE(bind_int(stmt, 13, event->amount));
		CALL_SQLITE(bind_int(stmt, 14, event->state));
		CALL_SQLITE(bind_int(stmt, 15, event->rangeBucket));
		CALL_SQLITE(bind_int(stmt, 16, event->buttons));
		CALL_SQLITE(bind_int(stmt, 17, event->saberMove));
		CALL_SQLITE(bind_int(stmt, 18, event->enemySaberMove));
		CALL_SQLITE(bind_int(stmt, 19, event->yawDelta));
		CALL_SQLITE(bind_int(stmt, 20, event->selfHealth));
		CALL_SQLITE(bind_int(stmt, 21, event->selfArmor));
		CALL_SQLITE(bind_int(stmt, 22, event->selfForce));
		CALL_SQLITE(bind_int(stmt, 23, event->enemyHealth));
		CALL_SQLITE(bind_int(stmt, 24, event->enemyArmor));
		CALL_SQLITE(bind_int(stmt, 25, event->enemyForce));
		CALL_SQLITE(bind_text(stmt, 26, event->sequenceLabel, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_text(stmt, 27, event->quality, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_text(stmt, 28, event->note, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_text(stmt, 29, event->swingSide, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_text(stmt, 30, event->preSwingStrafe, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_text(stmt, 31, event->movementIntent, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_int(stmt, 32, event->radialSpeed));
		CALL_SQLITE(bind_int(stmt, 33, event->yawSweep));
		CALL_SQLITE(bind_int(stmt, 34, event->attackElapsedMs));
		CALL_SQLITE(bind_int(stmt, 35, event->throwYawOffset));
		CALL_SQLITE(bind_int(stmt, 36, event->forwardmove));
		CALL_SQLITE(bind_int(stmt, 37, event->rightmove));
		CALL_SQLITE(bind_int(stmt, 38, event->upmove));
		CALL_SQLITE(bind_int(stmt, 39, event->saberStance));
		CALL_SQLITE(bind_int(stmt, 40, event->grounded));
		CALL_SQLITE(bind_int(stmt, 41, event->damageSource));
		CALL_SQLITE(bind_int(stmt, 43, event->controllerOwnsInputs));
		CALL_SQLITE(bind_int(stmt, 44, event->controllerFamily));
		CALL_SQLITE(bind_int(stmt, 45, event->controllerEnabled));
		CALL_SQLITE(bind_int(stmt, 46, event->controllerFanBias));
		CALL_SQLITE(bind_int(stmt, 47, event->controllerCandidate));
		CALL_SQLITE(bind_int(stmt, 48, event->controllerMistakeBias));
		CALL_SQLITE(bind_double(stmt, 49, event->controllerSkill));
		CALL_SQLITE(bind_text(stmt, 42, event->damageAttackerKey, -1, SQLITE_TRANSIENT));
		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE)
		{
			if (s != SQLITE_BUSY && s != SQLITE_LOCKED)
			{
				G_ErrorPrint("ERROR: SQL Insert Failed (LocalDuelTrackEvent)", s);
				G_TrackedDBError("insert LocalDuelTrackEvent", db, s);
			}
			insertFailed = qtrue;
			break;
		}
		CALL_SQLITE(reset(stmt));
		CALL_SQLITE(clear_bindings(stmt));

		if (captureGeometry && hasAnyGeometry && event->hasGeometry)
		{
			CALL_SQLITE(bind_int64(geomStmt, 1, summaryId));
			CALL_SQLITE(bind_text(geomStmt, 2, runtime->identityKey, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_text(geomStmt, 3, event->opponentKey, -1, SQLITE_STATIC));
			CALL_SQLITE(bind_int(geomStmt, 4, event->relTime));
			CALL_SQLITE(bind_int64(geomStmt, 5, (sqlite3_int64)event->eventIndex));
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
				if (s != SQLITE_BUSY && s != SQLITE_LOCKED)
				{
					G_ErrorPrint("ERROR: SQL Insert Failed (LocalDuelTrackGeometry)", s);
					G_TrackedDBError("insert LocalDuelTrackGeometry", db, s);
				}
				insertFailed = qtrue;
				break;
			}
			CALL_SQLITE(reset(geomStmt));
			CALL_SQLITE(clear_bindings(geomStmt));
		}
	}

	CALL_SQLITE(finalize(stmt));
	if (captureGeometry && hasAnyGeometry)
		CALL_SQLITE(finalize(geomStmt));
	return insertFailed ? qfalse : qtrue;
}

//Arcade runs share the duel tracking table family. The summary row carries source_context
//'arcade' plus the run result/level, and the participant row carries the per-run counters that
//used to live in LocalArcadeTrackSession.
static qboolean G_InsertTrackedArcadeParticipant(sqlite3 *db, sqlite3_int64 summaryId, tracked_arcade_runtime_t *runtime, int won)
{
	sqlite3_stmt *stmt = NULL;
	char *sql;
	int s;

	sql = "INSERT INTO LocalDuelTrackParticipant(summary_id, participant_key, participant_label, participant_kind, elo_key, opponent_key, won, side, opponent_side, matchup, total_force_spent, total_force_regen, ending_force, ending_hp, ending_armor, low_force_windows, saber_throw_punishes, knockdown_events, total_kills, total_damage_taken, total_damage_dealt, counter_successes, punish_successes, reset_successes) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_int64(stmt, 1, summaryId));
	CALL_SQLITE(bind_text(stmt, 2, runtime->identityKey, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 3, runtime->identityLabel, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 4, runtime->identityKind));
	CALL_SQLITE(bind_text(stmt, 5, runtime->identityKey, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 6, "", -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 7, won ? 1 : 0));
	CALL_SQLITE(bind_int(stmt, 8, 0));
	CALL_SQLITE(bind_int(stmt, 9, 0));
	CALL_SQLITE(bind_int(stmt, 10, 0));
	CALL_SQLITE(bind_int(stmt, 11, runtime->totalForceSpent));
	CALL_SQLITE(bind_int(stmt, 12, runtime->totalForceRegen));
	CALL_SQLITE(bind_int(stmt, 13, runtime->endingForce));
	CALL_SQLITE(bind_int(stmt, 14, runtime->endingHP));
	CALL_SQLITE(bind_int(stmt, 15, runtime->endingArmor));
	CALL_SQLITE(bind_int(stmt, 16, runtime->lowForceWindows));
	CALL_SQLITE(bind_int(stmt, 17, runtime->saberReturnPunishes));
	CALL_SQLITE(bind_int(stmt, 18, runtime->knockdownEvents));
	CALL_SQLITE(bind_int(stmt, 19, runtime->killCount));
	CALL_SQLITE(bind_int(stmt, 20, runtime->totalDamageTaken));
	CALL_SQLITE(bind_int(stmt, 21, runtime->totalDamageDealt));
	CALL_SQLITE(bind_int(stmt, 22, runtime->counterSuccessEvents));
	CALL_SQLITE(bind_int(stmt, 23, runtime->punishSuccessEvents));
	CALL_SQLITE(bind_int(stmt, 24, runtime->resetSuccessEvents));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
	{
		if (s != SQLITE_BUSY && s != SQLITE_LOCKED)
		{
			G_ErrorPrint("ERROR: SQL Insert Failed (LocalDuelTrackParticipant/arcade)", s);
			G_TrackedDBError("insert LocalDuelTrackParticipant (arcade)", db, s);
		}
	}
	CALL_SQLITE(finalize(stmt));
	return (s == SQLITE_DONE) ? G_InsertTrackedCaptureDiagnostics(db, summaryId,
		runtime->identityKey, &runtime->capture, runtime->eventCount) : qfalse;
}

static void G_PersistTrackedArcadeCombat(tracked_arcade_runtime_t *runtime, const char *result, int arcadeLevel)
{
	sqlite3 *db;
	sqlite3_stmt *stmt = NULL;
	char *sql;
	qboolean persistOk = qfalse;
	int s;
	int won;
	const char *resultName = (result && result[0]) ? result : "finished";
	sqlite3_int64 summaryId;
	time_t rawtime;
	int endTimestamp;
	int startTimestamp;
	const int duration = (runtime && runtime->startTime > 0) ? (level.time - runtime->startTime) : 0;
	const int durationSeconds = duration / 1000;

	if (!runtime)
		return;

	won = (!Q_stricmp(resultName, "arcade_complete") || !Q_stricmp(resultName, "level_clear")) ? 1 : 0;

	time(&rawtime);
	endTimestamp = (int)rawtime;
	startTimestamp = endTimestamp - durationSeconds;

	if (!G_OpenTrackedLocalDB(&db, NULL, 0))
		return;
	G_EnsureLocalDuelTrackingSchema(db);
	CALL_SQLITE(exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL));

	sql = "INSERT INTO LocalDuelTrackSummary(source_context, start_time, end_time, duration, type, mapname, winner_key, winner_label, winner_kind, winner_side, loser_key, loser_label, loser_kind, loser_side, draw, winner_opening, loser_opening, result, arcade_level, capture_version, capture_revision) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	CALL_SQLITE(bind_text(stmt, 1, "arcade", -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 2, startTimestamp));
	CALL_SQLITE(bind_int(stmt, 3, endTimestamp));
	CALL_SQLITE(bind_int(stmt, 4, durationSeconds));
	CALL_SQLITE(bind_int(stmt, 5, TRACKED_ARCADE_DUEL_TYPE));
	CALL_SQLITE(bind_text(stmt, 6, level.rawmapname, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 7, won ? runtime->identityKey : "", -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 8, won ? runtime->identityLabel : "", -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 9, won ? runtime->identityKind : 0));
	CALL_SQLITE(bind_int(stmt, 10, 0));
	CALL_SQLITE(bind_text(stmt, 11, won ? "" : runtime->identityKey, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 12, won ? "" : runtime->identityLabel, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 13, won ? 0 : runtime->identityKind));
	CALL_SQLITE(bind_int(stmt, 14, 0));
	CALL_SQLITE(bind_int(stmt, 15, 0));
	CALL_SQLITE(bind_text(stmt, 16, "", -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 17, "", -1, SQLITE_STATIC));
	CALL_SQLITE(bind_text(stmt, 18, resultName, -1, SQLITE_TRANSIENT));
	CALL_SQLITE(bind_int(stmt, 20, TRACKED_CAPTURE_VERSION));
	CALL_SQLITE(bind_text(stmt, 21, GIT_HASH, -1, SQLITE_STATIC));
	CALL_SQLITE(bind_int(stmt, 19, arcadeLevel));
	s = sqlite3_step(stmt);
	if (s != SQLITE_DONE)
	{
		if (s != SQLITE_BUSY && s != SQLITE_LOCKED)
		{
			G_ErrorPrint("ERROR: SQL Insert Failed (LocalDuelTrackSummary/arcade)", s);
			G_TrackedDBError("insert LocalDuelTrackSummary (arcade)", db, s);
		}
	}
	CALL_SQLITE(finalize(stmt));
	summaryId = sqlite3_last_insert_rowid(db);

	persistOk = (s == SQLITE_DONE) ? G_InsertTrackedArcadeParticipant(db, summaryId, runtime, won) : qfalse;
	if (persistOk)
		persistOk = G_InsertTrackedArcadeEvents(db, summaryId, runtime);
	if (persistOk)
	{
		CALL_SQLITE(exec(db, "COMMIT", NULL, NULL, NULL));
	}
	else
	{
		CALL_SQLITE(exec(db, "ROLLBACK", NULL, NULL, NULL));
	}
	CALL_SQLITE(close(db));
}

void G_StartTrackedArcadeCombat(gentity_t *ent)
{
	tracked_arcade_runtime_t *runtime;
	gentity_t *opponent;

	if (!G_IsTrackedDuelCollectionEnabled() || !ent || !ent->client || (ent->r.svFlags & SVF_BOT))
		return;
	if (ent->s.number < 0 || ent->s.number >= MAX_CLIENTS)
		return;

	runtime = &g_trackedArcadeCombats[ent->s.number];
	G_DuelCaptureClearRuntime(runtime, sizeof(*runtime), runtime->events);
	runtime->active = qtrue;
	runtime->startTime = level.time;
	runtime->lastForce = ent->client->ps.fd.forcePower;
	runtime->lowestForce = ent->client->ps.fd.forcePower;
	runtime->lastHealthArmor = G_GetTrackedCombatHealthArmor(ent);
	runtime->lastSelectedPower = ent->client->ps.fd.forcePowerSelected;
	runtime->lastPowersActive = ent->client->ps.fd.forcePowersActive;
	runtime->lastAirborne = (ent->client->ps.groundEntityNum == ENTITYNUM_NONE) ? 1 : 0;
	runtime->lastKnockdown = BG_InKnockDown(ent->client->ps.legsAnim) ? 1 : 0;
	runtime->lastGripCripple = ent->client->ps.fd.forceGripCripple ? 1 : 0;
	runtime->lastButtons = ent->client->pers.cmd.buttons;
	runtime->lastSaberMove = ent->client->ps.saberMove;
	runtime->lastSaberInFlight = ent->client->ps.saberInFlight ? 1 : 0;
	runtime->lastRightMove = ent->client->pers.cmd.rightmove;
	runtime->swing.lastYaw = ent->client->ps.viewangles[YAW];
	G_GetDuelTrackingIdentity(ent, runtime->identityKey, sizeof(runtime->identityKey), runtime->identityLabel, sizeof(runtime->identityLabel), &runtime->identityKind);

	opponent = G_GetTrackedArcadePrimaryOpponent(ent);
	if (opponent && opponent->client)
	{
		runtime->lastOpponentClientNum = opponent->s.number;
		runtime->lastRangeBucket = G_GetTrackedRangeBucket(ent, opponent);
		runtime->lastOpponentHealthArmor = G_GetTrackedCombatHealthArmor(opponent);
	}
	else
	{
		runtime->lastOpponentClientNum = ENTITYNUM_NONE;
		runtime->lastRangeBucket = 0;
		runtime->lastOpponentHealthArmor = 0;
	}
}

void G_UpdateTrackedArcadeCombatFrame(gentity_t *ent)
{
	tracked_arcade_runtime_t *runtime;
	gentity_t *opponent;
	int curForce;
	int curHealthArmor;
	int forceDelta;
	int curRangeBucket = 0;
	int airborne;
	int knockedDown;
	int state = DUEL_TRACK_STATE_NEUTRAL;
	int opponentHealthArmor = 0;
	int attackButtons;
	const int attackMask = BUTTON_ATTACK | BUTTON_ALT_ATTACK;
	qboolean linkedSwing;
	qboolean observeInputs;

	if (!ent || !ent->client)
		return;
	if (!G_IsTrackedDuelCollectionEnabled())
		return;

	runtime = &g_trackedArcadeCombats[ent->s.number];
	if (!runtime->active || runtime->hasLethalDamage)
		return;

	opponent = G_GetTrackedArcadePrimaryOpponent(ent);
	if (opponent && opponent->client)
	{
		curRangeBucket = G_GetTrackedRangeBucket(ent, opponent);
		state = G_InferTrackedForceState(ent, opponent);
		opponentHealthArmor = G_GetTrackedCombatHealthArmor(opponent);
	}

	curForce = ent->client->ps.fd.forcePower;
	curHealthArmor = G_GetTrackedCombatHealthArmor(ent);
	airborne = (ent->client->ps.groundEntityNum == ENTITYNUM_NONE) ? 1 : 0;
	knockedDown = BG_InKnockDown(ent->client->ps.legsAnim) ? 1 : 0;
	if (opponent &&
		ent->client->ps.saberInFlight &&
		!runtime->lastSaberInFlight &&
		G_IsTrackedSaberThrowRelease(ent))
	{
		runtime->lastThrowTime = level.time;
		runtime->lastThrowYawOffset = G_GetTrackedThrowYawOffset(ent, opponent);
		G_TouchTrackedArcadeSequence(runtime);
	}

	if ((state == DUEL_TRACK_STATE_PANIC || state == DUEL_TRACK_STATE_DISADVANTAGE) &&
		curForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD)
	{
		runtime->lastForceSpendTime = level.time;
	}

	attackButtons = ent->client->pers.cmd.buttons & attackMask;
	observeInputs = G_DuelCaptureCanObserveInputs(ent->health, 0) ? qtrue : qfalse;
	linkedSwing = G_DuelCaptureSwingLinked(&runtime->swing,
		observeInputs && G_IsTrackedSaberContinuityAllowed(ent) && BG_SaberInAttack(ent->client->ps.saberMove),
		ent->client->ps.saberMove) ? qtrue : qfalse;
	if (runtime->swing.active && opponent &&
		(!G_IsTrackedSaberContinuityAllowed(ent) || !BG_SaberInAttack(ent->client->ps.saberMove) ||
		runtime->swing.move != ent->client->ps.saberMove))
	{
		G_DuelCaptureSampleYaw(&runtime->swing, ent->client->ps.viewangles[YAW]);
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_SWING_END, level.time - runtime->startTime,
			runtime->swing.move, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, "completed", ent, opponent);
	}
	if (G_DuelCaptureSwingStartedWithContinuity(&runtime->swing,
		observeInputs && G_IsTrackedSaberContinuityAllowed(ent) && BG_SaberInAttack(ent->client->ps.saberMove),
		observeInputs && G_IsTrackedSaberContinuityAllowed(ent) && PM_SaberInTransition(ent->client->ps.saberMove),
		ent->client->ps.saberMove, level.time, ent->client->ps.viewangles[YAW]) && opponent)
	{
		G_TouchTrackedArcadeSequence(runtime);
		runtime->lastAttackTime = level.time;
		runtime->lastAttackSwingSide = G_GetTrackedSwingSideValue(ent->client->ps.saberMove);
		runtime->swing.preStrafe = (runtime->lastRightMove > 0) - (runtime->lastRightMove < 0);
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_SWING_START, level.time - runtime->startTime,
			ent->client->ps.saberMove, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket,
			"accepted", ent, opponent);
		if (linkedSwing)
			G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_ATTACK_CHAIN, level.time - runtime->startTime,
				ent->client->ps.saberMove, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, "accepted_link", ent, opponent);
	}
	if (observeInputs && opponent && attackButtons && !(runtime->lastButtons & attackMask))
	{
		G_TouchTrackedArcadeSequence(runtime);
		runtime->lastAttackTime = level.time;
		runtime->lastAttackSwingSide = G_GetTrackedSwingSideValue(ent->client->ps.saberMove);
		runtime->lastAttackStrafeDir = G_GetTrackedStrafeDirValue(&ent->client->pers.cmd);
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_INPUT_START, level.time - runtime->startTime, attackButtons, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, (attackButtons & BUTTON_ALT_ATTACK) ? "alt" : "attack", ent, opponent);
		if (runtime->lastForceSpendTime > 0 &&
			level.time - runtime->lastForceSpendTime <= TRACKED_FORCE_TO_SABER_WINDOW_MS)
		{
			G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_FORCE_TO_SABER, level.time - runtime->startTime, ent->client->ps.saberMove, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, "force_to_saber", ent, opponent);
		}
	}
	forceDelta = curForce - runtime->lastForce;
	if (forceDelta < 0)
	{
		const int spent = -forceDelta;
		duel_track_power_t power = G_InferTrackedPowerSpendArcade(ent, runtime);

		runtime->totalForceSpent += spent;
		if (state == DUEL_TRACK_STATE_PANIC && curForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD)
			runtime->lowForceWindows++;
		runtime->lastForceSpendTime = level.time;
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_FORCE, level.time - runtime->startTime, spent, power, state, curRangeBucket, NULL, ent, opponent);
	}
	else if (forceDelta > 0)
	{
		runtime->totalForceRegen += forceDelta;
		runtime->pendingRegenAmount += forceDelta;
		if (level.time - runtime->lastRegenSampleTime >= TRACKED_REGEN_SAMPLE_INTERVAL_MS)
		{
			G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_REGEN, level.time - runtime->startTime, runtime->pendingRegenAmount, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, NULL, ent, opponent);
			runtime->pendingRegenAmount = 0;
			runtime->lastRegenSampleTime = level.time;
		}
		if (runtime->lastForce <= TRACKED_DUEL_LOW_FORCE_THRESHOLD &&
			curForce > TRACKED_DUEL_LOW_FORCE_THRESHOLD)
		{
			runtime->resetSuccessEvents++;
			G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_RESET_SUCCESS, level.time - runtime->startTime, forceDelta, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, "reset", ent, opponent);
		}
	}

	if ((opponent && curRangeBucket != runtime->lastRangeBucket) ||
		(!opponent && runtime->lastOpponentClientNum != ENTITYNUM_NONE))
	{
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_RANGE, level.time - runtime->startTime, curRangeBucket, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, opponent ? NULL : "no_target", ent, opponent);
	}

	if (airborne != runtime->lastAirborne)
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_AIR, level.time - runtime->startTime, airborne, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, airborne ? "airborne" : "landed", ent, opponent);

	if (knockedDown && !runtime->lastKnockdown)
	{
		runtime->knockdownEvents++;
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_KNOCKDOWN, level.time - runtime->startTime, 1, DUEL_TRACK_POWER_UNKNOWN, state, curRangeBucket, "knockdown", ent, opponent);
	}

	if (curForce < runtime->lowestForce)
		runtime->lowestForce = curForce;

	runtime->lastForce = curForce;
	runtime->lastHealthArmor = curHealthArmor;
	runtime->lastSelectedPower = ent->client->ps.fd.forcePowerSelected;
	runtime->lastPowersActive = ent->client->ps.fd.forcePowersActive;
	runtime->lastSaberInFlight = ent->client->ps.saberInFlight ? 1 : 0;
	runtime->lastRangeBucket = curRangeBucket;
	runtime->lastAirborne = airborne;
	runtime->lastKnockdown = knockedDown;
	runtime->lastGripCripple = ent->client->ps.fd.forceGripCripple ? 1 : 0;
	runtime->lastButtons = ent->client->pers.cmd.buttons;
	runtime->lastSaberMove = ent->client->ps.saberMove;
	runtime->lastRightMove = ent->client->pers.cmd.rightmove;
	if (opponent && opponent->client)
	{
		runtime->lastOpponentClientNum = opponent->s.number;
		runtime->lastOpponentHealthArmor = opponentHealthArmor;
	}
	else
	{
		runtime->lastOpponentClientNum = ENTITYNUM_NONE;
		runtime->lastOpponentHealthArmor = 0;
	}
}

void G_FinishTrackedArcadeCombat(gentity_t *ent, const char *result)
{
	tracked_arcade_runtime_t runtimeCopy;
	tracked_arcade_runtime_t *runtime;
	gentity_t *opponent;

	if (!ent || !ent->client)
		return;
	if (ent->s.number < 0 || ent->s.number >= MAX_CLIENTS)
		return;

	runtime = &g_trackedArcadeCombats[ent->s.number];
	if (!runtime->active)
		return;

	opponent = runtime->lastOpponentClientNum >= 0 && runtime->lastOpponentClientNum < MAX_CLIENTS ?
		&g_entities[runtime->lastOpponentClientNum] : NULL;
	if (runtime->swing.active)
	{
		G_DuelCaptureSampleYaw(&runtime->swing, ent->client->ps.viewangles[YAW]);
		G_AddTrackedArcadeEvent(runtime, DUEL_TRACK_EVENT_SWING_END, level.time - runtime->startTime,
			runtime->swing.move, DUEL_TRACK_POWER_UNKNOWN, G_InferTrackedForceState(ent, opponent),
			G_GetTrackedRangeBucket(ent, opponent), "session_end", ent, opponent);
		runtime->swing.active = 0;
		runtime->swing.chainPending = 0;
	}
	runtime->endingForce = ent->client->ps.fd.forcePower;
	runtime->endingHP = ent->health;
	runtime->endingArmor = ent->client->ps.stats[STAT_ARMOR];
	if (ent->s.number >= 0 && ent->s.number < MAX_CLIENTS)
		runtime->killCount = level.arcadeRoundKills[ent->s.number];

	G_ClassifyTrackedAttackOutcomes(runtime->events, runtime->eventCount, &runtime->capture);
	G_DuelCaptureMoveRuntime(&runtimeCopy, runtime, sizeof(runtimeCopy));
	G_PersistTrackedArcadeCombat(&runtimeCopy, result, level.arcadeLevel);
	free(runtimeCopy.events);
}

void G_ClearTrackedArcadeCombat(int clientNum)
{
	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
		return;

	G_DuelCaptureClearRuntime(&g_trackedArcadeCombats[clientNum], sizeof(g_trackedArcadeCombats[clientNum]),
		g_trackedArcadeCombats[clientNum].events);
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
	{
		if (s != SQLITE_BUSY && s != SQLITE_LOCKED)
		{
			G_ErrorPrint("ERROR: SQL Create Failed (InitGameAccountStuff arcade)", s);
		}
	}
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
		{
			if (s != SQLITE_BUSY && s != SQLITE_LOCKED)
			{
				G_ErrorPrint("ERROR: SQL Alter Failed (LocalArcade mapname)", s);
			}
		}
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

int G_GetDuelBotSkillLevel(gentity_t *ent)
{
	char userinfo[MAX_INFO_STRING];
	int level;

	if (!ent || !ent->client || !(ent->r.svFlags & SVF_BOT))
		return 0;

	trap->GetUserinfo(ent->s.number, userinfo, sizeof(userinfo));
	level = atoi(Info_ValueForKey(userinfo, "skill"));
	if (level < BOT_DUEL_LEVEL_MIN || level > BOT_DUEL_LEVEL_MAX)
		return 0;

	return level;
}

static int G_GetDuelParticipantBotLevel(gentity_t *ent)
{
	//Bot identity must stay stable regardless of which AI/ranking cvars are toggled,
	//otherwise the same bot is logged under different (or empty) names between duels.
	return G_GetDuelBotSkillLevel(ent);
}

static qboolean G_GetDuelBotIdentityName(gentity_t *ent, int botLevel, char *name, int nameSize)
{
	char userinfo[MAX_INFO_STRING];
	const char *fallbackName = NULL;

	if (!ent || !ent->client || !name || nameSize < 1)
		return qfalse;

	//Rated bots share one rating per skill level ("botlvl5"), whatever bot file or netname
	//they use, so the ladder holds one entry per level.
	trap->GetUserinfo(ent->s.number, userinfo, sizeof(userinfo));
	if (ent->client->pers.netname_nocolor[0])
		fallbackName = ent->client->pers.netname_nocolor;
	else if (ent->client->pers.netname[0])
		fallbackName = ent->client->pers.netname;

	return G_FormatBotDuelIdentity(Info_ValueForKey(userinfo, "personality"), fallbackName, botLevel, name, nameSize) ? qtrue : qfalse;
}

static int G_ParseBotLevelName(const char *name)
{
	int level = 0;
	const char *tag = NULL;

	if (!name || !name[0])
		return 0;

	if (!strncmp(name, "botlvl", 6))
	{
		level = atoi(name + 6);
	}
	else
	{
		tag = strstr(name, "[bot L");
		if (tag)
			level = atoi(tag + 6);
	}

	if (level < BOT_DUEL_LEVEL_MIN || level > BOT_DUEL_LEVEL_MAX)
		return 0;

	return level;
}

//Seed rating for an identity that has no rated duel history yet. Humans (and anything we
//cannot resolve to a bot level) keep the historic 1000 starting point; bots are spread
//linearly across BOT_DUEL_SEED_ELO_MIN..BOT_DUEL_SEED_ELO_MAX for levels 1..10, so a fresh
//database starts with L1 at 800 and L10 at 1100 instead of every bot sharing 1000.
float G_GetSeedEloForDuelName(const char *username)
{
	const int level = G_ParseBotLevelName(username);

	if (level < BOT_DUEL_LEVEL_MIN || level > BOT_DUEL_LEVEL_MAX)
		return BOT_DUEL_SEED_ELO_DEFAULT;

	if (BOT_DUEL_LEVEL_MAX == BOT_DUEL_LEVEL_MIN)
		return BOT_DUEL_SEED_ELO_MIN;

	return BOT_DUEL_SEED_ELO_MIN +
		((float)(level - BOT_DUEL_LEVEL_MIN) *
		 (float)(BOT_DUEL_SEED_ELO_MAX - BOT_DUEL_SEED_ELO_MIN) /
		 (float)(BOT_DUEL_LEVEL_MAX - BOT_DUEL_LEVEL_MIN));
}

qboolean G_GetDuelParticipantName(gentity_t *ent, char *name, int nameSize) {
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

	level = G_GetDuelParticipantBotLevel(ent);
	if (!level) {
		return qfalse;
	}

	return G_GetDuelBotIdentityName(ent, level, name, nameSize);
}

//Resolves both ladder identities for a duel. Registered accounts and bots use their own key;
//an unregistered human facing a bot is rated under their stable "iphash:" key so the bot's
//results against guests count. Returns qtrue when both sides resolved (the duel is rated).
qboolean G_GetRatedDuelParticipantNames(gentity_t *ent, gentity_t *opponent, char *entName, int entNameSize, char *opponentName, int opponentNameSize)
{
	qboolean entHasKey;
	qboolean opponentHasKey;
	const int entIsBot = (ent && (ent->r.svFlags & SVF_BOT)) ? 1 : 0;
	const int opponentIsBot = (opponent && (opponent->r.svFlags & SVF_BOT)) ? 1 : 0;

	entHasKey = G_GetDuelParticipantName(ent, entName, entNameSize);
	opponentHasKey = G_GetDuelParticipantName(opponent, opponentName, opponentNameSize);

	if (G_ShouldUseGuestDuelKey(entHasKey, entIsBot, opponentIsBot, opponentHasKey))
	{
		G_GetTrackingIPKey(ent, entName, entNameSize);
		entHasKey = entName[0] ? qtrue : qfalse;
	}
	if (G_ShouldUseGuestDuelKey(opponentHasKey, opponentIsBot, entIsBot, entHasKey))
	{
		G_GetTrackingIPKey(opponent, opponentName, opponentNameSize);
		opponentHasKey = opponentName[0] ? qtrue : qfalse;
	}

	return (entHasKey && opponentHasKey && entName[0] && opponentName[0]) ? qtrue : qfalse;
}

//Earlier builds rated each bot separately per level ("Kyle [bot L5]", "mediumds [bot L5]"),
//which filled /top with one redundant entry per bot. Fold those rows into the per-level
//"botlvlN" identity so each level keeps a single rating. Idempotent: once folded nothing matches.
static void G_FoldBotLadderKeysToLevels(sqlite3 *db)
{
	sqlite3_stmt *stmt = NULL;
	char levelName[16];
	char levelTagged[32];
	int botLevel;
	int pass;
	int s;
	int folded = 0;

	if (!db)
		return;

	if (sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL) != SQLITE_OK)
	{
		G_ErrorPrint("ERROR: SQL Begin Failed (G_FoldBotLadderKeysToLevels)", sqlite3_errcode(db));
		return;
	}

	for (botLevel = BOT_DUEL_LEVEL_MIN; botLevel <= BOT_DUEL_LEVEL_MAX; botLevel++)
	{
		Com_sprintf(levelName, sizeof(levelName), "botlvl%i", botLevel);
		Com_sprintf(levelTagged, sizeof(levelTagged), "%% [bot L%i]", botLevel);

		for (pass = 0; pass < 2; pass++)
		{
			const char *sql = pass ?
				"UPDATE LocalDuel SET loser = ? WHERE loser LIKE ?" :
				"UPDATE LocalDuel SET winner = ? WHERE winner LIKE ?";

			s = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
			if (s != SQLITE_OK || !stmt)
			{
				G_ErrorPrint("ERROR: SQL Prepare Failed (G_FoldBotLadderKeysToLevels)", s);
				if (stmt)
					sqlite3_finalize(stmt);
				sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
				return;
			}
			sqlite3_bind_text(stmt, 1, levelName, -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(stmt, 2, levelTagged, -1, SQLITE_TRANSIENT);
			s = sqlite3_step(stmt);
			sqlite3_finalize(stmt);
			stmt = NULL;
			if (s != SQLITE_DONE)
			{
				G_ErrorPrint("ERROR: SQL Update Failed (G_FoldBotLadderKeysToLevels)", s);
				sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
				return;
			}
			folded += sqlite3_changes(db);
		}
	}

	if (sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK)
	{
		G_ErrorPrint("ERROR: SQL Commit Failed (G_FoldBotLadderKeysToLevels)", sqlite3_errcode(db));
		sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
		return;
	}

	if (folded > 0)
		trap->Print("Duel ELO: folded %i per-bot rating rows into per-level botlvl1-%i ratings.\n",
			folded, BOT_DUEL_LEVEL_MAX);
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

	//Only consider rated rows: unranked bot-vs-bot duels are stored with elo -999, and
	//picking one of those up would reset the participant back to the seed value.
	sql = "SELECT winner_elo AS elo, end_time FROM LocalDuel where type = ? AND winner = ? AND end_time < ? AND winner_elo > -998 "
		"UNION ALL SELECT loser_elo AS elo, end_time FROM LocalDuel where type = ? AND loser = ? AND end_time < ? AND loser_elo > -998 "
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
		elo = G_GetSeedEloForDuelName(username); //Elo not found, seed by bot level (humans get 1000)
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

static int G_GetRankedBotVsBotLevelDuelsToday(int botLevel, int opponentLevel, int end_time, sqlite3 *db)
{
	char *sql;
	sqlite3_stmt *stmt;
	int s;
	int count = 0;
	sqlite3_int64 dayStart;
	sqlite3_int64 dayEnd;
	sqlite3_int64 endTime64;
	char botLevelName[16];
	char botLevelTaggedName[32];
	char opponentLevelName[16];
	char opponentLevelTaggedName[32];

	if (!db ||
		botLevel < BOT_DUEL_LEVEL_MIN || botLevel > BOT_DUEL_LEVEL_MAX ||
		opponentLevel < BOT_DUEL_LEVEL_MIN || opponentLevel > BOT_DUEL_LEVEL_MAX)
	{
		return 0;
	}

	// end_time is a Unix epoch timestamp (UTC seconds from time()).
	endTime64 = (sqlite3_int64)end_time;
	dayStart = endTime64 - (endTime64 % 86400);
	dayEnd = dayStart + 86400;
	Com_sprintf(botLevelName, sizeof(botLevelName), "botlvl%i", botLevel);
	Com_sprintf(botLevelTaggedName, sizeof(botLevelTaggedName), "%% [bot L%i]", botLevel);
	Com_sprintf(opponentLevelName, sizeof(opponentLevelName), "botlvl%i", opponentLevel);
	Com_sprintf(opponentLevelTaggedName, sizeof(opponentLevelTaggedName), "%% [bot L%i]", opponentLevel);

	sql = "SELECT COUNT(*) FROM LocalDuel "
		"WHERE end_time >= ? AND end_time < ? "
		"AND winner_elo > -998 AND loser_elo > -998 "
		"AND ("
			"(((winner = ? OR winner LIKE ?) AND (loser = ? OR loser LIKE ?)))"
			" OR "
			"(((loser = ? OR loser LIKE ?) AND (winner = ? OR winner LIKE ?)))"
		")";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_int64 (stmt, 1, dayStart));
	CALL_SQLITE (bind_int64 (stmt, 2, dayEnd));
	CALL_SQLITE (bind_text (stmt, 3, botLevelName, -1, SQLITE_TRANSIENT));
	CALL_SQLITE (bind_text (stmt, 4, botLevelTaggedName, -1, SQLITE_TRANSIENT));
	CALL_SQLITE (bind_text (stmt, 5, opponentLevelName, -1, SQLITE_TRANSIENT));
	CALL_SQLITE (bind_text (stmt, 6, opponentLevelTaggedName, -1, SQLITE_TRANSIENT));
	CALL_SQLITE (bind_text (stmt, 7, botLevelName, -1, SQLITE_TRANSIENT));
	CALL_SQLITE (bind_text (stmt, 8, botLevelTaggedName, -1, SQLITE_TRANSIENT));
	CALL_SQLITE (bind_text (stmt, 9, opponentLevelName, -1, SQLITE_TRANSIENT));
	CALL_SQLITE (bind_text (stmt, 10, opponentLevelTaggedName, -1, SQLITE_TRANSIENT));

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

static qboolean G_ShouldRankBotVsBotDuel(int winnerLevel, int loserLevel, int end_time, sqlite3 *db)
{
	int winnerDailyCount;
	int loserDailyCount;

	if (!winnerLevel || !loserLevel)
	{
		return qtrue;
	}

	winnerDailyCount = G_GetRankedBotVsBotLevelDuelsToday(winnerLevel, loserLevel, end_time, db);
	loserDailyCount = (loserLevel == winnerLevel) ? winnerDailyCount :
		G_GetRankedBotVsBotLevelDuelsToday(loserLevel, winnerLevel, end_time, db);

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

	if (!G_OpenSQLiteFile(LOCAL_DB_PATH, &db, "account"))
		return;
	if (type == 21)
	{
		G_EnsureLocalArcadeSchema(db);
	}
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

//Re-seed every stored rating row for one bot level onto the level curve (see
//G_GetSeedEloForDuelName). Matches both stored naming forms: the bare "botlvlN" identity
//and the tagged "<name> [bot LN]" identity produced by G_GetDuelBotIdentityName.
static int G_ReseedBotLevelElo(sqlite3 *db, int botLevel, float seedElo)
{
	char *sql;
	sqlite3_stmt *stmt;
	char levelName[16];
	char levelTagged[32];
	int s;
	int rows = 0;
	int pass;

	if (!db || botLevel < BOT_DUEL_LEVEL_MIN || botLevel > BOT_DUEL_LEVEL_MAX)
	{
		return 0;
	}

	Com_sprintf(levelName, sizeof(levelName), "botlvl%i", botLevel);
	Com_sprintf(levelTagged, sizeof(levelTagged), "%% [bot L%i]", botLevel);

	for (pass = 0; pass < 2; pass++)
	{
		sql = pass ?
			"UPDATE LocalDuel SET loser_elo = ? WHERE loser_elo > -998 AND (loser = ? OR loser LIKE ?)" :
			"UPDATE LocalDuel SET winner_elo = ? WHERE winner_elo > -998 AND (winner = ? OR winner LIKE ?)";
		CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
		CALL_SQLITE (bind_double (stmt, 1, seedElo));
		CALL_SQLITE (bind_text (stmt, 2, levelName, -1, SQLITE_TRANSIENT));
		CALL_SQLITE (bind_text (stmt, 3, levelTagged, -1, SQLITE_TRANSIENT));
		s = sqlite3_step(stmt);
		if (s != SQLITE_DONE) {
			G_ErrorPrint("ERROR: SQL Update Failed (G_ReseedBotLevelElo)", s);
		}
		else {
			rows += sqlite3_changes(db);
		}
		CALL_SQLITE (finalize(stmt));
	}

	return rows;
}

void SV_BotEloReset_f(void) {
	char input[32];
	char botName[16];
	sqlite3 *db;
	int botLevel;
	int rows;
	float seedElo;

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

	seedElo = G_GetSeedEloForDuelName(botName);

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
	rows = G_ReseedBotLevelElo(db, botLevel, seedElo);
	CALL_SQLITE (close(db));

	trap->Print("bot_eloreset: reset level %i bots to %.0f ELO across %i rating rows.\n",
		botLevel, seedElo, rows);
}

//Count stored rating rows for one bot level. Used to leave bots that have already earned a
//rating alone - see SV_BotEloSeed_f.
static int G_CountBotLevelRatedRows(sqlite3 *db, int botLevel)
{
	char *sql;
	sqlite3_stmt *stmt;
	char levelName[16];
	char levelTagged[32];
	int s;
	int count = 0;

	if (!db || botLevel < BOT_DUEL_LEVEL_MIN || botLevel > BOT_DUEL_LEVEL_MAX)
	{
		return 0;
	}

	Com_sprintf(levelName, sizeof(levelName), "botlvl%i", botLevel);
	Com_sprintf(levelTagged, sizeof(levelTagged), "%% [bot L%i]", botLevel);

	sql = "SELECT (SELECT COUNT(*) FROM LocalDuel WHERE winner_elo > -998 AND (winner = ?1 OR winner LIKE ?2)) + "
		"(SELECT COUNT(*) FROM LocalDuel WHERE loser_elo > -998 AND (loser = ?1 OR loser LIKE ?2))";
	CALL_SQLITE (prepare_v2 (db, sql, strlen (sql) + 1, & stmt, NULL));
	CALL_SQLITE (bind_text (stmt, 1, levelName, -1, SQLITE_TRANSIENT));
	CALL_SQLITE (bind_text (stmt, 2, levelTagged, -1, SQLITE_TRANSIENT));

	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW) {
		count = sqlite3_column_int(stmt, 0);
	}
	else if (s != SQLITE_DONE) {
		G_ErrorPrint("ERROR: SQL Select Failed (G_CountBotLevelRatedRows)", s);
	}

	CALL_SQLITE (finalize(stmt));
	return count;
}

//Seed bot levels onto the 800-1100 curve. Intended for use right after a database wipe, so
//it deliberately leaves any level that already has rated history untouched - progression a
//bot has already earned is never discarded here. Use bot_eloreset <level> to force one.
void SV_BotEloSeed_f(void) {
	sqlite3 *db;
	int botLevel;
	int totalRows = 0;
	int skippedLevels = 0;

	CALL_SQLITE (open (LOCAL_DB_PATH, & db));
	for (botLevel = BOT_DUEL_LEVEL_MIN; botLevel <= BOT_DUEL_LEVEL_MAX; botLevel++)
	{
		const float seedElo = G_GetSeedEloForDuelName(va("botlvl%i", botLevel));
		const int existingRows = G_CountBotLevelRatedRows(db, botLevel);
		int rows;

		if (existingRows > 0)
		{
			skippedLevels++;
			trap->Print("bot_eloseed: level %i kept (%i existing rating rows).\n", botLevel, existingRows);
			continue;
		}

		rows = G_ReseedBotLevelElo(db, botLevel, seedElo);
		totalRows += rows;
		trap->Print("bot_eloseed: level %i -> %.0f ELO (%i rating rows).\n", botLevel, seedElo, rows);
	}
	CALL_SQLITE (close(db));

	trap->Print("bot_eloseed: seeded %i rating rows across levels %i-%i (%i level(s) kept their existing ELO).\n",
		totalRows, BOT_DUEL_LEVEL_MIN, BOT_DUEL_LEVEL_MAX, skippedLevels);
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
			G_EnsureLocalArcadeSchema(db);
		}

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

			trap->SendServerCommand(ent-g_entities, va("print \"Topscore results for arcade on %s:\n ^5#   Username           Score       Level     Kills\n\"", level.rawmapname));
			while (1) {
				s = sqlite3_step(stmt);
				if (s == SQLITE_ROW) {
					char *tmpMsg = NULL;
					char displayName[32];
					int score = 0, levelReached = 0, kills = 0;

					Q_strncpyz(username, (char*)sqlite3_column_text(stmt, 0), sizeof(username));
					G_FormatArcadeLeaderboardName(username, displayName, sizeof(displayName));
					score = sqlite3_column_int(stmt, 1);
					levelReached = sqlite3_column_int(stmt, 2);
					kills = sqlite3_column_int(stmt, 3);

					tmpMsg = va("^5%2i^3: ^3%-18s ^3%-11i ^3%-9i %i\n", start+row, displayName, score, levelReached, kills);
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
					if (s != SQLITE_BUSY && s != SQLITE_LOCKED)
					{
						G_ErrorPrint("ERROR: SQL Select Failed (Cmd_DuelTop10_f arcade)", s);
					}
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
		//Restrict the "latest rating" CTE to rated rows (elo > -998). Unranked bot-vs-bot
		//duels are stored with elo -999, and if one of those was a participant's most recent
		//row they would drop off the ladder entirely instead of keeping their real rating.
		sql = "WITH DuelRows AS (SELECT rowid AS duel_rowid, 1 AS duel_side, winner AS username, type, ROUND(winner_elo,0) AS elo, end_time FROM LocalDuel WHERE type = ? AND winner_elo > -998 "
				"UNION ALL SELECT rowid AS duel_rowid, 0 AS duel_side, loser AS username, type, ROUND(loser_elo,0) AS elo, end_time FROM LocalDuel WHERE type = ? AND loser_elo > -998), "
				"LatestTimes AS (SELECT username, MAX(end_time) AS max_end_time FROM DuelRows GROUP BY username), "
				"LatestRows AS (SELECT DuelRows.* FROM DuelRows INNER JOIN LatestTimes ON DuelRows.username = LatestTimes.username AND DuelRows.end_time = LatestTimes.max_end_time), "
				"LatestRowIds AS (SELECT username, MAX(duel_rowid) AS max_duel_rowid FROM LatestRows GROUP BY username), "
				"LatestRowsById AS (SELECT LatestRows.* FROM LatestRows INNER JOIN LatestRowIds ON LatestRows.username = LatestRowIds.username AND LatestRows.duel_rowid = LatestRowIds.max_duel_rowid), "
				"LatestSides AS (SELECT username, MAX(duel_side) AS max_duel_side FROM LatestRowsById GROUP BY username), "
				"LatestChosen AS (SELECT LatestRowsById.username, LatestRowsById.type, LatestRowsById.elo FROM LatestRowsById INNER JOIN LatestSides "
				"ON LatestRowsById.username = LatestSides.username AND LatestRowsById.duel_side = LatestSides.max_duel_side) "
				"SELECT D1.username, D1.elo, CASE WHEN (COALESCE(D2.win_count, 0) + COALESCE(D3.loss_count, 0)) > 0 "
				"THEN 100-ROUND(100*(COALESCE(D2.win_ts, 0) + COALESCE(D3.loss_ts, 0))/"
				"(COALESCE(D2.win_count, 0) + COALESCE(D3.loss_count, 0)), 0) ELSE 0 END AS TS, "
				"COALESCE(D2.win_count, 0)+COALESCE(D3.loss_count, 0) AS count "
				"FROM (SELECT username, type, elo FROM LatestChosen WHERE elo > -998 AND username NOT LIKE 'iphash:%' ORDER BY elo DESC) AS D1 "
				"LEFT JOIN (SELECT winner AS username2, COUNT(*) AS win_count, SUM(odds) AS win_ts FROM LocalDuel WHERE type = ? GROUP BY username2) AS D2 "
				"ON D1.username = D2.username2 "
				"LEFT JOIN (SELECT loser AS username3, COUNT(*) AS loss_count, SUM(1-odds) AS loss_ts FROM LocalDuel WHERE type = ? GROUP BY username3) AS D3 "
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
				TS = sqlite3_column_int(stmt, 2);
				count = sqlite3_column_int(stmt, 3);

				tmpMsg = va("^5%2i^3: ^3%-18s ^3%-12i ^3%-9i %i\n", start+row, username, rank, TS, count);
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

void G_AddDuel(char *winner, char *loser, int winnerLevel, int loserLevel, int start_time, int type, int winner_hp, int winner_shield) {
	sqlite3 * db;
	time_t	rawtime;
	char	string[256] = {0};
	const int duelLogType = (level.gametype == GT_ARCADE) ? 21 : type;
	const int duration = start_time ? (level.time - start_time) : 0;

	time( &rawtime );
	localtime( &rawtime );

	Com_sprintf(string, sizeof(string), "%s;%s;%i;%i;%i;%i;%i\n", winner, loser, duration, duelLogType, winner_hp, winner_shield, rawtime);

	if (level.duelLog)
		trap->FS_Write(string, strlen(string), level.duelLog ); //Always write to text file, this file is remade every mapchange and its contents are put to database.

	//Might want to make this log to file, and have that sent to db on map change.  But whatever.. duel finishes are not as frequent as race course finishes usually.

#if _ELORANKING	
	if (g_eloRanking.integer && duelLogType != 21) {
		if (!G_OpenSQLiteFile(LOCAL_DB_PATH, &db, "account"))
			return;
		{
			const qboolean shouldRankDuel = G_ShouldRankBotVsBotDuel(winnerLevel, loserLevel, rawtime, db);
			if (shouldRankDuel)
			{
				G_AddDuelElo(winner, loser, duelLogType, duration, winner_hp, winner_shield, 0, rawtime, db);
			}

			else
			{
				G_AddDuelToDBWithHandle(db, winner, loser, duelLogType, duration, winner_hp, winner_shield, rawtime);
			}
		}
		CALL_SQLITE (close(db));
	}
	else if (duelLogType == 21)
	{
		if (!G_OpenSQLiteFile(LOCAL_DB_PATH, &db, "account"))
			return;
		G_AddDuelToDBWithHandle(db, winner, loser, duelLogType, duration, winner_hp, winner_shield, rawtime);
		CALL_SQLITE (close(db));
	}
#endif

}

qboolean G_AddArcadeScore(const char *username, const char *mapname, int score, int levelReached, int kills, int end_time)
{
	sqlite3 *db;
sqlite3_stmt *stmt = NULL;
	char *sql;
int s = SQLITE_ERROR;
	int busyRetry = 0;
	const int maxBusyRetries = 2;

	if (!username || !username[0] || !mapname || !mapname[0])
	{
		return qfalse;
	}

	CALL_SQLITE(open(LOCAL_DB_PATH, &db));
	sqlite3_busy_timeout(db, 100);
	G_EnsureLocalArcadeSchema(db);
	sql = "INSERT INTO LocalArcade(username, mapname, score, level, kills, end_time) VALUES (?, ?, ?, ?, ?, ?)";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	for (busyRetry = 0; busyRetry <= maxBusyRetries; busyRetry++)
	{
		CALL_SQLITE(reset(stmt));
		CALL_SQLITE(clear_bindings(stmt));
		CALL_SQLITE(bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_text(stmt, 2, mapname, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_int(stmt, 3, score));
		CALL_SQLITE(bind_int(stmt, 4, levelReached));
		CALL_SQLITE(bind_int(stmt, 5, kills));
		CALL_SQLITE(bind_int(stmt, 6, end_time));
		s = sqlite3_step(stmt);
		if ((s == SQLITE_BUSY || s == SQLITE_LOCKED) && busyRetry < maxBusyRetries)
		{
			sqlite3_sleep(5);
			continue;
		}
		break;
	}
	if (s == SQLITE_BUSY || s == SQLITE_LOCKED)
	{
		/* transient lock contention handled by caller via return value */
	}
	else if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Insert Failed (G_AddArcadeScore)", s);
	}
	CALL_SQLITE(finalize(stmt));
	CALL_SQLITE(close(db));
	return (s == SQLITE_DONE) ? qtrue : qfalse;
}

qboolean G_GetArcadeTopScore(const char *mapname, int *scoreOut, char *usernameOut, int usernameOutSize, qboolean *queryFailedOut)
{
	sqlite3 *db;
	sqlite3_stmt *stmt = NULL;
	char *sql;
	int s = SQLITE_ERROR;
	int busyRetry = 0;
	const int maxBusyRetries = 2;
	qboolean found = qfalse;
	qboolean queryFailed = qfalse;

	if (scoreOut)
	{
		*scoreOut = 0;
	}
	if (usernameOut && usernameOutSize > 0)
	{
		usernameOut[0] = '\0';
	}
	if (queryFailedOut)
	{
		*queryFailedOut = qfalse;
	}
	if (!mapname || !mapname[0])
	{
		return qfalse;
	}

	CALL_SQLITE(open(LOCAL_DB_PATH, &db));
	sqlite3_busy_timeout(db, 100);
	G_EnsureLocalArcadeSchema(db);
	sql = "WITH has_map(map_exists) AS (SELECT EXISTS(SELECT 1 FROM LocalArcade WHERE mapname = ?)) "
		"SELECT username, score FROM LocalArcade, has_map "
		"WHERE (has_map.map_exists = 1 AND LocalArcade.mapname = ?) "
		"OR (has_map.map_exists = 0 AND (LocalArcade.mapname = '' OR LocalArcade.mapname IS NULL)) "
		"ORDER BY " LOCAL_ARCADE_SCORE_ORDER " LIMIT 1";
	CALL_SQLITE(prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL));
	for (busyRetry = 0; busyRetry <= maxBusyRetries; busyRetry++)
	{
		CALL_SQLITE(reset(stmt));
		CALL_SQLITE(clear_bindings(stmt));
		CALL_SQLITE(bind_text(stmt, 1, mapname, -1, SQLITE_TRANSIENT));
		CALL_SQLITE(bind_text(stmt, 2, mapname, -1, SQLITE_TRANSIENT));
		s = sqlite3_step(stmt);
		if ((s == SQLITE_BUSY || s == SQLITE_LOCKED) && busyRetry < maxBusyRetries)
		{
			sqlite3_sleep(5);
			continue;
		}
		break;
	}
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
	else if (s == SQLITE_BUSY || s == SQLITE_LOCKED)
	{
		queryFailed = qtrue;
	}
	else if (s != SQLITE_DONE)
	{
		queryFailed = qtrue;
		G_ErrorPrint("ERROR: SQL Select Failed (G_GetArcadeTopScore)", s);
	}
	CALL_SQLITE(finalize(stmt));
	CALL_SQLITE(close(db));
	if (queryFailedOut)
	{
		*queryFailedOut = queryFailed;
	}
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

	G_AddDuel(winner, loser, 0, 0, level.time-1000, atoi(type), 420, 420);

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

//GitHub rejects web uploads of 25MB or larger, so every export file is capped below that.
#define TRACKED_EXPORT_MAX_PART_BYTES	(24 * 1000 * 1000)
#define TRACKED_EXPORT_MAX_PARTS		999

static size_t G_TrackedCSVCellLength(const char *text)
{
	size_t len = 2;

	if (!text)
		return len;
	for (; *text; text++)
		len += (*text == '"') ? 2 : 1;
	return len;
}

//Part 1 keeps the historical name (e.g. events.csv); later parts are events_part2.csv, ...
static void G_BuildTrackedExportPartName(const char *baseName, const char *exportSuffix, int part,
	char *out, int outSize)
{
	if (part <= 1)
		Com_sprintf(out, outSize, "%s%s.csv", baseName, exportSuffix ? exportSuffix : "");
	else
		Com_sprintf(out, outSize, "%s%s_part%i.csv", baseName, exportSuffix ? exportSuffix : "", part);
}

static FILE *G_OpenTrackedExportPart(sqlite3_stmt *stmt, int colCount, const char *outputPath, size_t *bytesWritten)
{
	FILE *out;
	int i;

	out = fopen(outputPath, "wb");
	if (!out)
	{
		trap->Print("Failed opening export file: %s\n", outputPath);
		return NULL;
	}

	*bytesWritten = 1;
	for (i = 0; i < colCount; i++)
	{
		const char *name = sqlite3_column_name(stmt, i);
		if (i > 0)
		{
			fputc(',', out);
			(*bytesWritten)++;
		}
		G_WriteTrackedCSVCell(out, name);
		*bytesWritten += G_TrackedCSVCellLength(name);
	}
	fputc('\n', out);
	return out;
}

//Removes split parts left behind by an earlier, larger export with the same file names.
static void G_RemoveTrackedExportParts(const char *dbDir, char pathSep, const char *safePrefix,
	const char *baseName, const char *exportSuffix, int firstPart)
{
	char partName[96];
	char partPath[MAX_OSPATH];
	int part;

	for (part = (firstPart < 1) ? 1 : firstPart; part <= TRACKED_EXPORT_MAX_PARTS; part++)
	{
		G_BuildTrackedExportPartName(baseName, exportSuffix, part, partName, sizeof(partName));
		G_BuildTrackedExportPath(dbDir, pathSep, safePrefix, partName, partPath, sizeof(partPath));
		if (remove(partPath) != 0 && part > 1)
			break;
	}
}

static qboolean G_ExportTrackedQueryCSV(sqlite3 *db, const char *sql, const char *dbDir, char pathSep,
	const char *safePrefix, const char *baseName, const char *exportSuffix, int *rowsWritten, int *partsWritten)
{
	sqlite3_stmt *stmt = NULL;
	FILE *out;
	char partName[96];
	char outputPath[MAX_OSPATH];
	size_t partBytes = 0;
	int s;
	int i;
	int colCount;
	int localRows = 0;
	int partRows = 0;
	int part = 1;

	if (!db || !sql || !sql[0] || !baseName || !baseName[0])
		return qfalse;

	G_BuildTrackedExportPartName(baseName, exportSuffix, part, partName, sizeof(partName));
	G_BuildTrackedExportPath(dbDir, pathSep, safePrefix, partName, outputPath, sizeof(outputPath));

	s = sqlite3_prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL);
	if (s != SQLITE_OK || !stmt)
	{
		trap->Print("Duel tracking: could not prepare export query for \"%s\" (%i: %s)\n",
			partName, s, sqlite3_errmsg(db));
		if (stmt)
			sqlite3_finalize(stmt);
		return qfalse;
	}
	colCount = sqlite3_column_count(stmt);
	if (colCount <= 0)
	{
		trap->Print("Duel tracking: export query for \"%s\" returned no columns.\n", partName);
		CALL_SQLITE(finalize(stmt));
		return qfalse;
	}

	out = G_OpenTrackedExportPart(stmt, colCount, outputPath, &partBytes);
	if (!out)
	{
		CALL_SQLITE(finalize(stmt));
		return qfalse;
	}

	while ((s = sqlite3_step(stmt)) == SQLITE_ROW)
	{
		size_t rowBytes = 1;

		for (i = 0; i < colCount; i++)
		{
			const unsigned char *text = sqlite3_column_text(stmt, i);
			rowBytes += G_TrackedCSVCellLength(text ? (const char *)text : "") + ((i > 0) ? 1 : 0);
		}

		//Start a new part (with its own header) before this row would push the file over the cap.
		if (partRows > 0 && part < TRACKED_EXPORT_MAX_PARTS &&
			partBytes + rowBytes > TRACKED_EXPORT_MAX_PART_BYTES)
		{
			fclose(out);
			part++;
			partRows = 0;
			G_BuildTrackedExportPartName(baseName, exportSuffix, part, partName, sizeof(partName));
			G_BuildTrackedExportPath(dbDir, pathSep, safePrefix, partName, outputPath, sizeof(outputPath));
			out = G_OpenTrackedExportPart(stmt, colCount, outputPath, &partBytes);
			if (!out)
			{
				CALL_SQLITE(finalize(stmt));
				G_RemoveTrackedExportParts(dbDir, pathSep, safePrefix, baseName, exportSuffix, 1);
				return qfalse;
			}
		}

		for (i = 0; i < colCount; i++)
		{
			const unsigned char *text;
			if (i > 0)
				fputc(',', out);
			text = sqlite3_column_text(stmt, i);
			G_WriteTrackedCSVCell(out, text ? (const char *)text : "");
		}
		fputc('\n', out);
		partBytes += rowBytes;
		partRows++;
		localRows++;
	}

	fclose(out);
	if (s != SQLITE_DONE)
	{
		G_ErrorPrint("ERROR: SQL Select Failed (G_ExportTrackedQueryCSV)", s);
		G_TrackedDBError(va("export query for \"%s\"", partName), db, s);
		CALL_SQLITE(finalize(stmt));
		G_RemoveTrackedExportParts(dbDir, pathSep, safePrefix, baseName, exportSuffix, 1);
		return qfalse;
	}

	CALL_SQLITE(finalize(stmt));
	G_RemoveTrackedExportParts(dbDir, pathSep, safePrefix, baseName, exportSuffix, part + 1);
	if (rowsWritten)
		*rowsWritten = localRows;
	if (partsWritten)
		*partsWritten = part;
	return qtrue;
}

static qboolean G_TrackedTableHasAllColumns(sqlite3 *db, const char *tableName,
	const char *const *columns, int count)
{
	int i;

	for (i = 0; i < count; i++)
	{
		if (!G_TrackedTableHasColumn(db, tableName, columns[i]))
		{
			trap->Print("Duel tracking: %s is missing column \"%s\", excluding it from this export.\n",
				tableName, columns[i]);
			return qfalse;
		}
	}
	return qtrue;
}

static const char *const g_trackedDuelSummaryColumns[] = {
	"source_context", "start_time", "end_time", "duration", "mapname", "type",
	"winner_key", "winner_label", "winner_kind", "winner_side",
	"loser_key", "loser_label", "loser_kind", "loser_side",
	"draw", "winner_opening", "loser_opening", "result", "arcade_level",
	"capture_version", "capture_revision"
};

static const char *const g_trackedDuelEventColumns[] = {
	"summary_id", "participant_key", "opponent_key", "opponent_label", "opponent_kind",
	"rel_time", "event_index", "sequence_id", "event_type", "power", "amount", "state",
	"range_bucket", "buttons", "saber_move", "enemy_saber_move", "yaw_delta",
	"self_hp", "self_armor", "self_force", "enemy_hp", "enemy_armor", "enemy_force",
	"sequence_label", "quality", "note", "swing_side", "pre_swing_strafe",
	"movement_intent", "radial_speed",
	"yaw_sweep", "attack_elapsed_ms", "throw_yaw_offset",
	"participant_label", "participant_kind", "forwardmove", "rightmove", "upmove",
	"saber_stance", "grounded", "damage_source", "damage_attacker_key",
	"controller_owns_inputs", "controller_family", "controller_enabled", "controller_fanbias",
	"controller_candidate", "controller_mistakebias", "controller_skill"
};

static const char *const g_trackedDuelGeometryColumns[] = {
	"summary_id", "participant_key", "opponent_key", "rel_time", "event_index",
	"self_x", "self_y", "self_z", "enemy_x", "enemy_y", "enemy_z",
	"self_vx", "self_vy", "self_vz", "enemy_vx", "enemy_vy", "enemy_vz",
	"self_yaw", "enemy_yaw"
};

/*
 * Duel and arcade records now share one table family, with LocalDuelTrackSummary.source_context
 * separating them, so each export is a single SELECT instead of a duel/arcade UNION. The session
 * export writes, in order: export_format_version, record_type, source_context, record_id,
 * start/end/duration/map/type/result/arcade_level, participant identity, duel winner+loser
 * identity, draw/openings, and the aggregated tracked combat counters.
 */
static const char *G_GetTrackedSessionExportQuery(void)
{
	return
		"SELECT 4 AS export_format_version, "
		"CASE WHEN s.source_context = 'arcade' THEN 'arcade_session' ELSE 'duel_summary' END AS record_type, "
		"s.source_context, s.id AS record_id, "
		"s.start_time, s.end_time, s.duration, s.mapname, s.type, s.result, s.arcade_level, "
		"CASE WHEN s.source_context = 'arcade' THEN COALESCE(p.participant_key, '') ELSE '' END AS participant_key, "
		"CASE WHEN s.source_context = 'arcade' THEN COALESCE(p.participant_label, '') ELSE '' END AS participant_label, "
		"CASE WHEN s.source_context = 'arcade' THEN COALESCE(p.participant_kind, 0) ELSE 0 END AS participant_kind, "
		"s.winner_key, s.winner_label, s.winner_kind, s.winner_side, "
		"s.loser_key, s.loser_label, s.loser_kind, s.loser_side, s.draw, "
		"s.winner_opening, s.loser_opening, "
		"COALESCE(p.total_kills, 0) AS total_kills, COALESCE(p.total_force_spent, 0) AS total_force_spent, "
		"COALESCE(p.total_force_regen, 0) AS total_force_regen, COALESCE(p.total_damage_taken, 0) AS total_damage_taken, "
		"COALESCE(p.total_damage_dealt, 0) AS total_damage_dealt, COALESCE(p.low_force_windows, 0) AS low_force_windows, "
		"COALESCE(p.knockdown_events, 0) AS knockdown_events, COALESCE(p.counter_successes, 0) AS counter_successes, "
		"COALESCE(p.punish_successes, 0) AS punish_successes, COALESCE(p.reset_successes, 0) AS reset_successes, "
		"COALESCE(p.saber_throw_punishes, 0) AS saber_throw_punishes, s.capture_version, s.capture_revision "
		"FROM LocalDuelTrackSummary s "
		"LEFT JOIN (SELECT summary_id, MAX(participant_key) AS participant_key, MAX(participant_label) AS participant_label, "
		"MAX(participant_kind) AS participant_kind, SUM(total_kills) AS total_kills, SUM(total_force_spent) AS total_force_spent, "
		"SUM(total_force_regen) AS total_force_regen, SUM(total_damage_taken) AS total_damage_taken, "
		"SUM(total_damage_dealt) AS total_damage_dealt, SUM(low_force_windows) AS low_force_windows, "
		"SUM(knockdown_events) AS knockdown_events, SUM(counter_successes) AS counter_successes, "
		"SUM(punish_successes) AS punish_successes, SUM(reset_successes) AS reset_successes, "
		"SUM(saber_throw_punishes) AS saber_throw_punishes FROM LocalDuelTrackParticipant GROUP BY summary_id) p ON p.summary_id = s.id";
}

static const char *G_GetTrackedParticipantExportQuery(void)
{
	return
		"SELECT 6 AS export_format_version, 'duel_participant' AS record_type, "
		"COALESCE(s.source_context, 'duel') AS source_context, p.id AS record_id, p.summary_id, "
		"p.participant_key, p.participant_label, p.participant_kind, p.elo_key, p.opponent_key, p.won, p.side, p.opponent_side, p.matchup, "
		"p.total_force_spent, p.total_force_regen, p.ending_force, p.ending_hp, p.ending_armor, "
		"p.low_force_windows, p.grip_cripple_events, p.saber_throw_punishes, p.knockdown_events, p.late_defense_spends, "
		"p.opening_tactic, p.primary_issue, p.spent_neutral, p.spent_advantage, p.spent_disadvantage, p.spent_panic, p.spent_finishing, "
		"p.force_push, p.force_pull, p.force_grip, p.force_drain, p.force_rage, p.force_absorb, p.force_protect, p.force_heal, "
		"p.force_speed, p.force_seeing, p.force_unknown, "
		"p.total_kills, p.total_damage_taken, p.total_damage_dealt, "
		"p.counter_successes, p.punish_successes, p.reset_successes, "
		"p.event_total, p.event_retained, p.event_dropped, p.event_critical_dropped, p.event_compactions, p.event_allocation_failures, "
		"COALESCE(s.capture_version, 0) AS capture_version, COALESCE(s.capture_revision, '') AS capture_revision "
		"FROM LocalDuelTrackParticipant p "
		"LEFT JOIN LocalDuelTrackSummary s ON s.id = p.summary_id";
}

static const char *G_GetTrackedEventExportQuery(void)
{
	return
		"SELECT 8 AS export_format_version, "
		"CASE WHEN COALESCE(s.source_context, 'duel') = 'arcade' THEN 'arcade_event' ELSE 'duel_event' END AS record_type, "
		"COALESCE(s.source_context, 'duel') AS source_context, e.id AS record_id, e.summary_id AS parent_id, "
		"e.participant_key, "
		"COALESCE(NULLIF(e.participant_label, ''), (SELECT p.participant_label FROM LocalDuelTrackParticipant p "
		"WHERE p.summary_id = e.summary_id AND p.participant_key = e.participant_key LIMIT 1), '') AS participant_label, "
		"COALESCE(NULLIF(e.participant_kind, 0), (SELECT p.participant_kind FROM LocalDuelTrackParticipant p "
		"WHERE p.summary_id = e.summary_id AND p.participant_key = e.participant_key LIMIT 1), 0) AS participant_kind, "
		"e.opponent_key, e.opponent_label, e.opponent_kind, "
		"e.rel_time, e.event_index, e.sequence_id, e.event_type, e.power, e.amount, e.state, e.range_bucket, "
		"e.buttons, e.saber_move, e.enemy_saber_move, e.yaw_delta, "
		"e.self_hp, e.self_armor, e.self_force, e.enemy_hp, e.enemy_armor, e.enemy_force, e.sequence_label, e.quality, e.note, "
		"e.swing_side, e.pre_swing_strafe, e.movement_intent, e.radial_speed, "
		"e.yaw_sweep, e.attack_elapsed_ms, e.throw_yaw_offset, "
		"e.forwardmove, e.rightmove, e.upmove, e.saber_stance, e.grounded, e.damage_source, e.damage_attacker_key, "
		"e.controller_owns_inputs, e.controller_family, e.controller_enabled, e.controller_fanbias, "
		"e.controller_candidate, e.controller_mistakebias, e.controller_skill, "
		"COALESCE(s.capture_version, 0) AS capture_version, COALESCE(s.capture_revision, '') AS capture_revision "
		"FROM LocalDuelTrackEvent e "
		"LEFT JOIN LocalDuelTrackSummary s ON s.id = e.summary_id";
}

static const char *G_GetTrackedGeometryExportQuery(void)
{
	return
		"SELECT 4 AS export_format_version, "
		"CASE WHEN COALESCE(s.source_context, 'duel') = 'arcade' THEN 'arcade_geometry' ELSE 'duel_geometry' END AS record_type, "
		"COALESCE(s.source_context, 'duel') AS source_context, g.id AS record_id, g.summary_id AS parent_id, "
		"g.participant_key, g.opponent_key, g.rel_time, g.event_index, "
		"g.self_x, g.self_y, g.self_z, g.enemy_x, g.enemy_y, g.enemy_z, "
		"g.self_vx, g.self_vy, g.self_vz, g.enemy_vx, g.enemy_vy, g.enemy_vz, g.self_yaw, g.enemy_yaw, "
		"COALESCE(s.capture_version, 0) AS capture_version, COALESCE(s.capture_revision, '') AS capture_revision "
		"FROM LocalDuelTrackGeometry g "
		"LEFT JOIN LocalDuelTrackSummary s ON s.id = g.summary_id";
}

static const char *G_GetTrackedAggregateExportQuery(void)
{
	return
		"SELECT 3 AS export_format_version, 'duel_aggregate' AS record_type, 'duel' AS source_context, "
		"participant_key, participant_kind, side, matchup, duels, wins, losses, "
		"total_force_spent, total_force_regen, low_force_deaths, grip_cripples, saber_throw_punishes, "
		"force_push, force_pull, force_grip, force_drain, force_rage, force_absorb, force_protect, force_heal, "
		"force_speed, force_seeing, force_unknown, capture_version, capture_revision "
		"FROM LocalDuelTrackAggregate";
}

static const char *G_GetTrackedLearnedExportQuery(void)
{
	return
		"SELECT 1 AS export_format_version, 'bot_learned_sequence' AS record_type, "
		"CASE source_kind WHEN 1 THEN 'bot' ELSE 'human' END AS source, skill_band, ctx_key, "
		"(ctx_key & 3) AS self_ha_bucket, ((ctx_key >> 2) & 3) AS enemy_ha_bucket, "
		"((ctx_key >> 4) & 3) AS self_force_bucket, ((ctx_key >> 6) & 3) AS enemy_force_bucket, "
		"((ctx_key >> 8) & 3) AS range_bucket, ((ctx_key >> 10) & 3) AS self_stance, ((ctx_key >> 12) & 3) AS enemy_stance, "
		"CASE stimulus " BOTLEARN_SQL_TOKEN_CASES " END AS stimulus, "
		"CASE response " BOTLEARN_SQL_TOKEN_CASES " END AS response, "
		"CASE follow1 " BOTLEARN_SQL_TOKEN_CASES " END AS follow1, "
		"CASE follow2 " BOTLEARN_SQL_TOKEN_CASES " END AS follow2, "
		"samples, wins, net_damage, "
		"CASE WHEN samples > 0 THEN (total_response_ms / samples) ELSE 0 END AS avg_response_ms, "
		"capture_version "
		"FROM LocalBotLearnedSequence ORDER BY samples DESC";
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

static void G_ExportTrackedTable(sqlite3 *db, qboolean wantExport, const char *sql, const char *baseName,
	const char *dbDir, char pathSep, const char *safePrefix, const char *exportSuffix)
{
	char exportFileName[96];
	int rows = 0;
	int parts = 0;

	G_BuildTrackedExportPartName(baseName, exportSuffix, 1, exportFileName, sizeof(exportFileName));
	if (!wantExport)
		G_RemoveTrackedExportParts(dbDir, pathSep, safePrefix, baseName, exportSuffix, 1);
	else if (!G_ExportTrackedQueryCSV(db, sql, dbDir, pathSep, safePrefix, baseName, exportSuffix, &rows, &parts))
		trap->Print("Duel tracking: %s export failed, kept any previous %s.\n", baseName, exportFileName);
	else if (parts > 1)
		trap->Print("Exported tracked %s (%d rows) split into %d files under 25MB: %s ... %s%s_part%d.csv\n",
			baseName, rows, parts, exportFileName, baseName, exportSuffix, parts);
	else
		trap->Print("Exported tracked %s (%d rows): %s\n", baseName, rows, exportFileName);
}

static void G_BuildTrackedExportSuffix(qboolean timestamped, char *out, int outSize)
{
	if (!out || outSize < 1)
		return;

	out[0] = '\0';
	if (timestamped)
	{
		time_t rawtime;
		struct tm *timeinfo;
		char timestamp[32];

		time(&rawtime);
		timeinfo = localtime(&rawtime);
		if (timeinfo && strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", timeinfo) > 0)
			Com_sprintf(out, outSize, "_%s", timestamp);
	}
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

static void G_GetLocalDBGameDir(char *out, int outSize)
{
	if (!out || outSize < 1)
		return;

	trap->Cvar_VariableStringBuffer("fs_game", out, outSize);
	if (!VALIDSTRING(out))
	{
		trap->Cvar_VariableStringBuffer("fs_basegame", out, outSize);
		if (!VALIDSTRING(out))
			Q_strncpyz(out, TAYSTJKGAME, outSize);
	}
}

//sqlite3_open() only records the path, the file itself is not touched (or created)
//until the first real access. Run a trivial statement so a broken directory or a
//read-only location fails here (and so the database file is actually created).
static qboolean G_OpenSQLiteFile(const char *path, sqlite3 **dbOut, const char *context)
{
	sqlite3 *db = NULL;
	int s;

	if (!dbOut)
		return qfalse;
	*dbOut = NULL;
	if (!path || !path[0])
		return qfalse;

	s = sqlite3_open(path, &db);
	if (s == SQLITE_OK)
		s = sqlite3_exec(db, "PRAGMA user_version;", NULL, NULL, NULL);
	if (s == SQLITE_OK)
	{
		*dbOut = db;
		return qtrue;
	}

	trap->Print("ERROR: could not open %s database \"%s\" (%i: %s)\n",
		context ? context : "sqlite", path, s, db ? sqlite3_errmsg(db) : "unknown error");
	if (db)
		sqlite3_close(db);
	return qfalse;
}

static qboolean G_OpenTrackedLocalDB(sqlite3 **dbOut, char *resolvedPath, int resolvedPathSize)
{
	char fallbackDbPath[MAX_OSPATH];
	char fs_game[MAX_QPATH];
	sqlite3 *db = NULL;

	if (!dbOut)
		return qfalse;
	*dbOut = NULL;
	if (!LOCAL_DUELTRACK_DB_PATH[0])
		return qfalse;

	G_GetLocalDBGameDir(fs_game, sizeof(fs_game));
	Com_sprintf(fallbackDbPath, sizeof(fallbackDbPath), "%s/dueltracks.db", fs_game);

	if (G_OpenSQLiteFile(LOCAL_DUELTRACK_DB_PATH, &db, "duel tracking"))
	{
		*dbOut = db;
		if (resolvedPath && resolvedPathSize > 0)
			Q_strncpyz(resolvedPath, LOCAL_DUELTRACK_DB_PATH, resolvedPathSize);
		return qtrue;
	}

	if (Q_stricmp(LOCAL_DUELTRACK_DB_PATH, fallbackDbPath) &&
		G_OpenSQLiteFile(fallbackDbPath, &db, "duel tracking"))
	{
		*dbOut = db;
		if (resolvedPath && resolvedPathSize > 0)
			Q_strncpyz(resolvedPath, fallbackDbPath, resolvedPathSize);
		return qtrue;
	}
	return qfalse;
}

//The account/elo database is opened all over this file through LOCAL_DB_PATH. Resolve
//(and if needed repoint) that path once during init so a bad path is reported loudly
//instead of turning every later query into a SQLITE_MISUSE (21) failure.
static qboolean G_OpenLocalAccountDB(sqlite3 **dbOut)
{
	char fallbackDbPath[MAX_OSPATH];
	char fs_game[MAX_QPATH];
	sqlite3 *db = NULL;

	if (!dbOut)
		return qfalse;
	*dbOut = NULL;
	if (!LOCAL_DB_PATH[0])
		return qfalse;

	if (G_OpenSQLiteFile(LOCAL_DB_PATH, &db, "account"))
	{
		*dbOut = db;
		return qtrue;
	}

	G_GetLocalDBGameDir(fs_game, sizeof(fs_game));
	Com_sprintf(fallbackDbPath, sizeof(fallbackDbPath), "%s/data.db", fs_game);
	if (Q_stricmp(LOCAL_DB_PATH, fallbackDbPath) &&
		G_OpenSQLiteFile(fallbackDbPath, &db, "account"))
	{
		trap->Print("Account database falling back to \"%s\".\n", fallbackDbPath);
		Q_strncpyz(LOCAL_DB_PATH, fallbackDbPath, sizeof(LOCAL_DB_PATH));
		*dbOut = db;
		return qtrue;
	}
	return qfalse;
}

static int G_CountTrackedDuelTableRows(sqlite3 *db, const char *tableName)
{
	sqlite3_stmt *stmt = NULL;
	char sql[128];
	int s;
	int count = -1;

	if (!db || !G_IsAllowedTrackedTableName(tableName))
		return -1;
	if (!G_DoesTrackedDuelTableExist(db, tableName))
		return -1;

	Com_sprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM main.%s", tableName);
	s = sqlite3_prepare_v2(db, sql, strlen(sql) + 1, &stmt, NULL);
	if (s != SQLITE_OK || !stmt)
	{
		G_TrackedDBError(va("count rows in %s", tableName), db, s);
		if (stmt)
			sqlite3_finalize(stmt);
		return -1;
	}
	s = sqlite3_step(stmt);
	if (s == SQLITE_ROW)
		count = sqlite3_column_int(stmt, 0);
	else
		G_TrackedDBError(va("count rows in %s", tableName), db, s);
	CALL_SQLITE(finalize(stmt));
	return count;
}

static void G_PrintTrackedDuelTableCounts(sqlite3 *db, const char *dbPath)
{
	int i;

	trap->Print("Duel tracking database: %s\n", (dbPath && dbPath[0]) ? dbPath : LOCAL_DUELTRACK_DB_PATH);
	for (i = 0; i < (int)ARRAY_LEN(g_trackedDuelTableNames); i++)
	{
		const int count = G_CountTrackedDuelTableRows(db, g_trackedDuelTableNames[i]);
		if (count < 0)
			trap->Print("  %-26s unavailable\n", g_trackedDuelTableNames[i]);
		else
			trap->Print("  %-26s %i rows\n", g_trackedDuelTableNames[i], count);
	}
}

void Svcmd_DuelTrackInfo_f(void)
{
	sqlite3 *db = NULL;
	char effectiveDbPath[MAX_OSPATH];

	if (!LOCAL_DUELTRACK_DB_PATH[0])
	{
		trap->Print("dueltrackinfo failed: duel tracking database path is not initialized.\n");
		return;
	}

	if (!G_OpenTrackedLocalDB(&db, effectiveDbPath, sizeof(effectiveDbPath)))
	{
		trap->Print("dueltrackinfo failed: unable to open the duel tracking database \"%s\".\n",
			LOCAL_DUELTRACK_DB_PATH);
		return;
	}

	G_EnsureLocalDuelTrackingSchema(db);
	G_PrintTrackedDuelTableCounts(db, effectiveDbPath);
	CALL_SQLITE(close(db));
}

//---------------------------------------------------------------------------------------
// Legacy duel tracking import
//
// Older builds kept duel tracking in dueltrack.db (and before that in data.db), with arcade
// runs in separate LocalArcadeTrack* tables. dueltracks.db starts clean, so importDuelTrack
// merges those rows back in. The legacy files are only read, never changed, and each source
// is recorded in LocalDuelTrackImport so it can't be imported twice (resetdueltrack clears it).
//---------------------------------------------------------------------------------------

#define TRACKED_IMPORT_LEGACY_FILE	"dueltrack.db"
#define TRACKED_IMPORT_COLUMNS_SIZE	4096

static qboolean G_TrackedImportExec(sqlite3 *db, const char *sql, const char *context)
{
	const int s = sqlite3_exec(db, sql, NULL, NULL, NULL);

	if (s != SQLITE_OK)
	{
		G_TrackedDBError(context, db, s);
		return qfalse;
	}
	return qtrue;
}

static void G_EnsureTrackedImportTable(sqlite3 *db)
{
	G_TrackedImportExec(db, "CREATE TABLE IF NOT EXISTS LocalDuelTrackImport("
		"source VARCHAR(260) PRIMARY KEY, imported_at UNSIGNED INTEGER, sessions UNSIGNED INTEGER)",
		"CREATE TABLE LocalDuelTrackImport");
}

static qboolean G_IsTrackedImportDone(sqlite3 *db, const char *source)
{
	sqlite3_stmt *stmt = NULL;
	qboolean done = qfalse;

	if (!G_DoesTrackedDuelTableExist(db, "LocalDuelTrackImport"))
		return qfalse;
	if (sqlite3_prepare_v2(db, "SELECT 1 FROM main.LocalDuelTrackImport WHERE source = ? LIMIT 1", -1, &stmt, NULL) != SQLITE_OK || !stmt)
	{
		if (stmt)
			sqlite3_finalize(stmt);
		return qfalse;
	}
	sqlite3_bind_text(stmt, 1, source, -1, SQLITE_TRANSIENT);
	done = (sqlite3_step(stmt) == SQLITE_ROW) ? qtrue : qfalse;
	sqlite3_finalize(stmt);
	return done;
}

static qboolean G_TrackedImportIsIdentifier(const char *name)
{
	if (!name || !name[0])
		return qfalse;
	for (; *name; name++)
	{
		if (!isalnum((unsigned char)*name) && *name != '_')
			return qfalse;
	}
	return qtrue;
}

static qboolean G_TrackedSchemaHasTable(sqlite3 *db, const char *schema, const char *tableName)
{
	sqlite3_stmt *stmt = NULL;
	qboolean exists = qfalse;
	char sql[128];

	Com_sprintf(sql, sizeof(sql), "SELECT 1 FROM %s.sqlite_master WHERE type='table' AND name=? LIMIT 1", schema);
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK || !stmt)
	{
		if (stmt)
			sqlite3_finalize(stmt);
		return qfalse;
	}
	sqlite3_bind_text(stmt, 1, tableName, -1, SQLITE_TRANSIENT);
	exists = (sqlite3_step(stmt) == SQLITE_ROW) ? qtrue : qfalse;
	sqlite3_finalize(stmt);
	return exists;
}

static qboolean G_TrackedSchemaHasColumn(sqlite3 *db, const char *schema, const char *tableName, const char *columnName)
{
	sqlite3_stmt *stmt = NULL;
	qboolean found = qfalse;
	char sql[160];
	int s;

	Com_sprintf(sql, sizeof(sql), "PRAGMA %s.table_info(%s)", schema, tableName);
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK || !stmt)
	{
		if (stmt)
			sqlite3_finalize(stmt);
		return qfalse;
	}
	while ((s = sqlite3_step(stmt)) == SQLITE_ROW)
	{
		const unsigned char *name = sqlite3_column_text(stmt, 1);
		if (name && !Q_stricmp((const char *)name, columnName))
		{
			found = qtrue;
			break;
		}
	}
	sqlite3_finalize(stmt);
	return found;
}

static qboolean G_TrackedImportIsExcluded(const char *name, const char *const *excluded, int excludedCount)
{
	int i;

	for (i = 0; i < excludedCount; i++)
	{
		if (!Q_stricmp(name, excluded[i]))
			return qtrue;
	}
	return qfalse;
}

//Builds "a, b, c" (and "l.a, l.b, l.c" for the select side) from the columns the legacy and
//current tables share, so rows written by any older schema version import cleanly.
static int G_BuildTrackedImportColumns(sqlite3 *db, const char *legacyTable, const char *mainTable,
	const char *const *excluded, int excludedCount, char *insertCols, char *selectCols, int size)
{
	sqlite3_stmt *stmt = NULL;
	char sql[160];
	int count = 0;
	int s;

	insertCols[0] = '\0';
	selectCols[0] = '\0';
	Com_sprintf(sql, sizeof(sql), "PRAGMA legacy.table_info(%s)", legacyTable);
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK || !stmt)
	{
		if (stmt)
			sqlite3_finalize(stmt);
		return 0;
	}
	while ((s = sqlite3_step(stmt)) == SQLITE_ROW)
	{
		const char *name = (const char *)sqlite3_column_text(stmt, 1);

		if (!G_TrackedImportIsIdentifier(name) || G_TrackedImportIsExcluded(name, excluded, excludedCount))
			continue;
		if (!G_TrackedSchemaHasColumn(db, "main", mainTable, name))
			continue;
		if ((int)(strlen(selectCols) + strlen(name) + 8) >= size)
			break;
		Q_strcat(insertCols, size, va("%s%s", count ? ", " : "", name));
		Q_strcat(selectCols, size, va("%sl.%s", count ? ", " : "", name));
		count++;
	}
	sqlite3_finalize(stmt);
	return count;
}

static int G_TrackedImportScalar(sqlite3 *db, const char *sql)
{
	sqlite3_stmt *stmt = NULL;
	int value = 0;

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK || !stmt)
	{
		if (stmt)
			sqlite3_finalize(stmt);
		return 0;
	}
	if (sqlite3_step(stmt) == SQLITE_ROW)
		value = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);
	return value;
}

//Copies child rows (participants/events/geometry) whose parent was imported, re-pointing
//them at the new summary ids through temp.dt_import_map.
static qboolean G_ImportTrackedChildRows(sqlite3 *db, const char *legacyTable, const char *legacyParentCol,
	const char *mainTable, int *rowsOut)
{
	static const char *const excluded[] = { "id", "summary_id", "session_id" };
	char *insertCols;
	char *selectCols;
	char *sql;
	qboolean ok;

	if (rowsOut)
		*rowsOut = 0;
	if (!G_TrackedSchemaHasTable(db, "legacy", legacyTable) ||
		!G_TrackedSchemaHasColumn(db, "legacy", legacyTable, legacyParentCol))
		return qtrue;

	insertCols = (char *)malloc(TRACKED_IMPORT_COLUMNS_SIZE);
	selectCols = (char *)malloc(TRACKED_IMPORT_COLUMNS_SIZE);
	sql = (char *)malloc(TRACKED_IMPORT_COLUMNS_SIZE * 3);
	if (!insertCols || !selectCols || !sql)
	{
		free(insertCols);
		free(selectCols);
		free(sql);
		return qfalse;
	}

	G_BuildTrackedImportColumns(db, legacyTable, mainTable, excluded, ARRAY_LEN(excluded),
		insertCols, selectCols, TRACKED_IMPORT_COLUMNS_SIZE);
	Com_sprintf(sql, TRACKED_IMPORT_COLUMNS_SIZE * 3,
		"INSERT INTO main.%s(summary_id%s%s) SELECT m.new_id%s%s FROM legacy.%s l "
		"JOIN temp.dt_import_map m ON m.old_id = l.%s ORDER BY l.rowid",
		mainTable, insertCols[0] ? ", " : "", insertCols, selectCols[0] ? ", " : "", selectCols,
		legacyTable, legacyParentCol);
	ok = G_TrackedImportExec(db, sql, va("import %s into %s", legacyTable, mainTable));
	if (ok && rowsOut)
		*rowsOut = sqlite3_changes(db);

	free(insertCols);
	free(selectCols);
	free(sql);
	return ok;
}

//Adds legacy per-identity aggregate counters onto matching rows and inserts the rest.
static qboolean G_ImportTrackedAggregate(sqlite3 *db)
{
	static const char *const keys[] = { "participant_key", "participant_kind", "side", "matchup" };
	char *insertCols;
	char *selectCols;
	char *sql;
	char match[512];
	qboolean ok = qtrue;
	int i;

	if (!G_TrackedSchemaHasTable(db, "legacy", "LocalDuelTrackAggregate"))
		return qtrue;
	for (i = 0; i < (int)ARRAY_LEN(keys); i++)
	{
		if (!G_TrackedSchemaHasColumn(db, "legacy", "LocalDuelTrackAggregate", keys[i]))
		{
			trap->Print("importDuelTrack: legacy aggregate table is missing \"%s\", aggregates not imported.\n", keys[i]);
			return qtrue;
		}
	}

	insertCols = (char *)malloc(TRACKED_IMPORT_COLUMNS_SIZE);
	selectCols = (char *)malloc(TRACKED_IMPORT_COLUMNS_SIZE);
	sql = (char *)malloc(TRACKED_IMPORT_COLUMNS_SIZE * 4);
	if (!insertCols || !selectCols || !sql)
	{
		free(insertCols);
		free(selectCols);
		free(sql);
		return qfalse;
	}

	match[0] = '\0';
	for (i = 0; i < (int)ARRAY_LEN(keys); i++)
		Q_strcat(match, sizeof(match), va("%sl.%s IS main.LocalDuelTrackAggregate.%s", i ? " AND " : "", keys[i], keys[i]));

	//Counter columns: everything shared except the key columns.
	if (G_BuildTrackedImportColumns(db, "LocalDuelTrackAggregate", "LocalDuelTrackAggregate", keys, ARRAY_LEN(keys),
		insertCols, selectCols, TRACKED_IMPORT_COLUMNS_SIZE) > 0)
	{
		char *setList = (char *)malloc(TRACKED_IMPORT_COLUMNS_SIZE * 3);
		char *cursor;
		char *token;

		if (!setList)
			ok = qfalse;
		else
		{
			char columnsCopy[TRACKED_IMPORT_COLUMNS_SIZE];

			setList[0] = '\0';
			Q_strncpyz(columnsCopy, insertCols, sizeof(columnsCopy));
			for (cursor = columnsCopy; cursor && *cursor; )
			{
				token = cursor;
				cursor = strstr(cursor, ", ");
				if (cursor)
				{
					*cursor = '\0';
					cursor += 2;
				}
				Q_strcat(setList, TRACKED_IMPORT_COLUMNS_SIZE * 3,
					va("%s%s = COALESCE(%s, 0) + COALESCE((SELECT l.%s FROM legacy.LocalDuelTrackAggregate l WHERE %s), 0)",
						setList[0] ? ", " : "", token, token, token, match));
			}
			Com_sprintf(sql, TRACKED_IMPORT_COLUMNS_SIZE * 4,
				"UPDATE main.LocalDuelTrackAggregate SET %s WHERE EXISTS (SELECT 1 FROM legacy.LocalDuelTrackAggregate l WHERE %s)",
				setList, match);
			ok = G_TrackedImportExec(db, sql, "merge legacy LocalDuelTrackAggregate");
			free(setList);
		}
	}

	if (ok)
	{
		static const char *const excludedNone[] = { "rowid" };

		G_BuildTrackedImportColumns(db, "LocalDuelTrackAggregate", "LocalDuelTrackAggregate", excludedNone, ARRAY_LEN(excludedNone),
			insertCols, selectCols, TRACKED_IMPORT_COLUMNS_SIZE);
		Com_sprintf(sql, TRACKED_IMPORT_COLUMNS_SIZE * 4,
			"INSERT INTO main.LocalDuelTrackAggregate(%s) SELECT %s FROM legacy.LocalDuelTrackAggregate l "
			"WHERE NOT EXISTS (SELECT 1 FROM main.LocalDuelTrackAggregate WHERE %s)",
			insertCols, selectCols, match);
		ok = G_TrackedImportExec(db, sql, "insert legacy LocalDuelTrackAggregate");
	}

	free(insertCols);
	free(selectCols);
	free(sql);
	return ok;
}

static qboolean G_ImportLegacyDuelSummaries(sqlite3 *db, int *sessionsOut, int *duplicatesOut)
{
	static const char *const excluded[] = { "id" };
	char insertCols[TRACKED_IMPORT_COLUMNS_SIZE];
	char selectCols[TRACKED_IMPORT_COLUMNS_SIZE];
	char sql[TRACKED_IMPORT_COLUMNS_SIZE * 3];
	int legacyTotal;

	*sessionsOut = 0;
	*duplicatesOut = 0;
	if (!G_TrackedSchemaHasTable(db, "legacy", "LocalDuelTrackSummary"))
		return qtrue;

	legacyTotal = G_TrackedImportScalar(db, "SELECT COUNT(*) FROM legacy.LocalDuelTrackSummary");
	//Skip summaries already present (same times, map and both identities), e.g. rows that a
	//previous build had copied from data.db into dueltrack.db.
	if (!G_TrackedImportExec(db,
		"INSERT INTO temp.dt_import_map(old_id, new_id) "
		"SELECT l.id, l.id + (SELECT COALESCE(MAX(id), 0) FROM main.LocalDuelTrackSummary) "
		"FROM legacy.LocalDuelTrackSummary l WHERE NOT EXISTS (SELECT 1 FROM main.LocalDuelTrackSummary m "
		"WHERE m.start_time IS l.start_time AND m.end_time IS l.end_time AND m.mapname IS l.mapname "
		"AND m.winner_key IS l.winner_key AND m.loser_key IS l.loser_key)",
		"map legacy LocalDuelTrackSummary ids"))
		return qfalse;
	*sessionsOut = G_TrackedImportScalar(db, "SELECT COUNT(*) FROM temp.dt_import_map");
	*duplicatesOut = legacyTotal - *sessionsOut;

	G_BuildTrackedImportColumns(db, "LocalDuelTrackSummary", "LocalDuelTrackSummary", excluded, ARRAY_LEN(excluded),
		insertCols, selectCols, sizeof(insertCols));
	Com_sprintf(sql, sizeof(sql),
		"INSERT INTO main.LocalDuelTrackSummary(id%s%s) SELECT m.new_id%s%s FROM legacy.LocalDuelTrackSummary l "
		"JOIN temp.dt_import_map m ON m.old_id = l.id ORDER BY l.id",
		insertCols[0] ? ", " : "", insertCols, selectCols[0] ? ", " : "", selectCols);
	return G_TrackedImportExec(db, sql, "import legacy LocalDuelTrackSummary");
}

static const char *const g_trackedLegacyArcadeSessionColumns[] = {
	"id", "start_time", "end_time", "duration", "mapname", "participant_key", "participant_label",
	"participant_kind", "result", "arcade_level", "total_kills", "total_force_spent", "total_force_regen",
	"total_damage_taken", "total_damage_dealt", "low_force_windows", "knockdown_events",
	"counter_successes", "punish_successes", "reset_successes", "saber_return_punishes"
};

//Arcade runs used to live in LocalArcadeTrackSession/Event/Geometry; map them onto the shared
//duel table family the same way G_PersistTrackedArcadeCombat writes new runs.
static qboolean G_ImportLegacyArcadeSessions(sqlite3 *db, int *sessionsOut, int *duplicatesOut)
{
	char sql[2048];
	int legacyTotal;
	int i;

	*sessionsOut = 0;
	*duplicatesOut = 0;
	if (!G_TrackedSchemaHasTable(db, "legacy", "LocalArcadeTrackSession"))
		return qtrue;
	for (i = 0; i < (int)ARRAY_LEN(g_trackedLegacyArcadeSessionColumns); i++)
	{
		if (!G_TrackedSchemaHasColumn(db, "legacy", "LocalArcadeTrackSession", g_trackedLegacyArcadeSessionColumns[i]))
		{
			trap->Print("importDuelTrack: legacy arcade sessions are missing \"%s\", arcade runs not imported.\n",
				g_trackedLegacyArcadeSessionColumns[i]);
			return qtrue;
		}
	}

	if (!G_TrackedImportExec(db, "DELETE FROM temp.dt_import_map", "clear import id map"))
		return qfalse;
	legacyTotal = G_TrackedImportScalar(db, "SELECT COUNT(*) FROM legacy.LocalArcadeTrackSession");
	if (!G_TrackedImportExec(db,
		"INSERT INTO temp.dt_import_map(old_id, new_id) "
		"SELECT l.id, l.id + (SELECT COALESCE(MAX(id), 0) FROM main.LocalDuelTrackSummary) "
		"FROM legacy.LocalArcadeTrackSession l WHERE NOT EXISTS (SELECT 1 FROM main.LocalDuelTrackSummary m "
		"WHERE m.source_context = 'arcade' AND m.start_time IS l.start_time AND m.end_time IS l.end_time "
		"AND m.mapname IS l.mapname AND (m.winner_key IS l.participant_key OR m.loser_key IS l.participant_key))",
		"map legacy LocalArcadeTrackSession ids"))
		return qfalse;
	*sessionsOut = G_TrackedImportScalar(db, "SELECT COUNT(*) FROM temp.dt_import_map");
	*duplicatesOut = legacyTotal - *sessionsOut;

#define TRACKED_IMPORT_ARCADE_WON "(LOWER(COALESCE(l.result, '')) IN ('arcade_complete', 'level_clear'))"
	Com_sprintf(sql, sizeof(sql),
		"INSERT INTO main.LocalDuelTrackSummary(id, source_context, start_time, end_time, duration, type, mapname, "
		"winner_key, winner_label, winner_kind, winner_side, loser_key, loser_label, loser_kind, loser_side, draw, "
		"winner_opening, loser_opening, result, arcade_level) "
		"SELECT m.new_id, 'arcade', l.start_time, l.end_time, l.duration, %i, l.mapname, "
		"CASE WHEN " TRACKED_IMPORT_ARCADE_WON " THEN l.participant_key ELSE '' END, "
		"CASE WHEN " TRACKED_IMPORT_ARCADE_WON " THEN l.participant_label ELSE '' END, "
		"CASE WHEN " TRACKED_IMPORT_ARCADE_WON " THEN l.participant_kind ELSE 0 END, 0, "
		"CASE WHEN " TRACKED_IMPORT_ARCADE_WON " THEN '' ELSE l.participant_key END, "
		"CASE WHEN " TRACKED_IMPORT_ARCADE_WON " THEN '' ELSE l.participant_label END, "
		"CASE WHEN " TRACKED_IMPORT_ARCADE_WON " THEN 0 ELSE l.participant_kind END, 0, 0, '', '', "
		"COALESCE(NULLIF(l.result, ''), 'finished'), l.arcade_level "
		"FROM legacy.LocalArcadeTrackSession l JOIN temp.dt_import_map m ON m.old_id = l.id ORDER BY l.id",
		TRACKED_ARCADE_DUEL_TYPE);
	if (!G_TrackedImportExec(db, sql, "import legacy LocalArcadeTrackSession"))
		return qfalse;

	if (!G_TrackedImportExec(db,
		"INSERT INTO main.LocalDuelTrackParticipant(summary_id, participant_key, participant_label, participant_kind, "
		"elo_key, opponent_key, won, side, opponent_side, matchup, total_force_spent, total_force_regen, ending_force, "
		"ending_hp, ending_armor, low_force_windows, saber_throw_punishes, knockdown_events, total_kills, "
		"total_damage_taken, total_damage_dealt, counter_successes, punish_successes, reset_successes) "
		"SELECT m.new_id, l.participant_key, l.participant_label, l.participant_kind, l.participant_key, '', "
		"CASE WHEN " TRACKED_IMPORT_ARCADE_WON " THEN 1 ELSE 0 END, 0, 0, 0, l.total_force_spent, l.total_force_regen, "
		"0, 0, 0, l.low_force_windows, l.saber_return_punishes, l.knockdown_events, l.total_kills, "
		"l.total_damage_taken, l.total_damage_dealt, l.counter_successes, l.punish_successes, l.reset_successes "
		"FROM legacy.LocalArcadeTrackSession l JOIN temp.dt_import_map m ON m.old_id = l.id ORDER BY l.id",
		"import legacy arcade participants"))
		return qfalse;
#undef TRACKED_IMPORT_ARCADE_WON

	return G_ImportTrackedChildRows(db, "LocalArcadeTrackEvent", "session_id", "LocalDuelTrackEvent", NULL) &&
		G_ImportTrackedChildRows(db, "LocalArcadeTrackGeometry", "session_id", "LocalDuelTrackGeometry", NULL);
}

static int G_CountLegacyTrackedSessions(const char *path)
{
	sqlite3 *ldb = NULL;
	int count = 0;

	if (!path || !path[0])
		return 0;
	//Read-only open: never creates a missing legacy file.
	if (sqlite3_open_v2(path, &ldb, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
	{
		if (ldb)
			sqlite3_close(ldb);
		return 0;
	}
	if (G_TrackedSchemaHasTable(ldb, "main", "LocalDuelTrackSummary"))
		count += G_TrackedImportScalar(ldb, "SELECT COUNT(*) FROM LocalDuelTrackSummary");
	if (G_TrackedSchemaHasTable(ldb, "main", "LocalArcadeTrackSession"))
		count += G_TrackedImportScalar(ldb, "SELECT COUNT(*) FROM LocalArcadeTrackSession");
	sqlite3_close(ldb);
	return count;
}

//Legacy sources, newest first: dueltrack.db next to dueltracks.db, then the account data.db.
static int G_GetLegacyTrackedSources(const char *trackDbPath, char sources[2][MAX_OSPATH])
{
	char *slash;
	char *backslash;
	int count = 0;

	if (trackDbPath && trackDbPath[0])
	{
		Q_strncpyz(sources[count], trackDbPath, MAX_OSPATH);
		slash = strrchr(sources[count], '/');
		backslash = strrchr(sources[count], '\\');
		if (backslash && (!slash || backslash > slash))
			slash = backslash;
		if (slash)
		{
			slash[1] = '\0';
			Q_strcat(sources[count], MAX_OSPATH, TRACKED_IMPORT_LEGACY_FILE);
		}
		else
			Q_strncpyz(sources[count], TRACKED_IMPORT_LEGACY_FILE, MAX_OSPATH);
		if (Q_stricmp(sources[count], trackDbPath))
			count++;
	}
	if (LOCAL_DB_PATH[0] && (!trackDbPath || Q_stricmp(LOCAL_DB_PATH, trackDbPath)))
		Q_strncpyz(sources[count++], LOCAL_DB_PATH, MAX_OSPATH);
	return count;
}

static qboolean G_ImportLegacyTrackedSource(sqlite3 *db, const char *path)
{
	sqlite3_stmt *stmt = NULL;
	int duelSessions = 0, duelDuplicates = 0;
	int arcadeSessions = 0, arcadeDuplicates = 0;
	int participants = 0, events = 0, geometry = 0;
	qboolean ok = qtrue;
	int s;

	if (!G_CountLegacyTrackedSessions(path))
		return qtrue;
	if (G_IsTrackedImportDone(db, path))
	{
		trap->Print("importDuelTrack: \"%s\" was already imported, skipping.\n", path);
		return qtrue;
	}

	s = sqlite3_prepare_v2(db, "ATTACH DATABASE ? AS legacy", -1, &stmt, NULL);
	if (s == SQLITE_OK && stmt)
	{
		sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
		s = sqlite3_step(stmt);
	}
	if (stmt)
		sqlite3_finalize(stmt);
	if (s != SQLITE_DONE)
	{
		G_TrackedDBError(va("attach legacy duel tracking database \"%s\"", path), db, s);
		return qfalse;
	}

	ok = G_TrackedImportExec(db, "BEGIN TRANSACTION", "begin importDuelTrack transaction");
	if (ok)
	{
		ok = G_TrackedImportExec(db, "DROP TABLE IF EXISTS temp.dt_import_map", "drop import id map") &&
			G_TrackedImportExec(db, "CREATE TEMP TABLE dt_import_map(old_id INTEGER PRIMARY KEY, new_id INTEGER)", "create import id map") &&
			G_ImportLegacyDuelSummaries(db, &duelSessions, &duelDuplicates) &&
			G_ImportTrackedChildRows(db, "LocalDuelTrackParticipant", "summary_id", "LocalDuelTrackParticipant", &participants) &&
			G_ImportTrackedChildRows(db, "LocalDuelTrackEvent", "summary_id", "LocalDuelTrackEvent", &events) &&
			G_ImportTrackedChildRows(db, "LocalDuelTrackGeometry", "summary_id", "LocalDuelTrackGeometry", &geometry);

		//Aggregates can't be de-duplicated row by row, so only merge them when every legacy
		//duel was new; otherwise their counters would be added twice.
		if (ok && duelSessions > 0 && duelDuplicates == 0)
			ok = G_ImportTrackedAggregate(db);
		else if (ok && duelDuplicates > 0)
			trap->Print("importDuelTrack: %i duels in \"%s\" were already present; aggregates from it were not merged.\n",
				duelDuplicates, path);

		if (ok)
			ok = G_ImportLegacyArcadeSessions(db, &arcadeSessions, &arcadeDuplicates);

		if (ok)
		{
			stmt = NULL;
			s = sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO main.LocalDuelTrackImport(source, imported_at, sessions) VALUES (?, ?, ?)", -1, &stmt, NULL);
			if (s == SQLITE_OK && stmt)
			{
				sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
				sqlite3_bind_int64(stmt, 2, (sqlite3_int64)time(NULL));
				sqlite3_bind_int(stmt, 3, duelSessions + arcadeSessions);
				s = sqlite3_step(stmt);
			}
			if (stmt)
				sqlite3_finalize(stmt);
			if (s != SQLITE_DONE)
			{
				G_TrackedDBError("record importDuelTrack source", db, s);
				ok = qfalse;
			}
		}

		if (ok)
			ok = G_TrackedImportExec(db, "COMMIT", "commit importDuelTrack transaction");
		if (!ok)
			sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
	}

	sqlite3_exec(db, "DROP TABLE IF EXISTS temp.dt_import_map", NULL, NULL, NULL);
	sqlite3_exec(db, "DETACH DATABASE legacy", NULL, NULL, NULL);

	if (ok)
		trap->Print("importDuelTrack: imported %i duels (%i participants, %i events, %i geometry rows) and %i arcade runs from \"%s\"%s.\n",
			duelSessions, participants, events, geometry, arcadeSessions, path,
			(duelDuplicates + arcadeDuplicates) ? va(", skipped %i already present", duelDuplicates + arcadeDuplicates) : "");
	else
		trap->Print("importDuelTrack: import from \"%s\" failed and was rolled back; nothing was changed.\n", path);
	return ok;
}

//Startup hint so a server that upgraded from a dueltrack.db build knows its history is still there.
static void G_ReportLegacyTrackedData(sqlite3 *db, const char *trackDbPath)
{
	char sources[2][MAX_OSPATH];
	const int numSources = G_GetLegacyTrackedSources(trackDbPath, sources);
	int i;

	for (i = 0; i < numSources; i++)
	{
		const int sessions = G_CountLegacyTrackedSessions(sources[i]);
		if (sessions > 0 && !G_IsTrackedImportDone(db, sources[i]))
			trap->Print("Duel tracking: %i sessions from an older build are in \"%s\". Run importDuelTrack to merge them into %s.\n",
				sessions, sources[i], trackDbPath);
	}
}

void Svcmd_ImportDuelTrack_f(void)
{
	sqlite3 *db = NULL;
	char effectiveDbPath[MAX_OSPATH];
	char sources[2][MAX_OSPATH];
	int numSources;
	int i;
	int found = 0;

	if (!LOCAL_DUELTRACK_DB_PATH[0])
	{
		trap->Print("importDuelTrack failed: duel tracking database path is not initialized.\n");
		return;
	}
	if (!G_OpenTrackedLocalDB(&db, effectiveDbPath, sizeof(effectiveDbPath)))
	{
		trap->Print("importDuelTrack failed: unable to open the duel tracking database \"%s\".\n",
			LOCAL_DUELTRACK_DB_PATH);
		return;
	}

	G_EnsureLocalDuelTrackingSchema(db);
	G_EnsureTrackedImportTable(db);
	numSources = G_GetLegacyTrackedSources(effectiveDbPath, sources);
	for (i = 0; i < numSources; i++)
	{
		if (G_CountLegacyTrackedSessions(sources[i]) <= 0)
			continue;
		found++;
		G_ImportLegacyTrackedSource(db, sources[i]);
	}
	if (!found)
		trap->Print("importDuelTrack: no legacy duel tracking data found next to \"%s\".\n", effectiveDbPath);

	G_PrintTrackedDuelTableCounts(db, effectiveDbPath);
	sqlite3_close(db);
}

void Svcmd_ResetDuelTrack_f(void)
{
	sqlite3 *db = NULL;
	char effectiveDbPath[MAX_OSPATH];
	char sql[128];
	int i;
	int trackedRows = 0;
	qboolean transactionStarted = qfalse;
	qboolean committed = qfalse;
	qboolean success = qtrue;

	if (trap->Argc() != 1)
	{
		trap->Print("Usage: resetdueltrack\n");
		return;
	}

	if (!LOCAL_DUELTRACK_DB_PATH[0])
	{
		trap->Print("resetdueltrack failed: duel tracking database path is not initialized.\n");
		return;
	}

	if (!G_OpenTrackedLocalDB(&db, effectiveDbPath, sizeof(effectiveDbPath)))
	{
		trap->Print("resetdueltrack failed: unable to open the duel tracking database \"%s\".\n",
			LOCAL_DUELTRACK_DB_PATH);
		return;
	}

	G_EnsureLocalDuelTrackingSchema(db);
	for (i = 0; i < (int)ARRAY_LEN(g_trackedDuelTableNames); i++)
	{
		if (!G_DoesTrackedDuelTableExist(db, g_trackedDuelTableNames[i]))
		{
			trap->Print("resetdueltrack failed: duel tracking schema is unavailable in \"%s\" (missing %s).\n",
				effectiveDbPath, g_trackedDuelTableNames[i]);
			sqlite3_close(db);
			return;
		}
	}

	i = sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);
	if (i != SQLITE_OK)
	{
		G_TrackedDBError("begin resetdueltrack transaction", db, i);
		success = qfalse;
	}
	else
		transactionStarted = qtrue;

	for (i = 0; success && i < (int)ARRAY_LEN(g_trackedDuelTableNames); i++)
	{
		int s;
		Com_sprintf(sql, sizeof(sql), "DELETE FROM main.%s", g_trackedDuelTableNames[i]);
		s = sqlite3_exec(db, sql, NULL, NULL, NULL);
		if (s != SQLITE_OK)
		{
			G_TrackedDBError(va("clear %s", g_trackedDuelTableNames[i]), db, s);
			success = qfalse;
		}
		else
			trackedRows += sqlite3_changes(db);
	}

	//A reset starts a clean slate, so forget which legacy sources were imported; running
	//importDuelTrack afterwards can then bring them back without double counting.
	if (success && G_DoesTrackedDuelTableExist(db, "LocalDuelTrackImport"))
	{
		int s = sqlite3_exec(db, "DELETE FROM main.LocalDuelTrackImport", NULL, NULL, NULL);
		if (s != SQLITE_OK)
		{
			G_TrackedDBError("clear LocalDuelTrackImport", db, s);
			success = qfalse;
		}
	}

	//Only the tracking tables are targeted here; account, Elo, race and arcade score data
	//lives in other tables (and in data.db) and must never be touched by this command.
	if (success && G_DoesTrackedDuelTableExist(db, "sqlite_sequence"))
	{
		for (i = 0; success && i < (int)ARRAY_LEN(g_trackedDuelTableNames); i++)
		{
			int s;
			Com_sprintf(sql, sizeof(sql), "DELETE FROM main.sqlite_sequence WHERE name='%s'",
				g_trackedDuelTableNames[i]);
			s = sqlite3_exec(db, sql, NULL, NULL, NULL);
			if (s != SQLITE_OK)
			{
				G_TrackedDBError(va("reset rowid sequence for %s", g_trackedDuelTableNames[i]), db, s);
				success = qfalse;
			}
		}
	}

	if (success)
	{
		int s = sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
		if (s == SQLITE_OK)
		{
			transactionStarted = qfalse;
			committed = qtrue;
		}
		else
		{
			G_TrackedDBError("commit resetdueltrack transaction", db, s);
			success = qfalse;
		}
	}

	if (transactionStarted)
		sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);

	if (committed)
	{
		//VACUUM has to run outside a transaction; it shrinks the file so the reset really
		//does leave a clean tracking database behind.
		const int s = sqlite3_exec(db, "VACUUM", NULL, NULL, NULL);
		if (s != SQLITE_OK)
			G_TrackedDBError("vacuum duel tracking database", db, s);
		G_EnsureLocalDuelTrackingSchema(db);
		for (i = 0; i < MAX_CLIENTS; i++)
		{
			G_ClearTrackedDuelRuntime(i);
			G_ClearTrackedArcadeCombat(i);
		}
		memset(g_duelAdviceSessions, 0, sizeof(g_duelAdviceSessions));
	}

	if (success)
	{
		trap->Print("resetdueltrack: cleared %i tracking rows in \"%s\"; account, Elo, arcade, and legacy data were not changed.\n",
			trackedRows, effectiveDbPath);
		G_PrintTrackedDuelTableCounts(db, effectiveDbPath);
	}
	else if (committed)
	{
		trap->Print("resetdueltrack: cleared %i tracking rows, but database cleanup failed for \"%s\".\n",
			trackedRows, effectiveDbPath);
		G_PrintTrackedDuelTableCounts(db, effectiveDbPath);
	}
	else
	{
		trap->Print("resetdueltrack failed: unable to clear duel tracking data in \"%s\"; no account, Elo, arcade, or legacy tables were targeted.\n",
			effectiveDbPath);
	}

	sqlite3_close(db);
}

void Svcmd_ExportDuelTrack_f(void)
{
	sqlite3 *db;
	char dbDir[MAX_OSPATH];
	char effectiveDbPath[MAX_OSPATH];
	char optionalPrefix[64];
	char safePrefix[64];
	char *slashPos;
	char *backslashPos;
	int i;
	char pathSep;
	qboolean preHadDuelSummary;
	qboolean preHadDuelParticipant;
	qboolean preHadDuelEvent;
	qboolean preHadDuelGeometry;
	qboolean preHadDuelAggregate;
	qboolean hadAnyTrackedTables;
	qboolean wantSessionExport;
	qboolean wantParticipantExport;
	qboolean wantEventExport;
	qboolean wantGeometryExport;
	qboolean wantAggregateExport;
	char exportSuffix[32];
	qboolean timestampedExport;

	optionalPrefix[0] = '\0';
	timestampedExport = qfalse;
	if (trap->Argc() >= 2)
	{
		char token[64];
		size_t curLen;
		size_t tokenLen;
		size_t needed;
		for (i = 1; i < trap->Argc(); i++)
		{
			trap->Argv(i, token, sizeof(token));
			if (!Q_stricmp(token, "-timestamped"))
			{
				timestampedExport = qtrue;
				continue;
			}
			if (!Q_stricmp(token, "-stable"))
			{
				timestampedExport = qfalse;
				continue;
			}
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

	if (!LOCAL_DUELTRACK_DB_PATH[0])
	{
		trap->Print("Duel tracking export unavailable: tracking database path is not initialized yet.\n");
		return;
	}

	if (!G_OpenTrackedLocalDB(&db, effectiveDbPath, sizeof(effectiveDbPath)))
	{
		trap->Print("exportDuelTrack failed: unable to open local duel database.\n");
		return;
	}
	preHadDuelSummary = G_DoesTrackedDuelTableExist(db, "LocalDuelTrackSummary");
	preHadDuelParticipant = G_DoesTrackedDuelTableExist(db, "LocalDuelTrackParticipant");
	preHadDuelEvent = G_DoesTrackedDuelTableExist(db, "LocalDuelTrackEvent");
	preHadDuelGeometry = G_DoesTrackedDuelTableExist(db, "LocalDuelTrackGeometry");
	preHadDuelAggregate = G_DoesTrackedDuelTableExist(db, "LocalDuelTrackAggregate");
	G_EnsureLocalDuelTrackingSchema(db);
	hadAnyTrackedTables = preHadDuelSummary || preHadDuelParticipant || preHadDuelEvent ||
		preHadDuelGeometry || preHadDuelAggregate;
	if (hadAnyTrackedTables)
	{
		wantSessionExport = preHadDuelSummary;
		wantParticipantExport = preHadDuelParticipant;
		wantEventExport = preHadDuelEvent;
		wantGeometryExport = preHadDuelGeometry;
		wantAggregateExport = preHadDuelAggregate;
	}
	else
	{
		wantSessionExport = G_DoesTrackedDuelTableExist(db, "LocalDuelTrackSummary");
		wantParticipantExport = G_DoesTrackedDuelTableExist(db, "LocalDuelTrackParticipant");
		wantEventExport = G_DoesTrackedDuelTableExist(db, "LocalDuelTrackEvent");
		wantGeometryExport = G_DoesTrackedDuelTableExist(db, "LocalDuelTrackGeometry");
		wantAggregateExport = G_DoesTrackedDuelTableExist(db, "LocalDuelTrackAggregate");
	}

	if (wantSessionExport && !G_TrackedTableHasAllColumns(db, "LocalDuelTrackSummary",
		g_trackedDuelSummaryColumns, ARRAY_LEN(g_trackedDuelSummaryColumns)))
	{
		wantSessionExport = qfalse;
	}
	if (wantEventExport && !G_TrackedTableHasAllColumns(db, "LocalDuelTrackEvent",
		g_trackedDuelEventColumns, ARRAY_LEN(g_trackedDuelEventColumns)))
	{
		wantEventExport = qfalse;
	}
	if (wantGeometryExport && !G_TrackedTableHasAllColumns(db, "LocalDuelTrackGeometry",
		g_trackedDuelGeometryColumns, ARRAY_LEN(g_trackedDuelGeometryColumns)))
	{
		wantGeometryExport = qfalse;
	}

	G_BuildTrackedExportSuffix(timestampedExport, exportSuffix, sizeof(exportSuffix));

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

	//Never delete an existing CSV up front: G_ExportTrackedQueryCSV only truncates the file
	//once its query has prepared, so a failed export leaves the previous good file in place.
	G_ExportTrackedTable(db, wantSessionExport, G_GetTrackedSessionExportQuery(), "sessions",
		dbDir, pathSep, safePrefix, exportSuffix);
	G_ExportTrackedTable(db, wantParticipantExport, G_GetTrackedParticipantExportQuery(), "participants",
		dbDir, pathSep, safePrefix, exportSuffix);
	G_ExportTrackedTable(db, wantEventExport, G_GetTrackedEventExportQuery(), "events",
		dbDir, pathSep, safePrefix, exportSuffix);
	G_ExportTrackedTable(db, wantGeometryExport, G_GetTrackedGeometryExportQuery(), "geometry",
		dbDir, pathSep, safePrefix, exportSuffix);
	G_ExportTrackedTable(db, wantAggregateExport, G_GetTrackedAggregateExportQuery(), "aggregate",
		dbDir, pathSep, safePrefix, exportSuffix);
	G_ExportTrackedTable(db, G_DoesTrackedDuelTableExist(db, "LocalBotLearnedSequence"),
		G_GetTrackedLearnedExportQuery(), "learned", dbDir, pathSep, safePrefix, exportSuffix);

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
			// Keep any future combat-rank query in sync with the active duel-top query above.
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
	char effectiveDuelTrackPath[MAX_OSPATH];
	int s;

	for (s = 0; s < MAX_CLIENTS; s++)
	{
		G_ClearTrackedDuelRuntime(s);
		G_ClearTrackedArcadeCombat(s);
	}
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
		Com_sprintf(LOCAL_DUELTRACK_DB_PATH, sizeof(LOCAL_DUELTRACK_DB_PATH), "%s/%s/dueltracks.db", fs_homepath, fs_game);
	} else {
		Com_sprintf(LOCAL_DB_PATH, sizeof(LOCAL_DB_PATH), "%s/data.db", fs_game);
		Com_sprintf(LOCAL_DUELTRACK_DB_PATH, sizeof(LOCAL_DUELTRACK_DB_PATH), "%s/dueltracks.db", fs_game);
	}
	g_duelTrackingSchemaReady = qfalse;
	g_duelTrackingSchemaPath[0] = '\0';

	if (!G_OpenLocalAccountDB(&db))
	{
		trap->Print("ERROR: account database unavailable, accounts/elo will not be saved this map.\n");
		return;
	}
	trap->Print("Account database: %s\n", LOCAL_DB_PATH);

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

	G_FoldBotLadderKeysToLevels(db);

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

	db = NULL;
	if (G_OpenTrackedLocalDB(&db, effectiveDuelTrackPath, sizeof(effectiveDuelTrackPath)))
	{
		G_EnsureLocalDuelTrackingSchema(db);
		trap->Print("Duel tracking database: %s\n", effectiveDuelTrackPath);
		G_ReportLegacyTrackedData(db, effectiveDuelTrackPath);
		G_BotLearnLoadCache(db);
		CALL_SQLITE(close(db));
	}
	else
	{
		trap->Print("ERROR: could not open dedicated duel tracking database \"%s\", duel tracking is disabled.\n",
			LOCAL_DUELTRACK_DB_PATH);
		G_ErrorPrint("ERROR: could not open dedicated duel tracking database", SQLITE_CANTOPEN);
		if (db)
			sqlite3_close(db);
	}

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
