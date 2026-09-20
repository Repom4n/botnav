#ifndef AI_COMBAT_TUNING_H
#define AI_COMBAT_TUNING_H

#define NEWBOTAI_TUNING_ESCAPE_YAW_SPEED 333.0f

typedef enum
{
	NEWBOTAI_DRAINLOCK_FORCE_NONE = 0,
	NEWBOTAI_DRAINLOCK_FORCE_PULL,
	NEWBOTAI_DRAINLOCK_FORCE_DRAIN
} newbotai_drainlock_force_choice_t;

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

	if (lastPromptDuel == duelsSeen)
	{
		return 0;
	}

	return ((duelsSeen - startAtDuel) % repeatInterval) == 0 ? 1 : 0;
}

static inline float NewBotAI_GetImmediateFlipkickYawTolerance(int immediateContact)
{
	return immediateContact ? 60.0f : 35.0f;
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
