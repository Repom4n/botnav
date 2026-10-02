#ifndef G_BOT_LEARNING_H
#define G_BOT_LEARNING_H

/*
 * Pure (engine-free) helpers for the bot sequence-learning system.
 *
 * At the end of every tracked duel each participant's event stream is turned into short
 * sequences: an opponent stimulus (or IDLE when we acted on our own initiative), our
 * response, and up to two follow-ups. Each sequence is tagged with a context key built
 * from HP+armor / force / range / stance buckets for both sides at the moment we chose
 * the response, and scored by the net damage over the next two seconds plus the duel
 * result. The aggregated counts are reloaded on map start and feed a capped, skill-scaled
 * bonus into the bot's existing reaction/combo weights.
 */

typedef enum
{
	BOTLEARN_TOK_NONE = 0,
	BOTLEARN_TOK_IDLE,
	BOTLEARN_TOK_PUSH,
	BOTLEARN_TOK_PULL,
	BOTLEARN_TOK_GRIP,
	BOTLEARN_TOK_DRAIN,
	BOTLEARN_TOK_THROW,
	BOTLEARN_TOK_SWING,
	BOTLEARN_TOK_KICK,
	BOTLEARN_TOK_JUMP,
	BOTLEARN_TOK_KNOCKDOWN,
	BOTLEARN_TOK_COUNT
} botlearn_token_t;

#define BOTLEARN_RESPONSE_WINDOW_MS 700
#define BOTLEARN_FOLLOWUP_WINDOW_MS 1000
#define BOTLEARN_OUTCOME_WINDOW_MS 2000
#define BOTLEARN_IDLE_GAP_MS 1000
#define BOTLEARN_REPEAT_COLLAPSE_MS 400
#define BOTLEARN_MAX_FOLLOWUPS 2
#define BOTLEARN_PRIOR_SAMPLES 4.0f
#define BOTLEARN_DEFAULT_MIN_SAMPLES 6
#define BOTLEARN_BONUS_CAP 30
#define BOTLEARN_COARSE_KEY_FLAG 0x8000

typedef enum
{
	BOTLEARN_SOURCE_HUMAN = 0,
	BOTLEARN_SOURCE_BOT = 1
} botlearn_source_t;

typedef struct
{
	int time;
	int actor;		/* 0 = self, 1 = opponent */
	int token;		/* botlearn_token_t, NONE for pure damage rows */
	int damage;		/* damage dealt by actor in this row (0 for action rows) */
	int selfHealthArmor;
	int selfForce;
	int enemyHealthArmor;
	int enemyForce;
	int rangeBucket;	/* 0 = <128u, 1 = <384u, 2 = beyond */
	int selfStance;
	int enemyStance;
} botlearn_event_t;

typedef struct
{
	int contextKey;
	int stimulus;
	int response;
	int follow1;
	int follow2;
	int responseDelayMs;
	int netDamage;
	int won;
} botlearn_sequence_t;

static inline const char *BotLearn_TokenName(int token)
{
	static const char *names[BOTLEARN_TOK_COUNT] = {
		"none", "idle", "push", "pull", "grip", "drain", "throw", "swing", "kick", "jump", "knockdown"
	};
	if (token < 0 || token >= BOTLEARN_TOK_COUNT)
		return "none";
	return names[token];
}

static inline int BotLearn_IsResponseToken(int token)
{
	return (token >= BOTLEARN_TOK_PUSH && token <= BOTLEARN_TOK_JUMP) ? 1 : 0;
}

static inline int BotLearn_HealthArmorBucket(int healthArmor)
{
	if (healthArmor <= 40)
		return 0;
	if (healthArmor <= 80)
		return 1;
	if (healthArmor <= 125)
		return 2;
	return 3;
}

static inline int BotLearn_ForceBucket(int force)
{
	if (force < 25)
		return 0;
	if (force < 50)
		return 1;
	if (force < 75)
		return 2;
	return 3;
}

static inline int BotLearn_RangeBucket(float distance)
{
	if (distance < 128.0f)
		return 0;
	if (distance < 384.0f)
		return 1;
	return 2;
}

/* SS_FAST/SS_MEDIUM/SS_TAVION -> 0, SS_STRONG/SS_DESANN -> 1, SS_DUAL/SS_STAFF -> 2. */
static inline int BotLearn_StanceBucket(int saberAnimLevel)
{
	if (saberAnimLevel == 3 || saberAnimLevel == 4)
		return 1;
	if (saberAnimLevel == 6 || saberAnimLevel == 7)
		return 2;
	return 0;
}

static inline int BotLearn_ContextKey(int selfHealthArmor, int enemyHealthArmor, int selfForce,
	int enemyForce, int rangeBucket, int selfStance, int enemyStance)
{
	if (rangeBucket < 0)
		rangeBucket = 0;
	else if (rangeBucket > 2)
		rangeBucket = 2;
	return BotLearn_HealthArmorBucket(selfHealthArmor) |
		(BotLearn_HealthArmorBucket(enemyHealthArmor) << 2) |
		(BotLearn_ForceBucket(selfForce) << 4) |
		(BotLearn_ForceBucket(enemyForce) << 6) |
		(rangeBucket << 8) |
		(BotLearn_StanceBucket(selfStance) << 10) |
		(BotLearn_StanceBucket(enemyStance) << 12);
}

/* Coarse context keeps only both force buckets and range: used when the fine context has
 * not collected enough samples yet. */
static inline int BotLearn_CoarseKey(int contextKey)
{
	return BOTLEARN_COARSE_KEY_FLAG | ((contextKey >> 4) & 0x3F);
}

static inline int BotLearn_SkillBand(int isBot, int skill)
{
	if (!isBot)
		return 0;
	if (skill <= 3)
		return 1;
	if (skill <= 5)
		return 2;
	if (skill <= 7)
		return 3;
	return 4;
}

/* Collapse continuous force spends (drain/grip tick every frame) and repeated tokens into
 * one action. Returns the new event count; events must already be sorted by time. */
static inline int BotLearn_CollapseRepeats(botlearn_event_t *events, int count)
{
	int lastToken[2] = { BOTLEARN_TOK_NONE, BOTLEARN_TOK_NONE };
	int lastTime[2] = { -100000, -100000 };
	int read, write = 0;

	for (read = 0; read < count; read++)
	{
		botlearn_event_t ev = events[read];
		const int actor = ev.actor ? 1 : 0;

		if (ev.token != BOTLEARN_TOK_NONE && ev.token != BOTLEARN_TOK_SWING &&
			ev.token == lastToken[actor] && ev.time - lastTime[actor] <= BOTLEARN_REPEAT_COLLAPSE_MS)
		{
			lastTime[actor] = ev.time;
			continue;
		}
		if (ev.token != BOTLEARN_TOK_NONE)
		{
			lastToken[actor] = ev.token;
			lastTime[actor] = ev.time;
		}
		events[write++] = ev;
	}
	return write;
}

static inline int BotLearn_NetDamage(const botlearn_event_t *events, int count, int fromTime, int windowMs)
{
	int i, net = 0;

	for (i = 0; i < count; i++)
	{
		if (events[i].token != BOTLEARN_TOK_NONE || events[i].damage <= 0)
			continue;
		if (events[i].time < fromTime || events[i].time > fromTime + windowMs)
			continue;
		net += events[i].actor ? -events[i].damage : events[i].damage;
	}
	return net;
}

/* Extract stimulus -> response -> follow-up sequences from a merged, time-sorted, collapsed
 * stream seen from "self". Returns the number of sequences written to out. */
static inline int BotLearn_ExtractSequences(const botlearn_event_t *events, int count, int won,
	botlearn_sequence_t *out, int maxOut)
{
	int i, produced = 0;

	for (i = 0; i < count && produced < maxOut; i++)
	{
		const botlearn_event_t *resp = &events[i];
		int stimulus = BOTLEARN_TOK_NONE;
		int stimulusTime = resp->time;
		int recentOwnAction = 0;
		int j, k, follows = 0, prevTime;
		botlearn_sequence_t seq;

		if (resp->actor != 0 || !BotLearn_IsResponseToken(resp->token))
			continue;

		for (j = i - 1; j >= 0; j--)
		{
			const botlearn_event_t *prev = &events[j];
			if (prev->token == BOTLEARN_TOK_NONE)
				continue;
			if (resp->time - prev->time > BOTLEARN_IDLE_GAP_MS)
				break;
			if (prev->actor == 0)
			{
				/* Our own action came first: this one is a follow-up, not a response. */
				if (BotLearn_IsResponseToken(prev->token))
				{
					recentOwnAction = 1;
					break;
				}
				continue;
			}
			if (resp->time - prev->time <= BOTLEARN_RESPONSE_WINDOW_MS)
			{
				stimulus = prev->token;
				stimulusTime = prev->time;
			}
			else
			{
				stimulus = BOTLEARN_TOK_NONE;
			}
			break;
		}
		if (recentOwnAction)
			continue;
		if (stimulus == BOTLEARN_TOK_NONE)
		{
			/* Nothing from the opponent recently (or only stale actions): our initiative. */
			stimulus = BOTLEARN_TOK_IDLE;
			stimulusTime = resp->time;
		}

		seq.contextKey = BotLearn_ContextKey(resp->selfHealthArmor, resp->enemyHealthArmor,
			resp->selfForce, resp->enemyForce, resp->rangeBucket, resp->selfStance, resp->enemyStance);
		seq.stimulus = stimulus;
		seq.response = resp->token;
		seq.follow1 = BOTLEARN_TOK_NONE;
		seq.follow2 = BOTLEARN_TOK_NONE;
		seq.responseDelayMs = resp->time - stimulusTime;
		seq.won = won ? 1 : 0;

		prevTime = resp->time;
		for (k = i + 1; k < count && follows < BOTLEARN_MAX_FOLLOWUPS; k++)
		{
			const botlearn_event_t *next = &events[k];
			if (next->token == BOTLEARN_TOK_NONE)
				continue;
			if (next->time - prevTime > BOTLEARN_FOLLOWUP_WINDOW_MS)
				break;
			if (next->actor != 0)
			{
				/* The opponent reacted; a knockdown we caused still counts as our follow-up. */
				if (next->token == BOTLEARN_TOK_KNOCKDOWN)
					continue;
				break;
			}
			if (follows == 0)
				seq.follow1 = next->token;
			else
				seq.follow2 = next->token;
			follows++;
			prevTime = next->time;
		}

		seq.netDamage = BotLearn_NetDamage(events, count, resp->time, BOTLEARN_OUTCOME_WINDOW_MS);
		out[produced++] = seq;
	}
	return produced;
}

/* Confidence-weighted success score: the average net damage and the win rate are both
 * shrunk toward neutral by BOTLEARN_PRIOR_SAMPLES so a handful of lucky rows cannot
 * dominate a well-sampled alternative. Positive scores mean the response tends to win. */
static inline float BotLearn_Score(int samples, int wins, int netDamage)
{
	float denom;
	float meanNet;
	float winRate;

	if (samples <= 0)
		return 0.0f;
	denom = (float)samples + BOTLEARN_PRIOR_SAMPLES;
	meanNet = (float)netDamage / denom;
	winRate = ((float)wins + BOTLEARN_PRIOR_SAMPLES * 0.5f) / denom;
	return meanNet + (winRate - 0.5f) * 20.0f;
}

/* Skill scale for the learned bonus: skill 7+ follows the data fully, lower skills lean on
 * it less (and add sampling noise, see BotLearn_SampleNoise). */
static inline float BotLearn_SkillScale(float skill)
{
	if (skill >= 7.0f)
		return 1.0f;
	if (skill <= 1.0f)
		return 0.4f;
	return 0.4f + 0.6f * (skill - 1.0f) / 6.0f;
}

static inline int BotLearn_WeightBonus(float score, int samples, int minSamples, float strength, float skill)
{
	float bonus;

	if (samples < minSamples || strength <= 0.0f)
		return 0;
	bonus = score * 2.0f * strength * BotLearn_SkillScale(skill);
	if (bonus > (float)BOTLEARN_BONUS_CAP)
		bonus = (float)BOTLEARN_BONUS_CAP;
	else if (bonus < -(float)BOTLEARN_BONUS_CAP)
		bonus = -(float)BOTLEARN_BONUS_CAP;
	return (int)bonus;
}

/* +/- range of random noise a bot adds to a learned bonus: 0 at skill 7+, up to 18 at skill 1,
 * so lower levels sample alternatives more widely instead of always taking the best move. */
static inline int BotLearn_SampleNoise(float skill)
{
	if (skill >= 7.0f)
		return 0;
	if (skill < 1.0f)
		skill = 1.0f;
	return (int)((7.0f - skill) * 3.0f);
}

#endif
