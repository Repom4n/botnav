/*
 * g_newbotai.h - New Bot AI system header
 *
 * Integrates waypoint-based navigation with improved AI logic,
 * stuck-detection with yaw correction, random strafing for
 * human-like behaviour, reaction-time delay, and difficulty levels.
 */

#ifndef G_NEWBOTAI_H
#define G_NEWBOTAI_H

/* ---------------------------------------------------------------
 * Tuneable constants
 * --------------------------------------------------------------- */

/* Maximum number of waypoints per map */
#define MAX_WAYPOINTS       1024

/* Radius within which a bot considers a waypoint "reached" */
#define WAYPOINT_REACH_DIST 48.0f

/* How many seconds before the bot is considered "stuck" */
#define BOT_STUCK_TIME      2.5f

/* Minimum / maximum random yaw adjustment (degrees) when stuck */
#define BOT_YAW_ADJUST_MIN  30.0f
#define BOT_YAW_ADJUST_MAX  150.0f

/* Probability (0-100) per think-frame of starting a strafe move */
#define BOT_STRAFE_CHANCE   15

/* Duration of a strafe movement (seconds) */
#define BOT_STRAFE_DURATION 0.6f

/* ---------------------------------------------------------------
 * Difficulty levels
 * --------------------------------------------------------------- */
typedef enum {
    BOT_DIFFICULTY_EASY   = 0,
    BOT_DIFFICULTY_MEDIUM = 1,
    BOT_DIFFICULTY_HARD   = 2,
    BOT_DIFFICULTY_EXPERT = 3
} BotDifficulty;

/* Per-difficulty reaction-time multipliers (applied on top of the
 * bot_reactiontime cvar value). */
static const float BOT_DIFFICULTY_REACTION_MULT[4] = {
    2.0f,   /* EASY   – slowest reactions  */
    1.25f,  /* MEDIUM                       */
    0.75f,  /* HARD                         */
    0.35f   /* EXPERT – fastest reactions   */
};

/* Per-difficulty aim-accuracy modifiers (fraction of perfect aim). */
static const float BOT_DIFFICULTY_ACCURACY[4] = {
    0.30f,  /* EASY   */
    0.55f,  /* MEDIUM */
    0.80f,  /* HARD   */
    0.98f   /* EXPERT */
};

/* ---------------------------------------------------------------
 * Waypoint
 * --------------------------------------------------------------- */
typedef struct {
    float   origin[3];      /* World-space position              */
    int     links[8];       /* Indices of connected waypoints    */
    int     numLinks;       /* Number of valid links             */
    int     flags;          /* Reserved for future use           */
} Waypoint;

/* ---------------------------------------------------------------
 * Bot navigation state
 * --------------------------------------------------------------- */
typedef struct {
    /* --- Waypoint path --- */
    int     waypointPath[MAX_WAYPOINTS]; /* Planned path (indices)   */
    int     pathLength;                  /* Number of steps in path  */
    int     pathIndex;                   /* Current step             */
    int     nearestWaypoint;             /* Closest wp to bot origin */

    /* --- Stuck detection --- */
    float   lastOrigin[3];   /* Position at last stuck-check snapshot */
    float   stuckTimer;      /* Seconds without meaningful movement   */
    float   yawAdjust;       /* Current corrective yaw offset         */
    int     stuckCount;      /* How many times stuck in a row         */

    /* --- Strafing --- */
    int     strafing;        /* Non-zero while strafing               */
    float   strafeTimer;     /* Remaining strafe duration (seconds)   */
    int     strafeDir;       /* +1 = right, -1 = left                 */

    /* --- Reaction delay --- */
    float   reactionAccum;   /* Accumulated time since last "think"   */
    float   reactionDelay;   /* Effective delay for this bot (seconds)*/

    /* --- Difficulty --- */
    BotDifficulty difficulty;
} BotNavState;

/* ---------------------------------------------------------------
 * Cvar declarations (defined in g_newbotai.c)
 * --------------------------------------------------------------- */
extern float cvar_bot_reactiontime;   /* Base reaction time in seconds */
extern int   cvar_bot_difficulty;     /* 0-3 difficulty level          */
extern int   cvar_bot_debug;          /* Non-zero enables debug output  */

/* ---------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------- */

/* Initialise / reset the navigation state for a bot. */
void BotNav_Init(BotNavState *nav, BotDifficulty difficulty);

/* Add a waypoint to the global waypoint table.
 * Returns the index of the new waypoint, or -1 on error. */
int  BotNav_AddWaypoint(float x, float y, float z);

/* Connect two waypoints bi-directionally. */
void BotNav_LinkWaypoints(int a, int b);

/* Clear all waypoints (e.g. on map change). */
void BotNav_ClearWaypoints(void);

/* Find the index of the waypoint nearest to an origin. */
int  BotNav_NearestWaypoint(const float origin[3]);

/* Build (BFS) a path from startWp to goalWp.
 * Returns 1 on success, 0 if no path found. */
int  BotNav_BuildPath(BotNavState *nav, int startWp, int goalWp);

/* Main think function – call every server frame for each bot.
 * 'deltaTime'  – seconds elapsed since last call.
 * 'botOrigin'  – current position of the bot.
 * 'goalOrigin' – position the bot wants to reach.
 * 'outMove'    – output: desired movement direction (unit vector, XY).
 * 'outYaw'     – output: desired yaw angle (degrees).
 * Returns 1 if the bot is ready to act this frame, 0 if still in
 * reaction delay. */
int  BotNav_Think(BotNavState *nav,
                  float deltaTime,
                  const float botOrigin[3],
                  const float goalOrigin[3],
                  float outMove[3],
                  float *outYaw);

#endif /* G_NEWBOTAI_H */
