/*
 * g_newbotai.c - New Bot AI implementation
 *
 * Features:
 *  - Waypoint-based navigation (BFS pathfinding)
 *  - Stuck detection with random yaw correction
 *  - Random strafing for human-like movement
 *  - Reaction-time delay cvar (bot_reactiontime)
 *  - Difficulty cvar (bot_difficulty 0-3)
 */

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "g_newbotai.h"

/* ---------------------------------------------------------------
 * Cvars (global, set externally by the engine or console)
 * --------------------------------------------------------------- */
float cvar_bot_reactiontime = 0.25f; /* default: 250 ms base delay */
int   cvar_bot_difficulty   = BOT_DIFFICULTY_MEDIUM;
int   cvar_bot_debug        = 0;

/* ---------------------------------------------------------------
 * Waypoint storage
 * --------------------------------------------------------------- */
static Waypoint g_waypoints[MAX_WAYPOINTS];
static int      g_numWaypoints = 0;

/* ---------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------- */

static float VecDist(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

static float VecLen(const float v[3])
{
    return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

static void VecNorm(float v[3])
{
    float len = VecLen(v);
    if (len > 0.0001f) {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

/* Random float in [lo, hi] */
static float RandFloat(float lo, float hi)
{
    return lo + ((float)rand() / (float)RAND_MAX) * (hi - lo);
}

/* Random int in [0, range) */
static int RandInt(int range)
{
    if (range <= 0) return 0;
    return rand() % range;
}

/* Convert a direction vector to a yaw angle in degrees */
static float VecToYaw(const float v[3])
{
    float yaw;
    if (v[0] == 0.0f && v[1] == 0.0f)
        return 0.0f;
    yaw = (float)(atan2f(v[1], v[0]) * (180.0f / 3.14159265358979f));
    if (yaw < 0.0f)
        yaw += 360.0f;
    return yaw;
}

/* ---------------------------------------------------------------
 * Waypoint API
 * --------------------------------------------------------------- */

void BotNav_ClearWaypoints(void)
{
    g_numWaypoints = 0;
    memset(g_waypoints, 0, sizeof(g_waypoints));
}

int BotNav_AddWaypoint(float x, float y, float z)
{
    Waypoint *wp;
    if (g_numWaypoints >= MAX_WAYPOINTS) {
        if (cvar_bot_debug)
            printf("[BotNav] WARNING: waypoint table full\n");
        return -1;
    }
    wp = &g_waypoints[g_numWaypoints];
    wp->origin[0] = x;
    wp->origin[1] = y;
    wp->origin[2] = z;
    wp->numLinks  = 0;
    wp->flags     = 0;
    return g_numWaypoints++;
}

void BotNav_LinkWaypoints(int a, int b)
{
    int i;
    Waypoint *wa, *wb;

    if (a < 0 || a >= g_numWaypoints) return;
    if (b < 0 || b >= g_numWaypoints) return;

    wa = &g_waypoints[a];
    wb = &g_waypoints[b];

    /* Add b -> a */
    if (wa->numLinks < 8) {
        for (i = 0; i < wa->numLinks; i++)
            if (wa->links[i] == b) goto skip_ab;
        wa->links[wa->numLinks++] = b;
    }
skip_ab:
    /* Add a -> b */
    if (wb->numLinks < 8) {
        for (i = 0; i < wb->numLinks; i++)
            if (wb->links[i] == a) goto skip_ba;
        wb->links[wb->numLinks++] = a;
    }
skip_ba:;
}

int BotNav_NearestWaypoint(const float origin[3])
{
    int   i, best = -1;
    float bestDist = 1e18f, d;

    for (i = 0; i < g_numWaypoints; i++) {
        d = VecDist(origin, g_waypoints[i].origin);
        if (d < bestDist) {
            bestDist = d;
            best     = i;
        }
    }
    return best;
}

/* ---------------------------------------------------------------
 * BFS pathfinding
 * --------------------------------------------------------------- */

int BotNav_BuildPath(BotNavState *nav, int startWp, int goalWp)
{
    /* BFS queue */
    int   queue[MAX_WAYPOINTS];
    int   prev[MAX_WAYPOINTS];
    int   visited[MAX_WAYPOINTS];
    int   head = 0, tail = 0;
    int   cur, i, len;

    if (startWp < 0 || startWp >= g_numWaypoints) return 0;
    if (goalWp  < 0 || goalWp  >= g_numWaypoints) return 0;
    if (startWp == goalWp) {
        nav->pathLength = 0;
        nav->pathIndex  = 0;
        return 1;
    }

    memset(visited, 0, sizeof(int) * g_numWaypoints);
    memset(prev,   -1, sizeof(int) * g_numWaypoints);

    queue[tail++] = startWp;
    visited[startWp] = 1;

    while (head < tail) {
        cur = queue[head++];
        if (cur == goalWp) goto found;
        for (i = 0; i < g_waypoints[cur].numLinks; i++) {
            int nb = g_waypoints[cur].links[i];
            if (!visited[nb]) {
                visited[nb] = 1;
                prev[nb] = cur;
                queue[tail++] = nb;
            }
        }
    }
    /* No path found */
    nav->pathLength = 0;
    nav->pathIndex  = 0;
    return 0;

found:
    /* Reconstruct path */
    len = 0;
    cur = goalWp;
    while (cur != -1 && len < MAX_WAYPOINTS) {
        nav->waypointPath[len++] = cur;
        cur = prev[cur];
    }
    /* Reverse so index 0 is start */
    for (i = 0; i < len / 2; i++) {
        int tmp = nav->waypointPath[i];
        nav->waypointPath[i] = nav->waypointPath[len - 1 - i];
        nav->waypointPath[len - 1 - i] = tmp;
    }
    nav->pathLength = len;
    nav->pathIndex  = 0;

    if (cvar_bot_debug)
        printf("[BotNav] Path built: %d -> %d (%d steps)\n",
               startWp, goalWp, len);
    return 1;
}

/* ---------------------------------------------------------------
 * Initialisation
 * --------------------------------------------------------------- */

void BotNav_Init(BotNavState *nav, BotDifficulty difficulty)
{
    memset(nav, 0, sizeof(*nav));
    nav->difficulty      = difficulty;
    nav->nearestWaypoint = -1;
    nav->pathLength      = 0;
    nav->pathIndex       = 0;
    nav->stuckTimer      = 0.0f;
    nav->stuckCount      = 0;
    nav->yawAdjust       = 0.0f;
    nav->strafing        = 0;
    nav->strafeTimer     = 0.0f;
    nav->strafeDir       = 1;
    nav->reactionAccum   = 0.0f;

    /* Effective reaction delay = base cvar × difficulty multiplier */
    nav->reactionDelay =
        cvar_bot_reactiontime *
        BOT_DIFFICULTY_REACTION_MULT[(int)difficulty];

    if (cvar_bot_debug)
        printf("[BotNav] Init: difficulty=%d reactionDelay=%.3fs\n",
               (int)difficulty, nav->reactionDelay);
}

/* ---------------------------------------------------------------
 * Main think function
 * --------------------------------------------------------------- */

int BotNav_Think(BotNavState *nav,
                 float        deltaTime,
                 const float  botOrigin[3],
                 const float  goalOrigin[3],
                 float        outMove[3],
                 float       *outYaw)
{
    float  dir[3];
    float  desiredYaw;
    int    wpIdx;
    float  distToWp;

    /* Default: no movement */
    outMove[0] = outMove[1] = outMove[2] = 0.0f;
    *outYaw    = 0.0f;

    /* --- Reaction-time delay ---------------------------------------- */
    nav->reactionAccum += deltaTime;
    if (nav->reactionAccum < nav->reactionDelay)
        return 0; /* Not yet ready to act */
    nav->reactionAccum -= nav->reactionDelay;

    /* --- Stuck detection -------------------------------------------- */
    {
        float moved = VecDist(botOrigin, nav->lastOrigin);
        if (moved < 8.0f) {
            nav->stuckTimer += deltaTime;
        } else {
            nav->stuckTimer = 0.0f;
            nav->stuckCount = 0;
            nav->lastOrigin[0] = botOrigin[0];
            nav->lastOrigin[1] = botOrigin[1];
            nav->lastOrigin[2] = botOrigin[2];
        }

        if (nav->stuckTimer >= BOT_STUCK_TIME) {
            float adjust = RandFloat(BOT_YAW_ADJUST_MIN, BOT_YAW_ADJUST_MAX);
            /* Alternate left / right each time */
            if (nav->stuckCount % 2 == 0)
                nav->yawAdjust =  adjust;
            else
                nav->yawAdjust = -adjust;

            nav->stuckTimer = 0.0f;
            nav->stuckCount++;
            /* Invalidate current path so it is rebuilt */
            nav->pathLength = 0;
            nav->pathIndex  = 0;

            if (cvar_bot_debug)
                printf("[BotNav] Bot stuck (count=%d), yaw adjust=%.1f\n",
                       nav->stuckCount, nav->yawAdjust);
        } else {
            /* Decay yaw adjustment once the bot is moving again */
            if (nav->yawAdjust > 0.0f)      nav->yawAdjust -= deltaTime * 45.0f;
            if (nav->yawAdjust < 0.0f)      nav->yawAdjust += deltaTime * 45.0f;
            if (fabsf(nav->yawAdjust) < 1.0f) nav->yawAdjust = 0.0f;
        }
    }

    /* --- Waypoint navigation ---------------------------------------- */
    if (g_numWaypoints > 0) {
        /* Find nearest wp to bot if we don't have a path */
        nav->nearestWaypoint = BotNav_NearestWaypoint(botOrigin);

        if (nav->pathLength == 0 || nav->pathIndex >= nav->pathLength) {
            /* Need to (re)build a path toward the goal */
            int goalWp = BotNav_NearestWaypoint(goalOrigin);
            BotNav_BuildPath(nav, nav->nearestWaypoint, goalWp);
        }

        /* Advance along the path */
        if (nav->pathIndex < nav->pathLength) {
            wpIdx   = nav->waypointPath[nav->pathIndex];
            distToWp = VecDist(botOrigin, g_waypoints[wpIdx].origin);

            if (distToWp < WAYPOINT_REACH_DIST) {
                nav->pathIndex++;
                if (nav->pathIndex >= nav->pathLength) {
                    /* Path complete */
                    nav->pathLength = 0;
                }
            }

            /* Direction toward current waypoint */
            if (nav->pathIndex < nav->pathLength) {
                int nextWp = nav->waypointPath[nav->pathIndex];
                dir[0] = g_waypoints[nextWp].origin[0] - botOrigin[0];
                dir[1] = g_waypoints[nextWp].origin[1] - botOrigin[1];
                dir[2] = g_waypoints[nextWp].origin[2] - botOrigin[2];
            } else {
                dir[0] = goalOrigin[0] - botOrigin[0];
                dir[1] = goalOrigin[1] - botOrigin[1];
                dir[2] = goalOrigin[2] - botOrigin[2];
            }
        } else {
            /* Fall through to direct movement */
            dir[0] = goalOrigin[0] - botOrigin[0];
            dir[1] = goalOrigin[1] - botOrigin[1];
            dir[2] = goalOrigin[2] - botOrigin[2];
        }
    } else {
        /* No waypoints: move directly toward goal */
        dir[0] = goalOrigin[0] - botOrigin[0];
        dir[1] = goalOrigin[1] - botOrigin[1];
        dir[2] = goalOrigin[2] - botOrigin[2];
    }

    VecNorm(dir);
    desiredYaw = VecToYaw(dir);

    /* Apply stuck yaw adjustment */
    desiredYaw += nav->yawAdjust;
    if (desiredYaw >= 360.0f) desiredYaw -= 360.0f;
    if (desiredYaw <    0.0f) desiredYaw += 360.0f;

    /* --- Random strafing -------------------------------------------- */
    if (!nav->strafing) {
        /* Randomly decide to start strafing */
        if (RandInt(100) < BOT_STRAFE_CHANCE) {
            nav->strafing    = 1;
            nav->strafeTimer = BOT_STRAFE_DURATION;
            nav->strafeDir   = (RandInt(2) == 0) ? 1 : -1;

            if (cvar_bot_debug)
                printf("[BotNav] Starting strafe dir=%d\n", nav->strafeDir);
        }
    } else {
        nav->strafeTimer -= deltaTime;
        if (nav->strafeTimer <= 0.0f)
            nav->strafing = 0;
    }

    /* Output */
    if (nav->strafing) {
        /* Strafe: move perpendicular to the desired yaw */
        float yawRad = desiredYaw * (3.14159265358979f / 180.0f);
        outMove[0] = -sinf(yawRad) * (float)nav->strafeDir; /* right vector */
        outMove[1] =  cosf(yawRad) * (float)nav->strafeDir;
        outMove[2] = 0.0f;
    } else {
        outMove[0] = dir[0];
        outMove[1] = dir[1];
        outMove[2] = dir[2];
    }

    *outYaw = desiredYaw;
    return 1;
}
