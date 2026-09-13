#ifndef AI_COMBAT_TUNING_H
#define AI_COMBAT_TUNING_H

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

// Drainlock policy: the "drain" half comes first. Once the enemy is actually below the
// free-pull threshold, pull becomes the finisher; before then, even a pullkick-drain
// window should keep choosing drain until the force advantage has been cashed into a free
// pullkick.
static inline newbotai_drainlock_force_choice_t NewBotAI_GetDrainlockForceChoice(
	newbotai_drainlock_force_context_t context)
{
	if (!context.drainlockAdvantage && !context.pullkickDrainWindow)
	{
		return NEWBOTAI_DRAINLOCK_FORCE_NONE;
	}

	if (context.enemyForce < 20 &&
		context.pullWeight >= context.minWeight)
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

static inline int NewBotAI_ShouldIgnoreBotSaberLoss(int isBot, int noSaberDropEnabled)
{
	return (isBot && noSaberDropEnabled) ? 1 : 0;
}

#endif
