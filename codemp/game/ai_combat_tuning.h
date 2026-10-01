#ifndef AI_COMBAT_TUNING_H
#define AI_COMBAT_TUNING_H

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

// Outcome grades derived from human saber-only duels (dueltrack sessions 44-172):
// correct = counter within ~600ms when hit / keep chaining after landing a hit / step in
// while swinging; good = short controlled chain then reposition; mediocre = stand and
// swing at 96-128u; bad = retreat after landing a hit or hold still when hit; mistake =
// swing beyond 150u or back away while swinging.
typedef enum
{
	NEWBOTAI_SABER_GRADE_CORRECT = 0,
	NEWBOTAI_SABER_GRADE_GOOD,
	NEWBOTAI_SABER_GRADE_MEDIOCRE,
	NEWBOTAI_SABER_GRADE_BAD,
	NEWBOTAI_SABER_GRADE_MISTAKE
} newbotai_saber_grade_t;

#define NEWBOTAI_SABER_ATTACK_RANGE 96.0f
#define NEWBOTAI_SABER_STEP_IN_RANGE 160.0f
#define NEWBOTAI_SABER_SWING_START_MAX_RANGE 128.0f
#define NEWBOTAI_SABER_LONG_SWING_RANGE 150.0f
#define NEWBOTAI_SABER_CRITICAL_TOTAL_HEALTH 30
#define NEWBOTAI_SABER_COUNTER_WINDOW_MS 600
#define NEWBOTAI_SABER_GOOD_CHAIN_LENGTH 2

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

// Winner-derived choice for the current saber-only duel state.
static inline newbotai_saber_tactic_t NewBotAI_GetCorrectSaberTactic(
	newbotai_saber_tactic_context_t context)
{
	const float dist = context.enemyDistance;
	const int inSwingReach = (dist <= NEWBOTAI_SABER_SWING_START_MAX_RANGE) ? 1 : 0;

	if (!context.saberOnlyDuel)
		return NEWBOTAI_SABER_TACTIC_HOLD;

	//Retreat only at critical health while losing the exchange.
	if (!context.enemyVulnerable &&
		context.ourTotalHealth <= NEWBOTAI_SABER_CRITICAL_TOTAL_HEALTH &&
		context.enemyTotalHealth > context.ourTotalHealth &&
		(context.recentlyHurt || context.enemyAttacking))
	{
		return NEWBOTAI_SABER_TACTIC_RESET;
	}

	if (context.enemyVulnerable)
		return inSwingReach ? NEWBOTAI_SABER_TACTIC_COUNTER : NEWBOTAI_SABER_TACTIC_ADVANCE;

	//Countering right after being hit lands a return hit ~50% of the time versus ~18% for
	//retreating, and retreating is not safer - so counter is the default.
	if (context.recentlyHurt ||
		(context.enemyAttacking && dist <= NEWBOTAI_SABER_STEP_IN_RANGE))
	{
		return inSwingReach ? NEWBOTAI_SABER_TACTIC_COUNTER : NEWBOTAI_SABER_TACTIC_STEP_IN;
	}

	//Continuing after a landed hit doubles the follow-up hit rate; no fixed chain cap.
	if (context.selfAttacking || context.landedHit)
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
				NEWBOTAI_SABER_TACTIC_STAND_SWING : NEWBOTAI_SABER_TACTIC_HOLD;
		default:
			return tactic;
		}
	case NEWBOTAI_SABER_GRADE_BAD:
		return (tactic == NEWBOTAI_SABER_TACTIC_CHAIN) ?
			NEWBOTAI_SABER_TACTIC_RESET : NEWBOTAI_SABER_TACTIC_HOLD;
	case NEWBOTAI_SABER_GRADE_MISTAKE:
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
	if (!context.saberOnlyDuel)
		return NEWBOTAI_SABER_TACTIC_HOLD;

	return NewBotAI_ApplySaberChoiceGrade(context,
		NewBotAI_GetCorrectSaberTactic(context),
		NewBotAI_GetSaberChoiceGrade(context.skill, context.mistakeBias, context.mistakeRoll));
}

// Saber-only footwork: tactics that may start a new swing this frame. Swing starts are
// limited to NEWBOTAI_SABER_SWING_START_MAX_RANGE except for the LONG_SWING mistake.
static inline int NewBotAI_SaberTacticAllowsSwingStart(newbotai_saber_tactic_t tactic, float enemyDistance)
{
	switch (tactic)
	{
	case NEWBOTAI_SABER_TACTIC_LONG_SWING:
		return 1;
	case NEWBOTAI_SABER_TACTIC_ATTACK:
	case NEWBOTAI_SABER_TACTIC_CHAIN:
	case NEWBOTAI_SABER_TACTIC_COUNTER:
	case NEWBOTAI_SABER_TACTIC_STEP_IN:
	case NEWBOTAI_SABER_TACTIC_STAND_SWING:
	case NEWBOTAI_SABER_TACTIC_BACK_SWING:
		return (enemyDistance <= NEWBOTAI_SABER_SWING_START_MAX_RANGE) ? 1 : 0;
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

	// Once the enemy is truly below the free-pull threshold, preserve the legacy tie-break:
	// equal pull/drain weights still resolve to pull so the finisher can fire immediately.
	if (context.enemyForce < 20 &&
		context.pullWeight >= context.minWeight &&
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

#endif
