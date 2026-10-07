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

static inline int JVM_IsMode(int gametype) {
	return gametype == 11 || gametype == 12;
}

static inline int JVM_IsTeamGame(int gametype) {
	/* Preserve legacy Arcade behaviour at call sites with their own exclusion. */
	return gametype >= 6 && gametype != 12;
}

static inline int JVM_Class(int gametype, int team, int selected) {
	if (gametype == 11)
		return team == 2 ? JVM_MERC : JVM_JEDI;
	return selected >= JVM_JEDI && selected <= JVM_TANK ? selected : JVM_JEDI;
}

static inline int JVM_ReplicatedClass(int restrictions) {
	return (restrictions & JVM_CLASS_MASK) >> JVM_CLASS_SHIFT;
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

static inline int JVM_PickupAllowed(int playerClass, int weapon, int ammo,
	int health, int armor, int saber) {
	if (playerClass == JVM_TANK)
		return health || armor;
	if (playerClass == JVM_MERC)
		return !saber;
	return !weapon && !ammo && !health && !armor;
}

static inline float JVM_DamageScale(int targetClass, int attackerClass,
	int saber, int gripOrFlipkick, float tankScale, float saberScale,
	float reduction) {
	float scale = targetClass == JVM_TANK ? JVM_ClampScale(tankScale, 10.0f) : 1.0f;
	if (saber && attackerClass == JVM_TANK)
		scale *= JVM_ClampScale(saberScale, 10.0f);
	if (targetClass == JVM_MERC && gripOrFlipkick)
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
