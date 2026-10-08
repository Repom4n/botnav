#include "../codemp/qcommon/q_shared.h"
#include "bg_public.h"
#include <boost/test/unit_test.hpp>
#include <limits>

BOOST_AUTO_TEST_SUITE(jvm_rules)

BOOST_AUTO_TEST_CASE(mode_ids_and_team_semantics)
{
	BOOST_CHECK_EQUAL(GT_CTF, 8);
	BOOST_CHECK_EQUAL(GT_CTY, 9);
	BOOST_CHECK_EQUAL(GT_ARCADE, 10);
	BOOST_CHECK_EQUAL(GT_JVM, 11);
	BOOST_CHECK_EQUAL(GT_JOM, 12);
	for (int mode = 0; mode < 13; ++mode) {
		BOOST_CHECK_EQUAL(JVM_IsMode(mode), mode == 11 || mode == 12);
		BOOST_CHECK_EQUAL(JVM_IsTeamGame(mode), mode >= 6 && mode != 12);
	}
	BOOST_CHECK(JVM_IsTeamGame(8));
	BOOST_CHECK(JVM_IsTeamGame(9));
	BOOST_CHECK(JVM_IsTeamGame(10));
	BOOST_CHECK(!JVM_IsTeamGame(12));
}

BOOST_AUTO_TEST_CASE(classes_are_not_jom_teams)
{
	BOOST_CHECK_EQUAL(JVM_Class(11, 1, JVM_TANK), JVM_TANK);
	BOOST_CHECK_EQUAL(JVM_Class(11, 1, JVM_MERC), JVM_JEDI);
	BOOST_CHECK_EQUAL(JVM_Class(11, 2, JVM_TANK), JVM_MERC);
	BOOST_CHECK_EQUAL(JVM_Class(11, TEAM_SPECTATOR, JVM_TANK), JVM_JEDI);
	BOOST_CHECK_EQUAL(JVM_Class(11, 2, JVM_JEDI), JVM_MERC);
	for (int playerClass = JVM_JEDI; playerClass <= JVM_TANK; ++playerClass) {
		BOOST_CHECK_EQUAL(JVM_Class(12, 0, playerClass), playerClass);
		BOOST_CHECK_EQUAL(JVM_ReplicatedClass(JVM_ACTIVE |
			(playerClass << JVM_CLASS_SHIFT) | 31 | JVM_FLIPKICK), playerClass);
	}
	BOOST_CHECK_EQUAL(JVM_Class(12, 0, -1), JVM_JEDI);
	BOOST_CHECK_EQUAL(JVM_Class(12, 0, 3), JVM_JEDI);
	BOOST_CHECK_EQUAL(JVM_CLASS_MASK & 31, 0);
	BOOST_CHECK_LT(JVM_ACTIVE | JVM_CLASS_MASK | JVM_FLIPKICK | JVM_COUNTERGRIP | JVM_PUSHPULL, 32768);
}

BOOST_AUTO_TEST_CASE(pickup_categories)
{
	/* categories: weapon, ammo, health, armor, other (holdable/powerup) */
	for (int category = 0; category < 5; ++category) {
		BOOST_CHECK(!JVM_PickupAllowed(JVM_JEDI, category == 0,
			category == 1, category == 2, category == 3, 0, 0));
		BOOST_CHECK(JVM_PickupAllowed(JVM_MERC, category == 0,
			category == 1, category == 2, category == 3, 0, 0));
		BOOST_CHECK_EQUAL(JVM_PickupAllowed(JVM_TANK, category == 0,
			category == 1, category == 2, category == 3, 0, 0), category == 2 || category == 3);
	}
	BOOST_CHECK(!JVM_PickupAllowed(JVM_MERC, 1, 0, 0, 0, 1, 0));
	for (int playerClass = JVM_JEDI; playerClass <= JVM_TANK; ++playerClass)
		BOOST_CHECK(JVM_PickupAllowed(playerClass, 0, 0, 0, 0, 0, 1));
}

BOOST_AUTO_TEST_CASE(merc_loadout_defaults_and_team_digits)
{
	const int melee = 1 << WP_MELEE;
	const int saber = 1 << WP_SABER;
	const int def = JVM_MERC_DEFAULT_WEAPONS;
	for (int w = WP_BRYAR_PISTOL; w <= WP_ROCKET_LAUNCHER; ++w)
		BOOST_CHECK(def & (1 << w));
	BOOST_CHECK(def & (1 << WP_CONCUSSION));
	BOOST_CHECK(def & (1 << WP_BRYAR_OLD));
	for (int w : {WP_NONE, WP_STUN_BATON, WP_MELEE, WP_SABER, WP_THERMAL, WP_TRIP_MINE, WP_DET_PACK})
		BOOST_CHECK_EQUAL(def & (1 << w), 0);
	BOOST_CHECK_EQUAL(JVM_MercStartingWeapons(def, 0, melee, saber), def | melee);
	BOOST_CHECK_EQUAL(JVM_MercStartingWeapons(saber, 0, melee, saber), melee);
	BOOST_CHECK_EQUAL(JVM_MercStartingWeapons(def, 1 << WP_BLASTER, melee, saber) & (1 << WP_BLASTER), 0);
	BOOST_CHECK_EQUAL(JVM_MercStartingWeapons(0, melee, melee, saber), melee);
	BOOST_CHECK_EQUAL(JVM_TeamArgClass("1"), JVM_JEDI);
	BOOST_CHECK_EQUAL(JVM_TeamArgClass("2"), JVM_MERC);
	BOOST_CHECK_EQUAL(JVM_TeamArgClass("3"), JVM_TANK);
	BOOST_CHECK_EQUAL(JVM_TeamArgClass("0"), -1);
	BOOST_CHECK_EQUAL(JVM_TeamArgClass("12"), -1);
	BOOST_CHECK_EQUAL(JVM_TeamArgClass("free"), -1);
	BOOST_CHECK_EQUAL(JVM_TeamArgClass(""), -1);
}

BOOST_AUTO_TEST_CASE(force_exceptions_and_shared_jump)
{
	for (int rank = -2; rank <= 5; ++rank) {
		const int expected = rank < 0 ? 0 : rank > 3 ? 3 : rank;
		BOOST_CHECK_EQUAL(JVM_ForceRank(JVM_MERC, 3, 1, 0, rank), expected);
		BOOST_CHECK_EQUAL(JVM_ForceRank(JVM_TANK, 3, 1, 0, rank), expected);
	}
	BOOST_CHECK_EQUAL(JVM_ForceRank(JVM_MERC, 3, 0, 1, 1), 0);
	BOOST_CHECK_EQUAL(JVM_ForceRank(JVM_TANK, 0, 0, 1, 1), 3);
	BOOST_CHECK_EQUAL(JVM_ForceRank(JVM_TANK, 3, 0, 0, 1), 0);
	BOOST_CHECK_EQUAL(JVM_ForceRank(JVM_MERC, 3, 0, 0, 1), 0);
	BOOST_CHECK_EQUAL(JVM_ForceRank(JVM_JEDI, 2, 0, 0, 1), 2);
}

BOOST_AUTO_TEST_CASE(legacy_force_cannot_regrant_disabled_class_jump)
{
	for (int mode = GT_FFA; mode <= GT_JOM; ++mode) {
		for (int playerClass = JVM_JEDI; playerClass <= JVM_TANK; ++playerClass) {
			const bool restricted = JVM_IsMode(mode) && playerClass != JVM_JEDI;
			BOOST_CHECK_EQUAL(JVM_AllowsLegacyForce(mode, playerClass), !restricted);
			if (!restricted)
				continue;
			for (int jumpRank = 0; jumpRank <= 3; ++jumpRank) {
				int known = 0;
				for (int power = 0; power < NUM_FORCE_POWERS; ++power) {
					const bool saber = power == FP_SABER_OFFENSE ||
						power == FP_SABER_DEFENSE || power == FP_SABERTHROW;
					int rank = JVM_ForceRank(playerClass, 3, power == FP_LEVITATION,
						saber, jumpRank);
					if (power == FP_LEVITATION && JVM_AllowsLegacyForce(mode, playerClass))
						rank = rank < 1 ? 1 : rank;
					const int expected = power == FP_LEVITATION ? jumpRank :
						playerClass == JVM_TANK && saber ? 3 : 0;
					BOOST_CHECK_EQUAL(rank, expected);
					if (rank)
						known |= 1 << power;
				}
				const int saberMask = (1 << FP_SABER_OFFENSE) |
					(1 << FP_SABER_DEFENSE) | (1 << FP_SABERTHROW);
				BOOST_CHECK_EQUAL(known, (jumpRank ? 1 << FP_LEVITATION : 0) |
					(playerClass == JVM_TANK ? saberMask : 0));
			}
		}
	}
}

BOOST_AUTO_TEST_CASE(global_force_disable_preserves_only_normal_freebies)
{
	for (int playerClass : {JVM_MERC, JVM_TANK}) {
		for (int jumpRank = 0; jumpRank <= 3; ++jumpRank) {
			for (int disabled : {0, (1 << NUM_FORCE_POWERS) - 1, 1 << FP_LEVITATION,
				1 << FP_SABERTHROW, 1 << FP_SABER_OFFENSE, 1 << FP_SABER_DEFENSE}) {
				int known = 0;
				for (int power = 0; power < NUM_FORCE_POWERS; ++power) {
					const bool jump = power == FP_LEVITATION;
					const bool freeSaber = power == FP_SABER_OFFENSE || power == FP_SABER_DEFENSE;
					const bool saber = freeSaber || power == FP_SABERTHROW;
					const int rank = JVM_ForceRankWithDisable(playerClass, 3, jump, saber,
						jumpRank, disabled & (1 << power), freeSaber);
					const int expected = jump ? ((disabled & (1 << power)) && jumpRank ? 1 : jumpRank) :
						playerClass == JVM_TANK && saber &&
						(freeSaber || !(disabled & (1 << power))) ? 3 : 0;
					BOOST_CHECK_EQUAL(rank, expected);
					if (rank)
						known |= 1 << power;
				}
				BOOST_CHECK_EQUAL(known & (1 << FP_HEAL), 0);
				BOOST_CHECK_EQUAL(known & (1 << FP_DRAIN), 0);
				if (disabled & (1 << FP_SABERTHROW))
					BOOST_CHECK_EQUAL(known & (1 << FP_SABERTHROW), 0);
			}
		}
	}
}

BOOST_AUTO_TEST_CASE(merc_bot_rate_gate_uses_final_gun_not_melee_or_saber)
{
	for (int mode = GT_FFA; mode <= GT_JOM; ++mode) {
		for (int playerClass = JVM_JEDI; playerClass <= JVM_TANK; ++playerClass) {
			for (int commandWeapon = -1; commandWeapon <= WP_NUM_WEAPONS; ++commandWeapon) {
				for (int currentWeapon = WP_NONE; currentWeapon < WP_NUM_WEAPONS; ++currentWeapon) {
					const int weapon = commandWeapon > WP_NONE && commandWeapon < WP_NUM_WEAPONS ?
						commandWeapon : currentWeapon;
					const bool gun = weapon > WP_NONE && weapon != WP_MELEE && weapon != WP_SABER;
					BOOST_CHECK_EQUAL(JVM_BotGunAttack(mode, playerClass, commandWeapon, currentWeapon,
						WP_NUM_WEAPONS, WP_MELEE, WP_SABER), JVM_IsMode(mode) && playerClass == JVM_MERC && gun);
				}
			}
		}
	}
	BOOST_CHECK(!JVM_BotGunAttack(GT_JOM, JVM_MERC, WP_NONE, WP_NUM_WEAPONS,
		WP_NUM_WEAPONS, WP_MELEE, WP_SABER));
}

BOOST_AUTO_TEST_CASE(map_healing_only_blocks_medpacs_not_other_merc_holdables)
{
	playerState_t ps = {};
	for (int active : {0, JVM_ACTIVE}) {
		for (int playerClass = JVM_JEDI; playerClass <= JVM_TANK; ++playerClass) {
			ps.stats[STAT_RESTRICTIONS] = active | (playerClass << JVM_CLASS_SHIFT);
			for (int item = HI_NONE; item < HI_NUM_HOLDABLE; ++item) {
				const bool medpac = item == HI_MEDPAC || item == HI_MEDPAC_BIG;
				BOOST_CHECK_EQUAL(BG_CanUseHoldable(&ps, item),
					!medpac || !active || playerClass == JVM_JEDI);
			}
			BOOST_CHECK(JVM_PickupAllowed(JVM_MERC, 0, 0, 0, 0, 0, 0));
		}
	}
}

BOOST_AUTO_TEST_CASE(merc_regeneration_overrides_live_interval_only_in_class_modes)
{
	for (int mode = GT_FFA; mode <= GT_JOM; ++mode) {
		for (int playerClass = JVM_JEDI; playerClass <= JVM_TANK; ++playerClass) {
			for (int interval : {25, 40, 200, 500, 7000}) {
				for (int mercInterval : {-200, 0, 1, 200, 1000}) {
					const int selected = JVM_IsMode(mode) && playerClass == JVM_MERC ?
						mercInterval : interval;
					BOOST_CHECK_EQUAL(JVM_ForceRegenInterval(mode, playerClass, interval,
						mercInterval), selected < 1 ? 1 : selected);
				}
			}
		}
	}
	BOOST_CHECK_EQUAL(JVM_ForceRegenInterval(GT_FFA, JVM_MERC, 0, 200), 1);
}

BOOST_AUTO_TEST_CASE(damage_scales_only_intended_sources)
{
	BOOST_CHECK_CLOSE(JVM_DamageScale(JVM_TANK, JVM_JEDI, 0, 0, .5f, 2, 0), .5f, .001f);
	BOOST_CHECK_CLOSE(JVM_DamageScale(JVM_JEDI, JVM_TANK, 1, 0, .5f, 2, 0), 2.0f, .001f);
	BOOST_CHECK_CLOSE(JVM_DamageScale(JVM_TANK, JVM_TANK, 1, 0, .5f, 2, 0), 1.0f, .001f);
	BOOST_CHECK_CLOSE(JVM_DamageScale(JVM_MERC, JVM_JEDI, 0, 1, .5f, 2, .75f), .25f, .001f);
	/* Lightning and punches are not marked as grip/flipkick by production. */
	BOOST_CHECK_EQUAL(JVM_DamageScale(JVM_MERC, JVM_JEDI, 0, 0, .5f, 2, 1), 1);
	BOOST_CHECK_EQUAL(JVM_DamageScale(JVM_MERC, JVM_JEDI, 0, 1, .5f, 2, 5), 0);
	BOOST_CHECK_EQUAL(JVM_DamageScale(JVM_MERC, JVM_JEDI, 0, 1, .5f, 2, -2), 1);
	BOOST_CHECK_EQUAL(JVM_ClampScale(-1, 10), 0);
	BOOST_CHECK_EQUAL(JVM_ClampScale(1000, 10), 10);
	BOOST_CHECK_EQUAL(JVM_ClampScale(std::numeric_limits<float>::quiet_NaN(), 10), 0);
}

BOOST_AUTO_TEST_CASE(fractional_grapple_is_frame_rate_independent)
{
	float fraction = 0;
	int total = 0;
	for (int i = 0; i < 125; ++i)
		total += JVM_GrappleDrain(&fraction, 8, 10);
	BOOST_CHECK_EQUAL(total, 10);
	BOOST_CHECK_SMALL(fraction, .0001f);
	BOOST_CHECK_EQUAL(JVM_GrappleDrain(&fraction, 1000, 10), 10);
	fraction = 0;
	for (int i = 0; i < 9; ++i)
		BOOST_CHECK_EQUAL(JVM_GrappleDrain(&fraction, 10, 10), 0);
	BOOST_CHECK_EQUAL(JVM_GrappleDrain(&fraction, 10, 10), 1);
	BOOST_CHECK_EQUAL(JVM_GrappleDrain(&fraction, -100, 10), 0);
	BOOST_CHECK_EQUAL(JVM_GrappleDrain(&fraction, 100, -1), 0);
	BOOST_CHECK_EQUAL(JVM_GrappleDrain(&fraction, 1000, 5000), 1000);
}

BOOST_AUTO_TEST_CASE(duels_preserve_only_owned_permitted_weapons)
{
	const int melee = 1 << WP_MELEE;
	const int saber = 1 << WP_SABER;
	const int pistol = 1 << WP_BRYAR_PISTOL;
	const int rocket = 1 << WP_ROCKET_LAUNCHER;
	const int owned = melee | saber | pistol;
	BOOST_CHECK(JVM_DuelAllowsGuns(GT_JOM, 1));
	BOOST_CHECK(!JVM_DuelAllowsGuns(GT_JOM, 0));
	BOOST_CHECK(!JVM_DuelAllowsGuns(GT_JVM, 1));
	BOOST_CHECK(!JVM_DuelAllowsGuns(GT_FFA, 1));
	BOOST_CHECK_EQUAL(JVM_AllowedWeapons(JVM_MERC, owned, 0, melee, saber), melee | pistol);
	BOOST_CHECK_EQUAL(JVM_AllowedWeapons(JVM_MERC, owned, pistol | melee, melee, saber), melee);
	BOOST_CHECK_EQUAL(JVM_AllowedWeapons(JVM_MERC, 0, 0, melee, saber), 0);
	BOOST_CHECK_EQUAL(JVM_AllowedWeapons(JVM_TANK, owned, 0, melee, saber), saber);
	BOOST_CHECK_EQUAL(JVM_AllowedWeapons(JVM_JEDI, owned, 0, melee, saber), saber);
	BOOST_CHECK_EQUAL(JVM_AllowedWeapons(JVM_MERC, owned, 0, melee, saber) & rocket, 0);
}

BOOST_AUTO_TEST_CASE(flipkick_permission_is_replicated_and_mode_gated)
{
	const int merc = JVM_ACTIVE | (JVM_MERC << JVM_CLASS_SHIFT);
	BOOST_CHECK_EQUAL(JVM_FlipkickSetting(merc, 3), 0);
	BOOST_CHECK_EQUAL(JVM_FlipkickSetting(merc | JVM_FLIPKICK, 0), 2);
	BOOST_CHECK_EQUAL(JVM_FlipkickSetting(JVM_MERC << JVM_CLASS_SHIFT, 3), 3);
	BOOST_CHECK_EQUAL(JVM_FlipkickSetting(JVM_ACTIVE | JVM_FLIPKICK, 1), 1);
	BOOST_CHECK_EQUAL(JVM_FlipkickSetting(JVM_ACTIVE | (JVM_TANK << JVM_CLASS_SHIFT), 3), 3);
}

BOOST_AUTO_TEST_CASE(countergrip_and_tank_pushpull_are_replicated)
{
	for (int playerClass = JVM_JEDI; playerClass <= JVM_TANK; ++playerClass) {
		for (int active : {0, JVM_ACTIVE}) {
			for (int enabled : {0, JVM_COUNTERGRIP | JVM_PUSHPULL}) {
				const int restrictions = active | enabled | (playerClass << JVM_CLASS_SHIFT);
				BOOST_CHECK_EQUAL(JVM_CounterGrip(restrictions),
					active && enabled && playerClass == JVM_MERC);
				BOOST_CHECK_EQUAL(JVM_TankPushPull(restrictions, 1),
					active && enabled && playerClass == JVM_TANK);
				BOOST_CHECK(!JVM_TankPushPull(restrictions, 0));
				BOOST_CHECK_EQUAL(JVM_ReplicatedClass(restrictions), playerClass);
			}
		}
	}
}

BOOST_AUTO_TEST_CASE(passive_absorb_is_limited_to_jedi_push_pull_grip)
{
	for (int mode = GT_FFA; mode <= GT_JOM; ++mode) {
		for (int playerClass = JVM_JEDI; playerClass <= JVM_TANK; ++playerClass) {
			for (int enabled : {0, 1}) {
				for (int power = 0; power < NUM_FORCE_POWERS; ++power) {
					for (int rank = 1; rank <= 3; ++rank) {
						const bool eligible = power == FP_PUSH || power == FP_PULL || power == FP_GRIP;
						BOOST_CHECK_EQUAL(JVM_PassiveAbsorb(mode, playerClass, enabled, eligible, rank),
							JVM_IsMode(mode) && playerClass == JVM_JEDI && enabled && eligible ? 0 : -1);
					}
				}
			}
		}
	}
}

BOOST_AUTO_TEST_CASE(jedi_and_merc_gripkick_reductions_are_independent_inputs)
{
	BOOST_CHECK_CLOSE(JVM_DamageScale(JVM_JEDI, JVM_MERC, 0, 1, .5f, 2, .25f), .75f, .001f);
	BOOST_CHECK_CLOSE(JVM_DamageScale(JVM_MERC, JVM_JEDI, 0, 1, .5f, 2, .75f), .25f, .001f);
	BOOST_CHECK_EQUAL(JVM_DamageScale(JVM_JEDI, JVM_MERC, 0, 0, .5f, 2, 1), 1);
	BOOST_CHECK_CLOSE(JVM_DamageScale(JVM_TANK, JVM_JEDI, 0, 1, .5f, 2, 1), .5f, .001f);
}

BOOST_AUTO_TEST_CASE(disarm_probability_is_inverse_distance_and_bounded)
{
	BOOST_CHECK_EQUAL(JVM_DisarmChance(50, 100), 50);
	BOOST_CHECK_EQUAL(JVM_DisarmChance(50, 200), 25);
	BOOST_CHECK_EQUAL(JVM_DisarmChance(50, 50), 100);
	BOOST_CHECK_EQUAL(JVM_DisarmChance(50, 0), 100);
	BOOST_CHECK_EQUAL(JVM_DisarmChance(0, 100), 0);
	BOOST_CHECK_EQUAL(JVM_DisarmChance(-1, 100), 0);
	BOOST_CHECK_EQUAL(JVM_DisarmChance(200, 100), 100);
	BOOST_CHECK_EQUAL(JVM_DisarmChance(std::numeric_limits<float>::quiet_NaN(), 100), 0);
}

BOOST_AUTO_TEST_CASE(health_regeneration_keeps_fractional_amounts_and_intervals)
{
	float fraction = 0;
	int residual = 0, health = 90;
	for (int i = 0; i < 500; ++i)
		health += JVM_HealthRegen(&fraction, &residual, 8, .25f, 1000, health, 100);
	BOOST_CHECK_EQUAL(health, 91);
	BOOST_CHECK_EQUAL(residual, 0);
	BOOST_CHECK_SMALL(fraction, .0001f);
	BOOST_CHECK_EQUAL(JVM_HealthRegen(&fraction, &residual, 10000, 100, 1000, 99, 100), 1);
	BOOST_CHECK_EQUAL(JVM_HealthRegen(&fraction, &residual, 1000, 1, 1000, 100, 100), 0);
	BOOST_CHECK_EQUAL(JVM_HealthRegen(&fraction, &residual, 1000, 1, 1000, 110, 100), 0);
	BOOST_CHECK_EQUAL(JVM_HealthRegen(&fraction, &residual, 1000, 1, 1000, 0, 100), 0);
	BOOST_CHECK_EQUAL(JVM_HealthRegen(&fraction, &residual, 1000, -1, 1000, 90, 100), 0);
	BOOST_CHECK_EQUAL(JVM_HealthRegen(&fraction, &residual, 1000, 0, 1000, 90, 100), 0);
	BOOST_CHECK_EQUAL(JVM_HealthRegen(&fraction, &residual, 1000,
		std::numeric_limits<float>::quiet_NaN(), 1000, 90, 100), 0);
	BOOST_CHECK_EQUAL(JVM_HealthRegen(&fraction, &residual, 1, 1, 0, 90, 100), 1);
	fraction = .5f;
	residual = 500;
	BOOST_CHECK_EQUAL(JVM_HealthRegen(&fraction, &residual, 1000, 1, 1000, 0, 100), 0);
	BOOST_CHECK_EQUAL(residual, 0);
	BOOST_CHECK_EQUAL(fraction, 0);
	fraction = .5f;
	residual = 500;
	BOOST_CHECK_EQUAL(JVM_HealthRegen(&fraction, &residual, 0, 1, 1000, 90, 100), 0);
	BOOST_CHECK_EQUAL(residual, 500);
	BOOST_CHECK_EQUAL(fraction, .5f);
	fraction = 0;
	residual = std::numeric_limits<int>::max() - 1;
	BOOST_CHECK_EQUAL(JVM_HealthRegen(&fraction, &residual, 20, 1,
		std::numeric_limits<int>::max(), 90, 100), 1);
	BOOST_CHECK_EQUAL(residual, 19);
	BOOST_CHECK_EQUAL(JVM_HealthRegen(&fraction, &residual,
		std::numeric_limits<int>::max(), 1, 1, 90, 100), 10);
}

BOOST_AUTO_TEST_CASE(class_cycle_order)
{
	BOOST_CHECK_EQUAL(JVM_NextClass(JVM_JEDI), JVM_MERC);
	BOOST_CHECK_EQUAL(JVM_NextClass(JVM_MERC), JVM_TANK);
	BOOST_CHECK_EQUAL(JVM_NextClass(JVM_TANK), JVM_JEDI);
}

BOOST_AUTO_TEST_CASE(weapon_denial_preserves_nonweapon_and_protected_touches)
{
	for (int mode = GT_FFA; mode <= GT_JOM; ++mode) {
		for (int playerClass = JVM_JEDI; playerClass <= JVM_TANK; ++playerClass) {
			for (int flags = 0; flags < 128; ++flags) {
				const bool weapon = flags & 1;
				const bool saber = flags & 2;
				const bool duel = flags & 4;
				const bool race = flags & 8;
				const bool spectator = flags & 16;
				const bool jediMaster = flags & 32;
				const bool ownerBlocked = flags & 64;
				BOOST_CHECK_EQUAL(JVM_WeaponDenialAllowed(mode, playerClass, weapon, saber,
					duel, race, spectator, jediMaster, ownerBlocked),
					JVM_IsMode(mode) && playerClass != JVM_MERC && flags == 1);
			}
		}
	}
}

BOOST_AUTO_TEST_CASE(cycle_only_schedules_an_actual_death_not_class_or_team_changes)
{
	for (int mode = GT_FFA; mode <= GT_JOM; ++mode) {
		for (int playerClass = JVM_JEDI; playerClass <= JVM_TANK; ++playerClass) {
			for (int flags = 0; flags < 32; ++flags) {
				const bool enabled = flags & 1;
				const bool dead = flags & 2;
				const bool spectator = flags & 4;
				const bool classChanging = flags & 8;
				const bool teamChanging = flags & 16;
				BOOST_CHECK_EQUAL(JVM_DeathNextClass(mode, playerClass, enabled, dead,
					spectator, classChanging, teamChanging),
					mode == GT_JOM && flags == 3 ? JVM_NextClass(playerClass) + 1 : 0);
			}
		}
	}
}

BOOST_AUTO_TEST_CASE(disabled_passive_absorb_defers_every_power_to_ordinary_behavior)
{
	for (int power = 0; power < NUM_FORCE_POWERS; ++power) {
		const bool eligible = power == FP_PUSH || power == FP_PULL || power == FP_GRIP;
		BOOST_CHECK_EQUAL(JVM_PassiveAbsorb(GT_JOM, JVM_JEDI, 0, eligible, 3), -1);
	}
}

BOOST_AUTO_TEST_SUITE_END()
