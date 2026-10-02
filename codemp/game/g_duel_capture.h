#ifndef G_DUEL_CAPTURE_H
#define G_DUEL_CAPTURE_H

#include <string.h>

typedef struct
{
	int active;
	int chainPending;
	int move;
	int startTime;
	float lastYaw;
	float sweep;
	int preStrafe;
} duel_capture_swing_t;

typedef enum
{
	DUEL_CAPTURE_OUTCOME_OTHER = 0,
	DUEL_CAPTURE_OUTCOME_DEALT,
	DUEL_CAPTURE_OUTCOME_TAKEN
} duel_capture_outcome_kind_t;

typedef struct
{
	int dealt;
	int taken;
	int killedEnemy;
} duel_capture_outcome_t;

static inline int G_DuelCaptureCanObserveInputs(int health, int deathSnapshot)
{
	return health > 0 && !deathSnapshot;
}

static inline int G_DuelCaptureSwingLinked(const duel_capture_swing_t *swing, int attacking, int move)
{
	return swing->chainPending && attacking && (!swing->active || swing->move != move);
}

static inline int G_DuelCaptureMarkLethal(int health, int *recorded)
{
	if (health > 0 || *recorded)
		return 0;
	*recorded = 1;
	return 1;
}

static inline int G_DuelCaptureCreditsOpponent(int sessionActive, int observer,
	int attacker, int target, int selectedOpponent)
{
	return sessionActive && observer >= 0 && attacker == observer &&
		selectedOpponent >= 0 && target == selectedOpponent && target != observer;
}

static inline int G_DuelCaptureSameOpponent(const char *attackKey, int attackClient,
	const char *eventKey, int eventClient)
{
	return attackClient >= 0 && attackClient == eventClient && attackKey && attackKey[0] &&
		eventKey && !strcmp(attackKey, eventKey);
}

static inline void G_DuelCaptureAccumulateOutcome(duel_capture_outcome_t *outcome,
	int attackIndex, int endIndex, int startTime, int endTime,
	int eventIndex, int eventTime, int kind, int amount, int killedEnemy)
{
	if (eventIndex <= attackIndex || eventIndex >= endIndex ||
		eventTime < startTime || eventTime > endTime || amount <= 0)
		return;
	if (kind == DUEL_CAPTURE_OUTCOME_DEALT)
	{
		outcome->dealt += amount;
		outcome->killedEnemy |= killedEnemy;
	}
	else if (kind == DUEL_CAPTURE_OUTCOME_TAKEN)
		outcome->taken += amount;
}

static inline const char *G_DuelCaptureOutcomeQuality(const duel_capture_outcome_t *outcome)
{
	if (outcome->taken > outcome->dealt)
		return outcome->taken >= 60 ? "mistake" : "bad";
	if (outcome->taken == outcome->dealt)
		return "mediocre";
	if (outcome->dealt > 0 && (outcome->killedEnemy || outcome->dealt >= 60))
		return "correct";
	if (outcome->dealt >= 20)
		return "good";
	return "mediocre";
}

static inline int G_DuelCaptureDamageAmount(int oldHealth, int newHealth, int armorLost)
{
	int lost = oldHealth - newHealth;
	if (oldHealth <= 0)
		return 0;
	if (lost < 0)
		lost = 0;
	if (lost > oldHealth)
		lost = oldHealth;
	return lost + (armorLost > 0 ? armorLost : 0);
}

/* Sum signed shortest-angle steps, not the opponent-relative angle or a timed estimate. */
static inline void G_DuelCaptureSampleYaw(duel_capture_swing_t *swing, float yaw)
{
	float delta = yaw - swing->lastYaw;
	while (delta > 180.0f)
		delta -= 360.0f;
	while (delta < -180.0f)
		delta += 360.0f;
	if (swing->active)
		swing->sweep += delta;
	swing->lastYaw = yaw;
}

/* Leave the old move/sweep intact until its end event is recorded. */
static inline int G_DuelCapturePrepareSwingTransition(duel_capture_swing_t *swing,
	int attacking, int move, float yaw)
{
	int ended = swing->active && (!attacking || swing->move != move);
	G_DuelCaptureSampleYaw(swing, yaw);
	return ended;
}

static inline int G_DuelCaptureSwingStartedWithContinuity(duel_capture_swing_t *swing,
	int attacking, int transitioning, int move, int time, float yaw)
{
	int started = attacking && (!swing->active || swing->move != move);
	G_DuelCaptureSampleYaw(swing, yaw);
	if (started)
	{
		swing->sweep = 0.0f;
		swing->startTime = time;
	}
	swing->active = attacking;
	swing->move = move;
	if (attacking)
		swing->chainPending = 1;
	else if (!transitioning)
		swing->chainPending = 0;
	return started;
}

static inline int G_DuelCaptureSwingStarted(duel_capture_swing_t *swing,
	int attacking, int move, int time, float yaw)
{
	return G_DuelCaptureSwingStartedWithContinuity(swing, attacking, 0, move, time, yaw);
}

#endif
