// Bot sequence learning runtime.
//
// Two parts:
//  1. A per-client action observer that turns raw player state edges into the same
//     coarse action tokens the duel tracker records (push/pull/grip/drain/throw/swing/
//     kick/jump/knockdown). The bot AI uses it to recognise what an opponent just did
//     and to time its own combo follow-ups.
//  2. An in-memory cache of learned stimulus -> response (-> follow-up) outcomes loaded
//     from LocalBotLearnedSequence on map load (see g_account.c). The AI asks
//     G_BotLearnBonus for an additive, capped, skill-scaled weight bonus.

#include "g_local.h"
#include "g_bot_learning.h"
#include "ai_combat_tuning.h"

extern qboolean BG_InKnockDown(int anim);
extern qboolean PM_SaberInReturn(int move);
extern qboolean PM_SaberInParry(int move);
extern qboolean PM_SaberInReflect(int move);
extern qboolean PM_SaberInKnockaway(int move);
extern qboolean PM_SaberInBounce(int move);
extern qboolean PM_SaberInBrokenParry(int move);
extern void DownedSaberThink(gentity_t *saberent);

int G_BotLearnDefenseState(gentity_t *self)
{
	playerState_t *ps;
	int lost = 0;
	if (!self || !self->client || !self->inuse)
		return BOTLEARN_DEFENSE_UNKNOWN;
	ps = &self->client->ps;
	if (ps->weapon != WP_SABER && ps->weapon != WP_MELEE)
		return BOTLEARN_DEFENSE_UNKNOWN;
	/* Physical loss includes the cooldown before the saber becomes retrievable. */
	if (self->client->saberKnockedTime > level.time ||
		(self->client->saberKnockedTime > 0 && ps->saberEntityNum <= 0))
	{
		int blade = self->client->saberStoredIndex;
		gentity_t *saber;
		if (blade <= 0 || blade >= ENTITYNUM_WORLD || !g_entities[blade].inuse)
			return BOTLEARN_DEFENSE_UNKNOWN;
		saber = &g_entities[blade];
		if (!BotLearn_ValidatedDroppedBlade(self->client->saberKnockedTime > 0,
			saber->think == DownedSaberThink, saber->r.contents == CONTENTS_TRIGGER,
			saber->s.weapon == WP_SABER, saber->s.eType == ET_MISSILE,
			(ps->stats[STAT_WEAPONS] & (1 << WP_SABER)) &&
				(saber->r.ownerNum == self->s.number || saber->parent == self),
			saber->s.pos.trType == TR_GRAVITY || saber->s.pos.trType == TR_STATIONARY ||
				saber->s.pos.trType == TR_INTERPOLATE))
			return BOTLEARN_DEFENSE_UNKNOWN;
		lost = 1;
	}
	else if (ps->saberInFlight && (ps->saberEntityNum <= 0 || ps->saberEntityNum >= ENTITYNUM_WORLD ||
		!g_entities[ps->saberEntityNum].inuse || g_entities[ps->saberEntityNum].r.ownerNum != self->s.number ||
		g_entities[ps->saberEntityNum].s.weapon != WP_SABER))
		return BOTLEARN_DEFENSE_UNKNOWN;
	if (ps->weapon != WP_SABER && !lost)
		return BOTLEARN_DEFENSE_UNKNOWN;
	return NewBotAI_SaberDefenseState(
		ps->saberBlocked == BLOCKED_PARRY_BROKEN || PM_SaberInBrokenParry(ps->saberMove),
		ps->saberBlocked == BLOCKED_ATK_BOUNCE || PM_SaberInBounce(ps->saberMove),
		ps->saberBlocked != BLOCKED_NONE || PM_SaberInParry(ps->saberMove) ||
			PM_SaberInReflect(ps->saberMove) || PM_SaberInKnockaway(ps->saberMove),
		BG_InKnockDown(ps->legsAnim) ||
			(ps->forceHandExtend == HANDEXTEND_KNOCKDOWN && ps->forceHandExtendTime > level.time), lost);
}

typedef struct
{
	int lastTokenTime[BOTLEARN_TOK_COUNT];
	int lastToken;
	int lastTokenAt;
	int lastHandExtend;
	int lastPowersActive;
	int lastSaberMove;
	qboolean lastSaberInFlight;
	qboolean lastGrounded;
	qboolean lastKnockdown;
	qboolean initialized;
} botlearn_observer_t;

static botlearn_observer_t g_botLearnObservers[MAX_CLIENTS];

static void G_BotLearnNoteToken(botlearn_observer_t *obs, int token)
{
	if (token <= BOTLEARN_TOK_NONE || token >= BOTLEARN_TOK_COUNT)
		return;
	obs->lastTokenTime[token] = level.time;
	obs->lastToken = token;
	obs->lastTokenAt = level.time;
}

static qboolean G_BotLearnIsKickMove(int saberMove)
{
	return (saberMove >= LS_KICK_F && saberMove <= LS_KICK_L_AIR) ? qtrue : qfalse;
}

void G_BotLearnResetClient(int clientNum)
{
	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
		return;
	memset(&g_botLearnObservers[clientNum], 0, sizeof(g_botLearnObservers[clientNum]));
}

void G_BotLearnObserveClient(gentity_t *ent)
{
	botlearn_observer_t *obs;
	playerState_t *ps;
	qboolean grounded, knockedDown, inAttack;
	int handExtend;

	if (!ent || !ent->client || ent->s.number < 0 || ent->s.number >= MAX_CLIENTS)
		return;

	obs = &g_botLearnObservers[ent->s.number];
	ps = &ent->client->ps;
	if (ent->health <= 0 || ent->client->sess.sessionTeam == TEAM_SPECTATOR)
	{
		obs->initialized = qfalse;
		return;
	}

	grounded = (ps->groundEntityNum != ENTITYNUM_NONE) ? qtrue : qfalse;
	knockedDown = BG_InKnockDown(ps->legsAnim) ? qtrue : qfalse;
	handExtend = (ps->forceHandExtendTime > level.time) ? ps->forceHandExtend : HANDEXTEND_NONE;

	if (!obs->initialized)
	{
		memset(obs, 0, sizeof(*obs));
		obs->initialized = qtrue;
		obs->lastHandExtend = handExtend;
		obs->lastPowersActive = ps->fd.forcePowersActive;
		obs->lastSaberMove = ps->saberMove;
		obs->lastSaberInFlight = ps->saberInFlight ? qtrue : qfalse;
		obs->lastGrounded = grounded;
		obs->lastKnockdown = knockedDown;
		return;
	}

	if (handExtend != obs->lastHandExtend)
	{
		if (handExtend == HANDEXTEND_FORCEPUSH)
			G_BotLearnNoteToken(obs, BOTLEARN_TOK_PUSH);
		else if (handExtend == HANDEXTEND_FORCEPULL)
			G_BotLearnNoteToken(obs, BOTLEARN_TOK_PULL);
	}
	if ((ps->fd.forcePowersActive & (1 << FP_GRIP)) && !(obs->lastPowersActive & (1 << FP_GRIP)))
		G_BotLearnNoteToken(obs, BOTLEARN_TOK_GRIP);
	if ((ps->fd.forcePowersActive & (1 << FP_DRAIN)) && !(obs->lastPowersActive & (1 << FP_DRAIN)))
		G_BotLearnNoteToken(obs, BOTLEARN_TOK_DRAIN);
	if (ps->saberInFlight && !obs->lastSaberInFlight)
		G_BotLearnNoteToken(obs, BOTLEARN_TOK_THROW);

	inAttack = BG_SaberInAttack(ps->saberMove) ? qtrue : qfalse;
	if (ps->saberMove != obs->lastSaberMove)
	{
		if (G_BotLearnIsKickMove(ps->saberMove))
			G_BotLearnNoteToken(obs, BOTLEARN_TOK_KICK);
		else if (inAttack)
			G_BotLearnNoteToken(obs, BOTLEARN_TOK_SWING);
	}
	if (!grounded && obs->lastGrounded && ps->velocity[2] > 0.0f)
		G_BotLearnNoteToken(obs, BOTLEARN_TOK_JUMP);
	if (knockedDown && !obs->lastKnockdown)
		G_BotLearnNoteToken(obs, BOTLEARN_TOK_KNOCKDOWN);

	obs->lastHandExtend = handExtend;
	obs->lastPowersActive = ps->fd.forcePowersActive;
	obs->lastSaberMove = ps->saberMove;
	obs->lastSaberInFlight = ps->saberInFlight ? qtrue : qfalse;
	obs->lastGrounded = grounded;
	obs->lastKnockdown = knockedDown;
}

int G_BotLearnLastTokenTime(int clientNum, int token)
{
	if (clientNum < 0 || clientNum >= MAX_CLIENTS || token <= BOTLEARN_TOK_NONE || token >= BOTLEARN_TOK_COUNT)
		return 0;
	if (!g_botLearnObservers[clientNum].initialized)
		return 0;
	return g_botLearnObservers[clientNum].lastTokenTime[token];
}

qboolean G_BotLearnRecentToken(int clientNum, int token, int maxAgeMs)
{
	const int t = G_BotLearnLastTokenTime(clientNum, token);
	return (t > 0 && level.time - t <= maxAgeMs) ? qtrue : qfalse;
}

int G_BotLearnLatestToken(int clientNum, int maxAgeMs)
{
	const botlearn_observer_t *obs;

	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
		return BOTLEARN_TOK_NONE;
	obs = &g_botLearnObservers[clientNum];
	if (!obs->initialized || obs->lastTokenAt <= 0 || level.time - obs->lastTokenAt > maxAgeMs)
		return BOTLEARN_TOK_NONE;
	return obs->lastToken;
}

// ---------------------------------------------------------------------------------------
// Learned outcome cache
// ---------------------------------------------------------------------------------------

#define BOTLEARN_CACHE_SIZE 32768	// power of two
#define BOTLEARN_FOLLOW_ANY BOTLEARN_TOK_COUNT

typedef struct
{
	int used;
	int contextKey;
	int stimulus;
	int response;
	int follow1;
	float samples;		// source-weighted sample count
	float excessWins;	// wins above the source's baseline win rate (weighted)
	float netDamage;	// weighted
} botlearn_cache_entry_t;

static botlearn_cache_entry_t g_botLearnCache[BOTLEARN_CACHE_SIZE];
static int g_botLearnCacheCount;

static unsigned int G_BotLearnHash(int contextKey, int stimulus, int response, int follow1)
{
	unsigned int h = (unsigned int)contextKey * 2654435761u;
	h ^= (unsigned int)(stimulus * 31 + response) * 2246822519u;
	h ^= (unsigned int)follow1 * 3266489917u;
	h ^= h >> 15;
	return h & (BOTLEARN_CACHE_SIZE - 1);
}

static botlearn_cache_entry_t *G_BotLearnCacheFind(int contextKey, int stimulus, int response, int follow1,
	qboolean create)
{
	unsigned int idx = G_BotLearnHash(contextKey, stimulus, response, follow1);
	int probes;

	for (probes = 0; probes < BOTLEARN_CACHE_SIZE; probes++)
	{
		botlearn_cache_entry_t *e = &g_botLearnCache[idx];
		if (!e->used)
		{
			//Keep the table at most 3/4 full so probing stays short.
			if (!create || g_botLearnCacheCount >= (BOTLEARN_CACHE_SIZE * 3) / 4)
				return NULL;
			e->used = 1;
			e->contextKey = contextKey;
			e->stimulus = stimulus;
			e->response = response;
			e->follow1 = follow1;
			g_botLearnCacheCount++;
			return e;
		}
		if (e->contextKey == contextKey && e->stimulus == stimulus &&
			e->response == response && e->follow1 == follow1)
			return e;
		idx = (idx + 1) & (BOTLEARN_CACHE_SIZE - 1);
	}
	return NULL;
}

void G_BotLearnCacheClear(void)
{
	memset(g_botLearnCache, 0, sizeof(g_botLearnCache));
	g_botLearnCacheCount = 0;
}

int G_BotLearnCacheCount(void)
{
	return g_botLearnCacheCount;
}

static void G_BotLearnCacheAccumulate(int contextKey, int stimulus, int response, int follow1,
	float samples, float excessWins, float netDamage)
{
	botlearn_cache_entry_t *e = G_BotLearnCacheFind(contextKey, stimulus, response, follow1, qtrue);

	if (!e)
		return;
	e->samples += samples;
	e->excessWins += excessWins;
	e->netDamage += netDamage;
}

// samples/excessWins/netDamage are already weighted by source (BotLearn_SourceWeight) and
// excessWins is relative to the source's baseline win rate (BotLearn_ExcessWins).
void G_BotLearnCacheAdd(int contextKey, int stimulus, int response, int follow1,
	float samples, float excessWins, float netDamage)
{
	const int coarseKey = BotLearn_CoarseKey(contextKey);

	if (samples <= 0.0f || stimulus <= BOTLEARN_TOK_NONE || stimulus >= BOTLEARN_TOK_COUNT ||
		!BotLearn_IsResponseToken(response))
		return;
	if (follow1 < BOTLEARN_TOK_NONE || follow1 >= BOTLEARN_TOK_COUNT)
		follow1 = BOTLEARN_TOK_NONE;

	//Each row feeds the exact context and the coarse (force/range/stance-only) fallback,
	//both for its specific follow-up and for "any follow-up".
	G_BotLearnCacheAccumulate(contextKey, stimulus, response, BOTLEARN_FOLLOW_ANY, samples, excessWins, netDamage);
	G_BotLearnCacheAccumulate(coarseKey, stimulus, response, BOTLEARN_FOLLOW_ANY, samples, excessWins, netDamage);
	G_BotLearnCacheAccumulate(contextKey, stimulus, response, follow1, samples, excessWins, netDamage);
	G_BotLearnCacheAccumulate(coarseKey, stimulus, response, follow1, samples, excessWins, netDamage);
}

static int G_BotLearnMinSamples(void)
{
	return (bot_learningminsamples.integer > 0) ? bot_learningminsamples.integer : BOTLEARN_DEFAULT_MIN_SAMPLES;
}

int G_BotLearnFooting(gentity_t *self, gentity_t *enemy)
{
	if (!self || !self->client || !enemy || !enemy->client)
		return -1;
	return BotLearn_FootingCategory(self->client->ps.velocity[0], self->client->ps.velocity[1],
		enemy->client->ps.origin[0] - self->client->ps.origin[0],
		enemy->client->ps.origin[1] - self->client->ps.origin[1],
		self->client->ps.groundEntityNum != ENTITYNUM_NONE);
}

int G_BotLearnLiveContextKey(gentity_t *self, gentity_t *enemy)
{
	vec3_t diff;

	if (!self || !self->client || !enemy || !enemy->client)
		return -1;
	VectorSubtract(enemy->client->ps.origin, self->client->ps.origin, diff);
	return BotLearn_ContextFooting(BotLearn_ContextSafety(BotLearn_ContextKey(self->health + self->client->ps.stats[STAT_ARMOR],
		enemy->health + enemy->client->ps.stats[STAT_ARMOR],
		self->client->ps.fd.forcePower, enemy->client->ps.fd.forcePower,
		BotLearn_RangeBucket(VectorLength(diff)),
		self->client->ps.fd.saberAnimLevel, enemy->client->ps.fd.saberAnimLevel),
		G_BotLearnDuelMode(self),
		G_BotLearnDefenseState(self), G_BotLearnDefenseState(enemy),
		PM_SaberInReturn(self->client->ps.saberMove), PM_SaberInReturn(enemy->client->ps.saberMove),
		self->client->ps.groundEntityNum == ENTITYNUM_NONE,
		enemy->client->ps.groundEntityNum == ENTITYNUM_NONE), G_BotLearnFooting(self, enemy));
}

// Pool the live context with similar contexts (BotLearn_NeighborKey/BotLearn_NeighborWeight)
// into *out. Returns qfalse when nothing similar has been recorded.
static qboolean G_BotLearnPoolSimilar(int contextKey, int stimulus, int response, int followKey,
	botlearn_cache_entry_t *out)
{
	int a, b, c, d;

	memset(out, 0, sizeof(*out));
	for (a = -BOTLEARN_NEIGHBOR_MAX_DISTANCE; a <= BOTLEARN_NEIGHBOR_MAX_DISTANCE; a++)
		for (b = -BOTLEARN_NEIGHBOR_MAX_DISTANCE; b <= BOTLEARN_NEIGHBOR_MAX_DISTANCE; b++)
			for (c = -BOTLEARN_NEIGHBOR_MAX_DISTANCE; c <= BOTLEARN_NEIGHBOR_MAX_DISTANCE; c++)
				for (d = -BOTLEARN_NEIGHBOR_MAX_DISTANCE; d <= BOTLEARN_NEIGHBOR_MAX_DISTANCE; d++)
				{
					const int steps = abs(a) + abs(b) + abs(c) + abs(d);
					const float weight = BotLearn_NeighborWeight(steps);
					const int key = BotLearn_NeighborKey(contextKey, a, b, c, d);
					const botlearn_cache_entry_t *e;

					if (weight <= 0.0f || key < 0)
						continue;
					e = G_BotLearnCacheFind(key, stimulus, response, followKey, qfalse);
					if (!e)
						continue;
					out->samples += e->samples * weight;
					out->excessWins += e->excessWins * weight;
					out->netDamage += e->netDamage * weight;
				}
	out->used = out->samples > 0.0f;
	return out->used ? qtrue : qfalse;
}

// Additive learned weight for choosing `response` (and optionally `follow1`, or
// BOTLEARN_TOK_NONE for any follow-up) after the opponent's `stimulus` in the live context.
// Without enough live data the built-in baseline (BotLearn_BaselineBonus, scaled by
// bot_learningbaseline) is used instead. Returns 0 when both are off or have no entry.
int G_BotLearnBonus(gentity_t *self, gentity_t *enemy, int stimulus, int response, int follow1, float skill)
{
	const botlearn_cache_entry_t *e = NULL;
	botlearn_cache_entry_t pooled;
	const int followKey = (follow1 > BOTLEARN_TOK_NONE && follow1 < BOTLEARN_TOK_COUNT) ? follow1 : BOTLEARN_FOLLOW_ANY;
	const int minSamples = G_BotLearnMinSamples();
	const qboolean live = (bot_learning.integer && bot_learningstrength.value > 0.0f && g_botLearnCacheCount) ?
		qtrue : qfalse;
	int contextKey;
	int bonus;
	int noise;

	if (!live && bot_learningbaseline.value <= 0.0f)
		return 0;
	if (stimulus <= BOTLEARN_TOK_NONE)
		stimulus = BOTLEARN_TOK_IDLE;
	contextKey = G_BotLearnLiveContextKey(self, enemy);
	if (contextKey < 0)
		return 0;
	if (((contextKey >> 16) & 3) >= 2)
		return 0;

	if (live)
	{
		e = G_BotLearnCacheFind(contextKey, stimulus, response, followKey, qfalse);
		if ((!e || e->samples < minSamples) &&
			G_BotLearnPoolSimilar(contextKey, stimulus, response, followKey, &pooled) &&
			pooled.samples >= minSamples)
			e = &pooled;
		if (!e || e->samples < minSamples)
			e = G_BotLearnCacheFind(BotLearn_CoarseKey(contextKey), stimulus, response, followKey, qfalse);
	}
	if (e && e->samples >= minSamples)
	{
		bonus = BotLearn_WeightBonus(BotLearn_ScoreRelative(e->samples, e->excessWins, e->netDamage), e->samples,
			minSamples, bot_learningstrength.value, skill);
	}
	else
	{
		bonus = BotLearn_BaselineBonus(contextKey, stimulus, response, followKey,
			bot_learningbaseline.value, skill);
		if (!bonus)
			return 0;
	}
	noise = BotLearn_SampleNoise(skill);
	if (noise > 0)
		bonus += Q_irand(-noise, noise);
	return Com_Clampi(-BOTLEARN_BONUS_CAP, BOTLEARN_BONUS_CAP, bonus);
}

// Debug: best scoring responses per (coarse context, stimulus) with enough samples.
#define BOTLEARN_DEBUG_MAX_ROWS 4096

static int QDECL G_BotLearnCompareDebugRows(const void *a, const void *b)
{
	const botlearn_cache_entry_t *ea = *(const botlearn_cache_entry_t * const *)a;
	const botlearn_cache_entry_t *eb = *(const botlearn_cache_entry_t * const *)b;
	float sa, sb;

	if (ea->contextKey != eb->contextKey)
		return ea->contextKey - eb->contextKey;
	if (ea->stimulus != eb->stimulus)
		return ea->stimulus - eb->stimulus;
	sa = BotLearn_ScoreRelative(ea->samples, ea->excessWins, ea->netDamage);
	sb = BotLearn_ScoreRelative(eb->samples, eb->excessWins, eb->netDamage);
	return (sa > sb) ? -1 : (sa < sb) ? 1 : 0;
}

void G_BotLearnDebugPrint(int maxLines)
{
	static const botlearn_cache_entry_t *rows[BOTLEARN_DEBUG_MAX_ROWS];
	const int minSamples = G_BotLearnMinSamples();
	int i, count = 0, lines = 0;

	Com_Printf("^5Bot learning: %i cached entries (min samples %i)\n", g_botLearnCacheCount, minSamples);
	for (i = 0; i < BOTLEARN_CACHE_SIZE && count < BOTLEARN_DEBUG_MAX_ROWS; i++)
	{
		const botlearn_cache_entry_t *e = &g_botLearnCache[i];
		if (e->used && (e->contextKey & BOTLEARN_COARSE_KEY_FLAG) && e->follow1 == BOTLEARN_FOLLOW_ANY &&
			e->samples >= minSamples)
			rows[count++] = e;
	}
	qsort(rows, count, sizeof(rows[0]), G_BotLearnCompareDebugRows);

	for (i = 0; i < count && lines < maxLines; )
	{
		const botlearn_cache_entry_t *best = rows[i];
		int j = i + 1;

		Com_Printf("selfF %i enemyF %i range %i | vs %-9s -> %-9s %6.1f (%.1f)",
			best->contextKey & 3, (best->contextKey >> 2) & 3, (best->contextKey >> 4) & 3,
			BotLearn_TokenName(best->stimulus), BotLearn_TokenName(best->response),
			BotLearn_ScoreRelative(best->samples, best->excessWins, best->netDamage), best->samples);
		while (j < count && rows[j]->contextKey == best->contextKey && rows[j]->stimulus == best->stimulus)
		{
			if (j - i < 3)
				Com_Printf(" | %s %.1f (%.1f)", BotLearn_TokenName(rows[j]->response),
					BotLearn_ScoreRelative(rows[j]->samples, rows[j]->excessWins, rows[j]->netDamage), rows[j]->samples);
			j++;
		}
		Com_Printf("\n");
		lines++;
		i = j;
	}
}
