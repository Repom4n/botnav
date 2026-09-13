#ifndef AI_COMBAT_TUNING_H
#define AI_COMBAT_TUNING_H

typedef enum
{
	NEWBOTAI_DRAINLOCK_FORCE_NONE = 0,
	NEWBOTAI_DRAINLOCK_FORCE_PULL,
	NEWBOTAI_DRAINLOCK_FORCE_DRAIN
} newbotai_drainlock_force_choice_t;

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

static inline newbotai_drainlock_force_choice_t NewBotAI_GetDrainlockForceChoice(
	int minWeight, int drainlockAdvantage, int pullkickDrainWindow, int drainWeight, int pullWeight, int enemyForce)
{
	if (!drainlockAdvantage && !pullkickDrainWindow)
	{
		return NEWBOTAI_DRAINLOCK_FORCE_NONE;
	}

	if (pullWeight > minWeight && (enemyForce < 20 || pullWeight >= drainWeight))
	{
		return NEWBOTAI_DRAINLOCK_FORCE_PULL;
	}

	if (drainlockAdvantage && drainWeight > minWeight)
	{
		return NEWBOTAI_DRAINLOCK_FORCE_DRAIN;
	}

	return NEWBOTAI_DRAINLOCK_FORCE_NONE;
}

static inline int NewBotAI_ShouldBlockOrthogonalSaberSpecialInput(
	int upmove, int rightmove, int forwardmove, int isGrounded, int saberBusy, int attackPressed)
{
	if (upmove <= 0 || rightmove == 0 || forwardmove != 0)
	{
		return 0;
	}

	if (!isGrounded || saberBusy || !attackPressed)
	{
		return 0;
	}

	return 1;
}

#endif
