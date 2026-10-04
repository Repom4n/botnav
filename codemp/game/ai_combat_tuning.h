#ifndef AI_COMBAT_TUNING_H
#define AI_COMBAT_TUNING_H

#include <math.h>
#include "g_bot_learning.h"

#define NEWBOTAI_TUNING_ESCAPE_YAW_SPEED 333.0f

typedef enum
{
	NEWBOTAI_DRAINLOCK_FORCE_NONE = 0,
	NEWBOTAI_DRAINLOCK_FORCE_PULL,
	NEWBOTAI_DRAINLOCK_FORCE_DRAIN
} newbotai_drainlock_force_choice_t;

typedef enum
{
	NEWBOTAI_SABER_TACTIC_HOLD = 0,
	NEWBOTAI_SABER_TACTIC_ADVANCE,
	NEWBOTAI_SABER_TACTIC_ATTACK,
	NEWBOTAI_SABER_TACTIC_CHAIN,
	NEWBOTAI_SABER_TACTIC_COUNTER,
	NEWBOTAI_SABER_TACTIC_RESET,
	NEWBOTAI_SABER_TACTIC_STEP_IN,		// close from 96-160u, swing once inside reach
	NEWBOTAI_SABER_TACTIC_REPOSITION,	// "good": controlled lateral reset without swinging
	NEWBOTAI_SABER_TACTIC_STAND_SWING,	// "mediocre": swing in place without footwork
	NEWBOTAI_SABER_TACTIC_BACK_SWING,	// "mistake": back away while swinging
	NEWBOTAI_SABER_TACTIC_LONG_SWING	// "mistake": swing from outside reach (>150u)
} newbotai_saber_tactic_t;

// Grades describe timing/position errors, not permissions to bypass engine guards.
typedef enum
{
	NEWBOTAI_SABER_GRADE_CORRECT = 0,
	NEWBOTAI_SABER_GRADE_GOOD,
	NEWBOTAI_SABER_GRADE_MEDIOCRE,
	NEWBOTAI_SABER_GRADE_BAD,
	NEWBOTAI_SABER_GRADE_MISTAKE
} newbotai_saber_grade_t;

// 2D origin distances. Human hits cluster at 41-53u; bots started most swings from 100u+.
#define NEWBOTAI_SABER_ATTACK_RANGE 85.0f
#define NEWBOTAI_SABER_STEP_IN_RANGE 160.0f
#define NEWBOTAI_SABER_SWING_START_MAX_RANGE 85.0f
#define NEWBOTAI_SABER_LONG_SWING_RANGE 150.0f
#define NEWBOTAI_SABER_CRITICAL_TOTAL_HEALTH 30
#define NEWBOTAI_SABER_COUNTER_WINDOW_MS 600
#define NEWBOTAI_SABER_LANDED_HIT_WINDOW_MS 700
#define NEWBOTAI_SABER_GOOD_CHAIN_LENGTH 2
// Dueltracks2 (1c34db989): humans hit at ~54u still closing at +137-184u/s, so a swing
// started inside ~100u of predicted peak range lands when the bot steps in with it. The
// band around the threshold stops the reach decision flickering frame to frame.
#define NEWBOTAI_SABER_STEP_IN_SWING_RANGE 100.0f
#define NEWBOTAI_SABER_RANGE_HYSTERESIS 10.0f
#define NEWBOTAI_SABER_COMMIT_PEAK_RANGE 45.0f
#define NEWBOTAI_SABER_STAND_COMMIT_PEAK_RANGE 55.0f
#define NEWBOTAI_SABER_BACKPEDAL_SWING_MAX_SKILL 7
// Choice grades are held until the current swing finishes (or the cap), not re-rolled.
#define NEWBOTAI_SABER_GRADE_HOLD_MS 500
#define NEWBOTAI_SABER_GRADE_HOLD_MAX_MS 1000
// Movement intent carried across controller <-> force/fan code ownership changes.
#define NEWBOTAI_SABER_HANDOVER_MS 200

// Saber advance footwork (dueltracks3, capture 15a44a555): human saber hits landed at a
// median 73u while still closing at +170-250u/s (jundon: windup hits 101u at +249u/s, apex
// 44u at +42u/s, cooldown 75u at +358u/s); failed human swings started at 120-140u closing
// only ~+30u/s. Current bots released swings while backing off (-57u/s) and their damage per
// duel fell 204 -> 131. Bots therefore keep walking in through windup, apex and cooldown and
// between swings, and only hand the stick back to the planned direction in the last
// NEWBOTAI_SABER_ADVANCE_RELINK_MS before a chained swing is chosen, so the next swing's
// direction (which the engine reads from the movement keys) is not corrupted.
#define NEWBOTAI_SABER_ADVANCE_RELINK_MS 150
#define NEWBOTAI_SABER_ADVANCE_WINDUP_MIN_RANGE 40.0f
#define NEWBOTAI_SABER_ADVANCE_APEX_MIN_RANGE 60.0f
#define NEWBOTAI_SABER_ADVANCE_COOLDOWN_MIN_RANGE 48.0f
#define NEWBOTAI_SABER_ADVANCE_IDLE_MIN_RANGE 70.0f
#define NEWBOTAI_SABER_ADVANCE_MAX_RANGE 400.0f

typedef enum
{
	NEWBOTAI_SABER_PHASE_IDLE = 0,	// ready, between swings
	NEWBOTAI_SABER_PHASE_WINDUP,	// start move
	NEWBOTAI_SABER_PHASE_APEX,		// attack move
	NEWBOTAI_SABER_PHASE_COOLDOWN	// return / transition move
} newbotai_saber_phase_t;

// Longitudinal input (-1/0/1) for the final saber-duel input. plannedForward is what the
// planner already chose; chaining is set while attack is held for the next swing and
// msToChain is the time until the engine picks that swing (weaponTime).
static inline int NewBotAI_SaberAdvanceForward(newbotai_saber_phase_t phase, float range,
	int plannedForward, int chaining, int msToChain, int startingSwing, int enemyAttacking,
	int deliberateEscape)
{
	float minRange;

	if (deliberateEscape || plannedForward > 0 || startingSwing)
		return plannedForward;
	if (range > NEWBOTAI_SABER_ADVANCE_MAX_RANGE)
		return plannedForward;
	if (chaining && phase != NEWBOTAI_SABER_PHASE_IDLE && msToChain <= NEWBOTAI_SABER_ADVANCE_RELINK_MS)
		return plannedForward;
	switch (phase)
	{
	case NEWBOTAI_SABER_PHASE_WINDUP:
		minRange = NEWBOTAI_SABER_ADVANCE_WINDUP_MIN_RANGE;
		break;
	case NEWBOTAI_SABER_PHASE_APEX:
		minRange = NEWBOTAI_SABER_ADVANCE_APEX_MIN_RANGE;
		break;
	case NEWBOTAI_SABER_PHASE_COOLDOWN:
		minRange = NEWBOTAI_SABER_ADVANCE_COOLDOWN_MIN_RANGE;
		break;
	default:
		//Between swings humans advanced on ~64% of enemy swings and dodged back otherwise:
		//keep a planned dodge back from an incoming swing, otherwise close to swing range.
		if (enemyAttacking && plannedForward < 0)
			return plannedForward;
		minRange = NEWBOTAI_SABER_ADVANCE_IDLE_MIN_RANGE;
		break;
	}
	return range > minRange ? 1 : plannedForward;
}

// Range with a buffer: once inside `threshold`, the bot stays inside until the range
// exceeds threshold + band. Returns the range to plan with (clamped to the threshold).
static inline float NewBotAI_SaberRangeWithHysteresis(float range, float threshold, float band, int wasInside)
{
	if (wasInside && range > threshold && range <= threshold + band)
		return threshold;
	return range;
}

// Re-roll the saber choice grade only once the hold time passed and no swing is running,
// or once the hard cap passed.
static inline int NewBotAI_SaberGradeShouldReroll(int now, int rolledAt, int swingActive)
{
	const int elapsed = now - rolledAt;

	if (rolledAt <= 0 || elapsed < 0 || elapsed >= NEWBOTAI_SABER_GRADE_HOLD_MAX_MS)
		return 1;
	if (elapsed < NEWBOTAI_SABER_GRADE_HOLD_MS)
		return 0;
	return swingActive ? 0 : 1;
}

// Forward intent while handing movement between owners: within the handover window a
// previous forward intent is kept unless the new owner is in a deliberate escape.
static inline int NewBotAI_SaberHandoverForward(int previousForward, int newForward,
	int elapsedMs, int deliberateEscape)
{
	if (deliberateEscape || elapsedMs < 0 || elapsedMs > NEWBOTAI_SABER_HANDOVER_MS)
		return newForward;
	return (previousForward > 0 && newForward <= 0) ? previousForward : newForward;
}

// Humans never started a swing while backpedalling (0% vs 23% for bots): above the low
// skill band a swing start that is not a deliberate escape steps toward the opponent.
static inline int NewBotAI_SaberSwingStartForward(int skill, int startingSwing, int forward,
	int deliberateEscape)
{
	if (startingSwing && forward < 0 && !deliberateEscape &&
		skill >= NEWBOTAI_SABER_BACKPEDAL_SWING_MAX_SKILL)
		return 1;
	return forward;
}

typedef struct
{
	int saberOnlyDuel;
	int skill;
	int mistakeBias;
	int mistakeRoll;
	int ourTotalHealth;
	int enemyTotalHealth;
	int recentlyHurt;	// we took damage within NEWBOTAI_SABER_COUNTER_WINDOW_MS
	int enemyAttacking;
	int enemyVulnerable;
	int selfAttacking;
	int chainLength;
	int landedHit;		// we damaged the enemy within the last ~700ms
	float enemyDistance;
	int enemyRecovering;
	int selfBlocked;
	int counterReady;
	int saberCombat;
	int closingKnown;	// closingSpeed/currentDistance are valid (zero-initialized: unknown)
	float closingSpeed;	// 2D radial speed, > 0 while closing on the enemy
	float currentDistance;	// 2D range now (enemyDistance is the predicted peak range)
} newbotai_saber_tactic_context_t;

typedef struct
{
	int minWeight;
	int drainlockAdvantage;
	int pullkickDrainWindow;
	int drainWeight;
	int pullWeight;
	int enemyForce;
} newbotai_drainlock_force_context_t;

enum
{
	NEWBOTAI_PTK_ARMOR_NO_LEAD_PENALTY = 60,
	NEWBOTAI_PTK_ARMOR_SMALL_LEAD_PENALTY = 35,
	NEWBOTAI_PTK_ARMOR_STRONG_LEAD_BONUS = 10
};

static inline int NewBotAI_GetEffectiveMoveInput(int cmdMove, int forcedMove)
{
	return forcedMove ? forcedMove : cmdMove;
}

// Skill gradient: skill 7+ takes the best choice except for a small mistake-bias roll that
// reaches zero at skill 10; lower skills keep the full mistake band.
static inline int NewBotAI_GetHighSkillMistakeChance(int skill, int mistakeBias)
{
	if (skill >= 10)
		return 0;
	if (mistakeBias < 0)
		mistakeBias = 0;
	else if (mistakeBias > 100)
		mistakeBias = 100;
	return (mistakeBias * (10 - skill)) / 30;
}

static inline int NewBotAI_GetSaberTacticMistakeChance(int skill, int mistakeBias)
{
	int clampedSkill = skill;
	int clampedBias = mistakeBias;
	int chance;

	if (clampedSkill < 1)
		clampedSkill = 1;
	else if (clampedSkill > 10)
		clampedSkill = 10;
	if (clampedBias < 0)
		clampedBias = 0;
	else if (clampedBias > 100)
		clampedBias = 100;
	if (clampedSkill >= 10)
		return 0;
	// Skill 7+ plays the best known choice apart from a small bias roll (see
	// NewBotAI_GetHighSkillMistakeChance); the full mistake band is for skills 1-6.
	if (clampedSkill >= 7)
		return NewBotAI_GetHighSkillMistakeChance(clampedSkill, clampedBias);

	chance = (10 - clampedSkill) * 3;
	chance += (clampedBias * (140 - clampedSkill * 8)) / 100;
	return chance > 95 ? 95 : chance;
}

// Rolls how good the bot's next saber-duel choice is. Skill 10 (or a roll outside the
// mistake band) is always CORRECT; inside the band, lower skill draws deeper errors.
static inline newbotai_saber_grade_t NewBotAI_GetSaberChoiceGrade(int skill, int mistakeBias, int roll)
{
	const int chance = NewBotAI_GetSaberTacticMistakeChance(skill, mistakeBias);
	int clampedSkill = skill;
	int depth;

	if (chance <= 0 || roll <= 0 || roll > chance)
		return NEWBOTAI_SABER_GRADE_CORRECT;

	if (clampedSkill < 1)
		clampedSkill = 1;
	else if (clampedSkill > 10)
		clampedSkill = 10;

	depth = (roll * 100) / chance + clampedSkill * 3;
	if (depth <= 20)
		return NEWBOTAI_SABER_GRADE_MISTAKE;
	if (depth <= 40)
		return NEWBOTAI_SABER_GRADE_BAD;
	if (depth <= 70)
		return NEWBOTAI_SABER_GRADE_MEDIOCRE;
	return NEWBOTAI_SABER_GRADE_GOOD;
}

static inline int NewBotAI_SaberNeedsSafetyExit(newbotai_saber_tactic_context_t context)
{
	return context.selfBlocked || (!context.enemyVulnerable &&
		((context.ourTotalHealth <= NEWBOTAI_SABER_CRITICAL_TOTAL_HEALTH &&
		  context.enemyTotalHealth > context.ourTotalHealth &&
		  (context.recentlyHurt || context.enemyAttacking)) ||
		 (context.enemyAttacking && (context.chainLength >= 2 ||
		  (context.recentlyHurt && context.ourTotalHealth < context.enemyTotalHealth)))));
}

// A landed hit is not permission to trade indefinitely (BK duel563); exits are
// useful even above critical health when a continuation becomes exposed.
static inline newbotai_saber_tactic_t NewBotAI_GetCorrectSaberTactic(
	newbotai_saber_tactic_context_t context)
{
	const float dist = context.enemyDistance;
	const int inSwingReach = (dist <= NEWBOTAI_SABER_SWING_START_MAX_RANGE) ? 1 : 0;

	if (!context.saberOnlyDuel && !context.saberCombat)
		return NEWBOTAI_SABER_TACTIC_HOLD;

	if (NewBotAI_SaberNeedsSafetyExit(context))
		return NEWBOTAI_SABER_TACTIC_RESET;

	if (context.enemyVulnerable)
		return inSwingReach ? NEWBOTAI_SABER_TACTIC_COUNTER : NEWBOTAI_SABER_TACTIC_ADVANCE;

	if (context.recentlyHurt && !context.counterReady && inSwingReach)
		return NEWBOTAI_SABER_TACTIC_REPOSITION;

	if (context.recentlyHurt && context.counterReady && context.enemyRecovering)
	{
		return inSwingReach ? NEWBOTAI_SABER_TACTIC_COUNTER : NEWBOTAI_SABER_TACTIC_STEP_IN;
	}

	if (context.enemyAttacking && inSwingReach)
		return NEWBOTAI_SABER_TACTIC_REPOSITION;

	// Exit unproductive pressure, not a still-safe sequence that is landing hits.
	if (context.chainLength >= 3 && !context.landedHit && !context.enemyRecovering)
		return NEWBOTAI_SABER_TACTIC_RESET;

	if (context.landedHit || (context.selfAttacking && context.enemyRecovering))
	{
		if (inSwingReach)
			return NEWBOTAI_SABER_TACTIC_CHAIN;
		return (dist <= NEWBOTAI_SABER_STEP_IN_RANGE) ?
			NEWBOTAI_SABER_TACTIC_STEP_IN : NEWBOTAI_SABER_TACTIC_ADVANCE;
	}

	if (dist > NEWBOTAI_SABER_STEP_IN_RANGE)
		return NEWBOTAI_SABER_TACTIC_ADVANCE;
	if (dist > NEWBOTAI_SABER_ATTACK_RANGE)
		return NEWBOTAI_SABER_TACTIC_STEP_IN;
	return NEWBOTAI_SABER_TACTIC_ATTACK;
}

static inline newbotai_saber_tactic_t NewBotAI_ApplySaberChoiceGrade(
	newbotai_saber_tactic_context_t context, newbotai_saber_tactic_t tactic,
	newbotai_saber_grade_t grade)
{
	const float dist = context.enemyDistance;

	switch (grade)
	{
	case NEWBOTAI_SABER_GRADE_GOOD:
		if (tactic == NEWBOTAI_SABER_TACTIC_CHAIN &&
			context.chainLength >= NEWBOTAI_SABER_GOOD_CHAIN_LENGTH)
		{
			return NEWBOTAI_SABER_TACTIC_REPOSITION;
		}
		return tactic;
	case NEWBOTAI_SABER_GRADE_MEDIOCRE:
		switch (tactic)
		{
		case NEWBOTAI_SABER_TACTIC_COUNTER:
		case NEWBOTAI_SABER_TACTIC_CHAIN:
		case NEWBOTAI_SABER_TACTIC_ATTACK:
		case NEWBOTAI_SABER_TACTIC_STEP_IN:
			return (dist <= NEWBOTAI_SABER_SWING_START_MAX_RANGE) ?
				NEWBOTAI_SABER_TACTIC_STAND_SWING : NEWBOTAI_SABER_TACTIC_STEP_IN;
		default:
			return tactic;
		}
	case NEWBOTAI_SABER_GRADE_BAD:
		if (dist > NEWBOTAI_SABER_SWING_START_MAX_RANGE)
			return NEWBOTAI_SABER_TACTIC_STEP_IN;
		return (tactic == NEWBOTAI_SABER_TACTIC_CHAIN) ?
			NEWBOTAI_SABER_TACTIC_RESET : NEWBOTAI_SABER_TACTIC_REPOSITION;
	case NEWBOTAI_SABER_GRADE_MISTAKE:
		// Backpedal swings are a low-skill mistake only (humans: 0% of swing starts).
		if (context.skill >= NEWBOTAI_SABER_BACKPEDAL_SWING_MAX_SKILL &&
			dist <= NEWBOTAI_SABER_LONG_SWING_RANGE)
			return (dist <= NEWBOTAI_SABER_SWING_START_MAX_RANGE) ?
				NEWBOTAI_SABER_TACTIC_STAND_SWING : NEWBOTAI_SABER_TACTIC_STEP_IN;
		return (dist > NEWBOTAI_SABER_LONG_SWING_RANGE) ?
			NEWBOTAI_SABER_TACTIC_LONG_SWING : NEWBOTAI_SABER_TACTIC_BACK_SWING;
	case NEWBOTAI_SABER_GRADE_CORRECT:
	default:
		return tactic;
	}
}

static inline newbotai_saber_tactic_t NewBotAI_SelectSaberTactic(
	newbotai_saber_tactic_context_t context)
{
	if (!context.saberOnlyDuel && !context.saberCombat)
		return NEWBOTAI_SABER_TACTIC_HOLD;

	// Timing errors must never turn a necessary escape into an exposed swing.
	if (NewBotAI_GetCorrectSaberTactic(context) == NEWBOTAI_SABER_TACTIC_RESET)
		return NEWBOTAI_SABER_TACTIC_RESET;

	return NewBotAI_ApplySaberChoiceGrade(context,
		NewBotAI_GetCorrectSaberTactic(context),
		NewBotAI_GetSaberChoiceGrade(context.skill, context.mistakeBias, context.mistakeRoll));
}

typedef enum
{
	NEWBOTAI_SABER_BASIC = 0,
	NEWBOTAI_SABER_HORIZONTAL,
	NEWBOTAI_SABER_DIAGONAL_VERTICAL,
	NEWBOTAI_SABER_COUNTER_ENTRY,
	NEWBOTAI_SABER_FINISH,
	NEWBOTAI_SABER_BURST
} newbotai_saber_family_t;

static inline int NewBotAI_SaberBurstComplete(newbotai_saber_family_t family, int acceptedAttacks, int exposed)
{
	return family == NEWBOTAI_SABER_BURST && acceptedAttacks >= 2 && exposed;
}

typedef struct
{
	int forward;
	int right;
	int jump;
	int attack;
} newbotai_saber_command_t;

static inline int NewBotAI_SaberMoveAccepted(int move, int lastMove, int basicAttack)
{
	// Starts, transitions, returns and a held input do not advance a sequence.
	return basicAttack && move != lastMove;
}

static inline int NewBotAI_SaberHorizontalDirection(int move, int leftToRight, int rightToLeft)
{
	return move == leftToRight ? 1 : move == rightToLeft ? -1 : 0;
}

static inline int NewBotAI_SaberNextHorizontalDirection(int actualDirection, int desiredDirection)
{
	return actualDirection ? -actualDirection : desiredDirection;
}

typedef enum
{
	NEWBOTAI_SABER_YAW_PREPARE = 0,
	NEWBOTAI_SABER_YAW_ACTIVE,
	NEWBOTAI_SABER_YAW_RECOVER,
	NEWBOTAI_SABER_YAW_NEXT
} newbotai_saber_yaw_phase_t;

// Conservative control envelopes, not measured human animation templates.
#define NEWBOTAI_SABER_SWEEP_DEGREES 18.0f
#define NEWBOTAI_SABER_SWEEP_RATE 240.0f

static inline float NewBotAI_SaberAnimationProgress(int remaining, int duration)
{
	float progress;
	if (duration <= 0)
		return 0.0f;
	progress = 1.0f - (float)remaining / duration;
	return progress < 0.0f ? 0.0f : progress > 1.0f ? 1.0f : progress;
}

static inline newbotai_saber_yaw_phase_t NewBotAI_SaberYawPhase(
	int basicAttack, int preparing, int remaining, int duration)
{
	const float progress = NewBotAI_SaberAnimationProgress(remaining, duration);
	if (!basicAttack)
		return preparing ? NEWBOTAI_SABER_YAW_PREPARE : NEWBOTAI_SABER_YAW_RECOVER;
	if (remaining <= 0 || progress >= 0.8f)
		return NEWBOTAI_SABER_YAW_NEXT;
	return progress < 0.15f ? NEWBOTAI_SABER_YAW_PREPARE : NEWBOTAI_SABER_YAW_ACTIVE;
}

static inline int NewBotAI_SaberCanApplyPressure(newbotai_saber_yaw_phase_t phase,
	int acceptedAttack, int weaponTime)
{
	return phase == NEWBOTAI_SABER_YAW_ACTIVE && acceptedAttack && weaponTime > 0;
}

static inline float NewBotAI_SaberYawOffset(newbotai_saber_yaw_phase_t phase,
	float progress, int actualDirection, int desiredDirection, int sweep)
{
	const float amplitude = sweep ? NEWBOTAI_SABER_SWEEP_DEGREES : 0.0f;
	if (phase == NEWBOTAI_SABER_YAW_RECOVER)
		return 0.0f;
	if (phase == NEWBOTAI_SABER_YAW_NEXT)
		return desiredDirection * amplitude;
	if (phase == NEWBOTAI_SABER_YAW_PREPARE)
		return (actualDirection ? actualDirection : desiredDirection) * amplitude;
	progress = (progress - 0.15f) / 0.65f;
	if (progress < 0.0f) progress = 0.0f;
	if (progress > 1.0f) progress = 1.0f;
	// Conservative sign convention, not a universal human curve: L2R biases
	// yaw downward and R2L upward; recorded full-force variants can differ.
	return actualDirection * amplitude * (1.0f - 2.0f * progress);
}

static inline float NewBotAI_SaberApplyYawOffset(float baseYaw, float offset)
{
	baseYaw += offset;
	while (baseYaw >= 360.0f) baseYaw -= 360.0f;
	while (baseYaw < 0.0f) baseYaw += 360.0f;
	return baseYaw;
}

static inline float NewBotAI_SaberStepYawOffset(float current, float target, int elapsedMs)
{
	float delta;
	float step;
	if (target > NEWBOTAI_SABER_SWEEP_DEGREES) target = NEWBOTAI_SABER_SWEEP_DEGREES;
	if (target < -NEWBOTAI_SABER_SWEEP_DEGREES) target = -NEWBOTAI_SABER_SWEEP_DEGREES;
	if (elapsedMs < 0) elapsedMs = 0;
	if (elapsedMs > 100) elapsedMs = 100;
	delta = target - current;
	step = NEWBOTAI_SABER_SWEEP_RATE * elapsedMs / 1000.0f;
	if (delta > step) delta = step;
	if (delta < -step) delta = -step;
	return current + delta;
}

static inline int NewBotAI_SaberCanOwnInputs(int holdingSaber, int visibleEnemy,
	int engineBusy, int forceMovement, int navigationMovement)
{
	return holdingSaber && visibleEnemy && !engineBusy && !forceMovement && !navigationMovement;
}

static inline int NewBotAI_SaberPrimaryBladeAvailable(int holstered)
{
	// Staff/dual partial holster (1) disables the second blade, not primary attacks.
	return holstered >= 0 && holstered < 2;
}

static inline int NewBotAI_SaberOrdinaryAttackSafe(int ducked, int rising, int redDfaBoundary)
{
	return !ducked && !rising && !redDfaBoundary;
}

static inline int NewBotAI_ShouldAnticipateSaberThrow(int saberOnlyDuel, int saberInFlight, int basicSwing)
{
	return !saberOnlyDuel && !saberInFlight && !basicSwing;
}

static inline newbotai_saber_family_t NewBotAI_SelectSaberFamily(
	newbotai_saber_tactic_context_t context, int fanBias, int roll)
{
	// Zero bias keeps a legal basic attack. Bias weights learned complexity,
	// never attack permission or an old fan hold/dwell package.
	if (fanBias <= 0 || roll > (fanBias > 100 ? 100 : fanBias))
		return NEWBOTAI_SABER_BASIC;
	if (context.enemyTotalHealth <= 40 && (context.landedHit || context.enemyVulnerable))
		return NEWBOTAI_SABER_FINISH;
	if (context.recentlyHurt && context.counterReady &&
		(context.enemyRecovering || context.enemyVulnerable))
		return NEWBOTAI_SABER_COUNTER_ENTRY;
	if (context.ourTotalHealth < context.enemyTotalHealth || context.enemyAttacking)
		return NEWBOTAI_SABER_BURST;
	if (!context.recentlyHurt && (context.enemyRecovering || context.enemyVulnerable) && roll % 10 == 0)
		return NEWBOTAI_SABER_DIAGONAL_VERTICAL;
	return NEWBOTAI_SABER_HORIZONTAL;
}

static inline void NewBotAI_SaberSelectionInputs(newbotai_saber_family_t family,
	int stage, int direction, int *forward, int *right)
{
	*forward = 0;
	*right = direction < 0 ? -1 : 1;
	if (family == NEWBOTAI_SABER_DIAGONAL_VERTICAL)
	{
		if (stage % 3 == 0)
			*forward = 1;
		else if (stage % 3 == 1)
		{
			*forward = 1;
			*right = 0;
		}
	}
	else if (family == NEWBOTAI_SABER_BASIC)
	{
		if (stage % 3 == 1)
		{
			*forward = 1;
			*right = 0;
		}
		else if (stage % 3 == 2)
			*forward = 1;
	}
}

static inline int NewBotAI_SaberPreparationInputs(int attackIndex, int *forward, int *right)
{
	// Basic attack order: TL2BR, L2R, BL2TR, BR2TL, R2L, TR2BL, T2B.
	static const int forwardInputs[7] = { 1, 0, -1, -1, 0, 1, 1 };
	static const int rightInputs[7] = { 1, 1, 1, -1, -1, -1, 0 };
	if (attackIndex < 0 || attackIndex >= 7)
		return 0;
	*forward = forwardInputs[attackIndex];
	*right = rightInputs[attackIndex];
	return 1;
}

static inline void NewBotAI_SaberNoStrafeFallback(int suppressed, int preparing, int *forward, int *right)
{
	if (suppressed && !preparing && *right)
	{
		// Choose an ordinary grounded vertical attack explicitly at a free boundary.
		// A committed horizontal start must finish before its selection can change.
		*forward = 1;
		*right = 0;
	}
}

#define NEWBOTAI_SABER_EXIT_MS 350
#define NEWBOTAI_SABER_REENTRY_MS 650
#define NEWBOTAI_SABER_AIR_EXIT_MAX_MS 1400

static inline int NewBotAI_SaberEscapePhase(int now, int exitUntil, int reentryUntil,
	int airborne, int airExitUntil)
{
	if (exitUntil > now || (exitUntil > 0 && airborne && airExitUntil > now))
		return -1;
	if (reentryUntil > now)
		return 1;
	return 0;
}

static inline int NewBotAI_SaberCanEscapeJump(int grounded, int jumpReleased,
	int engineReady, int resourceReady, int safePath, int cooldownReady)
{
	return grounded && jumpReleased && engineReady && resourceReady && safePath && cooldownReady;
}

static inline int NewBotAI_SaberTacticAllowsSwingStart(newbotai_saber_tactic_t tactic, float enemyDistance);
static inline int NewBotAI_SaberFreshSwingWindowAllows(newbotai_saber_tactic_context_t context,
	newbotai_saber_tactic_t tactic);

// Forward input while attacking: commit toward ~40-55u at the swing peak. STAND_SWING
// (mediocre timing) commits less deep; BACK_SWING is the low-skill backpedal mistake.
static inline int NewBotAI_SaberAttackForward(newbotai_saber_tactic_t tactic, float enemyDistance)
{
	if (tactic == NEWBOTAI_SABER_TACTIC_BACK_SWING)
		return -1;
	if (tactic == NEWBOTAI_SABER_TACTIC_STAND_SWING)
		return enemyDistance > NEWBOTAI_SABER_STAND_COMMIT_PEAK_RANGE ? 1 : 0;
	return enemyDistance > NEWBOTAI_SABER_COMMIT_PEAK_RANGE ? 1 : 0;
}

static inline newbotai_saber_command_t NewBotAI_PlanSaberCommand(
	newbotai_saber_tactic_context_t context, newbotai_saber_tactic_t tactic,
	newbotai_saber_family_t family, int stage, int direction, int grounded,
	int choosingMove, int attackLegal, int escapePhase)
{
	newbotai_saber_command_t command = { 0, 0, 0, 0 };
	const int side = direction < 0 ? -1 : 1;

	if (escapePhase > 0 && !context.enemyAttacking && !NewBotAI_SaberNeedsSafetyExit(context))
		tactic = context.enemyDistance <= NEWBOTAI_SABER_SWING_START_MAX_RANGE ?
			NEWBOTAI_SABER_TACTIC_ATTACK : NEWBOTAI_SABER_TACTIC_STEP_IN;
	if (escapePhase < 0 || tactic == NEWBOTAI_SABER_TACTIC_RESET)
	{
		command.forward = (escapePhase < 0 && !grounded) || context.enemyDistance < 176.0f ? -1 : 0;
		command.right = side;
		return command;
	}
	if (tactic == NEWBOTAI_SABER_TACTIC_REPOSITION)
	{
		command.forward = context.enemyDistance < 80.0f ? -1 : 0;
		command.right = side;
		return command;
	}
	if (tactic == NEWBOTAI_SABER_TACTIC_ADVANCE || tactic == NEWBOTAI_SABER_TACTIC_STEP_IN)
		command.forward = 1;
	else if (tactic == NEWBOTAI_SABER_TACTIC_BACK_SWING)
		command.forward = -1;

	command.attack = grounded && attackLegal &&
		NewBotAI_SaberTacticAllowsSwingStart(tactic, context.enemyDistance) &&
		NewBotAI_SaberFreshSwingWindowAllows(context, tactic);
	if (command.attack)
	{
		command.right = side;
		command.forward = NewBotAI_SaberAttackForward(tactic, context.enemyDistance);
		// Starts and transitions can be much longer than 100ms (especially red).
		// Hold the deliberate family/stage selection until an accepted attack owns pressure.
		if (choosingMove)
			NewBotAI_SaberSelectionInputs(family, stage, side, &command.forward, &command.right);
	}
	// Airborne footwork stays deliberate, but cannot accidentally start aerial specials.
	if (!grounded)
	{
		command.forward = escapePhase > 0 && !context.enemyAttacking &&
			!NewBotAI_SaberNeedsSafetyExit(context) ? 1 :
			context.enemyDistance > 96.0f ? 1 : -1;
		command.right = side;
	}
	return command;
}

static inline void NewBotAI_SaberSuppressStrafe(newbotai_saber_command_t *command, int suppressed)
{
	if (suppressed)
		command->right = 0;
}

static inline void NewBotAI_SaberGuardSelection(newbotai_saber_command_t *command, int selecting,
	int selectedForward, int selectedRight)
{
	if (command->attack && selecting)
	{
		if (command->right != selectedRight || command->forward != selectedForward ||
			(!command->forward && !command->right))
			command->attack = 0;
		command->forward = selectedForward && command->attack ? selectedForward : 0;
	}
}

typedef struct
{
	int selecting;
	int selectedForward;
	int selectedRight;
	int pressureForward;
	int pressureRight;
	int strafeSuppressed;
	int attackSafe;
} newbotai_saber_final_context_t;

static inline newbotai_saber_command_t NewBotAI_FinalizeSaberCommand(
	newbotai_saber_command_t command, newbotai_saber_final_context_t context)
{
	if (!context.attackSafe)
		command.attack = 0;
	if (command.attack && (command.forward || command.right))
	{
		command.forward = context.selecting ? context.selectedForward : context.pressureForward;
		command.right = context.selecting ? context.selectedRight : context.pressureRight;
	}
	NewBotAI_SaberSuppressStrafe(&command, context.strafeSuppressed);
	NewBotAI_SaberGuardSelection(&command, context.selecting, context.selectedForward, context.selectedRight);
	return command;
}

static inline void NewBotAI_SaberApplyMovementSafety(newbotai_saber_command_t *command, int safe)
{
	if (!safe)
	{
		command->forward = 0;
		command->right = 0;
		command->jump = 0;
		// A blocked directional selection must not fall through to a different swing.
		command->attack = 0;
	}
}

static inline int NewBotAI_SaberOwnedActionFlags(int flags, int ownedMask, int plannedFlags)
{
	return (flags & ~ownedMask) | plannedFlags;
}

static inline int NewBotAI_SaberDuelActionFlags(int flags, int altAttackMask, int saberOnlyDuel)
{
	return saberOnlyDuel ? flags & ~altAttackMask : flags;
}

// Saber-only footwork: tactics that may start a new swing this frame. Swing starts are
// limited to NEWBOTAI_SABER_SWING_START_MAX_RANGE (STEP_IN: NEWBOTAI_SABER_STEP_IN_SWING_RANGE)
// except for the LONG_SWING mistake.
static inline int NewBotAI_SaberTacticAllowsSwingStart(newbotai_saber_tactic_t tactic, float enemyDistance)
{
	switch (tactic)
	{
	case NEWBOTAI_SABER_TACTIC_LONG_SWING:
		return 1;
	case NEWBOTAI_SABER_TACTIC_ATTACK:
	case NEWBOTAI_SABER_TACTIC_CHAIN:
	case NEWBOTAI_SABER_TACTIC_COUNTER:
	case NEWBOTAI_SABER_TACTIC_STAND_SWING:
	case NEWBOTAI_SABER_TACTIC_BACK_SWING:
		return (enemyDistance <= NEWBOTAI_SABER_SWING_START_MAX_RANGE) ? 1 : 0;
	case NEWBOTAI_SABER_TACTIC_STEP_IN:
		// Step in and swing together instead of waiting out the 85-100u band.
		return (enemyDistance <= NEWBOTAI_SABER_STEP_IN_SWING_RANGE) ? 1 : 0;
	default:
		return 0;
	}
}

// Tactics that keep attack held through swing transitions so chains link.
static inline int NewBotAI_SaberTacticHoldsChain(newbotai_saber_tactic_t tactic)
{
	return (tactic == NEWBOTAI_SABER_TACTIC_ATTACK ||
		tactic == NEWBOTAI_SABER_TACTIC_CHAIN ||
		tactic == NEWBOTAI_SABER_TACTIC_COUNTER ||
		tactic == NEWBOTAI_SABER_TACTIC_STEP_IN ||
		tactic == NEWBOTAI_SABER_TACTIC_STAND_SWING ||
		tactic == NEWBOTAI_SABER_TACTIC_BACK_SWING ||
		tactic == NEWBOTAI_SABER_TACTIC_LONG_SWING) ? 1 : 0;
}

// Health deficit (HP+armor) a bot tolerates before it stops starting saber attacks in
// force duels. Higher skill keeps countering from further behind, matching duel winners.
static inline int NewBotAI_GetSaberAttackSuppressDeficit(int skill)
{
	int clampedSkill = skill;

	if (clampedSkill < 1)
		clampedSkill = 1;
	else if (clampedSkill > 10)
		clampedSkill = 10;
	return 15 + clampedSkill * 5;
}

static inline int NewBotAI_ShouldSuppressSaberAttackForDeficit(int totalHealthDelta, int skill)
{
	return (totalHealthDelta < -NewBotAI_GetSaberAttackSuppressDeficit(skill)) ? 1 : 0;
}

// Red/strong stance may start a normal swing once the enemy is within reach or about to be.
static inline int NewBotAI_ShouldStartRedStanceSwing(float enemyDistance, int timeToInRangeMs, int ourHealth)
{
	if (ourHealth <= 25)
		return 0;
	return (enemyDistance < NEWBOTAI_SABER_ATTACK_RANGE || timeToInRangeMs < 300) ? 1 : 0;
}

// Fan entry spacing used by human horizontal swings (L2R/R2L landed mostly under 110u).
#define NEWBOTAI_FAN_ENTRY_MIN_RANGE 48.0f
#define NEWBOTAI_FAN_ENTRY_MAX_RANGE 110.0f

static inline int NewBotAI_IsFanEntrySpacing(float enemyDistance)
{
	return (enemyDistance >= NEWBOTAI_FAN_ENTRY_MIN_RANGE &&
		enemyDistance <= NEWBOTAI_FAN_ENTRY_MAX_RANGE) ? 1 : 0;
}

// Saber-only duels scale fan bias by the HP+armor difference instead of zeroing it at
// medium health.
static inline float NewBotAI_ScaleSaberDuelFanBias(float fanBias, int healthDelta)
{
	float scale = 1.0f + (float)healthDelta / 100.0f;

	if (fanBias <= 0.0f)
		return 0.0f;
	if (scale < 0.35f)
		scale = 0.35f;
	else if (scale > 1.25f)
		scale = 1.25f;
	fanBias *= scale;
	return (fanBias > 100.0f) ? 100.0f : fanBias;
}

// Fan HOLD footwork: the engine picks a horizontal L2R/R2L swing only from pure strafe input,
// so the swing-start frame strafes alone; once the swing is running, forward is added so the
// bot closes like human winners did while their horizontal swings landed.
static inline int NewBotAI_FanHoldUsesForward(int swingStarted, float enemyDistance)
{
	return (swingStarted && enemyDistance > NEWBOTAI_FAN_ENTRY_MIN_RANGE) ? 1 : 0;
}

//Saber-duel routing: private saber-only duels always use the saber-duel (NF) path, the same
//as a no-force/no-flipkick server holding a saber, regardless of g_forcePowerDisable/g_flipKick.
static inline int NewBotAI_UsesSaberDuelPath(int saberOnlyDuel, int forcePowerDisable, int flipKick, int holdingSaber)
{
	if (saberOnlyDuel)
		return 1;
	return ((forcePowerDisable == 163837 || forcePowerDisable == 163839) && !flipKick && holdingSaber) ? 1 : 0;
}

static inline int NewBotAI_AdjustPTKWeightForArmor(int weight, int enemyArmor, int forceLead)
{
	if (enemyArmor <= 0)
	{
		return weight;
	}

	if (forceLead < 1)
	{
		return weight - NEWBOTAI_PTK_ARMOR_NO_LEAD_PENALTY;
	}

	if (forceLead < 20)
	{
		return weight - NEWBOTAI_PTK_ARMOR_SMALL_LEAD_PENALTY;
	}

	return weight + NEWBOTAI_PTK_ARMOR_STRONG_LEAD_BONUS;
}

static inline int NewBotAI_GetSaberThrowPTKBonus(
	int freePullkickWindow, int enemySaberReturning, int ourHealth, int ourForce, int hisForce)
{
	if (freePullkickWindow)
	{
		if (enemySaberReturning)
		{
			return (ourHealth > 30 && ourForce > hisForce) ? 120 : 75;
		}

		return (ourHealth > 30 && ourForce > hisForce) ? 80 : 40;
	}

	return enemySaberReturning ? 20 : 10;
}

static inline int NewBotAI_GetPulledTowardEnemyPTKBonus(
	int freePullkickWindow, float enemyDistance)
{
	if (enemyDistance > 220.0f)
	{
		return 0;
	}

	return freePullkickWindow ? 140 : 90;
}

static inline int NewBotAI_IsAbsorbBaitWindow(
	int airborne, int recentFlipkickWindow, float enemyDistance,
	float immediateFlipkickRange, float pullkickRange)
{
	if (!airborne && !recentFlipkickWindow)
	{
		return 0;
	}

	return (enemyDistance > immediateFlipkickRange &&
		enemyDistance <= pullkickRange) ? 1 : 0;
}

static inline int NewBotAI_GetAbsorbBiasBonus(
	int biasPercent, int airborne, int recentFlipkickWindow, float enemyDistance,
	float immediateFlipkickRange, float pullkickRange)
{
	int clampedBias = biasPercent;
	int bonus;

	if (!NewBotAI_IsAbsorbBaitWindow(
		airborne, recentFlipkickWindow, enemyDistance, immediateFlipkickRange, pullkickRange))
	{
		return 0;
	}

	if (clampedBias < 0)
	{
		clampedBias = 0;
	}
	else if (clampedBias > 100)
	{
		clampedBias = 100;
	}

	bonus = 20 + (clampedBias * 45) / 100;
	if (airborne)
	{
		bonus += 10;
	}
	if (recentFlipkickWindow)
	{
		bonus += 5;
	}

	return bonus;
}

static inline int NewBotAI_GetAntiDarkPushBonus(
	int enemyDarkSide, int absorbAboutToEnd, int healthLead, float enemyDistance, float maxDistance)
{
	int bonus;

	if (!enemyDarkSide || !absorbAboutToEnd || healthLead <= 0 || enemyDistance > maxDistance)
	{
		return 0;
	}

	bonus = 65 + ((healthLead < 25) ? healthLead : 25);
	if (enemyDistance <= 160.0f)
	{
		bonus += 10;
	}

	return bonus;
}

static inline int NewBotAI_GetAntiDarkDrainBonus(
	int enemyDarkSide, int enemyCanHeal, int healthLead, int enemyDamaged)
{
	int bonus;

	if (!enemyDarkSide || !enemyCanHeal || healthLead <= 0)
	{
		return 0;
	}

	bonus = 60 + ((healthLead < 25) ? healthLead : 25);
	if (enemyDamaged)
	{
		bonus += 15;
	}

	return bonus;
}

static inline int NewBotAI_ShouldQueueLoginReminder(
	int duelsSeen, int startAtDuel, int repeatInterval, int lastPromptDuel)
{
	if (duelsSeen < startAtDuel || repeatInterval <= 0)
	{
		return 0;
	}

	if (lastPromptDuel >= duelsSeen)
	{
		return 0;
	}

	return ((duelsSeen - startAtDuel) % repeatInterval) == 0 ? 1 : 0;
}

static inline float NewBotAI_GetImmediateFlipkickYawTolerance(int immediateContact)
{
	return immediateContact ? 60.0f : 35.0f;
}

static inline int NewBotAI_ShouldAbortSpeedAttack(
	int beingGripped, int enemyGripActive, int enemyDrainActive,
	int ourForce, int forceLead, float enemyDistance, float maxDrainDistance)
{
	if (beingGripped)
	{
		return 1;
	}

	if (enemyGripActive && enemyDistance < 512.0f)
	{
		return 1;
	}

	if (enemyDrainActive &&
		(ourForce <= 35 || forceLead <= 0) &&
		enemyDistance < maxDrainDistance)
	{
		return 1;
	}

	return 0;
}

static const int NEWBOTAI_TUNING_SPEED_FINISHER_NO_ARMOR_HP_MAX = 30;
static const int NEWBOTAI_TUNING_SPEED_FINISHER_TOTAL_HP_AP_MAX = 50;
static const int NEWBOTAI_TUNING_SPEED_FINISHER_ARMORED_HP_MAX = 35;

static inline int NewBotAI_IsSpeedFinisherWindow(int enemyHealth, int enemyArmor)
{
	const int enemyTotalHealth = enemyHealth + enemyArmor;
	const int exposedNoArmorFinish =
		(enemyHealth <= NEWBOTAI_TUNING_SPEED_FINISHER_NO_ARMOR_HP_MAX && enemyArmor <= 0);
	const int lowPoolArmoredFinish =
		(enemyHealth <= NEWBOTAI_TUNING_SPEED_FINISHER_ARMORED_HP_MAX &&
		 enemyTotalHealth < NEWBOTAI_TUNING_SPEED_FINISHER_TOTAL_HP_AP_MAX);

	return (exposedNoArmorFinish || lowPoolArmoredFinish) ? 1 : 0;
}

static inline int NewBotAI_PassesSpeedAttackResourceLeadGate(
	int ourHealth, int ourArmor, int enemyHealth, int enemyArmor,
	float aggressionBias, int ourForce, int enemyForce)
{
	const int ourTotalHealth = ourHealth + ourArmor;
	const int enemyTotalHealth = enemyHealth + enemyArmor;
	const int healthLead = ourTotalHealth - enemyTotalHealth;
	const int forceLead = ourForce - enemyForce;

	if (ourTotalHealth <= 70 || aggressionBias < 0.35f)
	{
		return 0;
	}
	if (healthLead < 25 || forceLead < 15)
	{
		return 0;
	}

	return 1;
}

static inline float NewBotAI_GetViewAngleAxisFactor(float factor, int isYawAxis, int escapeYawOverrideActive)
{
	if (isYawAxis && escapeYawOverrideActive)
	{
		return 1.0f;
	}

	return factor;
}

static inline float NewBotAI_GetViewAngleAxisMaxChange(
	float defaultAxisMaxchange, float thinktime, int isYawAxis, int escapeYawOverrideActive)
{
	if (isYawAxis && escapeYawOverrideActive)
	{
		return NEWBOTAI_TUNING_ESCAPE_YAW_SPEED * thinktime;
	}

	return defaultAxisMaxchange;
}

// Drainlock policy: the "drain" half comes first. Before the enemy is actually below the
// free-pull threshold, a pullkick-drain window keeps choosing drain. Once the free-pull
// finisher is live, preserve the existing pull-vs-drain weight comparison so the chosen
// action still reflects the computed scores.
static inline newbotai_drainlock_force_choice_t NewBotAI_GetDrainlockForceChoice(
	newbotai_drainlock_force_context_t context)
{
	if (!context.drainlockAdvantage && !context.pullkickDrainWindow)
	{
		return NEWBOTAI_DRAINLOCK_FORCE_NONE;
	}

	// Pull whenever it matches or beats drain (jundon pulls while the enemy still has force
	// and saves drain for when they are low): equal weights resolve to pull.
	if (context.pullWeight > context.minWeight &&
		context.pullWeight >= context.drainWeight)
	{
		return NEWBOTAI_DRAINLOCK_FORCE_PULL;
	}

	if ((context.drainlockAdvantage || context.pullkickDrainWindow) &&
		context.drainWeight > context.minWeight)
	{
		return NEWBOTAI_DRAINLOCK_FORCE_DRAIN;
	}

	return NEWBOTAI_DRAINLOCK_FORCE_NONE;
}

static inline int NewBotAI_ShouldBlockOrthogonalSaberSpecialInput(
	int upmove, int rightmove, int hasForwardMove, int isGrounded, int saberBusy, int attackPressed)
{
	if (upmove <= 0 || rightmove == 0 || hasForwardMove)
	{
		return 0;
	}

	if (!isGrounded || saberBusy || !attackPressed)
	{
		return 0;
	}

	return 1;
}

static inline int NewBotAI_IsJumpAttackSuppressionWindowActive(
	int currentTime, int jumpEventTime, int windowMs)
{
	if (jumpEventTime <= 0 || windowMs < 0)
	{
		return 0;
	}

	return (currentTime >= jumpEventTime &&
		currentTime <= jumpEventTime + windowMs) ? 1 : 0;
}

static inline void NewBotAI_ApplyJumpAttackSuppression(
	int suppressionActive, int *doAttack, int *doAltAttack)
{
	if (!suppressionActive)
	{
		return;
	}

	if (doAttack)
	{
		*doAttack = 0;
	}
	if (doAltAttack)
	{
		*doAltAttack = 0;
	}
}

static inline int NewBotAI_GetContextualStrafeFrequency(
	int baseFrequency, int conservingForce, int saberWeapon, int attacking, int nonSpeedForceActive)
{
	int frequency = baseFrequency;

	if (frequency < 0)
	{
		frequency = 0;
	}
	else if (frequency > 100)
	{
		frequency = 100;
	}

	if (conservingForce)
	{
		frequency += 30;
	}

	if (saberWeapon)
	{
		frequency += 10;
	}
	else
	{
		frequency -= 20;
	}

	if (attacking)
	{
		frequency -= 15;
	}

	if (nonSpeedForceActive)
	{
		frequency -= 30;
	}

	if (frequency < 0)
	{
		frequency = 0;
	}
	else if (frequency > 100)
	{
		frequency = 100;
	}

	return frequency;
}

static inline int NewBotAI_ShouldSkipPullForNaturalFlipkick(
	int pullUsable, int pullkickOpportunity, int canAttemptFlipkick, int flipkickSetupReady,
	float enemyDistance, float timeToRangeMs)
{
	if (!pullUsable || !pullkickOpportunity || !canAttemptFlipkick)
	{
		return 0;
	}

	if (flipkickSetupReady)
	{
		return 1;
	}

	if (enemyDistance <= 220.0f &&
		timeToRangeMs >= 0.0f &&
		timeToRangeMs <= 350.0f)
	{
		return 1;
	}

	return 0;
}

static inline int NewBotAI_GetDuelRequestCooldownMs(
	int defaultCooldownMs, int botVsBotCooldownMs, int extendedModeEnabled,
	int challengerIsBot, int targetIsBot)
{
	if (defaultCooldownMs < 1000)
	{
		defaultCooldownMs = 1000;
	}
	if (botVsBotCooldownMs < 1000)
	{
		botVsBotCooldownMs = 1000;
	}

	if (extendedModeEnabled && challengerIsBot && targetIsBot)
	{
		return botVsBotCooldownMs;
	}

	return defaultCooldownMs;
}

static inline int NewBotAI_ShouldIgnoreBotSaberLoss(int isBot, int noSaberDropEnabled)
{
	return (isBot && noSaberDropEnabled) ? 1 : 0;
}

static inline int NewBotAI_ShouldApplyFanWobble(
	int fanActive, int attackHeld, int elapsedMs, int delayMs,
	float yawAmplitude, float pitchAmplitude, float speed)
{
	if (!fanActive || !attackHeld)
	{
		return 0;
	}

	if (elapsedMs < delayMs)
	{
		return 0;
	}

	if (speed <= 0.0f || (yawAmplitude <= 0.0f && pitchAmplitude <= 0.0f))
	{
		return 0;
	}

	return 1;
}

static inline void NewBotAI_GetFanWobbleOffsets(
	float elapsedMsAfterDelay, float yawAmplitude, float pitchAmplitude, float speed,
	float *yawOffset, float *pitchOffset)
{
	float phase = (elapsedMsAfterDelay * 0.001f) * speed * 6.28318530718f;

	if (yawOffset)
	{
		*yawOffset = (yawAmplitude > 0.0f) ? cosf(phase) * yawAmplitude : 0.0f;
	}
	if (pitchOffset)
	{
		*pitchOffset = (pitchAmplitude > 0.0f) ? -sinf(phase) * pitchAmplitude : 0.0f;
	}
}

static inline int NewBotAI_ShouldForceImmediateSaberThrowHop(
	float timeToImpactMs, float forwardDist, int isReturning, float skill)
{
	if (timeToImpactMs <= 60.0f ||
		forwardDist <= (isReturning ? 12.0f : 18.0f))
	{
		return 1;
	}

	if (skill >= 6.0f &&
		(timeToImpactMs <= 80.0f ||
		 forwardDist <= (isReturning ? 16.0f : 24.0f)))
	{
		return 1;
	}

	return 0;
}

/* ------------------------------------------------------------------------------------
 * Dueltracks2 data-driven tuning (fan chain, footing, dodges, combos, reactions).
 * ------------------------------------------------------------------------------------ */

// Fan chain (sessions 34-48): human L2R/R2L chains link on the frame the previous swing
// ends (no dwell), sweep 100-160 degrees per ~300ms swing, and keep running while hits land.
#define NEWBOTAI_FAN_SWEEP_SWING_MS 300
#define NEWBOTAI_FAN_SWEEP_MAX_HALF_ARC 80.0f
#define NEWBOTAI_FAN_SWEEP_FULL_RANGE 110.0f
#define NEWBOTAI_FAN_SWEEP_FADE_RANGE 200.0f
#define NEWBOTAI_FAN_CHAIN_BASE_MS 3000
#define NEWBOTAI_FAN_CHAIN_EXTENDED_MS 6000
#define NEWBOTAI_FAN_CHAIN_HIT_WINDOW_MS 700
#define NEWBOTAI_FAN_LINK_SKILL 7.0f

// Gap between fan swings: zero at skill 7+, otherwise the configured dwell shrunk by skill
// and by fan bias (a bot told to fan hard also links faster).
static inline int NewBotAI_GetFanDwellMs(int baseDwellMs, float skill, float fanBiasPercent)
{
	float scale;

	if (baseDwellMs <= 0 || skill >= NEWBOTAI_FAN_LINK_SKILL)
		return 0;
	if (skill < 1.0f)
		skill = 1.0f;
	if (fanBiasPercent < 0.0f)
		fanBiasPercent = 0.0f;
	else if (fanBiasPercent > 100.0f)
		fanBiasPercent = 100.0f;
	scale = (NEWBOTAI_FAN_LINK_SKILL - skill) / (NEWBOTAI_FAN_LINK_SKILL - 1.0f);
	scale *= 1.0f - 0.5f * (fanBiasPercent / 100.0f);
	return (int)((float)baseDwellMs * scale);
}

// Half of the yaw arc swept during one horizontal swing. High-skill bots approach the human
// 55-80 degree half arc; the arc fades with range so the aim stays on a distant target.
static inline float NewBotAI_GetFanSweepHalfArc(float skill, float fanBiasPercent, float enemyDistance2D)
{
	float skillT;
	float arc;

	if (skill < 1.0f)
		skill = 1.0f;
	skillT = (skill - 1.0f) / (NEWBOTAI_FAN_LINK_SKILL - 1.0f);
	if (skillT > 1.0f)
		skillT = 1.0f;
	if (fanBiasPercent < 0.0f)
		fanBiasPercent = 0.0f;
	else if (fanBiasPercent > 100.0f)
		fanBiasPercent = 100.0f;
	arc = 20.0f + 50.0f * skillT + 10.0f * (fanBiasPercent / 100.0f);
	if (enemyDistance2D > NEWBOTAI_FAN_SWEEP_FULL_RANGE)
	{
		float fade = 1.0f - 0.5f * (enemyDistance2D - NEWBOTAI_FAN_SWEEP_FULL_RANGE) /
			(NEWBOTAI_FAN_SWEEP_FADE_RANGE - NEWBOTAI_FAN_SWEEP_FULL_RANGE);
		if (fade < 0.5f)
			fade = 0.5f;
		arc *= fade;
	}
	return (arc > NEWBOTAI_FAN_SWEEP_MAX_HALF_ARC) ? NEWBOTAI_FAN_SWEEP_MAX_HALF_ARC : arc;
}

// Absolute yaw offset from the enemy direction during a horizontal swing. sweepDir follows
// BG_SaberHorizontalSweepDir (L2R = +1 turns right / yaw decreasing). The swing starts on
// the far side (+halfArc for L2R), crosses the target mid-swing (when hits land, 150ms in)
// and ends on the other side, matching the human yaw sweep.
static inline float NewBotAI_GetFanSweepOffset(float halfArc, int sweepDir, int elapsedMs, int swingMs)
{
	float progress;

	if (!sweepDir || halfArc <= 0.0f || swingMs <= 0)
		return 0.0f;
	progress = (float)elapsedMs / (float)swingMs;
	if (progress < 0.0f)
		progress = 0.0f;
	else if (progress > 1.0f)
		progress = 1.0f;
	return (float)sweepDir * halfArc * (1.0f - 2.0f * progress);
}

// Chains end at the base 3 seconds unless the last hit landed within the hit window, in
// which case they may run on up to the extended cap.
static inline int NewBotAI_FanChainMayContinue(int chainElapsedMs, int msSinceLastHit)
{
	if (chainElapsedMs <= NEWBOTAI_FAN_CHAIN_BASE_MS)
		return 1;
	if (chainElapsedMs > NEWBOTAI_FAN_CHAIN_EXTENDED_MS)
		return 0;
	return (msSinceLastHit >= 0 && msSinceLastHit <= NEWBOTAI_FAN_CHAIN_HIT_WINDOW_MS) ? 1 : 0;
}

// A fan chain that is still landing hits links straight into the next swing and keeps
// driving forward instead of resetting to a dwell.
static inline int NewBotAI_FanChainIsLanding(int msSinceLastHit)
{
	return (msSinceLastHit >= 0 && msSinceLastHit <= NEWBOTAI_FAN_CHAIN_HIT_WINDOW_MS) ? 1 : 0;
}

static inline int NewBotAI_GetFanLinkDwellMs(int dwellMs, int msSinceLastHit)
{
	return NewBotAI_FanChainIsLanding(msSinceLastHit) ? 0 : dwellMs;
}

// Saber-only planner sweep: humans swept ~60-70 degrees per swing at 200-450 deg/s
// (dueltracks2 1c34db989), starting ~20 degrees off the target on the swing's starting
// side. Amplitude is half the swept arc and scales with skill and fan bias.
#define NEWBOTAI_SABER_SWEEP_MIN_HALF_ARC 25.0f
#define NEWBOTAI_SABER_SWEEP_MAX_HALF_ARC 35.0f
#define NEWBOTAI_SABER_SWEEP_MIN_RATE 200.0f
#define NEWBOTAI_SABER_SWEEP_MAX_RATE 450.0f
#define NEWBOTAI_SABER_SWING_START_OFFSET 20.0f

static inline float NewBotAI_GetSaberSweepScale(float skill, float fanBiasPercent)
{
	float skillT;

	if (skill < 1.0f)
		skill = 1.0f;
	skillT = (skill - 1.0f) / (NEWBOTAI_FAN_LINK_SKILL - 1.0f);
	if (skillT > 1.0f)
		skillT = 1.0f;
	if (fanBiasPercent < 0.0f)
		fanBiasPercent = 0.0f;
	else if (fanBiasPercent > 100.0f)
		fanBiasPercent = 100.0f;
	return skillT * (0.5f + 0.5f * fanBiasPercent / 100.0f);
}

static inline float NewBotAI_GetSaberSweepAmplitude(float skill, float fanBiasPercent)
{
	return NEWBOTAI_SABER_SWEEP_MIN_HALF_ARC + (NEWBOTAI_SABER_SWEEP_MAX_HALF_ARC -
		NEWBOTAI_SABER_SWEEP_MIN_HALF_ARC) * NewBotAI_GetSaberSweepScale(skill, fanBiasPercent);
}

static inline float NewBotAI_GetSaberSweepRateScaled(float skill, float fanBiasPercent)
{
	return NEWBOTAI_SABER_SWEEP_MIN_RATE + (NEWBOTAI_SABER_SWEEP_MAX_RATE -
		NEWBOTAI_SABER_SWEEP_MIN_RATE) * NewBotAI_GetSaberSweepScale(skill, fanBiasPercent);
}

// Rate needed to cover the full arc in one swing, kept inside the human envelope.
static inline float NewBotAI_GetSaberSweepRate(float amplitude)
{
	const float rate = amplitude * 2.0f * 1000.0f / (float)NEWBOTAI_FAN_SWEEP_SWING_MS;
	return (rate < NEWBOTAI_SABER_SWEEP_MIN_RATE) ? NEWBOTAI_SABER_SWEEP_MIN_RATE :
		(rate > NEWBOTAI_SABER_SWEEP_MAX_RATE) ? NEWBOTAI_SABER_SWEEP_MAX_RATE : rate;
}

// Approaching without attacking, humans held aim ~13 degrees off the target (bots ~8).
#define NEWBOTAI_APPROACH_AIM_MIN_OFFSET 10.0f
#define NEWBOTAI_APPROACH_AIM_MAX_OFFSET 15.0f
#define NEWBOTAI_APPROACH_AIM_MIN_RANGE 60.0f
#define NEWBOTAI_APPROACH_AIM_MAX_RANGE 400.0f

static inline float NewBotAI_GetApproachAimOffset(float skill, float enemyDistance2D, int side)
{
	float skillT;

	if (!side || enemyDistance2D <= NEWBOTAI_APPROACH_AIM_MIN_RANGE ||
		enemyDistance2D > NEWBOTAI_APPROACH_AIM_MAX_RANGE)
		return 0.0f;
	if (skill < 1.0f)
		skill = 1.0f;
	skillT = (skill - 1.0f) / 9.0f;
	if (skillT > 1.0f)
		skillT = 1.0f;
	return (side < 0 ? -1.0f : 1.0f) * (NEWBOTAI_APPROACH_AIM_MIN_OFFSET +
		(NEWBOTAI_APPROACH_AIM_MAX_OFFSET - NEWBOTAI_APPROACH_AIM_MIN_OFFSET) * skillT);
}

// Yaw offset from the target: prepare ~20 degrees off on the starting side, then sweep the
// full 2*amplitude arc across the target during the active part of the swing.
static inline float NewBotAI_SaberYawOffsetScaled(newbotai_saber_yaw_phase_t phase,
	float progress, int actualDirection, int desiredDirection, int sweep, float amplitude)
{
	const float start = (amplitude < NEWBOTAI_SABER_SWING_START_OFFSET) ?
		amplitude : NEWBOTAI_SABER_SWING_START_OFFSET;
	if (!sweep || phase == NEWBOTAI_SABER_YAW_RECOVER)
		return 0.0f;
	if (phase == NEWBOTAI_SABER_YAW_NEXT)
		return desiredDirection * start;
	if (phase == NEWBOTAI_SABER_YAW_PREPARE)
		return (actualDirection ? actualDirection : desiredDirection) * start;
	progress = (progress - 0.15f) / 0.65f;
	if (progress < 0.0f) progress = 0.0f;
	if (progress > 1.0f) progress = 1.0f;
	return actualDirection * (start - 2.0f * amplitude * progress);
}

static inline float NewBotAI_SaberStepYawOffsetScaled(float current, float target, int elapsedMs,
	float amplitude, float rate)
{
	float delta;
	float step;

	// The active sweep ends 2*amplitude - start past the target.
	if (target > 2.0f * amplitude) target = 2.0f * amplitude;
	if (target < -2.0f * amplitude) target = -2.0f * amplitude;
	if (elapsedMs < 0) elapsedMs = 0;
	step = rate * (float)elapsedMs / 1000.0f;
	delta = target - current;
	if (delta > step) delta = step;
	if (delta < -step) delta = -step;
	return current + delta;
}

// Footing (dueltracks3: human hits start at a median 71u predicted range, p25-75 50-99u,
// while closing at +170-250u/s; failed human swings started from 120-140u closing ~+30u/s):
// start when the predicted distance ~150ms in is inside reach, or inside 70-100u while
// closing faster than ~150u/s. Beyond ~130u keep stepping in (bait) rather than swing, and
// never swing while backing away from 100u+.
#define NEWBOTAI_SWING_PEAK_LEAD_MS 150
#define NEWBOTAI_SWING_PEAK_RANGE 70.0f
#define NEWBOTAI_SWING_PEAK_RANGE_STAFF 70.0f
#define NEWBOTAI_SWING_LINK_RANGE 85.0f
#define NEWBOTAI_SWING_STEP_IN_RANGE 85.0f
#define NEWBOTAI_SWING_BACKING_BLOCK_RANGE 100.0f
#define NEWBOTAI_SWING_BACKING_SPEED 40.0f
#define NEWBOTAI_SWING_START_WINDOW_MAX 100.0f
#define NEWBOTAI_SWING_START_CLOSING_SPEED 150.0f
#define NEWBOTAI_SWING_BAIT_RANGE 130.0f

typedef enum
{
	NEWBOTAI_SWING_FOOTING_START = 0,
	NEWBOTAI_SWING_FOOTING_STEP_IN,
	NEWBOTAI_SWING_FOOTING_HOLD
} newbotai_swing_footing_t;

// 2D distance after leadMs given the 2D offset to the enemy and the relative velocity
// (enemy velocity minus ours).
static inline float NewBotAI_PredictRange2D(float dx, float dy, float relVx, float relVy, int leadMs)
{
	const float t = (float)leadMs / 1000.0f;
	const float px = dx + relVx * t;
	const float py = dy + relVy * t;
	return sqrtf(px * px + py * py);
}

// Fresh (non-linked) swing start window: inside the peak range always, 70-100u only while
// closing faster than NEWBOTAI_SWING_START_CLOSING_SPEED, never from beyond ~130u.
static inline int NewBotAI_SwingStartWindowAllows(float currentRange, float predictedRange,
	float radialSpeed, float peakRange)
{
	if (currentRange > NEWBOTAI_SWING_BAIT_RANGE)
		return 0;
	if (predictedRange <= peakRange)
		return 1;
	return (predictedRange <= NEWBOTAI_SWING_START_WINDOW_MAX &&
		radialSpeed >= NEWBOTAI_SWING_START_CLOSING_SPEED) ? 1 : 0;
}

// radialSpeed > 0 means we are closing on the enemy. linkedSwing relaxes the peak range for
// a chain continuation that is already committed.
static inline newbotai_swing_footing_t NewBotAI_GetSwingFooting(float currentRange, float predictedRange,
	float radialSpeed, int linkedSwing, int staff)
{
	const float peakRange = linkedSwing ? NEWBOTAI_SWING_LINK_RANGE :
		(staff ? NEWBOTAI_SWING_PEAK_RANGE_STAFF : NEWBOTAI_SWING_PEAK_RANGE);

	if (currentRange >= NEWBOTAI_SWING_BACKING_BLOCK_RANGE && radialSpeed < -NEWBOTAI_SWING_BACKING_SPEED)
		return NEWBOTAI_SWING_FOOTING_HOLD;
	if (linkedSwing)
		return predictedRange <= peakRange ? NEWBOTAI_SWING_FOOTING_START : NEWBOTAI_SWING_FOOTING_STEP_IN;
	// Otherwise keep stepping in (the caller adds forward input) until the window opens.
	return NewBotAI_SwingStartWindowAllows(currentRange, predictedRange, radialSpeed, peakRange) ?
		NEWBOTAI_SWING_FOOTING_START : NEWBOTAI_SWING_FOOTING_STEP_IN;
}

// Fresh swing starts (ATTACK/STAND_SWING/STEP_IN) follow the human start window when the
// closing speed is known: inside 70u, or 70-100u while closing >150u/s, never beyond 130u.
// Chains, counters and deliberate mistakes keep their own range limits.
static inline int NewBotAI_SaberFreshSwingWindowAllows(newbotai_saber_tactic_context_t context,
	newbotai_saber_tactic_t tactic)
{
	if (!context.closingKnown || context.enemyVulnerable)
		return 1;
	switch (tactic)
	{
	case NEWBOTAI_SABER_TACTIC_ATTACK:
	case NEWBOTAI_SABER_TACTIC_STAND_SWING:
	case NEWBOTAI_SABER_TACTIC_STEP_IN:
		return NewBotAI_SwingStartWindowAllows(context.currentDistance, context.enemyDistance,
			context.closingSpeed, NEWBOTAI_SWING_PEAK_RANGE);
	default:
		return 1;
	}
}

// Dodging an enemy swing that starts within 130u: stationary/backpedal defenders were hit
// 28-36%, a fast lateral strafe 7-9%, airborne 3-5%. Backpedal is a low-skill mistake only.
#define NEWBOTAI_SWING_DODGE_RANGE 130.0f

typedef enum
{
	NEWBOTAI_SWING_DODGE_NONE = 0,
	NEWBOTAI_SWING_DODGE_LATERAL,
	NEWBOTAI_SWING_DODGE_JUMP,
	NEWBOTAI_SWING_DODGE_BACKPEDAL
} newbotai_swing_dodge_t;

static inline newbotai_swing_dodge_t NewBotAI_GetSwingDodgeChoice(float enemyDistance2D, int grounded,
	int canJump, int mistakeChance, int mistakeRoll, int styleRoll)
{
	if (enemyDistance2D > NEWBOTAI_SWING_DODGE_RANGE)
		return NEWBOTAI_SWING_DODGE_NONE;
	if (mistakeChance > 0 && mistakeRoll > 0 && mistakeRoll <= mistakeChance)
		return NEWBOTAI_SWING_DODGE_BACKPEDAL;
	if (grounded && canJump && styleRoll > 0 && styleRoll <= 25)
		return NEWBOTAI_SWING_DODGE_JUMP;
	return NEWBOTAI_SWING_DODGE_LATERAL;
}

// Airborne swings hit 8% vs 17% grounded and bots landed 0 of 29: only allow jump attacks
// into a knocked-down/recovering enemy, or as a low-skill mistake.
static inline int NewBotAI_AllowAirborneSwingStart(int enemyKnockedOrRecovering, int mistakeChance, int roll)
{
	if (enemyKnockedOrRecovering)
		return 1;
	return (mistakeChance > 0 && roll > 0 && roll <= mistakeChance) ? 1 : 0;
}

// Combo gaps measured from jundon/void: pull->kick 0ms, throw->pull ~66ms, grip->throw
// ~100ms, drain->pull/throw ~66ms. Skill 7+ uses those directly; lower skills add 100-250ms
// plus the configured extra delay and a mistake-bias delay.
typedef enum
{
	NEWBOTAI_COMBO_PULL_KICK = 0,
	NEWBOTAI_COMBO_THROW_PULL,
	NEWBOTAI_COMBO_GRIP_THROW,
	NEWBOTAI_COMBO_DRAIN_FOLLOWUP,
	NEWBOTAI_COMBO_COUNT
} newbotai_combo_t;

static inline int NewBotAI_GetComboBaseGapMs(newbotai_combo_t combo)
{
	switch (combo)
	{
	case NEWBOTAI_COMBO_THROW_PULL:
		return 66;
	case NEWBOTAI_COMBO_GRIP_THROW:
		return 100;
	case NEWBOTAI_COMBO_DRAIN_FOLLOWUP:
		return 66;
	case NEWBOTAI_COMBO_PULL_KICK:
	default:
		return 0;
	}
}

static inline int NewBotAI_GetComboGapMs(newbotai_combo_t combo, float skill, int extraDelayMs, int mistakeChance)
{
	int gap = NewBotAI_GetComboBaseGapMs(combo);

	if (skill >= NEWBOTAI_FAN_LINK_SKILL)
		return gap;
	if (skill < 1.0f)
		skill = 1.0f;
	gap += 100 + (int)((6.0f - (skill > 6.0f ? 6.0f : skill)) * 30.0f);
	if (extraDelayMs > 0)
		gap += extraDelayMs;
	if (mistakeChance > 0)
		gap += (mistakeChance > 100 ? 100 : mistakeChance) * 2;
	return gap;
}

// Reaction re-weighting from the stimulus -> response outcomes (net damage over 2s):
// enemy drain -> pull (+20) / throw (+8..11), push -11; enemy grip -> throw/pull, late push -5;
// enemy throw -> counter-throw (+14..27), push -17; enemy pull -> throw/pull back, drain -20;
// enemy push -> instant drain (+11).
static inline int NewBotAI_GetReactionBonus(int enemyAction, int response)
{
	switch (enemyAction)
	{
	case BOTLEARN_TOK_DRAIN:
		if (response == BOTLEARN_TOK_PULL) return 35;
		if (response == BOTLEARN_TOK_THROW) return 20;
		if (response == BOTLEARN_TOK_PUSH) return -25;
		return 0;
	case BOTLEARN_TOK_GRIP:
		if (response == BOTLEARN_TOK_THROW) return 30;
		if (response == BOTLEARN_TOK_PULL) return 25;
		if (response == BOTLEARN_TOK_PUSH) return -20;
		return 0;
	case BOTLEARN_TOK_THROW:
		if (response == BOTLEARN_TOK_THROW) return 30;
		if (response == BOTLEARN_TOK_PUSH) return -20;
		if (response == BOTLEARN_TOK_JUMP) return -20;
		return 0;
	case BOTLEARN_TOK_PULL:
		if (response == BOTLEARN_TOK_THROW) return 25;
		if (response == BOTLEARN_TOK_PULL) return 25;
		if (response == BOTLEARN_TOK_DRAIN) return -20;
		if (response == BOTLEARN_TOK_JUMP) return -20;
		return 0;
	case BOTLEARN_TOK_PUSH:
		if (response == BOTLEARN_TOK_DRAIN) return 35;
		return 0;
	default:
		return 0;
	}
}

// Force economy (jundon pulls at enemy FP ~65, drains at enemy FP ~18): pull while the enemy
// still has force, save drain for when they are low.
static inline int NewBotAI_GetForceEconomyBonus(int response, int enemyForce)
{
	if (response == BOTLEARN_TOK_PULL)
		return (enemyForce >= 50) ? 15 : 0;
	if (response == BOTLEARN_TOK_DRAIN)
	{
		if (enemyForce <= 30)
			return 15;
		return (enemyForce >= 60) ? -10 : 0;
	}
	return 0;
}

// Saber-throw situational bias. Human throws hit 46-61% at every force level, bots 27-30%;
// the differences are the target state and what came right before: throws into a jumping
// target hit 18-27%, throws right after our own pull/grip/swing/kick 6-22%, throws at an
// idle or committed target 57-71%. Bots at high force from mid range were net negative.
static inline int NewBotAI_GetSaberThrowSituationBonus(int ourForce, float enemyDistance,
	int enemyAirborne, int enemyKnockedDown, int ownActionRecent, int enemyCommitted)
{
	int bonus = 0;

	if (enemyAirborne && !enemyKnockedDown)
		bonus -= 25;
	if (ownActionRecent)
		bonus -= 20;
	if (enemyCommitted)
		bonus += 15;
	if (ourForce <= 50)
		bonus += 10;
	else if (ourForce > 75 && enemyDistance >= 130.0f && enemyDistance <= 260.0f && !enemyCommitted)
		bonus -= 10;
	return bonus;
}

// Saber-throw hold (dueltracks2 1c34db989): human throw hit rate rises with alt hold
// (<=100ms 41%, 500-1000ms ~80%, >1s 85-89%), the saber passes the target ~630ms in,
// and holding past the pass landed 0.99 return-pass hits vs 0.11. Hold at least 600ms
// and through the first pass while it still heads toward the target, up to 1.5s.
#define NEWBOTAI_THROW_MIN_HOLD_MS 600
#define NEWBOTAI_THROW_MAX_PASS_HOLD_MS 1500
#define NEWBOTAI_THROW_TRAIL_MIN_DEG 5.0f
#define NEWBOTAI_THROW_TRAIL_MAX_DEG 10.0f
#define NEWBOTAI_THROW_PASS_SWITCH_DEG 9.0f
#define NEWBOTAI_THROW_TRAIL_RANGE 400.0f

static inline int NewBotAI_SaberThrowHoldProtected(int heldMs, int passedTarget, int headingToTarget)
{
	if (heldMs < NEWBOTAI_THROW_MIN_HOLD_MS)
		return 1;
	return (!passedTarget && headingToTarget && heldMs < NEWBOTAI_THROW_MAX_PASS_HOLD_MS) ? 1 : 0;
}

// Early-release rules only apply after the hold; drain-lock and lethal danger always may.
static inline int NewBotAI_SaberThrowMayRelease(int heldMs, int passedTarget, int headingToTarget,
	int drainlockRule, int lethalDanger)
{
	if (drainlockRule || lethalDanger)
		return 1;
	return !NewBotAI_SaberThrowHoldProtected(heldMs, passedTarget, headingToTarget);
}

// Yaw offset (degrees, positive toward the target's lateral motion) for a thrown saber:
// humans trail the moving target by 5-10 degrees until the saber passes it, then switch
// ~9 degrees to the other side so the homing return cuts back through the target.
static inline float NewBotAI_GetSaberThrowAimOffset(int lateralMotionSide, int passedTarget, float enemyDistance)
{
	float t;

	if (!lateralMotionSide)
		return 0.0f;
	if (passedTarget)
		return (lateralMotionSide < 0 ? -1.0f : 1.0f) * NEWBOTAI_THROW_PASS_SWITCH_DEG;
	t = (NEWBOTAI_THROW_TRAIL_RANGE - enemyDistance) / NEWBOTAI_THROW_TRAIL_RANGE;
	if (t < 0.0f)
		t = 0.0f;
	else if (t > 1.0f)
		t = 1.0f;
	return (lateralMotionSide < 0 ? 1.0f : -1.0f) * (NEWBOTAI_THROW_TRAIL_MIN_DEG +
		(NEWBOTAI_THROW_TRAIL_MAX_DEG - NEWBOTAI_THROW_TRAIL_MIN_DEG) * t);
}

#endif
