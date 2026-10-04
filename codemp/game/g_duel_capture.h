#ifndef G_DUEL_CAPTURE_H
#define G_DUEL_CAPTURE_H

#include <string.h>
#include <stdlib.h>

#define DUEL_CAPTURE_SAMPLE_MS 50
#define DUEL_CAPTURE_RECENT_CHAT_MS 15000

static inline int G_DuelCapturePublicChat(int mode, int publicMode, int targeted)
{
	return mode == publicMode && !targeted;
}

static inline int G_DuelCaptureRecoveryReentry(int damage, int recovery,
	int radialSpeed, int forwardmove, int rightmove)
{
	return damage > 0 && recovery && radialSpeed >= 40 && forwardmove > 0 && !rightmove;
}

static inline int G_DuelCaptureSampleDue(int now, int lastSample, int activeWindow)
{
	return activeWindow && (lastSample < 0 || now - lastSample >= DUEL_CAPTURE_SAMPLE_MS);
}

static inline int G_DuelCaptureRecentChat(int now, int finishedAt, int sameIdentity)
{
	return sameIdentity && finishedAt >= 0 && now >= finishedAt &&
		now - finishedAt <= DUEL_CAPTURE_RECENT_CHAT_MS;
}

/* Retain all public text up to the chat protocol limit; never interpret it as commands. */
static inline void G_DuelCaptureSanitizeText(const char *input, char *output, size_t size)
{
	size_t n = 0;
	if (!size)
		return;
	if (input)
	{
		while (*input && n + 1 < size)
		{
			unsigned char c = (unsigned char)*input++;
			if (c < 32 || c == 127)
				c = ' ';
			output[n++] = (char)c;
		}
	}
	output[n] = '\0';
}

typedef struct
{
	int capacity;
	unsigned long long total;
	unsigned long long dropped;
	unsigned long long criticalDropped;
	unsigned int compactions;
	unsigned int allocationFailures;
} duel_capture_storage_t;

/* Transfer a capture runtime's owned buffer to an empty destination, leaving
 * the source safe for reset/reuse while persistence still reads the copy. */
static inline void G_DuelCaptureMoveRuntime(void *destination, void *source, size_t size)
{
	memcpy(destination, source, size);
	memset(source, 0, size);
}

static inline void G_DuelCaptureClearRuntime(void *runtime, size_t size, void *records)
{
	free(records);
	memset(runtime, 0, size);
}

static inline int G_DuelCaptureCanRankOutcomes(const duel_capture_storage_t *storage)
{
	return !storage->dropped && !storage->criticalDropped;
}

static inline void G_DuelCaptureSuppressSequenceRanking(const duel_capture_storage_t *storage,
	char *goodSequence, char *badSequence)
{
	if (!G_DuelCaptureCanRankOutcomes(storage))
		goodSequence[0] = badSequence[0] = '\0';
}

/* Keep the first record and newest quarter intact. Fill the remaining half-buffer
 * budget with evenly spaced records, highest priority first, in original order. */
static inline int G_DuelCaptureCompact(void *records, int count, size_t stride,
	duel_capture_storage_t *storage, int (*priority)(const void *))
{
	int totals[3] = { 0, 0, 0 }, quotas[3], seen[3] = { 0, 0, 0 };
	int recent = count - count / 4, budget = count / 2 - (count - recent) - 1;
	int i, tier, retained = 0;
	unsigned char *bytes = (unsigned char *)records;
	for (i = 1; i < recent; ++i)
		++totals[priority(bytes + i * stride)];
	for (tier = 2; tier >= 0; --tier)
	{
		quotas[tier] = totals[tier] < budget ? totals[tier] : budget;
		budget -= quotas[tier];
	}
	for (i = 0; i < count; ++i)
	{
		int keep = i == 0 || i >= recent;
		tier = priority(bytes + i * stride);
		if (!keep)
		{
			keep = ((long long)(seen[tier] + 1) * quotas[tier] / totals[tier]) !=
				((long long)seen[tier] * quotas[tier] / totals[tier]);
			++seen[tier];
		}
		if (keep)
		{
			if (retained != i)
				memmove(bytes + retained * stride, bytes + i * stride, stride);
			++retained;
		}
		else
		{
			++storage->dropped;
			if (tier == 2)
				++storage->criticalDropped;
		}
	}
	++storage->compactions;
	return retained;
}

/* A failed growth still compacts existing storage; it never freezes the tail. */
static inline void *G_DuelCaptureReserveWithAllocator(void *records, int *count, size_t stride,
	duel_capture_storage_t *storage, int maximum, int (*priority)(const void *),
	void *(*allocate)(void *, size_t))
{
	if (maximum <= 0 || !stride || *count < 0 || storage->capacity < 0 ||
		*count > storage->capacity)
		return records;
	if (*count == storage->capacity && storage->capacity < maximum)
	{
		int capacity = storage->capacity ? storage->capacity * 2 : 128;
		void *grown;
		if (capacity > maximum)
			capacity = maximum;
		grown = allocate(records, (size_t)capacity * stride);
		if (grown)
		{
			records = grown;
			storage->capacity = capacity;
		}
		else
			++storage->allocationFailures;
	}
	if (*count == storage->capacity && *count >= 4)
		*count = G_DuelCaptureCompact(records, *count, stride, storage, priority);
	return records;
}

static inline void *G_DuelCaptureReserve(void *records, int *count, size_t stride,
	duel_capture_storage_t *storage, int maximum, int (*priority)(const void *))
{
	return G_DuelCaptureReserveWithAllocator(records, count, stride, storage, maximum, priority, realloc);
}

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

static inline int G_DuelCaptureHasIndexGap(unsigned long long previous, unsigned long long next)
{
	return next != previous + 1;
}

static inline void G_DuelCaptureAccumulateOutcome(duel_capture_outcome_t *outcome,
	unsigned long long attackIndex, unsigned long long endIndex, int startTime, int endTime,
	unsigned long long eventIndex, int eventTime, int kind, int amount, int killedEnemy)
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

static inline const char *G_DuelCaptureRankedOutcomeQuality(const duel_capture_storage_t *storage,
	const duel_capture_outcome_t *outcome)
{
	return G_DuelCaptureCanRankOutcomes(storage) ? G_DuelCaptureOutcomeQuality(outcome) : "unknown";
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
