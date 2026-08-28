/*
 * test_botnav.c – Unit tests for g_newbotai
 *
 * Compile & run:
 *   gcc -Wall -o test_botnav test_botnav.c g_newbotai.c -lm && ./test_botnav
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "g_newbotai.h"

#define PASS(msg) printf("[PASS] %s\n", msg)
#define FAIL(msg) do { printf("[FAIL] %s\n", msg); failures++; } while(0)

static int failures = 0;

/* ---------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------- */
static float fabs_f(float f) { return f < 0.0f ? -f : f; }

/* ---------------------------------------------------------------
 * Test 1: Waypoint add / link / nearest
 * --------------------------------------------------------------- */
static void test_waypoints(void)
{
    int a, b, c, n;
    float origin[3];

    BotNav_ClearWaypoints();

    a = BotNav_AddWaypoint(0.0f,   0.0f,  0.0f);
    b = BotNav_AddWaypoint(100.0f, 0.0f,  0.0f);
    c = BotNav_AddWaypoint(100.0f, 100.0f, 0.0f);

    if (a == 0 && b == 1 && c == 2)
        PASS("AddWaypoint returns correct indices");
    else
        FAIL("AddWaypoint returns correct indices");

    BotNav_LinkWaypoints(a, b);
    BotNav_LinkWaypoints(b, c);

    /* Nearest to (90, 10, 0) should be b (100,0,0) dist≈14 vs c (100,100) dist≈90 */
    origin[0] = 90.0f; origin[1] = 10.0f; origin[2] = 0.0f;
    n = BotNav_NearestWaypoint(origin);
    if (n == b)
        PASS("NearestWaypoint correct");
    else
        FAIL("NearestWaypoint correct");
}

/* ---------------------------------------------------------------
 * Test 2: BFS pathfinding
 * --------------------------------------------------------------- */
static void test_pathfinding(void)
{
    int a, b, c, d;
    BotNavState nav;
    int ok;

    BotNav_ClearWaypoints();

    a = BotNav_AddWaypoint(0,   0,   0);
    b = BotNav_AddWaypoint(100, 0,   0);
    c = BotNav_AddWaypoint(200, 0,   0);
    d = BotNav_AddWaypoint(300, 0,   0);

    BotNav_LinkWaypoints(a, b);
    BotNav_LinkWaypoints(b, c);
    BotNav_LinkWaypoints(c, d);

    BotNav_Init(&nav, BOT_DIFFICULTY_MEDIUM);
    ok = BotNav_BuildPath(&nav, a, d);

    if (ok)
        PASS("BuildPath succeeds on linear chain");
    else
        FAIL("BuildPath succeeds on linear chain");

    if (nav.pathLength == 4 &&
        nav.waypointPath[0] == a &&
        nav.waypointPath[3] == d)
        PASS("Path contains correct waypoints");
    else
        FAIL("Path contains correct waypoints");

    /* Disconnected waypoint – no path should be found */
    {
        int iso = BotNav_AddWaypoint(9999, 9999, 9999); /* isolated */
        ok = BotNav_BuildPath(&nav, a, iso);
        if (!ok)
            PASS("BuildPath returns 0 for unreachable waypoint");
        else
            FAIL("BuildPath returns 0 for unreachable waypoint");
    }
}

/* ---------------------------------------------------------------
 * Test 3: Reaction delay
 * --------------------------------------------------------------- */
static void test_reaction_delay(void)
{
    BotNavState nav;
    float move[3], yaw;
    int   ready;
    float botOrigin[3]  = {0, 0, 0};
    float goalOrigin[3] = {500, 0, 0};

    /* Use known cvar values */
    cvar_bot_reactiontime = 0.5f;
    cvar_bot_difficulty   = BOT_DIFFICULTY_EASY;

    BotNav_Init(&nav, BOT_DIFFICULTY_EASY);
    /* EASY multiplier = 2.0, so delay = 0.5 * 2.0 = 1.0 s */

    /* Advance only 0.4 s – should NOT be ready */
    ready = BotNav_Think(&nav, 0.4f, botOrigin, goalOrigin, move, &yaw);
    if (!ready)
        PASS("Reaction delay: not ready before delay expires");
    else
        FAIL("Reaction delay: not ready before delay expires");

    /* Advance another 0.7 s (total 1.1 s) – should be ready now */
    ready = BotNav_Think(&nav, 0.7f, botOrigin, goalOrigin, move, &yaw);
    if (ready)
        PASS("Reaction delay: ready after delay expires");
    else
        FAIL("Reaction delay: ready after delay expires");

    /* Reset */
    cvar_bot_reactiontime = 0.25f;
    cvar_bot_difficulty   = BOT_DIFFICULTY_MEDIUM;
}

/* ---------------------------------------------------------------
 * Test 4: Think returns valid yaw and movement direction
 * --------------------------------------------------------------- */
static void test_think_output(void)
{
    BotNavState nav;
    float move[3], yaw;
    int   i, ready = 0;
    float botOrigin[3]  = {0,   0, 0};
    float goalOrigin[3] = {200, 0, 0};

    BotNav_ClearWaypoints();
    cvar_bot_reactiontime = 0.0f; /* No delay for this test */

    BotNav_Init(&nav, BOT_DIFFICULTY_HARD);

    /* Run a few frames to get a ready result */
    for (i = 0; i < 10 && !ready; i++)
        ready = BotNav_Think(&nav, 0.1f, botOrigin, goalOrigin, move, &yaw);

    if (ready)
        PASS("Think produces a ready frame");
    else
        FAIL("Think produces a ready frame");

    /* Yaw should be near 0° (east) since goal is along +X */
    if (fabs_f(yaw) < 10.0f || fabs_f(yaw - 360.0f) < 10.0f)
        PASS("Think: yaw roughly correct for eastward goal");
    else
        FAIL("Think: yaw roughly correct for eastward goal");

    /* Movement vector should be roughly (1,0,0) */
    if (fabs_f(move[0] - 1.0f) < 0.01f && fabs_f(move[1]) < 0.01f)
        PASS("Think: movement vector correct for eastward goal");
    else
        FAIL("Think: movement vector correct for eastward goal");

    cvar_bot_reactiontime = 0.25f;
}

/* ---------------------------------------------------------------
 * Test 5: Think with waypoints follows path
 * --------------------------------------------------------------- */
static void test_think_with_waypoints(void)
{
    BotNavState nav;
    float move[3], yaw;
    int   i, a, b;
    float botOrigin[3]  = {0,   0, 0};
    float goalOrigin[3] = {200, 0, 0};

    BotNav_ClearWaypoints();
    a = BotNav_AddWaypoint(0,   0, 0);
    b = BotNav_AddWaypoint(200, 0, 0);
    BotNav_LinkWaypoints(a, b);

    cvar_bot_reactiontime = 0.0f;
    BotNav_Init(&nav, BOT_DIFFICULTY_HARD);

    for (i = 0; i < 5; i++)
        BotNav_Think(&nav, 0.1f, botOrigin, goalOrigin, move, &yaw);

    if (nav.pathLength > 0 || nav.pathIndex >= 0)
        PASS("Think with waypoints: path state initialised");
    else
        FAIL("Think with waypoints: path state initialised");

    cvar_bot_reactiontime = 0.25f;
}

/* ---------------------------------------------------------------
 * Test 6: Stuck detection causes yaw adjustment
 * --------------------------------------------------------------- */
static void test_stuck_detection(void)
{
    BotNavState nav;
    float move[3], yaw;
    int   i;
    float botOrigin[3]  = {0, 0, 0};  /* bot does NOT move */
    float goalOrigin[3] = {500, 0, 0};

    BotNav_ClearWaypoints();
    cvar_bot_reactiontime = 0.0f;
    BotNav_Init(&nav, BOT_DIFFICULTY_HARD);

    /* Simulate 3 seconds of no movement (stuck) */
    for (i = 0; i < 30; i++)
        BotNav_Think(&nav, 0.1f, botOrigin, goalOrigin, move, &yaw);

    if (nav.stuckCount > 0)
        PASS("Stuck detection: stuckCount incremented");
    else
        FAIL("Stuck detection: stuckCount incremented");

    cvar_bot_reactiontime = 0.25f;
}

/* ---------------------------------------------------------------
 * Main
 * --------------------------------------------------------------- */
int main(void)
{
    printf("=== BotNav Tests ===\n\n");

    test_waypoints();
    test_pathfinding();
    test_reaction_delay();
    test_think_output();
    test_think_with_waypoints();
    test_stuck_detection();

    printf("\n=== Results: %d failure(s) ===\n", failures);
    return failures ? 1 : 0;
}
