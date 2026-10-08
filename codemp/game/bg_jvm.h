#ifndef BG_JVM_H
#define BG_JVM_H

typedef enum {
	JVM_JEDI,
	JVM_MERC,
	JVM_TANK
} jvmClass_t;

/* STAT_RESTRICTIONS uses bits 0..4 for race restrictions. These bits fit
 * the existing signed 16-bit network field without changing playerState ABI. */
#define JVM_CLASS_SHIFT 8
#define JVM_CLASS_MASK (3 << JVM_CLASS_SHIFT)
#define JVM_ACTIVE (1 << 10)
#define JVM_FLIPKICK (1 << 11)
#define JVM_COUNTERGRIP (1 << 12)
#define JVM_PUSHPULL (1 << 13)

static inline int JVM_IsMode(int gametype) {
	return gametype == 11 || gametype == 12;
}

static inline int JVM_IsTeamGame(int gametype) {
	/* Preserve legacy Arcade behaviour at call sites with their own exclusion. */
	return gametype >= 6 && gametype != 12;
}

static inline int JVM_Class(int gametype, int team, int selected) {
	if (gametype == 11)
		return team == 2 ? JVM_MERC : team == 1 && selected == JVM_TANK ? JVM_TANK : JVM_JEDI;
	return selected >= JVM_JEDI && selected <= JVM_TANK ? selected : JVM_JEDI;
}

static inline int JVM_ReplicatedClass(int restrictions) {
	return (restrictions & JVM_CLASS_MASK) >> JVM_CLASS_SHIFT;
}

static inline int JVM_CounterGrip(int restrictions) {
	return (restrictions & JVM_ACTIVE) && (restrictions & JVM_COUNTERGRIP) &&
		JVM_ReplicatedClass(restrictions) == JVM_MERC;
}

static inline int JVM_TankPushPull(int restrictions, int pushPull) {
	return pushPull && (restrictions & JVM_ACTIVE) && (restrictions & JVM_PUSHPULL) &&
		JVM_ReplicatedClass(restrictions) == JVM_TANK;
}

static inline int JVM_NextClass(int playerClass) {
	return playerClass == JVM_JEDI ? JVM_MERC : playerClass == JVM_MERC ? JVM_TANK : JVM_JEDI;
}

static inline int JVM_PassiveAbsorb(int gametype, int playerClass, int enabled,
	int pushPullGrip, int powerLevel) {
	return JVM_IsMode(gametype) && playerClass == JVM_JEDI && enabled && pushPullGrip ?
		(powerLevel > 3 ? powerLevel - 3 : 0) : -1;
}

static inline int JVM_FlipkickSetting(int restrictions, int configured) {
	if ((restrictions & JVM_ACTIVE) && JVM_ReplicatedClass(restrictions) == JVM_MERC)
		return (restrictions & JVM_FLIPKICK) ? 2 : 0;
	return configured;
}

static inline int JVM_JumpRank(int rank) {
	return rank < 0 ? 0 : (rank > 3 ? 3 : rank);
}

static inline int JVM_AllowsLegacyForce(int gametype, int playerClass) {
	return !JVM_IsMode(gametype) || playerClass == JVM_JEDI;
}

static inline int JVM_ForceRank(int playerClass, int configured, int jump,
	int saber, int jumpRank) {
	if (playerClass == JVM_JEDI)
		return configured;
	if (jump)
		return JVM_JumpRank(jumpRank);
	return playerClass == JVM_TANK && saber ? 3 : 0;
}

static inline int JVM_ForceRankWithDisable(int playerClass, int configured,
	int jump, int saber, int jumpRank, int disabled, int freeSaber) {
	int rank = JVM_ForceRank(playerClass, configured, jump, saber, jumpRank);
	if (!disabled || freeSaber)
		return rank;
	/* Match BG_LegalizedForcePowers: disabled Jump retains only basic jumping. */
	return jump && rank > 0 ? 1 : 0;
}

static inline int JVM_BotGunAttack(int gametype, int playerClass,
	int commandWeapon, int currentWeapon, int weaponCount, int melee, int saber) {
	int weapon = commandWeapon > 0 && commandWeapon < weaponCount ?
		commandWeapon : currentWeapon;
	return JVM_IsMode(gametype) && playerClass == JVM_MERC &&
		weapon > 0 && weapon < weaponCount && weapon != melee && weapon != saber;
}

static inline int JVM_HoldableAllowed(int restrictions, int medpac) {
	return !medpac || !(restrictions & JVM_ACTIVE) ||
		JVM_ReplicatedClass(restrictions) == JVM_JEDI;
}

static inline int JVM_ForceRegenInterval(int gametype, int playerClass,
	int interval, int mercInterval) {
	if (JVM_IsMode(gametype) && playerClass == JVM_MERC)
		interval = mercInterval;
	return interval < 1 ? 1 : interval;
}

static inline int JVM_DuelAllowsGuns(int gametype, int duelType) {
	return gametype == 12 && duelType == 1;
}

static inline int JVM_AllowedWeapons(int playerClass, int owned, int disabled,
	int melee, int saber) {
	return playerClass == JVM_MERC ? owned & ~saber & ~(disabled & ~melee) : owned & saber;
}

static inline float JVM_ClampScale(float scale, float maximum) {
	return !(scale >= 0.0f) ? 0.0f : (scale > maximum ? maximum : scale);
}

static inline float JVM_DisarmChance(float configured, float distance) {
	float chance = JVM_ClampScale(configured, 100.0f);
	chance *= 100.0f / (distance > 1.0f ? distance : 1.0f);
	return JVM_ClampScale(chance, 100.0f);
}

static inline int JVM_HealthRegen(float *fraction, int *residual, int elapsed,
	float amount, int interval, int health, int maximum) {
	int remainder, whole;
	float ticks;
	if (health <= 0 || health >= maximum || !(amount > 0.0f)) {
		*fraction = 0.0f;
		*residual = 0;
		return 0;
	}
	if (elapsed <= 0)
		return 0;
	if (interval < 1)
		interval = 1;
	ticks = (float)(elapsed / interval) + (float)(*residual / interval);
	*residual %= interval;
	remainder = elapsed % interval;
	if (remainder >= interval - *residual) {
		ticks += 1.0f;
		*residual = remainder - (interval - *residual);
	}
	else
		*residual += remainder;
	*fraction += ticks * JVM_ClampScale(amount, (float)(maximum - health));
	if (*fraction >= maximum - health) {
		*fraction = 0.0f;
		*residual = 0;
		return maximum - health;
	}
	whole = (int)(*fraction + 0.00001f);
	*fraction -= whole;
	if (*fraction < 0.0f)
		*fraction = 0.0f;
	return whole;
}

/* Only mercs collect pickups; tanks take health/armor only; Jedi take nothing
 * except team objectives (flags), which every class may carry. */
static inline int JVM_PickupAllowed(int playerClass, int weapon, int ammo,
	int health, int armor, int saber, int teamItem) {
	if (teamItem)
		return 1;
	if (playerClass == JVM_TANK)
		return health || armor;
	if (playerClass == JVM_MERC)
		return !saber;
	return 0;
}

/* Default merc loadout: pistol..rocket launcher, concussion and old bryar
 * (WP_BRYAR_PISTOL=4..WP_ROCKET_LAUNCHER=11, WP_CONCUSSION=15, WP_BRYAR_OLD=16);
 * no stun baton, explosives or saber. Melee is always added. */
#define JVM_MERC_DEFAULT_WEAPONS 102384

static inline int JVM_MercStartingWeapons(int configured, int disabled,
	int melee, int saber) {
	return ((configured & ~saber & ~disabled) | melee);
}

/* /team 1|2|3 in Jedi-or-Merc selects jedi|merc|tank; -1 when not a class. */
static inline int JVM_TeamArgClass(const char *arg) {
	if (!arg || !arg[0] || arg[1])
		return -1;
	if (arg[0] == '1')
		return JVM_JEDI;
	if (arg[0] == '2')
		return JVM_MERC;
	if (arg[0] == '3')
		return JVM_TANK;
	return -1;
}

static inline float JVM_DamageScale(int targetClass, int attackerClass,
	int saber, int gripOrFlipkick, float tankScale, float saberScale,
	float reduction) {
	float scale = targetClass == JVM_TANK ? JVM_ClampScale(tankScale, 10.0f) : 1.0f;
	if (saber && attackerClass == JVM_TANK)
		scale *= JVM_ClampScale(saberScale, 10.0f);
	if ((targetClass == JVM_MERC || targetClass == JVM_JEDI) && gripOrFlipkick)
		scale *= 1.0f - JVM_ClampScale(reduction, 1.0f);
	return scale;
}

static inline int JVM_GrappleDrain(float *fraction, int elapsed, float rate) {
	int whole;
	if (elapsed <= 0)
		return 0;
	*fraction += elapsed * JVM_ClampScale(rate, 1000.0f) / 1000.0f;
	whole = (int)(*fraction + 0.00001f);
	*fraction -= whole;
	if (*fraction < 0.0f)
		*fraction = 0.0f;
	return whole;
}

#endif
