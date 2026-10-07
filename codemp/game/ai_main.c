/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

/*****************************************************************************
 * name:		ai_main.c
 *
 * desc:		Quake3 bot AI
 *
 * $Archive: /MissionPack/code/game/ai_main.c $
 * $Author: osman $
 * $Revision: 1.5 $
 * $Modtime: 6/06/01 1:11p $
 * $Date: 2003/03/15 23:43:59 $
 *
 *****************************************************************************/


#include "g_local.h"
#include "qcommon/q_shared.h"
#include "botlib/botlib.h"		//bot lib interface
#include "botlib/be_aas.h"
#include "botlib/be_ea.h"
#include "botlib/be_ai_char.h"
#include "botlib/be_ai_chat.h"
#include "botlib/be_ai_gen.h"
#include "botlib/be_ai_goal.h"
#include "botlib/be_ai_move.h"
#include "botlib/be_ai_weap.h"
//
#include "ai_main.h"
#include "ai_combat_tuning.h"
#include "w_saber.h"
//
#include "chars.h"
#include "inv.h"

/*
#define BOT_CTF_DEBUG	1
*/

#define BOT_THINK_TIME	0
#define NEWBOTAI_PTK_FORCE_BUDGET 40
#define NEWBOTAI_ABSORB_BAIT_WINDOW_MS 1500
#define NEWBOTAI_FLIPKICK_PREFERRED_RANGE 180.0f
#define NEWBOTAI_IMMEDIATE_FLIPKICK_RANGE 135.0f
#define NEWBOTAI_IMMEDIATE_FLIPKICK_CONTACT_RANGE 90.0f
#define NEWBOTAI_PULL_STUN_ONLY_RANGE 220.0f

//bot states
bot_state_t	*botstates[MAX_CLIENTS];
//number of bots
int numbots;
//floating point time
float floattime;
//time to do a regular update
float regularupdate_time;
//

//for siege:
extern int rebel_attackers;
extern int imperial_attackers;

boteventtracker_t gBotEventTracker[MAX_CLIENTS];

//rww - new bot cvars..
vmCvar_t bot_normgpath;
#ifndef FINAL_BUILD
vmCvar_t bot_getinthecarrr;
#endif

#ifdef _DEBUG
vmCvar_t bot_nogoals;
vmCvar_t bot_debugmessages;
#endif

vmCvar_t bot_attachments;
vmCvar_t bot_camp;

vmCvar_t bot_wp_info;
vmCvar_t bot_wp_edit;
vmCvar_t bot_wp_clearweight;
vmCvar_t bot_wp_distconnect;
vmCvar_t bot_wp_visconnect;
//end rww

static int BotGetNewBotAITargetMode(void);
qboolean PM_SaberInStart( int move );
qboolean PM_SaberInReturn( int move );
qboolean PM_SaberInTransition( int move );
qboolean PM_SaberInBrokenParry( int move );
qboolean BG_InKnockDown(int anim);
void saberBackToOwner(gentity_t *saberent);
extern const float pm_airaccelerate;
static qboolean BotTargetModeAllowsBotEnemies(int targetMode);
static qboolean BotTargetModePassesScanFilter(int targetMode, gentity_t *ent, qboolean preferredHumansOnly);
static qboolean BotTargetModeIsForceDuelOnly(int targetMode);
static qboolean NewBotAI_InFFAExploreWindow(bot_state_t *bs, int targetMode);
static void NewBotAI_RunNavigationOrAlone(bot_state_t *bs, float thinktime);
static qboolean NewBotAI_ClassifyForwardObstacle(bot_state_t *bs, const vec3_t moveDir, qboolean *requiresHopOut, int *hitEntityOut);
static qboolean NewBotAI_HasReachableFloorAtProbe(bot_state_t *bs, const vec3_t probeOrigin, float dropHeight, float *floorZOut);
static qboolean NewBotAI_TouchingWallNotEnemy(bot_state_t *bs);
static qboolean NewBotAI_HandleClimbableForwardObstacle(bot_state_t *bs, const vec3_t moveDir);
static qboolean NewBotAI_ShouldWallrunAgainstWalls(bot_state_t *bs);
static qboolean NewBotAI_ShouldAvoidDiagonalWallrun(bot_state_t *bs);
static int NewBotAI_GetWallAvoidCooldownMs(void);
static float NewBotAI_GetWallEscapeTurnAngle(void);
static void NewBotAI_RetreatDiagonal(bot_state_t *bs, qboolean moveLeft);
static int BotGetLowHangingFruitHP(void);
static float BotGetLowHangingFruitDistance(void);
static float BotGetAggressionBias(bot_state_t *bs);
static float BotGetAimSpeedLevel(void);
static float BotGetAimSpeedFactor(bot_state_t *bs);
static float BotGetAimSpeedMaxChange(bot_state_t *bs, float legacyMaxChange);
static float BotGetExtraPenaltyScaleForSkill(bot_state_t *bs);
static int BotGetReflexScaledResponseDelayMs(bot_state_t *bs);
static float BotGetChanceBiasPercent(float value);
static float BotGetMistakeBiasChance(bot_state_t *bs);
static float BotGetGripMistakeBiasChance(bot_state_t *bs);
static int NewBotAI_GetGripEscapeDelayMs(bot_state_t *bs);
static int NewBotAI_GetGripNeverEscapeChance(bot_state_t *bs);
static int NewBotAI_GetGripPushInsteadChance(bot_state_t *bs);
static int NewBotAI_GetGripSpeedMistakeChance(bot_state_t *bs);
static int BotGetAggressionWeightedBonus(bot_state_t *bs, float biasPercent, int maxBonus, qboolean aggressiveOnly);
static int BotGetDrainHoldBiasMs(bot_state_t *bs);
static int BotGetHealthBiasThreshold(void);
static int NewBotAI_GetAntiDrainWeight(bot_state_t *bs);
static float BotGetLightningMaxDistance(bot_state_t *bs);
static float BotGetLightningStartDistance(bot_state_t *bs);
static qboolean NewBotAI_IsWithinLightningRange(bot_state_t *bs);
static int NewBotAI_GetLightningWeight(bot_state_t *bs);
static int NewBotAI_GetSpeedAttackWeight(bot_state_t *bs);
static int NewBotAI_GetPTKWeight(bot_state_t *bs);
static qboolean NewBotAI_IsGetupAnim(int anim);
static qboolean NewBotAI_IsForceGetupAnim(int anim);
static qboolean NewBotAI_IsKnockdownRecoveryRoll(int anim);
static qboolean NewBotAI_IsSaberSwingStartWindow(bot_state_t *bs);
static qboolean NewBotAI_CanAttemptFlipkick(bot_state_t *bs);
static qboolean NewBotAI_IsSaberOnlyDuel(bot_state_t *bs);
static qboolean NewBotAI_CanUseForcePowerNow(bot_state_t *bs, forcePowers_t power);
static qboolean NewBotAI_IsFlipkickSetupReady(bot_state_t *bs);
qboolean BG_InRoll3(int anim);
static void NewBotAI_RetreatStraight(bot_state_t *bs);
static float NewBotAI_GetEnemyClosingSpeed(bot_state_t *bs);
static float NewBotAI_GetSelfFacingErrorToEnemy(bot_state_t *bs);
static void NewBotAI_SaberDuelIndecisionFallback(bot_state_t *bs, qboolean horizontalSwingStart);
static void NewBotAI_PrepareHorizontalSwingStart(bot_state_t *bs);
static void NewBotAI_ApplyHorizontalSwingMove(bot_state_t *bs);
static qboolean NewBotAI_FanHoldMayStartSwing(bot_state_t *bs);
static void NewBotAI_ResetFanChain(bot_state_t *bs);
static void NewBotAI_ApplyFanDwellYaw(bot_state_t *bs, qboolean aimRewritten);
static float NewBotAI_GetEnemyDistance2D(bot_state_t *bs);
static qboolean NewBotAI_ShouldCounterSwing(bot_state_t *bs);
static qboolean NewBotAI_SwingStartFootingAllows(bot_state_t *bs);
static qboolean NewBotAI_SwingChainFootingAllows(bot_state_t *bs);
static int NewBotAI_GetDecisionMistakeChance(bot_state_t *bs);
static qboolean NewBotAI_MayStartAirborneSwing(bot_state_t *bs);
static int NewBotAI_GetEnemyStimulusToken(bot_state_t *bs);
static qboolean NewBotAI_HasValidCurrentEnemy(bot_state_t *bs);
static qboolean NewBotAI_CanInitiateFlipkickUnderFanPressure(bot_state_t *bs);

// Fan-chain phases (see NewBotAI_PrepareHorizontalSwingStart): HOLD owns exclusive
// strafe+attack long enough to start the horizontal swing, and DWELL is the free-movement
// gap before the next alternating hold.
enum {
	FAN_PHASE_INACTIVE = 0,
	FAN_PHASE_HOLD,
	FAN_PHASE_DWELL
};

enum {
	FAN_PACKAGE_NONE = 0,
	FAN_PACKAGE_YELLOW_PRESSURE,
	FAN_PACKAGE_STAFF_PRESSURE
};

static qboolean NewBotAI_CanUseSaberThrowDefenseBreakForce(bot_state_t *bs, qboolean preferPull);
static void NewBotAI_TryRandomHop(bot_state_t *bs);
static int NewBotAI_GetNextHopIntervalMs(bot_state_t *bs, float hopFrequency);
static void NewBotAI_PushHopRetryCooldown(bot_state_t *bs);
static qboolean NewBotAI_ShouldUseCombatHop(bot_state_t *bs, qboolean forceImmediate);
static void NewBotAI_ConsumeCombatHop(bot_state_t *bs);
static float NewBotAI_GetPullkickTimeToKickRange(bot_state_t *bs, qboolean pullActingOnBot, int *timingModeOut);
static void NewBotAI_SchedulePullkickJump(bot_state_t *bs);
static qboolean NewBotAI_IsPullkickOpportunity(bot_state_t *bs);
static int NewBotAI_GetDrainTapTargetTicks(bot_state_t *bs);
static int NewBotAI_GetDrainTapTargetCost(bot_state_t *bs);
static qboolean NewBotAI_IsPullkickDrainWindow(bot_state_t *bs);
static qboolean NewBotAI_IsDrainlockAdvantage(bot_state_t *bs);
static void NewBotAI_UpdateHealDrainlockState(bot_state_t *bs);
static qboolean NewBotAI_ShouldHealDrainlock(bot_state_t *bs);
static qboolean NewBotAI_ShouldDrainlockDeep(bot_state_t *bs);
int NewBotAI_GetDrain(bot_state_t *bs);
int NewBotAI_GetGrip(bot_state_t *bs);
static qboolean NewBotAI_HasClearAdvantage(bot_state_t *bs);
static qboolean NewBotAI_ShouldPressAdvantage(bot_state_t *bs);
static qboolean NewBotAI_HasDroppedOwnSaber(bot_state_t *bs);
static qboolean NewBotAI_IsCombatProgressStalled(bot_state_t *bs);
static qboolean NewBotAI_IsEnemySaberReturning(bot_state_t *bs);
static qboolean NewBotAI_GetEnemySaberFlightThreat(bot_state_t *bs, float *forwardDistOut, float *saberSpeedOut, qboolean *isReturningOut);
static qboolean NewBotAI_IsEnemySaberThreatImminent(bot_state_t *bs);
static qboolean NewBotAI_IsIncomingSaberThrowLethal(bot_state_t *bs);
static qboolean NewBotAI_IsLethalEnemySwingImminent(bot_state_t *bs);
static qboolean NewBotAI_IsCertainDeathWindow(bot_state_t *bs);
static void NewBotAI_FilterDefensiveRollInput(bot_state_t *bs, bot_input_t *bi);
static qboolean NewBotAI_ShouldPlaySafeDrainVsSaberThrow(bot_state_t *bs);
static qboolean NewBotAI_ShouldJumpDrainVsSaberThrow(bot_state_t *bs);
static qboolean NewBotAI_ShouldEmergencyDrainRollSaberThrow(bot_state_t *bs);
static qboolean NewBotAI_HasStableSaberThrowDefenseAlignment(bot_state_t *bs);
static qboolean NewBotAI_ShouldStabilizeAgainstEnemySaberThrow(bot_state_t *bs);
static void NewBotAI_ApplySidewaysDrainRoll(bot_state_t *bs, qboolean moveBack);
static qboolean NewBotAI_ShouldUseSafePushWindowWhilePulled(bot_state_t *bs);
static qboolean NewBotAI_HasFreePullkickWindow(bot_state_t *bs);
static qboolean NewBotAI_IsBeingPulledTowardEnemy(bot_state_t *bs);
static qboolean NewBotAI_IsImmediateFlipkickContact(bot_state_t *bs);
static void NewBotAI_ClearRandomStrafeOverlay(bot_state_t *bs);
static void NewBotAI_RollRandomStrafeOverlay(bot_state_t *bs, int minDuration, int maxDuration, qboolean retreating);
static void NewBotAI_ApplyRandomStrafePattern(bot_state_t *bs);
static void NewBotAI_StartEscapeYawOverride(bot_state_t *bs, int durationMs);
static qboolean NewBotAI_IsDuelStrafeSuppressed(bot_state_t *bs);
static qboolean NewBotAI_CanControlSaber(bot_state_t *bs);
static void NewBotAI_RunSaberTechniques(bot_state_t *bs);
static void NewBotAI_ApplySaberTechniqueInput(bot_state_t *bs, bot_input_t *bi, int time);
static void NewBotAI_UpdateSaberDefense(bot_state_t *bs, int time);
static void NewBotAI_ApplySaberThrowInput(bot_state_t *bs, bot_input_t *bi);
static void NewBotAI_ApplySaberHandover(bot_state_t *bs, bot_input_t *bi, int time);
static void NewBotAI_ApplySaberAdvance(bot_state_t *bs, bot_input_t *bi);
static int NewBotAI_GetWallStrafeAwayDir(bot_state_t *bs);
static qboolean NewBotAI_ShouldPursueTargetThroughWaypoints(bot_state_t *bs, gentity_t *enemy);
static float NewBotAI_GetRecoveryYawSpeedDegPerSec(void);
static int NewBotAI_GetRecoveryYawIntervalMs(void);
static int NewBotAI_GetWallRedirectIntervalMs(void);
static void NewBotAI_ResetRecoveryMovement(bot_state_t *bs);
static void NewBotAI_ClearLostSightCombatInput(bot_state_t *bs);
static void NewBotAI_PrepareWaypointHandoff(bot_state_t *bs, qboolean clearEnemyLock);
static qboolean NewBotAI_ShouldForceLostSightWaypointReset(bot_state_t *bs);
static void NewBotAI_ClearLightningBurst(bot_state_t *bs);
static qboolean NewBotAI_IsEnemyGetupPushWindow(bot_state_t *bs);
static qboolean NewBotAI_IsEnemyPreGetupKnockdownState(bot_state_t *bs);
static qboolean NewBotAI_ShouldAbortChargedThrowForGetupPush(bot_state_t *bs);
static qboolean NewBotAI_TryAbortChargedThrowIntoPullkick(bot_state_t *bs);
static qboolean NewBotAI_HasTimedFanEntryWindow(bot_state_t *bs);
static qboolean NewBotAI_IsRecoveryMovementActive(bot_state_t *bs);
static qboolean NewBotAI_HasExclusiveFlipkickMovement(bot_state_t *bs);
static int BotGetNewBotAITargetMode(void);
static qboolean BotTargetModeAllowsBotEnemies(int targetMode);
static int BotGetTargetTimeoutMs(void);
static qboolean NewBotAI_ShouldRetainLostSightTarget(bot_state_t *bs, gentity_t *enemy);
static void NewBotAI_ClearCurrentEnemyLock(bot_state_t *bs);
static void NewBotAI_FaceEntityImmediately(bot_state_t *bs, gentity_t *target);
static qboolean NewBotAI_HasSafeSaberThrowClearance(bot_state_t *bs);
static qboolean NewBotAI_IsEnemyReadyToBlockFreshSaberThrow(bot_state_t *bs);
static gentity_t *NewBotAI_GetPendingDuelChallenger(bot_state_t *bs, int targetMode, int *duelTypeOut);
static qboolean NewBotAI_IsUnavailableDuelBot(bot_state_t *bs, gentity_t *ent, int targetMode);

#define NEWBOTAI_DRAIN_TICK_MSEC 100
#define NEWBOTAI_COMBAT_DISENGAGE_COOLDOWN_MS 2500
#define NEWBOTAI_TARGET_COMMIT_DISTANCE 768.0f
#define NEWBOTAI_ESCAPE_YAW_SPEED NEWBOTAI_TUNING_ESCAPE_YAW_SPEED
#define NEWBOTAI_ESCAPE_YAW_OVERRIDE_MS 250
#define NEWBOTAI_JUMP_ATTACK_GATE_MS 40
#define NEWBOTAI_NAV_RECOVERY_MODE_DIRECT 0
static qboolean NewBotAI_HandleRecoveryRollForcepower(bot_state_t *bs);
static qboolean NewBotAI_IsBetweenOwnSaberAndEnemy(bot_state_t *bs);
static qboolean NewBotAI_ShouldCloseGapVsEnemySaberThrow(bot_state_t *bs);
static void NewBotAI_AdjustSaberThrowLead(bot_state_t *bs);
static qboolean NewBotAI_SaberThrowTrace(bot_state_t *bs, const vec3_t angles,
	newbotai_throw_phase_t phase, vec3_t endpoint);
static qboolean NewBotAI_UpdateSaberThrowPass(bot_state_t *bs, qboolean *headingToTarget);
static qboolean NewBotAI_SaberThrowShouldHold(bot_state_t *bs, int heldMs, qboolean hardRecall);
static void NewBotAI_RecordSaberThrowDecision(bot_state_t *bs, int heldMs, qboolean hold, const char *reason);
static void NewBotAI_ConfigureSaberThrow(bot_state_t *bs);
static void NewBotAI_TrySaberThrowDefenseBreak(bot_state_t *bs);
static void NewBotAI_ApplyPullMistake(bot_state_t *bs);
static void NewBotAI_ApplyGripEscapePullMistake(bot_state_t *bs);
static qboolean BotNav_CheckFallingHazard(bot_state_t *bs, vec3_t moveDir, qboolean inCombat);
static void BotSFJ_DebugReject(bot_state_t *bs, const char *reason);
int WaitingForNow(bot_state_t *bs, vec3_t goalpos);
static void BotSFJ_SelectIntent(bot_state_t *bs);
static void BotSFJ_ApplyInput(bot_state_t *bs, bot_input_t *bi, int time, int elapsedTime);
static qboolean NewBotAI_ShouldConserveForce(bot_state_t *bs);
static qboolean NewBotAI_HasWaypointNavigation(void);
static qboolean NewBotAI_ShouldSkipPullForNaturalFlipkickPTK(bot_state_t *bs);
static void NewBotAI_ApplyJumpAttackGate(bot_state_t *bs);
qboolean NewBotAI_IsEnemyPullable(bot_state_t *bs);
static qboolean NewBotAI_IsDirectPathToEnemyBlocked(bot_state_t *bs);
static qboolean NewBotAI_ShouldForcePulledFlipkickOverride(bot_state_t *bs, int *timeToKickRangeMsOut, int *timingModeOut);
static void NewBotAI_ApplyFanAttackWobble(bot_state_t *bs);
static void NewBotAI_ApplyHumanSwingAimOffset(bot_state_t *bs);
static float NewBotAI_GetApproachAimYawOffset(bot_state_t *bs);
void Cmd_EngageDuel_f(gentity_t *ent, int dueltype);
extern void DownedSaberThink(gentity_t *saberent);
extern void CreateNewWP(vec3_t origin, int flags);

static qboolean BotHasActiveHumanPlayers(void);

wpobject_t *flagRed;
wpobject_t *oFlagRed;
wpobject_t *flagBlue;
wpobject_t *oFlagBlue;

gentity_t *eFlagRed;
gentity_t *droppedRedFlag;
gentity_t *eFlagBlue;
gentity_t *droppedBlueFlag;

char *ctfStateNames[] = {
	"CTFSTATE_NONE",
	"CTFSTATE_ATTACKER",
	"CTFSTATE_DEFENDER",
	"CTFSTATE_RETRIEVAL",
	"CTFSTATE_GUARDCARRIER",
	"CTFSTATE_GETFLAGHOME",
	"CTFSTATE_MAXCTFSTATES"
};

char *ctfStateDescriptions[] = {
	"I'm not occupied",
	"I'm attacking the enemy's base",
	"I'm defending our base",
	"I'm getting our flag back",
	"I'm escorting our flag carrier",
	"I've got the enemy's flag"
};

char *siegeStateDescriptions[] = {
	"I'm not occupied",
	"I'm attempting to complete the current objective",
	"I'm preventing the enemy from completing their objective"
};

char *teamplayStateDescriptions[] = {
	"I'm not occupied",
	"I'm following my squad commander",
	"I'm assisting my commanding",
	"I'm attempting to regroup and form a new squad"
};

void BotStraightTPOrderCheck(gentity_t *ent, int ordernum, bot_state_t *bs)
{
	switch (ordernum)
	{
	case 0:
		if (bs->squadLeader == ent)
		{
			bs->teamplayState = 0;
			bs->squadLeader = NULL;
		}
		break;
	case TEAMPLAYSTATE_FOLLOWING:
		bs->teamplayState = ordernum;
		bs->isSquadLeader = 0;
		bs->squadLeader = ent;
		bs->wpDestSwitchTime = 0;
		break;
	case TEAMPLAYSTATE_ASSISTING:
		bs->teamplayState = ordernum;
		bs->isSquadLeader = 0;
		bs->squadLeader = ent;
		bs->wpDestSwitchTime = 0;
		break;
	default:
		bs->teamplayState = ordernum;
		break;
	}
}

void BotSelectWeapon(int client, int weapon)
{
	if (weapon <= WP_NONE)
	{
//		assert(0);
		return;
	}
	trap->EA_SelectWeapon(client, weapon);
}

void BotReportStatus(bot_state_t *bs)
{
	if (level.gametype == GT_TEAM)
	{
		trap->EA_SayTeam(bs->client, teamplayStateDescriptions[bs->teamplayState]);
	}
	else if (level.gametype == GT_SIEGE)
	{
		trap->EA_SayTeam(bs->client, siegeStateDescriptions[bs->siegeState]);
	}
	else if (level.gametype == GT_CTF || level.gametype == GT_CTY)
	{
		trap->EA_SayTeam(bs->client, ctfStateDescriptions[bs->ctfState]);
	}
}

//accept a team order from a player
void BotOrder(gentity_t *ent, int clientnum, int ordernum)
{
	int stateMin = 0;
	int stateMax = 0;
	int i = 0;

	if (!ent || !ent->client || !ent->client->sess.teamLeader)
	{
		return;
	}

	if (clientnum != -1 && !botstates[clientnum])
	{
		return;
	}

	if (clientnum != -1 && !OnSameTeam(ent, &g_entities[clientnum]))
	{
		return;
	}

	if (level.gametype != GT_CTF && level.gametype != GT_CTY && level.gametype != GT_SIEGE && level.gametype != GT_TEAM)
	{
		return;
	}

	if (level.gametype == GT_CTF || level.gametype == GT_CTY)
	{
		stateMin = CTFSTATE_NONE;
		stateMax = CTFSTATE_MAXCTFSTATES;
	}
	else if (level.gametype == GT_SIEGE)
	{
		stateMin = SIEGESTATE_NONE;
		stateMax = SIEGESTATE_MAXSIEGESTATES;
	}
	else if (level.gametype == GT_TEAM)
	{
		stateMin = TEAMPLAYSTATE_NONE;
		stateMax = TEAMPLAYSTATE_MAXTPSTATES;
	}

	if ((ordernum < stateMin && ordernum != -1) || ordernum >= stateMax)
	{
		return;
	}

	if (clientnum != -1)
	{
		if (ordernum == -1)
		{
			BotReportStatus(botstates[clientnum]);
		}
		else
		{
			BotStraightTPOrderCheck(ent, ordernum, botstates[clientnum]);
			botstates[clientnum]->state_Forced = ordernum;
			botstates[clientnum]->chatObject = ent;
			botstates[clientnum]->chatAltObject = NULL;
			if (BotDoChat(botstates[clientnum], "OrderAccepted", 1))
			{
				botstates[clientnum]->chatTeam = 1;
			}
		}
	}
	else
	{
		while (i < MAX_CLIENTS)
		{
			if (botstates[i] && OnSameTeam(ent, &g_entities[i]))
			{
				if (ordernum == -1)
				{
					BotReportStatus(botstates[i]);
				}
				else
				{
					BotStraightTPOrderCheck(ent, ordernum, botstates[i]);
					botstates[i]->state_Forced = ordernum;
					botstates[i]->chatObject = ent;
					botstates[i]->chatAltObject = NULL;
					if (BotDoChat(botstates[i], "OrderAccepted", 0))
					{
						botstates[i]->chatTeam = 1;
					}
				}
			}

			i++;
		}
	}
}

//See if bot is mindtricked by the client in question
int BotMindTricked(int botClient, int enemyClient)
{
	forcedata_t *fd;

	if (!g_entities[enemyClient].client)
	{
		return 0;
	}

	fd = &g_entities[enemyClient].client->ps.fd;

	if (!fd)
	{
		return 0;
	}

	if (botClient > 47)
	{
		if (fd->forceMindtrickTargetIndex4 & (1 << (botClient-48)))
		{
			return 1;
		}
	}
	else if (botClient > 31)
	{
		if (fd->forceMindtrickTargetIndex3 & (1 << (botClient-32)))
		{
			return 1;
		}
	}
	else if (botClient > 15)
	{
		if (fd->forceMindtrickTargetIndex2 & (1 << (botClient-16)))
		{
			return 1;
		}
	}
	else
	{
		if (fd->forceMindtrickTargetIndex & (1 << botClient))
		{
			return 1;
		}
	}

	return 0;
}

int BotGetWeaponRange(bot_state_t *bs);
int PassLovedOneCheck(bot_state_t *bs, gentity_t *ent);

void ExitLevel( void );

void QDECL BotAI_Print(int type, char *fmt, ...) { return; }

qboolean WP_ForcePowerUsable( gentity_t *self, forcePowers_t forcePower );

int IsTeamplay(void)
{
	if ( !BG_IsTeamGame(level.gametype) )
	{
		return 0;
	}

	return 1;
}

/*
==================
BotAI_GetClientState
==================
*/
int BotAI_GetClientState( int clientNum, playerState_t *state ) {
	gentity_t	*ent;

	ent = &g_entities[clientNum];
	if ( !ent->inuse ) {
		return qfalse;
	}

	if ( !ent->client ) {
		return qfalse;
	}

	memcpy( state, &ent->client->ps, sizeof(playerState_t) );
	return qtrue;
}

/*
==================
BotAI_GetEntityState
==================
*/
int BotAI_GetEntityState( int entityNum, entityState_t *state ) {
	gentity_t	*ent;

	ent = &g_entities[entityNum];
	memset( state, 0, sizeof(entityState_t) );
	if (!ent->inuse) return qfalse;
	if (!ent->r.linked) return qfalse;
	if (ent->r.svFlags & SVF_NOCLIENT) return qfalse;
	memcpy( state, &ent->s, sizeof(entityState_t) );
	return qtrue;
}

/*
==================
BotAI_GetSnapshotEntity
==================
*/
int BotAI_GetSnapshotEntity( int clientNum, int sequence, entityState_t *state ) {
	int		entNum;

	entNum = trap->BotGetSnapshotEntity( clientNum, sequence );
	if ( entNum == -1 ) {
		memset(state, 0, sizeof(entityState_t));
		return -1;
	}

	BotAI_GetEntityState( entNum, state );

	return sequence + 1;
}

/*
==============
BotEntityInfo
==============
*/
void BotEntityInfo(int entnum, aas_entityinfo_t *info) {
	trap->AAS_EntityInfo(entnum, info);
}

/*
==============
NumBots
==============
*/
int NumBots(void) {
	return numbots;
}

/*
==============
AngleDifference
==============
*/
float AngleDifference(float ang1, float ang2) {
	float diff;

	diff = ang1 - ang2;
	if (ang1 > ang2) {
		if (diff > 180.0) diff -= 360.0;
	}
	else {
		if (diff < -180.0) diff += 360.0;
	}
	return diff;
}

/*
==============
BotChangeViewAngle
==============
*/
float BotChangeViewAngle(float angle, float ideal_angle, float speed) {
	float move;

	angle = AngleMod(angle);
	ideal_angle = AngleMod(ideal_angle);
	if (angle == ideal_angle) return angle;
	move = ideal_angle - angle;
	if (ideal_angle > angle) {
		if (move > 180.0) move -= 360.0;
	}
	else {
		if (move < -180.0) move += 360.0;
	}
	if (move > 0) {
		if (move > speed) move = speed;
	}
	else {
		if (move < -speed) move = -speed;
	}
	return AngleMod(angle + move);
}

//Returns the clamped [0,10] server-selected bot_aimspeed level. 0 means disabled (legacy
//per-bot .jkb turnspeed_combat/turnspeed behavior applies unmodified).
static float BotGetAimSpeedLevel(void)
{
	float level = bot_aimspeed.value;

	if (level < 0.0f)
	{
		level = 0.0f;
	}
	else if (level > 10.0f)
	{
		level = 10.0f;
	}

	return level;
}

//Returns a monotonic 0.10..1.0 multiplier for bot_aimspeed levels 1..10. It is applied
//on top of the existing legacy combat-turn factor (which already comes from the bot's
//.jkb turnspeed_combat and skill), so personality is preserved while level 10 is fastest.
static float BotGetAimSpeedFactor(bot_state_t *bs)
{
	float level = BotGetAimSpeedLevel();

	if (level <= 0.0f || !bs)
	{
		return -1.0f;
	}

	if (level >= 10.0f)
	{
		return 1.0f;
	}

	return 0.10f + ((level - 1.0f) / 9.0f) * 0.90f;
}

//Ramps the view-slew cap toward effectively instantaneous as bot_aimspeed approaches 9-10,
//so aim turn speed isn't bottlenecked by max-turn cap at high aimspeed levels.
//Quadratic ramp: every level widens the cap noticeably (x1 at 0, x6 at 5, x101 at 10),
//unlike the old t^4 curve which barely moved below level 8.
static float BotGetAimSpeedMaxChange(bot_state_t *bs, float legacyMaxChange)
{
	float level = BotGetAimSpeedLevel();
	float t;

	if (level <= 0.0f || !bs)
	{
		return legacyMaxChange;
	}

	t = level / 10.0f;
	return legacyMaxChange * (1.0f + (t * t) * 100.0f);
}

/*
==============
BotChangeViewAngles
==============
*/
void BotChangeViewAngles(bot_state_t *bs, float thinktime) {
	float diff, factor, maxchange, anglespeed, disired_speed, aimSpeedFactor;
	int i;

	if (bs->ideal_viewangles[PITCH] > 180) bs->ideal_viewangles[PITCH] -= 360;

	if (bs->currentEnemy && bs->frame_Enemy_Vis)
	{
		if (bs->settings.skill <= 1)
		{
			factor = (bs->skills.turnspeed_combat*0.4f)*bs->settings.skill;
		}
		else if (bs->settings.skill <= 2)
		{
			factor = (bs->skills.turnspeed_combat*0.6f)*bs->settings.skill;
		}
		else if (bs->settings.skill <= 3)
		{
			factor = (bs->skills.turnspeed_combat*0.8f)*bs->settings.skill;
		}
		else
		{
			factor = bs->skills.turnspeed_combat*bs->settings.skill;
		}
	}
	else
	{
		factor = bs->skills.turnspeed;
	}

	if (factor > 1)
		factor = 1;
	if (factor < 0.001)
		factor = 0.001f;

	aimSpeedFactor = BotGetAimSpeedFactor(bs);
	if (aimSpeedFactor > 0.0f)
	{
		factor *= aimSpeedFactor;
	}

	maxchange = bs->skills.maxturn;

	if (g_newBotAI.integer) {
		maxchange = 1800;
	}

	maxchange = BotGetAimSpeedMaxChange(bs, maxchange);
	for (i = 0; i < 2; i++) {
		const qboolean escapeYawOverrideActive = (i == YAW && bs->escapeYawOverrideUntil > level.time) ? qtrue : qfalse;
		float axisFactor = NewBotAI_GetViewAngleAxisFactor(factor, i == YAW, escapeYawOverrideActive);
		const float defaultAxisMaxchange = maxchange * thinktime;
		float axisMaxchange = NewBotAI_GetViewAngleAxisMaxChange(defaultAxisMaxchange, thinktime, i == YAW, escapeYawOverrideActive);

		if (i == YAW && escapeYawOverrideActive)
		{
			axisMaxchange = NewBotAI_GetRecoveryYawSpeedDegPerSec() * thinktime;
		}

		bs->viewangles[i] = AngleMod(bs->viewangles[i]);
		bs->ideal_viewangles[i] = AngleMod(bs->ideal_viewangles[i]);
		diff = AngleDifference(bs->viewangles[i], bs->ideal_viewangles[i]);
		disired_speed = diff * axisFactor;
		bs->viewanglespeed[i] += (bs->viewanglespeed[i] - disired_speed);
		if (bs->viewanglespeed[i] > 180) bs->viewanglespeed[i] = axisMaxchange;
		if (bs->viewanglespeed[i] < -180) bs->viewanglespeed[i] = -axisMaxchange;
		if (escapeYawOverrideActive)
		{
			if (bs->viewanglespeed[i] > axisMaxchange) bs->viewanglespeed[i] = axisMaxchange;
			if (bs->viewanglespeed[i] < -axisMaxchange) bs->viewanglespeed[i] = -axisMaxchange;
		}
		anglespeed = bs->viewanglespeed[i];
		if (anglespeed > axisMaxchange) anglespeed = axisMaxchange;
		if (anglespeed < -axisMaxchange) anglespeed = -axisMaxchange;
		bs->viewangles[i] += anglespeed;
		bs->viewangles[i] = AngleMod(bs->viewangles[i]);
		bs->viewanglespeed[i] *= 0.45 * (1 - axisFactor);
	}
	if (bs->viewangles[PITCH] > 180) bs->viewangles[PITCH] -= 360;
	trap->EA_View(bs->client, bs->viewangles);
}

/*
==============
BotInputToUserCommand
==============
*/
qboolean BotInputToUserCommand(bot_input_t *bi, usercmd_t *ucmd, int delta_angles[3],
	int time, int useTime, qboolean allowRandomUse) {
	vec3_t angles, forward, right;
	short temp;
	int j;
	float f, r, u, m;
	qboolean randomUse = qfalse;

	//clear the whole structure
	memset(ucmd, 0, sizeof(usercmd_t));
	//the duration for the user command in milli seconds
	ucmd->serverTime = time;
	//
	if (bi->actionflags & ACTION_DELAYEDJUMP) {
		bi->actionflags |= ACTION_JUMP;
		bi->actionflags &= ~ACTION_DELAYEDJUMP;
	}
	//set the buttons
	if (bi->actionflags & ACTION_RESPAWN) ucmd->buttons = BUTTON_ATTACK;
	if (bi->actionflags & ACTION_ATTACK) ucmd->buttons |= BUTTON_ATTACK;
	if (bi->actionflags & ACTION_ALT_ATTACK) ucmd->buttons |= BUTTON_ALT_ATTACK;
//	if (bi->actionflags & ACTION_TALK) ucmd->buttons |= BUTTON_TALK;
	if (bi->actionflags & ACTION_GESTURE) ucmd->buttons |= BUTTON_GESTURE;
	if (bi->actionflags & ACTION_USE) ucmd->buttons |= BUTTON_USE_HOLDABLE;
	if (bi->actionflags & ACTION_WALK) ucmd->buttons |= BUTTON_WALKING;

	if (bi->actionflags & ACTION_FORCEPOWER) ucmd->buttons |= BUTTON_FORCEPOWER;

	if (bi->actionflags & ACTION_SKI) ucmd->buttons |= BUTTON_DASH;

	if (allowRandomUse && useTime < level.time && Q_irand(1, 10) < 5)
	{ //for now just hit use randomly in case there's something useable around
		ucmd->buttons |= BUTTON_USE;
		randomUse = qtrue;
	}

#if 0
// Here's an interesting bit.  The bots in TA used buttons to do additional gestures.
// I ripped them out because I didn't want too many buttons given the fact that I was already adding some for JK2.
// We can always add some back in if we want though.
	if (bi->actionflags & ACTION_AFFIRMATIVE) ucmd->buttons |= BUTTON_AFFIRMATIVE;
	if (bi->actionflags & ACTION_NEGATIVE) ucmd->buttons |= BUTTON_NEGATIVE;
	if (bi->actionflags & ACTION_GETFLAG) ucmd->buttons |= BUTTON_GETFLAG;
	if (bi->actionflags & ACTION_GUARDBASE) ucmd->buttons |= BUTTON_GUARDBASE;
	if (bi->actionflags & ACTION_PATROL) ucmd->buttons |= BUTTON_PATROL;
	if (bi->actionflags & ACTION_FOLLOWME) ucmd->buttons |= BUTTON_FOLLOWME;
#endif //0

	if (bi->weapon == WP_NONE)
	{
#ifdef _DEBUG
//		Com_Printf("WARNING: Bot tried to use WP_NONE!\n");
#endif
		bi->weapon = WP_BRYAR_PISTOL;
	}

	//
	ucmd->weapon = bi->weapon;
	//set the view angles
	//NOTE: the ucmd->angles are the angles WITHOUT the delta angles
	ucmd->angles[PITCH] = ANGLE2SHORT(bi->viewangles[PITCH]);
	ucmd->angles[YAW] = ANGLE2SHORT(bi->viewangles[YAW]);
	ucmd->angles[ROLL] = ANGLE2SHORT(bi->viewangles[ROLL]);
	//subtract the delta angles
	for (j = 0; j < 3; j++) {
		temp = ucmd->angles[j] - delta_angles[j];
		ucmd->angles[j] = temp;
	}
	//NOTE: movement is relative to the REAL view angles
	//get the horizontal forward and right vector
	//get the pitch in the range [-180, 180]
	if (bi->dir[2]) angles[PITCH] = bi->viewangles[PITCH];
	else angles[PITCH] = 0;
	angles[YAW] = bi->viewangles[YAW];
	angles[ROLL] = 0;
	AngleVectors(angles, forward, right, NULL);
	//bot input speed is in the range [0, 400]
	bi->speed = bi->speed * 127 / 400;
	//set the view independent movement
	if (!bi->dir[2])
		NewBotAI_ProjectWorldMovement(bi->dir[0], bi->dir[1], bi->viewangles[YAW], &f, &r);
	else
	{
		f = DotProduct(forward, bi->dir);
		r = DotProduct(right, bi->dir);
	}
	u = fabs(forward[2]) * bi->dir[2];
	m = fabs(f);

	if (fabs(r) > m) {
		m = fabs(r);
	}

	if (fabs(u) > m) {
		m = fabs(u);
	}

	if (m > 0) {
		f *= bi->speed / m;
		r *= bi->speed / m;
		u *= bi->speed / m;
	}

	ucmd->forwardmove = f;
	ucmd->rightmove = r;
	ucmd->upmove = u;
	//normal keyboard movement
	if (bi->actionflags & ACTION_MOVEFORWARD) ucmd->forwardmove = 127;
	if (bi->actionflags & ACTION_MOVEBACK) ucmd->forwardmove = -127;
	if (bi->actionflags & ACTION_MOVELEFT) ucmd->rightmove = -127;
	if (bi->actionflags & ACTION_MOVERIGHT) ucmd->rightmove = 127;
	//jump/moveup
	if (bi->actionflags & ACTION_JUMP) ucmd->upmove = 127;
	//crouch/movedown
	if (bi->actionflags & ACTION_CROUCH) ucmd->upmove = -127;
	return randomUse;
}

/*
==============
BotUpdateInput
==============
*/
void BotUpdateInput(bot_state_t *bs, int time, int elapsed_time) {
	bot_input_t bi;
	int j;

	//add the delta angles to the bot's current view angles
	for (j = 0; j < 3; j++) {
		bs->viewangles[j] = AngleMod(bs->viewangles[j] + SHORT2ANGLE(bs->cur_ps.delta_angles[j]));
	}
	if (bs->saberTechniqueAppliedYaw || bs->saberTechniqueAppliedPitch)
	{
		bs->viewangles[YAW] = NewBotAI_SaberApplyYawOffset(bs->viewangles[YAW],
			-bs->saberTechniqueAppliedYaw);
		bs->viewangles[PITCH] -= bs->saberTechniqueAppliedPitch;
		bs->saberTechniqueAppliedYaw = bs->saberTechniqueAppliedPitch = 0.0f;
	}
	NewBotAI_UpdateSaberDefense(bs, time);
	if (g_newBotAI.integer && bs->saberTechniqueOwnsInputs && NewBotAI_CanControlSaber(bs))
	{
		vec3_t toEnemy;
		// Normal inertial aim owns the moving target. Only the sweep offset bypasses
		// that smoothing, and last command's offset must not become its next base.
		if (bs->saberTechniqueFamily != NEWBOTAI_SABER_BASIC || bs->saberTechniqueYawTime ||
			bs->saberDefenseActive)
		{
			VectorSubtract(bs->currentEnemy->r.currentOrigin,
				g_entities[bs->client].client->ps.origin, toEnemy);
			bs->ideal_viewangles[YAW] = vectoyaw(toEnemy);
			if (!bs->saberTechniqueYawTime && !bs->saberDefenseActive)
				bs->ideal_viewangles[YAW] = AngleNormalize360(bs->ideal_viewangles[YAW] +
					NewBotAI_GetApproachAimYawOffset(bs));
		}
	}
	//change the bot view angles
	BotChangeViewAngles(bs, (float) elapsed_time / 1000);
	//retrieve the bot input
	trap->EA_GetInput(bs->client, (float) time / 1000, &bi);
	//Flipkicks need a fresh jump press and release on successive user commands. The
	//bot think loop is much slower than command generation, so maintain that edge
	//here instead of leaving PMF_JUMP_HELD latched between AI updates.
	if (bs->flipkickInputTime > time)
	{
		bs->flipkickJumpHeld = !bs->flipkickJumpHeld;
		if (bs->flipkickJumpHeld)
			bi.actionflags |= ACTION_JUMP;
		else
			bi.actionflags &= ~(ACTION_JUMP | ACTION_DELAYEDJUMP);
	}
	else
	{
		bs->flipkickJumpHeld = qfalse;
	}
	//Defensive rolls are a last resort only - see NewBotAI_FilterDefensiveRollInput.
	NewBotAI_FilterDefensiveRollInput(bs, &bi);
	NewBotAI_ApplySaberTechniqueInput(bs, &bi, time);
	NewBotAI_ApplySaberHandover(bs, &bi, time);
	NewBotAI_ApplySaberAdvance(bs, &bi);
	NewBotAI_ApplySaberThrowInput(bs, &bi);
	if (bs->saberDefenseActive)
		bi.actionflags &= ~(ACTION_ATTACK | ACTION_ALT_ATTACK);
	if (g_newBotAI.integer && !bs->saberTechniqueOwnsInputs &&
		bs->saberTechniqueCandidate && NewBotAI_IsDuelStrafeSuppressed(bs))
	{
		vec3_t right, angles;
		bi.actionflags &= ~(ACTION_MOVELEFT | ACTION_MOVERIGHT);
		VectorSet(angles, 0, bi.viewangles[YAW], 0);
		AngleVectors(angles, NULL, right, NULL);
		VectorMA(bi.dir, -DotProduct(bi.dir, right), right, bi.dir);
	}
	bi.actionflags = NewBotAI_SaberDuelActionFlags(bi.actionflags, ACTION_ALT_ATTACK,
		NewBotAI_IsSaberOnlyDuel(bs));
	BotSFJ_ApplyInput(bs, &bi, time, elapsed_time);
	//respawn hack
	if (bi.actionflags & ACTION_RESPAWN) {
		if (bs->lastucmd.buttons & BUTTON_ATTACK) bi.actionflags &= ~(ACTION_RESPAWN|ACTION_ATTACK);
	}
	//convert the bot input to a usercmd
	bs->sfjLastRandomUse = BotInputToUserCommand(&bi, &bs->lastucmd,
		bs->cur_ps.delta_angles, time,
		bs->noUseTime, bs->sfjOwnsInput ? qfalse : qtrue);
	if (JVM_BotGunAttack(level.gametype,
		JVM_ReplicatedClass(bs->cur_ps.stats[STAT_RESTRICTIONS]),
		bs->lastucmd.weapon, bs->cur_ps.weapon, WP_NUM_WEAPONS, WP_MELEE, WP_SABER) &&
		g_entities[bs->client].client &&
		g_entities[bs->client].health > 0 &&
		(bs->lastucmd.buttons & (BUTTON_ATTACK | BUTTON_ALT_ATTACK))) {
		gclient_t *client = g_entities[bs->client].client;
		int interval = Com_Clampi(0, 2000, merc_botfloodprotect.integer);
		if (time < client->jvmBotAttackTime)
			bs->lastucmd.buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK);
		else
			client->jvmBotAttackTime = time + interval;
	}
	//subtract the delta angles
	for (j = 0; j < 3; j++) {
		bs->viewangles[j] = AngleMod(bs->viewangles[j] - SHORT2ANGLE(bs->cur_ps.delta_angles[j]));
	}
}

/*
==============
BotAIRegularUpdate
==============
*/
void BotAIRegularUpdate(void) {
	if (regularupdate_time < FloatTime()) {
		trap->BotUpdateEntityItems();
		regularupdate_time = FloatTime() + 0.3;
	}
}

/*
==============
RemoveColorEscapeSequences
==============
*/
void RemoveColorEscapeSequences( char *text ) {
	int i, l;

	l = 0;
	for ( i = 0; text[i]; i++ ) {
		if (Q_IsColorStringExt(&text[i])) {
			i++;
			continue;
		}
		if (text[i] > 0x7E)
			continue;
		text[l++] = text[i];
	}
	text[l] = '\0';
}


/*
==============
BotAI
==============
*/
int BotAI(int client, float thinktime) {
	bot_state_t *bs;
	char buf[1024], *args;
	int j;
#ifdef _DEBUG
	int start = 0;
	int end = 0;
#endif

	trap->EA_ResetInput(client);
	//
	bs = botstates[client];
	if (!bs || !bs->inuse) {
		BotAI_Print(PRT_FATAL, "BotAI: client %d is not setup\n", client);
		return qfalse;
	}

	//retrieve the current client state
	BotAI_GetClientState( client, &bs->cur_ps );

	//retrieve any waiting server commands
	while( trap->BotGetServerCommand(client, buf, sizeof(buf)) ) {
		//have buf point to the command and args to the command arguments
		args = strchr( buf, ' ');
		if (!args) continue;
		*args++ = '\0';

		//remove color espace sequences from the arguments
		RemoveColorEscapeSequences( args );

		if (!Q_stricmp(buf, "cp "))
			{ /*CenterPrintf*/ }
		else if (!Q_stricmp(buf, "cs"))
			{ /*ConfigStringModified*/ }
		else if (!Q_stricmp(buf, "scores"))
			{ /*FIXME: parse scores?*/ }
		else if (!Q_stricmp(buf, "clientLevelShot"))
			{ /*ignore*/ }
	}
	//add the delta angles to the bot's current view angles
	for (j = 0; j < 3; j++) {
		bs->viewangles[j] = AngleMod(bs->viewangles[j] + SHORT2ANGLE(bs->cur_ps.delta_angles[j]));
	}
	//increase the local time of the bot
	bs->ltime += thinktime;
	//
	bs->thinktime = thinktime;
	//origin of the bot
	VectorCopy(bs->cur_ps.origin, bs->origin);
	//eye coordinates of the bot
	VectorCopy(bs->cur_ps.origin, bs->eye);
	bs->eye[2] += bs->cur_ps.viewheight;
	//get the area the bot is in

#ifdef _DEBUG
	start = trap->Milliseconds();
#endif
	bs->saberTechniqueCandidate = qfalse;
	bs->saberTechniqueOwnsInputs = qfalse;
	bs->saberTechniqueClearQueuedAttack = qfalse;
	if (g_newBotAI.integer)
		NewBotAI(bs, thinktime);
	else
		StandardBotAI(bs, thinktime);
	BotSFJ_SelectIntent(bs);
#ifdef _DEBUG
	end = trap->Milliseconds();

	trap->Cvar_Update(&bot_debugmessages);

	if (bot_debugmessages.integer)
	{
		Com_Printf("Single AI frametime: %i\n", (end - start));
	}
#endif

	//subtract the delta angles
	for (j = 0; j < 3; j++) {
		bs->viewangles[j] = AngleMod(bs->viewangles[j] - SHORT2ANGLE(bs->cur_ps.delta_angles[j]));
	}
	//everything was ok
	return qtrue;
}

/*
==================
BotScheduleBotThink
==================
*/
void BotScheduleBotThink(void) {
	int i, botnum;

	botnum = 0;

	for( i = 0; i < MAX_CLIENTS; i++ ) {
		if( !botstates[i] || !botstates[i]->inuse ) {
			continue;
		}
		//initialize the bot think residual time
		botstates[i]->botthink_residual = BOT_THINK_TIME * botnum / numbots;
		botnum++;
	}
}

int PlayersInGame(void)
{
	int i = 0;
	gentity_t *ent;
	int pl = 0;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && ent->client->pers.connected == CON_CONNECTED)
		{
			pl++;
		}

		i++;
	}

	return pl;
}

/*
==============
BotAISetupClient
==============
*/
int BotAISetupClient(int client, struct bot_settings_s *settings, qboolean restart) {
	bot_state_t *bs;

	if (!botstates[client]) botstates[client] = (bot_state_t *) B_Alloc(sizeof(bot_state_t)); //G_Alloc(sizeof(bot_state_t));
																			  //rww - G_Alloc bad! B_Alloc good.

	memset(botstates[client], 0, sizeof(bot_state_t));

	bs = botstates[client];

	if (bs && bs->inuse) {
		BotAI_Print(PRT_FATAL, "BotAISetupClient: client %d already setup\n", client);
		return qfalse;
	}

	memcpy(&bs->settings, settings, sizeof(bot_settings_t));

	bs->client = client; //need to know the client number before doing personality stuff
	bs->randomStrafeDir = 0;
	bs->randomStrafeMode = 0;
	bs->randomStrafeEndTime = 0;
	bs->gripkickRestackDir = 0;
	bs->escapeYawOverrideUntil = 0;
	bs->lastWPIndex = -1;
	bs->enemyWaypointFallbackIndex = -1;
	bs->enemyWaypointFallbackEnemyNum = -1;

	//initialize weapon weight defaults..
	bs->botWeaponWeights[WP_NONE] = 0;
	bs->botWeaponWeights[WP_STUN_BATON] = 1;
	bs->botWeaponWeights[WP_SABER] = 10;
	bs->botWeaponWeights[WP_BRYAR_PISTOL] = 11;
	bs->botWeaponWeights[WP_BLASTER] = 12;
	bs->botWeaponWeights[WP_DISRUPTOR] = 13;
	bs->botWeaponWeights[WP_BOWCASTER] = 14;
	bs->botWeaponWeights[WP_REPEATER] = 15;
	bs->botWeaponWeights[WP_DEMP2] = 16;
	bs->botWeaponWeights[WP_FLECHETTE] = 17;
	bs->botWeaponWeights[WP_ROCKET_LAUNCHER] = 18;
	bs->botWeaponWeights[WP_THERMAL] = 14;
	bs->botWeaponWeights[WP_TRIP_MINE] = 0;
	bs->botWeaponWeights[WP_DET_PACK] = 0;
	bs->botWeaponWeights[WP_MELEE] = 1;

	BotUtilizePersonality(bs);

	if (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
	{
		bs->botWeaponWeights[WP_SABER] = 13;
	}

	//allocate a goal state
	bs->gs = trap->BotAllocGoalState(client);

	//allocate a weapon state
	bs->ws = trap->BotAllocWeaponState();

	bs->inuse = qtrue;
	bs->entitynum = client;
	bs->setupcount = 4;
	bs->entergame_time = FloatTime();
	bs->ms = trap->BotAllocMoveState();
	numbots++;

	//NOTE: reschedule the bot thinking
	BotScheduleBotThink();

	if (PlayersInGame())
	{ //don't talk to yourself
		BotDoChat(bs, "GeneralGreetings", 0);
	}

	return qtrue;
}

/*
==============
BotAIShutdownClient
==============
*/
int BotAIShutdownClient(int client, qboolean restart) {
	bot_state_t *bs;

	bs = botstates[client];
	if (!bs || !bs->inuse) {
		//BotAI_Print(PRT_ERROR, "BotAIShutdownClient: client %d already shutdown\n", client);
		return qfalse;
	}

	trap->BotFreeMoveState(bs->ms);
	//free the goal state`
	trap->BotFreeGoalState(bs->gs);
	//free the weapon weights
	trap->BotFreeWeaponState(bs->ws);
	//
	//clear the bot state
	memset(bs, 0, sizeof(bot_state_t));
	//set the inuse flag to qfalse
	bs->inuse = qfalse;
	//there's one bot less
	numbots--;
	//everything went ok
	return qtrue;
}

/*
==============
BotResetState

called when a bot enters the intermission or observer mode and
when the level is changed
==============
*/
void BotResetState(bot_state_t *bs) {
	int client, entitynum, inuse;
	int movestate, goalstate, weaponstate;
	bot_settings_t settings;
	playerState_t ps;							//current player state
	float entergame_time;

	//save some things that should not be reset here
	memcpy(&settings, &bs->settings, sizeof(bot_settings_t));
	memcpy(&ps, &bs->cur_ps, sizeof(playerState_t));
	inuse = bs->inuse;
	client = bs->client;
	entitynum = bs->entitynum;
	movestate = bs->ms;
	goalstate = bs->gs;
	weaponstate = bs->ws;
	entergame_time = bs->entergame_time;
	//reset the whole state
	memset(bs, 0, sizeof(bot_state_t));
	//copy back some state stuff that should not be reset
	bs->ms = movestate;
	bs->gs = goalstate;
	bs->ws = weaponstate;
	memcpy(&bs->cur_ps, &ps, sizeof(playerState_t));
	memcpy(&bs->settings, &settings, sizeof(bot_settings_t));
	bs->inuse = inuse;
	bs->client = client;
	bs->entitynum = entitynum;
	bs->entergame_time = entergame_time;
	bs->randomStrafeDir = 0;
	bs->randomStrafeMode = 0;
	bs->randomStrafeEndTime = 0;
	bs->gripkickRestackDir = 0;
	bs->escapeYawOverrideUntil = 0;
	bs->lastWPIndex = -1; //no waypoint memory yet (0 is a valid index, so memset isn't enough)
	bs->enemyWaypointFallbackIndex = -1;
	bs->enemyWaypointFallbackEnemyNum = -1;
	//reset several states
	if (bs->ms) trap->BotResetMoveState(bs->ms);
	if (bs->gs) trap->BotResetGoalState(bs->gs);
	if (bs->ws) trap->BotResetWeaponState(bs->ws);
	if (bs->gs) trap->BotResetAvoidGoals(bs->gs);
	if (bs->ms) trap->BotResetAvoidReach(bs->ms);
}

/*
==============
BotAILoadMap
==============
*/
int BotAILoadMap( int restart ) {
	int			i;

	for (i = 0; i < MAX_CLIENTS; i++) {
		if (botstates[i] && botstates[i]->inuse) {
			BotResetState( botstates[i] );
			botstates[i]->setupcount = 4;
		}
	}

	return qtrue;
}

//rww - bot ai

//standard visibility check
int OrgVisible(vec3_t org1, vec3_t org2, int ignore)
{
	trace_t tr;

	JP_Trace(&tr, org1, NULL, NULL, org2, ignore, MASK_SOLID, qfalse, 0, 0 );

	if (tr.fraction == 1)
	{
		return 1;
	}

	return 0;
}

//special waypoint visibility check
int WPOrgVisible(gentity_t *bot, vec3_t org1, vec3_t org2, int ignore)
{
	trace_t tr;
	gentity_t *ownent;

	JP_Trace(&tr, org1, NULL, NULL, org2, ignore, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		JP_Trace(&tr, org1, NULL, NULL, org2, ignore, MASK_PLAYERSOLID, qfalse, 0, 0);

		if (tr.fraction != 1 && tr.entityNum != ENTITYNUM_NONE && g_entities[tr.entityNum].s.eType == ET_SPECIAL)
		{
			if (g_entities[tr.entityNum].parent && g_entities[tr.entityNum].parent->client)
			{
				ownent = g_entities[tr.entityNum].parent;

				if (OnSameTeam(bot, ownent) || bot->s.number == ownent->s.number)
				{
					return 1;
				}
			}
			return 2;
		}

		return 1;
	}

	return 0;
}

//visibility check with hull trace
int OrgVisibleBox(vec3_t org1, vec3_t mins, vec3_t maxs, vec3_t org2, int ignore)
{
	trace_t tr;

	if (RMG.integer)
	{
		JP_Trace(&tr, org1, NULL, NULL, org2, ignore, MASK_SOLID, qfalse, 0, 0);
	}
	else
	{
		JP_Trace(&tr, org1, mins, maxs, org2, ignore, MASK_SOLID, qfalse, 0, 0);
	}

	if (tr.fraction == 1 && !tr.startsolid && !tr.allsolid)
	{
		return 1;
	}

	return 0;
}

//see if there's a func_* ent under the given pos.
//kind of badly done, but this shouldn't happen
//often.
int CheckForFunc(vec3_t org, int ignore)
{
	gentity_t *fent;
	vec3_t under;
	trace_t tr;

	VectorCopy(org, under);

	under[2] -= 64;

	JP_Trace(&tr, org, NULL, NULL, under, ignore, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		return 0;
	}

	fent = &g_entities[tr.entityNum];

	if (!fent)
	{
		return 0;
	}

	if (strstr(fent->classname, "func_"))
	{
		return 1; //there's a func brush here
	}

	return 0;
}

//perform pvs check based on rmg or not
qboolean BotPVSCheck( const vec3_t p1, const vec3_t p2 )
{
	if (RMG.integer && bot_pvstype.integer)
	{
		vec3_t subPoint;
		VectorSubtract(p1, p2, subPoint);

		if (VectorLength(subPoint) > 5000)
		{
			return qfalse;
		}
		return qtrue;
	}

	return trap->InPVS(p1, p2);
}

//get the index to the nearest visible waypoint in the global trail
int GetNearestVisibleWP(vec3_t org, int ignore)
{
	int i;
	float bestdist;
	float flLen;
	int bestindex;
	vec3_t a, mins, maxs;

	i = 0;
	if (RMG.integer)
	{
		bestdist = 300;
	}
	else
	{
		bestdist = 800;//99999;
				   //don't trace over 800 units away to avoid GIANT HORRIBLE SPEED HITS ^_^
	}
	bestindex = -1;

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = -1;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 1;

	while (i < gWPNum)
	{
		if (gWPArray[i] && gWPArray[i]->inuse)
		{
			VectorSubtract(org, gWPArray[i]->origin, a);
			flLen = VectorLength(a);

			if (flLen < bestdist && (RMG.integer || BotPVSCheck(org, gWPArray[i]->origin)) && OrgVisibleBox(org, mins, maxs, gWPArray[i]->origin, ignore))
			{
				bestdist = flLen;
				bestindex = i;
			}
		}

		i++;
	}

	return bestindex;
}

static void BotNav_RememberWP(bot_state_t *bs, int index)
{
	if (!bs)
		return;
	bs->wpRecentHead = BotNav_RecentPush(bs->wpRecent, BOT_NAV_RECENT_WAYPOINTS,
		bs->wpRecentHead, index);
}

/*
 * Nearest visible waypoint that the bot has not just passed.  Re-picking a
 * point it already reached is what makes bots walk back and forth; fall back
 * to any visible point when nothing fresh is in reach.
 */
static int BotNav_NearestVisibleFreshWP(bot_state_t *bs)
{
	vec3_t a, mins, maxs;
	float bestdist = RMG.integer ? 300.0f : 800.0f;
	int bestindex = -1;
	int i;

	VectorSet(mins, -15, -15, -1);
	VectorSet(maxs, 15, 15, 1);
	for (i = 0; i < gWPNum; i++)
	{
		float flLen;

		if (!gWPArray[i] || !gWPArray[i]->inuse ||
			BotNav_RecentContains(bs->wpRecent, BOT_NAV_RECENT_WAYPOINTS, i))
			continue;
		VectorSubtract(bs->origin, gWPArray[i]->origin, a);
		flLen = VectorLength(a);
		if (flLen < bestdist && (RMG.integer || BotPVSCheck(bs->origin, gWPArray[i]->origin)) &&
			OrgVisibleBox(bs->origin, mins, maxs, gWPArray[i]->origin, bs->client))
		{
			bestdist = flLen;
			bestindex = i;
		}
	}
	if (bestindex == -1)
		bestindex = GetNearestVisibleWP(bs->origin, bs->client);
	return bestindex;
}

/*
 * Falling hazard ahead: switch to the next waypoint in the travel direction
 * when it is visible and safe to head for, otherwise drop the waypoint so the
 * bot re-paths (avoiding the points it just passed).
 */
static void BotNav_AvoidHazardWaypoint(bot_state_t *bs)
{
	int next;

	if (!bs->wpCurrent)
		return;
	BotNav_RememberWP(bs, bs->wpCurrent->index);
	next = bs->wpDirection ? bs->wpCurrent->index - 1 : bs->wpCurrent->index + 1;
	if (next >= 0 && next < gWPNum && gWPArray[next] && gWPArray[next]->inuse &&
		OrgVisible(bs->origin, gWPArray[next]->origin, bs->client))
	{
		vec3_t dir;

		VectorSubtract(gWPArray[next]->origin, bs->origin, dir);
		dir[2] = 0.0f;
		if (VectorNormalize(dir) > 0.0f && !BotNav_CheckFallingHazard(bs, dir,
			(bs->currentEnemy && bs->frame_Enemy_Vis) ? qtrue : qfalse))
		{
			bs->wpCurrent = gWPArray[next];
			bs->wpSeenTime = level.time + 1500;
			bs->wpTravelTime = level.time + 10000;
			return;
		}
	}
	bs->wpCurrent = NULL; //force re-path
}

/*
 * Out of combat, a bot heading for a waypoint destination that has not got
 * closer for a few seconds is stuck in a local loop: mark its current point
 * as visited and re-path to a fresh one instead of reversing direction.
 */
static void BotNav_CheckProgress(bot_state_t *bs)
{
	float distance;
	int goal;

	if (!bs->wpCurrent || !bs->wpDestination || bs->wpCamping ||
		(bs->currentEnemy && bs->frame_Enemy_Vis) || bs->sfjRoute)
	{
		bs->navProgressGoal = 0;
		bs->navProgressBest = 0.0f;
		bs->navProgressTime = 0;
		return;
	}
	goal = bs->wpDestination->index + 1;
	distance = Distance(bs->origin, bs->wpDestination->origin);
	if (goal != bs->navProgressGoal || BotNav_ProgressImproved(bs->navProgressBest, distance))
	{
		bs->navProgressGoal = goal;
		bs->navProgressBest = distance;
		bs->navProgressTime = level.time;
		return;
	}
	if (!BotNav_ProgressStalled(bs->navProgressTime, level.time))
		return;
	{
		const int savedDirection = bs->wpDirection;
		int wp;

		BotNav_RememberWP(bs, bs->wpCurrent->index);
		wp = BotNav_NearestVisibleFreshWP(bs);
		bs->navProgressBest = distance;
		bs->navProgressTime = level.time;
		if (wp == -1 || wp == bs->wpCurrent->index)
			return;
		BotSFJ_DebugReject(bs, "waypoint progress stalled, re-pathing to an unvisited point");
		bs->wpCurrent = gWPArray[wp];
		bs->wpDirection = savedDirection;
		bs->wpSeenTime = level.time + 1500;
		bs->wpTravelTime = level.time + 10000;
	}
}

//wpDirection
//0 == FORWARD
//1 == BACKWARD

//see if this is a valid waypoint to pick up in our
//current state (whatever that may be)
int PassWayCheck(bot_state_t *bs, int windex)
{
	if (!gWPArray[windex] || !gWPArray[windex]->inuse)
	{ //bad point index
		return 0;
	}

	if (RMG.integer)
	{
		if ((gWPArray[windex]->flags & WPFLAG_RED_FLAG) ||
			(gWPArray[windex]->flags & WPFLAG_BLUE_FLAG))
		{ //red or blue flag, we'd like to get here
			return 1;
		}
	}

	if (bs->wpDirection && (gWPArray[windex]->flags & WPFLAG_ONEWAY_FWD))
	{ //we're not travelling in a direction on the trail that will allow us to pass this point
		return 0;
	}
	else if (!bs->wpDirection && (gWPArray[windex]->flags & WPFLAG_ONEWAY_BACK))
	{ //we're not travelling in a direction on the trail that will allow us to pass this point
		return 0;
	}

	if (bs->wpCurrent && gWPArray[windex]->forceJumpTo &&
		gWPArray[windex]->origin[2] > (bs->wpCurrent->origin[2]+64) &&
		bs->cur_ps.fd.forcePowerLevel[FP_LEVITATION] < gWPArray[windex]->forceJumpTo)
	{ //waypoint requires force jump level greater than our current one to pass
		return 0;
	}

	return 1;
}

//tally up the distance between two waypoints
float TotalTrailDistance(int start, int end, bot_state_t *bs)
{
	int beginat;
	int endat;
	float distancetotal;

	distancetotal = 0;

	if (start > end)
	{
		beginat = end;
		endat = start;
	}
	else
	{
		beginat = start;
		endat = end;
	}

	while (beginat < endat)
	{
		if (beginat >= gWPNum || !gWPArray[beginat] || !gWPArray[beginat]->inuse)
		{ //invalid waypoint index
			return -1;
		}

		if (!RMG.integer)
		{
			if ((end > start && gWPArray[beginat]->flags & WPFLAG_ONEWAY_BACK) ||
				(start > end && gWPArray[beginat]->flags & WPFLAG_ONEWAY_FWD))
			{ //a one-way point, this means this path cannot be travelled to the final point
				return -1;
			}
		}

#if 0 //disabled force jump checks for now
		if (gWPArray[beginat]->forceJumpTo)
		{
			if (gWPArray[beginat-1] && gWPArray[beginat-1]->origin[2]+64 < gWPArray[beginat]->origin[2])
			{
				gdif = gWPArray[beginat]->origin[2] - gWPArray[beginat-1]->origin[2];
			}

			if (gdif)
			{
				if (bs && bs->cur_ps.fd.forcePowerLevel[FP_LEVITATION] < gWPArray[beginat]->forceJumpTo)
				{
					return -1;
				}
			}
		}

		if (bs->wpCurrent && gWPArray[windex]->forceJumpTo &&
			gWPArray[windex]->origin[2] > (bs->wpCurrent->origin[2]+64) &&
			bs->cur_ps.fd.forcePowerLevel[FP_LEVITATION] < gWPArray[windex]->forceJumpTo)
		{
			return -1;
		}
#endif

		distancetotal += gWPArray[beginat]->disttonext;

		beginat++;
	}

	return distancetotal;
}

static qboolean NewBotAI_SelectWaypointDirectionTowardTarget(bot_state_t *bs, int fromIndex, int targetIndex)
{
	const int forwardIndex = fromIndex + 1;
	const int backwardIndex = fromIndex - 1;
	float bestForwardTrail = -1.0f;
	float bestBackwardTrail = -1.0f;
	int i;
	qboolean forwardLinked = qfalse;
	qboolean backwardLinked = qfalse;

	if (!bs || fromIndex < 0 || fromIndex >= gWPNum || targetIndex < 0 || targetIndex >= gWPNum ||
		!gWPArray[fromIndex] || !gWPArray[fromIndex]->inuse ||
		!gWPArray[targetIndex] || !gWPArray[targetIndex]->inuse)
	{
		return qfalse;
	}

	for (i = 0; i < gWPArray[fromIndex]->neighbornum; i++)
	{
		const int neighborIndex = gWPArray[fromIndex]->neighbors[i].num;
		if (neighborIndex == forwardIndex)
		{
			forwardLinked = qtrue;
		}
		else if (neighborIndex == backwardIndex)
		{
			backwardLinked = qtrue;
		}
	}

	if (forwardLinked && forwardIndex >= 0 && forwardIndex < gWPNum &&
		gWPArray[forwardIndex] && gWPArray[forwardIndex]->inuse)
	{
		bestForwardTrail = TotalTrailDistance(forwardIndex, targetIndex, bs);
	}
	if (backwardLinked && backwardIndex >= 0 && backwardIndex < gWPNum &&
		gWPArray[backwardIndex] && gWPArray[backwardIndex]->inuse)
	{
		bestBackwardTrail = TotalTrailDistance(backwardIndex, targetIndex, bs);
	}

	if (bestForwardTrail >= 0.0f && (bestBackwardTrail < 0.0f || bestForwardTrail <= bestBackwardTrail))
	{
		bs->wpDirection = 0;
		return qtrue;
	}
	if (bestBackwardTrail >= 0.0f)
	{
		bs->wpDirection = 1;
		return qtrue;
	}

	return qfalse;
}

//see if there's a route shorter than our current one to get
//to the final destination we currently desire
void CheckForShorterRoutes(bot_state_t *bs, int newwpindex)
{
	float bestlen;
	float checklen;
	int bestindex;
	int i;
	int fj;

	i = 0;
	fj = 0;

	if (!bs->wpDestination)
	{
		return;
	}

	//set our traversal direction based on the index of the point
	if (newwpindex < bs->wpDestination->index)
	{
		bs->wpDirection = 0;
	}
	else if (newwpindex > bs->wpDestination->index)
	{
		bs->wpDirection = 1;
	}

	//can't switch again yet
	if (bs->wpSwitchTime > level.time)
	{
		return;
	}

	//no neighboring points to check off of
	if (!gWPArray[newwpindex]->neighbornum)
	{
		return;
	}

	//get the trail distance for our wp
	bestindex = newwpindex;
	bestlen = TotalTrailDistance(newwpindex, bs->wpDestination->index, bs);

	while (i < gWPArray[newwpindex]->neighbornum)
	{ //now go through the neighbors and check the distance to the desired point from each neighbor
		checklen = TotalTrailDistance(gWPArray[newwpindex]->neighbors[i].num, bs->wpDestination->index, bs);

		if (checklen < bestlen-64 || bestlen == -1)
		{ //this path covers less distance, let's take it instead
			if (bs->cur_ps.fd.forcePowerLevel[FP_LEVITATION] >= gWPArray[newwpindex]->neighbors[i].forceJumpTo)
			{
				bestlen = checklen;
				bestindex = gWPArray[newwpindex]->neighbors[i].num;

				if (gWPArray[newwpindex]->neighbors[i].forceJumpTo)
				{
					fj = gWPArray[newwpindex]->neighbors[i].forceJumpTo;
				}
				else
				{
					fj = 0;
				}
			}
		}

		i++;
	}

	if (bestindex != newwpindex && bestindex != -1)
	{ //we found a path we want to switch to, let's do it
		bs->wpCurrent = gWPArray[bestindex];
		bs->wpSwitchTime = level.time + 3000;

		if (fj)
		{ //do we have to force jump to get to this neighbor?
#ifndef FORCEJUMP_INSTANTMETHOD
			bs->forceJumpChargeTime = level.time + 1000;
			bs->beStill = level.time + 1000;
			bs->forceJumping = bs->forceJumpChargeTime;
#else
			bs->beStill = level.time + 500;
			bs->jumpTime = level.time + fj*1200;
			bs->jDelay = level.time + 200;
			bs->forceJumping = bs->jumpTime;
#endif
		}
	}
}

//check for flags on the waypoint we're currently travelling to
//and perform the desired behavior based on the flag
void WPConstantRoutine(bot_state_t *bs)
{
	if (!bs->wpCurrent)
	{
		return;
	}

	if (bs->wpCurrent->flags & WPFLAG_DUCK)
	{ //duck while travelling to this point
		bs->duckTime = level.time + 100;
	}

#ifndef FORCEJUMP_INSTANTMETHOD
	if (bs->wpCurrent->flags & WPFLAG_JUMP)
	{ //jump while travelling to this point
		float heightDif = (bs->wpCurrent->origin[2] - bs->origin[2]+16);

		if (bs->origin[2]+16 >= bs->wpCurrent->origin[2])
		{ //don't need to jump, we're already higher than this point
			heightDif = 0;
		}

		if (heightDif > 40 && (bs->cur_ps.fd.forcePowersKnown & (1 << FP_LEVITATION)) && (bs->cur_ps.fd.forceJumpCharge < (forceJumpStrength[bs->cur_ps.fd.forcePowerLevel[FP_LEVITATION]]-100) || bs->cur_ps.groundEntityNum == ENTITYNUM_NONE))
		{ //alright, let's jump
			bs->forceJumpChargeTime = level.time + 1000;
			if (bs->cur_ps.groundEntityNum != ENTITYNUM_NONE && bs->jumpPrep < (level.time-300))
			{
				bs->jumpPrep = level.time + 700;
			}
			bs->beStill = level.time + 300;
			bs->jumpTime = 0;

			if (bs->wpSeenTime < (level.time + 600))
			{
				bs->wpSeenTime = level.time + 600;
			}
		}
		else if (heightDif > 64 && !(bs->cur_ps.fd.forcePowersKnown & (1 << FP_LEVITATION)))
		{ //this point needs force jump to reach and we don't have it
			//Kill the current point and turn around
			bs->wpCurrent = NULL;
			if (bs->wpDirection)
			{
				bs->wpDirection = 0;
			}
			else
			{
				bs->wpDirection = 1;
			}

			return;
		}
	}
#endif

	if (bs->wpCurrent->forceJumpTo)
	{
#ifdef FORCEJUMP_INSTANTMETHOD
		if (bs->origin[2]+16 < bs->wpCurrent->origin[2])
		{
			bs->jumpTime = level.time + 100;
		}
#else

		if (bs->cur_ps.fd.forceJumpCharge < (forceJumpStrength[bs->cur_ps.fd.forcePowerLevel[FP_LEVITATION]]-100))
		{
			bs->forceJumpChargeTime = level.time + 200;
		}
#endif
	}
}

//check if our ctf state is to guard the base
qboolean BotCTFGuardDuty(bot_state_t *bs)
{
	if (level.gametype != GT_CTF && level.gametype != GT_CTY)
	{
		return qfalse;
	}

	if (bs->ctfState == CTFSTATE_DEFENDER)
	{
		return qtrue;
	}

	return qfalse;
}

//when we reach the waypoint we are travelling to,
//this function will be called. We will perform any
//checks for flags on the current wp and activate
//any "touch" events based on that.
void WPTouchRoutine(bot_state_t *bs)
{
	int lastNum;

	if (!bs->wpCurrent)
	{
		return;
	}

	BotNav_RememberWP(bs, bs->wpCurrent->index);

	bs->wpTravelTime = level.time + 10000;

	if (bs->wpCurrent->flags & WPFLAG_NOMOVEFUNC)
	{ //don't try to use any nearby map objects for a little while
		bs->noUseTime = level.time + 4000;
	}

#ifdef FORCEJUMP_INSTANTMETHOD
	if ((bs->wpCurrent->flags & WPFLAG_JUMP) && bs->wpCurrent->forceJumpTo)
	{ //jump if we're flagged to but not if this indicates a force jump point. Force jumping is
	  //handled elsewhere.
		bs->jumpTime = level.time + 100;
	}
#else
	if ((bs->wpCurrent->flags & WPFLAG_JUMP) && !bs->wpCurrent->forceJumpTo)
	{ //jump if we're flagged to but not if this indicates a force jump point. Force jumping is
	  //handled elsewhere.
		bs->jumpTime = level.time + 100;
	}
#endif

	if (bs->isCamper && bot_camp.integer && (BotIsAChickenWuss(bs) || BotCTFGuardDuty(bs) || bs->isCamper == 2) && ((bs->wpCurrent->flags & WPFLAG_SNIPEORCAMP) || (bs->wpCurrent->flags & WPFLAG_SNIPEORCAMPSTAND)) &&
		bs->cur_ps.weapon != WP_SABER && bs->cur_ps.weapon != WP_MELEE && bs->cur_ps.weapon != WP_STUN_BATON)
	{ //if we're a camper and a chicken then camp
		if (bs->wpDirection)
		{
			lastNum = bs->wpCurrent->index+1;
		}
		else
		{
			lastNum = bs->wpCurrent->index-1;
		}

		if (gWPArray[lastNum] && gWPArray[lastNum]->inuse && gWPArray[lastNum]->index && bs->isCamping < level.time)
		{
			bs->isCamping = level.time + rand()%15000 + 30000;
			bs->wpCamping = bs->wpCurrent;
			bs->wpCampingTo = gWPArray[lastNum];

			if (bs->wpCurrent->flags & WPFLAG_SNIPEORCAMPSTAND)
			{
				bs->campStanding = qtrue;
			}
			else
			{
				bs->campStanding = qfalse;
			}
		}

	}
	else if ((bs->cur_ps.weapon == WP_SABER || bs->cur_ps.weapon == WP_STUN_BATON || bs->cur_ps.weapon == WP_MELEE) &&
		bs->isCamping > level.time)
	{ //don't snipe/camp with a melee weapon, that would be silly
		bs->isCamping = 0;
		bs->wpCampingTo = NULL;
		bs->wpCamping = NULL;
	}

	if (bs->wpDestination)
	{
		if (bs->wpCurrent->index == bs->wpDestination->index)
		{
			bs->wpDestination = NULL;

			if (bs->runningLikeASissy)
			{ //this obviously means we're scared and running, so we'll want to keep our navigational priorities less delayed
				bs->destinationGrabTime = level.time + 500;
			}
			else
			{
				bs->destinationGrabTime = level.time + 3500;
			}
		}
		else
		{
			CheckForShorterRoutes(bs, bs->wpCurrent->index);
		}
	}
}

//could also slowly lerp toward, but for now
//just copying straight over.
void MoveTowardIdealAngles(bot_state_t *bs)
{
	VectorCopy(bs->goalAngles, bs->ideal_viewangles);
}

#define BOT_STRAFE_AVOIDANCE

#ifdef BOT_STRAFE_AVOIDANCE
#define STRAFEAROUND_RIGHT			1
#define STRAFEAROUND_LEFT			2

//do some trace checks for strafing to get an idea of where we
//are and if we should move to avoid obstacles.
int BotTrace_Strafe(bot_state_t *bs, vec3_t traceto)
{
	vec3_t playerMins = {-15, -15, /*DEFAULT_MINS_2*/-8};
	vec3_t playerMaxs = {15, 15, DEFAULT_MAXS_2};
	vec3_t from, to;
	vec3_t dirAng, dirDif;
	vec3_t forward, right;
	trace_t tr;

	if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE)
	{ //don't do this in the air, it can be.. dangerous.
		return 0;
	}

	VectorSubtract(traceto, bs->origin, dirAng);
	VectorNormalize(dirAng);
	vectoangles(dirAng, dirAng);

	if (AngleDifference(bs->viewangles[YAW], dirAng[YAW]) > 60 ||
		AngleDifference(bs->viewangles[YAW], dirAng[YAW]) < -60)
	{ //If we aren't facing the direction we're going here, then we've got enough excuse to be too stupid to strafe around anyway
		return 0;
	}

	VectorCopy(bs->origin, from);
	VectorCopy(traceto, to);

	VectorSubtract(to, from, dirDif);
	VectorNormalize(dirDif);
	vectoangles(dirDif, dirDif);

	AngleVectors(dirDif, forward, 0, 0);

	to[0] = from[0] + forward[0]*32;
	to[1] = from[1] + forward[1]*32;
	to[2] = from[2] + forward[2]*32;

	JP_Trace(&tr, from, playerMins, playerMaxs, to, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		return 0;
	}

	AngleVectors(dirAng, 0, right, 0);

	from[0] += right[0]*32;
	from[1] += right[1]*32;
	from[2] += right[2]*16;

	to[0] += right[0]*32;
	to[1] += right[1]*32;
	to[2] += right[2]*32;

	JP_Trace(&tr, from, playerMins, playerMaxs, to, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		return STRAFEAROUND_RIGHT;
	}

	from[0] -= right[0]*64;
	from[1] -= right[1]*64;
	from[2] -= right[2]*64;

	to[0] -= right[0]*64;
	to[1] -= right[1]*64;
	to[2] -= right[2]*64;

	JP_Trace(&tr, from, playerMins, playerMaxs, to, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		return STRAFEAROUND_LEFT;
	}

	return 0;
}
#endif

//Similar to the trace check, but we want to trace to see
//if there's anything we can jump over.
int BotTrace_Jump(bot_state_t *bs, vec3_t traceto)
{
	vec3_t mins, maxs, a, fwd, traceto_mod, tracefrom_mod;
	trace_t tr;
	int orTr;

	VectorSubtract(traceto, bs->origin, a);
	vectoangles(a, a);

	AngleVectors(a, fwd, NULL, NULL);

	traceto_mod[0] = bs->origin[0] + fwd[0]*4;
	traceto_mod[1] = bs->origin[1] + fwd[1]*4;
	traceto_mod[2] = bs->origin[2] + fwd[2]*4;

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = -18;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 32;

	JP_Trace(&tr, bs->origin, mins, maxs, traceto_mod, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		return 0;
	}

	orTr = tr.entityNum;

	VectorCopy(bs->origin, tracefrom_mod);

	tracefrom_mod[2] += 41;
	traceto_mod[2] += 41;

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = 0;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 8;

	JP_Trace(&tr, tracefrom_mod, mins, maxs, traceto_mod, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		if (orTr >= 0 && orTr < MAX_CLIENTS && botstates[orTr] && botstates[orTr]->jumpTime > level.time)
		{
			return 0; //so bots don't try to jump over each other at the same time
		}

		if (bs->currentEnemy && bs->currentEnemy->s.number == orTr && (BotGetWeaponRange(bs) == BWEAPONRANGE_SABER || BotGetWeaponRange(bs) == BWEAPONRANGE_MELEE))
		{
			return 0;
		}

		return 1;
	}

	return 0;
}

//And yet another check to duck under any obstacles.
int BotTrace_Duck(bot_state_t *bs, vec3_t traceto)
{
	vec3_t mins, maxs, a, fwd, traceto_mod, tracefrom_mod;
	trace_t tr;

	VectorSubtract(traceto, bs->origin, a);
	vectoangles(a, a);

	AngleVectors(a, fwd, NULL, NULL);

	traceto_mod[0] = bs->origin[0] + fwd[0]*4;
	traceto_mod[1] = bs->origin[1] + fwd[1]*4;
	traceto_mod[2] = bs->origin[2] + fwd[2]*4;

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = -23;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 8;

	JP_Trace(&tr, bs->origin, mins, maxs, traceto_mod, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction != 1)
	{
		return 0;
	}

	VectorCopy(bs->origin, tracefrom_mod);

	tracefrom_mod[2] += 31;//33;
	traceto_mod[2] += 31;//33;

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = 0;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 32;

	JP_Trace(&tr, tracefrom_mod, mins, maxs, traceto_mod, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction != 1)
	{
		return 1;
	}

	return 0;
}

static qboolean BotNav_IsInstantKillTrigger(gentity_t *ent)
{
	if (!ent || !ent->classname || !(ent->r.contents & CONTENTS_TRIGGER) ||
		!ent->r.linked || (ent->flags & FL_INACTIVE))
	{
		return qfalse;
	}

	if (Q_stricmp(ent->classname, "trigger_hurt"))
	{
		return qfalse;
	}

	return (ent->damage == -1 || ent->damage >= 1000) ? qtrue : qfalse;
}

//Instant-kill trigger_hurt volumes rarely change, so they are collected into a small
//list (refreshed every few seconds) instead of calling EntitiesInBox (a 4KB stack array
//plus an engine area query) for every nav/strafe-jump hazard sample.
#define BOTNAV_MAX_KILL_TRIGGERS 128
#define BOTNAV_KILL_TRIGGER_REFRESH_MS 5000
static int g_botNavKillTriggers[BOTNAV_MAX_KILL_TRIGGERS];
static int g_botNavKillTriggerCount;
static int g_botNavKillTriggerRefreshAt;
static qboolean g_botNavKillTriggerOverflow;

static void BotNav_RefreshKillTriggerCache(void)
{
	int i;

	//level.time restarts on map change, so also refresh when the stamp is far ahead.
	if (g_botNavKillTriggerRefreshAt > level.time &&
		g_botNavKillTriggerRefreshAt - level.time <= BOTNAV_KILL_TRIGGER_REFRESH_MS)
	{
		return;
	}
	g_botNavKillTriggerRefreshAt = level.time + BOTNAV_KILL_TRIGGER_REFRESH_MS;
	g_botNavKillTriggerCount = 0;
	g_botNavKillTriggerOverflow = qfalse;
	for (i = MAX_CLIENTS; i < level.num_entities; i++)
	{
		gentity_t *ent = &g_entities[i];

		if (!ent->inuse || !ent->classname || Q_stricmp(ent->classname, "trigger_hurt") ||
			!(ent->damage == -1 || ent->damage >= 1000))
		{
			continue;
		}
		if (g_botNavKillTriggerCount >= BOTNAV_MAX_KILL_TRIGGERS)
		{
			g_botNavKillTriggerOverflow = qtrue;
			break;
		}
		g_botNavKillTriggers[g_botNavKillTriggerCount++] = i;
	}
}

//True when a player box swept from start to end touches an instant-kill trigger.
//Pass start == end for a single position.
static qboolean BotNav_SweepTouchesInstantKillTrigger(const vec3_t start, const vec3_t end)
{
	static vec3_t playerMins = {-15.0f, -15.0f, DEFAULT_MINS_2};
	static vec3_t playerMaxs = {15.0f, 15.0f, DEFAULT_MAXS_2};
	vec3_t sweepMins, sweepMaxs, delta, sample, mins, maxs;
	int i, k;

	BotNav_RefreshKillTriggerCache();
	if (g_botNavKillTriggerOverflow)
	{
		//Pathological map with lots of kill volumes: fall back to the engine area query.
		int touch[MAX_GENTITIES];
		int num;
		for (k = 0; k < 3; k++)
		{
			sweepMins[k] = (start[k] < end[k] ? start[k] : end[k]) + playerMins[k];
			sweepMaxs[k] = (start[k] > end[k] ? start[k] : end[k]) + playerMaxs[k];
		}
		num = trap->EntitiesInBox(sweepMins, sweepMaxs, touch, MAX_GENTITIES);
		for (i = 0; i < num; i++)
		{
			gentity_t *hit = &g_entities[touch[i]];
			if (BotNav_IsInstantKillTrigger(hit) &&
				trap->EntityContact(sweepMins, sweepMaxs, (sharedEntity_t *)hit, qfalse))
				return qtrue;
		}
		return qfalse;
	}
	if (!g_botNavKillTriggerCount)
		return qfalse;

	for (k = 0; k < 3; k++)
	{
		sweepMins[k] = (start[k] < end[k] ? start[k] : end[k]) + playerMins[k];
		sweepMaxs[k] = (start[k] > end[k] ? start[k] : end[k]) + playerMaxs[k];
	}
	VectorSubtract(end, start, delta);
	for (i = 0; i < g_botNavKillTriggerCount; i++)
	{
		gentity_t *hit = &g_entities[g_botNavKillTriggers[i]];
		const float length = VectorLength(delta);
		int samples, n;

		if (!BotNav_IsInstantKillTrigger(hit))
			continue;
		if (hit->r.absmin[0] > sweepMaxs[0] || hit->r.absmax[0] < sweepMins[0] ||
			hit->r.absmin[1] > sweepMaxs[1] || hit->r.absmax[1] < sweepMins[1] ||
			hit->r.absmin[2] > sweepMaxs[2] || hit->r.absmax[2] < sweepMins[2])
			continue;
		//Bounds overlap: confirm against the brush along the segment.
		samples = (int)ceilf(length / 16.0f);
		for (n = 0; n <= samples; n++)
		{
			VectorMA(start, samples ? (float)n / (float)samples : 0.0f, delta, sample);
			VectorAdd(sample, playerMins, mins);
			VectorAdd(sample, playerMaxs, maxs);
			if (trap->EntityContact(mins, maxs, (sharedEntity_t *)hit, qfalse))
				return qtrue;
		}
	}
	return qfalse;
}

static qboolean BotNav_TouchesInstantKillTrigger(bot_state_t *bs, vec3_t origin)
{
	if (!bs)
	{
		return qfalse;
	}
	return BotNav_SweepTouchesInstantKillTrigger(origin, origin);
}

//Trace ahead in the movement direction to detect falling hazards (ledges, lava, death pits,
//and instant-kill trigger_hurt volumes). Returns qtrue if walking in moveDir would lead the
//bot off a dangerous drop or into one of those hazards.
static qboolean BotNav_CheckInstantDeathHazardSample(bot_state_t *bs, vec3_t moveDir, float forwardDist, float downTraceDist)
{
	vec3_t start, end;
	trace_t tr;

	if (!bs)
	{
		return qfalse;
	}

	VectorCopy(bs->origin, start);
	start[0] += moveDir[0] * forwardDist;
	start[1] += moveDir[1] * forwardDist;

	if (BotNav_TouchesInstantKillTrigger(bs, start))
	{
		return qtrue;
	}

	VectorCopy(start, end);
	end[2] -= downTraceDist;

	JP_Trace(&tr, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.contents & (CONTENTS_LAVA | CONTENTS_NODROP))
	{
		return qtrue;
	}
	if (BotNav_TouchesInstantKillTrigger(bs, tr.endpos))
	{
		return qtrue;
	}
	return qfalse;
}

static qboolean BotNav_CheckFallingHazard(bot_state_t *bs, vec3_t moveDir, qboolean inCombat)
{
	static const float pitLookAhead[] = {96.0f, 144.0f};
	vec3_t start, end;
	trace_t tr;
	const float extendedPitTrace = 512.0f;
	int i;

	(void)inCombat;

	if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE)
	{
		return qfalse; //already airborne, can't steer
	}

	//Project a point ahead of the bot in the movement direction
	VectorCopy(bs->origin, start);
	start[0] += moveDir[0] * 48.0f;
	start[1] += moveDir[1] * 48.0f;

	if (BotNav_CheckInstantDeathHazardSample(bs, moveDir, 48.0f, 256.0f))
	{
		return qtrue;
	}

	VectorCopy(start, end);
	end[2] -= 256.0f;

	JP_Trace(&tr, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	//All ordinary ledges are treated as safe; only explicit instant-death hazards should stop movement.
	if (tr.fraction < 1.0f &&
		((tr.contents & (CONTENTS_LAVA | CONTENTS_NODROP)) ||
		 BotNav_TouchesInstantKillTrigger(bs, tr.endpos)))
	{
		return qtrue;
	}

	//Broader hazard awareness: check farther ahead only for explicit instant-death hazards.
	for (i = 0; i < ARRAY_LEN(pitLookAhead); i++)
	{
		if (BotNav_CheckInstantDeathHazardSample(bs, moveDir, pitLookAhead[i], extendedPitTrace))
		{
			return qtrue;
		}
	}

	return qfalse;
}

static qboolean BotSFJ_SupportedMovementStyle(const playerState_t *ps)
{
	int style;

	if (!ps)
		return qfalse;
	if (ps->stats[STAT_RACEMODE])
		style = ps->stats[STAT_MOVEMENTSTYLE];
	else if ((g_movementStyle.integer >= MV_SIEGE && g_movementStyle.integer <= MV_WSW) ||
		g_movementStyle.integer == MV_SP || g_movementStyle.integer == MV_SLICK ||
		g_movementStyle.integer == MV_OCPM || g_movementStyle.integer == MV_TRIBES)
		style = g_movementStyle.integer;
	else if (g_movementStyle.integer < MV_SIEGE)
		style = MV_SIEGE;
	else
		style = MV_JKA;
	return (style == MV_JKA
#if _COOP
		|| style == MV_COOP_JKA
#endif
		) ? qtrue : qfalse;
}

static qboolean BotSFJ_EffectiveInputDirection(const bot_input_t *bi, vec3_t direction)
{
	int hasForwardOverride;
	int hasRightOverride;
	int forwardOverride = 0;
	int rightOverride = 0;
	float x, y;

	if (!bi)
		return qfalse;
	hasForwardOverride = bi->actionflags & (ACTION_MOVEFORWARD | ACTION_MOVEBACK);
	hasRightOverride = bi->actionflags & (ACTION_MOVELEFT | ACTION_MOVERIGHT);
	if (bi->actionflags & ACTION_MOVEFORWARD)
		forwardOverride = 127;
	if (bi->actionflags & ACTION_MOVEBACK)
		forwardOverride = -127;
	if (bi->actionflags & ACTION_MOVELEFT)
		rightOverride = -127;
	if (bi->actionflags & ACTION_MOVERIGHT)
		rightOverride = 127;
	if (BotSFJ_EffectiveMovement(bi->dir[0], bi->dir[1], bi->speed,
		bi->viewangles[YAW], hasForwardOverride, forwardOverride,
		hasRightOverride, rightOverride, &x, &y) <= 1.0f)
	{
		VectorClear(direction);
		return qfalse;
	}
	VectorSet(direction, x, y, 0.0f);
	return qtrue;
}

/*
 * Strafe-jump outcome counters for strafeJumpStats, so wall rejections and
 * releases can be compared between builds.
 */
#define BOT_SFJ_STAT_REASONS 48

typedef struct
{
	int starts;
	int takeoffs;
	int aborts;
	int slowReleases;
	int routeStarts;
	int rejects;
	int reasonCount;
	char reasonText[BOT_SFJ_STAT_REASONS][80];
	int reasonHits[BOT_SFJ_STAT_REASONS];
	int sinceTime;
} bot_sfj_stats_t;

static bot_sfj_stats_t g_botSfjStats;
/* Rejection reasons are only tallied once strafeJumpStats has been run (or while
 * bot_strafejumps_debug is on), so normal play pays no string matching. */
static qboolean g_botSfjStatsCollecting;

static void BotSFJ_CountReason(const char *reason)
{
	int i;

	if (!reason || !reason[0] || (!g_botSfjStatsCollecting && !bot_strafejumps_debug.integer))
		return;
	g_botSfjStats.rejects++;
	for (i = 0; i < g_botSfjStats.reasonCount; i++)
	{
		if (!Q_strncmp(g_botSfjStats.reasonText[i], reason,
				sizeof(g_botSfjStats.reasonText[i]) - 1))
		{
			g_botSfjStats.reasonHits[i]++;
			return;
		}
	}
	if (g_botSfjStats.reasonCount >= BOT_SFJ_STAT_REASONS)
		return;
	Q_strncpyz(g_botSfjStats.reasonText[g_botSfjStats.reasonCount], reason,
		sizeof(g_botSfjStats.reasonText[0]));
	g_botSfjStats.reasonHits[g_botSfjStats.reasonCount++] = 1;
}

void BotSFJ_PrintStats(qboolean reset)
{
	int i;
	int order[BOT_SFJ_STAT_REASONS];

	if (!g_botSfjStatsCollecting && !bot_strafejumps_debug.integer)
		trap->Print("Rejection reasons are counted from now on (run strafeJumpStats again later).\n");
	g_botSfjStatsCollecting = qtrue;
	trap->Print("Strafe-jump stats over %.1f s:\n",
		(float)(level.time - g_botSfjStats.sinceTime) / 1000.0f);
	trap->Print("  starts %i, takeoffs %i, route starts %i, aborts %i, wall/slow releases %i\n",
		g_botSfjStats.starts, g_botSfjStats.takeoffs, g_botSfjStats.routeStarts,
		g_botSfjStats.aborts, g_botSfjStats.slowReleases);
	trap->Print("  reject frames %i by reason (most frequent first):\n", g_botSfjStats.rejects);
	for (i = 0; i < g_botSfjStats.reasonCount; i++)
		order[i] = i;
	for (i = 1; i < g_botSfjStats.reasonCount; i++)
	{
		int key = order[i];
		int j = i - 1;

		while (j >= 0 && g_botSfjStats.reasonHits[order[j]] < g_botSfjStats.reasonHits[key])
		{
			order[j + 1] = order[j];
			j--;
		}
		order[j + 1] = key;
	}
	for (i = 0; i < g_botSfjStats.reasonCount; i++)
		trap->Print("  %8i  %s\n", g_botSfjStats.reasonHits[order[i]],
			g_botSfjStats.reasonText[order[i]]);
	if (reset)
	{
		memset(&g_botSfjStats, 0, sizeof(g_botSfjStats));
		g_botSfjStats.sinceTime = level.time;
		trap->Print("Strafe-jump stats reset.\n");
	}
}

/* strafeJumpStats [reset] */
void Svcmd_StrafeJumpStats_f(void)
{
	char arg[16] = { 0 };

	if (trap->Argc() > 1)
		trap->Argv(1, arg, sizeof(arg));
	BotSFJ_PrintStats(!Q_stricmp(arg, "reset") ? qtrue : qfalse);
}

static void BotSFJ_Clear(bot_state_t *bs)
{
	if (!bs)
		return;
	bs->sfjPhase = BOT_SFJ_PHASE_OFF;
	bs->sfjPeakSpeed = 0.0f;
	bs->sfjRoute = 0;
	bs->sfjRouteNode = 0;
	bs->sfjIntent = BOT_SFJ_INTENT_NONE;
	bs->sfjIntentTime = 0;
	bs->sfjSafetyUntil = 0;
	bs->sfjPhaseTime = 0;
	bs->sfjCooldownUntil = 0;
	bs->sfjNextStartTime = 0;
	bs->sfjStartFrequency = 0;
	bs->sfjOwnsInput = qfalse;
	bs->sfjLastRandomUse = qfalse;
	bs->sfjPursuitLatched = qfalse;
	bs->sfjLastEnemyDistance = 0.0f;
	bs->sfjLastEnemyDistanceTime = 0;
	bs->sfjLastEnemyTargetNum = -1;
	bs->sfjArcCheckedTime = 0;
	bs->sfjArcSafe = qfalse;
	bs->sfjCorridorValid = qfalse;
	bs->sfjCorridorForwardGoal = qfalse;
	VectorClear(bs->sfjIntentDirection);
	VectorClear(bs->sfjIntentDestination);
}

static void BotSFJ_Abort(bot_state_t *bs, int time)
{
	if (!bs)
		return;
	if (bs->sfjPhase != BOT_SFJ_PHASE_ABORT && bs->sfjPhase != BOT_SFJ_PHASE_OFF)
		g_botSfjStats.aborts++;
	bs->sfjPhase = BOT_SFJ_PHASE_ABORT;
	bs->sfjPhaseTime = time;
	bs->sfjCooldownUntil = time + BOT_SFJ_ABORT_COOLDOWN_MS;
	bs->sfjPeakSpeed = 0.0f;
	bs->sfjRoute = 0;
	bs->sfjRouteNode = 0;
	bs->sfjIntent = BOT_SFJ_INTENT_NONE;
	bs->sfjIntentTime = 0;
	bs->sfjSafetyUntil = 0;
	bs->sfjOwnsInput = qfalse;
	bs->sfjCorridorValid = qfalse;
	bs->sfjCorridorForwardGoal = qfalse;
}

/* A wall (or anything) slowed the strafe to ground speed: hand control back to
 * normal navigation straight away, without the abort cooldown. */
static void BotSFJ_Release(bot_state_t *bs, int time)
{
	if (!bs)
		return;
	g_botSfjStats.slowReleases++;
	bs->sfjPhase = BOT_SFJ_PHASE_OFF;
	bs->sfjPhaseTime = time;
	bs->sfjCooldownUntil = 0;
	bs->sfjIntent = BOT_SFJ_INTENT_NONE;
	bs->sfjIntentTime = 0;
	bs->sfjSafetyUntil = 0;
	bs->sfjOwnsInput = qfalse;
	bs->sfjCorridorValid = qfalse;
	bs->sfjCorridorForwardGoal = qfalse;
	bs->sfjPeakSpeed = 0.0f;
	bs->sfjRoute = 0;
	bs->sfjRouteNode = 0;
	bs->sfjArcCheckedTime = 0;
}

#define BOT_SFJ_HARD(text) do { *reason = (text); return BOT_SFJ_CONFLICT_HARD; } while (0)
#define BOT_SFJ_SOFT(text) do { *reason = (text); return BOT_SFJ_CONFLICT_SOFT; } while (0)

/*
 * Classify why the bot cannot strafe jump this frame. Hard conflicts always
 * abort; soft conflicts only block initiation (see BotSFJ_ConflictBlocks).
 * Hard checks run first so the reported reason is the most severe one.
 */
static bot_sfj_conflict_t BotSFJ_GetInputConflict(bot_state_t *bs, playerState_t *ps,
	const bot_input_t *bi, const char **reason)
{
	const int forceMovementPowers = (1 << FP_GRIP) | (1 << FP_DRAIN) |
		(1 << FP_LIGHTNING) | (1 << FP_SPEED) | (1 << FP_RAGE);
	const int hardActions = ACTION_FORCEPOWER | ACTION_SKI;
	const int softActions = ACTION_ATTACK | ACTION_ALT_ATTACK | ACTION_CROUCH |
		ACTION_JUMP | ACTION_DELAYEDJUMP | ACTION_WALK;
	const char *unused;
	gclient_t *client;

	if (!reason)
		reason = &unused;
	*reason = NULL;
	if (!bs || !ps || !bi)
		BOT_SFJ_HARD("missing state");
	client = g_entities[bs->client].client;
	if (!client || g_entities[bs->client].health <= 0)
		BOT_SFJ_HARD("dead");
	if (ps->pm_type != PM_NORMAL || ps->m_iVehicleNum)
		BOT_SFJ_HARD("not in normal movement (pm_type/vehicle)");
	if (g_entities[bs->client].waterlevel > 0)
		BOT_SFJ_HARD("in water");
	if (!BotSFJ_SupportedMovementStyle(ps))
		BOT_SFJ_HARD("unsupported movement style");
	/* ps->speed is post-pmove (backpedal/saber scaling); test the real causes. */
	if (ps->basespeed <= 0 || client->bodyGrabIndex != ENTITYNUM_NONE)
		BOT_SFJ_HARD("base speed modified (zero or dragging body)");
	if (ps->forceHandExtend != HANDEXTEND_NONE)
		BOT_SFJ_HARD("force hand extend (push/pull/throw/knockdown)");
	if ((ps->fd.forcePowersActive & forceMovementPowers) ||
		ps->fd.forceRageRecoveryTime > level.time || ps->fd.forceGripCripple)
		BOT_SFJ_HARD("force power active (grip/drain/lightning/speed/rage)");
	if ((ps->fd.forcePowersActive & (1 << FP_LEVITATION)) &&
		bs->sfjPhase == BOT_SFJ_PHASE_OFF)
		BOT_SFJ_HARD("levitation already active");
	if (ps->fd.forceJumpCharge > 0 || ps->forceJumpFlip || bs->forceJumping > level.time)
		BOT_SFJ_HARD("force jump charge/flip");
	if (ps->fd.forcePowerLevel[FP_LEVITATION] < FORCE_LEVEL_0 ||
		ps->fd.forcePowerLevel[FP_LEVITATION] > FORCE_LEVEL_3 ||
		(ps->stats[STAT_RESTRICTIONS] & JAPRO_RESTRICT_SUPERJUMP))
		BOT_SFJ_HARD("jump level restricted");
	if (ps->pm_flags & (PMF_TIME_WATERJUMP | PMF_STUCK_TO_WALL | PMF_RESPAWNED))
		BOT_SFJ_HARD("waterjump/wall-stuck/respawn flags");
	if (BG_InKnockDown(ps->legsAnim) || BG_InRoll(ps, ps->legsAnim))
		BOT_SFJ_HARD("knockdown or roll");
	if (BG_InSpecialJump(ps->legsAnim) || BG_InReboundJump(ps->legsAnim))
		BOT_SFJ_HARD("special/rebound jump anim");
	if (ps->saberLockTime > level.time)
		BOT_SFJ_HARD("saber lock");
	if (bs->state_Forced || bs->forceMove_Forward || bs->forceMove_Right ||
		bs->forceMove_Up)
		BOT_SFJ_HARD("forced movement state");
	if (bs->pullKickJumpTime > 0 || bs->flipkickInputTime > level.time ||
		bs->gripkickActive || NewBotAI_HasExclusiveFlipkickMovement(bs))
		BOT_SFJ_HARD("kick technique owns movement");
	if (bs->escapeYawOverrideUntil > level.time)
		BOT_SFJ_HARD("escape yaw override");
	if (bs->beStill >= level.time || WaitingForNow(bs, bs->goalPosition) ||
		bs->isCamping > level.time || bs->wpCamping)
		BOT_SFJ_HARD("holding position (beStill/waiting/camping)");
	if (bi->actionflags & hardActions)
		BOT_SFJ_HARD("queued force power or ski");
	if (BotSFJ_UseIsConflict(bi->actionflags & ACTION_USE,
		client->pers.cmd.buttons & BUTTON_USE, bs->sfjLastRandomUse))
		BOT_SFJ_HARD("queued use");

	if (bi->actionflags & (ACTION_ATTACK | ACTION_ALT_ATTACK))
		BOT_SFJ_SOFT("queued attack");
	if (bi->actionflags & (softActions & ~(ACTION_ATTACK | ACTION_ALT_ATTACK)))
		BOT_SFJ_SOFT("queued jump/crouch/walk");
	if (ps->weaponTime > 0)
		BOT_SFJ_SOFT("weaponTime active");
	if (BG_SaberInSpecialAttack(ps->legsAnim) || BG_SaberInSpecialAttack(ps->torsoAnim) ||
		BG_SaberInSpecial(ps->saberMove))
		BOT_SFJ_SOFT("saber special anim");
	if (bs->saberDefenseActive)
		BOT_SFJ_SOFT("saber defense active");
	if (bs->jumpTime > level.time || bs->jumpHoldTime > level.time ||
		bs->jumpPrep > level.time)
		BOT_SFJ_SOFT("navigation jump timer");
	if (bs->sfjPhase == BOT_SFJ_PHASE_OFF &&
		((ps->pm_flags & PMF_JUMP_HELD) || bs->lastucmd.upmove > 0))
		BOT_SFJ_SOFT("jump still held");
	return BOT_SFJ_CONFLICT_NONE;
}

#undef BOT_SFJ_HARD
#undef BOT_SFJ_SOFT

#define BOT_SFJ_CORRIDOR_LOOKAHEAD 1024.0f
#define BOT_SFJ_CORRIDOR_STEER_DISTANCE 512.0f
#define BOT_SFJ_CORRIDOR_MIN_LENGTH 160.0f
#define BOT_SFJ_CORRIDOR_STRAIGHTNESS 0.9f
#define BOT_SFJ_NAV_ENEMY_GUARD 384.0f
#define BOT_SFJ_NAV_ENEMY_CLOSING_SPEED 250.0f

static void BotSFJ_DebugReject(bot_state_t *bs, const char *reason)
{
	gclient_t *client;

	BotSFJ_CountReason(reason);
	if (!bs || !reason || !bot_strafejumps_debug.integer)
		return;
	if (bs->sfjDebugNextTime > level.time && bs->sfjDebugNextTime - level.time <= 1000)
		return;
	bs->sfjDebugNextTime = level.time + 1000;
	client = g_entities[bs->client].client;
	Com_Printf("^5[SFJ]^7 %s: %s\n", client ? client->pers.netname : "bot", reason);
	if (bot_strafejumps_debug.integer >= 3)
	{
		trap->SendServerCommand(-1, va("print \"[SFJ] bot %i: %s%s\n\"",
			bs->client, bot_onlystrafes.integer ? "safe strafe preference: " : "", reason));
	}
}

static qboolean BotSFJ_WaypointsLinked(const wpobject_t *from, int toIndex)
{
	int i;

	for (i = 0; i < from->neighbornum; i++)
	{
		if (from->neighbors[i].num == toIndex)
			return qtrue;
	}
	/* Linear waypoint trails use their contiguous index as an implicit link. */
	return from->neighbornum <= 0 ? qtrue : qfalse;
}

static qboolean BotWaypointSkipPathSafe(bot_state_t *bs, const vec3_t destination,
	float maxDistance)
{
	vec3_t delta, point, floor;
	vec3_t mins = {-15.0f, -15.0f, DEFAULT_MINS_2};
	vec3_t maxs = {15.0f, 15.0f, DEFAULT_MAXS_2};
	trace_t trace;
	int sample, samples;
	float distance;
	const int mask = MASK_PLAYERSOLID | CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP;

	if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE)
		return qfalse;
	VectorSubtract(destination, bs->origin, delta);
	distance = VectorLength(delta);
	if (distance > maxDistance)
		return qfalse;
	JP_Trace(&trace, bs->origin, mins, maxs, (float *)destination,
		bs->client, mask, qfalse, 0, 0);
	if (trace.startsolid || trace.allsolid || trace.fraction < 1.0f ||
		BotNav_SweepTouchesInstantKillTrigger(bs->origin, destination))
		return qfalse;
	samples = (int)(distance / 32.0f) + 1;
	for (sample = 1; sample <= samples; sample++)
	{
		VectorMA(bs->origin, (float)sample / samples, delta, point);
		VectorCopy(point, floor);
		floor[2] -= 48.0f;
		JP_Trace(&trace, point, mins, maxs, floor, bs->client, mask, qfalse, 0, 0);
		if (trace.startsolid || trace.allsolid || trace.fraction >= 1.0f ||
			trace.entityNum != ENTITYNUM_WORLD || trace.plane.normal[2] < 0.7f ||
			(trace.contents & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP)) ||
			BotNav_SweepTouchesInstantKillTrigger(point, trace.endpos))
			return qfalse;
	}
	return qtrue;
}

/*
 * Walk forward along the bot's waypoint trail and return a straight, flat
 * corridor up to BOT_SFJ_CORRIDOR_LOOKAHEAD units long.  The steering direction
 * aims at a look-ahead point on the trail (path smoothing) instead of the very
 * next waypoint, so a strafe jump can carry the bot past several waypoints.
 */
static qboolean BotSFJ_GetOpenCorridor(bot_state_t *bs, vec3_t direction, vec3_t destination)
{
	const int waypointBudget = BotSFJ_WaypointBudget(bot_strafejumpwaypoints.integer);
	const qboolean grounded = bs && bs->cur_ps.groundEntityNum != ENTITYNUM_NONE;
	vec3_t lastPoint, firstDirection, steerPoint;
	wpobject_t *wp;
	wpobject_t *previous = NULL;
	float total = 0.0f;
	float baseZ;
	qboolean haveDirection = qfalse;
	qboolean haveSteer = qfalse;
	qboolean reachedEnd = qfalse;
	int step;
	int index;
	int count;

	if (!bs || !bs->wpCurrent || bs->doingFallback ||
		bs->wpCurrent->index < 0 || bs->wpCurrent->index >= gWPNum ||
		(!bs->frame_Waypoint_Vis && !(bs->wpCurrent->flags & WPFLAG_NOVIS)))
		return qfalse;
	step = bs->wpDirection ? -1 : 1;
	if (bs->wpDestination &&
		((step > 0 && bs->wpCurrent->index > bs->wpDestination->index) ||
		 (step < 0 && bs->wpCurrent->index < bs->wpDestination->index)))
		return qfalse;

	VectorCopy(bs->origin, lastPoint);
	/* Flight height is not a change in the underlying trail's elevation. */
	baseZ = grounded ? bs->origin[2] : bs->wpCurrent->origin[2];
	VectorClear(firstDirection);
	VectorClear(steerPoint);
	index = bs->wpCurrent->index;
	for (count = 0; count < waypointBudget; count++, index += step)
	{
		vec3_t segment;
		float segmentLength;

		if (index < 0 || index >= gWPNum)
			break;
		wp = gWPArray[index];
		if (!wp || !wp->inuse || wp->flags || wp->forceJumpTo ||
			!PassWayCheck(bs, index))
			break;
		if (previous)
		{
			if (!BotSFJ_WaypointsLinked(previous, index) ||
				fabsf(wp->origin[2] - previous->origin[2]) > 32.0f)
				break;
		}
		if (fabsf(wp->origin[2] - baseZ) > 64.0f)
			break;
		if (!previous && bs->sfjCorridorForwardGoal && bs->sfjOwnsInput &&
			BotSFJ_IntentIsFresh(level.time, bs->sfjIntentTime) &&
			BotSFJ_WaypointPassed(bs->origin, wp->origin, bs->sfjIntentDirection, 64.0f))
		{
			/* Keep the passed source as a link anchor, not a backward segment. */
			previous = wp;
			continue;
		}
		VectorSubtract(wp->origin, lastPoint, segment);
		segment[2] = 0.0f;
		segmentLength = VectorNormalize(segment);
		if (!previous && segmentLength > 640.0f)
			return qfalse;
		if (segmentLength > 1.0f)
		{
			/* A waypoint the bot has nearly reached says little about direction. */
			const qboolean nearlyReached = !previous && segmentLength < 48.0f;

			if (haveDirection &&
				DotProduct(segment, firstDirection) < BOT_SFJ_CORRIDOR_STRAIGHTNESS)
				break;
			if (!haveDirection && !nearlyReached)
			{
				VectorCopy(segment, firstDirection);
				haveDirection = qtrue;
			}
			if (!haveSteer && total + segmentLength >= BOT_SFJ_CORRIDOR_STEER_DISTANCE)
			{
				VectorMA(lastPoint, BOT_SFJ_CORRIDOR_STEER_DISTANCE - total, segment, steerPoint);
				haveSteer = qtrue;
			}
			if (total + segmentLength >= BOT_SFJ_CORRIDOR_LOOKAHEAD)
			{
				VectorMA(lastPoint, BOT_SFJ_CORRIDOR_LOOKAHEAD - total,
					segment, lastPoint);
				total = BOT_SFJ_CORRIDOR_LOOKAHEAD;
				previous = wp;
				reachedEnd = qtrue;
				break;
			}
			total += segmentLength;
		}
		VectorCopy(wp->origin, lastPoint);
		previous = wp;
		if (total >= BOT_SFJ_CORRIDOR_LOOKAHEAD ||
			(bs->wpDestination && wp == bs->wpDestination))
		{
			reachedEnd = qtrue;
			break;
		}
	}
	if (!reachedEnd && count == waypointBudget)
		BotSFJ_DebugReject(bs, "corridor density budget exhausted (bot_strafejumpwaypoints)");
	if (!previous || !haveDirection ||
		total < (grounded ? BOT_SFJ_CORRIDOR_MIN_LENGTH : 64.0f))
		return qfalse;
	if (!haveSteer)
		VectorCopy(lastPoint, steerPoint);
	VectorSubtract(steerPoint, bs->origin, direction);
	direction[2] = 0.0f;
	if (VectorNormalize(direction) <= 0.0f ||
		DotProduct(direction, firstDirection) < BOT_SFJ_CORRIDOR_STRAIGHTNESS)
		return qfalse;
	VectorCopy(lastPoint, destination);
	VectorCopy(bs->origin, bs->sfjCorridorStart);
	bs->sfjCorridorStart[2] = baseZ;
	bs->sfjCorridorFirst = bs->wpCurrent->index;
	bs->sfjCorridorLast = previous->index;
	bs->sfjCorridorStep = step;
	bs->sfjCorridorValid = qtrue;
	return qtrue;
}

/*
 * Consume only passed, unflagged points in the previously accepted corridor.
 * Run before navigation queues movement, otherwise an airborne bot aims back
 * at a passed dense waypoint and loses strafe input ownership.
 */
static void BotSFJ_AdvancePassedWaypoints(bot_state_t *bs)
{
	vec3_t delta, segment, end;
	vec3_t mins = {-15.0f, -15.0f, DEFAULT_MINS_2};
	vec3_t maxs = {15.0f, 15.0f, DEFAULT_MAXS_2};
	trace_t trace;
	int count;
	int step;
	const int budget = BotSFJ_WaypointBudget(bot_strafejumpwaypoints.integer);
	const int mask = MASK_PLAYERSOLID | CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP;

	if (bs)
		bs->sfjCorridorForwardGoal = qfalse;
	if (!bs || !bs->wpCurrent || !bs->sfjCorridorValid || !bs->sfjOwnsInput ||
		!bot_strafejumps.integer || bot_strafejumpfrequency.integer <= 0 ||
		!BotSFJ_CanOwnInput(1, BotSFJ_IntentIsFresh(level.time, bs->sfjIntentTime),
			bs->sfjSafetyUntil >= level.time, bs->sfjPhase))
		return;
	if (bs->currentEnemy && bs->frame_Enemy_Vis)
	{
		BotSFJ_DebugReject(bs, "safe advance: visible enemy combat lock");
		return;
	}
	step = bs->wpDirection ? -1 : 1;
	if (step != bs->sfjCorridorStep ||
		!BotSFJ_LandingWithinCorridor(bs->sfjCorridorStart[0], bs->sfjCorridorStart[1],
			bs->sfjIntentDestination[0], bs->sfjIntentDestination[1],
			bs->origin[0], bs->origin[1], 64.0f, 0.0f))
	{
		BotSFJ_DebugReject(bs, "safe advance: outside validated intent corridor");
		return;
	}
	for (count = 0; count < budget; count++)
	{
		wpobject_t *current = bs->wpCurrent;
		wpobject_t *next;
		const int nextIndex = current->index + step;
		float alignment;
		float length;
		float endpointProgress;
		qboolean clippedTarget;

		if (!current->inuse || current->flags || current->forceJumpTo ||
			current == bs->wpDestination ||
			(current->index - bs->sfjCorridorFirst) * step < 0 ||
			(nextIndex - bs->sfjCorridorLast) * step > 0 ||
			nextIndex < 0 || nextIndex >= gWPNum)
		{
			BotSFJ_DebugReject(bs, "safe advance: required waypoint or corridor endpoint");
			break;
		}
		next = gWPArray[nextIndex];
		if (!next || !next->inuse)
		{
			BotSFJ_DebugReject(bs, "safe advance: missing corridor waypoint");
			break;
		}
		VectorSubtract(next->origin, current->origin, segment);
		segment[2] = 0.0f;
		length = VectorNormalize(segment);
		alignment = length > 1.0f ? DotProduct(segment, bs->sfjIntentDirection) : 1.0f;
		if (!BotSFJ_WaypointPassed(bs->origin, current->origin,
			bs->sfjIntentDirection, 64.0f))
			break;
		VectorSubtract(bs->sfjIntentDestination, next->origin, delta);
		endpointProgress = DotProduct(delta, bs->sfjIntentDirection);
		VectorSubtract(next->origin, bs->origin, delta);
		delta[2] = 0.0f;
		/* A far new first point would fail the corridor's 640-unit entry bound. */
		clippedTarget = BotSFJ_UseClippedForwardTarget(1, endpointProgress,
			VectorLength(delta)) ? qtrue : qfalse;
		if (!BotSFJ_AdvanceLinkAllows(next->flags || next->forceJumpTo,
			BotSFJ_WaypointsLinked(current, nextIndex), PassWayCheck(bs, nextIndex),
			next->origin[2] - current->origin[2],
			next->origin[2] - bs->sfjCorridorStart[2], alignment,
			clippedTarget ? 0.0f : endpointProgress) ||
			(bs->wpDestination && (nextIndex - bs->wpDestination->index) * step > 0))
		{
			BotSFJ_DebugReject(bs, "safe advance: required waypoint, link, height, turn or endpoint");
			break;
		}
		/* Collision/hazard sweep at actual flight height, not a walk-floor test. */
		VectorCopy(clippedTarget ? bs->sfjIntentDestination : next->origin, end);
		end[2] = bs->origin[2];
		JP_Trace(&trace, bs->origin, mins, maxs, end, bs->client, mask, qfalse, 0, 0);
		if (trace.startsolid || trace.allsolid || trace.fraction < 1.0f ||
			BotNav_SweepTouchesInstantKillTrigger(bs->origin, end) ||
			(bs->cur_ps.groundEntityNum != ENTITYNUM_NONE &&
				!BotWaypointSkipPathSafe(bs, end, BOT_SFJ_CORRIDOR_LOOKAHEAD)))
		{
			BotSFJ_DebugReject(bs, "safe advance: blocked path, hazard or unsafe grounded floor");
			break;
		}
		if (clippedTarget)
		{
			/* Keep the link anchor until the next point is a valid first target. */
			bs->sfjCorridorForwardGoal = qtrue;
			break;
		}
		bs->lastWPIndex = current->index;
		bs->lastWPDir = bs->wpDirection;
		bs->wpCurrent = next;
		bs->wpTravelTime = level.time + 10000;
		bs->wpSeenTime = level.time + 1500;
	}
	if (count == budget)
		BotSFJ_DebugReject(bs, "safe advance: density budget exhausted");
}

/*
 * Direct chase/escape corridor used when the bot is moving straight toward or
 * away from its enemy rather than following waypoints.  The arc simulation still
 * validates the landing, so this only has to find open space ahead.
 */
static qboolean BotSFJ_GetEnemyCorridor(bot_state_t *bs, const vec3_t moveDirection,
	const vec3_t toEnemy, float enemyDistance, qboolean retreat,
	vec3_t direction, vec3_t destination)
{
	static vec3_t corridorMins = {-16.0f, -16.0f, 0.0f};
	static vec3_t corridorMaxs = {16.0f, 16.0f, 32.0f};
	vec3_t start, end;
	trace_t trace;
	float alignment;
	float length;

	if (!bs)
		return qfalse;
	alignment = DotProduct(moveDirection, toEnemy);
	if (retreat ? alignment > -0.5f : alignment < 0.7f)
		return qfalse;
	length = retreat ? BOT_SFJ_CORRIDOR_LOOKAHEAD : enemyDistance - 64.0f;
	if (length > BOT_SFJ_CORRIDOR_LOOKAHEAD)
		length = BOT_SFJ_CORRIDOR_LOOKAHEAD;
	if (length < BOT_SFJ_CORRIDOR_MIN_LENGTH)
		return qfalse;
	VectorCopy(bs->origin, start);
	start[2] += 18.0f;
	VectorMA(start, length, moveDirection, end);
	JP_Trace(&trace, start, corridorMins, corridorMaxs, end, bs->client,
		MASK_PLAYERSOLID & ~CONTENTS_BODY, qfalse, 0, 0);
	if (trace.startsolid || trace.allsolid)
		return qfalse;
	length *= trace.fraction;
	length -= 32.0f;
	if (length < BOT_SFJ_CORRIDOR_MIN_LENGTH)
		return qfalse;
	VectorCopy(moveDirection, direction);
	direction[2] = 0.0f;
	if (VectorNormalize(direction) <= 0.0f)
		return qfalse;
	VectorMA(bs->origin, length, direction, destination);
	return qtrue;
}

/*
 * Hand-authored strafe-jump routes, read once per map from
 * botroutes/<map>.botroute (or routes/<map>.botroute):
 *
 *   strafejump
 *   {
 *       start_pos   1200.5 -450.0 128.0
 *       end_pos     1850.0 -450.0 128.0
 *       min_speed   450
 *   }
 *
 * src_area/dest_area from AAS-based route files are accepted and ignored; these
 * bots navigate by waypoints, so a hint is matched by position instead.
 *
 * Recorded human routes (bot_strafetrack + exportStrafeTrack) are multi-node:
 *
 *   strafejump_route
 *   {
 *       start_pos   x y z            // where the human circle-jumped
 *       min_speed   320              // speed at the circle jump
 *       node        x y z speed side air_ms   // one per landing, in order
 *   }
 *
 * side is the strafe key held during the hop that ended at the node (-1 left,
 * 1 right) and air_ms its flight time.  Recorded routes take precedence over
 * single hints and waypoints; waypoints remain the fallback.
 */
#define BOT_SFJ_MAX_ROUTE_HINTS 256
#define BOT_SFJ_MAX_TRACK_ROUTES 128
#define BOT_SFJ_ROUTE_FILE_MAX 1048576
#define BOT_SFJ_TRACK_ROUTE_START_RADIUS 96.0f
#define BOT_SFJ_TRACK_ROUTE_HEIGHT_TOLERANCE 64.0f
#define BOT_SFJ_TRACK_NODE_REACHED 80.0f
#define BOT_SFJ_TRACK_NODE_TIMEOUT_MS 3000
/* cos(~45 deg): the first recorded landing must lie ahead of the bot's movement. */
#define BOT_SFJ_TRACK_ROUTE_MIN_ALIGNMENT 0.7f

typedef struct
{
	vec3_t start;
	float minSpeed;
	int nodeCount;
	vec3_t node[BOT_SFJ_TRACK_MAX_NODES];
	float nodeSpeed[BOT_SFJ_TRACK_MAX_NODES];
	int nodeSide[BOT_SFJ_TRACK_MAX_NODES];
	int nodeAirMs[BOT_SFJ_TRACK_MAX_NODES];
} bot_sfj_track_route_t;

static bot_sfj_track_route_t g_botSfjTrackRoutes[BOT_SFJ_MAX_TRACK_ROUTES];
static int g_botSfjTrackRouteCount;

typedef struct
{
	vec3_t start;
	vec3_t end;
	float minSpeed;
} bot_sfj_route_hint_t;

static bot_sfj_route_hint_t g_botSfjRouteHints[BOT_SFJ_MAX_ROUTE_HINTS];
static int g_botSfjRouteHintCount;
static qboolean g_botSfjRouteHintsLoaded; /* the game module is reloaded on map change */

static qboolean BotSFJ_ParseRouteVector(const char **text, vec3_t out)
{
	int i;

	for (i = 0; i < 3; i++)
	{
		if (COM_ParseFloat(text, &out[i]))
			return qfalse;
	}
	return qtrue;
}

/* Parses the body of a strafejump_route block (after the opening brace). */
static void BotSFJ_ParseTrackRoute(const char **text, const char *path)
{
	bot_sfj_track_route_t route;
	qboolean haveStart = qfalse, valid = qtrue;
	char *token;

	memset(&route, 0, sizeof(route));
	while (1)
	{
		token = COM_ParseExt(text, qtrue);
		if (!token[0])
		{
			trap->Print(S_COLOR_YELLOW "%s: unexpected end of file in strafejump_route\n", path);
			return;
		}
		if (!strcmp(token, "}"))
			break;
		if (!Q_stricmp(token, "start_pos"))
		{
			if (BotSFJ_ParseRouteVector(text, route.start))
				haveStart = qtrue;
			else
				valid = qfalse;
		}
		else if (!Q_stricmp(token, "min_speed"))
		{
			if (COM_ParseFloat(text, &route.minSpeed))
				valid = qfalse;
		}
		else if (!Q_stricmp(token, "node"))
		{
			vec3_t origin;
			float speed = 0.0f;
			int side = 0, airMs = 0;

			if (!BotSFJ_ParseRouteVector(text, origin) || COM_ParseFloat(text, &speed) ||
				COM_ParseInt(text, &side) || COM_ParseInt(text, &airMs))
			{
				valid = qfalse;
				continue;
			}
			if (route.nodeCount >= BOT_SFJ_TRACK_MAX_NODES)
				continue;
			VectorCopy(origin, route.node[route.nodeCount]);
			route.nodeSpeed[route.nodeCount] = speed;
			route.nodeSide[route.nodeCount] = side < 0 ? -1 : (side > 0 ? 1 : 0);
			route.nodeAirMs[route.nodeCount] = airMs;
			route.nodeCount++;
		}
		else
			COM_ParseExt(text, qfalse);
	}
	if (!valid || !haveStart || route.nodeCount < 1)
	{
		trap->Print(S_COLOR_YELLOW "%s: skipping strafejump_route without valid start_pos/nodes\n", path);
		return;
	}
	if (g_botSfjTrackRouteCount >= BOT_SFJ_MAX_TRACK_ROUTES)
		return;
	g_botSfjTrackRoutes[g_botSfjTrackRouteCount++] = route;
}

static void BotSFJ_ParseRouteHints(const char *buffer, const char *path)
{
	const char *text = buffer;
	char *token;

	COM_BeginParseSession(path);
	while (1)
	{
		bot_sfj_route_hint_t hint;
		qboolean haveStart = qfalse, haveEnd = qfalse, valid = qtrue;

		token = COM_ParseExt(&text, qtrue);
		if (!token[0])
			break;
		if (!Q_stricmp(token, "strafejump_route"))
		{
			token = COM_ParseExt(&text, qtrue);
			if (strcmp(token, "{"))
			{
				trap->Print(S_COLOR_YELLOW "%s: expected { after strafejump_route\n", path);
				break;
			}
			BotSFJ_ParseTrackRoute(&text, path);
			continue;
		}
		if (Q_stricmp(token, "strafejump"))
		{
			if (!strcmp(token, "{"))
				SkipBracedSection(&text, 1);
			continue;
		}
		token = COM_ParseExt(&text, qtrue);
		if (strcmp(token, "{"))
		{
			trap->Print(S_COLOR_YELLOW "%s: expected { after strafejump\n", path);
			break;
		}
		memset(&hint, 0, sizeof(hint));
		while (1)
		{
			token = COM_ParseExt(&text, qtrue);
			if (!token[0])
			{
				trap->Print(S_COLOR_YELLOW "%s: unexpected end of file\n", path);
				valid = qfalse;
				break;
			}
			if (!strcmp(token, "}"))
				break;
			if (!Q_stricmp(token, "start_pos"))
			{
				if (BotSFJ_ParseRouteVector(&text, hint.start))
					haveStart = qtrue;
				else
					valid = qfalse;
			}
			else if (!Q_stricmp(token, "end_pos"))
			{
				if (BotSFJ_ParseRouteVector(&text, hint.end))
					haveEnd = qtrue;
				else
					valid = qfalse;
			}
			else if (!Q_stricmp(token, "min_speed"))
			{
				if (COM_ParseFloat(&text, &hint.minSpeed))
					valid = qfalse;
			}
			else
				COM_ParseExt(&text, qfalse); /* src_area, dest_area, unknown keys */
		}
		if (!valid || !haveStart || !haveEnd)
		{
			trap->Print(S_COLOR_YELLOW "%s: skipping strafejump block without valid start_pos/end_pos\n", path);
			continue;
		}
		if (g_botSfjRouteHintCount >= BOT_SFJ_MAX_ROUTE_HINTS)
		{
			trap->Print(S_COLOR_YELLOW "%s: more than %i strafejump routes, ignoring the rest\n",
				path, BOT_SFJ_MAX_ROUTE_HINTS);
			continue;
		}
		g_botSfjRouteHints[g_botSfjRouteHintCount++] = hint;
	}
}

static void BotSFJ_LoadRouteHints(void)
{
	static const char *folders[] = { "botroutes", "routes" };
	char mapname[MAX_QPATH];
	char path[MAX_QPATH];
	fileHandle_t f;
	char *buffer;
	int len;
	int i;

	if (g_botSfjRouteHintsLoaded)
		return;
	g_botSfjRouteHintsLoaded = qtrue;
	g_botSfjRouteHintCount = 0;
	g_botSfjTrackRouteCount = 0;
	trap->Cvar_VariableStringBuffer("mapname", mapname, sizeof(mapname));
	if (!mapname[0])
		return;
	for (i = 0; i < (int)ARRAY_LEN(folders); i++)
	{
		Com_sprintf(path, sizeof(path), "%s/%s.botroute", folders[i], mapname);
		len = trap->FS_Open(path, &f, FS_READ);
		if (!f)
			continue;
		if (len <= 0 || len >= BOT_SFJ_ROUTE_FILE_MAX)
		{
			trap->Print(S_COLOR_YELLOW "%s: empty or larger than %i bytes, ignored\n",
				path, BOT_SFJ_ROUTE_FILE_MAX);
			trap->FS_Close(f);
			return;
		}
		buffer = (char *)malloc(len + 1);
		if (!buffer)
		{
			trap->FS_Close(f);
			return;
		}
		trap->FS_Read(buffer, len, f);
		trap->FS_Close(f);
		buffer[len] = '\0';
		BotSFJ_ParseRouteHints(buffer, path);
		free(buffer);
		trap->Print("Loaded %i strafe-jump route hint(s) and %i recorded route(s) from %s\n",
			g_botSfjRouteHintCount, g_botSfjTrackRouteCount, path);
		return;
	}
}

/* exportStrafeTrack rewrites the route file; pick it up on the next bot frame. */
void BotSFJ_ReloadRouteHints(void)
{
	int i;

	g_botSfjRouteHintsLoaded = qfalse;
	for (i = 0; i < MAX_CLIENTS; i++)
	{
		if (botstates[i] && botstates[i]->inuse)
		{
			botstates[i]->sfjRoute = 0;
			botstates[i]->sfjRouteNode = 0;
		}
	}
	BotSFJ_LoadRouteHints();
}

/* Current recorded-route node for a bot following one (movement goal override). */
static qboolean BotSFJ_GetTrackRouteGoal(const bot_state_t *bs, vec3_t goal)
{
	const bot_sfj_track_route_t *route;

	if (!bs || bs->sfjRoute <= 0 || bs->sfjRoute > g_botSfjTrackRouteCount)
		return qfalse;
	route = &g_botSfjTrackRoutes[bs->sfjRoute - 1];
	if (bs->sfjRouteNode < 0 || bs->sfjRouteNode >= route->nodeCount)
		return qfalse;
	VectorCopy(route->node[bs->sfjRouteNode], goal);
	return qtrue;
}

static void BotSFJ_EndTrackRoute(bot_state_t *bs, const char *reason)
{
	if (!bs || !bs->sfjRoute)
		return;
	BotSFJ_DebugReject(bs, reason);
	bs->sfjRoute = 0;
	bs->sfjRouteNode = 0;
}

/*
 * Corridor from a recorded human route: the next landing node is the bot's
 * next goal.  A bot picks up a route standing at its start when the route
 * heads the way it is moving and ends closer to its goal.
 */
static qboolean BotSFJ_GetTrackRouteCorridor(bot_state_t *bs, const playerState_t *ps,
	const vec3_t moveDirection, vec3_t direction, vec3_t destination)
{
	const qboolean grounded = ps->groundEntityNum != ENTITYNUM_NONE;
	const bot_sfj_track_route_t *route;
	vec3_t toNode;
	int i;

	BotSFJ_LoadRouteHints();
	if (!g_botSfjTrackRouteCount)
		return qfalse;
	if (bs->sfjRoute > g_botSfjTrackRouteCount)
		bs->sfjRoute = 0;
	if (!bs->sfjRoute)
	{
		float myGoalDistance;
		int best = -1;
		float bestStartDistance = BOT_SFJ_TRACK_ROUTE_START_RADIUS + 1.0f;

		if (!grounded)
			return qfalse;
		myGoalDistance = Distance(bs->origin, bs->goalPosition);
		for (i = 0; i < g_botSfjTrackRouteCount; i++)
		{
			const bot_sfj_track_route_t *candidate = &g_botSfjTrackRoutes[i];
			vec3_t delta;
			float startDistance;

			VectorSubtract(candidate->start, ps->origin, delta);
			if (fabsf(delta[2]) > BOT_SFJ_TRACK_ROUTE_HEIGHT_TOLERANCE)
				continue;
			delta[2] = 0.0f;
			startDistance = VectorLength(delta);
			if (startDistance > BOT_SFJ_TRACK_ROUTE_START_RADIUS ||
				startDistance >= bestStartDistance)
				continue;
			VectorSubtract(candidate->node[0], ps->origin, toNode);
			toNode[2] = 0.0f;
			if (VectorNormalize(toNode) <= 0.0f || DotProduct(toNode, moveDirection) < BOT_SFJ_TRACK_ROUTE_MIN_ALIGNMENT)
				continue;
			/* The route must take the bot closer to where it is going. */
			if (Distance(candidate->node[candidate->nodeCount - 1], bs->goalPosition) >=
				myGoalDistance)
				continue;
			best = i;
			bestStartDistance = startDistance;
		}
		if (best < 0)
			return qfalse;
		bs->sfjRoute = best + 1;
		bs->sfjRouteNode = 0;
		bs->sfjRouteTime = level.time;
		g_botSfjStats.routeStarts++;
		if (bot_strafejumps_debug.integer > 1)
			BotSFJ_DebugReject(bs, va("start recorded route %i (%i landings)", best,
				g_botSfjTrackRoutes[best].nodeCount));
	}
	route = &g_botSfjTrackRoutes[bs->sfjRoute - 1];
	/* Advance past landings already reached (or passed along the segment). */
	while (bs->sfjRouteNode < route->nodeCount)
	{
		const float *node = route->node[bs->sfjRouteNode];
		const float *from = bs->sfjRouteNode > 0 ? route->node[bs->sfjRouteNode - 1] : route->start;
		vec3_t segment, toBot;
		float horizontal;

		VectorSubtract(node, ps->origin, toNode);
		horizontal = sqrtf(toNode[0] * toNode[0] + toNode[1] * toNode[1]);
		VectorSubtract(node, from, segment);
		VectorSubtract(ps->origin, node, toBot);
		segment[2] = toBot[2] = 0.0f;
		if (horizontal > BOT_SFJ_TRACK_NODE_REACHED && DotProduct(segment, toBot) <= 0.0f)
			break;
		bs->sfjRouteNode++;
		bs->sfjRouteTime = level.time;
	}
	if (bs->sfjRouteNode >= route->nodeCount)
	{
		BotSFJ_EndTrackRoute(bs, "recorded route complete, back to waypoints");
		return qfalse;
	}
	if (level.time - bs->sfjRouteTime > BOT_SFJ_TRACK_NODE_TIMEOUT_MS)
	{
		BotSFJ_EndTrackRoute(bs, "recorded route node not reached in time, back to waypoints");
		return qfalse;
	}
	VectorSubtract(route->node[bs->sfjRouteNode], ps->origin, toNode);
	toNode[2] = 0.0f;
	if (VectorNormalize(toNode) <= 0.0f)
		return qfalse;
	VectorCopy(toNode, direction);
	VectorCopy(route->node[bs->sfjRouteNode], destination);
	return qtrue;
}

/* Strafe key the human held on the hop toward the bot's next route node (0 = any). */
static int BotSFJ_TrackRouteSide(const bot_state_t *bs)
{
	const bot_sfj_track_route_t *route;

	if (!bs || bs->sfjRoute <= 0 || bs->sfjRoute > g_botSfjTrackRouteCount)
		return 0;
	route = &g_botSfjTrackRoutes[bs->sfjRoute - 1];
	if (bs->sfjRouteNode < 0 || bs->sfjRouteNode >= route->nodeCount)
		return 0;
	return route->nodeSide[bs->sfjRouteNode];
}

/*
 * Corridor from a .botroute hint the bot is standing on (or at the start of)
 * and moving along.  Takes priority over the waypoint corridor; the takeoff arc
 * is still validated like any other strafe jump.
 */
static qboolean BotSFJ_GetRouteHintCorridor(bot_state_t *bs, const playerState_t *ps,
	const vec3_t moveDirection, vec3_t direction, vec3_t destination)
{
	const qboolean grounded = ps->groundEntityNum != ENTITYNUM_NONE;
	const float horizontalSpeed = sqrtf(ps->velocity[0] * ps->velocity[0] +
		ps->velocity[1] * ps->velocity[1]);
	int i;

	BotSFJ_LoadRouteHints();
	for (i = 0; i < g_botSfjRouteHintCount; i++)
	{
		const bot_sfj_route_hint_t *hint = &g_botSfjRouteHints[i];
		vec3_t toEnd;
		float progress;

		if (!BotSFJ_RouteHintProgress(ps->origin, hint->start, hint->end,
				BOT_SFJ_ROUTE_HINT_START_RADIUS, BOT_SFJ_ROUTE_HINT_HALF_WIDTH,
				BOT_SFJ_ROUTE_HINT_HEIGHT_TOLERANCE, BOT_SFJ_ROUTE_HINT_MAX_PROGRESS,
				&progress))
			continue;
		VectorSubtract(hint->end, ps->origin, toEnd);
		toEnd[2] = 0.0f;
		if (VectorNormalize(toEnd) <= 0.0f || DotProduct(toEnd, moveDirection) < 0.7f)
			continue;
		if (!BotSFJ_RouteHintSpeedAllows(progress, grounded, horizontalSpeed, hint->minSpeed))
		{
			BotSFJ_DebugReject(bs, "route hint: below min_speed past the speed gate");
			continue;
		}
		VectorCopy(toEnd, direction);
		VectorCopy(hint->end, destination);
		return qtrue;
	}
	return qfalse;
}

/*
 * Cheap per-frame hazard look-ahead while airborne: one hull trace along the
 * predicted velocity.  Liquid, nodrop or a kill volume coming up ends the strafe
 * cleanly; walls are allowed (see BotSFJ_SlowedOut).
 */
static qboolean BotSFJ_HazardAhead(bot_state_t *bs, const playerState_t *ps)
{
	static vec3_t playerMins = {-15.0f, -15.0f, DEFAULT_MINS_2};
	static vec3_t playerMaxs = {15.0f, 15.0f, DEFAULT_MAXS_2};
	const float lookSeconds = 0.2f;
	vec3_t end;
	trace_t trace;

	if (!bs || !ps)
		return qfalse;
	end[0] = ps->origin[0] + ps->velocity[0] * lookSeconds;
	end[1] = ps->origin[1] + ps->velocity[1] * lookSeconds;
	end[2] = ps->origin[2] + ps->velocity[2] * lookSeconds -
		0.5f * (float)ps->gravity * lookSeconds * lookSeconds;
	JP_Trace(&trace, (float *)ps->origin, playerMins, playerMaxs, end, bs->client,
		MASK_PLAYERSOLID | CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP,
		qfalse, 0, 0);
	if (trace.startsolid || trace.allsolid)
		return qfalse;
	/* Plain walls are not hazards; the live speed check ends a strafe a wall slowed out. */
	if (trace.fraction < 1.0f &&
		(trace.contents & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP)))
		return qtrue;
	return BotNav_SweepTouchesInstantKillTrigger(ps->origin, trace.endpos);
}

static qboolean BotSFJ_TriggerAlongSegment(bot_state_t *bs, const vec3_t start,
	const vec3_t end)
{
	if (!bs)
		return qfalse;
	return BotNav_SweepTouchesInstantKillTrigger(start, end);
}

static qboolean BotSFJ_GetCommandTiming(const playerState_t *ps, int commandTime,
	int fallbackMsec, int *commandMsec, int *sliceMsec)
{
	int msec;

	if (!ps || !commandMsec || !sliceMsec)
		return qfalse;
	msec = commandTime - ps->commandTime;
	if (msec < 1)
		msec = fallbackMsec;
	if (msec < 1 || msec > 100)
		return qfalse;
	/*
	 * JKA race movement bypasses the ordinary 66 ms chopping path.  Long race
	 * commands are excluded rather than approximated as multiple slices.
	 */
	if (ps->stats[STAT_RACEMODE] && msec > 66)
		return qfalse;
	*commandMsec = msec;
	*sliceMsec = ps->stats[STAT_RACEMODE] ? msec :
		BotSFJ_PmoveSliceMsec(msec, pmove_fixed.integer, pmove_msec.integer, 0);
	return qtrue;
}

static qboolean BotSFJ_ArcIsSafe(bot_state_t *bs, const playerState_t *ps,
	const vec3_t routeDirection, const vec3_t routeDestination,
	int commandMsec, int sliceMsec)
{
	/* Real 15u player hull: walls the path only touches are handled as contacts below. */
	static vec3_t playerMins = {-15.0f, -15.0f, DEFAULT_MINS_2};
	static vec3_t playerMaxs = {15.0f, 15.0f, DEFAULT_MAXS_2};
	const int traceMask = MASK_PLAYERSOLID | CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP;
	vec3_t position, velocity, next, wishDirection;
	vec3_t traveled;
	trace_t trace;
	float routeYaw;
	float gravity;
	float elapsed = 0.0f;
	float previousVertical;
	float launchStartZ;
	int heldRemaining;
	int steps;
	int side;
	int wallContacts = 0;
	qboolean slowedByWall = qfalse;
	float slowedAt = 0.0f;
	const qboolean launched = ps && ps->groundEntityNum != ENTITYNUM_NONE;
	qboolean forceEligible = qfalse;
	qboolean predictedLanding = qfalse;

	if (!bs || !ps || ps->gravity <= 0 || ps->speed <= 0 ||
		commandMsec < 1 || sliceMsec < 1)
		return qfalse;
	{
		const float horizontalSpeed = sqrtf(ps->velocity[0] * ps->velocity[0] +
			ps->velocity[1] * ps->velocity[1]);
		if (horizontalSpeed > 1200.0f)
			return qfalse;
	}
	VectorCopy(ps->origin, position);
	VectorCopy(ps->velocity, velocity);
	launchStartZ = ps->origin[2];
	heldRemaining = launched ? commandMsec : 0;
	if (launched)
	{
		vec3_t groundEnd;

		if (ps->groundEntityNum != ENTITYNUM_WORLD)
			return qfalse;
		VectorCopy(position, groundEnd);
		groundEnd[2] -= 4.0f;
		JP_Trace(&trace, position, playerMins, playerMaxs, groundEnd,
			bs->client, traceMask, qfalse, 0, 0);
		if (trace.startsolid || trace.allsolid || trace.fraction >= 1.0f ||
			trace.entityNum != ENTITYNUM_WORLD || trace.plane.normal[2] < 0.9f)
			return qfalse;
		velocity[2] = BOT_SFJ_JUMP_VELOCITY;
		forceEligible = ps->fd.forcePowerLevel[FP_LEVITATION] > FORCE_LEVEL_0 &&
			ps->fd.forcePowerLevel[FP_LEVITATION] <= FORCE_LEVEL_3 &&
			BG_CanUseFPNow(level.gametype, (playerState_t *)ps, level.time,
				FP_LEVITATION);
	}
	gravity = (float)ps->gravity;
	routeYaw = vectoyaw(routeDirection);
	side = bs->sfjStrafeSide ? bs->sfjStrafeSide : 1;

	for (steps = 0; steps < BOT_SFJ_MAX_ARC_STEPS && elapsed < 2.0f; steps++)
	{
		/* Coarse 50 ms steps once the jump command has been applied. */
		int msec = (heldRemaining > 0 || sliceMsec >= 50) ? sliceMsec : 50;
		float stepSeconds;
		float commandYaw;
		float radians;
		float currentSpeed;
		float addSpeed;
		float accelSpeed;
		int contents;

		if (heldRemaining > 0 && msec > heldRemaining)
			msec = heldRemaining;
		if (elapsed + (float)msec / 1000.0f > 2.0f)
			msec = (int)((2.0f - elapsed) * 1000.0f);
		if (msec < 1)
			break;
		stepSeconds = (float)msec / 1000.0f;
		commandYaw = BotSFJ_CommandYaw(routeYaw, velocity[0], velocity[1],
			(float)ps->speed, pm_airaccelerate, stepSeconds, side);
		radians = commandYaw * 0.017453292519943295f;
		VectorSet(wishDirection, cosf(radians), sinf(radians), 0.0f);
		currentSpeed = DotProduct(velocity, wishDirection);
		addSpeed = (float)ps->speed - currentSpeed;
		/* Once a wall has slowed the strafe out, navigation takes over: no more air strafing. */
		if (addSpeed > 0.0f && !slowedByWall)
		{
			accelSpeed = pm_airaccelerate * stepSeconds * (float)ps->speed;
			if (accelSpeed > addSpeed)
				accelSpeed = addSpeed;
			VectorMA(velocity, accelSpeed, wishDirection, velocity);
		}

		if (heldRemaining > 0)
		{
			const float launchHeight = position[2] - launchStartZ;
			if (forceEligible && (launchHeight <= 32.0f || ps->fd.forcePower > 0))
				velocity[2] = BotSFJ_JKALaunchVelocity(
					ps->fd.forcePowerLevel[FP_LEVITATION], launchHeight, 1);
			else if (velocity[2] > BOT_SFJ_JUMP_VELOCITY)
				velocity[2] = BOT_SFJ_JUMP_VELOCITY;
			heldRemaining -= msec;
		}
		previousVertical = velocity[2];
		velocity[2] -= gravity * stepSeconds;
		VectorMA(position, stepSeconds, velocity, next);
		next[2] = position[2] +
			(previousVertical + velocity[2]) * 0.5f * stepSeconds;
		JP_Trace(&trace, position, playerMins, playerMaxs, next,
			bs->client, traceMask, qfalse, 0, 0);
		if (BotSFJ_TriggerAlongSegment(bs, position, trace.endpos))
			return qfalse;
		if (trace.startsolid || trace.allsolid)
			return qfalse;
		if (trace.fraction < 1.0f)
		{
			if (velocity[2] < 0.0f &&
				(!launched || elapsed + stepSeconds >= 0.25f) &&
				trace.plane.normal[2] >= 0.7f &&
				!(trace.contents & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP)) &&
				trace.entityNum == ENTITYNUM_WORLD)
			{
				VectorCopy(trace.endpos, position);
				predictedLanding = qtrue;
				break;
			}
			if (wallContacts < BOT_SFJ_MAX_WALL_CONTACTS &&
				trace.entityNum == ENTITYNUM_WORLD &&
				!(trace.contents & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP)) &&
				trace.plane.normal[2] < 0.7f && trace.plane.normal[2] >= -0.1f)
			{
				/*
				 * Direct wall contact is allowed: clip like PM_ClipVelocity and keep
				 * going. If the contact drops horizontal speed to ground speed or
				 * below, the strafe ends there and the bot only has to come down safely.
				 */
				const float backoff = DotProduct(velocity, trace.plane.normal) * 1.001f;

				VectorMA(velocity, -backoff, trace.plane.normal, velocity);
				VectorCopy(trace.endpos, position);
				elapsed += stepSeconds;
				wallContacts++;
				if (!BotSFJ_WallContactKeepsStrafe(sqrtf(velocity[0] * velocity[0] +
						velocity[1] * velocity[1]), (float)ps->speed) && !slowedByWall)
				{
					slowedByWall = qtrue;
					slowedAt = elapsed;
				}
				continue;
			}
			return qfalse;
		}
		if (trace.entityNum != ENTITYNUM_NONE && trace.entityNum != ENTITYNUM_WORLD)
			return qfalse;
		contents = trap->PointContents(next, bs->client);
		if (contents & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_NODROP))
			return qfalse;
		VectorCopy(next, position);
		elapsed += stepSeconds;
	}

	if (!predictedLanding)
		return qfalse;

	/* Slowed out by a wall: a safe landing is enough, the corridor no longer applies. */
	if (slowedByWall)
		return slowedAt >= BOT_SFJ_MIN_USEFUL_FLIGHT_S ? qtrue : qfalse;
	VectorSubtract(trace.endpos, ps->origin, traveled);
	traveled[2] = 0.0f;
	if (VectorLength(traveled) > 1.0f &&
		!BotSFJ_RouteSafetyAllows(qtrue, qtrue, qtrue, trace.plane.normal[2],
			DotProduct(traveled, routeDirection) / VectorLength(traveled)))
		return qfalse;
	if (!BotSFJ_LandingWithinCorridor(ps->origin[0], ps->origin[1],
		routeDestination[0], routeDestination[1], trace.endpos[0], trace.endpos[1],
		64.0f, 96.0f))
		return qfalse;
	return qtrue;
}

#define BOT_SFJ_ARC_CACHE_MS 100

/*
 * The arc simulation is the expensive part of the controller, so the result is
 * cached per bot.  It runs at most once per BOT_SFJ_ARC_CACHE_MS while the bot is
 * deciding whether to jump, plus once at each takeoff/rejump; while airborne the
 * cheap BotSFJ_HazardAhead look-ahead is used instead.
 */
static qboolean BotSFJ_CachedArcIsSafe(bot_state_t *bs, const playerState_t *ps,
	const vec3_t routeDirection, const vec3_t routeDestination,
	int commandMsec, int sliceMsec, int time, qboolean forceRefresh)
{
	if (!forceRefresh && bs->sfjArcCheckedTime > 0 &&
		time >= bs->sfjArcCheckedTime &&
		time - bs->sfjArcCheckedTime < BOT_SFJ_ARC_CACHE_MS)
		return bs->sfjArcSafe;
	bs->sfjArcSafe = BotSFJ_ArcIsSafe(bs, ps, routeDirection, routeDestination,
		commandMsec, sliceMsec);
	bs->sfjArcCheckedTime = time;
	return bs->sfjArcSafe;
}

static float BotSFJ_MinStrafeDistance(void)
{
	return bot_minstrafe.value > 0.0f ? bot_minstrafe.value : 0.0f;
}

static qboolean BotSFJ_ClientCarriesFlag(const gclient_t *client)
{
	return client && (client->ps.powerups[PW_REDFLAG] ||
		client->ps.powerups[PW_BLUEFLAG] || client->ps.powerups[PW_NEUTRALFLAG]);
}

static void BotSFJ_UpdatePursuit(bot_state_t *bs)
{
	const float startDistance = BotSFJ_MinStrafeDistance();

	if (!bs->currentEnemy || !bs->currentEnemy->client)
	{
		bs->sfjPursuitLatched = qfalse;
		bs->sfjLastEnemyDistance = 0.0f;
		bs->sfjLastEnemyDistanceTime = 0;
		bs->sfjLastEnemyTargetNum = -1;
		return;
	}
	if (bs->sfjLastEnemyTargetNum != bs->currentEnemy->s.number)
	{
		bs->sfjPursuitLatched = qfalse;
		bs->sfjLastEnemyDistance = bs->frame_Enemy_Len;
		bs->sfjLastEnemyDistanceTime = level.time;
		bs->sfjLastEnemyTargetNum = bs->currentEnemy->s.number;
		return;
	}
	if (bs->sfjLastEnemyDistanceTime > 0)
	{
		bs->sfjPursuitLatched = BotSFJ_UpdatePursuitLatchEx(bs->sfjPursuitLatched,
			bs->frame_Enemy_Len, bs->sfjLastEnemyDistance,
			level.time - bs->sfjLastEnemyDistanceTime, startDistance,
			BotSFJ_StopDistanceFor(startDistance)) ? qtrue : qfalse;
	}
	bs->sfjLastEnemyDistance = bs->frame_Enemy_Len;
	bs->sfjLastEnemyDistanceTime = level.time;
}

static void BotSFJ_Reject(bot_state_t *bs, const char *reason)
{
	BotSFJ_DebugReject(bs, reason);
	if (bs->sfjPhase != BOT_SFJ_PHASE_OFF && bs->sfjPhase != BOT_SFJ_PHASE_ABORT)
		BotSFJ_Abort(bs, level.time);
}

static void BotSFJ_SelectIntent(bot_state_t *bs)
{
	bot_input_t queued;
	playerState_t *ps;
	gclient_t *enemyClient = NULL;
	vec3_t routeDirection, routeDestination, effectiveDirection, toEnemy;
	bot_sfj_intent_t purpose = BOT_SFJ_INTENT_NONE;
	const float minStrafe = BotSFJ_MinStrafeDistance();
	float enemyDistance = 0.0f;
	qboolean carryingFlag;
	qboolean enemyCarriesFlag = qfalse;
	qboolean retreating;
	qboolean haveRoute;
	qboolean airborneJump;
	bot_sfj_conflict_t conflict;
	const char *conflictReason = NULL;
	int commandMsec;
	int sliceMsec;
	int fallbackMsec;

	if (!bs)
		return;
	if (!bot_strafejumps.integer || bot_strafejumpfrequency.integer <= 0)
	{
		BotSFJ_DebugReject(bs, "strafe initiation disabled");
		BotSFJ_Clear(bs);
		return;
	}
	ps = &g_entities[bs->client].client->ps;
	BotSFJ_UpdatePursuit(bs);
	trap->EA_GetInput(bs->client, (float)level.time / 1000.0f, &queued);
	/*
	 * Queued navigation jump/walk requests are replaced by the controller's own
	 * jump timing once it owns input (see BotSFJ_ApplyInput), so they should not
	 * block initiation either; the arc check still has to clear the path.
	 */
	queued.actionflags &= ~(ACTION_JUMP | ACTION_DELAYEDJUMP | ACTION_WALK);
	conflict = BotSFJ_GetInputConflict(bs, ps, &queued, &conflictReason);
	if (BotSFJ_ConflictBlocks(conflict, bs->sfjPhase))
	{
		BotSFJ_Reject(bs, va("input/state conflict: %s", conflictReason));
		return;
	}
	if (!BotSFJ_EffectiveInputDirection(&queued, effectiveDirection))
	{
		BotSFJ_Reject(bs, "no horizontal movement input");
		return;
	}

	carryingFlag = BotSFJ_ClientCarriesFlag(g_entities[bs->client].client);
	VectorClear(toEnemy);
	if (bs->currentEnemy && bs->currentEnemy->client)
	{
		enemyClient = bs->currentEnemy->client;
		VectorSubtract(enemyClient->ps.origin, ps->origin, toEnemy);
		toEnemy[2] = 0.0f;
		enemyDistance = VectorNormalize(toEnemy);
		if (enemyDistance <= 0.0f)
		{
			BotSFJ_Reject(bs, "enemy overlapping");
			return;
		}
		enemyCarriesFlag = BotSFJ_ClientCarriesFlag(enemyClient);
	}
	/* Carrying or returning a flag counts as escape intent in CTF. */
	retreating = (bs->combatAction == BOT_COMBAT_ACTION_RETREAT_DEFENSE ||
		bs->runningLikeASissy || bs->runningToEscapeThreat || carryingFlag) ? qtrue : qfalse;

	bs->sfjCorridorValid = qfalse;
	/* Fights take priority over recorded routes. */
	if (bs->sfjRoute && enemyClient && bs->frame_Enemy_Vis &&
		enemyDistance < BOT_SFJ_NAV_ENEMY_GUARD && !carryingFlag)
		BotSFJ_EndTrackRoute(bs, "enemy close, leaving recorded route");
	haveRoute = (!enemyClient || !bs->frame_Enemy_Vis || enemyDistance >= BOT_SFJ_NAV_ENEMY_GUARD ||
		carryingFlag) ? BotSFJ_GetTrackRouteCorridor(bs, ps, effectiveDirection,
			routeDirection, routeDestination) : qfalse;
	if (!haveRoute)
		haveRoute = BotSFJ_GetRouteHintCorridor(bs, ps, effectiveDirection,
			routeDirection, routeDestination);
	if (!haveRoute)
	{
		haveRoute = BotSFJ_GetOpenCorridor(bs, routeDirection, routeDestination);
		if (haveRoute && DotProduct(effectiveDirection, routeDirection) < 0.5f)
		{
			haveRoute = qfalse;
			bs->sfjCorridorValid = qfalse;
		}
	}
	if (!haveRoute && enemyClient &&
		(retreating || bs->sfjPursuitLatched || enemyCarriesFlag ||
		 (bot_onlystrafes.integer && enemyDistance >= minStrafe)))
	{
		haveRoute = BotSFJ_GetEnemyCorridor(bs, effectiveDirection, toEnemy,
			enemyDistance, retreating, routeDirection, routeDestination);
	}
	if (!haveRoute)
	{
		BotSFJ_Reject(bs, "no straight open corridor along movement");
		return;
	}

	if (enemyClient)
	{
		const float alignment = DotProduct(routeDirection, toEnemy);
		const float stopDistance = BotSFJ_StopDistanceFor(minStrafe);

		if (retreating && alignment < -0.25f && enemyDistance >= minStrafe)
			purpose = BOT_SFJ_INTENT_RETREAT;
		else if ((bs->sfjPursuitLatched ||
				(bot_onlystrafes.integer && enemyDistance >= minStrafe) ||
				(enemyCarriesFlag && enemyDistance >= minStrafe)) &&
			alignment > 0.5f && enemyDistance > stopDistance)
			purpose = BOT_SFJ_INTENT_PURSUIT;
		else if (bs->frame_Enemy_Vis &&
			(enemyDistance < BOT_SFJ_NAV_ENEMY_GUARD ||
			 (enemyDistance < BOT_SFJ_CORRIDOR_LOOKAHEAD &&
			  NewBotAI_GetEnemyClosingSpeed(bs) > BOT_SFJ_NAV_ENEMY_CLOSING_SPEED)))
		{
			BotSFJ_Reject(bs, retreating ?
				"enemy closer than bot_minstrafe or not behind" :
				"visible enemy in close-combat range or closing fast");
			return;
		}
	}
	if (purpose == BOT_SFJ_INTENT_NONE)
		purpose = BOT_SFJ_INTENT_NAVIGATION;
	if (bs->sfjPhase == BOT_SFJ_PHASE_OFF || bs->sfjPhase == BOT_SFJ_PHASE_ABORT ||
		bs->sfjPhase == BOT_SFJ_PHASE_PREPARE || bs->sfjPhase == BOT_SFJ_PHASE_LANDING)
	{
		bs->sfjStrafeSide = BotSFJ_SelectSide(vectoyaw(routeDirection),
			ps->velocity[0], ps->velocity[1], bs->sfjStrafeSide ?
			bs->sfjStrafeSide : ((bs->client & 1) ? -1 : 1));
		/* Recorded routes replay the human's strafe key for this hop. */
		{
			const int recordedSide = BotSFJ_TrackRouteSide(bs);

			if (recordedSide)
				bs->sfjStrafeSide = recordedSide;
		}
	}
	fallbackMsec = sv_fps.integer > 0 ? 1000 / sv_fps.integer : 25;
	if (!BotSFJ_GetCommandTiming(ps, level.time, fallbackMsec,
			&commandMsec, &sliceMsec))
	{
		BotSFJ_Reject(bs, "unsupported command timing");
		return;
	}
	/* Mid-jump the takeoff arc was already validated; only watch for hazards. */
	airborneJump = (bs->sfjPhase == BOT_SFJ_PHASE_TAKEOFF ||
		bs->sfjPhase == BOT_SFJ_PHASE_REJUMP ||
		bs->sfjPhase == BOT_SFJ_PHASE_AIR) && ps->groundEntityNum == ENTITYNUM_NONE;
	if (airborneJump ? BotSFJ_HazardAhead(bs, ps) :
		!BotSFJ_CachedArcIsSafe(bs, ps, routeDirection, routeDestination,
			commandMsec, sliceMsec, level.time, qfalse))
	{
		BotSFJ_Reject(bs, airborneJump ? "hazard ahead in flight" :
			"predicted jump arc unsafe (wall, drop, hazard or lands off route)");
		return;
	}

	bs->sfjIntent = purpose;
	VectorCopy(routeDirection, bs->sfjIntentDirection);
	VectorCopy(routeDestination, bs->sfjIntentDestination);
	bs->sfjIntentTime = level.time;
	bs->sfjSafetyUntil = level.time + BOT_SFJ_INTENT_MAX_AGE_MS;
	if ((bs->sfjPhase == BOT_SFJ_PHASE_OFF ||
		(bs->sfjPhase == BOT_SFJ_PHASE_ABORT && bs->sfjCooldownUntil <= level.time)) &&
		ps->groundEntityNum != ENTITYNUM_NONE)
	{
		/* Recorded routes start right away; the frequency gate is for improvised strafes. */
		if (!bot_onlystrafes.integer && !bs->sfjRoute)
		{
			if (!bs->sfjNextStartTime ||
				bs->sfjStartFrequency != bot_strafejumpfrequency.integer ||
				bs->sfjNextStartTime - level.time > 100000)
			{
				bs->sfjStartFrequency = bot_strafejumpfrequency.integer;
				bs->sfjNextStartTime = level.time +
					BotSFJ_StartIntervalMs(bot_strafejumpfrequency.integer);
			}
			if (bs->sfjNextStartTime > level.time)
			{
				BotSFJ_DebugReject(bs, "waiting for strafe initiation interval");
				return;
			}
		}
		bs->sfjNextStartTime = 0;
		bs->sfjPhase = BOT_SFJ_PHASE_PREPARE;
		bs->sfjPhaseTime = level.time;
		bs->sfjPeakSpeed = 0.0f;
		g_botSfjStats.starts++;
		if (bot_strafejumps_debug.integer > 1)
		{
			BotSFJ_DebugReject(bs, purpose == BOT_SFJ_INTENT_RETREAT ? "start escape" :
				purpose == BOT_SFJ_INTENT_PURSUIT ? "start chase" : "start navigation");
		}
	}
}

static void BotSFJ_ApplyInput(bot_state_t *bs, bot_input_t *bi, int time, int elapsedTime)
{
	bot_input_t candidate;
	bot_input_t masked;
	playerState_t *ps;
	bot_sfj_phase_t nextPhase;
	bot_sfj_conflict_t conflict;
	const char *conflictReason = NULL;
	qboolean grounded;
	qboolean fresh;
	qboolean eligible;
	vec3_t effectiveDirection;
	float routeYaw;
	float commandYaw;
	float horizontalSpeed;
	qboolean circleJump;
	int commandMsec;
	int sliceMsec;

	if (!bs || !bi)
		return;
	if (!bot_strafejumps.integer || bot_strafejumpfrequency.integer <= 0)
	{
		BotSFJ_Clear(bs);
		return;
	}
	if (!g_entities[bs->client].client)
	{
		BotSFJ_Abort(bs, time);
		return;
	}
	ps = &g_entities[bs->client].client->ps;
	grounded = ps->groundEntityNum != ENTITYNUM_NONE ? qtrue : qfalse;
	fresh = (BotSFJ_IntentIsFresh(time, bs->sfjIntentTime) &&
		bs->sfjSafetyUntil >= time) ? qtrue : qfalse;
	/*
	 * The controller owns jump timing once a strafe jump is underway, so
	 * navigation jump/crouch/walk requests and vertical trail input are
	 * masked out before checking eligibility.
	 */
	masked = *bi;
	if (bs->sfjPhase != BOT_SFJ_PHASE_OFF && bs->sfjPhase != BOT_SFJ_PHASE_ABORT)
	{
		masked.actionflags &= ~(ACTION_JUMP | ACTION_DELAYEDJUMP | ACTION_CROUCH |
			ACTION_WALK);
		masked.dir[2] = 0.0f;
	}
	conflict = BotSFJ_GetInputConflict(bs, ps, &masked, &conflictReason);
	eligible = BotSFJ_ConflictBlocks(conflict, bs->sfjPhase) ? qfalse : qtrue;
	if (eligible && (!BotSFJ_EffectiveInputDirection(&masked, effectiveDirection) ||
		DotProduct(effectiveDirection, bs->sfjIntentDirection) < 0.5f))
	{
		eligible = qfalse;
		conflictReason = "movement misaligned with corridor";
	}
	if (eligible && bs->currentEnemy && bs->currentEnemy->client)
	{
		vec3_t liveSeparation;
		float separation;
		VectorSubtract(bs->currentEnemy->client->ps.origin, ps->origin, liveSeparation);
		liveSeparation[2] = 0.0f;
		separation = VectorLength(liveSeparation);
		if (bs->sfjIntent == BOT_SFJ_INTENT_NAVIGATION)
		{
			/* Hand control back to combat when an enemy gets into saber range. */
			if (bs->frame_Enemy_Vis && separation < BOT_SFJ_NAV_ENEMY_GUARD * 0.5f &&
				!BotSFJ_ClientCarriesFlag(g_entities[bs->client].client))
			{
				eligible = qfalse;
				conflictReason = "visible enemy in saber range";
			}
		}
		else if (separation < BotSFJ_StopDistanceFor(BotSFJ_MinStrafeDistance()))
		{
			eligible = qfalse;
			conflictReason = "enemy inside stop distance";
		}
	}
	if (!BotSFJ_CanOwnInput(bot_strafejumps.integer, fresh, eligible, bs->sfjPhase))
	{
		if (bs->sfjPhase != BOT_SFJ_PHASE_OFF && bs->sfjPhase != BOT_SFJ_PHASE_ABORT)
		{
			BotSFJ_DebugReject(bs, !fresh ? "final input: expired corridor intent" :
				va("final input: %s", conflictReason ? conflictReason : "not eligible"));
			BotSFJ_Abort(bs, time);
		}
		bs->sfjOwnsInput = qfalse;
		return;
	}
	if (((bs->sfjPhase == BOT_SFJ_PHASE_PREPARE ||
		bs->sfjPhase == BOT_SFJ_PHASE_TAKEOFF ||
		bs->sfjPhase == BOT_SFJ_PHASE_LANDING ||
		bs->sfjPhase == BOT_SFJ_PHASE_REJUMP) &&
		time - bs->sfjPhaseTime > 200) ||
		(bs->sfjPhase == BOT_SFJ_PHASE_AIR && time - bs->sfjPhaseTime > 1500))
	{
		BotSFJ_Abort(bs, time);
		return;
	}

	horizontalSpeed = sqrtf(ps->velocity[0] * ps->velocity[0] +
		ps->velocity[1] * ps->velocity[1]);
	if (horizontalSpeed > bs->sfjPeakSpeed)
		bs->sfjPeakSpeed = horizontalSpeed;
	/* Walls may be touched; once one slows the bot to ground speed, navigate normally. */
	if ((bs->sfjPhase == BOT_SFJ_PHASE_AIR || bs->sfjPhase == BOT_SFJ_PHASE_LANDING ||
			bs->sfjPhase == BOT_SFJ_PHASE_REJUMP) &&
		BotSFJ_SlowedOut(bs->sfjPeakSpeed, horizontalSpeed, (float)ps->speed))
	{
		BotSFJ_DebugReject(bs, "slowed to ground speed (wall contact), resuming navigation");
		BotSFJ_Release(bs, time);
		return;
	}
	/*
	 * Circle-jump start: from a slow standing start, spend a few ground frames
	 * swinging the view across the route before the first takeoff so the jump
	 * leaves the ground with extra speed.
	 */
	circleJump = (bs->sfjPhase == BOT_SFJ_PHASE_PREPARE && grounded && fresh &&
		eligible && horizontalSpeed < 250.0f && time - bs->sfjPhaseTime < 120) ?
		qtrue : qfalse;
	nextPhase = circleJump ? BOT_SFJ_PHASE_PREPARE :
		BotSFJ_NextPhase(bs->sfjPhase, qtrue, fresh, eligible, grounded,
			time - bs->sfjPhaseTime);
	if (!BotSFJ_GetCommandTiming(ps, time, elapsedTime, &commandMsec, &sliceMsec))
	{
		BotSFJ_DebugReject(bs, "unsupported command timing");
		BotSFJ_Abort(bs, time);
		return;
	}
	if ((nextPhase == BOT_SFJ_PHASE_TAKEOFF || nextPhase == BOT_SFJ_PHASE_REJUMP) &&
		nextPhase != bs->sfjPhase)
	{
		/* One full arc check per takeoff (reusing a very recent check). */
		if (!BotSFJ_CachedArcIsSafe(bs, ps, bs->sfjIntentDirection,
				bs->sfjIntentDestination, commandMsec, sliceMsec, time, qfalse))
		{
			BotSFJ_DebugReject(bs, "takeoff arc unsafe");
			BotSFJ_Abort(bs, time);
			return;
		}
	}
	else if (nextPhase == BOT_SFJ_PHASE_AIR && !grounded && BotSFJ_HazardAhead(bs, ps))
	{
		BotSFJ_DebugReject(bs, "hazard ahead in flight, releasing");
		BotSFJ_Abort(bs, time);
		return;
	}
	if (nextPhase != bs->sfjPhase)
	{
		if (nextPhase == BOT_SFJ_PHASE_TAKEOFF)
			g_botSfjStats.takeoffs++;
		bs->sfjPhase = nextPhase;
		bs->sfjPhaseTime = time;
		if (nextPhase == BOT_SFJ_PHASE_LANDING)
			bs->sfjStrafeSide = BotSFJ_SelectSide(vectoyaw(bs->sfjIntentDirection),
				ps->velocity[0], ps->velocity[1], -bs->sfjStrafeSide);
	}
	if (bs->sfjPhase == BOT_SFJ_PHASE_ABORT || bs->sfjPhase == BOT_SFJ_PHASE_OFF)
	{
		bs->sfjOwnsInput = qfalse;
		return;
	}

	routeYaw = vectoyaw(bs->sfjIntentDirection);
	commandYaw = BotSFJ_CommandYaw(routeYaw, ps->velocity[0], ps->velocity[1],
		(float)ps->speed, pm_airaccelerate,
		(float)sliceMsec / 1000.0f,
		bs->sfjStrafeSide);
	if (circleJump)
	{
		const float swing = 30.0f * (1.0f - (float)(time - bs->sfjPhaseTime) / 120.0f);
		commandYaw = AngleNormalize360(routeYaw -
			(bs->sfjStrafeSide < 0 ? -swing : swing));
	}
	candidate = *bi;
	candidate.viewangles[YAW] = commandYaw;
	VectorClear(candidate.dir);
	candidate.speed = 400.0f;
	candidate.actionflags &= ~(ACTION_MOVEFORWARD | ACTION_MOVEBACK | ACTION_MOVELEFT |
		ACTION_MOVERIGHT | ACTION_CROUCH | ACTION_JUMP | ACTION_DELAYEDJUMP | ACTION_WALK);
	candidate.actionflags |= ACTION_MOVEFORWARD;
	if (BotSFJ_JumpPressed(bs->sfjPhase, grounded))
		candidate.actionflags |= ACTION_JUMP;
	if (!BotSFJ_EffectiveInputDirection(&candidate, effectiveDirection) ||
		DotProduct(effectiveDirection, bs->sfjIntentDirection) < 0.25f)
	{
		BotSFJ_Abort(bs, time);
		return;
	}
	*bi = candidate;
	bs->sfjOwnsInput = qtrue;
	bs->viewangles[YAW] = commandYaw;
	bs->ideal_viewangles[YAW] = commandYaw;
}

//check of the potential enemy is a valid one
int PassStandardEnemyChecks(bot_state_t *bs, gentity_t *en)
{
	if (!bs || !en)
	{ //shouldn't happen
		return 0;
	}

	if (!en->client)
	{ //not a client, don't care about him
		return 0;
	}

	if (en->client->sess.raceMode)
		return 0;

	if (en->client->ps.pm_type == PM_NOCLIP) 
		return 0;

	if (!BotTargetModeAllowsBotEnemies(BotGetNewBotAITargetMode()) && (en->r.svFlags & SVF_BOT))
		return 0;

	if (en->health < 1)
	{ //he's already dead
		return 0;
	}

	if (!en->takedamage)
	{ //a client that can't take damage?
		return 0;
	}

	if (bs->doingFallback &&
		(gLevelFlags & LEVELFLAG_IGNOREINFALLBACK))
	{ //we screwed up in our nav routines somewhere and we've reverted to a fallback state to
		//try to get back on the trail. If the level specifies to ignore enemies in this state,
		//then ignore them.
		return 0;
	}

	if (en->client->ps.pm_type == PM_INTERMISSION ||
		en->client->ps.pm_type == PM_SPECTATOR ||
		en->client->sess.sessionTeam == TEAM_SPECTATOR)
	{ //don't attack spectators
		return 0;
	}

	if (!en->client->pers.connected)
	{ //a "zombie" client?
		return 0;
	}

	if (!en->s.solid)
	{ //shouldn't happen
		return 0;
	}

	if (bs->client == en->s.number)
	{ //don't attack yourself
		return 0;
	}

	if (OnSameTeam(&g_entities[bs->client], en))
	{ //don't attack teammates
		return 0;
	}

	if (BotMindTricked(bs->client, en->s.number))
	{
		if (bs->currentEnemy && bs->currentEnemy->s.number == en->s.number)
		{ //if mindtricked by this enemy, then be less "aware" of them, even though
			//we know they're there.
			vec3_t vs;
			float vLen = 0;

			VectorSubtract(bs->origin, en->client->ps.origin, vs);
			vLen = VectorLength(vs);

			if (vLen > 64 /*&& (level.time - en->client->dangerTime) > 150*/)
			{
				return 0;
			}
		}
	}

	if (en->client->ps.duelInProgress && en->client->ps.duelIndex != bs->client)
	{ //don't attack duelists unless you're dueling them
		return 0;
	}

	if (bs->cur_ps.duelInProgress && en->s.number != bs->cur_ps.duelIndex)
	{ //ditto, the other way around
		return 0;
	}

	if (level.gametype == GT_JEDIMASTER && !en->client->ps.isJediMaster && !bs->cur_ps.isJediMaster)
	{ //rules for attacking non-JM in JM mode
		vec3_t vs;
		float vLen = 0;

		if (!g_friendlyFire.value)
		{ //can't harm non-JM in JM mode if FF is off
			return 0;
		}

		VectorSubtract(bs->origin, en->client->ps.origin, vs);
		vLen = VectorLength(vs);

		if (vLen > 350)
		{
			return 0;
		}
	}

	return 1;
}

//Notifies the bot that he has taken damage from "attacker".
void BotDamageNotification(gclient_t *bot, gentity_t *attacker)
{
	bot_state_t *bs;
	bot_state_t *bs_a;
	int i;

	if (!bot || !attacker || !attacker->client)
	{
		return;
	}

	if (bot->ps.clientNum >= MAX_CLIENTS)
	{ //an NPC.. do nothing for them.
		return;
	}

	if (attacker->s.number >= MAX_CLIENTS)
	{ //if attacker is an npc also don't care I suppose.
		return;
	}

	bs_a = botstates[attacker->s.number];

	if (bs_a)
	{ //if the client attacking us is a bot as well
		bs_a->lastAttacked = &g_entities[bot->ps.clientNum];
		i = 0;

		while (i < MAX_CLIENTS)
		{
			if (botstates[i] &&
				i != bs_a->client &&
				botstates[i]->lastAttacked == &g_entities[bot->ps.clientNum])
			{
				botstates[i]->lastAttacked = NULL;
			}

			i++;
		}
	}
	else //got attacked by a real client, so no one gets rights to lastAttacked
	{
		i = 0;

		while (i < MAX_CLIENTS)
		{
			if (botstates[i] &&
				botstates[i]->lastAttacked == &g_entities[bot->ps.clientNum])
			{
				botstates[i]->lastAttacked = NULL;
			}

			i++;
		}
	}

	bs = botstates[bot->ps.clientNum];

	if (!bs)
	{
		return;
	}

	bs->lastHurt = attacker;
	bs->lastHurtTime = level.time;

	if (bs->currentEnemy)
	{ //we don't care about the guy attacking us if we have an enemy already
		return;
	}

	if (!PassStandardEnemyChecks(bs, attacker))
	{ //the person that hurt us is not a valid enemy
		return;
	}

	if (PassLovedOneCheck(bs, attacker))
	{ //the person that hurt us is the one we love!
		bs->currentEnemy = attacker;
		bs->enemySeenTime = level.time + ENEMY_FORGET_MS;
	}
}

//perform cheap "hearing" checks based on the event catching
//system
int BotCanHear(bot_state_t *bs, gentity_t *en, float endist)
{
	float minlen;

	if (!en || !en->client)
	{
		return 0;
	}

	if (en && en->client && en->client->ps.otherSoundTime > level.time)
	{ //they made a noise in recent time
		minlen = en->client->ps.otherSoundLen;
		goto checkStep;
	}

	if (en && en->client && en->client->ps.footstepTime > level.time)
	{ //they made a footstep
		minlen = 256;
		goto checkStep;
	}

	if (gBotEventTracker[en->s.number].eventTime < level.time)
	{ //no recent events to check
		return 0;
	}

	switch(gBotEventTracker[en->s.number].events[gBotEventTracker[en->s.number].eventSequence & (MAX_PS_EVENTS-1)])
	{ //did the last event contain a sound?
	case EV_GLOBAL_SOUND:
		minlen = 256;
		break;
	case EV_FIRE_WEAPON:
	case EV_ALT_FIRE:
	case EV_SABER_ATTACK:
		minlen = 512;
		break;
	case EV_STEP_4:
	case EV_STEP_8:
	case EV_STEP_12:
	case EV_STEP_16:
	case EV_FOOTSTEP:
	case EV_FOOTSTEP_METAL:
	case EV_FOOTWADE:
		minlen = 256;
		break;
	case EV_JUMP:
	case EV_ROLL:
		minlen = 256;
		break;
	default:
		minlen = 999999;
		break;
	}
checkStep:
	if (BotMindTricked(bs->client, en->s.number))
	{ //if mindtricked by this person, cut down on the minlen so they can't "hear" as well
		minlen /= 4;
	}

	if (endist <= minlen)
	{ //we heard it
		return 1;
	}

	return 0;
}

//check for new events
void UpdateEventTracker(void)
{
	int i;

	i = 0;

	while (i < MAX_CLIENTS)
	{
		if (gBotEventTracker[i].eventSequence != level.clients[i].ps.eventSequence)
		{ //updated event
			gBotEventTracker[i].eventSequence = level.clients[i].ps.eventSequence;
			gBotEventTracker[i].events[0] = level.clients[i].ps.events[0];
			gBotEventTracker[i].events[1] = level.clients[i].ps.events[1];
			gBotEventTracker[i].eventTime = level.time + 0.5;
		}

		i++;
	}
}

//check if said angles are within our fov
int InFieldOfVision(vec3_t viewangles, float fov, vec3_t angles)
{
	int i;
	float diff, angle;

	for (i = 0; i < 2; i++)
	{
		angle = AngleMod(viewangles[i]);
		angles[i] = AngleMod(angles[i]);
		diff = angles[i] - angle;
		if (angles[i] > angle)
		{
			if (diff > 180.0)
			{
				diff -= 360.0;
			}
		}
		else
		{
			if (diff < -180.0)
			{
				diff += 360.0;
			}
		}
		if (diff > 0)
		{
			if (diff > fov * 0.5)
			{
				return 0;
			}
		}
		else
		{
			if (diff < -fov * 0.5)
			{
				return 0;
			}
		}
	}
	return 1;
}

//We cannot hurt the ones we love. Unless of course this
//function says we can.
int PassLovedOneCheck(bot_state_t *bs, gentity_t *ent)
{
	int i;
	bot_state_t *loved;

	if (!bs->lovednum)
	{
		return 1;
	}

	if (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
	{ //There is no love in 1-on-1
		return 1;
	}

	i = 0;

	if (!botstates[ent->s.number])
	{ //not a bot
		return 1;
	}

	if (!bot_attachments.integer)
	{
		return 1;
	}

	loved = botstates[ent->s.number];

	while (i < bs->lovednum)
	{
		if (strcmp(level.clients[loved->client].pers.netname, bs->loved[i].name) == 0)
		{
			if (!IsTeamplay() && bs->loved[i].level < 2)
			{ //if FFA and level of love is not greater than 1, just don't care
				return 1;
			}
			else if (IsTeamplay() && !OnSameTeam(&g_entities[bs->client], &g_entities[loved->client]) && bs->loved[i].level < 2)
			{ //is teamplay, but not on same team and level < 2
				return 1;
			}
			else
			{
				return 0;
			}
		}

		i++;
	}

	return 1;
}

qboolean G_ThereIsAMaster(void);

//standard check to find a new enemy.
int ScanForEnemies(bot_state_t* bs)
{
	vec3_t a;
	float distcheck;
	float closest;
	int bestindex;
	int i;
	float hasEnemyDist = 0;
	qboolean noAttackNonJM = qfalse;

	closest = 999999;
	i = 0;
	bestindex = -1;

	if (bs->currentEnemy)
	{ //only switch to a new enemy if he's significantly closer
		hasEnemyDist = bs->frame_Enemy_Len;
	}

	while (i <= MAX_CLIENTS)
	{
		if (i != bs->client && g_entities[i].client && !OnSameTeam(&g_entities[bs->client], &g_entities[i]) && PassStandardEnemyChecks(bs, &g_entities[i]) && BotPVSCheck(g_entities[i].client->ps.origin, bs->eye) && PassLovedOneCheck(bs, &g_entities[i]))
		{
			VectorSubtract(g_entities[i].client->ps.origin, bs->eye, a);
			distcheck = VectorLength(a);
			vectoangles(a, a);
			/*
						if (g_entities[i].client->ps.isJediMaster)
						{ //make us think the Jedi Master is close so we'll attack him above all
							distcheck = 1;
						}
			*/
			if (distcheck < closest && ((InFieldOfVision(bs->viewangles, 90, a) && !BotMindTricked(bs->client, i)) || BotCanHear(bs, &g_entities[i], distcheck)) && OrgVisible(bs->eye, g_entities[i].client->ps.origin, -1))
			{
				if (BotMindTricked(bs->client, i))
				{
					if (distcheck < 256 || (level.time - g_entities[i].client->dangerTime) < 100)
					{
						if (!hasEnemyDist || distcheck < (hasEnemyDist - 128))
						{ //if we have an enemy, only switch to closer if he is 128+ closer to avoid flipping out
							if (!noAttackNonJM)	//|| g_entities[i].client->ps.isJediMaster)
							{
								closest = distcheck;
								bestindex = i;
							}
						}
					}
				}
				else
				{
					if (!hasEnemyDist || distcheck < (hasEnemyDist - 128))
					{ //if we have an enemy, only switch to closer if he is 128+ closer to avoid flipping out
						if (!noAttackNonJM)	//|| g_entities[i].client->ps.isJediMaster)
						{
							closest = distcheck;
							bestindex = i;
						}
					}
				}
			}
		}
		i++;
	}

	return bestindex;
}

int WaitingForNow(bot_state_t *bs, vec3_t goalpos)
{ //checks if the bot is doing something along the lines of waiting for an elevator to raise up
	(void)bs;
	(void)goalpos;
	return 0;
}

//get an ideal distance for us to be at in relation to our opponent
//based on our weapon.
int BotGetWeaponRange(bot_state_t *bs)
{
	switch (bs->cur_ps.weapon)
	{
	case WP_STUN_BATON:
	case WP_MELEE:
		return BWEAPONRANGE_MELEE;
	case WP_SABER:
		return BWEAPONRANGE_SABER;
	case WP_BRYAR_PISTOL:
		return BWEAPONRANGE_MID;
	case WP_BLASTER:
		return BWEAPONRANGE_MID;
	case WP_DISRUPTOR:
		return BWEAPONRANGE_LONG;
	case WP_BOWCASTER:
		return BWEAPONRANGE_LONG;
	case WP_REPEATER:
		return BWEAPONRANGE_MID;
	case WP_DEMP2:
		return BWEAPONRANGE_LONG;
	case WP_FLECHETTE:
		return BWEAPONRANGE_MID;
	case WP_ROCKET_LAUNCHER:
		return BWEAPONRANGE_LONG;
	case WP_THERMAL:
		return BWEAPONRANGE_LONG;
	case WP_TRIP_MINE:
		return BWEAPONRANGE_LONG;
	case WP_DET_PACK:
		return BWEAPONRANGE_LONG;
	default:
		return BWEAPONRANGE_MID;
	}
}

//see if we want to run away from the opponent for whatever reason
int BotIsAChickenWuss(bot_state_t *bs)
{
	int bWRange;

	if (gLevelFlags & LEVELFLAG_IMUSTNTRUNAWAY)
	{ //The level says we mustn't run away!
		return 0;
	}

	if (level.gametype == GT_SINGLE_PLAYER)
	{ //"coop" (not really)
		return 0;
	}

	if (level.gametype == GT_JEDIMASTER && !bs->cur_ps.isJediMaster)
	{ //Then you may know no fear.
		//Well, unless he's strong.
		if (bs->currentEnemy && bs->currentEnemy->client &&
			bs->currentEnemy->client->ps.isJediMaster &&
			bs->currentEnemy->health > 40 &&
			bs->cur_ps.weapon < WP_ROCKET_LAUNCHER)
		{ //explosive weapons are most effective against the Jedi Master
			goto jmPass;
		}
		return 0;
	}

	if (level.gametype == GT_CTF && bs->currentEnemy && bs->currentEnemy->client)
	{
		if (bs->currentEnemy->client->ps.powerups[PW_REDFLAG] ||
			bs->currentEnemy->client->ps.powerups[PW_BLUEFLAG])
		{ //don't be afraid of flag carriers, they must die!
			return 0;
		}
	}

jmPass:
	if (bs->chickenWussCalculationTime > level.time)
	{
		return 2; //don't want to keep going between two points...
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_RAGE))
	{ //don't run while raging
		return 0;
	}

	if (level.gametype == GT_JEDIMASTER && !bs->cur_ps.isJediMaster)
	{ //be frightened of the jedi master? I guess in this case.
		return 1;
	}

	bs->chickenWussCalculationTime = level.time + MAX_CHICKENWUSS_TIME;

	if (g_entities[bs->client].health < BOT_RUN_HEALTH)
	{ //we're low on health, let's get away
		return 1;
	}

	bWRange = BotGetWeaponRange(bs);

	if (bWRange == BWEAPONRANGE_MELEE || bWRange == BWEAPONRANGE_SABER)
	{
		if (bWRange != BWEAPONRANGE_SABER || !bs->saberSpecialist)
		{ //run away if we're using melee, or if we're using a saber and not a "saber specialist"
			return 1;
		}
	}

	if (bs->cur_ps.weapon == WP_BRYAR_PISTOL)
	{ //the bryar is a weak weapon, so just try to find a new one if it's what you're having to use
		return 1;
	}

	if (bs->currentEnemy && bs->currentEnemy->client &&
		bs->currentEnemy->client->ps.weapon == WP_SABER &&
		bs->frame_Enemy_Len < 512 && bs->cur_ps.weapon != WP_SABER)
	{ //if close to an enemy with a saber and not using a saber, then try to back off
		return 1;
	}

	if ((level.time-bs->cur_ps.electrifyTime) < 16000)
	{ //lightning is dangerous.
		return 1;
	}

	//didn't run, reset the timer
	bs->chickenWussCalculationTime = 0;

	return 0;
}

//look for "bad things". bad things include detpacks, thermal detonators,
//and other dangerous explodey items.
gentity_t *GetNearestBadThing(bot_state_t *bs)
{
	int i = 0;
	float glen;
	vec3_t hold;
	int bestindex = 0;
	float bestdist = 800; //if not within a radius of 800, it's no threat anyway
	int foundindex = 0;
	float factor = 0;
	gentity_t *ent;
	trace_t tr;

	while (i < level.num_entities)
	{
		ent = &g_entities[i];

		if ( (ent &&
			!ent->client &&
			ent->inuse &&
			ent->damage &&
			/*(ent->s.weapon == WP_THERMAL || ent->s.weapon == WP_FLECHETTE)*/
			ent->s.weapon &&
			ent->splashDamage) ||
			(ent &&
			ent->genericValue5 == 1000 &&
			ent->inuse &&
			ent->health > 0 &&
			ent->genericValue3 != bs->client &&
			g_entities[ent->genericValue3].client && !OnSameTeam(&g_entities[bs->client], &g_entities[ent->genericValue3])) )
		{ //try to escape from anything with a non-0 s.weapon and non-0 damage. This hopefully only means dangerous projectiles.
		  //Or a sentry gun if bolt_Head == 1000. This is a terrible hack, yes.
			VectorSubtract(bs->origin, ent->r.currentOrigin, hold);
			glen = VectorLength(hold);

			if (ent->s.weapon != WP_THERMAL && ent->s.weapon != WP_FLECHETTE &&
				ent->s.weapon != WP_DET_PACK && ent->s.weapon != WP_TRIP_MINE)
			{
				factor = 0.5;

				if (ent->s.weapon && glen <= 256 && bs->settings.skill > 2)
				{ //it's a projectile so push it away
					bs->doForcePush = level.time + 700;
					//trap->Print("PUSH PROJECTILE\n");
				}
			}
			else
			{
				factor = 1;
			}

			if (ent->s.weapon == WP_ROCKET_LAUNCHER &&
				(ent->r.ownerNum == bs->client ||
				(ent->r.ownerNum > 0 && ent->r.ownerNum < MAX_CLIENTS &&
				g_entities[ent->r.ownerNum].client && OnSameTeam(&g_entities[bs->client], &g_entities[ent->r.ownerNum]))) )
			{ //don't be afraid of your own rockets or your teammates' rockets
				factor = 0;
			}

			if (ent->s.weapon == WP_DET_PACK &&
				(ent->r.ownerNum == bs->client ||
				(ent->r.ownerNum > 0 && ent->r.ownerNum < MAX_CLIENTS &&
				g_entities[ent->r.ownerNum].client && OnSameTeam(&g_entities[bs->client], &g_entities[ent->r.ownerNum]))) )
			{ //don't be afraid of your own detpacks or your teammates' detpacks
				factor = 0;
			}

			if (ent->s.weapon == WP_TRIP_MINE &&
				(ent->r.ownerNum == bs->client ||
				(ent->r.ownerNum > 0 && ent->r.ownerNum < MAX_CLIENTS &&
				g_entities[ent->r.ownerNum].client && OnSameTeam(&g_entities[bs->client], &g_entities[ent->r.ownerNum]))) )
			{ //don't be afraid of your own trip mines or your teammates' trip mines
				factor = 0;
			}

			if (ent->s.weapon == WP_THERMAL &&
				(ent->r.ownerNum == bs->client ||
				(ent->r.ownerNum > 0 && ent->r.ownerNum < MAX_CLIENTS &&
				g_entities[ent->r.ownerNum].client && OnSameTeam(&g_entities[bs->client], &g_entities[ent->r.ownerNum]))) )
			{ //don't be afraid of your own thermals or your teammates' thermals
				factor = 0;
			}

			if (glen < bestdist*factor && BotPVSCheck(bs->origin, ent->s.pos.trBase))
			{
				JP_Trace(&tr, bs->origin, NULL, NULL, ent->s.pos.trBase, bs->client, MASK_SOLID, qfalse, 0, 0);

				if (tr.fraction == 1 || tr.entityNum == ent->s.number)
				{
					bestindex = i;
					bestdist = glen;
					foundindex = 1;
				}
			}
		}

		if (ent && !ent->client && ent->inuse && ent->damage && ent->s.weapon && ent->r.ownerNum < MAX_CLIENTS && ent->r.ownerNum >= 0)
		{ //if we're in danger of a projectile belonging to someone and don't have an enemy, set the enemy to them
			gentity_t *projOwner = &g_entities[ent->r.ownerNum];

			if (projOwner && projOwner->inuse && projOwner->client)
			{
				if (!bs->currentEnemy)
				{
					if (PassStandardEnemyChecks(bs, projOwner))
					{
						if (PassLovedOneCheck(bs, projOwner))
						{
							VectorSubtract(bs->origin, ent->r.currentOrigin, hold);
							glen = VectorLength(hold);

							if (glen < 512)
							{
								bs->currentEnemy = projOwner;
								bs->enemySeenTime = level.time + ENEMY_FORGET_MS;
							}
						}
					}
				}
			}
		}

		i++;
	}

	if (foundindex)
	{
		bs->dontGoBack = level.time + 1500;
		return &g_entities[bestindex];
	}
	else
	{
		return NULL;
	}
}

//Keep our CTF priorities on defending our team's flag
int BotDefendFlag(bot_state_t *bs)
{
	wpobject_t *flagPoint;
	vec3_t a;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		flagPoint = flagRed;
	}
	else if (level.clients[bs->client].sess.sessionTeam == TEAM_BLUE)
	{
		flagPoint = flagBlue;
	}
	else
	{
		return 0;
	}

	if (!flagPoint)
	{
		return 0;
	}

	VectorSubtract(bs->origin, flagPoint->origin, a);

	if (VectorLength(a) > BASE_GUARD_DISTANCE)
	{
		bs->wpDestination = flagPoint;
	}

	return 1;
}

//Keep our CTF priorities on getting the other team's flag
int BotGetEnemyFlag(bot_state_t *bs)
{
	wpobject_t *flagPoint;
	vec3_t a;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		flagPoint = flagBlue;
	}
	else if (level.clients[bs->client].sess.sessionTeam == TEAM_BLUE)
	{
		flagPoint = flagRed;
	}
	else
	{
		return 0;
	}

	if (!flagPoint)
	{
		return 0;
	}

	VectorSubtract(bs->origin, flagPoint->origin, a);

	if (VectorLength(a) > BASE_GETENEMYFLAG_DISTANCE)
	{
		bs->wpDestination = flagPoint;
	}

	return 1;
}

//Our team's flag is gone, so try to get it back
int BotGetFlagBack(bot_state_t *bs)
{
	int i = 0;
	int myFlag = 0;
	int foundCarrier = 0;
	int tempInt = 0;
	gentity_t *ent = NULL;
	vec3_t usethisvec;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		myFlag = PW_REDFLAG;
	}
	else
	{
		myFlag = PW_BLUEFLAG;
	}

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && ent->client->ps.powerups[myFlag] && !OnSameTeam(&g_entities[bs->client], ent))
		{
			foundCarrier = 1;
			break;
		}

		i++;
	}

	if (!foundCarrier)
	{
		return 0;
	}

	if (!ent)
	{
		return 0;
	}

	if (bs->wpDestSwitchTime < level.time)
	{
		if (ent->client)
		{
			VectorCopy(ent->client->ps.origin, usethisvec);
		}
		else
		{
			VectorCopy(ent->s.origin, usethisvec);
		}

		tempInt = GetNearestVisibleWP(usethisvec, 0);

		if (tempInt != -1 && TotalTrailDistance(bs->wpCurrent->index, tempInt, bs) != -1)
		{
			bs->wpDestination = gWPArray[tempInt];
			bs->wpDestSwitchTime = level.time + Q_irand(1000, 5000);
		}
	}

	return 1;
}

//Someone else on our team has the enemy flag, so try to get
//to their assistance
int BotGuardFlagCarrier(bot_state_t *bs)
{
	int i = 0;
	int enemyFlag = 0;
	int foundCarrier = 0;
	int tempInt = 0;
	gentity_t *ent = NULL;
	vec3_t usethisvec;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		enemyFlag = PW_BLUEFLAG;
	}
	else
	{
		enemyFlag = PW_REDFLAG;
	}

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && ent->client->ps.powerups[enemyFlag] && OnSameTeam(&g_entities[bs->client], ent))
		{
			foundCarrier = 1;
			break;
		}

		i++;
	}

	if (!foundCarrier)
	{
		return 0;
	}

	if (!ent)
	{
		return 0;
	}

	if (bs->wpDestSwitchTime < level.time)
	{
		if (ent->client)
		{
			VectorCopy(ent->client->ps.origin, usethisvec);
		}
		else
		{
			VectorCopy(ent->s.origin, usethisvec);
		}

		tempInt = GetNearestVisibleWP(usethisvec, 0);

		if (tempInt != -1 && TotalTrailDistance(bs->wpCurrent->index, tempInt, bs) != -1)
		{
			bs->wpDestination = gWPArray[tempInt];
			bs->wpDestSwitchTime = level.time + Q_irand(1000, 5000);
		}
	}

	return 1;
}

//We have the flag, let's get it home.
int BotGetFlagHome(bot_state_t *bs)
{
	wpobject_t *flagPoint;
	vec3_t a;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		flagPoint = flagRed;
	}
	else if (level.clients[bs->client].sess.sessionTeam == TEAM_BLUE)
	{
		flagPoint = flagBlue;
	}
	else
	{
		return 0;
	}

	if (!flagPoint)
	{
		return 0;
	}

	VectorSubtract(bs->origin, flagPoint->origin, a);

	if (VectorLength(a) > BASE_FLAGWAIT_DISTANCE)
	{
		bs->wpDestination = flagPoint;
	}

	return 1;
}

void GetNewFlagPoint(wpobject_t *wp, gentity_t *flagEnt, int team)
{ //get the nearest possible waypoint to the flag since it's not in its original position
	int i = 0;
	vec3_t a, mins, maxs;
	float bestdist;
	float testdist;
	int bestindex = 0;
	int foundindex = 0;
	trace_t tr;

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = -5;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 5;

	VectorSubtract(wp->origin, flagEnt->s.pos.trBase, a);

	bestdist = VectorLength(a);

	if (bestdist <= WP_KEEP_FLAG_DIST)
	{
		JP_Trace(&tr, wp->origin, mins, maxs, flagEnt->s.pos.trBase, flagEnt->s.number, MASK_SOLID, qfalse, 0, 0);

		if (tr.fraction == 1)
		{ //this point is good
			return;
		}
	}

	while (i < gWPNum)
	{
		VectorSubtract(gWPArray[i]->origin, flagEnt->s.pos.trBase, a);
		testdist = VectorLength(a);

		if (testdist < bestdist)
		{
			JP_Trace(&tr, gWPArray[i]->origin, mins, maxs, flagEnt->s.pos.trBase, flagEnt->s.number, MASK_SOLID, qfalse, 0, 0);

			if (tr.fraction == 1)
			{
				foundindex = 1;
				bestindex = i;
				bestdist = testdist;
			}
		}

		i++;
	}

	if (foundindex)
	{
		if (team == TEAM_RED)
		{
			flagRed = gWPArray[bestindex];
		}
		else
		{
			flagBlue = gWPArray[bestindex];
		}
	}
}

//See if our CTF state should take priority in our nav routines
int CTFTakesPriority(bot_state_t *bs)
{
	gentity_t *ent = NULL;
	int enemyFlag = 0;
	int myFlag = 0;
	int enemyHasOurFlag = 0;
	//int weHaveEnemyFlag = 0;
	int numOnMyTeam = 0;
	//int numOnEnemyTeam = 0;
	int numAttackers = 0;
	//int numDefenders = 0;
	int i = 0;
	int idleWP;
	int dosw = 0;
	wpobject_t *dest_sw = NULL;
#ifdef BOT_CTF_DEBUG
	vec3_t t;

	trap->Print("CTFSTATE: %s\n", ctfStateNames[bs->ctfState]);
#endif

	if (level.gametype != GT_CTF && level.gametype != GT_CTY)
	{
		return 0;
	}

	if (bs->cur_ps.weapon == WP_BRYAR_PISTOL &&
		(level.time - bs->lastDeadTime) < BOT_MAX_WEAPON_GATHER_TIME)
	{ //get the nearest weapon laying around base before heading off for battle
		idleWP = GetBestIdleGoal(bs);

		if (idleWP != -1 && gWPArray[idleWP] && gWPArray[idleWP]->inuse)
		{
			if (bs->wpDestSwitchTime < level.time)
			{
				bs->wpDestination = gWPArray[idleWP];
			}
			return 1;
		}
	}
	else if (bs->cur_ps.weapon == WP_BRYAR_PISTOL &&
		(level.time - bs->lastDeadTime) < BOT_MAX_WEAPON_CHASE_CTF &&
		bs->wpDestination && bs->wpDestination->weight)
	{
		dest_sw = bs->wpDestination;
		dosw = 1;
	}

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		myFlag = PW_REDFLAG;
	}
	else
	{
		myFlag = PW_BLUEFLAG;
	}

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		enemyFlag = PW_BLUEFLAG;
	}
	else
	{
		enemyFlag = PW_REDFLAG;
	}

	if (!flagRed || !flagBlue ||
		!flagRed->inuse || !flagBlue->inuse ||
		!eFlagRed || !eFlagBlue)
	{
		return 0;
	}

#ifdef BOT_CTF_DEBUG
	VectorCopy(flagRed->origin, t);
	t[2] += 128;
	G_TestLine(flagRed->origin, t, 0x0000ff, 500);

	VectorCopy(flagBlue->origin, t);
	t[2] += 128;
	G_TestLine(flagBlue->origin, t, 0x0000ff, 500);
#endif

	if (droppedRedFlag && (droppedRedFlag->flags & FL_DROPPED_ITEM))
	{
		GetNewFlagPoint(flagRed, droppedRedFlag, TEAM_RED);
	}
	else
	{
		flagRed = oFlagRed;
	}

	if (droppedBlueFlag && (droppedBlueFlag->flags & FL_DROPPED_ITEM))
	{
		GetNewFlagPoint(flagBlue, droppedBlueFlag, TEAM_BLUE);
	}
	else
	{
		flagBlue = oFlagBlue;
	}

	if (!bs->ctfState)
	{
		return 0;
	}

	i = 0;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client)
		{
			/*if (ent->client->ps.powerups[enemyFlag] && OnSameTeam(&g_entities[bs->client], ent))
			{
				weHaveEnemyFlag = 1;
			}
			else */if (ent->client->ps.powerups[myFlag] && !OnSameTeam(&g_entities[bs->client], ent))
			{
				enemyHasOurFlag = 1;
			}

			if (OnSameTeam(&g_entities[bs->client], ent))
			{
				numOnMyTeam++;
			}
			else
			{
				//numOnEnemyTeam++;
			}

			if (botstates[ent->s.number])
			{
				if (botstates[ent->s.number]->ctfState == CTFSTATE_ATTACKER ||
					botstates[ent->s.number]->ctfState == CTFSTATE_RETRIEVAL)
				{
					numAttackers++;
				}
				else
				{
					//numDefenders++;
				}
			}
			else
			{ //assume real players to be attackers in our logic
				numAttackers++;
			}
		}
		i++;
	}

	if (bs->cur_ps.powerups[enemyFlag])
	{
		if ((numOnMyTeam < 2 || !numAttackers) && enemyHasOurFlag)
		{
			bs->ctfState = CTFSTATE_RETRIEVAL;
		}
		else
		{
			bs->ctfState = CTFSTATE_GETFLAGHOME;
		}
	}
	else if (bs->ctfState == CTFSTATE_GETFLAGHOME)
	{
		bs->ctfState = 0;
	}

	if (bs->state_Forced)
	{
		bs->ctfState = bs->state_Forced;
	}

	if (bs->ctfState == CTFSTATE_DEFENDER)
	{
		if (BotDefendFlag(bs))
		{
			goto success;
		}
	}

	if (bs->ctfState == CTFSTATE_ATTACKER)
	{
		if (BotGetEnemyFlag(bs))
		{
			goto success;
		}
	}

	if (bs->ctfState == CTFSTATE_RETRIEVAL)
	{
		if (BotGetFlagBack(bs))
		{
			goto success;
		}
		else
		{ //can't find anyone on another team being a carrier, so ignore this priority
			bs->ctfState = 0;
		}
	}

	if (bs->ctfState == CTFSTATE_GUARDCARRIER)
	{
		if (BotGuardFlagCarrier(bs))
		{
			goto success;
		}
		else
		{ //can't find anyone on our team being a carrier, so ignore this priority
			bs->ctfState = 0;
		}
	}

	if (bs->ctfState == CTFSTATE_GETFLAGHOME)
	{
		if (BotGetFlagHome(bs))
		{
			goto success;
		}
	}

	return 0;

success:
	if (dosw)
	{ //allow ctf code to run, but if after a particular item then keep going after it
		bs->wpDestination = dest_sw;
	}

	return 1;
}

int EntityVisibleBox(vec3_t org1, vec3_t mins, vec3_t maxs, vec3_t org2, int ignore, int ignore2)
{
	trace_t tr;

	JP_Trace(&tr, org1, mins, maxs, org2, ignore, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction == 1 && !tr.startsolid && !tr.allsolid)
	{
		return 1;
	}
	else if (tr.entityNum != ENTITYNUM_NONE && tr.entityNum == ignore2)
	{
		return 1;
	}

	return 0;
}

//Get the closest objective for siege and go after it
int Siege_TargetClosestObjective(bot_state_t *bs, int flag)
{
	int i = 0;
	int bestindex = -1;
	float testdistance = 0;
	float bestdistance = 999999999.9f;
	gentity_t *goalent;
	vec3_t a, dif;
	vec3_t mins, maxs;

	mins[0] = -1;
	mins[1] = -1;
	mins[2] = -1;

	maxs[0] = 1;
	maxs[1] = 1;
	maxs[2] = 1;

	if ( bs->wpDestination && (bs->wpDestination->flags & flag) && bs->wpDestination->associated_entity != ENTITYNUM_NONE &&
		 g_entities[bs->wpDestination->associated_entity].inuse && g_entities[bs->wpDestination->associated_entity].use )
	{
		goto hasPoint;
	}

	while (i < gWPNum)
	{
		if ( gWPArray[i] && gWPArray[i]->inuse && (gWPArray[i]->flags & flag) && gWPArray[i]->associated_entity != ENTITYNUM_NONE &&
			 g_entities[gWPArray[i]->associated_entity].inuse && g_entities[gWPArray[i]->associated_entity].use )
		{
			VectorSubtract(gWPArray[i]->origin, bs->origin, a);
			testdistance = VectorLength(a);

			if (testdistance < bestdistance)
			{
				bestdistance = testdistance;
				bestindex = i;
			}
		}

		i++;
	}

	if (bestindex != -1)
	{
		bs->wpDestination = gWPArray[bestindex];
	}
	else
	{
		return 0;
	}
hasPoint:
	goalent = &g_entities[bs->wpDestination->associated_entity];

	if (!goalent)
	{
		return 0;
	}

	VectorSubtract(bs->origin, bs->wpDestination->origin, a);

	testdistance = VectorLength(a);

	dif[0] = (goalent->r.absmax[0]+goalent->r.absmin[0])/2;
	dif[1] = (goalent->r.absmax[1]+goalent->r.absmin[1])/2;
	dif[2] = (goalent->r.absmax[2]+goalent->r.absmin[2])/2;
	//brush models can have tricky origins, so this is our hacky method of getting the center point

	if (goalent->takedamage && testdistance < BOT_MIN_SIEGE_GOAL_SHOOT &&
		EntityVisibleBox(bs->origin, mins, maxs, dif, bs->client, goalent->s.number))
	{
		bs->shootGoal = goalent;
		bs->touchGoal = NULL;
	}
	else if (goalent->use && testdistance < BOT_MIN_SIEGE_GOAL_TRAVEL)
	{
		bs->shootGoal = NULL;
		bs->touchGoal = goalent;
	}
	else
	{ //don't know how to handle this goal object!
		bs->shootGoal = NULL;
		bs->touchGoal = NULL;
	}

	if (BotGetWeaponRange(bs) == BWEAPONRANGE_MELEE ||
		BotGetWeaponRange(bs) == BWEAPONRANGE_SABER)
	{
		bs->shootGoal = NULL; //too risky
	}

	if (bs->touchGoal)
	{
		//trap->Print("Please, master, let me touch it!\n");
		VectorCopy(dif, bs->goalPosition);
	}

	return 1;
}

void Siege_DefendFromAttackers(bot_state_t *bs)
{ //this may be a little cheap, but the best way to find our defending point is probably
  //to just find the nearest person on the opposing team since they'll most likely
  //be on offense in this situation
	int wpClose = -1;
	int i = 0;
	float testdist = 999999;
	int bestindex = -1;
	float bestdist = 999999;
	gentity_t *ent;
	vec3_t a;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && ent->client->sess.sessionTeam != g_entities[bs->client].client->sess.sessionTeam &&
			ent->health > 0 && ent->client->sess.sessionTeam != TEAM_SPECTATOR)
		{
			VectorSubtract(ent->client->ps.origin, bs->origin, a);

			testdist = VectorLength(a);

			if (testdist < bestdist)
			{
				bestindex = i;
				bestdist = testdist;
			}
		}

		i++;
	}

	if (bestindex == -1)
	{
		return;
	}

	wpClose = GetNearestVisibleWP(g_entities[bestindex].client->ps.origin, -1);

	if (wpClose != -1 && gWPArray[wpClose] && gWPArray[wpClose]->inuse)
	{
		bs->wpDestination = gWPArray[wpClose];
		bs->destinationGrabTime = level.time + 10000;
	}
}

//how many defenders on our team?
int Siege_CountDefenders(bot_state_t *bs)
{
	int i = 0;
	int num = 0;
	gentity_t *ent;
	bot_state_t *bot;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];
		bot = botstates[i];

		if (ent && ent->client && bot)
		{
			if (bot->siegeState == SIEGESTATE_DEFENDER &&
				ent->client->sess.sessionTeam == g_entities[bs->client].client->sess.sessionTeam)
			{
				num++;
			}
		}

		i++;
	}

	return num;
}

//how many other players on our team?
int Siege_CountTeammates(bot_state_t *bs)
{
	int i = 0;
	int num = 0;
	gentity_t *ent;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client)
		{
			if (ent->client->sess.sessionTeam == g_entities[bs->client].client->sess.sessionTeam)
			{
				num++;
			}
		}

		i++;
	}

	return num;
}

//see if siege objective completion should take priority in our
//nav routines.
int SiegeTakesPriority(bot_state_t *bs)
{
	int attacker;
	//int flagForDefendableObjective;
	int flagForAttackableObjective;
	int defenders, teammates;
	int idleWP;
	wpobject_t *dest_sw = NULL;
	int dosw = 0;
	gclient_t *bcl;
	vec3_t dif;
	trace_t tr;

	if (level.gametype != GT_SIEGE)
	{
		return 0;
	}

	bcl = g_entities[bs->client].client;

	if (!bcl)
	{
		return 0;
	}

	if (bs->cur_ps.weapon == WP_BRYAR_PISTOL &&
		(level.time - bs->lastDeadTime) < BOT_MAX_WEAPON_GATHER_TIME)
	{ //get the nearest weapon laying around base before heading off for battle
		idleWP = GetBestIdleGoal(bs);

		if (idleWP != -1 && gWPArray[idleWP] && gWPArray[idleWP]->inuse)
		{
			if (bs->wpDestSwitchTime < level.time)
			{
				bs->wpDestination = gWPArray[idleWP];
			}
			return 1;
		}
	}
	else if (bs->cur_ps.weapon == WP_BRYAR_PISTOL &&
		(level.time - bs->lastDeadTime) < BOT_MAX_WEAPON_CHASE_TIME &&
		bs->wpDestination && bs->wpDestination->weight)
	{
		dest_sw = bs->wpDestination;
		dosw = 1;
	}

	if (bcl->sess.sessionTeam == SIEGETEAM_TEAM1)
	{
		attacker = imperial_attackers;
		//flagForDefendableObjective = WPFLAG_SIEGE_REBELOBJ;
		flagForAttackableObjective = WPFLAG_SIEGE_IMPERIALOBJ;
	}
	else
	{
		attacker = rebel_attackers;
		//flagForDefendableObjective = WPFLAG_SIEGE_IMPERIALOBJ;
		flagForAttackableObjective = WPFLAG_SIEGE_REBELOBJ;
	}

	if (attacker)
	{
		bs->siegeState = SIEGESTATE_ATTACKER;
	}
	else
	{
		bs->siegeState = SIEGESTATE_DEFENDER;
		defenders = Siege_CountDefenders(bs);
		teammates = Siege_CountTeammates(bs);

		if (defenders > teammates/3 && teammates > 1)
		{ //devote around 1/4 of our team to completing our own side goals even if we're a defender.
		  //If we have no side goals we will realize that later on and join the defenders
			bs->siegeState = SIEGESTATE_ATTACKER;
		}
	}

	if (bs->state_Forced)
	{
		bs->siegeState = bs->state_Forced;
	}

	if (bs->siegeState == SIEGESTATE_ATTACKER)
	{
		if (!Siege_TargetClosestObjective(bs, flagForAttackableObjective))
		{ //looks like we have no goals other than to keep the other team from completing objectives
			Siege_DefendFromAttackers(bs);
			if (bs->shootGoal)
			{
				dif[0] = (bs->shootGoal->r.absmax[0]+bs->shootGoal->r.absmin[0])/2;
				dif[1] = (bs->shootGoal->r.absmax[1]+bs->shootGoal->r.absmin[1])/2;
				dif[2] = (bs->shootGoal->r.absmax[2]+bs->shootGoal->r.absmin[2])/2;

				if (!BotPVSCheck(bs->origin, dif))
				{
					bs->shootGoal = NULL;
				}
				else
				{
					JP_Trace(&tr, bs->origin, NULL, NULL, dif, bs->client, MASK_SOLID, qfalse, 0, 0);

					if (tr.fraction != 1 && tr.entityNum != bs->shootGoal->s.number)
					{
						bs->shootGoal = NULL;
					}
				}
			}
		}
	}
	else if (bs->siegeState == SIEGESTATE_DEFENDER)
	{
		Siege_DefendFromAttackers(bs);
		if (bs->shootGoal)
		{
			dif[0] = (bs->shootGoal->r.absmax[0]+bs->shootGoal->r.absmin[0])/2;
			dif[1] = (bs->shootGoal->r.absmax[1]+bs->shootGoal->r.absmin[1])/2;
			dif[2] = (bs->shootGoal->r.absmax[2]+bs->shootGoal->r.absmin[2])/2;

			if (!BotPVSCheck(bs->origin, dif))
			{
				bs->shootGoal = NULL;
			}
			else
			{
				JP_Trace(&tr, bs->origin, NULL, NULL, dif, bs->client, MASK_SOLID, qfalse, 0, 0);

				if (tr.fraction != 1 && tr.entityNum != bs->shootGoal->s.number)
				{
					bs->shootGoal = NULL;
				}
			}
		}
	}
	else
	{ //get busy!
		Siege_TargetClosestObjective(bs, flagForAttackableObjective);
		if (bs->shootGoal)
		{
			dif[0] = (bs->shootGoal->r.absmax[0]+bs->shootGoal->r.absmin[0])/2;
			dif[1] = (bs->shootGoal->r.absmax[1]+bs->shootGoal->r.absmin[1])/2;
			dif[2] = (bs->shootGoal->r.absmax[2]+bs->shootGoal->r.absmin[2])/2;

			if (!BotPVSCheck(bs->origin, dif))
			{
				bs->shootGoal = NULL;
			}
			else
			{
				JP_Trace(&tr, bs->origin, NULL, NULL, dif, bs->client, MASK_SOLID, qfalse, 0, 0);

				if (tr.fraction != 1 && tr.entityNum != bs->shootGoal->s.number)
				{
					bs->shootGoal = NULL;
				}
			}
		}
	}

	if (dosw)
	{ //allow siege objective code to run, but if after a particular item then keep going after it
		bs->wpDestination = dest_sw;
	}

	return 1;
}

//see if jedi master priorities should take priority in our nav
//routines.
int JMTakesPriority(bot_state_t *bs)
{
	int i = 0;
	int wpClose = -1;
	gentity_t *theImportantEntity = NULL;

	if (level.gametype != GT_JEDIMASTER)
	{
		return 0;
	}

	if (bs->cur_ps.isJediMaster)
	{
		return 0;
	}

	//jmState becomes the index for the one who carries the saber. If jmState is -1 then the saber is currently
	//without an owner
	bs->jmState = -1;

	while (i < MAX_CLIENTS)
	{
		if (g_entities[i].client && g_entities[i].inuse &&
			g_entities[i].client->ps.isJediMaster)
		{
			bs->jmState = i;
			break;
		}

		i++;
	}

	if (bs->jmState != -1)
	{
		theImportantEntity = &g_entities[bs->jmState];
	}
	else
	{
		theImportantEntity = gJMSaberEnt;
	}

	if (theImportantEntity && theImportantEntity->inuse && bs->destinationGrabTime < level.time)
	{
		if (theImportantEntity->client)
		{
			wpClose = GetNearestVisibleWP(theImportantEntity->client->ps.origin, theImportantEntity->s.number);
		}
		else
		{
			wpClose = GetNearestVisibleWP(theImportantEntity->r.currentOrigin, theImportantEntity->s.number);
		}

		if (wpClose != -1 && gWPArray[wpClose] && gWPArray[wpClose]->inuse)
		{
			/*
			Com_Printf("BOT GRABBED IDEAL JM LOCATION\n");
			if (bs->wpDestination != gWPArray[wpClose])
			{
				Com_Printf("IDEAL WAS NOT ALREADY IDEAL\n");

				if (!bs->wpDestination)
				{
					Com_Printf("IDEAL WAS NULL\n");
				}
			}
			*/
			bs->wpDestination = gWPArray[wpClose];
			bs->destinationGrabTime = level.time + 4000;
		}
	}

	return 1;
}

//see if we already have an item/powerup/etc. that is associated
//with this waypoint.
int BotHasAssociated(bot_state_t *bs, wpobject_t *wp)
{
	gentity_t *as;

	if (wp->associated_entity == ENTITYNUM_NONE)
	{ //make it think this is an item we have so we don't go after nothing
		return 1;
	}

	as = &g_entities[wp->associated_entity];

	if (!as || !as->item)
	{
		return 0;
	}

	if (as->item->giType == IT_WEAPON)
	{
		if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << as->item->giTag))
		{
			return 1;
		}

		return 0;
	}
	else if (as->item->giType == IT_HOLDABLE)
	{
		if (bs->cur_ps.stats[STAT_HOLDABLE_ITEMS] & (1 << as->item->giTag))
		{
			return 1;
		}

		return 0;
	}
	else if (as->item->giType == IT_POWERUP)
	{
		if (bs->cur_ps.powerups[as->item->giTag])
		{
			return 1;
		}

		return 0;
	}
	else if (as->item->giType == IT_AMMO)
	{
		if (bs->cur_ps.ammo[as->item->giTag] > 10) //hack
		{
			return 1;
		}

		return 0;
	}

	return 0;
}

//we don't really have anything we want to do right now,
//let's just find the best thing to do given the current
//situation.
int GetBestIdleGoal(bot_state_t *bs)
{
	int i = 0;
	int highestweight = 0;
	int desiredindex = -1;
	int dist_to_weight = 0;
	int traildist;

	if (!bs->wpCurrent)
	{
		return -1;
	}

	if (bs->isCamper != 2)
	{
		if (bs->randomNavTime < level.time)
		{
			if (Q_irand(1, 10) < 5)
			{
				bs->randomNav = 1;
			}
			else
			{
				bs->randomNav = 0;
			}

			bs->randomNavTime = level.time + Q_irand(5000, 15000);
		}
	}

	if (bs->randomNav)
	{ //stop looking for items and/or camping on them
		return -1;
	}

	while (i < gWPNum)
	{
		if (gWPArray[i] &&
			gWPArray[i]->inuse &&
			(gWPArray[i]->flags & WPFLAG_GOALPOINT) &&
			gWPArray[i]->weight > highestweight &&
			!BotHasAssociated(bs, gWPArray[i]))
		{
			traildist = TotalTrailDistance(bs->wpCurrent->index, i, bs);

			if (traildist != -1)
			{
				dist_to_weight = (int)traildist/10000;
				dist_to_weight = (gWPArray[i]->weight)-dist_to_weight;

				if (dist_to_weight > highestweight)
				{
					highestweight = dist_to_weight;
					desiredindex = i;
				}
			}
		}

		i++;
	}

	return desiredindex;
}

//go through the list of possible priorities for navigating
//and work out the best destination point.
void GetIdealDestination(bot_state_t *bs)
{
	int tempInt, cWPIndex, bChicken, idleWP;
	float distChange, plusLen, minusLen;
	vec3_t usethisvec, a;
	gentity_t *badthing;

#ifdef _DEBUG
	trap->Cvar_Update(&bot_nogoals);

	if (bot_nogoals.integer)
	{
		return;
	}
#endif

	if (!bs->wpCurrent)
	{
		return;
	}

	if ((level.time - bs->escapeDirTime) > 4000)
	{
		badthing = GetNearestBadThing(bs);
	}
	else
	{
		badthing = NULL;
	}

	if (badthing && badthing->inuse &&
		badthing->health > 0 && badthing->takedamage)
	{
		bs->dangerousObject = badthing;
	}
	else
	{
		bs->dangerousObject = NULL;
	}

	if (!badthing && bs->dontGoBack > level.time)
	{
		if (bs->wpDestination)
		{
			bs->wpStoreDest = bs->wpDestination;
		}
		bs->wpDestination = NULL;
		return;
	}
	else if (!badthing && bs->wpStoreDest)
	{ //after we finish running away, switch back to our original destination
		bs->wpDestination = bs->wpStoreDest;
		bs->wpStoreDest = NULL;
	}

	if (badthing && bs->wpCamping)
	{
		bs->wpCamping = NULL;
	}

	if (bs->wpCamping)
	{
		bs->wpDestination = bs->wpCamping;
		return;
	}

	if (!badthing && CTFTakesPriority(bs))
	{
		if (bs->ctfState)
		{
			bs->runningToEscapeThreat = 1;
		}
		return;
	}
	else if (!badthing && SiegeTakesPriority(bs))
	{
		if (bs->siegeState)
		{
			bs->runningToEscapeThreat = 1;
		}
		return;
	}
	else if (!badthing && JMTakesPriority(bs))
	{
		bs->runningToEscapeThreat = 1;
	}

	if (badthing)
	{
		bs->runningLikeASissy = level.time + 100;

		if (bs->wpDestination)
		{
			bs->wpStoreDest = bs->wpDestination;
		}
		bs->wpDestination = NULL;

		if (bs->wpDirection)
		{
			tempInt = bs->wpCurrent->index+1;
		}
		else
		{
			tempInt = bs->wpCurrent->index-1;
		}

		if (gWPArray[tempInt] && gWPArray[tempInt]->inuse && bs->escapeDirTime < level.time)
		{
			VectorSubtract(badthing->s.pos.trBase, bs->wpCurrent->origin, a);
			plusLen = VectorLength(a);
			VectorSubtract(badthing->s.pos.trBase, gWPArray[tempInt]->origin, a);
			minusLen = VectorLength(a);

			if (plusLen < minusLen)
			{
				if (bs->wpDirection)
				{
					bs->wpDirection = 0;
				}
				else
				{
					bs->wpDirection = 1;
				}

				bs->wpCurrent = gWPArray[tempInt];

				bs->escapeDirTime = level.time + Q_irand(500, 1000);//Q_irand(1000, 1400);

				//trap->Print("Escaping from scary bad thing [%s]\n", badthing->classname);
			}
		}
		//trap->Print("Run away run away run away!\n");
		return;
	}

	distChange = 0; //keep the compiler from complaining

	tempInt = BotGetWeaponRange(bs);

	if (tempInt == BWEAPONRANGE_MELEE)
	{
		distChange = 1;
	}
	else if (tempInt == BWEAPONRANGE_SABER)
	{
		distChange = 1;
	}
	else if (tempInt == BWEAPONRANGE_MID)
	{
		distChange = 128;
	}
	else if (tempInt == BWEAPONRANGE_LONG)
	{
		distChange = 300;
	}

	if (bs->revengeEnemy && bs->revengeEnemy->health > 0 &&
		bs->revengeEnemy->client && bs->revengeEnemy->client->pers.connected == CON_CONNECTED)
	{ //if we hate someone, always try to get to them
		if (bs->wpDestSwitchTime < level.time)
		{
			if (bs->revengeEnemy->client)
			{
				VectorCopy(bs->revengeEnemy->client->ps.origin, usethisvec);
			}
			else
			{
				VectorCopy(bs->revengeEnemy->s.origin, usethisvec);
			}

			tempInt = GetNearestVisibleWP(usethisvec, 0);

			if (tempInt != -1 && TotalTrailDistance(bs->wpCurrent->index, tempInt, bs) != -1)
			{
				bs->wpDestination = gWPArray[tempInt];
				bs->wpDestSwitchTime = level.time + Q_irand(5000, 10000);
			}
		}
	}
	else if (bs->squadLeader && bs->squadLeader->health > 0 &&
		bs->squadLeader->client && bs->squadLeader->client->pers.connected == CON_CONNECTED)
	{
		if (bs->wpDestSwitchTime < level.time)
		{
			if (bs->squadLeader->client)
			{
				VectorCopy(bs->squadLeader->client->ps.origin, usethisvec);
			}
			else
			{
				VectorCopy(bs->squadLeader->s.origin, usethisvec);
			}

			tempInt = GetNearestVisibleWP(usethisvec, 0);

			if (tempInt != -1 && TotalTrailDistance(bs->wpCurrent->index, tempInt, bs) != -1)
			{
				bs->wpDestination = gWPArray[tempInt];
				bs->wpDestSwitchTime = level.time + Q_irand(5000, 10000);
			}
		}
	}
	else if (bs->currentEnemy)
	{
		if (bs->currentEnemy->client)
		{
			VectorCopy(bs->currentEnemy->client->ps.origin, usethisvec);
		}
		else
		{
			VectorCopy(bs->currentEnemy->s.origin, usethisvec);
		}

		bChicken = BotIsAChickenWuss(bs);
		bs->runningToEscapeThreat = bChicken;

		if (bs->frame_Enemy_Len < distChange || (bChicken && bChicken != 2))
		{
			cWPIndex = bs->wpCurrent->index;

			if (bs->frame_Enemy_Len > 400)
			{ //good distance away, start running toward a good place for an item or powerup or whatever
				idleWP = GetBestIdleGoal(bs);

				if (idleWP != -1 && gWPArray[idleWP] && gWPArray[idleWP]->inuse)
				{
					bs->wpDestination = gWPArray[idleWP];
				}
			}
			else if (gWPArray[cWPIndex-1] && gWPArray[cWPIndex-1]->inuse &&
				gWPArray[cWPIndex+1] && gWPArray[cWPIndex+1]->inuse)
			{
				VectorSubtract(gWPArray[cWPIndex+1]->origin, usethisvec, a);
				plusLen = VectorLength(a);
				VectorSubtract(gWPArray[cWPIndex-1]->origin, usethisvec, a);
				minusLen = VectorLength(a);

				if (minusLen > plusLen)
				{
					bs->wpDestination = gWPArray[cWPIndex-1];
				}
				else
				{
					bs->wpDestination = gWPArray[cWPIndex+1];
				}
			}
		}
		else if (bChicken != 2 && bs->wpDestSwitchTime < level.time)
		{
			tempInt = GetNearestVisibleWP(usethisvec, 0);

			if (tempInt != -1 && TotalTrailDistance(bs->wpCurrent->index, tempInt, bs) != -1)
			{
				bs->wpDestination = gWPArray[tempInt];

				if (level.gametype == GT_SINGLE_PLAYER)
				{ //be more aggressive
					bs->wpDestSwitchTime = level.time + Q_irand(300, 1000);
				}
				else
				{
					bs->wpDestSwitchTime = level.time + Q_irand(1000, 5000);
				}
			}
		}
	}

	if (!bs->wpDestination && bs->wpDestSwitchTime < level.time)
	{
		//trap->Print("I need something to do\n");
		idleWP = GetBestIdleGoal(bs);

		if (idleWP != -1 && gWPArray[idleWP] && gWPArray[idleWP]->inuse)
		{
			bs->wpDestination = gWPArray[idleWP];
		}
	}
}

//commander CTF AI - tell other bots in the so-called
//"squad" what to do.
void CommanderBotCTFAI(bot_state_t *bs)
{
	int i = 0;
	gentity_t *ent;
	int squadmates = 0;
	gentity_t *squad[MAX_CLIENTS];
	int defendAttackPriority = 0; //0 == attack, 1 == defend
	int guardDefendPriority = 0; //0 == defend, 1 == guard
	int attackRetrievePriority = 0; //0 == retrieve, 1 == attack
	int myFlag = 0;
	int enemyFlag = 0;
	int enemyHasOurFlag = 0;
	int weHaveEnemyFlag = 0;
	int numOnMyTeam = 0;
	//int numOnEnemyTeam = 0;
	int numAttackers = 0;
	//int numDefenders = 0;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		myFlag = PW_REDFLAG;
	}
	else
	{
		myFlag = PW_BLUEFLAG;
	}

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED)
	{
		enemyFlag = PW_BLUEFLAG;
	}
	else
	{
		enemyFlag = PW_REDFLAG;
	}

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client)
		{
			if (ent->client->ps.powerups[enemyFlag] && OnSameTeam(&g_entities[bs->client], ent))
			{
				weHaveEnemyFlag = 1;
			}
			else if (ent->client->ps.powerups[myFlag] && !OnSameTeam(&g_entities[bs->client], ent))
			{
				enemyHasOurFlag = 1;
			}

			if (OnSameTeam(&g_entities[bs->client], ent))
			{
				numOnMyTeam++;
			}
			else
			{
				//numOnEnemyTeam++;
			}

			if (botstates[ent->s.number])
			{
				if (botstates[ent->s.number]->ctfState == CTFSTATE_ATTACKER ||
					botstates[ent->s.number]->ctfState == CTFSTATE_RETRIEVAL)
				{
					numAttackers++;
				}
				else
				{
					//numDefenders++;
				}
			}
			else
			{ //assume real players to be attackers in our logic
				numAttackers++;
			}
		}
		i++;
	}

	i = 0;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && botstates[i] && botstates[i]->squadLeader && botstates[i]->squadLeader->s.number == bs->client && i != bs->client)
		{
			squad[squadmates] = ent;
			squadmates++;
		}

		i++;
	}

	squad[squadmates] = &g_entities[bs->client];
	squadmates++;

	i = 0;

	if (enemyHasOurFlag && !weHaveEnemyFlag)
	{ //start off with an attacker instead of a retriever if we don't have the enemy flag yet so that they can't capture it first.
	  //after that we focus on getting our flag back.
		attackRetrievePriority = 1;
	}

	while (i < squadmates)
	{
		if (squad[i] && squad[i]->client && botstates[squad[i]->s.number])
		{
			if (botstates[squad[i]->s.number]->ctfState != CTFSTATE_GETFLAGHOME)
			{ //never tell a bot to stop trying to bring the flag to the base
				if (defendAttackPriority)
				{
					if (weHaveEnemyFlag)
					{
						if (guardDefendPriority)
						{
							botstates[squad[i]->s.number]->ctfState = CTFSTATE_GUARDCARRIER;
							guardDefendPriority = 0;
						}
						else
						{
							botstates[squad[i]->s.number]->ctfState = CTFSTATE_DEFENDER;
							guardDefendPriority = 1;
						}
					}
					else
					{
						botstates[squad[i]->s.number]->ctfState = CTFSTATE_DEFENDER;
					}
					defendAttackPriority = 0;
				}
				else
				{
					if (enemyHasOurFlag)
					{
						if (attackRetrievePriority)
						{
							botstates[squad[i]->s.number]->ctfState = CTFSTATE_ATTACKER;
							attackRetrievePriority = 0;
						}
						else
						{
							botstates[squad[i]->s.number]->ctfState = CTFSTATE_RETRIEVAL;
							attackRetrievePriority = 1;
						}
					}
					else
					{
						botstates[squad[i]->s.number]->ctfState = CTFSTATE_ATTACKER;
					}
					defendAttackPriority = 1;
				}
			}
			else if ((numOnMyTeam < 2 || !numAttackers) && enemyHasOurFlag)
			{ //I'm the only one on my team who will attack and the enemy has my flag, I have to go after him
				botstates[squad[i]->s.number]->ctfState = CTFSTATE_RETRIEVAL;
			}
		}

		i++;
	}
}

//similar to ctf ai, for siege
void CommanderBotSiegeAI(bot_state_t *bs)
{
	int i = 0;
	int squadmates = 0;
	int commanded = 0;
	int teammates = 0;
	gentity_t *squad[MAX_CLIENTS];
	gentity_t *ent;
	bot_state_t *bst;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && OnSameTeam(&g_entities[bs->client], ent) && botstates[ent->s.number])
		{
			bst = botstates[ent->s.number];

			if (bst && !bst->isSquadLeader && !bst->state_Forced)
			{
				squad[squadmates] = ent;
				squadmates++;
			}
			else if (bst && !bst->isSquadLeader && bst->state_Forced)
			{ //count them as commanded
				commanded++;
			}
		}

		if (ent && ent->client && OnSameTeam(&g_entities[bs->client], ent))
		{
			teammates++;
		}

		i++;
	}

	if (!squadmates)
	{
		return;
	}

	//tell squad mates to do what I'm doing, up to half of team, let the other half make their own decisions
	i = 0;

	while (i < squadmates && squad[i])
	{
		bst = botstates[squad[i]->s.number];

		if (commanded > teammates/2)
		{
			break;
		}

		if (bst)
		{
			bst->state_Forced = bs->siegeState;
			bst->siegeState = bs->siegeState;
			commanded++;
		}

		i++;
	}
}

//teamplay ffa squad ai
void BotDoTeamplayAI(bot_state_t *bs)
{
	if (bs->state_Forced)
	{
		bs->teamplayState = bs->state_Forced;
	}

	if (bs->teamplayState == TEAMPLAYSTATE_REGROUP)
	{ //force to find a new leader
		bs->squadLeader = NULL;
		bs->isSquadLeader = 0;
	}
}

//like ctf and siege commander ai, instruct the squad
void CommanderBotTeamplayAI(bot_state_t *bs)
{
	int i = 0;
	int squadmates = 0;
	//int teammates = 0;
	int teammate_indanger = -1;
	int teammate_helped = 0;
	int foundsquadleader = 0;
	int worsthealth = 50;
	gentity_t *squad[MAX_CLIENTS];
	gentity_t *ent;
	bot_state_t *bst;

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && OnSameTeam(&g_entities[bs->client], ent) && botstates[ent->s.number])
		{
			bst = botstates[ent->s.number];

			if (foundsquadleader && bst && bst->isSquadLeader)
			{ //never more than one squad leader
				bst->isSquadLeader = 0;
			}

			if (bst && !bst->isSquadLeader)
			{
				squad[squadmates] = ent;
				squadmates++;
			}
			else if (bst)
			{
				foundsquadleader = 1;
			}
		}

		if (ent && ent->client && OnSameTeam(&g_entities[bs->client], ent))
		{
			//teammates++;

			if (ent->health < worsthealth)
			{
				teammate_indanger = ent->s.number;
				worsthealth = ent->health;
			}
		}

		i++;
	}

	if (!squadmates)
	{
		return;
	}

	i = 0;

	while (i < squadmates && squad[i])
	{
		bst = botstates[squad[i]->s.number];

		if (bst && !bst->state_Forced)
		{ //only order if this guy is not being ordered directly by the real player team leader
			if (teammate_indanger >= 0 && !teammate_helped)
			{ //send someone out to help whoever needs help most at the moment
				bst->teamplayState = TEAMPLAYSTATE_ASSISTING;
				bst->squadLeader = &g_entities[teammate_indanger];
				teammate_helped = 1;
			}
			else if ((teammate_indanger == -1 || teammate_helped) && bst->teamplayState == TEAMPLAYSTATE_ASSISTING)
			{ //no teammates need help badly, but this guy is trying to help them anyway, so stop
				bst->teamplayState = TEAMPLAYSTATE_FOLLOWING;
				bst->squadLeader = &g_entities[bs->client];
			}

			if (bs->squadRegroupInterval < level.time && Q_irand(1, 10) < 5)
			{ //every so often tell the squad to regroup for the sake of variation
				if (bst->teamplayState == TEAMPLAYSTATE_FOLLOWING)
				{
					bst->teamplayState = TEAMPLAYSTATE_REGROUP;
				}

				bs->isSquadLeader = 0;
				bs->squadCannotLead = level.time + 500;
				bs->squadRegroupInterval = level.time + Q_irand(45000, 65000);
			}
		}

		i++;
	}
}

//pick which commander ai to use based on gametype
void CommanderBotAI(bot_state_t *bs)
{
	if (level.gametype == GT_CTF || level.gametype == GT_CTY)
	{
		CommanderBotCTFAI(bs);
	}
	else if (level.gametype == GT_SIEGE)
	{
		CommanderBotSiegeAI(bs);
	}
	else if (level.gametype == GT_TEAM)
	{
		CommanderBotTeamplayAI(bs);
	}
}

//close range combat routines
void MeleeCombatHandling(bot_state_t *bs)
{
	vec3_t usethisvec;
	vec3_t downvec;
	vec3_t midorg;
	vec3_t a;
	vec3_t fwd;
	vec3_t mins, maxs;
	trace_t tr;
	int en_down;
	int me_down;
	int mid_down;

	if (!bs->currentEnemy)
	{
		return;
	}

	if (bs->currentEnemy->client)
	{
		VectorCopy(bs->currentEnemy->client->ps.origin, usethisvec);
	}
	else
	{
		VectorCopy(bs->currentEnemy->s.origin, usethisvec);
	}

	if (bs->meleeStrafeTime < level.time)
	{
		if (bs->meleeStrafeDir < 0)
		{
			bs->meleeStrafeDir = 1;
		}
		else
		{
			bs->meleeStrafeDir = -1;
		}

		bs->meleeStrafeTime = level.time + Q_irand(500, 1800);
	}

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = -24;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 32;

	VectorCopy(usethisvec, downvec);
	downvec[2] -= 4096;

	JP_Trace(&tr, usethisvec, mins, maxs, downvec, -1, MASK_SOLID, qfalse, 0, 0);

	en_down = (int)tr.endpos[2];

	VectorCopy(bs->origin, downvec);
	downvec[2] -= 4096;

	JP_Trace(&tr, bs->origin, mins, maxs, downvec, -1, MASK_SOLID, qfalse, 0, 0);

	me_down = (int)tr.endpos[2];

	VectorSubtract(usethisvec, bs->origin, a);
	vectoangles(a, a);
	AngleVectors(a, fwd, NULL, NULL);

	midorg[0] = bs->origin[0] + fwd[0]*bs->frame_Enemy_Len/2;
	midorg[1] = bs->origin[1] + fwd[1]*bs->frame_Enemy_Len/2;
	midorg[2] = bs->origin[2] + fwd[2]*bs->frame_Enemy_Len/2;

	VectorCopy(midorg, downvec);
	downvec[2] -= 4096;

	JP_Trace(&tr, midorg, mins, maxs, downvec, -1, MASK_SOLID, qfalse, 0, 0);

	mid_down = (int)tr.endpos[2];

	if (me_down == en_down &&
		en_down == mid_down)
	{
		VectorCopy(usethisvec, bs->goalPosition);
	}
}

//saber combat routines (it's simple, but it works)
void SaberCombatHandling(bot_state_t *bs)
{
	vec3_t usethisvec;
	vec3_t downvec;
	vec3_t midorg;
	vec3_t a;
	vec3_t fwd;
	vec3_t mins, maxs;
	trace_t tr;
	int en_down;
	int me_down;
	int mid_down;

	if (!bs->currentEnemy)
	{
		return;
	}

	if (bs->currentEnemy->client)
	{
		VectorCopy(bs->currentEnemy->client->ps.origin, usethisvec);
	}
	else
	{
		VectorCopy(bs->currentEnemy->s.origin, usethisvec);
	}

	if (bs->meleeStrafeTime < level.time)
	{
		if (bs->meleeStrafeDir < 0)
		{
			bs->meleeStrafeDir = 1;
		}
		else
		{
			bs->meleeStrafeDir = -1;
		}

		bs->meleeStrafeTime = level.time + Q_irand(500, 1800);
	}

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = -24;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 32;

	VectorCopy(usethisvec, downvec);
	downvec[2] -= 4096;

	JP_Trace(&tr, usethisvec, mins, maxs, downvec, -1, MASK_SOLID, qfalse, 0, 0);

	en_down = (int)tr.endpos[2];

	if (tr.startsolid || tr.allsolid)
	{
		en_down = 1;
		me_down = 2;
	}
	else
	{
		VectorCopy(bs->origin, downvec);
		downvec[2] -= 4096;

		JP_Trace(&tr, bs->origin, mins, maxs, downvec, -1, MASK_SOLID, qfalse, 0, 0);

		me_down = (int)tr.endpos[2];

		if (tr.startsolid || tr.allsolid)
		{
			en_down = 1;
			me_down = 2;
		}
	}

	VectorSubtract(usethisvec, bs->origin, a);
	vectoangles(a, a);
	AngleVectors(a, fwd, NULL, NULL);

	midorg[0] = bs->origin[0] + fwd[0]*bs->frame_Enemy_Len/2;
	midorg[1] = bs->origin[1] + fwd[1]*bs->frame_Enemy_Len/2;
	midorg[2] = bs->origin[2] + fwd[2]*bs->frame_Enemy_Len/2;

	VectorCopy(midorg, downvec);
	downvec[2] -= 4096;

	JP_Trace(&tr, midorg, mins, maxs, downvec, -1, MASK_SOLID, qfalse, 0, 0);

	mid_down = (int)tr.endpos[2];

	if (me_down == en_down &&
		en_down == mid_down)
	{
		if (usethisvec[2] > (bs->origin[2]+32) &&
			bs->currentEnemy->client &&
			bs->currentEnemy->client->ps.groundEntityNum == ENTITYNUM_NONE)
		{
			bs->jumpTime = level.time + 100;
		}

		if (bs->frame_Enemy_Len > 128)
		{ //be ready to attack
			bs->saberDefending = 0;
			bs->saberDefendDecideTime = level.time + Q_irand(1000, 2000);
		}
		else
		{
			if (bs->saberDefendDecideTime < level.time)
			{
				if (bs->saberDefending)
				{
					bs->saberDefending = 0;
				}
				else
				{
					bs->saberDefending = 1;
				}

				bs->saberDefendDecideTime = level.time + Q_irand(500, 2000);
			}
		}

		if (bs->frame_Enemy_Len < 54)
		{
			VectorCopy(bs->origin, bs->goalPosition);
			bs->saberBFTime = 0;
		}
		else
		{
			VectorCopy(usethisvec, bs->goalPosition);
		}

		if (bs->currentEnemy && bs->currentEnemy->client)
		{
			if (!BG_SaberInSpecial(bs->currentEnemy->client->ps.saberMove) && bs->frame_Enemy_Len > 90 && bs->saberBFTime > level.time && bs->saberBTime > level.time && bs->beStill < level.time && bs->saberSTime < level.time)
			{
				bs->beStill = level.time + Q_irand(500, 1000);
				bs->saberSTime = level.time + Q_irand(1200, 1800);
			}
			else if (bs->currentEnemy->client->ps.weapon == WP_SABER && bs->frame_Enemy_Len < 80 && ((Q_irand(1, 10) < 8 && bs->saberBFTime < level.time) || bs->saberBTime > level.time || BG_SaberInKata(bs->currentEnemy->client->ps.saberMove) || bs->currentEnemy->client->ps.saberMove == LS_SPINATTACK || bs->currentEnemy->client->ps.saberMove == LS_SPINATTACK_DUAL))
			{
				vec3_t vs;
				vec3_t groundcheck;
				int idealDist;
				int checkIncr = 0;

				VectorSubtract(bs->origin, usethisvec, vs);
				VectorNormalize(vs);

				if (BG_SaberInKata(bs->currentEnemy->client->ps.saberMove) || bs->currentEnemy->client->ps.saberMove == LS_SPINATTACK || bs->currentEnemy->client->ps.saberMove == LS_SPINATTACK_DUAL)
				{
					idealDist = 256;
				}
				else
				{
					idealDist = 64;
				}

				while (checkIncr < idealDist)
				{
					bs->goalPosition[0] = bs->origin[0] + vs[0]*checkIncr;
					bs->goalPosition[1] = bs->origin[1] + vs[1]*checkIncr;
					bs->goalPosition[2] = bs->origin[2] + vs[2]*checkIncr;

					if (bs->saberBTime < level.time)
					{
						bs->saberBFTime = level.time + Q_irand(900, 1300);
						bs->saberBTime = level.time + Q_irand(300, 700);
					}

					VectorCopy(bs->goalPosition, groundcheck);

					groundcheck[2] -= 64;

					JP_Trace(&tr, bs->goalPosition, NULL, NULL, groundcheck, bs->client, MASK_SOLID, qfalse, 0, 0);
					
					if (tr.fraction == 1.0f)
					{ //don't back off of a ledge
						VectorCopy(usethisvec, bs->goalPosition);
						break;
					}
					checkIncr += 64;
				}
			}
			else if (bs->currentEnemy->client->ps.weapon == WP_SABER && bs->frame_Enemy_Len >= 75)
			{
				bs->saberBFTime = level.time + Q_irand(700, 1300);
				bs->saberBTime = 0;
			}
		}

		/*AngleVectors(bs->viewangles, NULL, fwd, NULL);

		if (bs->meleeStrafeDir)
		{
			bs->goalPosition[0] += fwd[0]*16;
			bs->goalPosition[1] += fwd[1]*16;
			bs->goalPosition[2] += fwd[2]*16;
		}
		else
		{
			bs->goalPosition[0] -= fwd[0]*16;
			bs->goalPosition[1] -= fwd[1]*16;
			bs->goalPosition[2] -= fwd[2]*16;
		}*/
	}
	else if (bs->frame_Enemy_Len <= 56)
	{
		bs->doAttack = 1;
		bs->saberDefending = 0;
	}
}

//should we be "leading" our aim with this weapon? And if
//so, by how much?
float BotWeaponCanLead(bot_state_t *bs)
{
	switch ( bs->cur_ps.weapon )
	{
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
		return 0.5f;
	case WP_BLASTER:
		return 0.35f;
	case WP_BOWCASTER:
		return 0.5f;
	case WP_REPEATER:
		return 0.45f;
	case WP_THERMAL:
		return 0.5f;
	case WP_DEMP2:
		return 0.35f;
	case WP_ROCKET_LAUNCHER:
		return 0.9f;
	case WP_CONCUSSION:
		if (bs->doAltAttack)
			return 0.03f;
		else
			return 0.2f;
	case WP_FLECHETTE:
		return 0.3f;
	case WP_DISRUPTOR:
		if (g_tweakWeapons.integer & WT_PROJ_SNIPER)
			return 0.08f;
		else
			return 0.03f;
	default:
		return 0.0f;
	}
}

float G_NewBotAIGetProjectileSpeed(int weapon, qboolean altFire) {
	float projectileSpeed = 0;

	if (weapon == WP_BRYAR_OLD || weapon == WP_BRYAR_PISTOL || (weapon == WP_REPEATER && !altFire))
		projectileSpeed = 1600;
	else if (weapon == WP_BLASTER && (g_tweakWeapons.integer & WT_TRIBES))
		projectileSpeed = 10440;
	else if (weapon == WP_BLASTER)
		projectileSpeed = 2300;
	else if (weapon == WP_DISRUPTOR && (g_tweakWeapons.integer & WT_PROJ_SNIPER))
		projectileSpeed = 9000;
	else if (weapon == WP_BOWCASTER)
		projectileSpeed = 1300;
	else if (weapon == WP_DEMP2 && !altFire) {
		if (g_tweakWeapons.integer & WT_TRIBES)
			projectileSpeed = 2200;
		else
			projectileSpeed = 1800;
	}
	else if (weapon == WP_REPEATER && altFire) {
		if (g_tweakWeapons.integer & WT_TRIBES)
			projectileSpeed = 1400;
		else
			projectileSpeed = 1100;
	}
	else if (weapon == WP_FLECHETTE && (g_tweakWeapons.integer & WT_STAKE_GUN))
		projectileSpeed = 3000;
	else if ((weapon == WP_FLECHETTE) && altFire)
		projectileSpeed = 1150;
	else if (weapon == WP_FLECHETTE && !altFire)
		projectileSpeed = 3500;
	else if (weapon == WP_REPEATER && (g_tweakWeapons.integer & WT_TRIBES) && altFire)
		projectileSpeed = 2000;
	else if (weapon == WP_ROCKET_LAUNCHER && !altFire) {
		if (g_tweakWeapons.integer & WT_TRIBES)
			projectileSpeed = 2040;
		else
			projectileSpeed = 900;
	}
	else if (weapon == WP_ROCKET_LAUNCHER && altFire)
		projectileSpeed = 450;
	else if (weapon == WP_CONCUSSION && !altFire && (g_tweakWeapons.integer & WT_TRIBES))
		projectileSpeed = 2275;
	else if (weapon == WP_CONCUSSION && !altFire)
		projectileSpeed = 3000;
	else if (weapon == WP_THERMAL)
		projectileSpeed = 900;

	return projectileSpeed * g_projectileVelocityScale.value;
}

/*
trace between us and this new point, if there is something there, see how far away it is, and if its close enough to splash damage us dont fire, or just dont fire at all? - or switch to demp2 alt if it would dmg them
if no LOS, aim at someone else if LOS.. ?
*/
//offset the desired view angles with aim leading in mind
void G_NewBotAIAimLeading(bot_state_t* bs, vec3_t headlevel) {
	vec3_t predictedSpot, a, ang;
	float eta = 0, projectileDrop = 0;
	float projectileSpeed;

	if (!bs->currentEnemy || !bs->currentEnemy->client)
		return;

	projectileSpeed = G_NewBotAIGetProjectileSpeed(bs->cur_ps.weapon, bs->doAltAttack);

	if (projectileSpeed) { //this should be done after playr movement
		if (g_projectileInheritance.value) { //todo still have to teach brodie full inheritence projectiles
			vec3_t botforward;
			AngleVectors(bs->viewangles, botforward, NULL, NULL);
			projectileSpeed += DotProduct(botforward, g_entities[bs->client].client->ps.velocity)*g_projectileInheritance.value;
		}

		eta = (bs->frame_Enemy_Len / projectileSpeed); //TODO: Adjust if its a curved projectile arc
		VectorMA(headlevel, eta, bs->currentEnemy->client->ps.velocity, predictedSpot); //Multiple vel by eta, and add it to their origin to get predicted spot
		if (((bs->cur_ps.weapon == WP_REPEATER) && bs->doAltAttack) ||
			((bs->cur_ps.weapon == WP_DISRUPTOR) && (g_tweakWeapons.integer & WT_PROJ_SNIPER)) ||
			((bs->cur_ps.weapon == WP_ROCKET_LAUNCHER) && !bs->doAltAttack && (g_tweakWeapons.integer & WT_TRIBES)) ||
			((bs->cur_ps.weapon == WP_FLECHETTE) && (g_tweakWeapons.integer & WT_STAKE_GUN)) ||
			((bs->cur_ps.weapon == WP_FLECHETTE) && !(g_tweakWeapons.integer & WT_STAKE_GUN) && bs->doAltAttack) ||
			(bs->cur_ps.weapon == WP_THERMAL) ||
			(g_tweakWeapons.integer & WT_PROJECTILE_GRAVITY)) {
			projectileDrop = (0.5) * (800) * (eta * eta); //If weapon has gravity, compensate to aim higher
			predictedSpot[2] += projectileDrop;
		}
		if (bs->currentEnemy->client->ps.groundEntityNum == ENTITYNUM_NONE) { //In Air
			if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_LEVITATION) && bs->currentEnemy->client->ps.velocity[2] > JUMP_VELOCITY - 10) { //If person is forcejumping up... assume they will keep forcejumping.. until end of jump?
				const float diff = (predictedSpot[2] - bs->cur_ps.fd.forceJumpZStart);
				if (diff > forceJumpHeight[bs->currentEnemy->client->ps.fd.forcePowerLevel[FP_LEVITATION]]) { //We predict they will be higher than they can possibly jump to, so correct
					predictedSpot[2] -= (diff - forceJumpHeight[bs->currentEnemy->client->ps.fd.forcePowerLevel[FP_LEVITATION]]);
				}
			}
			else if (bs->currentEnemy->client->ps.eFlags & EF_JETPACK_ACTIVE) { //dont predict drops if they are jetting?
			}
			else {
				vec3_t predictedSpotGrav;
				trace_t tr;
				float playerDrop = (0.5f) * g_gravity.value * (eta * eta);

				predictedSpotGrav[0] = predictedSpot[0];
				predictedSpotGrav[1] = predictedSpot[1];
				predictedSpotGrav[2] = predictedSpot[2] - playerDrop;

				JP_Trace(&tr, headlevel, 0, 0, predictedSpotGrav, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0); //eh? do this if any eta tbh.. not just if jumping
				VectorCopy(tr.endpos, predictedSpot);

				//G_CheckGroundPound(bs, eta, tr.fraction * eta); //return if so?

				//now trace from us to pos and see if its a los, if not don't fire? or...,.,.,.,.,
				//def never fire if within selfkill range
				//y headlevel dont work?
			}
		}
	}
	else { //Hitscan, maybe tweak this so it leads a tiny bit for netcode
		vec3_t dir;
		VectorCopy(bs->currentEnemy->client->ps.velocity, dir);
		VectorNormalize(dir);
		VectorMA(headlevel, 4, dir, predictedSpot); //Lead them by 4u in their direction of motion?

		if ((bs->cur_ps.weapon == WP_DEMP2) && bs->doAltAttack && ((bs->currentEnemy->client->ps.groundEntityNum != ENTITYNUM_NONE) || bs->currentEnemy->client->ps.velocity[2] < 0)) { //stupid demp2 delay compensate, only if they are not in air, or in air and moving down
			trace_t tr;
			vec3_t predictedSpotGround;

			VectorMA(headlevel, 0.5f, bs->currentEnemy->client->ps.velocity, predictedSpot); //Lead them by 500ms
			predictedSpotGround[0] = predictedSpot[0];
			predictedSpotGround[1] = predictedSpot[1];
			predictedSpotGround[2] = predictedSpot[2] - 1024;
			JP_Trace(&tr, headlevel, 0, 0, predictedSpotGround, ENTITYNUM_NONE, MASK_SOLID, qfalse, 0, 0); //aim at ground below them
			VectorCopy(tr.endpos, predictedSpot);

			//G_CheckGroundPound(bs, 0.2f, tr.fraction * 0.2f); //return if so?
		}
	}

	if (bs->cur_ps.weapon == WP_SABER && bs->cur_ps.saberMove >= 4) { //Poke
		vec3_t saberDiff;
		VectorSubtract(bs->currentEnemy->client->ps.origin, g_entities[bs->client].client->saber[0].blade[0].trail.tip, saberDiff);
		VectorAdd(saberDiff, predictedSpot, predictedSpot);
	}


	VectorSubtract(predictedSpot, bs->eye, a);
	vectoangles(a, ang);
	VectorCopy(ang, bs->goalAngles);

	if (bs->cur_ps.weapon == WP_SABER && bs->cur_ps.saberMove >= 4) { //Poke and Wiggle
		if (level.time % 100 > 50) {
			bs->goalAngles[YAW] += 1.5f;
		}
		else {
			bs->goalAngles[YAW] -= 1.5f;
		}

		if (level.time % 200 > 100) {
			bs->goalAngles[PITCH] += 3.0f;
		}
		else {
			bs->goalAngles[PITCH] -= 3.0f;
		}

		if (bs->cur_ps.saberMove == LS_A_T2B) {
			if (bs->cur_ps.torsoTimer > 400) { //aim lower at start of red swing
				bs->goalAngles[PITCH] += 45;
			}
			else if (bs->cur_ps.torsoTimer < 300) {
				bs->goalAngles[PITCH] -= 45.0f;
			}
		}
		else if (bs->cur_ps.saberMove == LS_S_R2L) {//Start of yellow horizontal
			bs->goalAngles[YAW] += 45;
		}
		else if (bs->cur_ps.saberMove == LS_A_R2L) {//yellow horizontal
			if (level.time % 200 > 100)
				bs->goalAngles[YAW] += 20;
			else
				bs->goalAngles[YAW] += 20;

		}
		else if (bs->cur_ps.saberMove == LS_R_R2L) {//end of yellow horizontal
			//bs->goalAngles[YAW] -= 30;
		}

		bs->goalAngles[YAW] = AngleNormalize360(bs->goalAngles[YAW]);
		bs->goalAngles[PITCH] = AngleNormalize360(bs->goalAngles[PITCH]);
	}
}


//offset the desired view angles with aim leading in mind
void BotAimLeading(bot_state_t* bs, vec3_t headlevel, float leadAmount)
{
	int x;
	vec3_t predictedSpot;
	vec3_t movementVector;
	vec3_t a, ang;
	float vtotal;

	if (!bs->currentEnemy ||
		!bs->currentEnemy->client)
	{
		return;
	}

	if (!bs->frame_Enemy_Len)
	{
		return;
	}

	vtotal = 0;

	if (bs->currentEnemy->client->ps.velocity[0] < 0)
	{
		vtotal += -bs->currentEnemy->client->ps.velocity[0];
	}
	else
	{
		vtotal += bs->currentEnemy->client->ps.velocity[0];
	}

	if (bs->currentEnemy->client->ps.velocity[1] < 0)
	{
		vtotal += -bs->currentEnemy->client->ps.velocity[1];
	}
	else
	{
		vtotal += bs->currentEnemy->client->ps.velocity[1];
	}

	if (bs->currentEnemy->client->ps.velocity[2] < 0)
	{
		vtotal += -bs->currentEnemy->client->ps.velocity[2];
	}
	else
	{
		vtotal += bs->currentEnemy->client->ps.velocity[2];
	}

	//G_Printf("Leadin target with a velocity total of %f\n", vtotal);

	VectorCopy(bs->currentEnemy->client->ps.velocity, movementVector);

	VectorNormalize(movementVector);

	x = bs->frame_Enemy_Len * leadAmount; //hardly calculated with an exact science, but it works

	if (vtotal > 400)
	{
		vtotal = 400;
	}

	if (vtotal)
	{
		x = (bs->frame_Enemy_Len * 0.9) * leadAmount * (vtotal * 0.0012); //hardly calculated with an exact science, but it works
	}
	else
	{
		x = (bs->frame_Enemy_Len * 0.9) * leadAmount; //hardly calculated with an exact science, but it works
	}

	predictedSpot[0] = headlevel[0] + (movementVector[0] * x);
	predictedSpot[1] = headlevel[1] + (movementVector[1] * x);
	predictedSpot[2] = headlevel[2] + (movementVector[2] * x);

	VectorSubtract(predictedSpot, bs->eye, a);
	vectoangles(a, ang);
	VectorCopy(ang, bs->goalAngles);
}

//wobble our aim around based on our sk1llz
void BotAimOffsetGoalAngles(bot_state_t *bs)
{
	int i;
	float accVal;
	qboolean tightenCombatAim = qfalse;
	i = 0;

	if (bs->skills.perfectaim)
	{
		return;
	}

	if (bs->aimOffsetTime > level.time)
	{
		if (bs->aimOffsetAmtYaw)
		{
			bs->goalAngles[YAW] += bs->aimOffsetAmtYaw;
		}

		if (bs->aimOffsetAmtPitch)
		{
			bs->goalAngles[PITCH] += bs->aimOffsetAmtPitch;
		}

		while (i <= 2)
		{
			if (bs->goalAngles[i] > 360)
			{
				bs->goalAngles[i] -= 360;
			}

			if (bs->goalAngles[i] < 0)
			{
				bs->goalAngles[i] += 360;
			}

			i++;
		}
		return;
	}

	accVal = bs->skills.accuracy/bs->settings.skill;

	if (bs->currentEnemy && BotMindTricked(bs->client, bs->currentEnemy->s.number))
	{ //having to judge where they are by hearing them, so we should be quite inaccurate here
		accVal *= 7;

		if (accVal < 30)
		{
			accVal = 30;
		}
	}

	if (bs->revengeEnemy && bs->revengeHateLevel &&
		bs->currentEnemy == bs->revengeEnemy)
	{ //bot becomes more skilled as anger level raises
		accVal = accVal/bs->revengeHateLevel;
	}

	if (bs->currentEnemy && bs->frame_Enemy_Vis)
	{ //assume our goal is aiming at the enemy, seeing as he's visible and all
		if (!bs->currentEnemy->s.pos.trDelta[0] &&
			!bs->currentEnemy->s.pos.trDelta[1] &&
			!bs->currentEnemy->s.pos.trDelta[2])
		{
			accVal = 0; //he's not even moving, so he shouldn't really be hard to hit.
		}
		else
		{
			accVal += accVal*0.25; //if he's moving he's this much harder to hit
		}

		if (g_entities[bs->client].s.pos.trDelta[0] ||
			g_entities[bs->client].s.pos.trDelta[1] ||
			g_entities[bs->client].s.pos.trDelta[2])
		{
			accVal += accVal*0.15; //make it somewhat harder to aim if we're moving also
		}
	}

	tightenCombatAim = (g_newBotAI.integer &&
		bs->currentEnemy && bs->frame_Enemy_Vis &&
		bs->frame_Enemy_Len > 0 && bs->frame_Enemy_Len < NEWBOTAI_TARGET_COMMIT_DISTANCE &&
		bs->combatNavHoldUntil > level.time) ? qtrue : qfalse;
	if (tightenCombatAim)
	{
		accVal *= 0.35f;
		if (accVal > 6.0f)
		{
			accVal = 6.0f;
		}
	}

	if (accVal > 90)
	{
		accVal = 90;
	}
	if (accVal < 1)
	{
		accVal = 0;
	}

	if (!accVal)
	{
		bs->aimOffsetAmtYaw = 0;
		bs->aimOffsetAmtPitch = 0;
		return;
	}

	if (rand()%10 <= 5)
	{
		bs->aimOffsetAmtYaw = rand()%(int)accVal;
	}
	else
	{
		bs->aimOffsetAmtYaw = -(rand()%(int)accVal);
	}

	if (rand()%10 <= 5)
	{
		bs->aimOffsetAmtPitch = rand()%(int)accVal;
	}
	else
	{
		bs->aimOffsetAmtPitch = -(rand()%(int)accVal);
	}

	if (tightenCombatAim)
	{
		bs->aimOffsetTime = level.time + rand()%400 + 500;
	}
	else
	{
		bs->aimOffsetTime = level.time + rand()%500 + 200;
	}
}

//do we want to alt fire with this weapon?
int ShouldSecondaryFire(bot_state_t *bs)
{
	int weap;
	int dif;
	float rTime;

	weap = bs->cur_ps.weapon;

	if (bs->cur_ps.ammo[weaponData[weap].ammoIndex] < weaponData[weap].altEnergyPerShot)
	{
		return 0;
	}

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT && bs->cur_ps.weapon == WP_ROCKET_LAUNCHER)
	{
		float heldTime = (level.time - bs->cur_ps.weaponChargeTime);

		rTime = bs->cur_ps.rocketLockTime;

		if (rTime < 1)
		{
			rTime = bs->cur_ps.rocketLastValidTime;
		}

		if (heldTime > 5000)
		{ //just give up and release it if we can't manage a lock in 5 seconds
			return 2;
		}

		if (rTime > 0)
		{
			dif = ( level.time - rTime ) / ( 1200.0f / 16.0f );

			if (dif >= 10)
			{
				return 2;
			}
			else if (bs->frame_Enemy_Len > 250)
			{
				return 1;
			}
		}
		else if (bs->frame_Enemy_Len > 250)
		{
			return 1;
		}
	}
	else if ((bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT) && (level.time - bs->cur_ps.weaponChargeTime) > bs->altChargeTime)
	{
		return 2;
	}
	else if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)
	{
		return 1;
	}

	if (weap == WP_BRYAR_PISTOL && bs->frame_Enemy_Len < 300)
	{
		return 1;
	}
	else if (weap == WP_BOWCASTER && bs->frame_Enemy_Len > 300)
	{
		return 1;
	}
	else if (weap == WP_REPEATER && bs->frame_Enemy_Len < 500 && bs->frame_Enemy_Len > 250)
	{
		return 1;
	}
	else if (weap == WP_BLASTER && bs->frame_Enemy_Len < 1000)
	{
		return 1;
	}
	else if (weap == WP_FLECHETTE && bs->frame_Enemy_Len < 500 && bs->frame_Enemy_Len > 250)
	{
		return 1;
	}

	return 0;
}

//standard weapon combat routines
int CombatBotAI(bot_state_t *bs, float thinktime)
{
	vec3_t eorg, a;
	int secFire;
	float fovcheck;

	if (!bs->currentEnemy)
	{
		return 0;
	}

	if (bs->currentEnemy->client)
	{
		VectorCopy(bs->currentEnemy->client->ps.origin, eorg);
	}
	else
	{
		VectorCopy(bs->currentEnemy->s.origin, eorg);
	}

	VectorSubtract(eorg, bs->eye, a);
	vectoangles(a, a);

	if (BotGetWeaponRange(bs) == BWEAPONRANGE_SABER)
	{
		if (bs->frame_Enemy_Len <= SABER_ATTACK_RANGE)
		{
			bs->doAttack = 1;
		}
	}
	else if (BotGetWeaponRange(bs) == BWEAPONRANGE_MELEE)
	{
		if (bs->frame_Enemy_Len <= MELEE_ATTACK_RANGE)
		{
			bs->doAttack = 1;
		}
	}
	else
	{
		if (bs->cur_ps.weapon == WP_THERMAL || bs->cur_ps.weapon == WP_ROCKET_LAUNCHER)
		{ //be careful with the hurty weapons
			fovcheck = 40;

			if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT &&
				bs->cur_ps.weapon == WP_ROCKET_LAUNCHER)
			{ //if we're charging the weapon up then we can hold fire down within a normal fov
				fovcheck = 60;
			}
		}
		else
		{
			fovcheck = 60;
		}

		if (bs->cur_ps.weaponstate == WEAPON_CHARGING ||
			bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)
		{
			fovcheck = 160;
		}

		if (bs->frame_Enemy_Len < 128)
		{
			fovcheck *= 2;
		}

		if (InFieldOfVision(bs->viewangles, fovcheck, a))
		{
			if (bs->cur_ps.weapon == WP_THERMAL)
			{
				if (((level.time - bs->cur_ps.weaponChargeTime) < (bs->frame_Enemy_Len*2) &&
					(level.time - bs->cur_ps.weaponChargeTime) < 4000 &&
					bs->frame_Enemy_Len > 64) ||
					(bs->cur_ps.weaponstate != WEAPON_CHARGING &&
					bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT))
				{
					if (bs->cur_ps.weaponstate != WEAPON_CHARGING && bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT)
					{
						if (bs->frame_Enemy_Len > 512 && bs->frame_Enemy_Len < 800)
						{
							bs->doAltAttack = 1;
							//bs->doAttack = 1;
						}
						else
						{
							bs->doAttack = 1;
							//bs->doAltAttack = 1;
						}
					}

					if (bs->cur_ps.weaponstate == WEAPON_CHARGING)
					{
						bs->doAttack = 1;
					}
					else if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)
					{
						bs->doAltAttack = 1;
					}
				}
			}
			else
			{
				secFire = ShouldSecondaryFire(bs);

				if (bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT &&
					bs->cur_ps.weaponstate != WEAPON_CHARGING)
				{
					bs->altChargeTime = Q_irand(500, 1000);
				}

				if (secFire == 1)
				{
					bs->doAltAttack = 1;
				}
				else if (!secFire)
				{
					if (bs->cur_ps.weapon != WP_THERMAL)
					{
						if (bs->cur_ps.weaponstate != WEAPON_CHARGING ||
							bs->altChargeTime > (level.time - bs->cur_ps.weaponChargeTime))
						{
							bs->doAttack = 1;
						}
					}
					else
					{
						bs->doAttack = 1;
					}
				}

				if (secFire == 2)
				{ //released a charge
					return 1;
				}
			}
		}
	}

	return 0;
}

//we messed up and got off the normal path, let's fall
//back to jumping around and turning in random
//directions off walls to see if we can get back to a
//good place.
int BotFallbackNavigation(bot_state_t *bs)
{
	vec3_t b_angle, fwd, trto, mins, maxs;
	trace_t tr;

	if (!NewBotAI_HasWaypointNavigation())
	{
		return 0;
	}

	if (NewBotAI_HasValidCurrentEnemy(bs))
	{
		return 2; //we're busy
	}

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = 0;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 32;

	bs->goalAngles[PITCH] = 0;
	bs->goalAngles[ROLL] = 0;

	VectorCopy(bs->goalAngles, b_angle);

	AngleVectors(b_angle, fwd, NULL, NULL);

	trto[0] = bs->origin[0] + fwd[0]*16;
	trto[1] = bs->origin[1] + fwd[1]*16;
	trto[2] = bs->origin[2] + fwd[2]*16;

	JP_Trace(&tr, bs->origin, mins, maxs, trto, bs->client, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction == 1)
	{
		VectorCopy(trto, bs->goalPosition);
		return 1; //success!
	}
	else
	{
		float baseYaw = bs->goalAngles[YAW];
		float probeYaw[3];
		const qboolean preferLeftFirst = ((bs->customNavReverseTime++ & 1) == 0) ? qtrue : qfalse;
		int i;
		vec3_t desiredDelta;

		VectorSubtract(bs->goalPosition, bs->origin, desiredDelta);
		desiredDelta[2] = 0.0f;
		if (VectorLengthSquared(desiredDelta) > 1.0f)
		{
			baseYaw = vectoyaw(desiredDelta);
		}

		probeYaw[0] = AngleNormalize360(baseYaw + (preferLeftFirst ? 90.0f : -90.0f));
		probeYaw[1] = AngleNormalize360(baseYaw + (preferLeftFirst ? -90.0f : 90.0f));
		probeYaw[2] = AngleNormalize360(baseYaw + 180.0f);

		for (i = 0; i < 3; i++)
		{
			bs->goalAngles[YAW] = probeYaw[i];
			VectorCopy(bs->goalAngles, b_angle);
			AngleVectors(b_angle, fwd, NULL, NULL);
			trto[0] = bs->origin[0] + fwd[0]*48;
			trto[1] = bs->origin[1] + fwd[1]*48;
			trto[2] = bs->origin[2];
			JP_Trace(&tr, bs->origin, mins, maxs, trto, bs->client, MASK_SOLID, qfalse, 0, 0);
			if (tr.fraction == 1.0f)
			{
				VectorCopy(trto, bs->goalPosition);
				return 1;
			}
		}

		return 0;
	}
}

int BotTryAnotherWeapon(bot_state_t *bs)
{ //out of ammo, resort to the first weapon we come across that has ammo
	int i;

	i = 1;

	while (i < WP_NUM_WEAPONS)
	{
		if (bs->cur_ps.ammo[weaponData[i].ammoIndex] >= weaponData[i].energyPerShot &&
			(bs->cur_ps.stats[STAT_WEAPONS] & (1 << i)))
		{
			bs->virtualWeapon = i;
			BotSelectWeapon(bs->client, i);
			//bs->cur_ps.weapon = i;
			//level.clients[bs->client].ps.weapon = i;
			return 1;
		}

		i++;
	}

	if (bs->cur_ps.weapon != 1 && bs->virtualWeapon != 1)
	{ //should always have this.. shouldn't we?
		bs->virtualWeapon = 1;
		BotSelectWeapon(bs->client, 1);
		//bs->cur_ps.weapon = 1;
		//level.clients[bs->client].ps.weapon = 1;
		return 1;
	}

	return 0;
}

//is this weapon available to us?
qboolean BotWeaponSelectable(bot_state_t *bs, int weapon)
{
	if (weapon == WP_NONE)
	{
		return qfalse;
	}

	if (bs->cur_ps.ammo[weaponData[weapon].ammoIndex] >= weaponData[weapon].energyPerShot &&
		(bs->cur_ps.stats[STAT_WEAPONS] & (1 << weapon)))
	{
		return qtrue;
	}

	return qfalse;
}

qboolean BotWeaponSelectableAltFire(bot_state_t *bs, int weapon)
{
	if (weapon == WP_NONE)
	{
		return qfalse;
	}

	if (bs->cur_ps.ammo[weaponData[weapon].ammoIndex] >= weaponData[weapon].altEnergyPerShot &&
		(bs->cur_ps.stats[STAT_WEAPONS] & (1 << weapon)))
	{
		return qtrue;
	}
	
	return qfalse;
}

//select the best weapon we can
int BotSelectIdealWeapon(bot_state_t *bs)
{
	int i;
	int bestweight = -1;
	int bestweapon = 0;

	i = 0;

	if (g_newBotAI.integer && (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))) { //always use saber for new bot.. sad hack
		BotSelectWeapon(bs->client, WP_SABER);
		return 0;
	}

	while (i < WP_NUM_WEAPONS)
	{
		if (bs->cur_ps.ammo[weaponData[i].ammoIndex] >= weaponData[i].energyPerShot &&
			bs->botWeaponWeights[i] > bestweight &&
			(bs->cur_ps.stats[STAT_WEAPONS] & (1 << i)))
		{
			if (i == WP_THERMAL)
			{ //special case..
				if (bs->currentEnemy && bs->frame_Enemy_Len < 700)
				{
					bestweight = bs->botWeaponWeights[i];
					bestweapon = i;
				}
			}
			else
			{
				bestweight = bs->botWeaponWeights[i];
				bestweapon = i;
			}
		}

		i++;
	}

	if ( bs->currentEnemy && bs->frame_Enemy_Len < 300 &&
		bestweapon == WP_BRYAR_PISTOL &&
		(bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER)) )
	{
		bestweapon = WP_SABER;
		bestweight = 1;
	}

	if ( bs->currentEnemy )
	{
		if (bs->frame_Enemy_Len > 1000)
		{
			if (BotWeaponSelectable(bs, WP_DISRUPTOR))
			{
				bestweapon = WP_DISRUPTOR;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_DEMP2))
			{
				bestweapon = WP_DEMP2;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_BLASTER))
			{
				bestweapon = WP_BLASTER;
				bestweight = 1;
			}
		}
		else if (bs->frame_Enemy_Len > 300 && bs->frame_Enemy_Len < 900)
		{
			if (BotWeaponSelectableAltFire(bs, WP_BLASTER))
			{
				bestweapon = WP_BLASTER;
				bestweight = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_REPEATER))
			{
				bestweapon = WP_REPEATER;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
			{
				bestweapon = WP_DISRUPTOR;
				bestweight = 1;
			}
		}
		else if (bs->frame_Enemy_Len < 150)
		{
			if (BotWeaponSelectable(bs, WP_FLECHETTE))
			{
				bestweapon = WP_FLECHETTE;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_REPEATER))
			{
				bestweapon = WP_REPEATER;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER))
			{
				bestweapon = WP_ROCKET_LAUNCHER;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION))
			{
				bestweapon = WP_CONCUSSION;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_BLASTER))
			{
				bestweapon = WP_BLASTER;
				bestweight = 1;
			}
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
			{
				bestweapon = WP_DISRUPTOR;
				bestweight = 1;
			}
		}
	}

	//assert(bs->cur_ps.weapon > 0 && bestweapon > 0);

	if (bestweight != -1 && bs->cur_ps.weapon != bestweapon && bs->virtualWeapon != bestweapon)
	{
		bs->virtualWeapon = bestweapon;
		BotSelectWeapon(bs->client, bestweapon);
		//bs->cur_ps.weapon = bestweapon;
		//level.clients[bs->client].ps.weapon = bestweapon;
		return 1;
	}

	//assert(bs->cur_ps.weapon > 0);

	return 0;
}

//check/select the chosen weapon
int BotSelectChoiceWeapon(bot_state_t *bs, int weapon, int doselection)
{ //if !doselection then bot will only check if he has the specified weapon and return 1 (yes) or 0 (no)
	int i;
	int hasit = 0;

	i = 0;

	while (i < WP_NUM_WEAPONS)
	{
		if (bs->cur_ps.ammo[weaponData[i].ammoIndex] > weaponData[i].energyPerShot &&
			i == weapon &&
			(bs->cur_ps.stats[STAT_WEAPONS] & (1 << i)))
		{
			hasit = 1;
			break;
		}

		i++;
	}

	if (hasit && bs->cur_ps.weapon != weapon && doselection && bs->virtualWeapon != weapon)
	{
		bs->virtualWeapon = weapon;
		BotSelectWeapon(bs->client, weapon);
		//bs->cur_ps.weapon = weapon;
		//level.clients[bs->client].ps.weapon = weapon;
		return 2;
	}

	if (hasit)
	{
		return 1;
	}

	return 0;
}

//override our standard weapon choice with a melee weapon
int BotSelectMelee(bot_state_t *bs)
{
	if (bs->cur_ps.weapon != 1 && bs->virtualWeapon != 1)
	{
		bs->virtualWeapon = 1;
		BotSelectWeapon(bs->client, 1);
		//bs->cur_ps.weapon = 1;
		//level.clients[bs->client].ps.weapon = 1;
		return 1;
	}

	return 0;
}

//See if we our in love with the potential bot.
int GetLoveLevel(bot_state_t *bs, bot_state_t *love)
{
	int i = 0;
	const char *lname = NULL;

	if (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
	{ //There is no love in 1-on-1
		return 0;
	}

	if (!bs || !love || !g_entities[love->client].client)
	{
		return 0;
	}

	if (!bs->lovednum)
	{
		return 0;
	}

	if (!bot_attachments.integer)
	{
		return 1;
	}

	lname = g_entities[love->client].client->pers.netname;

	if (!lname)
	{
		return 0;
	}

	while (i < bs->lovednum)
	{
		if (strcmp(bs->loved[i].name, lname) == 0)
		{
			return bs->loved[i].level;
		}

		i++;
	}

	return 0;
}

//Our loved one was killed. We must become infuriated!
void BotLovedOneDied(bot_state_t *bs, bot_state_t *loved, int lovelevel)
{
	if (!loved->lastHurt || !loved->lastHurt->client ||
		loved->lastHurt->s.number == loved->client)
	{
		return;
	}

	if (level.gametype == GT_DUEL || level.gametype == GT_POWERDUEL)
	{ //There is no love in 1-on-1
		return;
	}

	if (!IsTeamplay())
	{
		if (lovelevel < 2)
		{
			return;
		}
	}
	else if (OnSameTeam(&g_entities[bs->client], loved->lastHurt))
	{ //don't hate teammates no matter what
		return;
	}

	if (loved->client == loved->lastHurt->s.number)
	{
		return;
	}

	if (bs->client == loved->lastHurt->s.number)
	{ //oops!
		return;
	}

	if (!bot_attachments.integer)
	{
		return;
	}

	if (!PassLovedOneCheck(bs, loved->lastHurt))
	{ //a loved one killed a loved one.. you cannot hate them
		bs->chatObject = loved->lastHurt;
		bs->chatAltObject = &g_entities[loved->client];
		BotDoChat(bs, "LovedOneKilledLovedOne", 0);
		return;
	}

	if (bs->revengeEnemy == loved->lastHurt)
	{
		if (bs->revengeHateLevel < bs->loved_death_thresh)
		{
			bs->revengeHateLevel++;

			if (bs->revengeHateLevel == bs->loved_death_thresh)
			{
				//broke into the highest anger level
				//CHAT: Hatred section
				bs->chatObject = loved->lastHurt;
				bs->chatAltObject = NULL;
				BotDoChat(bs, "Hatred", 1);
			}
		}
	}
	else if (bs->revengeHateLevel < bs->loved_death_thresh-1)
	{ //only switch hatred if we don't hate the existing revenge-enemy too much
		//CHAT: BelovedKilled section
		bs->chatObject = &g_entities[loved->client];
		bs->chatAltObject = loved->lastHurt;
		BotDoChat(bs, "BelovedKilled", 0);
		bs->revengeHateLevel = 0;
		bs->revengeEnemy = loved->lastHurt;
	}
}

void BotDeathNotify(bot_state_t *bs)
{ //in case someone has an emotional attachment to us, we'll notify them
	int i = 0;
	int ltest = 0;

	while (i < MAX_CLIENTS)
	{
		if (botstates[i] && botstates[i]->lovednum)
		{
			ltest = 0;
			while (ltest < botstates[i]->lovednum)
			{
				if (strcmp(level.clients[bs->client].pers.netname, botstates[i]->loved[ltest].name) == 0)
				{
					BotLovedOneDied(botstates[i], bs, botstates[i]->loved[ltest].level);
					break;
				}

				ltest++;
			}
		}

		i++;
	}
}

//perform strafe trace checks
void StrafeTracing(bot_state_t *bs)
{
	vec3_t mins, maxs;
	vec3_t right, rorg, moveDir;
	trace_t tr;

	mins[0] = -15;
	mins[1] = -15;
	//mins[2] = -24;
	mins[2] = -22;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 32;

	AngleVectors(bs->viewangles, NULL, right, NULL);

	if (bs->meleeStrafeDir < 0)
	{
		rorg[0] = bs->origin[0] - right[0]*32;
		rorg[1] = bs->origin[1] - right[1]*32;
		rorg[2] = bs->origin[2] - right[2]*32;
	}
	else
	{
		rorg[0] = bs->origin[0] + right[0]*32;
		rorg[1] = bs->origin[1] + right[1]*32;
		rorg[2] = bs->origin[2] + right[2]*32;
	}

	JP_Trace(&tr, bs->origin, mins, maxs, rorg, bs->client, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction != 1)
	{
		bs->meleeStrafeDisable = level.time + Q_irand(500, 1500);
	}

	VectorSubtract(rorg, bs->origin, moveDir);
	moveDir[2] = 0.0f;
	if (VectorNormalize(moveDir) > 0.0f &&
		BotNav_CheckFallingHazard(bs, moveDir, qtrue))
	{ //Only suppress combat strafes for genuinely dangerous drops, not short ledges.
		bs->meleeStrafeDisable = level.time + Q_irand(500, 1500);
	}
}

//doing primary weapon fire
int PrimFiring(bot_state_t *bs)
{
	if (bs->cur_ps.weaponstate != WEAPON_CHARGING &&
		bs->doAttack)
	{
		return 1;
	}

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING &&
		!bs->doAttack)
	{
		return 1;
	}

	return 0;
}

//should we keep our primary weapon from firing?
int KeepPrimFromFiring(bot_state_t *bs)
{
	if (bs->cur_ps.weaponstate != WEAPON_CHARGING &&
		bs->doAttack)
	{
		bs->doAttack = 0;
	}

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING &&
		!bs->doAttack)
	{
		bs->doAttack = 1;
	}

	return 0;
}

//doing secondary weapon fire
int AltFiring(bot_state_t *bs)
{
	if (bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT &&
		bs->doAltAttack)
	{
		return 1;
	}

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT &&
		!bs->doAltAttack)
	{
		return 1;
	}

	return 0;
}

//should we keep our alt from firing?
int KeepAltFromFiring(bot_state_t *bs)
{
	if (bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT &&
		bs->doAltAttack)
	{
		bs->doAltAttack = 0;
	}

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT &&
		!bs->doAltAttack)
	{
		bs->doAltAttack = 1;
	}

	return 0;
}

//Try not to shoot our friends in the back. Or in the face. Or anywhere, really.
gentity_t *CheckForFriendInLOF(bot_state_t *bs)
{
	vec3_t fwd;
	vec3_t trfrom, trto;
	vec3_t mins, maxs;
	gentity_t *trent;
	trace_t tr;

	mins[0] = -3;
	mins[1] = -3;
	mins[2] = -3;

	maxs[0] = 3;
	maxs[1] = 3;
	maxs[2] = 3;

	AngleVectors(bs->viewangles, fwd, NULL, NULL);

	VectorCopy(bs->eye, trfrom);

	trto[0] = trfrom[0] + fwd[0]*2048;
	trto[1] = trfrom[1] + fwd[1]*2048;
	trto[2] = trfrom[2] + fwd[2]*2048;

	JP_Trace(&tr, trfrom, mins, maxs, trto, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction != 1 && tr.entityNum <= MAX_CLIENTS)
	{
		trent = &g_entities[tr.entityNum];

		if (trent && trent->client)
		{
			if (IsTeamplay() && OnSameTeam(&g_entities[bs->client], trent))
			{
				return trent;
			}

			if (botstates[trent->s.number] && GetLoveLevel(bs, botstates[trent->s.number]) > 1)
			{
				return trent;
			}
		}
	}

	return NULL;
}

void BotScanForLeader(bot_state_t *bs)
{ //bots will only automatically obtain a leader if it's another bot using this method.
	int i = 0;
	gentity_t *ent;

	if (bs->isSquadLeader)
	{
		return;
	}

	while (i < MAX_CLIENTS)
	{
		ent = &g_entities[i];

		if (ent && ent->client && botstates[i] && botstates[i]->isSquadLeader && bs->client != i)
		{
			if (OnSameTeam(&g_entities[bs->client], ent))
			{
				bs->squadLeader = ent;
				break;
			}
			if (GetLoveLevel(bs, botstates[i]) > 1 && !IsTeamplay())
			{ //ignore love status regarding squad leaders if we're in teamplay
				bs->squadLeader = ent;
				break;
			}
		}

		i++;
	}
}

//w3rd to the p33pz.
void BotReplyGreetings(bot_state_t *bs)
{
	int i = 0;
	int numhello = 0;

	while (i < MAX_CLIENTS)
	{
		if (botstates[i] &&
			botstates[i]->canChat &&
			i != bs->client)
		{
			botstates[i]->chatObject = &g_entities[bs->client];
			botstates[i]->chatAltObject = NULL;
			if (BotDoChat(botstates[i], "ResponseGreetings", 0))
			{
				numhello++;
			}
		}

		if (numhello > 3)
		{ //don't let more than 4 bots say hello at once
			return;
		}

		i++;
	}
}

//try to move in to grab a nearby flag
void CTFFlagMovement(bot_state_t *bs)
{
	int diddrop = 0;
	gentity_t *desiredDrop = NULL;
	vec3_t a, mins, maxs;
	trace_t tr;

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = -7;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 7;

	if (bs->wantFlag && (bs->wantFlag->flags & FL_DROPPED_ITEM))
	{
		if (bs->staticFlagSpot[0] == bs->wantFlag->s.pos.trBase[0] &&
			bs->staticFlagSpot[1] == bs->wantFlag->s.pos.trBase[1] &&
			bs->staticFlagSpot[2] == bs->wantFlag->s.pos.trBase[2])
		{
			VectorSubtract(bs->origin, bs->wantFlag->s.pos.trBase, a);

			if (VectorLength(a) <= BOT_FLAG_GET_DISTANCE)
			{
				VectorCopy(bs->wantFlag->s.pos.trBase, bs->goalPosition);
				return;
			}
			else
			{
				bs->wantFlag = NULL;
			}
		}
		else
		{
			bs->wantFlag = NULL;
		}
	}
	else if (bs->wantFlag)
	{
		bs->wantFlag = NULL;
	}

	if (flagRed && flagBlue)
	{
		if (bs->wpDestination == flagRed ||
			bs->wpDestination == flagBlue)
		{
			if (bs->wpDestination == flagRed && droppedRedFlag && (droppedRedFlag->flags & FL_DROPPED_ITEM) && droppedRedFlag->classname && strcmp(droppedRedFlag->classname, "freed") != 0)
			{
				desiredDrop = droppedRedFlag;
				diddrop = 1;
			}
			if (bs->wpDestination == flagBlue && droppedBlueFlag && (droppedBlueFlag->flags & FL_DROPPED_ITEM) && droppedBlueFlag->classname && strcmp(droppedBlueFlag->classname, "freed") != 0)
			{
				desiredDrop = droppedBlueFlag;
				diddrop = 1;
			}

			if (diddrop && desiredDrop)
			{
				VectorSubtract(bs->origin, desiredDrop->s.pos.trBase, a);

				if (VectorLength(a) <= BOT_FLAG_GET_DISTANCE)
				{
					JP_Trace(&tr, bs->origin, mins, maxs, desiredDrop->s.pos.trBase, bs->client, MASK_SOLID, qfalse, 0, 0);

					if (tr.fraction == 1 || tr.entityNum == desiredDrop->s.number)
					{
						VectorCopy(desiredDrop->s.pos.trBase, bs->goalPosition);
						VectorCopy(desiredDrop->s.pos.trBase, bs->staticFlagSpot);
						return;
					}
				}
			}
		}
	}
}

//see if we want to make our detpacks blow up
void BotCheckDetPacks(bot_state_t *bs)
{
	gentity_t *dp = NULL;
	gentity_t *myDet = NULL;
	vec3_t a;
	float enLen;
	float myLen;

	while ( (dp = G_Find( dp, FOFS(classname), "detpack") ) != NULL )
	{
		if (dp && dp->parent && dp->parent->s.number == bs->client)
		{
			myDet = dp;
			break;
		}
	}

	if (!myDet)
	{
		return;
	}

	if (!bs->currentEnemy || !bs->currentEnemy->client || !bs->frame_Enemy_Vis)
	{ //require the enemy to be visilbe just to be fair..

		//unless..
		if (bs->currentEnemy && bs->currentEnemy->client &&
			(level.time - bs->plantContinue) < 5000)
		{ //it's a fresh plant (within 5 seconds) so we should be able to guess
			goto stillmadeit;
		}
		return;
	}

stillmadeit:

	VectorSubtract(bs->currentEnemy->client->ps.origin, myDet->s.pos.trBase, a);
	enLen = VectorLength(a);

	VectorSubtract(bs->origin, myDet->s.pos.trBase, a);
	myLen = VectorLength(a);

	if (enLen > myLen)
	{
		return;
	}

	if (enLen < BOT_PLANT_BLOW_DISTANCE && OrgVisible(bs->currentEnemy->client->ps.origin, myDet->s.pos.trBase, bs->currentEnemy->s.number))
	{ //we could just call the "blow all my detpacks" function here, but I guess that's cheating.
		bs->plantKillEmAll = level.time + 500;
	}
}

//see if it would be beneficial at this time to use one of our inv items
int BotUseInventoryItem(bot_state_t *bs)
{
	if (bs->cur_ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_MEDPAC))
	{
		if (g_entities[bs->client].health <= 75)
		{
			bs->cur_ps.stats[STAT_HOLDABLE_ITEM] = BG_GetItemIndexByTag(HI_MEDPAC, IT_HOLDABLE);
			goto wantuseitem;
		}
	}
	if (bs->cur_ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_MEDPAC_BIG))
	{
		if (g_entities[bs->client].health <= 50)
		{
			bs->cur_ps.stats[STAT_HOLDABLE_ITEM] = BG_GetItemIndexByTag(HI_MEDPAC_BIG, IT_HOLDABLE);
			goto wantuseitem;
		}
	}
	if (bs->cur_ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SEEKER))
	{
		if (bs->currentEnemy && bs->frame_Enemy_Vis)
		{
			bs->cur_ps.stats[STAT_HOLDABLE_ITEM] = BG_GetItemIndexByTag(HI_SEEKER, IT_HOLDABLE);
			goto wantuseitem;
		}
	}
	if (bs->cur_ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SENTRY_GUN))
	{
		if (bs->currentEnemy && bs->frame_Enemy_Vis)
		{
			bs->cur_ps.stats[STAT_HOLDABLE_ITEM] = BG_GetItemIndexByTag(HI_SENTRY_GUN, IT_HOLDABLE);
			goto wantuseitem;
		}
	}
	if (bs->cur_ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_SHIELD))
	{
		if (bs->currentEnemy && bs->frame_Enemy_Vis && bs->runningToEscapeThreat)
		{ //this will (hopefully) result in the bot placing the shield down while facing
		  //the enemy and running away
			bs->cur_ps.stats[STAT_HOLDABLE_ITEM] = BG_GetItemIndexByTag(HI_SHIELD, IT_HOLDABLE);
			goto wantuseitem;
		}
	}

	return 0;

wantuseitem:
	level.clients[bs->client].ps.stats[STAT_HOLDABLE_ITEM] = bs->cur_ps.stats[STAT_HOLDABLE_ITEM];

	return 1;
}

//trace forward to see if we can plant a detpack or something
int BotSurfaceNear(bot_state_t *bs)
{
	trace_t tr;
	vec3_t fwd;

	AngleVectors(bs->viewangles, fwd, NULL, NULL);

	fwd[0] = bs->origin[0]+(fwd[0]*64);
	fwd[1] = bs->origin[1]+(fwd[1]*64);
	fwd[2] = bs->origin[2]+(fwd[2]*64);

	JP_Trace(&tr, bs->origin, NULL, NULL, fwd, bs->client, MASK_SOLID, qfalse, 0, 0);

	if (tr.fraction != 1)
	{
		return 1;
	}

	return 0;
}

//could we block projectiles from the weapon potentially with a light saber?
int BotWeaponBlockable(int weapon)
{
	switch (weapon)
	{
	case WP_STUN_BATON:
	case WP_MELEE:
		return 0;
	case WP_DISRUPTOR:
		return 0;
	case WP_DEMP2:
		return 0;
	case WP_ROCKET_LAUNCHER:
		return 0;
	case WP_THERMAL:
		return 0;
	case WP_TRIP_MINE:
		return 0;
	case WP_DET_PACK:
		return 0;
	default:
		return 1;
	}
}

void Cmd_EngageDuel_f(gentity_t *ent, int dueltype);
void Cmd_ToggleSaber_f(gentity_t *ent);

//movement overrides
void Bot_SetForcedMovement(int bot, int forward, int right, int up)
{
	bot_state_t *bs;

	bs = botstates[bot];

	if (!bs)
	{ //not a bot
		return;
	}

	if (forward != -1)
	{
		if (bs->forceMove_Forward)
		{
			bs->forceMove_Forward = 0;
		}
		else
		{
			bs->forceMove_Forward = forward;
		}
	}
	if (right != -1)
	{
		if (bs->forceMove_Right)
		{
			bs->forceMove_Right = 0;
		}
		else
		{
			bs->forceMove_Right = right;
		}
	}
	if (up != -1)
	{
		if (bs->forceMove_Up)
		{
			bs->forceMove_Up = 0;
		}
		else
		{
			bs->forceMove_Up = up;
		}
	}
}

void NewBotAI_GetStrafeAim(bot_state_t *bs)
{
	vec3_t headlevel, a, ang;
	float optimalAngle, newAngle = 0, frameTime = 0.008f;
	const float baseSpeed = bs->cur_ps.speed, currentSpeed = sqrt((bs->cur_ps.velocity[0] * bs->cur_ps.velocity[0]) + (bs->cur_ps.velocity[1] * bs->cur_ps.velocity[1]));

	VectorCopy(bs->currentEnemy->client->ps.origin, headlevel);

	if (bs->currentEnemy->client)
		headlevel[2] += bs->currentEnemy->client->ps.viewheight - 24;//aim at chest?

	VectorSubtract(headlevel, bs->eye, a);
	vectoangles(a, ang);
	VectorCopy(ang, bs->goalAngles);

	//Treat angle to them as our base angle.. add offset angle to that to accel?

	optimalAngle = acos((double) ((baseSpeed - (baseSpeed * frameTime)) / currentSpeed)) * (180.0f/M_PI) - 45.0f; 

	optimalAngle += 1.0f + bot_strafeOffset.value; //Ayy

	if (optimalAngle < 0 || optimalAngle > 360)
		optimalAngle = 0;

	if (bs->forceMove_Forward) {
		if (bs->forceMove_Right < 0)
			newAngle = optimalAngle; //WA
		else if (bs->forceMove_Right > 0)
			newAngle = -optimalAngle; //WD
	}
	else {
		if (bs->forceMove_Right < 0)
			newAngle = 45.0f - optimalAngle;//D
		else if (bs->forceMove_Right > 0)
			newAngle = -(45.0f - optimalAngle);//A
	}

	//trap->Print("Current: %f, Dir: %f, New: %f\n", bs->goalAngles[YAW],  moveAngles[YAW], newAngle);

	bs->goalAngles[YAW] = bs->aimOffsetAmtYaw + newAngle;

	VectorCopy(bs->goalAngles, bs->ideal_viewangles);
	//trap_EA_View(bs->client, bs->goalAngles); // if we want instant aim?
}

static void NewBotAI_ApplyFanAttackWobble(bot_state_t *bs)
{
	usercmd_t *cmd;
	qboolean fanActive;
	qboolean attackHeld;
	float yawAmplitude;
	float pitchAmplitude;
	float speed;
	int wobbleDelayMs;
	float elapsedMs;
	float yawOffset;
	float pitchOffset;

	if (!bs)
	{
		return;
	}

	if (bs->saberTechniqueCandidate || bs->saberDefenseActive ||
		bs->cur_ps.saberBlocked == BLOCKED_PARRY_BROKEN ||
		PM_SaberInBrokenParry(bs->cur_ps.saberMove))
	{
		bs->fanWobbleStartTime = 0;
		return;
	}
	fanActive = (bs->cur_ps.weapon == WP_SABER && bs->fanPhase != FAN_PHASE_INACTIVE) ? qtrue : qfalse;
	if (!fanActive)
	{
		bs->fanWobbleStartTime = 0;
		return;
	}

	cmd = &level.clients[bs->client].pers.cmd;
	attackHeld = (BG_SaberInAttack(bs->cur_ps.saberMove) &&
		bs->cur_ps.weaponTime > 0 && bs->cur_ps.saberBlocked == BLOCKED_NONE &&
		(bs->doAttack || (cmd->buttons & BUTTON_ATTACK))) ? qtrue : qfalse;
	if (!attackHeld)
	{
		bs->fanWobbleStartTime = 0;
		return;
	}

	if (bs->fanWobbleStartTime <= 0)
	{
		bs->fanWobbleStartTime = level.time;
	}

	wobbleDelayMs = Com_Clampi(0, 2000, bot_wobbledelay.integer);
	elapsedMs = (float)(level.time - bs->fanWobbleStartTime);
	yawAmplitude = Com_Clamp(0.0f, 45.0f, bot_wobbleyaw.value);
	pitchAmplitude = Com_Clamp(0.0f, 20.0f, bot_wobblepitch.value);
	speed = Com_Clamp(0.0f, 20.0f, bot_wobblespeed.value);
	if (bs->fanPackage == FAN_PACKAGE_STAFF_PRESSURE)
	{
		yawAmplitude *= 1.15f;
		pitchAmplitude *= 0.45f;
	}
	else if (bs->fanPackage == FAN_PACKAGE_YELLOW_PRESSURE)
	{
		yawAmplitude *= 0.85f;
		pitchAmplitude *= 0.3f;
	}
	if (bs->frame_Enemy_Len < 128.0f)
	{
		yawAmplitude *= 0.55f;
		pitchAmplitude *= 0.25f;
	}
	else if (bs->frame_Enemy_Len < 196.0f)
	{
		yawAmplitude *= 0.75f;
		pitchAmplitude *= 0.5f;
	}
	if (!NewBotAI_ShouldApplyFanWobble(
		fanActive,
		attackHeld,
		(int)elapsedMs,
		wobbleDelayMs,
		yawAmplitude,
		pitchAmplitude,
		speed))
	{
		return;
	}

	NewBotAI_GetFanWobbleOffsets(
		elapsedMs - wobbleDelayMs,
		yawAmplitude,
		pitchAmplitude,
		speed,
		&yawOffset,
		&pitchOffset);

	// Counter-clockwise oval around the current normal aim center.
	if (yawOffset != 0.0f)
	{
		bs->goalAngles[YAW] = AngleNormalize360(bs->goalAngles[YAW] + yawOffset);
	}
	if (pitchOffset != 0.0f)
	{
		bs->goalAngles[PITCH] = AngleNormalize180(bs->goalAngles[PITCH] + pitchOffset);
	}
}

static int NewBotAI_GetHorizontalSwingSweepDir(int saberMove)
{
	return BG_SaberHorizontalSweepDir(saberMove);
}

static void NewBotAI_ApplyRelativeAimPointOffset(bot_state_t *bs, float sideOffsetUnits, float heightOffset)
{
	vec3_t centerPoint, adjustedPoint, toEnemy, lateral;
	vec3_t centerAim, adjustedAim;
	float len;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client || sideOffsetUnits == 0.0f)
		return;

	VectorCopy(bs->currentEnemy->client->ps.origin, centerPoint);
	centerPoint[2] += bs->currentEnemy->client->ps.viewheight - 16 + heightOffset;
	VectorSubtract(centerPoint, bs->eye, toEnemy);
	toEnemy[2] = 0.0f;
	len = VectorLength(toEnemy);
	if (len <= 1.0f)
		return;

	VectorScale(toEnemy, 1.0f / len, toEnemy);
	lateral[0] = -toEnemy[1];
	lateral[1] = toEnemy[0];
	lateral[2] = 0.0f;

	VectorMA(centerPoint, sideOffsetUnits, lateral, adjustedPoint);
	VectorSubtract(centerPoint, bs->eye, centerAim);
	VectorSubtract(adjustedPoint, bs->eye, adjustedAim);
	vectoangles(centerAim, centerAim);
	vectoangles(adjustedAim, adjustedAim);

	bs->goalAngles[YAW] = AngleNormalize360(bs->goalAngles[YAW] +
		AngleSubtract(adjustedAim[YAW], centerAim[YAW]));
	bs->goalAngles[PITCH] = AngleNormalize180(bs->goalAngles[PITCH] +
		AngleSubtract(adjustedAim[PITCH], centerAim[PITCH]));
}

// Yaw offset (degrees) held while approaching a saber opponent without attacking.
static float NewBotAI_GetApproachAimYawOffset(bot_state_t *bs)
{
	playerState_t *ps;
	vec3_t diff;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
		return 0.0f;
	ps = &bs->cur_ps;
	if (ps->weapon != WP_SABER || ps->saberInFlight || ps->weaponTime > 0 ||
		BG_SaberInAttack(ps->saberMove) || PM_SaberInStart(ps->saberMove) ||
		PM_SaberInTransition(ps->saberMove) || bs->doAttack)
		return 0.0f;
	VectorSubtract(bs->currentEnemy->client->ps.origin, ps->origin, diff);
	diff[2] = 0.0f;
	return NewBotAI_GetApproachAimOffset(bs->settings.skill, VectorLength(diff),
		bs->saberTacticStrafeDir ? bs->saberTacticStrafeDir : 1);
}

static void NewBotAI_ApplyHumanSwingAimOffset(bot_state_t *bs)
{
	int sweepDir;
	float sideOffsetUnits;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
		return;
	if (bs->cur_ps.weapon != WP_SABER || bs->cur_ps.saberInFlight)
		return;
	if (bs->frame_Enemy_Len <= 0.0f || bs->frame_Enemy_Len > 220.0f)
		return;
	if (!BG_SaberInAttack(bs->cur_ps.saberMove))
	{
		//Approaching without attacking: hold aim 10-15 degrees off centre like humans.
		bs->goalAngles[YAW] = AngleNormalize360(bs->goalAngles[YAW] +
			NewBotAI_GetApproachAimYawOffset(bs));
		return;
	}

	sweepDir = NewBotAI_GetHorizontalSwingSweepDir(bs->cur_ps.saberMove);
	if (!sweepDir)
		return;

	sideOffsetUnits = 24.0f;
	if (bs->cur_ps.fd.saberAnimLevel == SS_STAFF)
		sideOffsetUnits = 32.0f;
	else if (bs->cur_ps.fd.saberAnimLevel == SS_STRONG)
		sideOffsetUnits = 20.0f;

	if (bs->frame_Enemy_Len < 96.0f)
		sideOffsetUnits *= 1.15f;
	else if (bs->frame_Enemy_Len > 176.0f)
		sideOffsetUnits *= 0.75f;

	/* Sweep direction is opposite the side the saber starts on:
	 * a leftward swing (R2L) starts from the right, so offset the aim right first. */
	NewBotAI_ApplyRelativeAimPointOffset(bs, -((float)sweepDir) * sideOffsetUnits, 0.0f);
}

// Swing-tracking yaw sweep: during a horizontal swing turn with the blade across the target
// (L2R turns right, R2L left), 100-160 degrees per ~300ms swing at high skill like the human
// fan chains, crossing the target mid-swing when hits land. Must run right after the aim was
// rewritten from the enemy position because the offset is absolute. Returns qtrue if applied.
static qboolean NewBotAI_ApplySwingSweepYaw(bot_state_t *bs)
{
	const int move = bs ? bs->cur_ps.saberMove : LS_NONE;
	int sweepDir;
	float dist2D;
	float halfArc;
	float offset;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
		return qfalse;
	if (bs->cur_ps.weapon != WP_SABER || bs->cur_ps.saberInFlight)
	{
		bs->fanSweepMove = 0;
		return qfalse;
	}
	sweepDir = NewBotAI_GetHorizontalSwingSweepDir(move);
	if (!sweepDir || !BG_SaberInAttack(move))
	{
		bs->fanSweepMove = 0;
		return qfalse;
	}
	if (move != bs->fanSweepMove)
	{
		bs->fanSweepMove = move;
		bs->fanSweepStartTime = level.time;
	}

	dist2D = NewBotAI_GetEnemyDistance2D(bs);
	if (dist2D > 220.0f)
		return qfalse;

	halfArc = NewBotAI_GetFanSweepHalfArc(bs->settings.skill,
		BotGetChanceBiasPercent(bot_fanbias.value), dist2D);
	if (bs->fanPhase == FAN_PHASE_INACTIVE)
	{
		//Single horizontal swings outside a fan chain sweep less.
		halfArc *= 0.6f;
	}
	offset = NewBotAI_GetFanSweepOffset(halfArc, sweepDir,
		level.time - bs->fanSweepStartTime, NEWBOTAI_FAN_SWEEP_SWING_MS);
	bs->goalAngles[YAW] = AngleNormalize360(bs->goalAngles[YAW] + offset);
	return qtrue;
}

static void NewBotAI_ApplyFanDwellYaw(bot_state_t *bs, qboolean aimRewritten)
{
	float dwellElapsedMs;
	float dwellDurationMs;
	float halfDwellMs;
	float yawSpeed;
	float desiredOffset;

	if (!bs)
	{
		return;
	}
	if (aimRewritten)
	{
		//goalAngles was rebuilt from the enemy position this frame, so the previous offset
		//is no longer part of it; apply the full offset rather than the delta.
		bs->fanDwellYawOffset = 0.0f;
	}

	if (bs->fanPhase != FAN_PHASE_DWELL || !bs->fanAttackDir)
	{
		if (bs->fanDwellYawOffset != 0.0f)
		{
			bs->goalAngles[YAW] = AngleNormalize360(bs->goalAngles[YAW] - bs->fanDwellYawOffset);
		}
		bs->fanDwellYawOffset = 0.0f;
		return;
	}

	dwellDurationMs = (float)(bs->fanAttackTime - bs->fanPhaseStartTime);
	if (dwellDurationMs <= 0.0f)
	{
		if (bs->fanDwellYawOffset != 0.0f)
		{
			bs->goalAngles[YAW] = AngleNormalize360(bs->goalAngles[YAW] - bs->fanDwellYawOffset);
			bs->fanDwellYawOffset = 0.0f;
		}
		return;
	}

	dwellElapsedMs = (float)(level.time - bs->fanPhaseStartTime);
	if (dwellElapsedMs < 0.0f)
	{
		dwellElapsedMs = 0.0f;
	}
	else if (dwellElapsedMs > dwellDurationMs)
	{
		dwellElapsedMs = dwellDurationMs;
	}

	halfDwellMs = dwellDurationMs * 0.5f;
	yawSpeed = Com_Clamp(-360.0f, 360.0f, bot_fanyawspeed.value);
	if (halfDwellMs <= 0.0f || yawSpeed == 0.0f)
	{
		if (bs->fanDwellYawOffset != 0.0f)
		{
			bs->goalAngles[YAW] = AngleNormalize360(bs->goalAngles[YAW] - bs->fanDwellYawOffset);
		}
		bs->fanDwellYawOffset = 0.0f;
		return;
	}

	if (dwellElapsedMs <= halfDwellMs)
	{
		desiredOffset = yawSpeed * (dwellElapsedMs / 1000.0f);
	}
	else
	{
		desiredOffset = yawSpeed * ((dwellDurationMs - dwellElapsedMs) / 1000.0f);
	}
	desiredOffset *= (float)bs->fanAttackDir;

	bs->goalAngles[YAW] = AngleNormalize360(bs->goalAngles[YAW] +
		(desiredOffset - bs->fanDwellYawOffset));
	bs->fanDwellYawOffset = desiredOffset;
}

void NewBotAI_GetAim(bot_state_t *bs)
{
	vec3_t headlevel;
	int i = 0;
	int closestSaber = 0, saberDistance = 9999999, dist, saberOwner;
	vec3_t saberDiff;
	gentity_t *saber;
	qboolean aimRewritten = qfalse;

	bs->hitSpotted = qfalse;
	if (bs->runningLikeASissy) {
		NewBotAI_GetStrafeAim(bs);
		return;
	}

	if (!bs->currentEnemy || !bs->currentEnemy->client)
		return;

	/*
		trType_t	trType;
	int		trTime;
	int		trDuration;			// if non 0, trTime + trDuration = stop time
	vec3_t	trBase;
	vec3_t	trDelta;			// velocity, etc
	*/
	//Well we should loop through every client and see if they are saberthrowing.  Then get the closest saber to us and aim at that if its close enough.
	if (!g_entities[bs->client].client->ps.saberInFlight) {
		while (i <= MAX_CLIENTS)
		{
			if (i != bs->client && g_entities[i].client && g_entities[i].client->ps.saberInFlight && !OnSameTeam(&g_entities[bs->client], &g_entities[i]) && PassStandardEnemyChecks(bs, &g_entities[i])
				&& PassLovedOneCheck(bs, &g_entities[i]))
			{
				saber = &g_entities[g_entities[i].client->ps.saberEntityNum];

				VectorSubtract(bs->cur_ps.origin, saber->s.pos.trBase, saberDiff);
				dist = VectorLengthSquared(saberDiff);

				if (dist < saberDistance && saber->s.pos.trTime) {
					saberOwner = i;
					closestSaber = g_entities[i].client->ps.saberEntityNum;
					saberDistance = dist;
				}
			}
			i++;
		}
	}
	//Saber is inrange , AND  nearest target is far enough away OR thrower is nearest target(?
	if (saberDistance < 200*200 && (bs->frame_Enemy_Len > 200 || bs->currentEnemy->client->ps.clientNum == saberOwner)) { //Dont aim at it if theres a diff enemy in saber range?
		vec3_t a, ang;
		saber = &g_entities[closestSaber];

		//VectorCopy(saber->s.pos.trBase, headlevel);
		//BotAimLeading(bs, headlevel, bLeadAmount);

		if (saber) {
			VectorSubtract(saber->s.pos.trBase, bs->eye, a);
			vectoangles(a, ang);
			VectorCopy(ang, bs->goalAngles);
			bs->hitSpotted = qtrue;
		}
	}
	/*
	if (bs->cur_ps.weapon == WP_SABER && bs->currentEnemy->client->ps.saberInFlight && bs->frame_Enemy_Len > 200) { //Try to block saber in air
		//Go through each entity, check if saber and if owner is currentenemY? if yes aim at it..?

		gentity_t *saber = &g_entities[bs->currentEnemy->client->ps.saberEntityNum];
		VectorCopy(saber->s.pos.trBase, headlevel);

		BotAimLeading(bs, headlevel, bLeadAmount);

		g_entities[bs->client].client->pers.JAWARUN = qtrue;


	} */

	else { //Normal aim at player
		VectorCopy(bs->currentEnemy->client->ps.origin, headlevel);

		if (bs->currentEnemy && bs->currentEnemy->client)
			headlevel[2] += bs->currentEnemy->client->ps.viewheight - 16;//aim at chest?
		if (bs->cur_ps.saberInFlight)
		{
			headlevel[2] += 24;
		}
		G_NewBotAIAimLeading(bs, headlevel);
		if (bs->cur_ps.saberInFlight)
		{
			NewBotAI_AdjustSaberThrowLead(bs);
		}
		else if (bs->saberTechniqueCandidate || bs->saberDefenseActive ||
			bs->cur_ps.saberBlocked == BLOCKED_PARRY_BROKEN ||
			PM_SaberInBrokenParry(bs->cur_ps.saberMove))
		{
			// The controller applies offsets once, after inertial base aim.
		}
		else if (!NewBotAI_ApplySwingSweepYaw(bs))
		{
			NewBotAI_ApplyHumanSwingAimOffset(bs);
		}
		aimRewritten = qtrue;
	}
	NewBotAI_ApplyFanDwellYaw(bs, aimRewritten);
	NewBotAI_ApplyFanAttackWobble(bs);
	VectorCopy(bs->goalAngles, bs->ideal_viewangles);
}

float NewBotAI_GetDist(bot_state_t *bs)
{
	if (bs->currentEnemy)
	{
		vec3_t diff, eorg;
		VectorCopy(bs->currentEnemy->client->ps.origin, eorg);
		VectorSubtract(eorg, bs->eye, diff);
		return VectorLength(diff);
	}
	return 0;
}

//Milliseconds until 2 moving points are within X distance of eachother
int NewBotAI_GetTimeToInRange(bot_state_t *bs, int range, int maxTime) {
	//Get velocity of bot relative to player
	//Dotproduct that velocity with the position difference between players

	//or simulate it
	float tick = 1000/sv_fps.integer;
	int timeToInRange = 0;
	int i;
	int attempts = sv_fps.integer * maxTime * 0.001f;
	int remainder;
	vec3_t diff, pos1, pos2, vel1, vel2;

	VectorCopy(bs->cur_ps.origin, pos1);
	VectorCopy(bs->currentEnemy->client->ps.origin, pos2);
	VectorCopy(bs->cur_ps.velocity, vel1);
	VectorCopy(bs->currentEnemy->client->ps.velocity, vel2);
	range = range * range; //for vectorlength squared

	remainder = maxTime % (int)tick;
	timeToInRange += remainder;

	vel1[0] /= tick;
	vel1[1] /= tick;
	vel1[2] /= tick;

	vel2[0] /= tick;
	vel2[1] /= tick;
	vel2[2] /= tick;

	for (i=0; i<attempts; i++) { //Check up to 1 second in future?
		VectorSubtract(pos1, pos2, diff);

		VectorAdd(pos1, vel1, pos1);
		VectorAdd(pos2, vel2, pos2);

		if (VectorLengthSquared(diff) < range) {
			break;
		}

		timeToInRange += tick;
	}

	//Com_Printf("TTR is %i\n", timeToInRange);
	return timeToInRange; //If this is less than maxTime, we will be in range!

}
qboolean BG_SaberInAttack(int move);
qboolean BG_InKnockDown(int anim);
int NewBotAI_GetAbsorb(bot_state_t* bs) {
	const int ourForce = bs->cur_ps.fd.forcePower;
	const int absorbBias = (int)BotGetChanceBiasPercent(bot_absorbbias.value);
	const int healthLead = g_entities[bs->client].health - bs->currentEnemy->health;
	const qboolean airborne = (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE) ? qtrue : qfalse;
	const qboolean recentFlipkickWindow =
		(bs->lastFlipkickAttemptTime > level.time - (NEWBOTAI_ABSORB_BAIT_WINDOW_MS - 300)) ? qtrue : qfalse;
	const qboolean lightsideVsDarkside =
		(bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE &&
		 bs->currentEnemy->client->ps.fd.forceSide == FORCE_DARKSIDE) ? qtrue : qfalse;

	if (g_forcePowerDisable.integer & (1 << FP_ABSORB))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_ABSORB)))
		return 0;
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return 0;
	if (ourForce < 12)
		return 0;
	if (!bs->frame_Enemy_Vis) //Only absorb if LOS
		return 0;
	if (bs->frame_Enemy_Len > MAX_DRAIN_DISTANCE)
		return 0;

	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN) || bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_LIGHTNING)) //check if they are being hit though. not if enemy is using it?
		return 100;

	if (bs->cur_ps.weapon > WP_BRYAR_PISTOL && (bs->frame_Enemy_Len < 200)) { //Protect our guns
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB;
		return 75; //eh?
	}

	if (bs->currentEnemy->client->ps.fd.forcePower >= 20 && g_entities[bs->client].health < 30) {//PK range? and he can be pulled? and low
		if (bs->frame_Enemy_Len > 50) { //no point in absorbing if we are touching them anyway
			if (bs->frame_Enemy_Len < 256 && (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE || BG_SaberInAttack(bs->cur_ps.saberMove) || BG_InKnockDown(bs->cur_ps.legsAnim)))//can actually be pulled towards a kick so stop that
				return ourForce * 0.5f - 10;
		}
	}

	if (lightsideVsDarkside && absorbBias > 0 &&
		NewBotAI_GetAbsorbBiasBonus(
			absorbBias, airborne, recentFlipkickWindow,
			bs->frame_Enemy_Len, NEWBOTAI_IMMEDIATE_FLIPKICK_RANGE, NEWBOTAI_PULL_STUN_ONLY_RANGE) > 0)
	{
		int absorbBonus = NewBotAI_GetAbsorbBiasBonus(
			absorbBias, airborne, recentFlipkickWindow,
			bs->frame_Enemy_Len, NEWBOTAI_IMMEDIATE_FLIPKICK_RANGE, NEWBOTAI_PULL_STUN_ONLY_RANGE);
		if (healthLead > 0)
		{
			absorbBonus += (healthLead < 15) ? (healthLead / 2) : 10;
		}
		return absorbBonus;
	}

	return 0;
}

int NewBotAI_GetProtect(bot_state_t* bs) {
	const int ourForce = bs->cur_ps.fd.forcePower;
	const int totalhealth = g_entities[bs->client].health + bs->currentEnemy->client->ps.stats[STAT_ARMOR];
	//Only toggle protect on if we are about to get damaged..

	if (g_forcePowerDisable.integer & (1 << FP_PROTECT))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PROTECT)))
		return 0;
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT))
		return 0;
	if (ourForce < 12)
		return 0;

	if (bs->hitSpotted && totalhealth > 6 && (BG_SaberInAttack(bs->cur_ps.saberMove) || BG_InKnockDown(bs->cur_ps.legsAnim))) { //About to be saberthrowed and we can survive it and we can't block it
		if (totalhealth > 35) { //we could survive it anyway
			return (ourForce - 15);
		}
		return 100;
	}

	if (bs->frame_Enemy_Len < 120 && BG_SaberInAttack(bs->currentEnemy->client->ps.saberMove)) { //better way to check if we are about to take damage?
		vec3_t diff;

		VectorSubtract(bs->cur_ps.origin, bs->currentEnemy->client->saber[0].blade[0].trail.tip, diff);
		if (VectorLengthSquared(diff) > (48 * 48)) //out of range
			return (ourForce * 0.5f) - 10;
	}

	//Get nearest gun.. if (bs->currentEnemy->client->ps.weapon != WP_SABER)?  trace nearby projectiles? or is that not quick enough reaction time
	//Weigh differently if we already have absorb ?
	return 0;
}

void NewBotAI_Getup(bot_state_t *bs)
{
	qboolean useTheForce = qfalse;
	qboolean rollingEscape = qfalse;
	qboolean drainRollingEscape = qfalse;
	const int ourHealth = g_entities[bs->client].health;
	qboolean enemyIncomingSaber = qfalse;
	const qboolean imminentThrowThreat = NewBotAI_IsEnemySaberThreatImminent(bs);
	const qboolean jumpDrainThreat = NewBotAI_ShouldJumpDrainVsSaberThrow(bs);
	const qboolean enemyIncomingThrow = (bs->currentEnemy && bs->currentEnemy->client &&
		bs->currentEnemy->client->ps.saberInFlight) ? qtrue : qfalse;
	const qboolean enemyTooClose = (bs->currentEnemy && bs->currentEnemy->client &&
		bs->frame_Enemy_Len < 250) ? qtrue : qfalse;
	//A lethal inbound throw takes the getup push off the table entirely: the hand-extend
	//drops our block for the whole animation, which is exactly how these getups were dying.
	const qboolean lethalIncomingThrow = NewBotAI_IsIncomingSaberThrowLethal(bs);
	const qboolean canPushGetup = (bs->currentEnemy && bs->currentEnemy->client &&
		!lethalIncomingThrow &&
		!(g_forcePowerDisable.integer & (1 << FP_PUSH)) &&
		(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH)) &&
		bs->cur_ps.fd.forcePower >= 20 &&
		bs->frame_Enemy_Len <= 640 &&
		!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))) ? qtrue : qfalse;
	const qboolean emergencyRollEscape = (ourHealth < 31 && imminentThrowThreat && bs->frame_Enemy_Len < 220) ? qtrue : qfalse;

	//Getup rolls are a rare last-ditch escape only; default defense is jump+push.
	if (bs->currentEnemy && bs->currentEnemy->client &&
		bs->currentEnemy->client->ps.weapon == WP_SABER &&
		BG_SaberInAttack(bs->currentEnemy->client->ps.saberMove) &&
		bs->frame_Enemy_Len < 250)
	{
		enemyIncomingSaber = qtrue;
	}

	if (NewBotAI_ShouldEmergencyDrainRollSaberThrow(bs))
	{
		NewBotAI_ApplySidewaysDrainRoll(bs, qfalse);
		rollingEscape = qtrue;
		drainRollingEscape = qtrue;
	}
	else if (enemyIncomingSaber && enemyTooClose && !canPushGetup && emergencyRollEscape)
	{
		if (bs->drainRollYawStart <= 0 || bs->drainRollYawStart > level.time)
		{
			bs->drainRollDir = Q_irand(0, 1) ? 1 : -1;
			bs->drainRollYawStart = level.time;
		}
		NewBotAI_StartEscapeYawOverride(bs, NEWBOTAI_ESCAPE_YAW_OVERRIDE_MS);

		//Sideways roll away from the incoming swing: hold a lateral input (alternating so
		//we don't just run in a straight line) to trigger/steer the sideways getup roll.
		if (bs->drainRollDir < 0)
			trap->EA_MoveLeft(bs->client);
		else
			trap->EA_MoveRight(bs->client);
		rollingEscape = qtrue;
		drainRollingEscape = qfalse;
	}

	if (!rollingEscape)
	{
		trap->EA_Jump(bs->client);
	}

	if (!useTheForce && drainRollingEscape &&
		!(g_forcePowerDisable.integer & (1 << FP_DRAIN)) &&
		(bs->cur_ps.fd.forcePowersKnown & (1 << FP_DRAIN)) &&
		bs->cur_ps.fd.forcePower >= 25)
	{
		vec3_t a_fo;
		vec3_t yawTarget;
		float yawBlend;
		float yawOffset;

		level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
		useTheForce = qtrue;

		if (bs->drainRollYawStart <= 0 || bs->drainRollYawStart > level.time)
		{
			bs->drainRollYawStart = level.time;
		}

		VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
		vectoangles(a_fo, a_fo);
		yawBlend = (float)(level.time - bs->drainRollYawStart) / 200.0f;
		if (yawBlend > 1.0f)
		{
			yawBlend = 1.0f;
		}
		yawOffset = (bs->drainRollDir < 0) ? -90.0f : 90.0f;
		VectorCopy(bs->ideal_viewangles, yawTarget);
		yawTarget[YAW] = AngleNormalize360(a_fo[YAW] + (yawOffset * yawBlend));
		bs->ideal_viewangles[YAW] = yawTarget[YAW];
		bs->goalAngles[YAW] = yawTarget[YAW];
	}
	else if (!useTheForce && jumpDrainThreat)
	{
		if (!lethalIncomingThrow && NewBotAI_ShouldUseSafePushWindowWhilePulled(bs))
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
			useTheForce = qtrue;
		}
		else if ((bs->cur_ps.forceHandExtend == HANDEXTEND_FORCEPULL ||
			bs->cur_ps.powerups[PW_PULL] > level.time) &&
			!(g_forcePowerDisable.integer & (1 << FP_DRAIN)) &&
			(bs->cur_ps.fd.forcePowersKnown & (1 << FP_DRAIN)) &&
			bs->cur_ps.fd.forcePower >= 20)
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
			useTheForce = qtrue;
		}
	}
	else if (!useTheForce && canPushGetup && (enemyTooClose || enemyIncomingSaber || enemyIncomingThrow))
	{
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
		useTheForce = qtrue;
	}
	else
	{
		bs->drainRollYawStart = 0;
	}

	if (useTheForce) {
		trap->EA_ForcePower(bs->client);
	}
}

static int NewBotAI_GetEnemyTotalHealth(bot_state_t *bs)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return 0;
	}

	return bs->currentEnemy->health + bs->currentEnemy->client->ps.stats[STAT_ARMOR];
}

static qboolean NewBotAI_ShouldSuppressDrainlockSaberThrow(bot_state_t *bs)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (bs->cur_ps.fd.forceSide != FORCE_DARKSIDE)
	{
		return qfalse;
	}

	return ((NewBotAI_IsPullkickDrainWindow(bs) || NewBotAI_IsDrainlockAdvantage(bs)) &&
		NewBotAI_GetEnemyTotalHealth(bs) > 24) ? qtrue : qfalse;
}

//Force a completed saber throw costs us in practice: forcePowerNeeded is only the per-tick
//drain (10 at level 3) and it is charged repeatedly for as long as the saber is out.
#define NEWBOTAI_SABER_THROW_FORCE_BUDGET 40
//Force we must still have banked after the throw to push/pull our way out of a drain lock.
//Human throws stayed net positive even though 43% were followed by an enemy drain, so the
//reserve only guards against throwing ourselves fully dry.
#define NEWBOTAI_DRAINLOCK_ESCAPE_RESERVE 10
//Below this the enemy cannot open a drain lock on us in the first place.
#define NEWBOTAI_DRAINLOCK_ENEMY_MIN_FORCE 25

//The mirror of NewBotAI_ShouldSuppressDrainlockSaberThrow: would throwing right now hand
//the *enemy* a drain lock on us? The duel tracks show this as the dominant way our bots get
//drainlocked - they spend the throw's force budget, drop under the push/pull escape cost,
//and then have nothing left when the drain starts. Veto the throw in that window.
static qboolean NewBotAI_WouldThrowInviteDrainlock(bot_state_t *bs)
{
	int ourForce;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	//Only a drain-capable opponent inside drain range can punish the throw this way.
	if (!(bs->currentEnemy->client->ps.fd.forcePowersKnown & (1 << FP_DRAIN)) &&
		!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN)))
	{
		return qfalse;
	}

	if (bs->frame_Enemy_Len > MAX_DRAIN_DISTANCE)
	{
		return qfalse;
	}

	//Absorb makes their drain harmless, so the throw is safe to spend.
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB))
	{
		return qfalse;
	}

	//Already being drained with little force left: throwing now is the exact mistake the
	//tracks flag. With force to spare the throw (drain -> throw) still paid off.
	if ((bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN)) &&
		bs->cur_ps.fd.forcePower < 40)
	{
		return qtrue;
	}

	if (bs->currentEnemy->client->ps.fd.forcePower < NEWBOTAI_DRAINLOCK_ENEMY_MIN_FORCE)
	{
		return qfalse;
	}

	//Otherwise veto only when the throw would leave us under the escape reserve.
	ourForce = bs->cur_ps.fd.forcePower;

	return ((ourForce - NEWBOTAI_SABER_THROW_FORCE_BUDGET) <
		NEWBOTAI_DRAINLOCK_ESCAPE_RESERVE) ? qtrue : qfalse;
}

static int NewBotAI_GetTotalHealthDelta(bot_state_t *bs)
{
	int ourTotalHealth;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return 0;
	}

	ourTotalHealth = g_entities[bs->client].health + bs->cur_ps.stats[STAT_ARMOR];

	return ourTotalHealth - NewBotAI_GetEnemyTotalHealth(bs);
}

//Private saber-only duel (dueltype 0): the engine refuses every force power except saber
//offense/defense/levitation, so the bot must fight with saber footwork alone.
static qboolean NewBotAI_IsSaberOnlyDuel(bot_state_t *bs)
{
	return (bs->cur_ps.duelInProgress &&
		dueltypes[bs->client] == 0 &&
		bs->cur_ps.weapon == WP_SABER) ? qtrue : qfalse;
}

//Wraps the engine force-power gate so bot pickers never queue powers (including saber
//throw) that BG_CanUseFPNow would refuse, e.g. in saber-only duels.
static qboolean NewBotAI_CanUseForcePowerNow(bot_state_t *bs, forcePowers_t power)
{
	return BG_CanUseFPNow(level.gametype, &level.clients[bs->client].ps, level.time, power);
}

static qboolean NewBotAI_CanAttemptFlipkick(bot_state_t *bs)
{
	if (!g_flipKick.integer)
	{
		return qfalse;
	}

	if (bs->cur_ps.duelInProgress &&
		dueltypes[bs->client] == 0)
	{
		return qfalse;
	}

	return qtrue;
}

// Item 4: for this long after a fresh grip session begins, levels 1-9 never successfully
// pull/push free of the grip (see NewBotAI_ReactToBeingGripped) - giving a human player's
// grip a short, human-like window to build up speed before any bot mistake-bias escape
// weighting applies.
#define NEWBOTAI_GRIP_NO_ESCAPE_WINDOW_MS 100
// Gripkick timing (dueltracks3 2026-10-03): jundon kept forwardmove > 0 on 84% of the
// frames in the 300ms before a kick (bots 55% - they stood still through the look-down
// settle) and spaced retries ~875ms+ apart (p25; bots 363ms). The settle is shorter and
// keeps walking in, and unconfirmed attempts dwell longer before the next try.
#define NEWBOTAI_GRIPKICK_LOOKDOWN_SETTLE_MS 120
#define NEWBOTAI_GRIPKICK_SETTLE_MIN_RANGE 40.0f
#define NEWBOTAI_GRIPKICK_RETRY_DWELL_MIN_MS 450
#define NEWBOTAI_GRIPKICK_RETRY_DWELL_MAX_MS 700
#define NEWBOTAI_RECOVERY_YAW_SPEED_DEG_PER_SEC 50.0f
#define NEWBOTAI_RECOVERY_YAW_INTERVAL_MS 800
#define NEWBOTAI_RECOVERY_STUCK_TIMEOUT_MS 5000
#define NEWBOTAI_WALL_ESCAPE_TURN_MIN_DEG 125
#define NEWBOTAI_WALL_ESCAPE_TURN_MAX_DEG 145
#define NEWBOTAI_WALL_CONTACT_PROBE_DISTANCE 24.0f
#define NEWBOTAI_WALL_STEP_HEIGHT 18.0f
#define NEWBOTAI_WALL_HOP_HEIGHT 48.0f

//How long (ms) a flipkick attempt keeps toggling fresh jump presses after the initial
//jump. This was raised from the original 350 to 500, and that extra time outlived the
//jump arc itself: the bot landed with jump input still live and immediately hopped
//again, which is why bots hopped after every flipkick attempt. The bot_fkduration cvar
//(default 300 - 50ms shorter than the old 350) now tunes this window in-game: a window
//that outlives the jump arc leaves jump input latched at landing, so it should cover
//just the ascent to the engine's kick window (velocity[2]>200 near the ground).
static int NewBotAI_GetFlipkickInputWindowMs(void)
{
	return Com_Clampi(50, 1000, bot_fkduration.integer);
}

static qboolean NewBotAI_HasWaypointNavigation(void)
{
	return (bot_navigation.integer && gWPNum > 0) ? qtrue : qfalse;
}

static float NewBotAI_GetPullkickAssumedPullSpeed(void)
{
	return Com_Clamp(0.0f, 2000.0f, bot_ptk_pullspeed.value);
}

static int NewBotAI_GetPullkickExtraDelayMs(void)
{
	return Com_Clampi(0, 500, bot_ptk_extradelay.integer);
}

static int NewBotAI_GetPullkickDefensiveReactionWindowMs(void)
{
	return Com_Clampi(0, 1000, bot_ptk_pullreactwindow.integer);
}

static void NewBotAI_StartEscapeYawOverride(bot_state_t *bs, int durationMs)
{
	if (!bs)
	{
		return;
	}

	if (durationMs < 0)
	{
		durationMs = 0;
	}

	bs->escapeYawOverrideUntil = level.time + durationMs;
}

static float NewBotAI_GetRecoveryYawSpeedDegPerSec(void)
{
	float yawSpeed = NEWBOTAI_RECOVERY_YAW_SPEED_DEG_PER_SEC;

	if (yawSpeed < 0.0f)
	{
		yawSpeed = 0.0f;
	}
	else if (yawSpeed > 1080.0f)
	{
		yawSpeed = 1080.0f;
	}

	return yawSpeed;
}

static int NewBotAI_GetRecoveryYawIntervalMs(void)
{
	return NEWBOTAI_RECOVERY_YAW_INTERVAL_MS;
}

static int NewBotAI_GetWallAvoidCooldownMs(void)
{
	return Com_Clampi(0, 30000, bot_redirectcooldown.integer);
}

static int NewBotAI_GetWallRedirectIntervalMs(void)
{
	const int cooldownMs = NewBotAI_GetWallAvoidCooldownMs();

	if (cooldownMs > 0)
	{
		return cooldownMs;
	}

	return NewBotAI_GetRecoveryYawIntervalMs();
}

static float NewBotAI_GetWallEscapeTurnAngle(void)
{
	const float yawTurn = (float)Q_irand(NEWBOTAI_WALL_ESCAPE_TURN_MIN_DEG, NEWBOTAI_WALL_ESCAPE_TURN_MAX_DEG);

	return (Q_irand(0, 1) ? yawTurn : -yawTurn);
}

static int NewBotAI_GetLightningBurstHoldMs(void)
{
	return Q_irand(1000, 2000);
}

static void NewBotAI_ResetRecoveryMovement(bot_state_t *bs)
{
	if (!bs)
	{
		return;
	}

	bs->navObstacleUntil = 0;
	bs->navRecoverMode = NEWBOTAI_NAV_RECOVERY_MODE_DIRECT;
	bs->navRecoverModeUntil = 0;
	bs->navHoldUntil = 0;
	bs->navRecoverStuckSince = 0;
	bs->customNavReverseTime = 0;
	bs->escapeYawOverrideUntil = 0;
	bs->navBuildWaypointTrail = qfalse;
	VectorClear(bs->navHoldDirection);
	VectorClear(bs->navHoldGoal);
	bs->navHoldGoalValid = qfalse;
}

static void NewBotAI_ClearLostSightCombatInput(bot_state_t *bs)
{
	if (!bs || bs->frame_Enemy_Vis)
	{
		return;
	}

	bs->doAttack = 0;
	bs->doAltAttack = 0;
	bs->doForcePush = 0;
	bs->drainHoldTime = 0;
	bs->flipkickInputTime = 0;
	bs->flipkickJumpHeld = qfalse;
	bs->pullKickJumpTime = 0;
	bs->lastFlipkickAttemptTime = 0;
	bs->drainRollYawStart = 0;
	bs->drainRollResetTime = 0;
	bs->drainRollDir = 0;
	bs->gripkickLookDownUntil = 0;
	bs->gripkickRestackDir = 0;
	bs->enemyWaypointFallbackIndex = -1;
	bs->enemyWaypointFallbackTime = 0;
	bs->enemyWaypointFallbackEnemyNum = -1;
	bs->combatAction = BOT_COMBAT_ACTION_AGGRESSION;
	bs->runningLikeASissy = 0;
	bs->forceMove_Forward = 0;
	bs->forceMove_Right = 0;
	bs->forceMove_Up = 0;
	bs->ideal_viewangles[PITCH] = 0.0f;
	bs->goalAngles[PITCH] = 0.0f;
	NewBotAI_ClearRandomStrafeOverlay(bs);
	NewBotAI_ResetFanChain(bs);
	if (!(bs->cur_ps.fd.forcePowersActive & (1 << FP_GRIP)))
	{
		bs->gripkickActive = qfalse;
		bs->gripkickJerkUntil = 0;
		bs->gripkickJerkCount = 0;
		bs->gripkickKickCount = 0;
		bs->gripkickJerkDirection = 0;
		bs->gripkickJerkPitch = 0.0f;
		bs->gripkickDwellUntil = 0;
	}
	NewBotAI_ClearLightningBurst(bs);
}

static void NewBotAI_PrepareWaypointHandoff(bot_state_t *bs, qboolean clearEnemyLock)
{
	if (!bs)
	{
		return;
	}

	NewBotAI_ClearLostSightCombatInput(bs);
	bs->frame_Enemy_Vis = 0;
	NewBotAI_ResetRecoveryMovement(bs);
	bs->combatStuckSince = 0;
	VectorCopy(bs->origin, bs->combatStuckOrigin);
	if (clearEnemyLock)
	{
		NewBotAI_ClearCurrentEnemyLock(bs);
	}
}

static qboolean NewBotAI_ShouldForceLostSightWaypointReset(bot_state_t *bs)
{
	const int targetTimeoutMs = BotGetTargetTimeoutMs();
	int retainWindowMs;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client || bs->frame_Enemy_Vis)
	{
		return qfalse;
	}
	if (!NewBotAI_HasWaypointNavigation())
	{
		return qfalse;
	}
	if (!NewBotAI_ShouldRetainLostSightTarget(bs, bs->currentEnemy))
	{
		return qtrue;
	}
	if (bs->lastVisibleEnemyTime <= 0)
	{
		return qfalse;
	}

	retainWindowMs = targetTimeoutMs / 2;
	if (retainWindowMs < 300)
	{
		retainWindowMs = 300;
	}
	else if (retainWindowMs > 1500)
	{
		retainWindowMs = 1500;
	}
	if (level.time < bs->lastVisibleEnemyTime + retainWindowMs)
	{
		return qfalse;
	}

	if (VectorLengthSquared(bs->cur_ps.velocity) > 900.0f)
	{
		return qfalse;
	}

	return qtrue;
}

static qboolean NewBotAI_ShouldKeepWaypointPursuitTarget(bot_state_t *bs, gentity_t *enemy)
{
	return (NewBotAI_ShouldPursueTargetThroughWaypoints(bs, enemy) &&
		!NewBotAI_ShouldForceLostSightWaypointReset(bs)) ? qtrue : qfalse;
}

static qboolean NewBotAI_HasValidCurrentEnemy(bot_state_t *bs)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}
	if (bs->currentEnemy->health < 1)
	{
		return qfalse;
	}
	if (bs->currentEnemy->client->pers.connected != CON_CONNECTED &&
		bs->currentEnemy->client->pers.connected != CON_CONNECTING)
	{
		return qfalse;
	}

	return qtrue;
}

static void NewBotAI_ClearLightningBurst(bot_state_t *bs)
{
	if (!bs)
	{
		return;
	}

	bs->lightningHoldUntil = 0;
}

static qboolean NewBotAI_IsRecoveryMovementActive(bot_state_t *bs)
{
	(void)bs;
	return qfalse;
}

static qboolean NewBotAI_HasExclusiveFlipkickMovement(bot_state_t *bs)
{
	return (bs && (bs->flipkickInputTime > level.time || bs->pullKickJumpTime != 0)) ? qtrue : qfalse;
}

static void NewBotAI_StartLightningBurst(bot_state_t *bs)
{
	if (!bs)
	{
		return;
	}
	if (bs->lightningHoldUntil > level.time)
	{
		return;
	}

	bs->lightningHoldUntil = level.time + NewBotAI_GetLightningBurstHoldMs();
}

static qboolean NewBotAI_CanContinueLightningBurst(bot_state_t *bs)
{
	vec3_t a_fo;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}
	if (bs->lightningHoldUntil <= level.time)
	{
		return qfalse;
	}
	if (g_forcePowerDisable.integer & (1 << FP_LIGHTNING))
	{
		return qfalse;
	}
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_LIGHTNING)) ||
		bs->cur_ps.fd.forcePowerLevel[FP_LIGHTNING] <= FORCE_LEVEL_0)
	{
		return qfalse;
	}
	if (!bs->frame_Enemy_Vis ||
		(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB)))
	{
		return qfalse;
	}
	if (bs->cur_ps.fd.forcePower <= 25 ||
		bs->frame_Enemy_Len < BotGetLightningStartDistance(bs) ||
		bs->frame_Enemy_Len > BotGetLightningMaxDistance(bs))
	{
		return qfalse;
	}

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
	vectoangles(a_fo, a_fo);
	if (!InFieldOfVision(bs->viewangles, 50, a_fo))
	{
		return qfalse;
	}

	return qtrue;
}

static qboolean NewBotAI_IsDuelStrafeSuppressed(bot_state_t *bs)
{
	return (bs && bs->duelNoStrafeUntil > level.time) ? qtrue : qfalse;
}

static int NewBotAI_GetWallStrafeAwayDir(bot_state_t *bs)
{
	vec3_t right, start, end;
	trace_t trLeft, trRight;

	if (!bs || bs->cur_ps.groundEntityNum == ENTITYNUM_NONE)
	{
		return 0;
	}

	AngleVectors(bs->viewangles, NULL, right, NULL);
	VectorCopy(bs->origin, start);
	start[2] += 24.0f;

	VectorMA(start, 40.0f, right, end);
	JP_Trace(&trRight, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	VectorMA(start, -40.0f, right, end);
	JP_Trace(&trLeft, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (trRight.fraction < 1.0f && trLeft.fraction >= 1.0f)
	{
		return -1;
	}
	if (trLeft.fraction < 1.0f && trRight.fraction >= 1.0f)
	{
		return 1;
	}

	return 0;
}

static qboolean NewBotAI_IsActivelyEngagedInCombat(bot_state_t *bs)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (bs->frame_Enemy_Vis &&
		(bs->frame_Enemy_Len <= (NEWBOTAI_TARGET_COMMIT_DISTANCE * 1.5f) ||
		 bs->doAttack || bs->doAltAttack ||
		 bs->cur_ps.weaponstate == WEAPON_FIRING ||
		 bs->cur_ps.weaponstate == WEAPON_CHARGING ||
		 bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT))
	{
		return qtrue;
	}

	if (bs->lastHurtTime > level.time - 1200 ||
		bs->combatNavHoldUntil > level.time)
	{
		return qtrue;
	}

	return qfalse;
}

//"Barely moving" despite trying to move - roughly 30 units/sec (30*30). Shared by the
//retreat wall-avoid jump and the no-waypoint yaw escape to detect a genuinely stuck bot.
#define NEWBOTAI_WALLAVOID_STUCK_SPEED_SQ 900.0f
#define NEWBOTAI_COMBAT_STUCK_DISTANCE_SQ (96.0f * 96.0f)
#define NEWBOTAI_COMBAT_STUCK_TIME_MS 3000
#define NEWBOTAI_DUEL_TARGET_BLACKLIST_MS 15000
static qboolean NewBotAI_ShouldPreferFlipkickOverThrow(bot_state_t *bs)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}
	if (bs->currentEnemy->client->ps.saberInFlight &&
		NewBotAI_ShouldStabilizeAgainstEnemySaberThrow(bs))
	{
		return qfalse;
	}

	return (NewBotAI_CanAttemptFlipkick(bs) && bs->frame_Enemy_Len <= NEWBOTAI_FLIPKICK_PREFERRED_RANGE) ? qtrue : qfalse;
}

static qboolean NewBotAI_IsImmediateFlipkickContact(bot_state_t *bs)
{
	return (bs && bs->currentEnemy && bs->currentEnemy->client &&
		bs->frame_Enemy_Vis &&
		bs->frame_Enemy_Len <= NEWBOTAI_IMMEDIATE_FLIPKICK_CONTACT_RANGE) ? qtrue : qfalse;
}

static qboolean NewBotAI_HasFreePullkickWindow(bot_state_t *bs)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	return (bs->cur_ps.fd.forcePower > bs->currentEnemy->client->ps.fd.forcePower &&
		bs->currentEnemy->client->ps.fd.forcePower < 20) ? qtrue : qfalse;
}

static qboolean NewBotAI_IsBeingPulledTowardEnemy(bot_state_t *bs)
{
	if (!bs)
	{
		return qfalse;
	}

	return (bs->cur_ps.forceHandExtend == HANDEXTEND_FORCEPULL ||
		bs->cur_ps.powerups[PW_PULL] > level.time) ? qtrue : qfalse;
}

// Executes the "drain + sidestep" escape used in place of a flipkick when our bot is too
// low on health to risk the flipkick's vulnerability window (see NewBotAI_Flipkick).
// Rolling away was a weakness - a rolling bot is committed to the roll animation and easy
// to chase down - so this now just strafes/backpedals on foot while draining instead.
static void NewBotAI_DrainRollEscape(bot_state_t *bs)
{
	if (!(g_forcePowerDisable.integer & (1 << FP_DRAIN)) && (bs->cur_ps.fd.forcePowersKnown & (1 << FP_DRAIN)))
	{
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
		trap->EA_ForcePower(bs->client);
	}

	if (bs->drainRollResetTime < level.time)
	{
		bs->drainRollDir = Q_irand(0, 2) - 1; //-1 left, 0 back, 1 right, chosen at random
		bs->drainRollResetTime = level.time + 400;
	}

	if (bs->drainRollDir < 0)
		NewBotAI_RetreatDiagonal(bs, qtrue);
	else
		NewBotAI_RetreatDiagonal(bs, qfalse);
	bs->drainRollYawStart = 0;
}

static void NewBotAI_RetreatDiagonal(bot_state_t *bs, qboolean moveLeft)
{
	if (NewBotAI_IsDuelStrafeSuppressed(bs))
	{
		trap->EA_MoveBack(bs->client);
		return;
	}

	if (bs->currentEnemy && bs->frame_Enemy_Vis)
	{
		int awayDir = NewBotAI_GetWallStrafeAwayDir(bs);
		if (awayDir < 0)
		{
			moveLeft = qtrue;
		}
		else if (awayDir > 0)
		{
			moveLeft = qfalse;
		}
	}

	trap->EA_MoveBack(bs->client);
	if (moveLeft)
		trap->EA_MoveLeft(bs->client);
	else
		trap->EA_MoveRight(bs->client);
}

static void NewBotAI_RetreatStraight(bot_state_t *bs)
{
	trap->EA_MoveBack(bs->client);
}

// The only scenario where our bot should avoid flipkicking despite being able to: the enemy has
// enough health to survive a fight (>49) while we are critically low (<20), meaning we would be
// one hit from dying during the flipkick's vulnerable window. In that exact scenario we drain and
// step away instead of kicking.
static qboolean NewBotAI_ShouldAvoidFlipkickForSafety(bot_state_t *bs)
{
	if (!bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (bs->currentEnemy->health > 49 && g_entities[bs->client].health < 20)
	{
		return qtrue;
	}

	return qfalse;
}

static qboolean NewBotAI_CanBackflip(bot_state_t *bs)
{
	const qboolean enemyCanDrain = (bs->currentEnemy && bs->currentEnemy->client &&
		((bs->currentEnemy->client->ps.fd.forcePowersKnown & (1 << FP_DRAIN)) ||
		 (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN)))) ? qtrue : qfalse;
	const qboolean enemyCanGrip = (bs->currentEnemy && bs->currentEnemy->client &&
		((bs->currentEnemy->client->ps.fd.forcePowersKnown & (1 << FP_GRIP)) ||
		 (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_GRIP)))) ? qtrue : qfalse;

	return (g_entities[bs->client].health == 100 &&
		enemyCanDrain &&
		NewBotAI_GetAntiDrainWeight(bs) > 0 &&
		!enemyCanGrip &&
		bs->frame_Enemy_Len > MAX_GRIP_DISTANCE * 2) ? qtrue : qfalse;
}

static qboolean NewBotAI_IsFlipkickSetupReady(bot_state_t *bs)
{
	vec3_t a_fo;
	float yawDiff;
	float yawTolerance;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (!NewBotAI_CanAttemptFlipkick(bs))
	{
		return qfalse;
	}

	if (bs->cur_ps.saberInFlight)
	{
		return qfalse;
	}

	//Already in a live jump-toggle window for a kick attempt.
	if (bs->flipkickInputTime > level.time)
	{
		return qtrue;
	}

	if (bs->frame_Enemy_Len > NEWBOTAI_IMMEDIATE_FLIPKICK_RANGE)
	{
		return qfalse;
	}

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
	vectoangles(a_fo, a_fo);
	yawDiff = AngleDifference(a_fo[YAW], bs->viewangles[YAW]);
	yawTolerance = NewBotAI_GetImmediateFlipkickYawTolerance(
		NewBotAI_IsImmediateFlipkickContact(bs) ? 1 : 0);

	return (yawDiff <= yawTolerance && yawDiff >= -yawTolerance) ? qtrue : qfalse;
}

// Item 6: higher-skill bots satisfy the real flipkick/pullkick gating (see
// NewBotAI_Flipkick, NewBotAI_GetPull) far more often in normal combat than lower-skill
// bots do, so their genuine kick attempts alone already read as frequent hopping - the
// ambient hop below then stacks right on top of that and makes it worse the higher the
// skill gets. Scale the wait interval longer as skill rises so bot_hopfrequency is the
// one dial that actually reduces the *ambient* hop rate without touching real kick
// gating; skillDampen ranges 1.0x at skill 0 up to 2.5x at skill 10, so a level 10 bot's
// ambient hops are spread roughly 2.5x further apart than a level 0 bot's for the same
// bot_hopfrequency value.
static int NewBotAI_GetNextHopIntervalMs(bot_state_t *bs, float hopFrequency)
{
	const float skillDampen = 1.0f + (bs->settings.skill * 0.15f);
	return (int)((float)Q_irand(500, 8000) * (100.0f / hopFrequency) * skillDampen);
}

static void NewBotAI_PushHopRetryCooldown(bot_state_t *bs)
{
	const float hopFrequency = bot_hopfrequency.value;
	const qboolean isGrounded = (bs->cur_ps.groundEntityNum != ENTITYNUM_NONE) ? qtrue : qfalse;

	if (!isGrounded || hopFrequency <= 0.0f)
	{
		return;
	}

	bs->nextHopTime = level.time + NewBotAI_GetNextHopIntervalMs(bs, hopFrequency);
	bs->hopWasGrounded = qtrue;
}

static qboolean NewBotAI_ShouldUseCombatHop(bot_state_t *bs, qboolean forceImmediate)
{
	const float hopFrequency = bot_hopfrequency.value;

	if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE)
	{
		return qfalse;
	}

	if (forceImmediate)
	{
		return qtrue;
	}

	if (hopFrequency <= 0.0f)
	{
		return qfalse;
	}

	if (bs->nextHopTime == 0)
	{
		bs->nextHopTime = level.time + NewBotAI_GetNextHopIntervalMs(bs, hopFrequency);
		bs->hopWasGrounded = qtrue;
		return qfalse;
	}

	if (bs->nextHopTime == -1 || bs->nextHopTime > level.time)
	{
		return qfalse;
	}

	return qtrue;
}

static void NewBotAI_ConsumeCombatHop(bot_state_t *bs)
{
	if (bot_hopfrequency.value <= 0.0f)
	{
		return;
	}

	bs->nextHopTime = -1;
	bs->hopWasGrounded = qtrue;
}

// Optional random hop. Flipkicks only add jump input when a kick is truly possible, so any
// ambient hopping is handled here instead: bot_hopfrequency scales how soon after each hop
// the next one is scheduled (default 100 = a random 0.5-8 second interval, higher = less
// frequent hops, lower = more frequent, 0 = disabled). The interval is only re-rolled once
// the bot is back on the ground after a hop, so a very short roll genuinely chains one hop
// into the next while the bot otherwise stays on the ground. See
// NewBotAI_GetNextHopIntervalMs for the additional per-skill dampening applied on top.
static void NewBotAI_TryRandomHop(bot_state_t *bs)
{
	const float hopFrequency = bot_hopfrequency.value;
	const qboolean isGrounded = (bs->cur_ps.groundEntityNum != ENTITYNUM_NONE) ? qtrue : qfalse;

	//Item 12: read the float value (not the truncated integer) so sub-1 frequencies like
	//0.1 actually throttle hopping instead of rounding to 0/off or 1.
	if (hopFrequency <= 0.0f)
	{
		bs->hopWasGrounded = isGrounded;
		return;
	}

	//Mid-hop (a hop fired, still airborne): hold off on re-rolling until we land again.
	if (bs->nextHopTime == -1)
	{
		if (isGrounded)
		{
			//Just landed: roll the next interval now. The range is wide enough that most
			//hops are singles (a multi-second wait) while an occasional short roll chains
			//one hop straight into the next, keeping the bot unpredictable. Divide by the
			//frequency so higher values spread hops further apart (100 = 0.5-8s).
			bs->nextHopTime = level.time + NewBotAI_GetNextHopIntervalMs(bs, hopFrequency);
		}
		bs->hopWasGrounded = isGrounded;
		return;
	}

	//Item 2: landing from airborne time that wasn't our own tracked hop (a flipkick, a
	//knockdown, walking off a ledge) used to fall straight through to the "grounded,
	//time's up" check below and fire an unrelated hop the instant we touched down -
	//this was the bot "wanting to keep hopping" right out of a jump/flipkick landing.
	//Re-roll a fresh wide interval on that landing transition instead of firing.
	if (isGrounded && !bs->hopWasGrounded)
	{
		bs->nextHopTime = level.time + NewBotAI_GetNextHopIntervalMs(bs, hopFrequency);
		bs->hopWasGrounded = isGrounded;
		return;
	}

	if (bs->nextHopTime > level.time)
	{
		bs->hopWasGrounded = isGrounded;
		return;
	}

	if (!isGrounded)
	{
		//Airborne for a non-hop reason (a flipkick, a knockdown, walking off a ledge):
		//the scheduled hop time passed unused - push the next roll out a full wide
		//interval instead of firing the moment we touch down.
		bs->nextHopTime = level.time + NewBotAI_GetNextHopIntervalMs(bs, hopFrequency);
		bs->hopWasGrounded = isGrounded;
		return;
	}

	//A flipkick attempt just aborted while we are grounded: the kick never left the
	//ground but the attempt still holds post-attempt jump input. Treat it like a
	//missed hop - re-roll the next interval so bot_hopfrequency gates the retry
	//instead of letting the bot bounce again the moment the leftover input clears.
	if (bs->flipkickInputTime > level.time && bs->flipkickJumpHeld)
	{
		bs->nextHopTime = level.time + NewBotAI_GetNextHopIntervalMs(bs, hopFrequency);
		bs->hopWasGrounded = isGrounded;
		return;
	}

	//Item 2B: a hop into wall contact starts a vertical wallrun - skip it unless we
	//are retreating for our life. The scheduled hop time passes unused (no re-roll,
	//so we don't spam a fresh roll every think while hugging a wall).
	if (NewBotAI_TouchingWallNotEnemy(bs) && !NewBotAI_ShouldWallrunAgainstWalls(bs))
	{
		bs->hopWasGrounded = isGrounded;
		return;
	}

	//Fire the hop and mark mid-hop: the next interval only rolls once we land again.
	trap->EA_Jump(bs->client);
	bs->nextHopTime = -1;
	bs->hopWasGrounded = isGrounded;
}

static qboolean NewBotAI_ClassifyForwardObstacle(bot_state_t *bs, const vec3_t moveDir, qboolean *requiresHopOut, int *hitEntityOut)
{
	trace_t tr;
	trace_t thinTrace;
	trace_t stepTrace;
	trace_t hopTrace;
	float stepFloorZ = 0.0f;
	vec3_t dir, end, thinStart, thinEnd, stepStart, stepEnd, hopStart, hopEnd;
	vec3_t mins, maxs, thinMins, thinMaxs;

	if (requiresHopOut)
	{
		*requiresHopOut = qfalse;
	}
	if (hitEntityOut)
	{
		*hitEntityOut = ENTITYNUM_NONE;
	}

	if (!bs)
	{
		return qfalse;
	}

	VectorCopy(moveDir, dir);
	dir[2] = 0.0f;
	if (VectorNormalize(dir) <= 0.0f)
	{
		return qfalse;
	}

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = 0;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 32;
	thinMins[0] = -2.0f;
	thinMins[1] = -2.0f;
	thinMins[2] = 0.0f;
	thinMaxs[0] = 2.0f;
	thinMaxs[1] = 2.0f;
	thinMaxs[2] = 8.0f;

	VectorMA(bs->cur_ps.origin, NEWBOTAI_WALL_CONTACT_PROBE_DISTANCE, dir, end);
	JP_Trace(&tr, bs->cur_ps.origin, mins, maxs, end, bs->cur_ps.clientNum, CONTENTS_SOLID, qfalse, 0, 0);

	if (tr.fraction >= 1.0f)
	{
		return qfalse;
	}
	if (hitEntityOut)
	{
		*hitEntityOut = tr.entityNum;
	}

	if (bs->currentEnemy && tr.entityNum == bs->currentEnemy->s.number)
	{
		return qfalse;
	}
	if (tr.plane.normal[2] > 0.5f)
	{
		return qfalse;
	}
	VectorMA(tr.endpos, -2.0f, dir, thinStart);
	VectorMA(thinStart, 8.0f, dir, thinEnd);
	JP_Trace(&thinTrace, thinStart, thinMins, thinMaxs, thinEnd, bs->cur_ps.clientNum, CONTENTS_SOLID, qfalse, 0, 0);
	if (!thinTrace.startsolid &&
		!thinTrace.allsolid &&
		thinTrace.fraction >= 1.0f)
	{
		return qfalse; //ignore paper-thin decorative trim and lips
	}

	VectorCopy(bs->cur_ps.origin, stepStart);
	VectorCopy(tr.endpos, stepEnd);
	VectorMA(stepEnd, 8.0f, dir, stepEnd);
	stepStart[2] += NEWBOTAI_WALL_STEP_HEIGHT;
	stepEnd[2] += NEWBOTAI_WALL_STEP_HEIGHT;
	JP_Trace(&stepTrace, stepStart, mins, maxs, stepEnd, bs->cur_ps.clientNum, CONTENTS_SOLID, qfalse, 0, 0);
	if (!stepTrace.startsolid &&
		!stepTrace.allsolid &&
		stepTrace.fraction >= 1.0f &&
		NewBotAI_HasReachableFloorAtProbe(bs, stepTrace.endpos, NEWBOTAI_WALL_STEP_HEIGHT + 8.0f, &stepFloorZ) &&
		stepFloorZ <= bs->cur_ps.origin[2] + STEPSIZE + 1.0f)
	{
		return qtrue;
	}

	if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE ||
		BotNav_CheckFallingHazard(bs, dir, qtrue))
	{
		return qfalse;
	}

	VectorCopy(bs->cur_ps.origin, hopStart);
	VectorCopy(tr.endpos, hopEnd);
	VectorMA(hopEnd, NEWBOTAI_WALL_CONTACT_PROBE_DISTANCE, dir, hopEnd);
	hopStart[2] += NEWBOTAI_WALL_HOP_HEIGHT;
	hopEnd[2] += NEWBOTAI_WALL_HOP_HEIGHT;
	JP_Trace(&hopTrace, hopStart, mins, maxs, hopEnd, bs->cur_ps.clientNum, CONTENTS_SOLID, qfalse, 0, 0);
	if (!hopTrace.startsolid &&
		!hopTrace.allsolid &&
		hopTrace.fraction >= 1.0f &&
		NewBotAI_HasReachableFloorAtProbe(bs, hopTrace.endpos, NEWBOTAI_WALL_HOP_HEIGHT + 24.0f, NULL))
	{
		if (requiresHopOut)
		{
			*requiresHopOut = qtrue;
		}
		return qtrue;
	}

	return qfalse;
}

//True while a short player-sized trace from the bot hits a solid wall and whatever
//we hit is not our current enemy. Null bot states and enemy-body contact both return
//qfalse so callers only treat real map-geometry contact as a wall.
static qboolean NewBotAI_TouchingWallNotEnemy(bot_state_t *bs)
{
	trace_t tr;
	vec3_t mins, maxs, traceFrom, traceto;
	vec3_t wallProbeAngles, wallProbeForward;
	float wallFacingDot;

	if (!bs)
	{
		return qfalse;
	}

	VectorClear(wallProbeAngles);
	wallProbeAngles[YAW] = bs->ideal_viewangles[YAW];
	AngleVectors(wallProbeAngles, wallProbeForward, NULL, NULL);
	wallProbeForward[2] = 0.0f;
	if (VectorNormalize(wallProbeForward) <= 0.0f)
	{
		VectorClear(wallProbeAngles);
		wallProbeAngles[YAW] = bs->viewangles[YAW];
		AngleVectors(wallProbeAngles, wallProbeForward, NULL, NULL);
		wallProbeForward[2] = 0.0f;
		if (VectorNormalize(wallProbeForward) <= 0.0f)
		{
			return qfalse;
		}
	}
	VectorCopy(bs->cur_ps.origin, traceFrom);
	traceFrom[2] += 24.0f;
	VectorMA(traceFrom, NEWBOTAI_WALL_CONTACT_PROBE_DISTANCE, wallProbeForward, traceto);

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = -8;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 8;

	JP_Trace(&tr, traceFrom, mins, maxs, traceto, bs->cur_ps.clientNum, CONTENTS_SOLID, qfalse, 0, 0);

	if (tr.fraction >= 1.0f)
	{
		return qfalse;
	}

	if (bs->currentEnemy && tr.entityNum == bs->currentEnemy->s.number)
	{
		return qfalse;
	}
	if (tr.plane.normal[2] > 0.35f)
	{
		return qfalse;
	}

	wallFacingDot = DotProduct(wallProbeForward, tr.plane.normal);
	if (wallFacingDot > -0.25f)
	{
		return qfalse;
	}

	return qtrue;
}

static qboolean NewBotAI_HandleClimbableForwardObstacle(bot_state_t *bs, const vec3_t moveDir)
{
	qboolean requiresHop = qfalse;
	int hitEntityNum = ENTITYNUM_NONE;

	if (!NewBotAI_ClassifyForwardObstacle(bs, moveDir, &requiresHop, &hitEntityNum))
	{
		return qfalse;
	}
	if (hitEntityNum >= 0 &&
		hitEntityNum < MAX_CLIENTS)
	{
		return qfalse;
	}

	if (requiresHop)
	{
		if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE ||
			bs->wallAvoidNextTime > level.time)
		{
			return qfalse;
		}
		trap->EA_Jump(bs->client);
		bs->wallAvoidNextTime = level.time + NewBotAI_GetWallRedirectIntervalMs();
	}
	trap->EA_MoveForward(bs->client);

	return qtrue;
}

static qboolean NewBotAI_HasReachableFloorAtProbe(bot_state_t *bs, const vec3_t probeOrigin, float dropHeight, float *floorZOut)
{
	trace_t floorTrace;
	vec3_t probeEnd;
	vec3_t mins, maxs;

	if (floorZOut)
	{
		*floorZOut = 0.0f;
	}

	if (!bs || dropHeight <= 0.0f)
	{
		return qfalse;
	}

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = 0;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 32;

	VectorCopy(probeOrigin, probeEnd);
	probeEnd[2] -= dropHeight;
	JP_Trace(&floorTrace, probeOrigin, mins, maxs, probeEnd, bs->cur_ps.clientNum, CONTENTS_SOLID, qfalse, 0, 0);

	if (!floorTrace.startsolid &&
		!floorTrace.allsolid &&
		floorTrace.fraction < 1.0f &&
		floorZOut)
	{
		*floorZOut = floorTrace.endpos[2];
	}

	return (!floorTrace.startsolid && !floorTrace.allsolid && floorTrace.fraction < 1.0f) ? qtrue : qfalse;
}

//Vertical wallruns (repeat jump inputs while making wall contact) are only wanted as
//an escape tool: while retreating below 49 health. Against opponents the same jump
//input is the flipkick, so callers only consult this once they know the contact is a
//wall.
static qboolean NewBotAI_ShouldWallrunAgainstWalls(bot_state_t *bs)
{
	return (bs->combatAction == BOT_COMBAT_ACTION_RETREAT_DEFENSE &&
		g_entities[bs->client].health < 49) ? qtrue : qfalse;
}

//Diagonal wallruns start from lateral+forward inputs followed by a jump while already
//touching a wall - only wanted as an escape tool while retreating with over 50 health;
//everywhere else they just look like accidental wall-hugging, so strip the lateral input.
static qboolean NewBotAI_ShouldAvoidDiagonalWallrun(bot_state_t *bs)
{
	const qboolean escapingWithHealth = (g_entities[bs->client].health > 50 &&
		bs->combatAction == BOT_COMBAT_ACTION_RETREAT_DEFENSE) ? qtrue : qfalse;

	return (!escapingWithHealth && NewBotAI_TouchingWallNotEnemy(bs)) ? qtrue : qfalse;
}

//Estimated milliseconds until the airborne bot reaches the ground directly below it,
//based on actual traced ground distance and current vertical velocity. Used to cut a
//flipkick short before touchdown so the leftover jump input doesn't register as a hop.
//Returns FLT_MAX while rising with no ground in trace range (we are nowhere near
//landing), 0 once we are already at/past the traced ground.
static float NewBotAI_FlipkickMsToGround(bot_state_t *bs)
{
	trace_t tr;
	vec3_t end;
	float groundDistance;

	VectorCopy(bs->cur_ps.origin, end);
	end[2] -= 256.0f;
	JP_Trace(&tr, bs->cur_ps.origin, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	groundDistance = (bs->cur_ps.origin[2] - tr.endpos[2]);
	if (groundDistance < 0.0f)
	{
		groundDistance = 0.0f;
	}

	if (bs->cur_ps.velocity[2] <= 0.0f)
	{
		//Falling (or at apex): time until we drop the traced distance. Gravity is
		//g_gravity (default 800 u/s^2); approximate with constant current fall speed,
		//which slightly overestimates the time - safe, it only makes us cut earlier.
		if (bs->cur_ps.velocity[2] < 0.0f)
		{
			return (groundDistance / -bs->cur_ps.velocity[2]) * 1000.0f;
		}
		return FLT_MAX;
	}

	//Rising: time to apex plus fall back to the traced ground.
	{
		const float gravity = (g_gravity.value > 0.0f) ? g_gravity.value : 800.0f;
		const float timeToApex = bs->cur_ps.velocity[2] / gravity; //seconds
		const float apexHeight = groundDistance + (bs->cur_ps.velocity[2] * timeToApex * 0.5f);
		const float fallTime = sqrtf(2.0f * apexHeight / gravity);
		return (timeToApex + fallTime) * 1000.0f;
	}
}

void NewBotAI_Flipkick(bot_state_t *bs)
{
	qboolean enemySwing = qfalse;
	//During Gripkick our own forward move / velocity is dictated by chasing the gripped
	//target, not a normal engagement decision, so the range/speed gate below (meant to stop
	//us kicking at a stationary point-blank enemy) doesn't apply - always let the grip
	//sequence's flipkick attempts through.
	const qboolean isGripSequence = (bs->cur_ps.fd.forcePowersActive & (1 << FP_GRIP)) ? qtrue : qfalse;

	if (!NewBotAI_CanAttemptFlipkick(bs))
	{
		return;
	}

	if (!NewBotAI_CanInitiateFlipkickUnderFanPressure(bs))
	{
		//During active fan pressure, only commit the flipkick once we just confirmed a
		//saber hit so the combo is saber-contact -> flipkick.
		return;
	}

	if (bs->cur_ps.saberInFlight)
	{
		//Never kick empty-handed mid-throw.
		return;
	}

	if (!isGripSequence && bs->flipkickInputTime <= level.time && !NewBotAI_IsFlipkickSetupReady(bs))
	{
		NewBotAI_PushHopRetryCooldown(bs);
		return;
	}
	if (!isGripSequence && bs->currentEnemy && bs->currentEnemy->client &&
		bs->currentEnemy->client->ps.saberInFlight &&
		NewBotAI_ShouldStabilizeAgainstEnemySaberThrow(bs))
	{
		NewBotAI_PushHopRetryCooldown(bs);
		return;
	}

	//We already committed to a flipkick and are still airborne and rising - keep
	//re-arming the jump press/release toggle for the whole ascent instead of only a
	//narrow window after the initial jump, so we don't stop pressing before the engine's
	//own timing (velocity[2]>200, near ground) has a chance to land the kick.
	if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE && bs->cur_ps.velocity[2] > 0 && bs->flipkickInputTime > level.time)
	{
		//Item 2B: while airborne and rising against a wall (not an opponent), the
		//repeated jump press below is what starts a vertical wallrun - drop it unless
		//we are retreating for our life. Against opponents the same input is the
		//flipkick and is preserved.
		if (!isGripSequence && NewBotAI_TouchingWallNotEnemy(bs) && !NewBotAI_ShouldWallrunAgainstWalls(bs))
		{
			trap->EA_MoveForward(bs->client);
			bs->flipkickInputTime = 0;
			return;
		}
		//Cut the kick short when we are about to land: the kick can only fire while
		//airborne and rising, so re-arming jump inside the last stretch of the arc
		//just leaves jump input latched when we touch down and registers as another
		//hop. Bail at least 30ms before touchdown instead (with an extra 2-think
		//margin so slow think ticks don't slip a fresh press in under the wire).
		//Trace for the actual ground distance below us so landing on higher/lower
		//ground than the jump start still times out correctly.
		if (NewBotAI_FlipkickMsToGround(bs) < 30.0f + (2.0f * FRAMETIME))
		{
			trap->EA_MoveForward(bs->client);
			bs->flipkickInputTime = 0;
			bs->flipkickJumpHeld = qfalse;
			return;
		}
		trap->EA_MoveForward(bs->client);
		trap->EA_DelayedJump(bs->client);
		bs->flipkickInputTime = level.time + NewBotAI_GetFlipkickInputWindowMs();
		return;
	}

	if (!isGripSequence && NewBotAI_ShouldAvoidFlipkickForSafety(bs))
	{
		NewBotAI_PushHopRetryCooldown(bs);
		NewBotAI_DrainRollEscape(bs);
		return;
	}

	if (!isGripSequence && bs->lastFlipkickAttemptTime > level.time)
	{
		NewBotAI_PushHopRetryCooldown(bs);
		return;
	}

	if (!isGripSequence && bs->frame_Enemy_Len < 160 && VectorLengthSquared(bs->cur_ps.velocity) < 4900)
	{
		if (!NewBotAI_IsImmediateFlipkickContact(bs))
		{
			return;
		}
	}

	if (isGripSequence && bs->currentEnemy && bs->currentEnemy->client)
	{
		//Item 3: the engine's forward flipkick only actually triggers when the target is
		//inside a tight ~32-unit forward trace while we're airborne and rising (see the
		//comment on the gripkick's straight-forward-only movement below) - the old 160
		//unit / 90 degree gate here was far more generous than that real trigger, so the
		//bot was jumping well before a kick was actually going to land. Only allow the
		//jump once the gripped target is genuinely close and centered in front.
		//The facing check is YAW-ONLY: the engine's kick trace (bg_pmove.c) fires along
		//fwdAngles = (0, viewangles[YAW], 0), so pitch never factors into whether the
		//kick can land. A pitch-inclusive FOV test can never pass here - the gripkick
		//aims straight down (pitch 89) while the target floats in front of us - which
		//left bots holding enemies in place directly in front of them without ever
		//flipkicking.
		vec3_t a_fo;
		float yawDiff;

		VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
		vectoangles(a_fo, a_fo);
		yawDiff = AngleDifference(a_fo[YAW], bs->viewangles[YAW]);
		if (bs->frame_Enemy_Len > 110 || yawDiff > 20.0f || yawDiff < -20.0f)
		{
			return;
		}
	}
	else
	{
		if (bs->currentEnemy && bs->currentEnemy->client && bs->currentEnemy->client->ps.weapon == WP_SABER && bs->cur_ps.torsoTimer) {
			enemySwing = qtrue;
		}

		if (bs->currentEnemy && bs->currentEnemy->client && bs->cur_ps.saberMove == LS_A_T2B && (bs->currentEnemy->client->ps.saberMove != LS_READY || bs->currentEnemy->client->ps.weapon != WP_SABER || bs->currentEnemy->client->ps.fd.saberAnimLevel != SS_STRONG) && bs->frame_Enemy_Len < 180 && !enemySwing) {//In range and they can't block it
			if (bs->cur_ps.torsoTimer < 350 && bs->cur_ps.torsoTimer > 100 && bs->frame_Enemy_Len < 130) {
				return;
			}
		}
	}

	bs->lastFlipkickAttemptTime = level.time + 300;

	if (isGripSequence) {
		//Gripkick: repeat fresh jump presses while moving exclusively straight forward -
		//the engine's forward flipkick only fires on a fresh jump while we're airborne
		//and rising near the ground with the target in a 32-unit forward trace. The
		//BotUpdateInput flipkick toggle keeps releasing/re-pressing jump between thinks.
		trap->EA_Move(bs->client, vec3_origin, 0);
		NewBotAI_ClearRandomStrafeOverlay(bs);
		trap->EA_MoveForward(bs->client);
		trap->EA_Jump(bs->client);
		bs->flipkickInputTime = level.time + NewBotAI_GetFlipkickInputWindowMs();
		bs->flipkickJumpHeld = qtrue;
		return;
	}

	if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE - 1 || bs->cur_ps.fd.forceJumpZStart < 16) {//idk
		//Kill any latched lateral direction so the kick's input is exclusively straight
		//forward - mixing strafe into this caused diagonal wallruns off opponents and walls.
		trap->EA_Move(bs->client, vec3_origin, 0);
		NewBotAI_ClearRandomStrafeOverlay(bs);
		trap->EA_MoveForward(bs->client);
		trap->EA_Jump(bs->client);
		bs->flipkickInputTime = level.time + NewBotAI_GetFlipkickInputWindowMs();
		bs->flipkickJumpHeld = qtrue;
	}

	//if red swing and during the good part of anim and they are in range of saber dont kick them.. yet
	//Crouch removed: it interrupted the flipkick approach (crouch negates the kick's forward
	//movement window); just hold the kick instead.
	if (bs->currentEnemy && bs->currentEnemy->client && bs->cur_ps.saberMove == LS_A_T2B && (bs->currentEnemy->client->ps.saberMove != LS_READY || bs->currentEnemy->client->ps.weapon != WP_SABER || bs->currentEnemy->client->ps.fd.saberAnimLevel != SS_STRONG) && bs->frame_Enemy_Len < 180 && !enemySwing) {//In range and they can't block it
		if (!NewBotAI_IsImmediateFlipkickContact(bs) &&
			bs->cur_ps.torsoTimer < 250 && bs->cur_ps.torsoTimer > 100 && bs->frame_Enemy_Len < 110) {
			return;
		}
	}

	//The engine's forward flipkick (BOTH_WALL_FLIP_BACK1) is only triggered by moving
	//straight forward into the enemy while rapidly repeating jump inputs - it is not an
	//alt-attack move. Pressing alt-attack here would instead trigger a staff-style
	//(double bladed saber) kick/spin move, which is a different move entirely and must
	//not be mixed into a flipkick attempt. Keep this purely forward+jump.
	if (((bs->origin[2] - bs->cur_ps.fd.forceJumpZStart) > 24) && ((bs->origin[2] - bs->cur_ps.fd.forceJumpZStart) < 48))
	{
		trap->EA_DelayedJump(bs->client);
		bs->flipkickInputTime = level.time + NewBotAI_GetFlipkickInputWindowMs();
	}
}

void NewBotAI_ReactToBeingGripped(bot_state_t *bs) //Test this more, does it push?  wtf?
{
	vec3_t a_fo;
	qboolean useTheForce = qfalse;
	qboolean gripMistakeActive;
	qboolean currentEnemyGripThreat = qfalse;

	if (!(g_entities[bs->client].r.svFlags & SVF_BOT))
	{
		return;
	}
	if (bs->currentEnemy && bs->currentEnemy->client &&
		(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_GRIP)) &&
		bs->frame_Enemy_Len < 512.0f)
	{
		currentEnemyGripThreat = qtrue;
	}
	//If speed is active while gripped (by anyone), turn it off immediately so FP regen resumes
	//and follow-up escape powers (push/pull) become available again. Spam the force button
	//(press on one think, release on the next) like a human mashing the key: the engine only
	//toggles on a fresh press (forceButtonNeedRelease), so a held button can miss the toggle.
	if ((bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED)) &&
		(bs->cur_ps.fd.forcePowersKnown & (1 << FP_SPEED)) &&
		(currentEnemyGripThreat || bs->cur_ps.fd.forceGripBeingGripped > level.time))
	{
		bs->cur_ps.fd.forcePowerSelected = FP_SPEED;
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_SPEED;
		bs->gripSpeedOffPressed = !bs->gripSpeedOffPressed;
		if (bs->gripSpeedOffPressed)
		{
			trap->EA_ForcePower(bs->client);
		}
		return;
	}
	bs->gripSpeedOffPressed = qfalse;

	//Item 4: a fresh grip session is detected by a gap since the last think we were
	//reacting to being gripped (a continuous grip calls this every think). Roll the
	//escape delay once per session instead of re-rolling a chance every think - a
	//per-think chance converges to breaking free within a couple of thinks no matter
	//how high bot_gkmistakebias is set, which is why it never felt effective. Each fresh
	//session also rolls its own failure package: whether this grip is ever escaped by
	//a pull/push at all (low levels occasionally fail entirely), and the escape delay.
	if (bs->gripReactLastCallTime < level.time - 300)
	{
		const int neverEscapeChance = NewBotAI_GetGripNeverEscapeChance(bs);

		bs->gripSessionStartTime = level.time;
		bs->gripMistakeNeverEscape = (neverEscapeChance > 0 && Q_irand(1, 100) <= neverEscapeChance) ?
			(level.time + 10000) : -1;
		bs->gripMistakeDelayUntil = (bs->gripMistakeNeverEscape > 0) ? 0 :
			(level.time + NewBotAI_GetGripEscapeDelayMs(bs));
	}
	bs->gripReactLastCallTime = level.time;

	//Item 4: give a human player's grip a short window to actually build up speed before
	//any escape can land - levels 1-9 (not the perfect level 10, see
	//BotGetMistakeBiasChance) never successfully pull free within the first 100ms of a
	//fresh grip session, no matter how the mistake rolls above landed. This keeps the
	//bot's escape timing window more human-like instead of reacting to the very first
	//think of the grip.
	if (bs->settings.skill < 10.0f && level.time < bs->gripSessionStartTime + NEWBOTAI_GRIP_NO_ESCAPE_WINDOW_MS)
	{
		NewBotAI_Flipkick(bs);
		return;
	}

	//Item 4 (speed-weighted): being whipped around fast mid-grip rattles the bot - a
	//per-think speed roll can shove the escape timer back out, so the faster the
	//gripper moves us the longer the escape takes on top of the rolled delay. The
	//extension window (75-225ms) is gated by the speed chance and now weighted a
	//little heavier than before so a genuinely fast gripkick jerk chain holds the
	//escape off noticeably longer than chance alone would.
	if (bs->gripMistakeNeverEscape <= 0 && bs->gripMistakeDelayUntil <= level.time + 1000)
	{
		const int speedMistakeChance = NewBotAI_GetGripSpeedMistakeChance(bs);
		if (speedMistakeChance > 0 && Q_irand(1, 100) <= speedMistakeChance)
		{
			bs->gripMistakeDelayUntil = level.time + Q_irand(75, 225);
		}
	}

	gripMistakeActive = (bs->gripMistakeDelayUntil > level.time) ? qtrue : qfalse;

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
	vectoangles(a_fo, a_fo);
	
	if (!(g_forcePowerDisable.integer & (1 << FP_ABSORB)) && bs->cur_ps.fd.forcePowersKnown & (1 << FP_ABSORB)) {//Can pull and not push
		if (bs->cur_ps.fd.forcePower >= 12) {
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB;
			useTheForce = qtrue;
		}
	}
	else if (!(g_forcePowerDisable.integer & (1 << FP_PULL)) && !(g_forcePowerDisable.integer & (1 << FP_PUSH)) && (bs->cur_ps.fd.forcePowersKnown & (1 << FP_PULL)) && (bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH))) {//Can push or pull
		if (bs->cur_ps.fd.forcePower >= 20 && InFieldOfVision(bs->viewangles, 50, a_fo)) {
			if (g_entities[bs->client].health < 30 && bs->gripMistakeNeverEscape <= 0) {
				//Low on health: pull free immediately. The late push out of a grip lost the
				//exchange in the duel data, so only lower levels keep it as a mistake.
				if (bs->settings.skill >= 7.0f)
				{
					level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
					NewBotAI_ApplyGripEscapePullMistake(bs);
				}
				else
				{
					level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
				}
				useTheForce = qtrue;
			}
			else {
				if (bs->gripMistakeNeverEscape > 0)
				{
					//bot_gkmistakebias rolled a total escape failure for this grip: keep
					//kick-struggling and just wait the grip out - no pull, no push.
					NewBotAI_Flipkick(bs);
					return;
				}
				if (gripMistakeActive)
				{
					NewBotAI_Flipkick(bs);
					return;
				}
				if (bs->gripMistakeReverseUntil < level.time)
				{
					//The fumble chance stacks the per-attempt mistakebias roll with the
					//speed pressure of being moved fast mid-grip - a hard, fast jerk is
					//what rattles the bot into shoving instead of pulling free. Item 4:
					//weight the speed-driven pressure more heavily than the flat random
					//mistakebias roll, so a genuinely fast gripkick jerk is the dominant
					//reason for a fumble rather than chance alone.
					int pushInsteadChance = NewBotAI_GetGripPushInsteadChance(bs) +
						(int)(NewBotAI_GetGripSpeedMistakeChance(bs) * 1.5f);
					if (pushInsteadChance > 100)
					{
						pushInsteadChance = 100;
					}
					if (pushInsteadChance > 0 && Q_irand(1, 100) <= pushInsteadChance)
					{
						//Confused escape: shove the gripper away with push instead of
						//pulling free. Only one fumbled direction per window - no
						//instant re-roll back to the correct pull next think.
						bs->gripMistakeReverseUntil = level.time + Q_irand(250, 500);
					}
				}
				if (bs->gripMistakeReverseUntil > level.time)
				{
					level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
				}
				else
				{
					level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
					NewBotAI_ApplyGripEscapePullMistake(bs);
				}
				useTheForce = qtrue;
			}
		}
	}
	else if (!(g_forcePowerDisable.integer & (1 << FP_PULL)) && bs->cur_ps.fd.forcePowersKnown & (1 << FP_PULL)) {//Can pull and not push
		if (bs->cur_ps.fd.forcePower >= 20 && InFieldOfVision(bs->viewangles, 50, a_fo)) {
			if (bs->gripMistakeNeverEscape > 0)
			{
				//bot_gkmistakebias rolled a total escape failure for this grip: keep
				//kick-struggling and just wait the grip out - no pull.
				NewBotAI_Flipkick(bs);
				return;
			}
			if (gripMistakeActive)
			{
				NewBotAI_Flipkick(bs);
				return;
			}
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
			NewBotAI_ApplyGripEscapePullMistake(bs);
			useTheForce = qtrue;
		}
	}
	else if (!(g_forcePowerDisable.integer & (1 << FP_PULL)) && bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH)) {//Can push and not pull
		if (bs->cur_ps.fd.forcePower >= 20 && InFieldOfVision(bs->viewangles, 50, a_fo)) {
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
			useTheForce = qtrue;
		}
	}
	else if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED)) { // we are speeding
		if (bs->cur_ps.fd.forcePowersKnown & (1 << FP_SPEED)) {//make sure..
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_SPEED; //lets turn off speed
			useTheForce = qtrue;
		}
	}

	//if (bs->cur_ps.fd.forcePower >= 20 && InFieldOfVision(bs->viewangles, 50, a_fo)) {
	//	level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
	//	useTheForce = qtrue;
	//}

	//No flipkick here - while we are gripping, NewBotAI_Gripkick owns all inputs, and
	//Flipkick's low-health drain-roll escape fires a random attack that would break
	//the gripkicks' "movement + held grip + flipkick only" input discipline.

	if (useTheForce && (level.framenum % 2)) {
		trap->EA_ForcePower(bs->client);
	}
}

// bot_bully hazard check: trace backward off the bot's heels looking for a lethal fall,
// lava, or a death pit. Used while gripping so a bully bot standing with its back to a
// hazard can hurl its opponent over the edge during the backward jerk phases.
static qboolean NewBotAI_GripHazardBehind(bot_state_t *bs)
{
	vec3_t start, end, fwd;
	trace_t tr;

	AngleVectors(bs->viewangles, fwd, NULL, NULL);

	// A point just behind the bot - the jerk phases back the bot (and the gripped
	// target it drags along) toward this spot.
	VectorCopy(bs->origin, start);
	start[0] -= fwd[0] * 48.0f;
	start[1] -= fwd[1] * 48.0f;

	// Look for ground below that point. No ground within 256 units means a lethal
	// fall; CONTENTS_LAVA/CONTENTS_NODROP on the ground means lava or a death pit.
	VectorCopy(start, end);
	end[2] -= 256.0f;

	JP_Trace(&tr, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction == 1.0f)
	{
		return qtrue;
	}

	if (tr.contents & (CONTENTS_LAVA | CONTENTS_NODROP))
	{
		return qtrue;
	}

	return qfalse;
}

//Item 3: at FORCE_LEVEL_3 grip the target's own facing check (InFront in w_force.c) is
//not enforced, so jerks there can use the full dramatic 145-200 degree yaw swing. Below
//that level the same facing check still applies (see w_force.c's InFront gate on
//FORCE_LEVEL_1/2 grip) - exceeding its ~26 degree cone ends the grip immediately
//regardless of us still holding the grip key - so those levels keep a small, safe offset.
void NewBotAI_Gripkick(bot_state_t *bs)
{
	//float heightDiff = bs->cur_ps.origin[2] - bs->currentEnemy->client->ps.origin[2]; //We are above them by this much
	const int gripkickBonus = BotGetAggressionWeightedBonus(bs, BotGetChanceBiasPercent(bot_gripkickbias.value), 30, qtrue);

	if (!bs->gripkickActive)
	{
		bs->gripkickActive = qtrue;
		bs->gripkickJerkUntil = 0;
		bs->gripkickJerkCount = 0;
		bs->gripkickKickCount = 0;
		bs->gripkickJerkDirection = 0;
		bs->gripkickJerkYawOffset = 0.0f;
		bs->gripkickJerkPitch = -70.0f;
		bs->gripkickPitchVariant = Q_irand(0, 1);
		bs->gripkickAttemptTime = 0;
		bs->gripkickDwellUntil = 0;
		bs->gripkickLookDownUntil = 0;
		bs->gripkickRestackDir = 0;
		if (BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim))
		{
			bs->gripkickJerkCount = Q_irand(1, 2);
		}
	}

	//Grip initiation moves straight forward from the very first think - no backward
	//opener tap. Backward movement is reserved for after a successful flipkick (the
	//jerk phases below).
	if (BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim)) { //Splat and enough time? - how to see if they are splattable and were not gripped during midair. forcejumpzheight ?
		float heightdiff = bs->currentEnemy->client->ps.origin[2] - bs->eye[2]; //Them minus ours,  they are 500, we are 300. height diff is 200.

		if (heightdiff < 110) {
			bs->ideal_viewangles[PITCH] = -90; //60?
		}
		else {
			bs->ideal_viewangles[PITCH] = 90; //-80
		}
		if (bot_bully.integer && bs->currentEnemy->client->ps.velocity[2] < -400) {//going fast enough to die, let go
			return;
		}
		//bot_bully is off: keep holding the grip instead of releasing for the splat.
		//not a good splat, has to predict based on their momentum
	}
	else {
		//Gripkick discipline: movement, a held force grip, and flipkicks - nothing else.
		//Move exclusively straight forward while bringing the gripped target in close for
		//the flipkick. After a confirmed kick (or during the jerk windows below) move
		//exclusively backward while the yaw jerk swings them, until the jerk brings them
		//back into a reasonably centered forward yaw lane - then look down, move forward,
		//and let NewBotAI_Flipkick take the actual jump once the tighter engine trigger is
		//ready.
		vec3_t a_fo;
		qboolean targetInFront;
		qboolean attemptedKick = qfalse;
		qboolean successfulKick = qfalse;
		qboolean enemyOnTopOfUs;
		qboolean weAreOnTopOfEnemy;

		(void)gripkickBonus; //jerk pitch is fully randomized now (see the jerk phases below)

		VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
		vectoangles(a_fo, a_fo);
		targetInFront = InFieldOfVision(bs->viewangles, 90, a_fo);

		//The approach below moves forward until the target is basically touching us, so
		//one of us can easily end up stacked on the other. Track both stackings - the
		//target landed on us (we must step out from under them before they can be in
		//front for the flipkick), and us landed on them (the next flipkick attempt is
		//skipped straight to the jerk phase, since kicking while standing on their head
		//just whiffs off the top).
		enemyOnTopOfUs = (qboolean)(bs->frame_Enemy_Len < 70.0f &&
			bs->cur_ps.groundEntityNum == bs->currentEnemy->s.number);
		weAreOnTopOfEnemy = (qboolean)(bs->frame_Enemy_Len < 70.0f &&
			bs->currentEnemy->client->ps.groundEntityNum == bs->client);

		//Only a confirmed kick counts - our own kick/flip animation playing, or the
		//gripped target knocked down right after our attempt. Counting mere attempts
		//here was what cycled the bot into the next jerk phase without ever landing
		//the kick.
		if (bs->gripkickAttemptTime > level.time - 800)
		{
			switch (bs->cur_ps.legsAnim)
			{
			case BOTH_A7_KICK_F:
			case BOTH_A7_KICK_B:
			case BOTH_A7_KICK_R:
			case BOTH_A7_KICK_L:
			case BOTH_A7_KICK_S:
			case BOTH_A7_KICK_BF:
			case BOTH_A7_KICK_RL:
			case BOTH_A7_KICK_F_AIR:
			case BOTH_A7_KICK_B_AIR:
			case BOTH_A7_KICK_R_AIR:
			case BOTH_A7_KICK_L_AIR:
			case BOTH_WALL_FLIP_BACK1:
				successfulKick = qtrue;
				break;
			default:
				break;
			}
			if (BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim))
			{
				successfulKick = qtrue;
			}
		}

		if (successfulKick && bs->gripkickKickCount < 3)
		{
			bs->gripkickAttemptTime = 0;
			bs->gripkickDwellUntil = 0;
			bs->gripkickLookDownUntil = 0;
			bs->gripkickRestackDir = 0;
			bs->gripkickKickCount++;
			bs->lastGripkickSuccessTime = level.time;
			if (weAreOnTopOfEnemy)
			{
				//Landed on the opponent: skipping the next flipkick attempt means
				//kicking again from on top of them just whiffs - begin the next jerk
				//phase immediately instead.
				bs->gripkickJerkCount = Q_irand(1, 2);
			}
			else
			{
				bs->gripkickJerkCount = (bs->gripkickKickCount == 1) ? Q_irand(1, 3) : Q_irand(1, 2);
			}
			bs->gripkickJerkUntil = 0;
			bs->gripkickJerkYawOffset = 0.0f;
		}

		if (bot_bully.integer && bs->gripkickKickCount == 0 && bs->gripkickJerkCount > 0 &&
			bs->gripkickJerkUntil > level.time && NewBotAI_GripHazardBehind(bs))
		{
			//bot_bully: our back is to lava or a lethal fall and the first jerk phase is
			//already swinging the target past us toward it - let go mid-jerk so momentum
			//carries them off the edge. Just stop holding the grip key; the natural grip
			//release keeps the target flying along the jerk arc.
			bs->ideal_viewangles[YAW] = AngleNormalize360(bs->ideal_viewangles[YAW]);
			bs->ideal_viewangles[PITCH] = AngleNormalize360(bs->ideal_viewangles[PITCH]);
			return;
		}

		if (bs->gripkickJerkUntil > level.time) {
			bs->gripkickLookDownUntil = 0;
			//Jerk phases only move backward while looking up; do not issue
			//jump, attack, or other competing inputs during them. Yaw is kept
			//anchored to a_fo (the true, live direction to the gripped target)
			//plus the clamped jerk offset every frame - a static/accumulating
			//yaw here can drift far enough from the target to fail the force
			//grip's own facing check and auto-break the grip well before our
			//intended hold duration.
			bs->ideal_viewangles[YAW] = a_fo[YAW] + bs->gripkickJerkYawOffset;
			bs->ideal_viewangles[PITCH] = bs->gripkickJerkPitch;
			trap->EA_Move(bs->client, vec3_origin, 0);
			NewBotAI_RetreatStraight(bs);
		}
		else if (bs->gripkickJerkCount > 0) {
			const int dwellPercent = Com_Clampi(10, 300, bot_gripkickdwell.integer);
			const int gripLevel = bs->cur_ps.fd.forcePowerLevel[FP_GRIP];
			bs->gripkickLookDownUntil = 0;
			//Item 3: the force grip's own facing check (see ForceGrip in w_force.c) is only
			//enforced below FORCE_LEVEL_3 - at max grip level the target can be jerked hard
			//without auto-breaking the grip, so give it the full dramatic 145-200 degree yaw
			//swing there. Lower grip levels still need to stay inside the ~25 degree facing
			//cone or the grip breaks early, so they keep the old small, safe offset.
			const int yawMagnitude = (gripLevel >= FORCE_LEVEL_3) ? Q_irand(145, 200) : Q_irand(15, 20);

			//Complete one randomized jerk at a time so bot_gripkickdwell controls
			//each upward hold rather than collapsing the whole sequence into one
			//long phase.
			bs->gripkickJerkCount--;
			bs->gripkickJerkDirection = (Q_irand(0, 1) * 2) - 1;
			bs->gripkickJerkUntil = level.time +
				(Q_irand(550, 900) * dwellPercent) / 100;
			//Each jerk independently rolls its own random yaw direction/magnitude - no
			//accumulation across jerks within the same phase.
			bs->gripkickJerkYawOffset = (float)(bs->gripkickJerkDirection * yawMagnitude);
			//Split upward jerk pitch per grip sequence:
			//variant A uses 20-60, variant B uses 50-90.
			if (bs->gripkickPitchVariant == 0)
			{
				bs->gripkickJerkPitch = -(float)Q_irand(20, 60);
			}
			else
			{
				bs->gripkickJerkPitch = -(float)Q_irand(50, 90);
			}
			bs->ideal_viewangles[YAW] = a_fo[YAW] + bs->gripkickJerkYawOffset;
			bs->ideal_viewangles[PITCH] = bs->gripkickJerkPitch;
			trap->EA_Move(bs->client, vec3_origin, 0);
			NewBotAI_RetreatStraight(bs);
		}
		else if (bs->gripkickDwellUntil > level.time) {
			bs->gripkickLookDownUntil = 0;
			//Hold the target straight down while moving forward, but do not yaw back
			//toward them until the actual flipkick attempt window. Pre-rotating here was
			//pushing the target away before the kick landed.
			bs->ideal_viewangles[YAW] = bs->viewangles[YAW];
			bs->ideal_viewangles[PITCH] = 89;
			trap->EA_Move(bs->client, vec3_origin, 0);
			trap->EA_MoveForward(bs->client);
		}
		else if (bs->gripkickKickCount >= 3) {
			bs->gripkickLookDownUntil = 0;
			//Item 1B: after the 3rd confirmed grip flipkick, stop approaching/kicking
			//entirely and just hold the target gripped (aimed at them) until grip ends
			//on its own (max grip duration) or the target escapes/dies.
			bs->ideal_viewangles[YAW] = a_fo[YAW];
			bs->ideal_viewangles[PITCH] = a_fo[PITCH];
			trap->EA_Move(bs->client, vec3_origin, 0);
		}
		else if (targetInFront) {
			if (enemyOnTopOfUs || weAreOnTopOfEnemy)
			{
				//Vertical stacking breaks forward-kick reliability in both directions:
				//if they are on us, step out from underneath; if we are on them, step
				//off before any flipkick attempt. Once we pick a restack direction, keep
				//holding it until the vertical overlap is actually gone instead of
				//flipping directions from think to think.
				if (!bs->gripkickRestackDir)
				{
					bs->gripkickRestackDir = (enemyOnTopOfUs || targetInFront) ? -1 : 1;
				}
				bs->ideal_viewangles[YAW] = a_fo[YAW];
				bs->ideal_viewangles[PITCH] = 89;
				trap->EA_Move(bs->client, vec3_origin, 0);
				if (bs->gripkickRestackDir < 0)
				{
					trap->EA_MoveBack(bs->client);
				}
				else
				{
					trap->EA_MoveForward(bs->client);
				}
				goto gripkick_finalize;
			}
			bs->gripkickRestackDir = 0;
			//Once the target is in the forward kick cone, lock straight to them, look down,
			//and briefly hold still so grip drag can settle the target into flipkick range.
			bs->ideal_viewangles[YAW] = a_fo[YAW];
			bs->ideal_viewangles[PITCH] = 89;
			trap->EA_Move(bs->client, vec3_origin, 0);
			//Keep walking in through the settle like jundon instead of standing still.
			if (bs->frame_Enemy_Len > NEWBOTAI_GRIPKICK_SETTLE_MIN_RANGE)
				trap->EA_MoveForward(bs->client);
			if (bs->gripkickLookDownUntil <= 0)
			{
				bs->gripkickLookDownUntil = level.time + NEWBOTAI_GRIPKICK_LOOKDOWN_SETTLE_MS;
			}
			if (bs->gripkickLookDownUntil <= level.time && bs->frame_Enemy_Len <= 130)
			{
				//NewBotAI_Flipkick skips its own post-attempt cooldown for grip
				//sequences, so enforce it here - otherwise the kick is re-offered
				//every think while out of range and each offer resets the 300ms
				//dwell below, and a target making contact never gets a fresh jump.
				const int previousAttempt = bs->lastFlipkickAttemptTime;
				if (previousAttempt <= level.time)
				{
					NewBotAI_Flipkick(bs);
					attemptedKick = (bs->lastFlipkickAttemptTime != previousAttempt) ? qtrue : qfalse;
				}
				else
				{
					attemptedKick = qfalse;
				}
				if (attemptedKick)
				{
					const int dwellPercent = Com_Clampi(10, 300, bot_gripkickdwell.integer);

					//An unconfirmed attempt dwells here (aim-down hold) before the next
					//forward approach instead of immediately cycling into a jerk. Aim-down
					//dwells run the same length as the upward jerks for the same
					//bot_gripkickdwell setting.
					bs->gripkickAttemptTime = level.time;
					bs->gripkickDwellUntil = level.time + (Q_irand(NEWBOTAI_GRIPKICK_RETRY_DWELL_MIN_MS,
						NEWBOTAI_GRIPKICK_RETRY_DWELL_MAX_MS) * dwellPercent) / 100;
					bs->gripkickLookDownUntil = 0;
				}
			}
		}
		else {
			//The target drifted outside the forward kick cone (or landed on top of us):
			//keep rotating yaw toward them, but issue no strafe input during gripkick.
			//Move straight forward to re-center, or straight back only when stacked.
			bs->ideal_viewangles[PITCH] = 89;
			bs->gripkickLookDownUntil = 0;
			if (enemyOnTopOfUs || weAreOnTopOfEnemy)
			{
				if (!bs->gripkickRestackDir)
				{
					bs->gripkickRestackDir = enemyOnTopOfUs ? -1 : 1;
				}
			}
			else
			{
				bs->gripkickRestackDir = 0;
			}
			if (enemyOnTopOfUs)
			{
				//They are stacked on us - face the target's true direction so the
				//straight back step resolves cleanly instead of wandering off the
				//accumulating yaw correction below.
				bs->ideal_viewangles[YAW] = a_fo[YAW];
			}
			else
			{
				bs->ideal_viewangles[YAW] = a_fo[YAW];
			}
			trap->EA_Move(bs->client, vec3_origin, 0);
			if (bs->gripkickRestackDir < 0)
			{
				trap->EA_MoveBack(bs->client);
			}
			else
			{
				trap->EA_MoveForward(bs->client);
			}
		}
	}

gripkick_finalize:
	bs->ideal_viewangles[YAW] = AngleNormalize360(bs->ideal_viewangles[YAW]); //Normalize the angles
	bs->ideal_viewangles[PITCH] = AngleNormalize360(bs->ideal_viewangles[PITCH]);

	trap->EA_ForcePower(bs->client); //Always hold grip key during grip
}

void NewBotAI_Draining(bot_state_t *bs)
{
	const int ourHealth = g_entities[bs->client].health;
	const int hisForce = bs->currentEnemy->client->ps.fd.forcePower;
	const qboolean enemyVisible = (OrgVisible(bs->eye, bs->currentEnemy->client->ps.origin, bs->client) &&
		!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))) ? qtrue : qfalse;
	int drainTapTargetTicks;
	qboolean healDrainlock;
	const qboolean safeDrainVsThrow = NewBotAI_ShouldPlaySafeDrainVsSaberThrow(bs);
	const qboolean jumpDrainThreat = NewBotAI_ShouldJumpDrainVsSaberThrow(bs);
	const qboolean flipkickDrainEscape = (safeDrainVsThrow && NewBotAI_ShouldPreferFlipkickOverThrow(bs)) ? qtrue : qfalse;
	const qboolean stabilizeVsSaberThrow = NewBotAI_ShouldStabilizeAgainstEnemySaberThrow(bs);
	const qboolean maintainDrainlockTaps = (NewBotAI_IsPullkickDrainWindow(bs) || NewBotAI_IsDrainlockAdvantage(bs)) ? qtrue : qfalse;
	qboolean shouldHold = qfalse;
	int holdMs = 0;

	NewBotAI_UpdateHealDrainlockState(bs);
	drainTapTargetTicks = NewBotAI_GetDrainTapTargetTicks(bs);
	healDrainlock = NewBotAI_ShouldHealDrainlock(bs);

	if (!enemyVisible)
	{
		bs->drainHoldTime = 0;
		if (bs->cur_ps.fd.forcePowersActive & (1 << FP_DRAIN))
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
			trap->EA_ForcePower(bs->client);
		}
		return;
	}

	if ((ourHealth < 100 || maintainDrainlockTaps) && hisForce && enemyVisible)
	{
		//Ordinary health-biased bots just want minimal drain taps to top their own health off,
		//so they release the drain key almost immediately (one think) instead of holding it
		//down. Heal-driven drainlocks and force-biased pullkick setups both hold exactly long
		//enough for the computed whole-tick FP removal, not a moment longer.
		if (drainTapTargetTicks > 0 &&
			(healDrainlock || maintainDrainlockTaps))
		{
			holdMs = (drainTapTargetTicks * NEWBOTAI_DRAIN_TICK_MSEC) + 1; //hold through the last full drain tick
		}
		else
		{
			holdMs = 100 + BotGetDrainHoldBiasMs(bs);
		}

		if (holdMs < 20)
		{
			holdMs = 20;
		}

		shouldHold = qtrue;
		bs->drainHoldTime = level.time + holdMs;
	}
	else if (bs->drainHoldTime > level.time)
	{
		shouldHold = qtrue;
	}
	else if ((healDrainlock || maintainDrainlockTaps) && hisForce && enemyVisible)
	{
		//If a heal-driven deep-drain tap was truncated by our current FP pool, keep holding
		//drain so we immediately resume channeling as force regenerates instead of breaking
		//the drainlock into a different action before we're topped off (or while keeping
		//a pullkick drainlock loop active at full health).
		shouldHold = qtrue;
	}

	if (shouldHold)
	{
		trap->EA_ForcePower(bs->client);
	}

	if (safeDrainVsThrow)
	{
		if (NewBotAI_ShouldEmergencyDrainRollSaberThrow(bs))
		{
			NewBotAI_ApplySidewaysDrainRoll(bs, qfalse);
		}
		else if (jumpDrainThreat)
		{
			if (!stabilizeVsSaberThrow && (flipkickDrainEscape || NewBotAI_IsDrainlockAdvantage(bs)))
			{
				trap->EA_MoveForward(bs->client);
			}
			else
			{
				NewBotAI_RetreatDiagonal(bs, (level.framenum & 1) ? qtrue : qfalse);
			}
			trap->EA_Jump(bs->client);
		}
		else if (!flipkickDrainEscape || stabilizeVsSaberThrow)
		{
			NewBotAI_RetreatDiagonal(bs, (level.framenum & 1) ? qtrue : qfalse);
		}
	}

	//Between drain taps, or as soon as the enemy is drained low enough to be pullable, go for
	//the pullkick follow-up instead of standing in the drain.
	if (!healDrainlock && !maintainDrainlockTaps &&
		(flipkickDrainEscape || hisForce < 20 || bs->drainHoldTime <= level.time) &&
		!jumpDrainThreat)
	{
		NewBotAI_Flipkick(bs);
	}
}

void NewBotAI_Speeding(bot_state_t *bs)
{
	const qboolean enemyKnockedDown = (bs->currentEnemy && bs->currentEnemy->client &&
		BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim)) ? qtrue : qfalse;
	const qboolean beingGripped = (bs->cur_ps.fd.forceGripBeingGripped > level.time) ? qtrue : qfalse;
	const qboolean enemyGripActive = (bs->currentEnemy && bs->currentEnemy->client &&
		(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_GRIP))) ? qtrue : qfalse;
	const qboolean enemyDrainActive = (bs->currentEnemy && bs->currentEnemy->client &&
		(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN))) ? qtrue : qfalse;
	const qboolean imminentSaberThrowThreat = NewBotAI_IsEnemySaberThreatImminent(bs);
	int ourHealthTotal = g_entities[bs->client].health + bs->cur_ps.stats[STAT_ARMOR];
	int enemyHealthTotal = 0;
	int enemyHealth = 0;
	int enemyArmor = 0;
	int enemyForce = 0;
	const int ourForce = bs->cur_ps.fd.forcePower;
	qboolean lostAdvantage = qfalse;
	qboolean trappedByDrain = qfalse;
	qboolean enemyGripThreat = qfalse;
	qboolean speedFinisherWindow = qfalse;

	if (bs->currentEnemy && bs->currentEnemy->client)
	{
		enemyHealth = bs->currentEnemy->health;
		enemyArmor = bs->currentEnemy->client->ps.stats[STAT_ARMOR];
		enemyHealthTotal = enemyHealth + enemyArmor;
		enemyForce = bs->currentEnemy->client->ps.fd.forcePower;
		speedFinisherWindow = NewBotAI_IsSpeedFinisherWindow(enemyHealth, enemyArmor) ? qtrue : qfalse;
		enemyGripThreat = (enemyGripActive && bs->frame_Enemy_Len < 512.0f) ? qtrue : qfalse;
		if (ourHealthTotal + 10 < enemyHealthTotal || ourForce + 15 < enemyForce)
		{
			lostAdvantage = qtrue;
		}
		if (NewBotAI_ShouldAbortSpeedAttack(
			beingGripped ? 1 : 0,
			enemyGripActive ? 1 : 0,
			enemyDrainActive ? 1 : 0,
			ourForce,
			ourForce - enemyForce,
			bs->frame_Enemy_Len,
			MAX_DRAIN_DISTANCE))
		{
			trappedByDrain = qtrue;
		}
	}

	if (enemyKnockedDown ||
		beingGripped ||
		trappedByDrain ||
		enemyGripThreat ||
		imminentSaberThrowThreat ||
		!speedFinisherWindow ||
		lostAdvantage ||
		(g_entities[bs->client].health) < 50 ||
		(ourForce < 20))
	{
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_SPEED;
		trap->EA_ForcePower(bs->client);
	}
}

void NewBotAI_Raging(bot_state_t *bs)
{
	//Never re-apply rage - bots don't use force rage at all anymore. If it's somehow
	//still active, just let it expire on its own.
}

void NewBotAI_Protecting(bot_state_t *bs)
{
	qboolean stopProtecting = qfalse;

	//Don't protect if they are out of range
	if (!bs->frame_Enemy_Vis)
		stopProtecting = qtrue;
	else if (bs->frame_Enemy_Len > 512) {
		if (bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB)) {
			if ((bs->cur_ps.fd.forcePower < 30))
				stopProtecting = qtrue;
		}
		else {
			if ((bs->cur_ps.fd.forcePower < 40))
				stopProtecting = qtrue;
		}
	}

	if (!bs->currentEnemy->client->ps.saberEntityNum) { //They have a dropped saber = no regen
		if (bs->currentEnemy->client->ps.fd.forcePower < 35)
			stopProtecting = qtrue;
	}

	if (bs->frame_Enemy_Len > 1024)
		stopProtecting = qtrue;

	if (stopProtecting) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PROTECT;
		trap->EA_ForcePower(bs->client);
	}
}

void NewBotAI_Absorbing(bot_state_t *bs)
{
	qboolean stopAbsorbing = qfalse;
	if (!bs->frame_Enemy_Vis)
		stopAbsorbing = qtrue;
	else if (NewBotAI_GetDist(bs) > MAX_DRAIN_DISTANCE || ((bs->currentEnemy->client->ps.fd.forcePower < 20) && (!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN))))) {
		if ((bs->cur_ps.fd.forceGripBeingGripped < level.time)) //Not being gripped
			stopAbsorbing = qtrue;
	}

	if (!bs->currentEnemy->client->ps.saberEntityNum) { //They have a dropped saber = no regen
		if (bs->currentEnemy->client->ps.fd.forcePower < 35)
			stopAbsorbing = qtrue;
	}

	if (stopAbsorbing) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB;
		trap->EA_ForcePower(bs->client);
	}
}

// Tracks whether our thrown saber is still heading toward the target and whether it has
// flown past it once (sticky for the current throw).
static qboolean NewBotAI_UpdateSaberThrowPass(bot_state_t *bs, qboolean *headingToTarget)
{
	gentity_t *saberEnt;
	vec3_t saberToEnemy;

	*headingToTarget = qfalse;
	if (!bs->currentEnemy || !bs->currentEnemy->client || !bs->cur_ps.saberInFlight ||
		bs->cur_ps.saberEntityNum <= 0 || bs->cur_ps.saberEntityNum >= ENTITYNUM_WORLD)
		return bs->saberThrowPassedTarget;
	saberEnt = &g_entities[bs->cur_ps.saberEntityNum];
	if (!saberEnt->inuse)
		return bs->saberThrowPassedTarget;
	VectorSubtract(bs->currentEnemy->client->ps.origin, saberEnt->r.currentOrigin, saberToEnemy);
	saberToEnemy[2] = 0.0f;
	if (VectorLengthSquared(saberEnt->s.pos.trDelta) > 1.0f)
	{
		vec3_t flight;
		VectorCopy(saberEnt->s.pos.trDelta, flight);
		flight[2] = 0.0f;
		if (DotProduct(flight, saberToEnemy) > 0.0f)
			*headingToTarget = qtrue;
		else if (bs->saberThrowStartTime > 0 &&
			NewBotAI_SaberThrowMayMarkPass((newbotai_throw_phase_t)bs->saberThrowPhase,
				level.time - bs->saberThrowStartTime, level.time - bs->saberThrowPhaseTime,
				NewBotAI_SaberThrowSteerCadence(bs->cur_ps.fd.forcePowerLevel[FP_SABERTHROW])))
			bs->saberThrowPassedTarget = qtrue;
	}
	return bs->saberThrowPassedTarget;
}

void NewBotAI_SaberThrowing(bot_state_t* bs)
{
	if (NewBotAI_HasDroppedOwnSaber(bs))
		return;
	if (bs->saberThrowStartTime <= 0)
		bs->saberThrowStartTime = bs->cur_ps.saberDidThrowTime > 0 ?
			bs->cur_ps.saberDidThrowTime : level.time;

	if (!NewBotAI_SaberThrowShouldHold(bs, level.time - bs->saberThrowStartTime, qfalse))
	{
		bs->saberThrowPhase = NEWBOTAI_THROW_RECALL;
		return;
	}
	trap->EA_Alt_Attack(bs->client);
	NewBotAI_TrySaberThrowDefenseBreak(bs);
}

float BS_GroundDistance(bot_state_t *bs)
{
	return (bs->origin[2] - bs->cur_ps.fd.forceJumpZStart);
}

float NewBotAI_GetSpeedTowardsEnemy(bot_state_t *bs)
{
	if (bs->currentEnemy)
	{
		float dot;
		qboolean n = qfalse;
		vec3_t diff, eorg;

		VectorCopy(bs->currentEnemy->client->ps.origin, eorg);
		VectorSubtract(eorg, bs->eye, diff);
		dot = DotProduct(diff, bs->cur_ps.velocity); //Should take into account enemy velocity too?
		if (dot < 0) {
			dot = -dot;
			n = qtrue;
		}
		dot = sqrt(dot);
		if (n)
			dot = -dot;
		return dot;//Should we cancel the Z axis on this?
	}
	return 0;
}


int NewBotAI_GetTribesWeapon(bot_state_t *bs)
{
	const int /*hisHealth = bs->currentEnemy->health,*/ distance = bs->frame_Enemy_Len;
//	int hisWeapon = WP_SABER;
	int bestWeapon = bs->cur_ps.weapon;
	const int forcedFireMode = level.clients[bs->client].forcedFireMode;

	bs->doAltAttack = 0;

//	if (bs->currentEnemy->client)
//		hisWeapon = bs->currentEnemy->client->ps.weapon;

	//Dependant on distance from enemy, enemys health, enemys weapon, and our health?


	if (distance > 3000) {
		if (BotWeaponSelectable(bs, WP_DISRUPTOR))
			bestWeapon = WP_DISRUPTOR;
		else if (BotWeaponSelectable(bs, WP_BLASTER) && ((bs->cur_ps.weapon != WP_BLASTER && bs->cur_ps.jetpackFuel == 100) || (bs->cur_ps.weapon == WP_BLASTER && bs->cur_ps.jetpackFuel > 10))) {
			bestWeapon = WP_BLASTER;
		}
		else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD) && ((bs->cur_ps.weapon == WP_BRYAR_OLD && bs->cur_ps.jetpackFuel == 100) || (bs->cur_ps.weapon == WP_BRYAR_OLD && bs->cur_ps.jetpackFuel > 10))) { //logic to let us run down to 0 but nto switch to 0
			bestWeapon = WP_BRYAR_OLD;
			bs->doAltAttack = 1;
			bs->altChargeTime = 800;
		}
		else if (BotWeaponSelectable(bs, WP_REPEATER) && forcedFireMode != 2)
			bestWeapon = WP_REPEATER;
		else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
			bestWeapon = WP_DEMP2;
			bs->doAltAttack = 1;
			bs->altChargeTime = 2100;
		}
		else if (BotWeaponSelectable(bs, WP_CONCUSSION) && bs->cur_ps.fd.forcePower > 90 && forcedFireMode != 1) {
			bestWeapon = WP_CONCUSSION;
			bs->doAltAttack = 1;
		}
		else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 2) {
			bestWeapon = WP_CONCUSSION;
		}
		else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER) && forcedFireMode != 2) {
			bestWeapon = WP_ROCKET_LAUNCHER;
		}
		else if (BotWeaponSelectable(bs, WP_FLECHETTE)) {
			bestWeapon = WP_FLECHETTE;
			bs->doAltAttack = 1;
		}
		else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
			bestWeapon = WP_SABER;
	}
	else if (distance > 800 && distance < 2800) { //Have some padding between distance tiers so we dont weaponswitch spam
		if (BotWeaponSelectableAltFire(bs, WP_BLASTER) && ((bs->cur_ps.weapon != WP_BLASTER && bs->cur_ps.jetpackFuel == 100) || (bs->cur_ps.weapon == WP_BLASTER && bs->cur_ps.jetpackFuel > 10))) {
			bestWeapon = WP_BLASTER;
		}
		else if (BotWeaponSelectable(bs, WP_REPEATER) && forcedFireMode != 2)
			bestWeapon = WP_REPEATER;
		else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD) && ((bs->cur_ps.weapon == WP_BRYAR_OLD && bs->cur_ps.jetpackFuel == 100) || (bs->cur_ps.weapon == WP_BRYAR_OLD && bs->cur_ps.jetpackFuel > 10))) {
			bestWeapon = WP_BRYAR_OLD;
			bs->doAltAttack = 1;
			bs->altChargeTime = 800;
		}
		else if (BotWeaponSelectableAltFire(bs, WP_BOWCASTER)) {
			bestWeapon = WP_BOWCASTER;
			bs->doAltAttack = 1;
		}
		else if (BotWeaponSelectable(bs, WP_CONCUSSION) && bs->cur_ps.fd.forcePower > 90 && forcedFireMode != 1) {
			bestWeapon = WP_CONCUSSION;
			bs->doAltAttack = 1;
		}
		else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 2) {
			bestWeapon = WP_CONCUSSION;
		}
		else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER) && forcedFireMode != 2) {
			bestWeapon = WP_ROCKET_LAUNCHER;
		}
		else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
			bestWeapon = WP_DISRUPTOR;
		else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 2) {
			bestWeapon = WP_DEMP2;
		}
		else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
			bestWeapon = WP_DEMP2;
			bs->doAltAttack = 1;
			bs->altChargeTime = 2100;
		}
		else if (BotWeaponSelectable(bs, WP_FLECHETTE)) {
			bestWeapon = WP_FLECHETTE;
			bs->doAltAttack = 1;
		}
		else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
			bestWeapon = WP_SABER;
	}
	else if (distance < 600) { //Most DPS!
		if (BotWeaponSelectableAltFire(bs, WP_THERMAL) && (bs->currentEnemy->client->ps.powerups[PW_REDFLAG] || bs->currentEnemy->client->ps.powerups[PW_BLUEFLAG] || bs->currentEnemy->client->ps.powerups[PW_NEUTRALFLAG])) {
			bestWeapon = WP_THERMAL;
		}
		if (BotWeaponSelectableAltFire(bs, WP_BLASTER) && ((bs->cur_ps.weapon == WP_BLASTER && bs->cur_ps.jetpackFuel == 100) || (bs->cur_ps.weapon == WP_BLASTER && bs->cur_ps.jetpackFuel > 10))) {
			bestWeapon = WP_BLASTER;
			bs->doAltAttack = 1;
		}
		else if (BotWeaponSelectable(bs, WP_FLECHETTE))
			bestWeapon = WP_FLECHETTE;
		else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER))
			bestWeapon = WP_ROCKET_LAUNCHER;
		else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 2)
			bestWeapon = WP_CONCUSSION;
		else if (BotWeaponSelectableAltFire(bs, WP_BOWCASTER)) {
			bestWeapon = WP_BOWCASTER;
			bs->doAltAttack = 1;
		}
		else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 2) {
			bestWeapon = WP_DEMP2;
		}
		else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
			bestWeapon = WP_DISRUPTOR;
		else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
			bestWeapon = WP_BRYAR_OLD;
			bs->doAltAttack = 1;
			bs->altChargeTime = 800;
		}
		else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
			bestWeapon = WP_BRYAR_PISTOL;
			bs->doAltAttack = 1;
			bs->altChargeTime = 1200;
		}
		else if (BotWeaponSelectable(bs, WP_CONCUSSION) && bs->cur_ps.fd.forcePower > 90 && forcedFireMode != 1) {
			bestWeapon = WP_CONCUSSION;
			bs->doAltAttack = 1;
		}
		else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
			bestWeapon = WP_DEMP2;
			bs->doAltAttack = 1;
			bs->altChargeTime = 2100;
		}
		else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
			bestWeapon = WP_SABER;
	}


	if (bs->currentEnemy->client && bs->currentEnemy->client->ps.weapon == WP_DEMP2) //dont charge if they can cancel it
		bs->altChargeTime = 50;

	if (forcedFireMode == 1)
		bs->doAltAttack = 0;
	else if (forcedFireMode == 2)
		bs->doAltAttack = 1;

	//todo- weapon table.

	return bestWeapon;
}

int NewBotAI_GetWeapon(bot_state_t *bs)
{
	const int /*hisHealth = bs->currentEnemy->health,*/ distance = bs->frame_Enemy_Len;
	int hisWeapon = WP_SABER;
	int bestWeapon = bs->cur_ps.weapon;
	const int forcedFireMode = level.clients[bs->client].forcedFireMode;

	bs->doAltAttack = 0;

	if (bs->currentEnemy->client)
		hisWeapon = bs->currentEnemy->client->ps.weapon;

	//Dependant on distance from enemy, enemys health, enemys weapon, and our health?

	if (hisWeapon == WP_SABER) { //Use splash damage if possible
		if (distance > 1300) {
			if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
			else if (BotWeaponSelectable(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
			}
			else if (distance > 500 && BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (distance > 500 && BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
				bestWeapon = WP_SABER;
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
		}
		else if (distance > 350 && distance < 1100) { //Have some padding between distance tiers so we dont weaponswitch spam
			if (distance < 900 && BotWeaponSelectableAltFire(bs, WP_REPEATER) && forcedFireMode != 1) {
				bestWeapon = WP_REPEATER;
				bs->doAltAttack = 1;
			}
			else if (distance < 768 && BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_LG) && !(g_tweakWeapons.integer & WT_STUN_HEAL)) {
				bestWeapon = WP_STUN_BATON;
			}
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectableAltFire(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BOWCASTER)) {
				bestWeapon = WP_BOWCASTER;
				bs->doAltAttack = 1;
			}
			else if (distance > 600 && BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (distance > 700 && BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
				bestWeapon = WP_SABER;
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
		}
		else if (distance < 200) {
			if (BotWeaponSelectableAltFire(bs, WP_FLECHETTE)) {
				bestWeapon = WP_FLECHETTE;
				if (!(g_tweakWeapons.integer & WT_STAKE_GUN))
					bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER) && forcedFireMode != 2)
				bestWeapon = WP_ROCKET_LAUNCHER;
			else if (BotWeaponSelectableAltFire(bs, WP_REPEATER)) {
				bestWeapon = WP_REPEATER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 2)
				bestWeapon = WP_CONCUSSION;
			else if (BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_SHOCKLANCE)) {
				bestWeapon = WP_STUN_BATON;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
				bestWeapon = WP_SABER;
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
		}
	}

	else if (hisWeapon == WP_ROCKET_LAUNCHER || hisWeapon == WP_REPEATER || hisWeapon == WP_CONCUSSION || hisWeapon == WP_FLECHETTE) { //Likely going to splash damage us, so dont bother trying to block with saber
		if (distance > 1024) {
			if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
			else if (BotWeaponSelectable(bs, WP_BLASTER))
				bestWeapon = WP_BLASTER;
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
				bestWeapon = WP_SABER;
		}
		else if (distance > 350 && distance < 900) { //Have some padding between distance tiers so we dont weaponswitch spam
			if (BotWeaponSelectableAltFire(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_REPEATER) && forcedFireMode != 2)
				bestWeapon = WP_REPEATER;
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (distance < 768 && BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_LG) && !(g_tweakWeapons.integer & WT_STUN_HEAL)) {
				bestWeapon = WP_STUN_BATON;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BOWCASTER)) {
				bestWeapon = WP_BOWCASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
				bestWeapon = WP_SABER;
		}
		else if (distance < 200) { //Most DPS!
			if (BotWeaponSelectable(bs, WP_REPEATER) && forcedFireMode != 2)
				bestWeapon = WP_REPEATER;
			else if (BotWeaponSelectableAltFire(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_FLECHETTE))
				bestWeapon = WP_FLECHETTE;
			else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER))
				bestWeapon = WP_ROCKET_LAUNCHER;
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 2)
				bestWeapon = WP_CONCUSSION;
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectableAltFire(bs, WP_BOWCASTER)) {
				bestWeapon = WP_BOWCASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_SHOCKLANCE)) {
				bestWeapon = WP_STUN_BATON;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER))
				bestWeapon = WP_SABER;
		}
	}


	else { //We can block most of his bullets with saber i guess
		if (distance > 1024) {
			if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectable(bs, WP_BLASTER))
				bestWeapon = WP_BLASTER;
			else if (bs->cur_ps.stats[STAT_WEAPONS] & WP_SABER)
				bestWeapon = WP_SABER;
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
		}
		else if (distance > 350 && distance < 900) { //Have some padding between distance tiers so we dont weaponswitch spam
			if (BotWeaponSelectableAltFire(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_REPEATER) && forcedFireMode != 2)
				bestWeapon = WP_REPEATER;
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_LG) && !(g_tweakWeapons.integer & WT_STUN_HEAL)) {
				bestWeapon = WP_STUN_BATON;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & WP_SABER)
				bestWeapon = WP_SABER;
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
		}
		else if (distance < 200) {
			if (BotWeaponSelectable(bs, WP_REPEATER) && forcedFireMode != 2)
				bestWeapon = WP_REPEATER;
			else if (BotWeaponSelectableAltFire(bs, WP_BLASTER)) {
				bestWeapon = WP_BLASTER;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectable(bs, WP_FLECHETTE))
				bestWeapon = WP_FLECHETTE;
			else if (BotWeaponSelectable(bs, WP_ROCKET_LAUNCHER) && forcedFireMode != 2)
				bestWeapon = WP_ROCKET_LAUNCHER;
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 2)
				bestWeapon = WP_CONCUSSION;
			else if (BotWeaponSelectable(bs, WP_DISRUPTOR))
				bestWeapon = WP_DISRUPTOR;
			else if (BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_LG) && !(g_tweakWeapons.integer & WT_STUN_HEAL)) {
				bestWeapon = WP_STUN_BATON;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_STUN_BATON) && (g_tweakWeapons.integer & WT_STUN_SHOCKLANCE)) {
				bestWeapon = WP_STUN_BATON;
				bs->doAltAttack = 1;
			}
			else if (bs->cur_ps.stats[STAT_WEAPONS] & WP_SABER)
				bestWeapon = WP_SABER;
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_OLD)) {
				bestWeapon = WP_BRYAR_OLD;
				bs->doAltAttack = 1;
				bs->altChargeTime = 800;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_BRYAR_PISTOL)) {
				bestWeapon = WP_BRYAR_PISTOL;
				bs->doAltAttack = 1;
				bs->altChargeTime = 1200;
			}
			else if (BotWeaponSelectable(bs, WP_CONCUSSION) && forcedFireMode != 1) {
				bestWeapon = WP_CONCUSSION;
				bs->doAltAttack = 1;
			}
			else if (BotWeaponSelectableAltFire(bs, WP_DEMP2) && forcedFireMode != 1) {
				bestWeapon = WP_DEMP2;
				bs->doAltAttack = 1;
				bs->altChargeTime = 2100;
			}
		}
	}

	if (bestWeapon == WP_THERMAL) {
		//bs->doAltAttack = 1;
		bs->altChargeTime = 1250;
		bs->ChargeTime = 1250;
	}
	else if (bestWeapon == WP_BOWCASTER) {
		bs->doAltAttack = 1;
	}

	if (bs->currentEnemy->client && bs->currentEnemy->client->ps.weapon == WP_DEMP2) //dont charge if they can cancel it
		bs->altChargeTime = 50;

	if (forcedFireMode == 1)
		bs->doAltAttack = 0;
	else if (forcedFireMode == 2)
		bs->doAltAttack = 1;

	//todo- weapon table.

	return bestWeapon;
}

int NewBotAI_GetAltCharge(bot_state_t *bs)
{
	int weap;

	weap = bs->cur_ps.weapon;

	if (bs->cur_ps.ammo[weaponData[weap].ammoIndex] < weaponData[weap].altEnergyPerShot && weap != WP_STUN_BATON)
		return 0;

	if ((bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT) && (level.time - bs->cur_ps.weaponChargeTime) > bs->altChargeTime)
		return 2;//release charge.. was 2 ?
	if (bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT)
		return 3;

	return 1;
}

int NewBotAI_GetCharge(bot_state_t* bs)
{
	int weap;

	weap = bs->cur_ps.weapon;

	if (bs->cur_ps.ammo[weaponData[weap].ammoIndex] < weaponData[weap].energyPerShot && weap != WP_STUN_BATON)
		return 0;

	if ((bs->cur_ps.weaponstate == WEAPON_CHARGING) && (level.time - bs->cur_ps.weaponChargeTime) > bs->ChargeTime)
		return 2;//release charge.. was 2 ?
	if (bs->cur_ps.weaponstate != WEAPON_CHARGING)
		return 3;

	return 1;
}


void NewBotAI_GetAttack(bot_state_t *bs)
{
	int weapon;
	const int totalHealthDelta = NewBotAI_GetTotalHealthDelta(bs);
	const qboolean hasDroppedOwnSaber = NewBotAI_HasDroppedOwnSaber(bs);
	//Only stop starting swings once clearly behind; duel winners kept countering from small
	//deficits, and higher skill tolerates a larger deficit before backing off.
	const qboolean hasHealthDisadvantage = NewBotAI_ShouldSuppressSaberAttackForDeficit(
		totalHealthDelta, (int)bs->settings.skill) ? qtrue : qfalse;
	const qboolean suppressSaberAttack = (hasHealthDisadvantage && !hasDroppedOwnSaber) ? qtrue : qfalse;
	// const float speed = NewBotAI_GetSpeedTowardsEnemy(bs);

	if (!bs->client || !bs->currentEnemy || !bs->currentEnemy->client)
		return;

	if (hasDroppedOwnSaber)
		weapon = WP_SABER;
	else if (g_tweakWeapons.integer & WT_TRIBES)
		weapon = NewBotAI_GetTribesWeapon(bs);
	else
		weapon = NewBotAI_GetWeapon(bs);
	BotSelectWeapon(bs->client, weapon);
	if (NewBotAI_IsEnemySaberThreatImminent(bs))
		return;

	if (bs->runningLikeASissy) //Dont attack when chasing them with strafe i guess
		return;
	if (bs->isCamper && (bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT) && (bs->cur_ps.weaponstate != WEAPON_CHARGING)) {//don't attack if waiting for them to land to groundpound - modify this so we don't cancel a charge
		return;
	}

	if (bs->currentEnemy->client->invulnerableTimer && (bs->currentEnemy->client->invulnerableTimer > level.time)) {//don't attack them if they can't take dmg
		return;
	}

	if (bs->cur_ps.weapon == WP_SABER) {//Fullforce saber attacks
		if (bs->saberTechniqueCandidate)
			return;
		if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE &&
			!BG_SaberInAttack(bs->cur_ps.saberMove) &&
			!NewBotAI_MayStartAirborneSwing(bs))
		{
			return;
		}

		if (!suppressSaberAttack && !bs->hitSpotted && NewBotAI_ShouldCounterSwing(bs))
		{
			trap->EA_Attack(bs->client);
			return;
		}

		const qboolean preferDrainlockFan = (NewBotAI_IsDrainlockAdvantage(bs) &&
			bs->cur_ps.fd.saberAnimLevel != SS_STAFF &&
			bs->cur_ps.fd.saberAnimLevel != SS_DUAL) ? qtrue : qfalse;
		if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE || preferDrainlockFan ||
			g_entities[bs->client].client->ps.fd.saberAnimLevel == SS_STAFF) { //Yellow/staff sweep
			if (preferDrainlockFan)
			{
				g_entities[bs->client].client->ps.fd.saberAnimLevel = SS_MEDIUM;
			}

			if (BG_SaberInAttack(bs->cur_ps.saberMove)) {
				if (g_entities[bs->client].client->ps.fd.saberAnimLevel == SS_MEDIUM)
					Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
			}
			else if (g_entities[bs->client].client->ps.fd.saberAnimLevel == SS_STAFF &&
				bs->fanPackage != FAN_PACKAGE_STAFF_PRESSURE)
				Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
				//g_entities[bs->client].client->ps.fd.saberAnimLevel = SS_MEDIUM; //SS_STAFF
				//Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
			//else
				//g_entities[bs->client].client->ps.fd.saberAnimLevel = SS_STAFF; //SS_STAFF
			//g_entities[bs->client].client->ps.fd.saberAnimLevel = SS_MEDIUM; //SS_STAFF

			if (bs->hitSpotted) //don't start a swing if a saberthrow is near us?
				return;

			if (g_gunGame.integer && g_entities[bs->client].client->forcedFireMode == 2) {
				trap->EA_Alt_Attack(bs->client);
				return;
			}

			//A committed fan chain now alternates between an exclusive strafe+attack
			//hold window and a free-movement dwell. Only HOLD presses attack; DWELL keeps
			//the chain reserved while allowing normal repositioning and dwell yaw.
			if (bs->fanPhase != FAN_PHASE_INACTIVE)
			{
				NewBotAI_ApplyHorizontalSwingMove(bs);
				if (!suppressSaberAttack && bs->fanPhase == FAN_PHASE_HOLD &&
					NewBotAI_FanHoldMayStartSwing(bs))
					trap->EA_Attack(bs->client);
				return;
			}

			//Mid-swing/transition with the enemy close and closing: keep the attack button
			//held so the engine's saber combo chains straight into the next swing the
			//moment weaponTime clears. The old LS_NONE/LS_READY-only gate below released
			//attack for the entire tail of every swing, so the press usually landed during
			//the recovery/return phase (no swing started) and the chain died after one hit.
			if (BG_SaberInAttack(bs->cur_ps.saberMove) &&
				(bs->fanPhase != FAN_PHASE_INACTIVE || bs->cur_ps.saberMove == LS_A_L2R || bs->cur_ps.saberMove == LS_A_R2L) &&
				bs->frame_Enemy_Len < 320 &&
				NewBotAI_GetTimeToInRange(bs, 75, 800) < 800 && g_entities[bs->client].health > 40 &&
				NewBotAI_SwingChainFootingAllows(bs))
			{
				if (!suppressSaberAttack)
					trap->EA_Attack(bs->client);
				return;
			}

			if ((g_entities[bs->client].client->ps.saberMove == LS_NONE || g_entities[bs->client].client->ps.saberMove == LS_READY) &&
				bs->fanPhase == FAN_PHASE_HOLD &&
				bs->frame_Enemy_Len < 256 &&
				((NewBotAI_GetTimeToInRange(bs, 75, 800) < 800) || bs->frame_Enemy_Len < 128)) {
				if (g_entities[bs->client].health > 40) {
					//See if they can't saberthrow?
					//Com_Printf("Their torso time is %i\n", bs->currentEnemy->client->ps.torsoTimer);
					//if ((bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN) || (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))) || ((bs->frame_Enemy_Len < 70) && (bs->currentEnemy->client->ps.origin[2] - bs->cur_ps.origin[2]) > 50)) {
						NewBotAI_ApplyHorizontalSwingMove(bs);
						if (!suppressSaberAttack)
							trap->EA_Attack(bs->client);
						return;
					//}
				}
			}

			//No fan chain running (e.g. bot_fanbias 0): still start a normal swing once the
			//enemy is inside reach so attacking never depends on the fan.
			if ((g_entities[bs->client].client->ps.saberMove == LS_NONE || g_entities[bs->client].client->ps.saberMove == LS_READY) &&
				bs->fanPhase == FAN_PHASE_INACTIVE &&
				NewBotAI_ShouldStartRedStanceSwing(NewBotAI_GetEnemyDistance2D(bs), NewBotAI_GetTimeToInRange(bs, 75, 300),
					g_entities[bs->client].health) &&
				NewBotAI_SwingStartFootingAllows(bs))
			{
				if (!suppressSaberAttack)
					trap->EA_Attack(bs->client);
				return;
			}

		}
#if 0
		if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE) { //Yellow sweep
			g_entities[bs->client].client->ps.fd.saberAnimLevel = SS_MEDIUM;
			if (bs->cur_ps.torsoTimer) {
				if (bs->cur_ps.torsoTimer < 200) {
					if (bs->cur_ps.saberMove == LS_A_L2R) {//Right
						trap->EA_MoveLeft(bs->client);
					}
					else if (bs->cur_ps.saberMove == LS_A_R2L) {//Left
						trap->EA_MoveRight(bs->client);
					}
					if (g_entities[bs->client].client->pers.cmd.forwardmove) {
						trap->EA_MoveBack(bs->client);
					}
					if (bs->cur_ps.movementDir == 6 || bs->cur_ps.movementDir == 2)
						trap->EA_Attack(bs->client);
				}
			}
			else {//Start it w/ a right swing i guess
				trap->EA_MoveLeft(bs->client);
				if (g_entities[bs->client].client->pers.cmd.forwardmove) {
					trap->EA_MoveBack(bs->client);
				}
				trap->EA_Attack(bs->client);
			}
			//proble we have to stop moving forward, or just hold S?
			//Get movement dir, if W, add S during start of swing
			//Get swing anim, if left, do right. if right, do left.
			//get swing anim point, if less than 50ms left, hold key and attack.  otherwise dont
		}
#endif
		else {
			g_entities[bs->client].client->ps.fd.saberAnimLevel = SS_STRONG;

			if (bs->hitSpotted) //don't start a swing if a saberthrow is near us?
				return;

			if (g_gunGame.integer && g_entities[bs->client].client->forcedFireMode == 2) {
				trap->EA_Alt_Attack(bs->client);
				return;
			}

			//Red stance can fan too: alternating horizontal L2R/R2L swings driven by the
			//same HOLD/DWELL chain as the yellow/staff path.
			if (bs->fanPhase != FAN_PHASE_INACTIVE)
			{
				NewBotAI_ApplyHorizontalSwingMove(bs);
				if (!suppressSaberAttack && bs->fanPhase == FAN_PHASE_HOLD &&
					NewBotAI_FanHoldMayStartSwing(bs))
					trap->EA_Attack(bs->client);
				return;
			}

			//Mid-swing/transition: hold attack so the red-style combo chains into the next
			//swing instead of releasing through every swing tail (same fix as the lightside
			//path above - the press has to still be down when weaponTime clears).
			if (BG_SaberInAttack(bs->cur_ps.saberMove) && NewBotAI_GetTimeToInRange(bs, 75, 600) < 600 &&
				g_entities[bs->client].health > 70 && NewBotAI_SwingChainFootingAllows(bs))
			{
				if (!suppressSaberAttack)
					trap->EA_Attack(bs->client);
				return;
			}

			//todo - skip if we are already during a swing
			if ((g_entities[bs->client].client->ps.saberMove == LS_NONE || g_entities[bs->client].client->ps.saberMove == LS_READY) && NewBotAI_GetTimeToInRange(bs, 75, 600) < 600) {
				if (g_entities[bs->client].health > 70) {
					if (((bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN) || (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))) ||
						((bs->cur_ps.fd.forcePower < 60) || ((bs->frame_Enemy_Len < 70) && (bs->currentEnemy->client->ps.origin[2] - bs->cur_ps.origin[2]) > 50))) &&
						NewBotAI_SwingStartFootingAllows(bs)) {
						if (!suppressSaberAttack)
							trap->EA_Attack(bs->client);
						return;
					}
				}
			}

			//Normal red-stance swing start once the enemy is within reach (or about to be);
			//previously red only swung in the drain/absorb/low-force special cases.
			if ((g_entities[bs->client].client->ps.saberMove == LS_NONE || g_entities[bs->client].client->ps.saberMove == LS_READY) &&
				NewBotAI_ShouldStartRedStanceSwing(NewBotAI_GetEnemyDistance2D(bs), NewBotAI_GetTimeToInRange(bs, 75, 300),
					g_entities[bs->client].health) &&
				NewBotAI_SwingStartFootingAllows(bs))
			{
				if (!suppressSaberAttack)
					trap->EA_Attack(bs->client);
				return;
			}

			/*
			if (((speed >= 0) && ((bs->frame_Enemy_Len / speed) < 1.2f)) || (bs->frame_Enemy_Len < 64)) {
				if (g_entities[bs->client].health > 60) {
					if ((bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN) || (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))) || bs->cur_ps.fd.forcePower < 40)
						trap->EA_Attack(bs->client);
				}
			}
			*/
		}

		return;
	}

	if (!bs->frame_Enemy_Vis && (bs->cur_ps.weapon != WP_DEMP2)) { //Dont waste ammo if we cant see them..?
		return;
	}

	if (!bs->isCamper && (bs->cur_ps.weapon == weapon)) {
		if (bs->doAltAttack) {
			const int altCharge = NewBotAI_GetAltCharge(bs);
			if (weapon == WP_STUN_BATON && bs->frame_Enemy_Len > 240 && (g_tweakWeapons.integer & WT_STUN_SHOCKLANCE)) { //Weird case for stun baton since low range and low firerate, dont bother until they are in range
			}
			else if (altCharge == 1) {
				trap->EA_Alt_Attack(bs->client);
			}
			else if (altCharge == 3) {
				if (level.framenum % 2)
					trap->EA_Alt_Attack(bs->client);
			}
		}
		else {
			const int charge = NewBotAI_GetCharge(bs);
			if (charge == 1) { //0 is no ammo, 1 charging, 2 is release charge, 3 is not charging
				trap->EA_Attack(bs->client);
			}
			else if (charge == 3) {
				if (level.framenum % 2)
					trap->EA_Attack(bs->client);
			}
		}
	}
}

void NewBotAI_GetGroundDodge(bot_state_t *bs) {
	bs->runningLikeASissy = 0;

	if (bs->forceMove_Right > 0)
		trap->EA_MoveRight(bs->client);
	else if(bs->forceMove_Right < 0)
		trap->EA_MoveLeft(bs->client);

	if (level.time % 1500 > 750) {
		if (Q_flrand(0.0f, 1.0f) > 0.5)
			bs->forceMove_Right = 1;
		else
			bs->forceMove_Right = -1;
	}
}

// Returns how fast the enemy is closing the distance towards us (their velocity projected onto
// the direction from them to us). Positive values mean they are approaching; used to detect a
// fast incoming enemy in saber duels when flipkick is unavailable (see item 2B).
static float NewBotAI_GetEnemyClosingSpeed(bot_state_t *bs)
{
	vec3_t toUs;

	if (!bs->currentEnemy || !bs->currentEnemy->client)
	{
		return 0.0f;
	}

	VectorSubtract(bs->eye, bs->currentEnemy->client->ps.origin, toUs);
	VectorNormalize(toUs);

	return DotProduct(bs->currentEnemy->client->ps.velocity, toUs);
}

static float NewBotAI_GetSelfFacingErrorToEnemy(bot_state_t *bs)
{
	vec3_t toEnemy;
	vec3_t enemyAngles;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return 180.0f;
	}

	VectorSubtract(bs->currentEnemy->r.currentOrigin, bs->cur_ps.origin, toEnemy);
	toEnemy[2] = 0.0f;
	if (toEnemy[0] == 0.0f && toEnemy[1] == 0.0f)
	{
		return 0.0f;
	}
	vectoangles(toEnemy, enemyAngles);

	return fabs(AngleSubtract(enemyAngles[YAW], bs->viewangles[YAW]));
}

static qboolean NewBotAI_IsEnemyCollapsePressure(bot_state_t *bs)
{
	float closingSpeed;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}
	closingSpeed = NewBotAI_GetEnemyClosingSpeed(bs);
	if (!bs->frame_Enemy_Vis || bs->frame_Enemy_Len < 80.0f || bs->frame_Enemy_Len > 320.0f)
	{
		return qfalse;
	}
	if (bs->currentEnemy->client->ps.weapon != WP_SABER)
	{
		return qfalse;
	}

	return (closingSpeed >= 220.0f) ? qtrue : qfalse;
}

static qboolean NewBotAI_HasStableSaberThrowDefenseAlignment(bot_state_t *bs)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}
	if (!bs->frame_Enemy_Vis || bs->frame_Enemy_Len < 72.0f || bs->frame_Enemy_Len > 224.0f)
	{
		return qfalse;
	}

	return (NewBotAI_GetSelfFacingErrorToEnemy(bs) <= 18.0f) ? qtrue : qfalse;
}

static qboolean NewBotAI_ShouldStabilizeAgainstEnemySaberThrow(bot_state_t *bs)
{
	qboolean enemySaberReturning;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client ||
		!bs->currentEnemy->client->ps.saberInFlight)
	{
		return qfalse;
	}
	enemySaberReturning = NewBotAI_IsEnemySaberReturning(bs);

	if (NewBotAI_IsEnemySaberThreatImminent(bs))
	{
		return qtrue;
	}
	if (!NewBotAI_HasStableSaberThrowDefenseAlignment(bs))
	{
		return qtrue;
	}
	if (!enemySaberReturning && !NewBotAI_HasFreePullkickWindow(bs))
	{
		return qtrue;
	}

	return qfalse;
}

static qboolean NewBotAI_ShouldPreDefenseAgainstCollapse(bot_state_t *bs)
{
	int ourHealth;
	int hisHealth;
	int ourForce;
	int hisForce;
	int ourTotalHealth;
	int totalHealthDelta;
	qboolean recentlyHurt;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}
	ourHealth = g_entities[bs->client].health;
	hisHealth = bs->currentEnemy->health;
	ourForce = bs->cur_ps.fd.forcePower;
	hisForce = bs->currentEnemy->client->ps.fd.forcePower;
	ourTotalHealth = ourHealth + bs->cur_ps.stats[STAT_ARMOR];
	totalHealthDelta = NewBotAI_GetTotalHealthDelta(bs);
	recentlyHurt = (bs->lastHurtTime > level.time - 900) ? qtrue : qfalse;

	if (!NewBotAI_IsEnemyCollapsePressure(bs))
	{
		return qfalse;
	}
	if (recentlyHurt && ourTotalHealth <= 35 && totalHealthDelta <= -20)
	{
		return qtrue;
	}
	if (ourForce > hisForce + 10 && ourHealth > hisHealth + 10)
	{
		return qfalse;
	}
	if (recentlyHurt && ourTotalHealth <= 45 &&
		bs->frame_Enemy_Vis && bs->frame_Enemy_Len <= 256.0f)
	{
		return qtrue;
	}

	return qtrue;
}

static qboolean NewBotAI_IsStablePTKCommitWindow(bot_state_t *bs)
{
	float closingSpeed;

	if (!bs)
	{
		return qfalse;
	}
	closingSpeed = NewBotAI_GetEnemyClosingSpeed(bs);
	if (!bs->frame_Enemy_Vis || bs->frame_Enemy_Len < 96.0f || bs->frame_Enemy_Len > 384.0f)
	{
		return qfalse;
	}
	if (bs->currentEnemy && bs->currentEnemy->client &&
		bs->currentEnemy->client->ps.saberInFlight &&
		NewBotAI_ShouldStabilizeAgainstEnemySaberThrow(bs))
	{
		return qfalse;
	}
	if (NewBotAI_GetSelfFacingErrorToEnemy(bs) > 26.0f)
	{
		return qfalse;
	}
	/* Avoid forcing PTK in chaotic collapse windows or when the target is disengaging hard. */
	if (closingSpeed > 260.0f || closingSpeed < -120.0f)
	{
		return qfalse;
	}

	return qtrue;
}

static qboolean NewBotAI_HasTimedFanEntryWindow(bot_state_t *bs)
{
	qboolean enemyDisabled;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}
	//Human horizontal swings landed almost entirely inside 48-110u; entering the fan from
	//further out just strafed in place without connecting.
	if (!bs->frame_Enemy_Vis || !NewBotAI_IsFanEntrySpacing(bs->frame_Enemy_Len))
	{
		return qfalse;
	}
	if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE || bs->currentEnemy->client->ps.saberInFlight)
	{
		return qfalse;
	}
	if (NewBotAI_IsEnemySaberThreatImminent(bs) || NewBotAI_ShouldPreDefenseAgainstCollapse(bs))
	{
		return qfalse;
	}
	enemyDisabled = BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim) ? qtrue : qfalse;
	if (NewBotAI_ShouldPressAdvantage(bs) || NewBotAI_HasClearAdvantage(bs) || enemyDisabled)
	{
		return qtrue;
	}
	if (BG_SaberInAttack(bs->cur_ps.saberMove) ||
		PM_SaberInStart(bs->cur_ps.saberMove) ||
		PM_SaberInTransition(bs->cur_ps.saberMove))
	{
		return qtrue;
	}

	//Neutral spacing is a valid entry too: winners opened exchanges with horizontal swings
	//without needing a prior advantage.
	return qtrue;
}

enum
{
	NEWBOTAI_PULL_TIMING_NONE,
	NEWBOTAI_PULL_TIMING_IMMEDIATE,
	NEWBOTAI_PULL_TIMING_STUN_ONLY,
	NEWBOTAI_PULL_TIMING_PULL_CLOSE
};

static int NewBotAI_GetPullkickTimingMode(bot_state_t *bs, qboolean pullActingOnBot)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return NEWBOTAI_PULL_TIMING_NONE;
	}

	if (bs->frame_Enemy_Len <= NEWBOTAI_IMMEDIATE_FLIPKICK_RANGE)
	{
		return NEWBOTAI_PULL_TIMING_IMMEDIATE;
	}

	if (bs->frame_Enemy_Len <= NEWBOTAI_PULL_STUN_ONLY_RANGE)
	{
		return NEWBOTAI_PULL_TIMING_STUN_ONLY;
	}

	if (pullActingOnBot || NewBotAI_IsEnemyPullable(bs))
	{
		return NEWBOTAI_PULL_TIMING_PULL_CLOSE;
	}

	return NEWBOTAI_PULL_TIMING_STUN_ONLY;
}

static float NewBotAI_GetProjectedPullkickClosingSpeed(bot_state_t *bs, int timingMode)
{
	float closing;
	float ourForwardSpeed;
	vec3_t toThem;

	if (!bs->currentEnemy || !bs->currentEnemy->client)
	{
		return 0.0f;
	}

	closing = NewBotAI_GetEnemyClosingSpeed(bs);

	//Our own forward run also closes the gap - project our velocity onto the
	//direction toward the enemy (only count actual approach, not backing off).
	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->cur_ps.origin, toThem);
	toThem[2] = 0.0f;
	if (VectorNormalize(toThem) > 0.0f)
	{
		ourForwardSpeed = DotProduct(bs->cur_ps.velocity, toThem);
		if (ourForwardSpeed > 0.0f)
		{
			closing += ourForwardSpeed;
		}
	}

	if (timingMode == NEWBOTAI_PULL_TIMING_PULL_CLOSE)
	{
		closing += NewBotAI_GetPullkickAssumedPullSpeed();
	}

	return closing;
}

// Predicts (in ms) when the enemy's horizontal distance to us will fall inside the
// flipkick's forward trace range (~135 units of clearance for the 32-unit box trace)
// after accounting for either a close-range pull stun or an assumed pull-driven speed-up.
static float NewBotAI_GetPullkickTimeToKickRange(bot_state_t *bs, qboolean pullActingOnBot, int *timingModeOut)
{
	const int timingMode = NewBotAI_GetPullkickTimingMode(bs, pullActingOnBot);
	const float closing = NewBotAI_GetProjectedPullkickClosingSpeed(bs, timingMode);

	if (timingModeOut)
	{
		*timingModeOut = timingMode;
	}

	if (timingMode == NEWBOTAI_PULL_TIMING_NONE)
	{
		return -1.0f;
	}
	if (timingMode == NEWBOTAI_PULL_TIMING_IMMEDIATE)
	{
		return 0.0f;
	}
	if (closing <= 0.0f)
	{
		return -1.0f;
	}

	return ((bs->frame_Enemy_Len - NEWBOTAI_IMMEDIATE_FLIPKICK_RANGE) / closing) * 1000.0f;
}

#define NEWBOTAI_FAN_FLIPKICK_INIT_DELAY_MS 100
#define NEWBOTAI_FAN_PRESSURE_TIMEOUT_MS 3000

static qboolean NewBotAI_CanInitiateFlipkickUnderFanPressure(bot_state_t *bs)
{
	if (bs->fanPhase == FAN_PHASE_INACTIVE || bs->fanPhase == FAN_PHASE_DWELL)
	{
		return qtrue;
	}

	if (!bs->fanAttackDir || bs->fanChainStartTime <= 0 ||
		level.time - bs->fanChainStartTime > NEWBOTAI_FAN_PRESSURE_TIMEOUT_MS)
	{
		return qfalse;
	}

	if (level.time < bs->fanChainStartTime + NEWBOTAI_FAN_FLIPKICK_INIT_DELAY_MS)
	{
		return qfalse;
	}

	return qtrue;
}

// Schedules the pk/ptk flipkick jump only when the distance/speed projection says the
// pull should actually drag the enemy into kick range. Immediate kick-range cases are
// handled by the direct pull+flipkick path; close pull-stun cases use only natural
// closing speed; farther targets add an assumed pull-close speed before the final delay.
// Reuse the same post-attempt cooldown NewBotAI_Flipkick uses (lastFlipkickAttemptTime)
// so once a scheduled jump fires it does not instantly re-arm on the next think.
#define NEWBOTAI_PULLKICK_RANGED_EXTRA_DELAY_MS 120
static void NewBotAI_SchedulePullkickJump(bot_state_t *bs)
{
	float timeToRange;
	int timingMode;
	int extraDelay = 0;

	if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE)
	{
		bs->pullKickJumpTime = 0;
		return;
	}

	if (bs->lastFlipkickAttemptTime > level.time)
	{
		//Still cooling down from the last kick attempt/jump - don't re-arm a new one yet.
		bs->pullKickJumpTime = 0;
		return;
	}

	if (!NewBotAI_CanInitiateFlipkickUnderFanPressure(bs))
	{
		//Don't start a new pullkick jump during the fan chain's initial delay window;
		//keep any existing pending schedule intact.
		return;
	}

	if (!NewBotAI_CanAttemptFlipkick(bs))
	{
		bs->pullKickJumpTime = 0;
		NewBotAI_PushHopRetryCooldown(bs);
		return;
	}

	timeToRange = NewBotAI_GetPullkickTimeToKickRange(bs, qfalse, &timingMode);
	if (timingMode == NEWBOTAI_PULL_TIMING_IMMEDIATE)
	{
		bs->pullKickJumpTime = 0;
		return;
	}

	if (timeToRange >= 0.0f)
	{
		if (timingMode == NEWBOTAI_PULL_TIMING_PULL_CLOSE)
		{
			extraDelay = (int)((bs->frame_Enemy_Len - NEWBOTAI_IMMEDIATE_FLIPKICK_RANGE) * 0.15f);
			if (extraDelay > NEWBOTAI_PULLKICK_RANGED_EXTRA_DELAY_MS)
			{
				extraDelay = NEWBOTAI_PULLKICK_RANGED_EXTRA_DELAY_MS;
			}
		}
		//Skill 7+ kicks on the predicted arrival like jundon (no extra gap); lower skills
		//react 100-250ms late plus a mistake-bias delay.
		bs->pullKickJumpTime = level.time + (int)timeToRange +
			NewBotAI_GetPullkickExtraDelayMs() + extraDelay +
			NewBotAI_GetComboGapMs(NEWBOTAI_COMBO_PULL_KICK, bs->settings.skill, 0,
				NewBotAI_GetDecisionMistakeChance(bs));
	}
	else
	{
		bs->pullKickJumpTime = 0;
	}
}

static qboolean NewBotAI_IsPullkickOpportunity(bot_state_t *bs)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim) ||
		BG_InRoll3(bs->currentEnemy->client->ps.legsAnim) ||
		bs->currentEnemy->client->ps.saberInFlight)
	{
		return qtrue;
	}

	if (bs->currentEnemy->client->ps.groundEntityNum == ENTITYNUM_NONE ||
		bs->currentEnemy->client->ps.fd.forcePower < 20)
	{
		return qtrue;
	}

	return (bs->frame_Enemy_Len <= NEWBOTAI_PULL_STUN_ONLY_RANGE) ? qtrue : qfalse;
}

static qboolean NewBotAI_ShouldSkipPullForNaturalFlipkickPTK(bot_state_t *bs)
{
	float timeToRange;
	const qboolean pullUsable = (bs &&
		!(g_forcePowerDisable.integer & (1 << FP_PULL)) &&
		(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PULL)) &&
		bs->cur_ps.fd.forcePower >= 20 &&
		bs->cur_ps.groundEntityNum != ENTITYNUM_NONE &&
		bs->frame_Enemy_Vis &&
		bs->currentEnemy && bs->currentEnemy->client &&
		!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))) ? qtrue : qfalse;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client || !g_flipKick.integer)
	{
		return qfalse;
	}

	if (!NewBotAI_IsPullkickOpportunity(bs) || !NewBotAI_CanAttemptFlipkick(bs))
	{
		return qfalse;
	}

	timeToRange = NewBotAI_GetPullkickTimeToKickRange(bs, qfalse, NULL);
	return NewBotAI_ShouldSkipPullForNaturalFlipkick(
		pullUsable ? 1 : 0,
		1,
		1,
		NewBotAI_IsFlipkickSetupReady(bs) ? 1 : 0,
		bs->frame_Enemy_Len,
		timeToRange) ? qtrue : qfalse;
}

// Saber-duel deadlock fix: when flipkick isn't available (g_flipkick disabled, or the duel type
// disallows it), give the bot a real goal instead of standing indecisively -- lean on fan-chain
// attacks and pick evenly between red/staff and yellow swing chains (no bias toward red).
static void NewBotAI_SaberDuelIndecisionFallback(bot_state_t *bs, qboolean horizontalSwingStart)
{
	const float fanBias = BotGetChanceBiasPercent(bot_fanbias.value);

	//Fan bias now prefers recorded human-like yellow/staff pressure packages over generic
	//red/yellow coinflips. Staff keeps its package active, while single-blade fan pressure
	//leans into yellow for more natural horizontal pressure strings.
	if (fanBias > 0.0f && Q_irand(1, 100) <= (int)fanBias)
	{
		if (g_entities[bs->client].client->ps.fd.saberAnimLevel == SS_STAFF)
		{
			bs->fanPackage = FAN_PACKAGE_STAFF_PRESSURE;
		}
		else
		{
			if (g_entities[bs->client].client->ps.fd.saberAnimLevel != SS_MEDIUM)
			{
				g_entities[bs->client].client->ps.fd.saberAnimLevel = SS_MEDIUM;
			}
			bs->fanPackage = FAN_PACKAGE_YELLOW_PRESSURE;
		}
	}

	if (horizontalSwingStart)
	{
		NewBotAI_ApplyHorizontalSwingMove(bs);
		return;
	}

}

static qboolean NewBotAI_ShouldHoldForPTKForce(bot_state_t *bs)
{
	const int ourForce = (bs) ? bs->cur_ps.fd.forcePower : 0;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
		return qfalse;
	if (ourForce <= 25 || ourForce >= NEWBOTAI_PTK_FORCE_BUDGET)
		return qfalse;
	if (!g_flipKick.integer || !NewBotAI_IsEnemyPullable(bs) || !NewBotAI_IsPullkickOpportunity(bs))
		return qfalse;
	if (!bs->frame_Enemy_Vis || bs->frame_Enemy_Len < 50 || bs->frame_Enemy_Len > 640)
		return qfalse;
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return qfalse;

	return qtrue;
}

// bot_conservation: 0-100 bias (see g_xcvar.h) controlling how often a bot disengages
// to let its force points regenerate instead of pressing the attack. Weighted heavily
// by our force disadvantage relative to the current enemy - a bigger deficit makes the
// bot much likelier to take a moment and back off at a given bias value. Guarded so we
// never idle into an unsafe moment (enemy mid-swing/throw, or already close and hitting us).
static qboolean NewBotAI_ShouldConserveForce(bot_state_t *bs)
{
	const float conservationBias = BotGetChanceBiasPercent(bot_conservation.value);
	const int ourForce = bs->cur_ps.fd.forcePower;
	const int hisForce = (bs->currentEnemy && bs->currentEnemy->client) ? bs->currentEnemy->client->ps.fd.forcePower : 0;
	const int forceDeficit = hisForce - ourForce;
	float chance;

	if (conservationBias <= 0.0f)
		return qfalse;
	if (!bs->currentEnemy || !bs->currentEnemy->client)
		return qfalse;
	if (ourForce >= 90) //already nearly full, nothing meaningful to regen
		return qfalse;
	if (NewBotAI_ShouldPressAdvantage(bs))
		return qfalse;
	//Item 5: never disengage to conserve while actively holding a drainlock advantage -
	//pressing the chase/drain/pullkick loop takes priority over letting force regen.
	if (NewBotAI_IsDrainlockAdvantage(bs))
		return qfalse;
	//Never idle into an unsafe moment - only conserve when disengaging is actually safe.
	if (bs->hitSpotted || bs->currentEnemy->client->ps.saberInFlight ||
		BG_SaberInAttack(bs->currentEnemy->client->ps.saberMove) ||
		bs->frame_Enemy_Len < 150)
		return qfalse;

	//Item 1: if the bot wants to force drain but simply cannot reach its opponent with
	//it, stop pressing and take the conservation window to regain force instead.
	if (!(g_forcePowerDisable.integer & (1 << FP_DRAIN)) &&
		(bs->cur_ps.fd.forcePowersKnown & (1 << FP_DRAIN)) &&
		ourForce < 90 &&
		(bs->frame_Enemy_Len > MAX_DRAIN_DISTANCE || !bs->frame_Enemy_Vis))
	{
		return qtrue;
	}

	//When PTK would be the preferred path but we are just short of the budgeted FP cost,
	//take a short conservation window to regen first instead of burning force early.
	if (NewBotAI_ShouldHoldForPTKForce(bs))
	{
		return qtrue;
	}

	//Small baseline chance from the bias alone, heavily amplified by how far behind we
	//are on force points.
	chance = conservationBias * 0.1f;
	if (forceDeficit > 0)
		chance += (float)forceDeficit * (conservationBias / 100.0f) * 0.6f;

	if (chance <= 0.0f)
		return qfalse;

	return (Q_flrand(0.0f, 100.0f) < chance) ? qtrue : qfalse;
}

// Track when the enemy's current saber swing (start or attack move) began.
static void NewBotAI_TrackEnemySwing(bot_state_t *bs)
{
	const playerState_t *enemyPs;
	qboolean swinging;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		bs->enemySwingActive = qfalse;
		return;
	}
	enemyPs = &bs->currentEnemy->client->ps;
	swinging = (enemyPs->weapon == WP_SABER && !enemyPs->saberInFlight &&
		(PM_SaberInStart(enemyPs->saberMove) || BG_SaberInAttack(enemyPs->saberMove))) ? qtrue : qfalse;
	if (swinging && !bs->enemySwingActive)
		bs->enemySwingStartTime = level.time;
	bs->enemySwingActive = swinging;
}

// Counter-swing ~100ms into an enemy swing when inside 90u (jundon's counter timing).
static qboolean NewBotAI_ShouldCounterSwing(bot_state_t *bs)
{
	NewBotAI_TrackEnemySwing(bs);
	if (!bs->enemySwingActive || bs->cur_ps.groundEntityNum == ENTITYNUM_NONE ||
		bs->cur_ps.weaponTime > 0 || bs->cur_ps.saberBlocked != BLOCKED_NONE ||
		bs->cur_ps.saberInFlight ||
		(bs->cur_ps.saberMove != LS_NONE && bs->cur_ps.saberMove != LS_READY))
		return qfalse;
	if (!NewBotAI_CounterSwingReady(level.time - bs->enemySwingStartTime,
		NewBotAI_GetEnemyDistance2D(bs), bs->counterSwingFor == bs->enemySwingStartTime))
		return qfalse;
	bs->counterSwingFor = bs->enemySwingStartTime;
	return qtrue;
}

// Enemy swing starting within ~130u: humans advanced into ~65% of them and dodged back
// otherwise (fast lateral-back or a jump; plain backpedal stays a low-skill mistake roll).
// Rolled once per enemy swing. Returns qtrue when the reaction drove movement this frame.
static qboolean NewBotAI_UpdateSwingDodge(bot_state_t *bs)
{
	float dist2D;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client || bs->cur_ps.weapon != WP_SABER)
		return qfalse;
	NewBotAI_TrackEnemySwing(bs);
	if (!bs->enemySwingActive)
	{
		bs->swingDodgeEnemyMove = 0;
		return qfalse;
	}
	//Our own swing or a running fan hold owns movement.
	if (BG_SaberInAttack(bs->cur_ps.saberMove) || PM_SaberInStart(bs->cur_ps.saberMove) ||
		bs->fanPhase == FAN_PHASE_HOLD || NewBotAI_HasExclusiveFlipkickMovement(bs))
		return qfalse;

	dist2D = NewBotAI_GetEnemyDistance2D(bs);
	if (bs->swingDodgeRolledFor != bs->enemySwingStartTime)
	{
		const int mistakeChance = (bs->settings.skill < 7.0f) ? NewBotAI_GetDecisionMistakeChance(bs) / 2 : 0;

		bs->swingDodgeRolledFor = bs->enemySwingStartTime;
		bs->swingDodgeEnemyMove = bs->currentEnemy->client->ps.saberMove;
		bs->swingDodgeStyle = NewBotAI_GetSwingDodgeChoice(dist2D,
			bs->cur_ps.groundEntityNum != ENTITYNUM_NONE ? 1 : 0,
			(bs->cur_ps.fd.forcePowersKnown & (1 << FP_LEVITATION)) ? 1 : 0,
			mistakeChance, Q_irand(1, 100), Q_irand(1, 100));
		if (bs->swingDodgeStyle != NEWBOTAI_SWING_DODGE_NONE &&
			bs->swingDodgeStyle != NEWBOTAI_SWING_DODGE_BACKPEDAL &&
			NewBotAI_EnemySwingAdvances(Q_irand(1, 100)))
			bs->swingDodgeStyle = NEWBOTAI_SWING_DODGE_ADVANCE;
		bs->swingDodgeDir = Q_irand(0, 1) ? 1 : -1;
		if (bs->swingDodgeStyle == NEWBOTAI_SWING_DODGE_JUMP)
			trap->EA_Jump(bs->client);
	}
	if (bs->swingDodgeStyle == NEWBOTAI_SWING_DODGE_NONE)
		return qfalse;

	if (bs->swingDodgeStyle == NEWBOTAI_SWING_DODGE_ADVANCE)
	{
		trap->EA_MoveForward(bs->client);
		return qtrue;
	}
	if (bs->swingDodgeStyle == NEWBOTAI_SWING_DODGE_BACKPEDAL)
	{
		trap->EA_MoveBack(bs->client);
		return qtrue;
	}
	if (bs->swingDodgeDir > 0)
		trap->EA_MoveRight(bs->client);
	else
		trap->EA_MoveLeft(bs->client);
	//Dodge back out of reach as well as aside while the swing can still connect.
	if (bs->swingDodgeStyle == NEWBOTAI_SWING_DODGE_LATERAL && dist2D < NEWBOTAI_SWING_DODGE_RANGE)
		trap->EA_MoveBack(bs->client);
	return qtrue;
}

//Retreat wall escapes: a retreating bot backpedals away from its enemy, so the normal
//forward wall probe never sees the wall behind it. NewBotAI_RetreatWallImminent probes
//along the actual movement direction a little ahead of contact; when a wall is coming
//up the bot commits to one of several escapes, chosen at random and weighted by bot
//learning (the decision is recorded so the outcomes can later be turned into fixed logic).
enum
{
	NEWBOTAI_WALLESC_NONE = 0,
	NEWBOTAI_WALLESC_WALLRUN,
	NEWBOTAI_WALLESC_FLIPKICK_DRAIN,
	NEWBOTAI_WALLESC_DRAIN_FLIPKICK,
	NEWBOTAI_WALLESC_ROLL_AROUND,
	NEWBOTAI_WALLESC_HOP_OVER,
	NEWBOTAI_WALLESC_COUNT
};

#define NEWBOTAI_WALL_ESCAPE_COMMIT_MS 900
#define NEWBOTAI_WALL_ESCAPE_RETRY_MS 1200
#define NEWBOTAI_WALL_ESCAPE_BASE_WEIGHT 50
#define NEWBOTAI_WALL_ESCAPE_MIN_WEIGHT 5
#define NEWBOTAI_WALL_ESCAPE_ENGAGE_RANGE 220.0f

static const char *NewBotAI_WallEscapeName(int option)
{
	switch (option)
	{
	case NEWBOTAI_WALLESC_WALLRUN: return "vertical wallrun";
	case NEWBOTAI_WALLESC_FLIPKICK_DRAIN: return "flipkick -> drain";
	case NEWBOTAI_WALLESC_DRAIN_FLIPKICK: return "drain -> flipkick";
	case NEWBOTAI_WALLESC_ROLL_AROUND: return "roll around";
	case NEWBOTAI_WALLESC_HOP_OVER: return "hop around/over";
	default: return "none";
	}
}

static void NewBotAI_WallEscapeTokens(int option, int *response, int *follow)
{
	*follow = BOTLEARN_TOK_NONE;
	switch (option)
	{
	case NEWBOTAI_WALLESC_WALLRUN: *response = BOTLEARN_TOK_WALLRUN; break;
	case NEWBOTAI_WALLESC_FLIPKICK_DRAIN: *response = BOTLEARN_TOK_KICK; *follow = BOTLEARN_TOK_DRAIN; break;
	case NEWBOTAI_WALLESC_DRAIN_FLIPKICK: *response = BOTLEARN_TOK_DRAIN; *follow = BOTLEARN_TOK_KICK; break;
	case NEWBOTAI_WALLESC_ROLL_AROUND: *response = BOTLEARN_TOK_ROLL; break;
	case NEWBOTAI_WALLESC_HOP_OVER: *response = BOTLEARN_TOK_HOP; break;
	default: *response = BOTLEARN_TOK_NONE; break;
	}
}

static qboolean NewBotAI_RetreatWallImminent(bot_state_t *bs, vec3_t wallNormal)
{
	static vec3_t probeMins = {-15.0f, -15.0f, -8.0f};
	static vec3_t probeMaxs = {15.0f, 15.0f, 8.0f};
	vec3_t moveDir, start, end;
	trace_t tr;
	float speed;
	float probeDistance;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
		return qfalse;
	VectorSet(moveDir, bs->cur_ps.velocity[0], bs->cur_ps.velocity[1], 0.0f);
	speed = VectorNormalize(moveDir);
	if (speed < 80.0f)
	{
		//Barely moving: assume we are trying to back away from the enemy.
		VectorSubtract(bs->cur_ps.origin, bs->currentEnemy->client->ps.origin, moveDir);
		moveDir[2] = 0.0f;
		if (VectorNormalize(moveDir) <= 0.0f)
			return qfalse;
	}
	probeDistance = NEWBOTAI_WALL_CONTACT_PROBE_DISTANCE + speed * 0.3f;
	if (probeDistance < 32.0f)
		probeDistance = 32.0f;
	else if (probeDistance > 112.0f)
		probeDistance = 112.0f;
	VectorCopy(bs->cur_ps.origin, start);
	start[2] += 24.0f;
	VectorMA(start, probeDistance, moveDir, end);
	JP_Trace(&tr, start, probeMins, probeMaxs, end, bs->client,
		MASK_PLAYERSOLID & ~CONTENTS_BODY, qfalse, 0, 0);
	if (tr.startsolid || tr.allsolid || tr.fraction >= 1.0f)
		return qfalse;
	if (tr.entityNum < MAX_CLIENTS || tr.plane.normal[2] > 0.35f ||
		DotProduct(moveDir, tr.plane.normal) > -0.25f)
		return qfalse;
	if (wallNormal)
		VectorCopy(tr.plane.normal, wallNormal);
	return qtrue;
}

static qboolean NewBotAI_WallEscapeCanDrain(bot_state_t *bs)
{
	return (bot_forcepowers.integer && !g_forcePowerDisable.integer &&
		(bs->cur_ps.fd.forcePowersKnown & (1 << FP_DRAIN)) &&
		bs->cur_ps.fd.forcePower >= 10 &&
		bs->frame_Enemy_Len < MAX_DRAIN_DISTANCE - 100) ? qtrue : qfalse;
}

//True when a player-sized box can move sideways (side -1 left / 1 right, relative to
//facing the enemy) far enough to get around them.
static qboolean NewBotAI_WallEscapeSideOpen(bot_state_t *bs, int side, float height)
{
	static vec3_t mins = {-15.0f, -15.0f, 0.0f};
	static vec3_t maxs = {15.0f, 15.0f, 32.0f};
	vec3_t toEnemy, right, start, end;
	trace_t tr;

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->cur_ps.origin, toEnemy);
	toEnemy[2] = 0.0f;
	if (VectorNormalize(toEnemy) <= 0.0f)
		return qfalse;
	VectorSet(right, toEnemy[1], -toEnemy[0], 0.0f);
	VectorCopy(bs->cur_ps.origin, start);
	start[2] += height;
	VectorMA(start, 96.0f * (float)side, right, end);
	VectorMA(end, 48.0f, toEnemy, end);
	JP_Trace(&tr, start, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
	return (!tr.startsolid && !tr.allsolid && tr.fraction >= 0.9f) ? qtrue : qfalse;
}

static qboolean NewBotAI_WallEscapeHeadroomOverEnemy(bot_state_t *bs)
{
	vec3_t start, end;
	trace_t tr;

	VectorCopy(bs->currentEnemy->client->ps.origin, start);
	start[2] += DEFAULT_MAXS_2;
	VectorCopy(start, end);
	end[2] += 72.0f;
	JP_Trace(&tr, start, NULL, NULL, end, bs->client, MASK_SOLID, qfalse, 0, 0);
	return (!tr.startsolid && tr.fraction >= 1.0f) ? qtrue : qfalse;
}

static qboolean NewBotAI_WallEscapeOptionValid(bot_state_t *bs, int option, int *side)
{
	const qboolean grounded = (bs->cur_ps.groundEntityNum != ENTITYNUM_NONE) ? qtrue : qfalse;
	const qboolean flipkickReady = (NewBotAI_CanAttemptFlipkick(bs) && !bs->cur_ps.saberInFlight &&
		bs->frame_Enemy_Len < NEWBOTAI_WALL_ESCAPE_ENGAGE_RANGE) ? qtrue : qfalse;
	const int preferredSide = (bs->wallEscapeSide < 0) ? -1 : 1;

	switch (option)
	{
	case NEWBOTAI_WALLESC_WALLRUN:
		return (grounded &&
			bs->cur_ps.fd.forcePowerLevel[FP_LEVITATION] >= FORCE_LEVEL_1 &&
			!(g_forcePowerDisable.integer & (1 << FP_LEVITATION))) ? qtrue : qfalse;
	case NEWBOTAI_WALLESC_FLIPKICK_DRAIN:
		return (grounded && flipkickReady) ? qtrue : qfalse;
	case NEWBOTAI_WALLESC_DRAIN_FLIPKICK:
		return (flipkickReady && NewBotAI_WallEscapeCanDrain(bs)) ? qtrue : qfalse;
	case NEWBOTAI_WALLESC_ROLL_AROUND:
	case NEWBOTAI_WALLESC_HOP_OVER:
	{
		const float height = (option == NEWBOTAI_WALLESC_HOP_OVER) ? 40.0f : 0.0f;
		if (!grounded || bs->frame_Enemy_Len > NEWBOTAI_WALL_ESCAPE_ENGAGE_RANGE)
			return qfalse;
		if (NewBotAI_WallEscapeSideOpen(bs, preferredSide, height))
		{
			*side = preferredSide;
			return qtrue;
		}
		if (NewBotAI_WallEscapeSideOpen(bs, -preferredSide, height))
		{
			*side = -preferredSide;
			return qtrue;
		}
		if (option == NEWBOTAI_WALLESC_HOP_OVER && NewBotAI_WallEscapeHeadroomOverEnemy(bs))
		{
			*side = 0;
			return qtrue;
		}
		return qfalse;
	}
	default:
		return qfalse;
	}
}

static int NewBotAI_ChooseWallEscape(bot_state_t *bs, const vec3_t wallNormal)
{
	int weights[NEWBOTAI_WALLESC_COUNT];
	int sides[NEWBOTAI_WALLESC_COUNT];
	int bonuses[NEWBOTAI_WALLESC_COUNT];
	const int stimulus = NewBotAI_GetEnemyStimulusToken(bs);
	int total = 0;
	int option;
	int roll;

	bs->wallEscapeSide = Q_irand(0, 1) ? 1 : -1;
	for (option = 0; option < NEWBOTAI_WALLESC_COUNT; option++)
	{
		int response, follow;

		weights[option] = 0;
		sides[option] = bs->wallEscapeSide;
		bonuses[option] = 0;
		if (option == NEWBOTAI_WALLESC_NONE ||
			!NewBotAI_WallEscapeOptionValid(bs, option, &sides[option]))
			continue;
		NewBotAI_WallEscapeTokens(option, &response, &follow);
		bonuses[option] = G_BotLearnBonus(&g_entities[bs->client], bs->currentEnemy,
			stimulus, response, follow, bs->settings.skill);
		weights[option] = NEWBOTAI_WALL_ESCAPE_BASE_WEIGHT + bonuses[option];
		if (weights[option] < NEWBOTAI_WALL_ESCAPE_MIN_WEIGHT)
			weights[option] = NEWBOTAI_WALL_ESCAPE_MIN_WEIGHT;
		total += weights[option];
	}
	if (total <= 0)
		return NEWBOTAI_WALLESC_NONE;

	roll = Q_irand(0, total - 1);
	for (option = 1; option < NEWBOTAI_WALLESC_COUNT; option++)
	{
		if (roll < weights[option])
			break;
		roll -= weights[option];
	}
	if (option >= NEWBOTAI_WALLESC_COUNT)
		return NEWBOTAI_WALLESC_NONE;

	{
		int response, follow;
		NewBotAI_WallEscapeTokens(option, &response, &follow);
		G_BotLearnDecision(&g_entities[bs->client], bs->currentEnemy, stimulus, response,
			follow, bonuses[option]);
	}
	bs->wallEscapeSide = sides[option];
	bs->wallEscapeYaw = vectoyaw(wallNormal) + 180.0f;
	if (bot_learning_debug.integer)
	{
		Com_Printf("^3[wall escape]^7 %s: %s (weight %d of %d, learned %+d)\n",
			g_entities[bs->client].client->pers.netname, NewBotAI_WallEscapeName(option),
			weights[option], total, bonuses[option]);
	}
	return option;
}

static void NewBotAI_WallEscapeFaceEnemy(bot_state_t *bs)
{
	vec3_t toEnemy, angles;

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->cur_ps.origin, toEnemy);
	vectoangles(toEnemy, angles);
	bs->ideal_viewangles[YAW] = angles[YAW];
}

static void NewBotAI_WallEscapeDrain(bot_state_t *bs)
{
	if (NewBotAI_WallEscapeCanDrain(bs))
	{
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
		trap->EA_ForcePower(bs->client);
	}
}

static void NewBotAI_WallEscapeSideMove(bot_state_t *bs)
{
	if (bs->wallEscapeSide < 0)
		trap->EA_MoveLeft(bs->client);
	else if (bs->wallEscapeSide > 0)
		trap->EA_MoveRight(bs->client);
}

//Runs (and if needed picks) a retreat wall escape. Returns qtrue while an escape owns
//this think's movement.
static qboolean NewBotAI_RunRetreatWallEscape(bot_state_t *bs)
{
	vec3_t wallNormal;
	int age;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		if (bs)
			bs->wallEscapeOption = NEWBOTAI_WALLESC_NONE;
		return qfalse;
	}
	if (bs->wallEscapeOption != NEWBOTAI_WALLESC_NONE &&
		(bs->wallEscapeUntil <= level.time || bs->wallEscapeStart > level.time))
	{
		bs->wallEscapeOption = NEWBOTAI_WALLESC_NONE;
		bs->wallEscapeNextTime = level.time + NEWBOTAI_WALL_ESCAPE_RETRY_MS;
	}
	if (bs->wallEscapeOption == NEWBOTAI_WALLESC_NONE)
	{
		if ((bs->wallEscapeNextTime > level.time &&
				bs->wallEscapeNextTime - level.time <= NEWBOTAI_WALL_ESCAPE_RETRY_MS) ||
			!NewBotAI_RetreatWallImminent(bs, wallNormal))
			return qfalse;
		bs->wallEscapeOption = NewBotAI_ChooseWallEscape(bs, wallNormal);
		if (bs->wallEscapeOption == NEWBOTAI_WALLESC_NONE)
		{
			bs->wallEscapeNextTime = level.time + 250;
			return qfalse;
		}
		bs->wallEscapeStart = level.time;
		bs->wallEscapeUntil = level.time + NEWBOTAI_WALL_ESCAPE_COMMIT_MS;
	}

	age = level.time - bs->wallEscapeStart;
	switch (bs->wallEscapeOption)
	{
	case NEWBOTAI_WALLESC_WALLRUN:
		//Turn into the wall and run up it: forward + jump, then tap jump near the wall.
		bs->ideal_viewangles[YAW] = AngleNormalize360(bs->wallEscapeYaw);
		bs->ideal_viewangles[PITCH] = 0.0f;
		NewBotAI_StartEscapeYawOverride(bs, 200);
		trap->EA_MoveForward(bs->client);
		if (age > 100 && bs->cur_ps.groundEntityNum != ENTITYNUM_NONE && age < 500)
			trap->EA_Jump(bs->client);
		else if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE)
		{
			const float groundDist = BS_GroundDistance(bs);
			if (groundDist > 20 && groundDist < 40)
				trap->EA_Jump(bs->client);
		}
		break;
	case NEWBOTAI_WALLESC_FLIPKICK_DRAIN:
		NewBotAI_WallEscapeFaceEnemy(bs);
		if (age < 600)
		{
			trap->EA_MoveForward(bs->client);
			NewBotAI_Flipkick(bs);
		}
		else
		{
			NewBotAI_WallEscapeDrain(bs);
			NewBotAI_WallEscapeSideMove(bs);
		}
		break;
	case NEWBOTAI_WALLESC_DRAIN_FLIPKICK:
		NewBotAI_WallEscapeFaceEnemy(bs);
		if (age < 350)
		{
			NewBotAI_WallEscapeDrain(bs);
			trap->EA_MoveForward(bs->client);
		}
		else
		{
			trap->EA_MoveForward(bs->client);
			NewBotAI_Flipkick(bs);
		}
		break;
	case NEWBOTAI_WALLESC_ROLL_AROUND:
		//A crouch while running on the ground starts a roll; aim it past the enemy.
		NewBotAI_WallEscapeFaceEnemy(bs);
		trap->EA_MoveForward(bs->client);
		NewBotAI_WallEscapeSideMove(bs);
		if (age > 50 && age < 400 && bs->cur_ps.groundEntityNum != ENTITYNUM_NONE)
			trap->EA_Crouch(bs->client);
		break;
	case NEWBOTAI_WALLESC_HOP_OVER:
		NewBotAI_WallEscapeFaceEnemy(bs);
		trap->EA_MoveForward(bs->client);
		NewBotAI_WallEscapeSideMove(bs);
		if (age < 300 && bs->cur_ps.groundEntityNum != ENTITYNUM_NONE)
			trap->EA_Jump(bs->client);
		break;
	default:
		bs->wallEscapeOption = NEWBOTAI_WALLESC_NONE;
		return qfalse;
	}
	return qtrue;
}

void NewBotAI_GetMovement(bot_state_t *bs)
{
	const int hisWeapon = bs->currentEnemy->client->ps.weapon;
	float aggressionBias;
	int hardRetreatHealth;
	int softRetreatHealth;
	float retreatDistance;
	qboolean horizontalSwingStart = qfalse;
	const qboolean pressAdvantage = NewBotAI_ShouldPressAdvantage(bs);

	bs->combatAction = BOT_COMBAT_ACTION_AGGRESSION;

	//Take a moment to disengage and let force points regenerate instead of pressing the
	//attack - see bot_conservation. Continues an existing window, or rolls the chance to
	//start a new one (debounced so we don't re-roll every think frame).
	if (bs->conserveUntil > level.time)
	{
		if (pressAdvantage)
		{
			bs->conserveUntil = 0;
		}
		else
		{
			bs->combatAction = BOT_COMBAT_ACTION_RETREAT_DEFENSE;
			NewBotAI_RetreatDiagonal(bs, (level.framenum & 1) ? qtrue : qfalse);
			return;
		}
	}
	else if (bs->conserveNextRollTime <= level.time)
	{
		bs->conserveNextRollTime = level.time + 1500; //debounce between chances to start a window
		if (NewBotAI_ShouldConserveForce(bs))
		{
			if (NewBotAI_ShouldHoldForPTKForce(bs))
			{
				const int missingForce = NEWBOTAI_PTK_FORCE_BUDGET - bs->cur_ps.fd.forcePower;
				const int ptkConserveMs = Com_Clampi(300, 1400, missingForce * 140);
				bs->conserveUntil = level.time + ptkConserveMs;
			}
			else
			{
				//Item 6: widen the conservation window to 0.5-4 seconds (was 0.8-2s) so a
				//conservation pause actually gives meaningful force regen time.
				bs->conserveUntil = level.time + Q_irand(500, 4000);
			}
			bs->combatAction = BOT_COMBAT_ACTION_RETREAT_DEFENSE;
			NewBotAI_RetreatDiagonal(bs, (Q_irand(0, 1) == 0));
			return;
		}
	}

	if (NewBotAI_ShouldAbortChargedThrowForGetupPush(bs))
	{
		bs->combatAction = BOT_COMBAT_ACTION_AGGRESSION;
		trap->EA_MoveForward(bs->client);

		if (NewBotAI_IsPullkickOpportunity(bs) &&
			bs->frame_Enemy_Len <= NEWBOTAI_IMMEDIATE_FLIPKICK_RANGE &&
			NewBotAI_IsFlipkickSetupReady(bs))
		{
			NewBotAI_Flipkick(bs);
		}
		return;
	}

	aggressionBias = BotGetAggressionBias(bs);

	//A scheduled pk/ptk jump is only meaningful while a kick could actually land. If
	//flipkick stopped being possible (out of FP, no jump level, mid-swing ourselves,
	//etc.) drop the pending/hold schedule so we don't sit on a stale timer.
	if (bs->pullKickJumpTime != 0 && !NewBotAI_CanAttemptFlipkick(bs))
	{
		bs->pullKickJumpTime = 0;
	}

	NewBotAI_PrepareHorizontalSwingStart(bs);
	horizontalSwingStart = (bs->fanPhase == FAN_PHASE_HOLD) ? qtrue : qfalse;

	hardRetreatHealth = 30 - (int)(aggressionBias * 25.0f);
	softRetreatHealth = 60 - (int)(aggressionBias * 35.0f);
	retreatDistance = 350.0f - (aggressionBias * 150.0f);

	if (hardRetreatHealth < 5)
	{
		hardRetreatHealth = 5;
	}
	else if (hardRetreatHealth > 80)
	{
		hardRetreatHealth = 80;
	}

	if (softRetreatHealth < 20)
	{
		softRetreatHealth = 20;
	}
	else if (softRetreatHealth > 95)
	{
		softRetreatHealth = 95;
	}

	if (retreatDistance < 200.0f)
	{
		retreatDistance = 200.0f;
	}
	else if (retreatDistance > 550.0f)
	{
		retreatDistance = 550.0f;
	}

	//Ambient hopping is a random, opt-in feature (bot_hopfrequency) now that flipkick no
	//longer adds jump inputs when a kick isn't actually possible. Skip it while a pk/ptk
	//flipkick jump is pending/held - a random ambient hop right before the scheduled kick
	//jump is what made the bot hop too soon and miss the flipkick.
	if (bs->pullKickJumpTime == 0)
	{
		NewBotAI_TryRandomHop(bs);
	}

	if ((bs->frame_Enemy_Len > 2000) && (bs->currentEnemy && bs->currentEnemy->client && (hisWeapon == WP_SABER))) { //Chase movement
		const vec3_t xyVelocity = {bs->cur_ps.velocity[0], bs->cur_ps.velocity[1]};
		float diffAngle;
		vec3_t moveAngles, a_fo;

		bs->runningLikeASissy = 1;

		vectoangles( xyVelocity, moveAngles );
		bs->aimOffsetAmtYaw = moveAngles[YAW];

		VectorSubtract(bs->eye, bs->currentEnemy->client->ps.origin, a_fo);
		vectoangles(a_fo, a_fo);

		//LODA FIXME TODO:
		//Find angle from us to target.
		//Find angle of our movement
		//If we are holding WA, and our movement angle is X to the left of our target angle, switch to WD, otherwise keep holding WA.
		//If we are holding WD, and our movemetn angle si X to the right of our target angle, switch to WA, othwerise keep holding WD.
		


		diffAngle = bs->aimOffsetAmtYaw - a_fo[YAW];

		trap->EA_MoveForward(bs->client);//W	
		bs->forceMove_Forward = 1;

		if (bs->forceMove_Right >= 0) {
			if (diffAngle > 10) {
				trap->EA_MoveLeft(bs->client);
				bs->forceMove_Right = -1; //A
			}
			else {
				trap->EA_MoveRight(bs->client);
				bs->forceMove_Right = 1; //A
			}

		}
		else {
			if (diffAngle > 10) {
				trap->EA_MoveRight(bs->client);
				bs->forceMove_Right = 1; //A
			}
			else {
				trap->EA_MoveLeft(bs->client);
				bs->forceMove_Right = -1; //A
			}
		}

		NewBotAI_Flipkick(bs);
	}
	else if (hisWeapon > WP_SABER && bs->cur_ps.weapon > WP_SABER) { //gun battle..
		bs->runningLikeASissy = 0;

		if (hisWeapon == WP_REPEATER || hisWeapon == WP_ROCKET_LAUNCHER || hisWeapon == WP_CONCUSSION || (hisWeapon == WP_DEMP2 && bs->currentEnemy->client->forcedFireMode == 2)) { //Splash dmg, jump
			//see if they have jetpack and do movement for that
			//If we have a jetpack, and enough fuel, activate it
			//once its active, hold it
			if (bs->cur_ps.stats[STAT_HOLDABLE_ITEMS] & (1 << HI_JETPACK)) {
				if (g_tweakJetpack.integer) {
					if (bs->cur_ps.groundEntityNum != ENTITYNUM_NONE) { //On ground
						if (bs->cur_ps.jetpackFuel > 90) {
							trap->EA_Jump(bs->client);
							bs->runningToEscapeThreat = level.time;
						}
					}
					else { //In Air
						if (bs->runningToEscapeThreat < level.time - 200)
							trap->EA_Jump(bs->client);
					}
				}
				else {
					trap->EA_Jump(bs->client);
				}
			}
			else if (bs->cur_ps.fd.forcePower > 30) {
				if (!NewBotAI_TouchingWallNotEnemy(bs) || NewBotAI_ShouldWallrunAgainstWalls(bs)) {
					trap->EA_Jump(bs->client);
				}
			}
			NewBotAI_GetGroundDodge(bs);
			trap->EA_MoveForward(bs->client);
		}
		else { //Chaingun, dodge.  Every half second, pick between A or D at random.
			trap->EA_MoveForward(bs->client);
			NewBotAI_GetGroundDodge(bs);
			if (Q_flrand(0.0f, 1.0f) < 0.01) {
				trap->EA_MoveForward(bs->client);
				if (!NewBotAI_TouchingWallNotEnemy(bs) || NewBotAI_ShouldWallrunAgainstWalls(bs)) {
					trap->EA_Jump(bs->client);
				}
			}
		}
	}
	else { //FF Combat movement
		// const float speed = NewBotAI_GetSpeedTowardsEnemy(bs);
		gentity_t *saber;
		qboolean crouch = qfalse;
		const qboolean enemySaberThreatImminent = NewBotAI_IsEnemySaberThreatImminent(bs);
		const qboolean preCollapseDefense = NewBotAI_ShouldPreDefenseAgainstCollapse(bs);
		const qboolean enemySaberReturning = NewBotAI_IsEnemySaberReturning(bs);
		const qboolean stabilizeVsSaberThrow =
			(bs->currentEnemy->client->ps.saberInFlight &&
			 NewBotAI_ShouldStabilizeAgainstEnemySaberThrow(bs)) ? qtrue : qfalse;

		bs->runningLikeASissy = 0;
		bs->forceMove_Forward = 0;
		bs->forceMove_Right = 0;

		saber = &g_entities[bs->currentEnemy->client->ps.saberEntityNum];

		if (NewBotAI_HasDroppedOwnSaber(bs))
		{
			//A knocked-away saber should be recalled immediately while we retreat toward safer
			//wall contact for a vertical wallrun recovery instead of lingering in place or
			//pressing deeper into melee without a blade.
			bs->combatAction = BOT_COMBAT_ACTION_RETREAT_DEFENSE;
			if (!enemySaberThreatImminent && NewBotAI_TouchingWallNotEnemy(bs))
			{
				trap->EA_Jump(bs->client);
				trap->EA_MoveBack(bs->client);
			}
			else if (bs->frame_Enemy_Len <= 96.0f)
			{
				NewBotAI_GetGroundDodge(bs);
			}
			else
			{
				NewBotAI_RetreatDiagonal(bs, (level.framenum & 1) ? qtrue : qfalse);
			}
		}
		else if (enemySaberThreatImminent)
		{
			const int totalHealthDelta = NewBotAI_GetTotalHealthDelta(bs);
			const int ourHealth = g_entities[bs->client].health;

			if (NewBotAI_ShouldEmergencyDrainRollSaberThrow(bs))
			{
				bs->combatAction = BOT_COMBAT_ACTION_RETREAT_DEFENSE;
				NewBotAI_ApplySidewaysDrainRoll(bs, qfalse);
			}
			else if (NewBotAI_ShouldJumpDrainVsSaberThrow(bs))
			{
				const qboolean aggressiveHop =
					(!stabilizeVsSaberThrow &&
					 (NewBotAI_ShouldPreferFlipkickOverThrow(bs) ||
					 (NewBotAI_IsDrainlockAdvantage(bs) && ourHealth > 20) ||
					 (totalHealthDelta < 0 && NewBotAI_ShouldCloseGapVsEnemySaberThrow(bs)))) ? qtrue : qfalse;
				bs->combatAction = aggressiveHop ? BOT_COMBAT_ACTION_AGGRESSION : BOT_COMBAT_ACTION_RETREAT_DEFENSE;
				if (aggressiveHop)
				{
					trap->EA_MoveForward(bs->client);
				}
				else
				{
					NewBotAI_RetreatDiagonal(bs, (level.framenum & 1) ? qtrue : qfalse);
				}
				trap->EA_Jump(bs->client);
				trap->EA_Crouch(bs->client);
				NewBotAI_ConsumeCombatHop(bs);
				if (NewBotAI_IsBeingPulledTowardEnemy(bs) &&
					!(g_forcePowerDisable.integer & (1 << FP_DRAIN)) &&
					(bs->cur_ps.fd.forcePowersKnown & (1 << FP_DRAIN)) &&
					bs->cur_ps.fd.forcePower >= 20)
				{
					level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
					trap->EA_ForcePower(bs->client);
				}
			}
			else if (ourHealth < 51)
			{
				bs->combatAction = BOT_COMBAT_ACTION_RETREAT_DEFENSE;
				NewBotAI_RetreatDiagonal(bs, (level.framenum & 1) ? qtrue : qfalse);
				if (NewBotAI_ShouldPlaySafeDrainVsSaberThrow(bs) &&
					!(g_forcePowerDisable.integer & (1 << FP_DRAIN)) &&
					(bs->cur_ps.fd.forcePowersKnown & (1 << FP_DRAIN)) &&
					bs->cur_ps.fd.forcePower >= 25)
				{
					level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
					trap->EA_ForcePower(bs->client);
				}
			}
			else if (!(stabilizeVsSaberThrow && !enemySaberReturning) &&
				totalHealthDelta >= 30)
			{
				bs->combatAction = BOT_COMBAT_ACTION_AGGRESSION;
				trap->EA_MoveForward(bs->client);
			}
			else if (!(stabilizeVsSaberThrow && !enemySaberReturning) &&
				totalHealthDelta < 0 && NewBotAI_ShouldCloseGapVsEnemySaberThrow(bs))
			{
				bs->combatAction = BOT_COMBAT_ACTION_AGGRESSION;
				trap->EA_MoveForward(bs->client);
			}
			else if (totalHealthDelta < 0)
			{
				bs->combatAction = BOT_COMBAT_ACTION_RETREAT_DEFENSE;
				NewBotAI_RetreatDiagonal(bs, (level.framenum & 1) ? qtrue : qfalse);
			}
			else if (!(stabilizeVsSaberThrow && !enemySaberReturning) &&
				pressAdvantage)
			{
				bs->combatAction = BOT_COMBAT_ACTION_AGGRESSION;
				trap->EA_MoveForward(bs->client);
			}
			else
			{
				bs->combatAction = BOT_COMBAT_ACTION_RETREAT_DEFENSE;
				NewBotAI_RetreatDiagonal(bs, (level.framenum & 1) ? qtrue : qfalse);
			}
			return;
		}
		else if (preCollapseDefense && !NewBotAI_IsBeingPulledTowardEnemy(bs))
		{
			if (bs->conserveUntil < level.time + 500)
			{
				bs->conserveUntil = level.time + 500;
			}
			bs->combatAction = BOT_COMBAT_ACTION_RETREAT_DEFENSE;
			if (bs->frame_Enemy_Len <= 96.0f)
			{
				NewBotAI_GetGroundDodge(bs);
			}
			else
			{
				NewBotAI_RetreatDiagonal(bs, (level.framenum & 1) ? qtrue : qfalse);
			}
			return;
		}
		else if (NewBotAI_IsBeingPulledTowardEnemy(bs))
		{
			int pullKickRangeMs = -1;
			int pullTimingMode = NEWBOTAI_PULL_TIMING_NONE;

			if (NewBotAI_ShouldForcePulledFlipkickOverride(bs, &pullKickRangeMs, &pullTimingMode))
			{
				bs->combatAction = BOT_COMBAT_ACTION_AGGRESSION;
				trap->EA_MoveForward(bs->client);

				if (bs->cur_ps.groundEntityNum != ENTITYNUM_NONE)
				{
					if (NewBotAI_IsFlipkickSetupReady(bs) &&
						(pullTimingMode == NEWBOTAI_PULL_TIMING_IMMEDIATE || pullKickRangeMs <= 120))
					{
						NewBotAI_Flipkick(bs);
					}
					else if (pullKickRangeMs >= 0 && pullKickRangeMs <= 260)
					{
						const int jumpDelay = Com_Clampi(0, 220, pullKickRangeMs);
						bs->pullKickJumpTime = level.time + jumpDelay;
					}
				}
				else if (bs->cur_ps.velocity[2] <= 0.0f)
				{
					const float msToGround = NewBotAI_FlipkickMsToGround(bs);
					if (msToGround >= 0.0f && msToGround <= 260.0f)
					{
						const int landingDelay = Com_Clampi(0, 220, (int)msToGround);
						bs->pullKickJumpTime = level.time + landingDelay;
					}
				}
				return;
			}

			if (NewBotAI_ShouldUseSafePushWindowWhilePulled(bs))
			{
				bs->combatAction = BOT_COMBAT_ACTION_RETREAT_DEFENSE;
				NewBotAI_RetreatDiagonal(bs, (level.framenum & 1) ? qtrue : qfalse);
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
				trap->EA_ForcePower(bs->client);
				return;
			}
		}

		else if (bs->currentEnemy->client->ps.legsAnim == BOTH_GETUP_BROLL_B && bs->frame_Enemy_Len < 100) {//Dodge a getup?
			trap->EA_Crouch(bs->client);
			if (bs->cur_ps.viewheight < 48) //only move forward if they are crouched
				trap->EA_MoveForward(bs->client);
			crouch = qtrue;
		}
		else if ( bs->currentEnemy->client->ps.legsAnim == BOTH_GETUP_BROLL_F && bs->frame_Enemy_Len < 150) {
			trap->EA_Crouch(bs->client);
			if (bs->cur_ps.viewheight < 48) //only move forward if they are crouched
				trap->EA_MoveForward(bs->client);
			crouch = qtrue;
		}
		else if (NewBotAI_ShouldPlaySafeDrainVsSaberThrow(bs) &&
			bs->currentEnemy->client->ps.fd.forcePower < 20 &&
			!NewBotAI_ShouldPreferFlipkickOverThrow(bs))
		{
			bs->combatAction = BOT_COMBAT_ACTION_RETREAT_DEFENSE;
			NewBotAI_RetreatDiagonal(bs, (level.framenum & 1) ? qtrue : qfalse);
			return;
		}
		else if (!pressAdvantage && !NewBotAI_CanAttemptFlipkick(bs) && NewBotAI_GetEnemyClosingSpeed(bs) > 420.0f) {
			//Item 2B: retreat from a fast incoming enemy (saber duel / flipkick disabled) instead of
			//our normal forward approach. Attacks and jumps are unaffected -- they're decided by
			//NewBotAI_GetAttack and this block, respectively -- only the forward/back choice changes.
			bs->combatAction = BOT_COMBAT_ACTION_RETREAT_DEFENSE;
			if (!NewBotAI_RunRetreatWallEscape(bs))
			{
				NewBotAI_RetreatDiagonal(bs, qtrue);
				if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE - 1 &&
					(!NewBotAI_TouchingWallNotEnemy(bs) || NewBotAI_ShouldWallrunAgainstWalls(bs)))
				{
					trap->EA_Jump(bs->client);
				}
			}
		}
		else if (!pressAdvantage && ((g_entities[bs->client].health < hardRetreatHealth) ||
				((g_entities[bs->client].health < softRetreatHealth)
				&& (bs->cur_ps.fd.forcePower < 30)
				&& !(bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB))
				&& (bs->frame_Enemy_Len < retreatDistance)
				//Item 5: don't let a soft-retreat break off an active drainlock chase - the
				//critical hardRetreatHealth safety check above still applies regardless.
				&& !NewBotAI_IsDrainlockAdvantage(bs)))) {
			qboolean wallRun = qfalse;
			bs->combatAction = BOT_COMBAT_ACTION_RETREAT_DEFENSE;
			//Running routine, we should add a wallrun search to this.

			//trap_EA_MoveBack(bs->client);//Always move forward i guess	

			//Wallrun if possible
			//Set wallrun flag
			//If we are touching a suitable wall, insta 180 and wallrun it
			//stay in wallrun until end then jump

			if (NewBotAI_RunRetreatWallEscape(bs)) {
				wallRun = qtrue; //a wall escape owns this think's movement
			}
			else if (bs->frame_Enemy_Len > 200) {
				const float horizontalSpeedSquared = bs->cur_ps.velocity[0] * bs->cur_ps.velocity[0] +
					bs->cur_ps.velocity[1] * bs->cur_ps.velocity[1];
				//Only treat this as a real wall block (and worth a 180+jump escape) when we
				//are actually stuck - grounded and barely moving despite trying to retreat.
				//A bare proximity trace alone fires constantly while merely running past or
				//alongside a wall, turning every graze into a spinning jump spam.
				const qboolean actuallyStuck = (bs->cur_ps.groundEntityNum != ENTITYNUM_NONE &&
					horizontalSpeedSquared < NEWBOTAI_WALLAVOID_STUCK_SPEED_SQ) ? qtrue : qfalse;
			if (NewBotAI_TouchingWallNotEnemy(bs) && actuallyStuck && bs->wallAvoidNextTime <= level.time) { //Touching wall and genuinely blocked?
				vec3_t moveAngles, moveDir;
				float GroundDist = BS_GroundDistance(bs);
				VectorClear(moveAngles);
				moveAngles[YAW] = bs->ideal_viewangles[YAW];
				AngleVectors(moveAngles, moveDir, NULL, NULL);
				if (NewBotAI_HandleClimbableForwardObstacle(bs, moveDir))
				{
					wallRun = qfalse;
				}
				else
				{
					wallRun = qtrue;
					bs->wallAvoidNextTime = level.time + NewBotAI_GetWallRedirectIntervalMs(); //withhold repeat jump/turn attempts for a bit

					//Com_Printf("Touching Wall\n");
					bs->ideal_viewangles[YAW] = AngleNormalize360(bs->ideal_viewangles[YAW] + NewBotAI_GetWallEscapeTurnAngle());
					bs->ideal_viewangles[PITCH] = 0.0f;
					bs->goalAngles[PITCH] = 0.0f;
					bs->ideal_viewangles[ROLL] = 0.0f;
					bs->goalAngles[ROLL] = 0.0f;
					NewBotAI_StartEscapeYawOverride(bs, NewBotAI_GetWallRedirectIntervalMs());


					trap->EA_MoveForward(bs->client);

					if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE - 1) {
						trap->EA_Jump(bs->client); //Jump until we are off ground
					}
					else { //In Air
						if (GroundDist > 20 && GroundDist < 40) { //And we are moving down? idk
							trap->EA_Jump(bs->client);  //Tap jump while in air at wall
						}
						if (1) { //Jump at end of wallrun

						}
					}
				}
			}
			if (level.time % 3000 > 1500 && !wallRun)  {
				if (bs->frame_Enemy_Len < 140)
					trap->EA_MoveForward(bs->client);//Always move forward i guess	
				else if (bs->frame_Enemy_Len < MAX_DRAIN_DISTANCE - 100)
					NewBotAI_RetreatDiagonal(bs, qtrue);
				else
					trap->EA_MoveLeft(bs->client);
			}
			else if (!wallRun) {
				if (bs->frame_Enemy_Len < 140)
					trap->EA_MoveForward(bs->client);//Always move forward i guess	
				else if (bs->frame_Enemy_Len < MAX_DRAIN_DISTANCE - 100)
					NewBotAI_RetreatDiagonal(bs, qfalse);
				else
					trap->EA_MoveRight(bs->client);
			}
		}
	}
		//A scheduled pk/ptk flipkick jump: we pulled (or selected pull during a throw)
		//and already projected a final kick window from the target's current distance,
		//relative closing speed, and any assumed pull acceleration. The kick attempt
		//itself happens via NewBotAI_Flipkick in the normal combat path below.
		else if (bs->pullKickJumpTime != 0)
		{
			trap->EA_MoveForward(bs->client);
			if (bs->cur_ps.groundEntityNum != ENTITYNUM_NONE &&
				bs->pullKickJumpTime <= level.time)
			{
				trap->EA_Jump(bs->client);
				bs->flipkickInputTime = level.time + NewBotAI_GetFlipkickInputWindowMs();
				bs->flipkickJumpHeld = qtrue;
				bs->pullKickJumpTime = 0;
				//Same 300ms post-attempt cooldown NewBotAI_Flipkick applies to its own
				//attempts - stops NewBotAI_SchedulePullkickJump from immediately re-arming
				//another jump the instant we land next to the (still close) enemy.
				bs->lastFlipkickAttemptTime = level.time + 300;
			}
		}
		else if (!horizontalSwingStart && NewBotAI_UpdateSwingDodge(bs))
		{
			//Dodge handled (lateral strafe / jump, or a low-skill backpedal mistake).
		}
		else if (bs->frame_Enemy_Len > 80) {
			float dot;
			dot = DotProduct(bs->cur_ps.velocity, bs->currentEnemy->client->ps.velocity);
			//see if they are running away
			//dot product of our vel and theirs, if its high, bhop to them? or push/pull stun them
			if (dot > 50000) { //Running away nicely
				if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE && !NewBotAI_CanAttemptFlipkick(bs) && !NewBotAI_HasExclusiveFlipkickMovement(bs)) {
					//Only strafe when we aren't about to flipkick and no flipkick jump sequence is
					//live; flipkicks require straight-forward movement only, and mixing in lateral
					//input here was causing diagonal air movement that made the kick miss.
					if (level.time % 1000 > 500)
						trap->EA_MoveRight(bs->client);
					else
						trap->EA_MoveLeft(bs->client);
				}
				if (NewBotAI_IsFlipkickSetupReady(bs))
				{
					NewBotAI_Flipkick(bs);
				}
				else
				{
					NewBotAI_PushHopRetryCooldown(bs);
					NewBotAI_SaberDuelIndecisionFallback(bs, horizontalSwingStart);
				}
			}
			if (!horizontalSwingStart) {
				trap->EA_MoveForward(bs->client);//Always move forward i guess
				//In saber duels, two bots walking straight at each other glitch together.
				//Periodically pick a random lateral direction to break the symmetry.
				//Don't wiggle while a flipkick jump sequence is live - the kick needs exclusively
				//straight-forward input or it turns into a diagonal wallrun off opponents/walls.
				if (hisWeapon == WP_SABER && bs->cur_ps.weapon == WP_SABER && bs->cur_ps.groundEntityNum != ENTITYNUM_NONE &&
					!NewBotAI_IsRecoveryMovementActive(bs) && !NewBotAI_HasExclusiveFlipkickMovement(bs)) {
					if (bs->randomStrafeEndTime <= level.time) {
						NewBotAI_RollRandomStrafeOverlay(bs, 180, 700 + (int)(BotGetChanceBiasPercent(bot_fanbias.value) * 5.0f), qfalse);
					}
					if (!NewBotAI_ShouldAvoidDiagonalWallrun(bs))
						NewBotAI_ApplyRandomStrafePattern(bs);
				}
			}
			else
				NewBotAI_ApplyHorizontalSwingMove(bs);
		}
		else if (!horizontalSwingStart) {
			trap->EA_MoveForward(bs->client);
			//Point-blank saber duel: also wiggle laterally to avoid glitching into each other.
			if (hisWeapon == WP_SABER && bs->cur_ps.weapon == WP_SABER && bs->cur_ps.groundEntityNum != ENTITYNUM_NONE &&
				!NewBotAI_IsRecoveryMovementActive(bs) && !NewBotAI_HasExclusiveFlipkickMovement(bs)) {
				if (bs->randomStrafeEndTime <= level.time) {
					NewBotAI_RollRandomStrafeOverlay(bs, 200, 600, qfalse);
				}
				//Item 2C: lateral+forward while touching a wall becomes a diagonal wallrun -
				//strip the lateral component unless we are critically low and escaping.
				if (!NewBotAI_ShouldAvoidDiagonalWallrun(bs))
					NewBotAI_ApplyRandomStrafePattern(bs);
			}
		}
		else
			NewBotAI_ApplyHorizontalSwingMove(bs);

		if (!crouch && ((bs->cur_ps.groundEntityNum != ENTITYNUM_NONE - 1) || (bs->currentEnemy->client->ps.saberInFlight && saber->s.pos.trTime) || (NewBotAI_GetTimeToInRange(bs, 60, 100) < 100))) { //Jump if they will be in range of flipkick or they are trying to saberthrow
			if ((bs->cur_ps.groundEntityNum != ENTITYNUM_NONE - 1) && bs->currentEnemy->client->ps.saberInFlight && bs->frame_Enemy_Len > 250 && (BS_GroundDistance(bs) > 20)) {
				trap->EA_Crouch(bs->client); 
			}
			else {
				if (NewBotAI_IsFlipkickSetupReady(bs))
				{
					NewBotAI_Flipkick(bs);
				}
				else
				{
					//Item 2A: no flipkick available here (g_flipKick disabled or duel type disallows
					//it) -- use the fan-chain/red-swing/wiggle fallback instead of leaving the bot
					//to stand indecisively.
					NewBotAI_SaberDuelIndecisionFallback(bs, horizontalSwingStart);
				}
			}

		}

		/*
		if ((bs->cur_ps.groundEntityNum != ENTITYNUM_NONE - 1) || ((speed >= 0) && ((bs->frame_Enemy_Len / speed) < 0.63f)) ||
			(bs->frame_Enemy_Len < 90) || bs->currentEnemy->client->ps.saberInFlight) {//Flipkick if they are close or trying to saberthrow me
				if ((bs->cur_ps.fd.forcePower > 6))
					NewBotAI_Flipkick(bs);
		}
		*/


		else if (Q_flrand(0.0f, 1.0f) < 0.01 && (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE - 1)) {
			//trap->EA_MoveRight(bs->client);
			//NewBotAI_Flipkick(bs);
		}

		if (!(bs->cur_ps.weapon == WP_SABER && bs->cur_ps.saberInFlight && !bs->cur_ps.saberEntityNum) &&
			bs->frame_Enemy_Vis && bs->frame_Enemy_Len > 80 &&
			VectorLengthSquared(bs->cur_ps.velocity) < 100.0f &&
			bs->flipkickInputTime <= level.time)
		{
			trap->EA_MoveForward(bs->client);
		}
	}
}

qboolean BG_InRoll3(int anim)
{
	switch (anim)
	{
	case BOTH_GETUP_BROLL_B:
	case BOTH_GETUP_BROLL_F:
	case BOTH_GETUP_BROLL_L:
	case BOTH_GETUP_BROLL_R:
	case BOTH_GETUP_FROLL_B:
	case BOTH_GETUP_FROLL_F:
	case BOTH_GETUP_FROLL_L:
	case BOTH_GETUP_FROLL_R:
	case BOTH_ROLL_F:
	case BOTH_ROLL_B:
	case BOTH_ROLL_R:
	case BOTH_ROLL_L:
		return qtrue;
	}
	return qfalse;
}

static qboolean NewBotAI_IsGetupAnim(int anim)
{
	switch (anim)
	{
	case BOTH_GETUP1:
	case BOTH_GETUP2:
	case BOTH_GETUP3:
	case BOTH_GETUP4:
	case BOTH_GETUP5:
	case BOTH_FORCE_GETUP_F1:
	case BOTH_FORCE_GETUP_F2:
	case BOTH_FORCE_GETUP_B1:
	case BOTH_FORCE_GETUP_B2:
	case BOTH_FORCE_GETUP_B3:
	case BOTH_FORCE_GETUP_B4:
	case BOTH_FORCE_GETUP_B5:
	case BOTH_FORCE_GETUP_B6:
	case BOTH_GETUP_BROLL_B:
	case BOTH_GETUP_BROLL_F:
	case BOTH_GETUP_BROLL_L:
	case BOTH_GETUP_BROLL_R:
	case BOTH_GETUP_FROLL_B:
	case BOTH_GETUP_FROLL_F:
	case BOTH_GETUP_FROLL_L:
	case BOTH_GETUP_FROLL_R:
		return qtrue;
	}

	return qfalse;
}

static qboolean NewBotAI_IsForceGetupAnim(int anim)
{
	switch (anim)
	{
	case BOTH_FORCE_GETUP_F1:
	case BOTH_FORCE_GETUP_F2:
	case BOTH_FORCE_GETUP_B1:
	case BOTH_FORCE_GETUP_B2:
	case BOTH_FORCE_GETUP_B3:
	case BOTH_FORCE_GETUP_B4:
	case BOTH_FORCE_GETUP_B5:
	case BOTH_FORCE_GETUP_B6:
		return qtrue;
	}

	return qfalse;
}

static qboolean NewBotAI_IsStandingRoll(int anim)
{
	switch (anim)
	{
	case BOTH_ROLL_F:
	case BOTH_ROLL_B:
	case BOTH_ROLL_R:
	case BOTH_ROLL_L:
		return qtrue;
	}
	return qfalse;
}

static qboolean NewBotAI_IsEnemyPreGetupKnockdownState(bot_state_t *bs)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (!BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim) &&
		!BG_InKnockDown(bs->currentEnemy->client->ps.torsoAnim) &&
		!NewBotAI_IsGetupAnim(bs->currentEnemy->client->ps.legsAnim) &&
		!NewBotAI_IsGetupAnim(bs->currentEnemy->client->ps.torsoAnim))
	{
		return qfalse;
	}

	return qtrue;
}

static qboolean NewBotAI_IsEnemyGetupPushWindow(bot_state_t *bs)
{
	qboolean enemyForceGetup;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	enemyForceGetup = (NewBotAI_IsForceGetupAnim(bs->currentEnemy->client->ps.legsAnim) ||
		NewBotAI_IsForceGetupAnim(bs->currentEnemy->client->ps.torsoAnim)) ? qtrue : qfalse;

	if (!NewBotAI_IsGetupAnim(bs->currentEnemy->client->ps.legsAnim) &&
		!NewBotAI_IsGetupAnim(bs->currentEnemy->client->ps.torsoAnim))
	{
		return qfalse;
	}
	if (!enemyForceGetup)
	{
		return qfalse;
	}

	if (bs->currentEnemy->client->ps.forceHandExtend == HANDEXTEND_FORCEPUSH)
	{
		return qtrue;
	}

	//Keep the interrupt window alive briefly through the force-getup animation after
	//the exact push frame so charged-throw abort logic can still react consistently.
	if (bs->currentEnemy->client->ps.forceHandExtendTime > 0)
	{
		const int sincePushMs = level.time - bs->currentEnemy->client->ps.forceHandExtendTime;
		if (sincePushMs >= 0 && sincePushMs <= 400)
		{
			return qtrue;
		}
	}

	return qfalse;
}

static qboolean NewBotAI_ShouldAbortChargedThrowForGetupPush(bot_state_t *bs)
{
	int chargeAgeMs;
	vec3_t toEnemyAngles;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (bs->cur_ps.weapon != WP_SABER ||
		bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT ||
		bs->cur_ps.saberInFlight)
	{
		return qfalse;
	}

	if (!NewBotAI_IsEnemyGetupPushWindow(bs))
	{
		return qfalse;
	}

	if (!bs->frame_Enemy_Vis || bs->frame_Enemy_Len > 320)
	{
		return qfalse;
	}
	if (bs->cur_ps.weaponChargeTime <= 0)
	{
		return qfalse;
	}
	chargeAgeMs = level.time - bs->cur_ps.weaponChargeTime;
	if (chargeAgeMs < 150)
	{
		return qfalse;
	}

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, toEnemyAngles);
	vectoangles(toEnemyAngles, toEnemyAngles);
	if (!InFieldOfVision(bs->viewangles, 35, toEnemyAngles))
	{
		return qfalse;
	}

	return qtrue;
}

static qboolean NewBotAI_TryAbortChargedThrowIntoPullkick(bot_state_t *bs)
{
	if (!NewBotAI_ShouldAbortChargedThrowForGetupPush(bs))
	{
		return qfalse;
	}

	if (NewBotAI_IsPullkickOpportunity(bs) &&
		bs->frame_Enemy_Len <= NEWBOTAI_IMMEDIATE_FLIPKICK_RANGE &&
		NewBotAI_IsFlipkickSetupReady(bs))
	{
		NewBotAI_Flipkick(bs);
		return qtrue;
	}

	return qfalse;
}

static qboolean NewBotAI_IsKnockdownRecoveryRoll(int anim)
{
	switch (anim)
	{
	case BOTH_GETUP_BROLL_B:
	case BOTH_GETUP_BROLL_F:
	case BOTH_GETUP_BROLL_L:
	case BOTH_GETUP_BROLL_R:
	case BOTH_GETUP_FROLL_B:
	case BOTH_GETUP_FROLL_F:
	case BOTH_GETUP_FROLL_L:
	case BOTH_GETUP_FROLL_R:
		return qtrue;
	}

	return qfalse;
}

static int BotGetResponseDelayMs(void)
{
	int delay = bot_delay.integer;

	if (delay == 0)
	{
		delay = bot_delayresponsetime.integer;
	}
	if (delay == 0)
	{
		delay = bot_responseTimeDelay.integer;
	}

	if (delay < 0)
	{
		delay = 0;
	}
	else if (delay > 5000)
	{
		delay = 5000;
	}

	return delay;
}

//Shared 1-10 penalty scaler for extra handicaps layered on top of legacy behavior.
//Level 1 keeps 100% of the added penalty, level 9 keeps 10%, level 10 keeps none.
//This preserves the legacy "divide by level" feel while keeping the added handicap
//fully disabled for level 10.
static float BotGetExtraPenaltyScaleForSkill(bot_state_t *bs)
{
	float skill;

	if (!bs)
	{
		return 1.0f;
	}

	skill = bs->settings.skill;
	if (skill <= 1.0f)
	{
		return 1.0f;
	}
	if (skill >= 10.0f)
	{
		return 0.0f;
	}
	if (skill >= 9.0f)
	{
		return 0.1f;
	}

	return 1.0f / skill;
}

//Uses the existing reflex-scaled response delay behavior as the base, then applies the
//shared 1-10 penalty rule to bot_delay so higher levels reduce the added handicap.
//bot_aimspeed no longer modifies delay.
static int BotGetReflexScaledResponseDelayMs(bot_state_t *bs)
{
	int delay = BotGetResponseDelayMs();
	float reflexScale;

	if (!bs)
	{
		return delay;
	}

	reflexScale = 100.0f / (float)((bs->skills.reflex > 0) ? bs->skills.reflex : 100);
	delay = (int)((float)delay * reflexScale);
	delay = (int)((float)delay * BotGetExtraPenaltyScaleForSkill(bs));

	if (delay < 0)
	{
		delay = 0;
	}
	else if (delay > 5000)
	{
		delay = 5000;
	}

	return delay;
}

static float BotGetTargetDistanceLimit(void)
{
	float targetDistanceLimit = bot_targetdistance.value;

	if (targetDistanceLimit < 0.0f)
	{
		targetDistanceLimit = 0.0f;
	}

	return targetDistanceLimit;
}

static qboolean NewBotAI_IsEnemyWithinTargetDistance(bot_state_t *bs, gentity_t *enemy)
{
	float targetDistanceLimit;
	vec3_t enemyOrigin;
	vec3_t delta;
	float enemyDistance;

	if (!bs || !enemy || !enemy->client)
	{
		return qfalse;
	}

	targetDistanceLimit = BotGetTargetDistanceLimit();
	if (targetDistanceLimit <= 0.0f)
	{
		return qtrue;
	}

	VectorCopy(enemy->client->ps.origin, enemyOrigin);
	VectorSubtract(enemyOrigin, bs->origin, delta);
	enemyDistance = VectorLength(delta);

	return (enemyDistance <= targetDistanceLimit) ? qtrue : qfalse;
}

static int BotGetTargetTimeoutMs(void)
{
	return Com_Clampi(0, 60000, bot_target_timeout.integer);
}

static qboolean NewBotAI_ShouldRetainLostSightTarget(bot_state_t *bs, gentity_t *enemy)
{
	const int targetTimeoutMs = BotGetTargetTimeoutMs();

	if (!bs || !enemy || !enemy->client || bs->frame_Enemy_Vis || targetTimeoutMs <= 0)
	{
		return qfalse;
	}
	if (enemy->health < 1)
	{
		return qfalse;
	}
	if (enemy->client->pers.connected != CON_CONNECTED)
	{
		return qfalse;
	}
	if (bs->lastVisibleEnemyIndex != enemy->s.number)
	{
		return qfalse;
	}

	return (bs->lastVisibleEnemyTime > level.time - targetTimeoutMs) ? qtrue : qfalse;
}

static qboolean NewBotAI_ShouldPursueTargetThroughWaypoints(bot_state_t *bs, gentity_t *enemy)
{
	if (!bs || !enemy || !enemy->client || bs->frame_Enemy_Vis)
	{
		return qfalse;
	}
	if (enemy != bs->currentEnemy)
	{
		return qfalse;
	}
	if (!NewBotAI_HasWaypointNavigation())
	{
		return qfalse;
	}
	if (!NewBotAI_HasValidCurrentEnemy(bs))
	{
		return qfalse;
	}

	return NewBotAI_IsEnemyWithinTargetDistance(bs, enemy);
}

static void NewBotAI_ClearCurrentEnemyLock(bot_state_t *bs)
{
	if (!bs)
	{
		return;
	}

	bs->currentEnemy = NULL;
	bs->enemySeenTime = 0;
	bs->lastVisibleEnemyIndex = ENTITYNUM_NONE;
	bs->lastVisibleEnemyTime = 0;
	bs->enemyWaypointFallbackIndex = -1;
	bs->enemyWaypointFallbackTime = 0;
	bs->enemyWaypointFallbackEnemyNum = -1;
}

enum
{
	NEWBOTAI_TARGET_DEFAULT = -1,
	NEWBOTAI_TARGET_HUMANS_ONLY = -2,
	NEWBOTAI_TARGET_PREFER_HUMANS = -3,
	NEWBOTAI_TARGET_PREFER_HUMANS_DUEL = -4
};

static int BotGetNewBotAITargetMode(void)
{
	switch (g_newBotAITarget.integer)
	{
	case NEWBOTAI_TARGET_DEFAULT:
	case NEWBOTAI_TARGET_HUMANS_ONLY:
	case NEWBOTAI_TARGET_PREFER_HUMANS:
	case NEWBOTAI_TARGET_PREFER_HUMANS_DUEL:
		return g_newBotAITarget.integer;
	default:
		break;
	}

	if (g_newBotAITarget.integer < NEWBOTAI_TARGET_PREFER_HUMANS_DUEL)
	{
		return NEWBOTAI_TARGET_HUMANS_ONLY;
	}

	return g_newBotAITarget.integer;
}

//-3 and -4 both prefer human targets first, then fall back to allowing bot-vs-bot
//targeting once no humans are active; only -4 stays force-duel-only while searching.
static qboolean BotTargetModePrefersHumansThenBots(int targetMode)
{
	return (targetMode == NEWBOTAI_TARGET_PREFER_HUMANS || targetMode == NEWBOTAI_TARGET_PREFER_HUMANS_DUEL);
}

static qboolean BotTargetModeAllowsBotDuelChallenges(int targetMode)
{
	return (targetMode == NEWBOTAI_TARGET_PREFER_HUMANS ||
		targetMode == NEWBOTAI_TARGET_PREFER_HUMANS_DUEL);
}

//The long bot-vs-bot cooldown only exists to keep bots available for humans; with no
//humans around, bots go back to the normal cooldown so force-duel Elo keeps building.
static qboolean BotTargetModeUsesExtendedBotDuelCooldown(int targetMode)
{
	return (BotTargetModeAllowsBotDuelChallenges(targetMode) &&
		BotHasActiveHumanPlayers()) ? qtrue : qfalse;
}

static qboolean BotTargetModeIsForceDuelOnly(int targetMode)
{
	return (targetMode == NEWBOTAI_TARGET_PREFER_HUMANS_DUEL);
}

static qboolean BotTargetModeAllowsBotEnemies(int targetMode)
{
	return (targetMode != NEWBOTAI_TARGET_HUMANS_ONLY);
}

static qboolean BotTargetModePassesScanFilter(int targetMode, gentity_t *ent, qboolean preferredHumansOnly)
{
	const qboolean isBot = (ent->r.svFlags & SVF_BOT) ? qtrue : qfalse;

	if (isBot && !BotTargetModeAllowsBotEnemies(targetMode))
	{
		return qfalse;
	}

	if (preferredHumansOnly && isBot)
	{
		return qfalse;
	}

	return qtrue;
}

static qboolean BotHasActiveHumanPlayers(void)
{
	int i;

	for (i = 0; i < level.numConnectedClients; i++)
	{
		gentity_t *ent = &g_entities[level.sortedClients[i]];

		if (!ent->inuse)
		{
			continue;
		}
		if (ent->r.svFlags & SVF_BOT)
		{
			continue;
		}
		if (!ent->client)
		{
			continue;
		}
		if (ent->client->lastHereTime > level.time - 120000)
		{
			return qtrue;
		}
	}

	return qfalse;
}

//After bot_duelcountmax completed duels, -3/-4 bots return to FFA: they stop issuing
//and accepting duel challenges while they explore for a new opponent (tracked in
//NewBotAI via duelCompletedCount/ffaExploreUntil).
static qboolean NewBotAI_InFFAExploreWindow(bot_state_t *bs, int targetMode)
{
	if (!BotTargetModeAllowsBotDuelChallenges(targetMode))
	{
		return qfalse;
	}

	if (bs->ffaExploreUntil > level.time)
	{
		return qtrue;
	}

	return (bs->duelCompletedCount >= bot_duelcountmax.integer) ? qtrue : qfalse;
}

//Bots may only request a duel once every 7 seconds by default; bot-initiated bot-vs-bot
//offers are throttled much harder (2 minutes) in -3/-4 target modes so human duel
//opportunities are not crowded out by rapid bot challenge loops.
#define NEWBOTAI_DUEL_REQUEST_COOLDOWN_MS 7000
#define NEWBOTAI_DUEL_REQUEST_BOT_VS_BOT_COOLDOWN_MS 120000
#define NEWBOTAI_DUEL_REQUEST_MIN_INTERVAL_MS 1000
#define NEWBOTAI_DUEL_OFFER_HOLD_MS 3000
#define NEWBOTAI_DUEL_OFFER_HOLD_DAMAGE_CANCEL 30
#define NEWBOTAI_DUEL_ACCEPT_RANGE 224.0f
#define NEWBOTAI_DUEL_STALEMATE_MS 4000
#define NEWBOTAI_DUEL_STALEMATE_RADIUS 320.0f
#define NEWBOTAI_DUEL_STALEMATE_BLACKLIST_MS 20000
#define NEWBOTAI_DUEL_STALEMATE_ROAM_MS 6000

static qboolean NewBotAI_ShouldIssueBotDuelChallenge(bot_state_t *bs, int targetMode)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}
	if (!BotTargetModeAllowsBotDuelChallenges(targetMode))
	{
		return qfalse;
	}
	if (NewBotAI_InFFAExploreWindow(bs, targetMode))
	{
		return qfalse;
	}
	if (!bot_honorableduelacceptance.integer || !g_privateDuel.integer)
	{
		return qfalse;
	}
	if (bs->cur_ps.duelInProgress || bs->currentEnemy->client->ps.duelInProgress)
	{
		return qfalse;
	}
	if (bs->botDuelRequestThrottleUntil > level.time || bs->botChallengingTime > level.time)
	{
		return qfalse;
	}
	if (!bs->frame_Enemy_Vis || bs->frame_Enemy_Len > 220.0f)
	{
		return qfalse;
	}

	return qtrue;
}

static qboolean NewBotAI_TryIssueBotDuelChallenge(bot_state_t *bs, int targetMode)
{
	vec3_t toEnemy;
	vec3_t oldViewAngles;
	int duelType;

	if (!NewBotAI_ShouldIssueBotDuelChallenge(bs, targetMode))
	{
		return qfalse;
	}

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, toEnemy);
	vectoangles(toEnemy, toEnemy);
	VectorCopy(g_entities[bs->client].client->ps.viewangles, oldViewAngles);
	VectorCopy(toEnemy, g_entities[bs->client].client->ps.viewangles);
	VectorCopy(toEnemy, bs->ideal_viewangles);

	// Bot-vs-bot offers in -3/-4 are always full-force duels so the force Elo keeps
	// building. Offers to humans use the last duel type a human offered this bot
	// (saber or force), defaulting to force.
	duelType = 1;
	if (!(bs->currentEnemy->r.svFlags & SVF_BOT) && bs->humanDuelTypePref == 1)
	{
		duelType = 0;
	}
	Cmd_EngageDuel_f(&g_entities[bs->client], duelType);

	VectorCopy(oldViewAngles, g_entities[bs->client].client->ps.viewangles);
	bs->botChallengingTime = level.time + NewBotAI_GetDuelRequestCooldownMs(
		NEWBOTAI_DUEL_REQUEST_COOLDOWN_MS,
		NEWBOTAI_DUEL_REQUEST_BOT_VS_BOT_COOLDOWN_MS,
		BotTargetModeUsesExtendedBotDuelCooldown(targetMode) ? 1 : 0,
		(g_entities[bs->client].r.svFlags & SVF_BOT) ? 1 : 0,
		(bs->currentEnemy->r.svFlags & SVF_BOT) ? 1 : 0);
	bs->botDuelRequestThrottleUntil = level.time + NEWBOTAI_DUEL_REQUEST_MIN_INTERVAL_MS;
	bs->duelNoStrafeUntil = level.time + Com_Clampi(0, 10000, bot_duel_nostrafetime.integer);
	bs->beStill = level.time + 250;
	bs->doAttack = 0;
	bs->doAltAttack = 0;
	//Stay passive and targetable for a few seconds so the offer can be accepted - but
	//only when Cmd_EngageDuel_f actually sent it.
	if (g_entities[bs->client].client->ps.duelIndex == bs->currentEnemy->s.number &&
		g_entities[bs->client].client->ps.duelTime > level.time)
	{
		bs->duelOfferHoldUntil = level.time + NEWBOTAI_DUEL_OFFER_HOLD_MS;
		bs->duelOfferTargetNum = bs->currentEnemy->s.number;
		bs->duelOfferHoldHealth = g_entities[bs->client].health;
	}
	return qtrue;
}

static int BotGetLowHangingFruitHP(void)
{
	int threshold = bot_lowhangingfruitHP.integer;

	if (threshold < 0)
	{
		threshold = 0;
	}
	else if (threshold > 100)
	{
		threshold = 100;
	}

	return threshold;
}

static float BotGetLowHangingFruitDistance(void)
{
	float distance = bot_lowhanginfruitDistance.value;

	if (bot_lowhangingfruitdistance.value != 1024.0f ||
		bot_lowhanginfruitDistance.value == 1024.0f)
	{
		distance = bot_lowhangingfruitdistance.value;
	}

	if (distance < 0.0f)
	{
		distance = 0.0f;
	}

	return distance;
}

static float BotGetBiasWeight(float value)
{
	if (value < -1.0f)
	{
		return -1.0f;
	}
	else if (value > 1.0f)
	{
		return 1.0f;
	}

	return value;
}

static float BotGetChanceBiasPercent(float value)
{
	if (value < 0.0f)
	{
		return 0.0f;
	}
	else if (value > 100.0f)
	{
		return 100.0f;
	}

	return value;
}

static float BotScaleMistakeBiasForSkill(bot_state_t *bs, float biasValue)
{
	float skillScale;
	float chance;

	if (!bs)
	{
		return 0.0f;
	}

	chance = BotGetChanceBiasPercent(biasValue);
	if (chance <= 0.0f)
	{
		return 0.0f;
	}

	//Item 4: mistakebias needs to stay meaningful across the whole skill range - the old
	//squared skill falloff zeroed it out entirely for skill 6+ (so even a maxed-out bias
	//gave high-skill bots zero delay pulling out of a grip) and hard-capped skill 1-2 at
	//a token 5%. Level 10 bots are the one exception and play perfectly (no mistakes at
	//all); below that, lower skill leans further into the bias and higher skill leans
	//less, so lower levels miss all or most of their grip-escape pulls while higher
	//levels still miss some. The per-session failures this feeds into are rolled in
	//NewBotAI_ReactToBeingGripped (see the NewBotAI_GetGrip*Chance helpers below).
	if (bs->settings.skill >= 10.0f)
	{
		return 0.0f;
	}

	skillScale = 1.0f + ((5.0f - bs->settings.skill) * 0.08f);
	if (skillScale < 0.6f)
	{
		skillScale = 0.6f;
	}
	else if (skillScale > 1.4f)
	{
		skillScale = 1.4f;
	}

	chance *= skillScale;
	if (chance > 100.0f)
	{
		chance = 100.0f;
	}

	return chance;
}

//General combat mistakes (timing, positioning, combos, pull aim) - bot_mistakebias.
static float BotGetMistakeBiasChance(bot_state_t *bs)
{
	return BotScaleMistakeBiasForSkill(bs, bot_mistakebias.value);
}

//Gripkick escape mistakes only - bot_gkmistakebias, tuned separately so high-level bots
//can stop fumbling grip escapes while lower levels keep (or gain) general mistakes.
static float BotGetGripMistakeBiasChance(bot_state_t *bs)
{
	return BotScaleMistakeBiasForSkill(bs, bot_gkmistakebias.value);
}

// Item 4: the amount of time (ms) mistakebias should keep this bot from correctly
// breaking out of an opponent's grip once the grip starts - rolled fresh per grip
// session (see NewBotAI_ReactToBeingGripped) rather than re-rolled every think, since a
// per-think chance converges to a near-instant escape within a couple of thinks
// regardless of how high the bias is. The roll is a wide range from a token split-second
// of hesitation up to most of a grip's full duration, so one mistake may barely register
// while the next eats nearly the whole grip.
#define NEWBOTAI_GRIP_MISTAKE_MAX_DELAY_MS 3600
static int NewBotAI_GetGripEscapeDelayMs(bot_state_t *bs)
{
	const float mistakeChance = BotGetGripMistakeBiasChance(bs);
	int maxDelay;
	int roll;

	if (mistakeChance <= 0.0f)
	{
		return 0;
	}

	maxDelay = 250 + (int)((mistakeChance / 100.0f) * (NEWBOTAI_GRIP_MISTAKE_MAX_DELAY_MS - 250));

	//Every level sees both early and late escapes across grips: the base roll is uniform
	//over the full range, but higher-skill bots take the better (smaller) of two draws
	//more and more often, so their escapes skew early without ever losing the chance of
	//a late one. Level 10 returned 0 above (no mistakes at all).
	roll = Q_irand(0, maxDelay);
	if (bs->settings.skill > 5.0f &&
		Q_irand(1, 100) <= (int)((bs->settings.skill - 5.0f) * 20.0f))
	{
		const int secondRoll = Q_irand(0, maxDelay);
		if (secondRoll < roll)
		{
			roll = secondRoll;
		}
	}

	return roll;
}

// Item 4: per-session chance (0-100) that the bot never breaks this grip with a pull or
// push at all - it just kick-struggles and waits the grip out. One wide roll across the
// whole skill range is what makes low-level bots occasionally fail to break free
// entirely while higher levels (and always level 10, via BotGetMistakeBiasChance) still
// escape nearly every grip.
static int NewBotAI_GetGripNeverEscapeChance(bot_state_t *bs)
{
	return (int)(BotGetGripMistakeBiasChance(bs) * 0.45f);
}

// Item 4: per-attempt chance (0-100) that a grip-escape pull is fumbled into a push -
// the bot still fires force power at the gripper (so the attempt shows), it just chose
// the wrong direction and shoves them away instead of pulling free of the grip.
static int NewBotAI_GetGripPushInsteadChance(bot_state_t *bs)
{
	return (int)(BotGetGripMistakeBiasChance(bs) * 0.5f);
}

// Item 4: bot_gkmistakebias weights off gripkicks that achieve high speeds - the faster
// the gripper is moving our bot around mid-grip (hard yaw jerks, throws), the more likely
// the escape fails, so a skillful fast gripkick is genuinely harder to break out of
// than a slow one. Returns a 0-100 chance scaled off how fast we are currently being
// moved (full at ~900 u/s), weighted by the skill-scaled gripkick mistake chance: it
// used to bypass every bias and skill check, which made high-level bots fumble pull-outs
// even with no mistakes configured. Level 10 and bot_gkmistakebias 0 are never rattled.
#define NEWBOTAI_GRIP_SPEED_MISTAKE_MAX 900.0f
static int NewBotAI_GetGripSpeedMistakeChance(bot_state_t *bs)
{
	const float speed = VectorLength(bs->cur_ps.velocity);
	const float gripMistakeChance = BotGetGripMistakeBiasChance(bs);
	float chance;

	if (speed <= 0.0f || gripMistakeChance <= 0.0f)
	{
		return 0;
	}

	chance = (speed / NEWBOTAI_GRIP_SPEED_MISTAKE_MAX) * 100.0f;
	if (chance > 100.0f)
	{
		chance = 100.0f;
	}

	return (int)(chance * (gripMistakeChance / 100.0f));
}

static void NewBotAI_ApplyPullMistakeChance(bot_state_t *bs, float mistakeChance)
{
	float missAngle;

	if (!(g_entities[bs->client].r.svFlags & SVF_BOT))
	{
		return;
	}

	if (mistakeChance <= 0.0f || Q_irand(1, 100) > (int)mistakeChance)
	{
		return;
	}

	missAngle = (float)Q_irand(60, 120);
	if (Q_irand(0, 1))
	{
		missAngle = -missAngle;
	}

	//Keep the pull input intact but deliberately offset the aim so the
	//force pull can miss its target.
	bs->ideal_viewangles[YAW] = AngleNormalize360(bs->ideal_viewangles[YAW] + missAngle);
	bs->goalAngles[YAW] = bs->ideal_viewangles[YAW];
}

static void NewBotAI_ApplyPullMistake(bot_state_t *bs)
{
	NewBotAI_ApplyPullMistakeChance(bs, BotGetMistakeBiasChance(bs));
}

//Escape pulls while gripped are gripkick mistakes (bot_gkmistakebias), not general ones.
static void NewBotAI_ApplyGripEscapePullMistake(bot_state_t *bs)
{
	NewBotAI_ApplyPullMistakeChance(bs, BotGetGripMistakeBiasChance(bs));
}

static int BotGetAggressionWeightedBonus(bot_state_t *bs, float biasPercent, int maxBonus, qboolean aggressiveOnly)
{
	float aggressionBias;
	float normalized;

	if (biasPercent <= 0.0f || maxBonus <= 0)
	{
		return 0;
	}

	aggressionBias = BotGetAggressionBias(bs);
	if (aggressiveOnly)
	{
		if (aggressionBias <= 0.0f)
		{
			return 0;
		}
		normalized = aggressionBias;
	}
	else
	{
		normalized = fabsf(aggressionBias);
	}

	return (int)(normalized * (biasPercent / 100.0f) * (float)maxBonus);
}

static qboolean NewBotAI_HasClearAdvantage(bot_state_t *bs)
{
	int ourHealth;
	int ourForce;
	int hisHealth;
	int hisForce;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	ourHealth = g_entities[bs->client].health;
	ourForce = bs->cur_ps.fd.forcePower;
	hisHealth = bs->currentEnemy->health;
	hisForce = bs->currentEnemy->client->ps.fd.forcePower;

	return ((ourHealth - hisHealth) >= 40 || (ourForce - hisForce) >= 40) ? qtrue : qfalse;
}

static qboolean NewBotAI_ShouldPressAdvantage(bot_state_t *bs)
{
	float aggressionBias;
	int ourHealth;
	int ourForce;
	int hisHealth;
	int hisForce;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	ourHealth = g_entities[bs->client].health;
	ourForce = bs->cur_ps.fd.forcePower;
	hisHealth = bs->currentEnemy->health;
	hisForce = bs->currentEnemy->client->ps.fd.forcePower;

	aggressionBias = BotGetAggressionBias(bs);

	if (aggressionBias <= -0.6f)
	{
		return qfalse;
	}

	if (NewBotAI_IsDrainlockAdvantage(bs))
	{
		return qtrue;
	}

	if (NewBotAI_HasClearAdvantage(bs))
	{
		return qtrue;
	}

	if ((BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim) ||
		bs->currentEnemy->client->ps.groundEntityNum == ENTITYNUM_NONE) &&
		ourForce > hisForce && ourHealth >= hisHealth)
	{
		return qtrue;
	}

	if (bs->currentEnemy->client->ps.saberInFlight &&
		ourHealth > 30 &&
		ourForce >= hisForce)
	{
		return qtrue;
	}

	return (aggressionBias > 0.2f && ourHealth > hisHealth && ourForce > hisForce) ? qtrue : qfalse;
}

static qboolean NewBotAI_HasDroppedOwnSaber(bot_state_t *bs)
{
	gentity_t *saberEnt;
	int saberEntNum;
	int saberKnockedTime;

	if (!bs)
	{
		return qfalse;
	}

	if (!(bs->cur_ps.stats[STAT_WEAPONS] & (1 << WP_SABER)))
	{
		return qfalse;
	}

	if (bs->cur_ps.weapon != WP_SABER && bs->cur_ps.weapon != WP_MELEE)
	{
		return qfalse;
	}

	saberKnockedTime = g_entities[bs->client].client->saberKnockedTime;
	if (saberKnockedTime <= 0)
	{
		return qfalse;
	}
	if (saberKnockedTime > 0 && saberKnockedTime >= level.time)
	{
		return qfalse;
	}

	if (!bs->cur_ps.saberInFlight || bs->cur_ps.saberEntityNum)
	{
		return qfalse;
	}

	saberEntNum = g_entities[bs->client].client->ps.saberEntityNum;
	if (saberEntNum <= 0 || saberEntNum >= ENTITYNUM_WORLD)
	{
		saberEntNum = g_entities[bs->client].client->saberStoredIndex;
	}
	if (saberEntNum <= 0 || saberEntNum >= ENTITYNUM_WORLD)
	{
		return qfalse;
	}

	saberEnt = &g_entities[saberEntNum];
	if (!saberEnt->inuse)
	{
		return qfalse;
	}
	if (saberEnt->think != DownedSaberThink)
	{
		return qfalse;
	}
	if (saberEnt->r.contents != CONTENTS_TRIGGER)
	{
		return qfalse;
	}
	if (saberEnt->r.ownerNum != bs->client && saberEnt->parent != &g_entities[bs->client])
	{
		return qfalse;
	}

	if (saberEnt->s.eType != ET_MISSILE)
	{
		return qfalse;
	}

	//Dropped saber physics can include a brief interpolate phase while still in
	//DownedSaberThink, so keep all known downed movement states eligible.
	return (saberEnt->s.pos.trType == TR_GRAVITY ||
		saberEnt->s.pos.trType == TR_STATIONARY ||
		saberEnt->s.pos.trType == TR_INTERPOLATE) ? qtrue : qfalse;
}

static int BotGetDrainHoldBiasMs(bot_state_t *bs)
{
	float biasPercent;
	float skillScale;

	if (!bs)
	{
		return 0;
	}

	biasPercent = BotGetChanceBiasPercent(bot_drainbias.value);
	if (biasPercent <= 0.0f)
	{
		return 0;
	}

	skillScale = 6.0f - bs->settings.skill;
	if (skillScale < 0.0f)
	{
		skillScale = 0.0f;
	}

	return (int)(skillScale * (biasPercent / 100.0f) * 100.0f);
}

static int BotGetHealthBiasThreshold(void)
{
	float raw = bot_healthbias.value;
	int threshold;

	//Allow direct HP thresholds (1-100) while remaining backward-compatible with the
	//legacy -1..1 bias weighting by mapping that range to practical health breakpoints.
	if (raw > 1.0f && raw <= 100.0f)
	{
		threshold = (int)raw;
	}
	else
	{
		const float biasWeight = BotGetBiasWeight(raw);
		threshold = 60 - (int)(biasWeight * 25.0f);
	}

	if (threshold < 20)
	{
		threshold = 20;
	}
	else if (threshold > 95)
	{
		threshold = 95;
	}

	return threshold;
}

static float BotGetAggressionBias(bot_state_t *bs)
{
	float aggressionBias = bot_aggressionbias.value;
	float healthBias;
	float forceBias;
	float healthComponent;
	float forceComponent;

	if (aggressionBias < -1.0f)
	{
		aggressionBias = -1.0f;
	}
	else if (aggressionBias > 1.0f)
	{
		aggressionBias = 1.0f;
	}

	if (!bs)
	{
		return aggressionBias;
	}

	healthBias = BotGetBiasWeight(bot_healthbias.value);
	forceBias = BotGetBiasWeight(bot_forcebias.value);

	healthComponent = ((float)g_entities[bs->client].health - 50.0f) / 50.0f;
	if (healthComponent < -1.0f)
	{
		healthComponent = -1.0f;
	}
	else if (healthComponent > 1.0f)
	{
		healthComponent = 1.0f;
	}

	if (bs->currentEnemy && bs->currentEnemy->client)
	{
		forceComponent = ((float)bs->cur_ps.fd.forcePower - (float)bs->currentEnemy->client->ps.fd.forcePower) / 100.0f;
	}
	else
	{
		forceComponent = ((float)bs->cur_ps.fd.forcePower - 50.0f) / 50.0f;
	}

	if (forceComponent < -1.0f)
	{
		forceComponent = -1.0f;
	}
	else if (forceComponent > 1.0f)
	{
		forceComponent = 1.0f;
	}

	aggressionBias += (healthComponent * healthBias);
	aggressionBias += (forceComponent * forceBias);

	//Per-bot personality nudge derived from the .jkb "hatelevel" value (see
	//B_LoadPersonality in ai_util.c). This is additive on top of the server-wide
	//bot_aggressionbias cvar so individual bots can have distinct personalities.
	aggressionBias += bs->hateLevelAggressionBias;

	if (aggressionBias < -1.0f)
	{
		aggressionBias = -1.0f;
	}
	else if (aggressionBias > 1.0f)
	{
		aggressionBias = 1.0f;
	}

	return aggressionBias;
}

static int NewBotAI_GetAntiDrainWeight(bot_state_t *bs)
{
	const float antiDrainBias = BotGetChanceBiasPercent(bot_antidrainbias.value);
	const int totalHealthDelta = NewBotAI_GetTotalHealthDelta(bs);
	const int enemyHealth = bs->currentEnemy->health;
	const int enemyForce = bs->currentEnemy->client->ps.fd.forcePower;
	const int ourForce = bs->cur_ps.fd.forcePower;
	const int forceDisadvantage = enemyForce - ourForce;
	const qboolean lowForceAntiDrain = (ourForce <= 25 && enemyForce >= 20) ? qtrue : qfalse;
	const qboolean enemyCanDrain = ((bs->currentEnemy->client->ps.fd.forcePowersKnown & (1 << FP_DRAIN)) ||
		(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN))) ? qtrue : qfalse;
	int weight;

	if (antiDrainBias <= 0.0f || !enemyCanDrain || bs->cur_ps.weapon != WP_SABER)
	{
		return 0;
	}

	//Antidrain is for situations where we can spend force to deny enemy healing while
	//we still hold a sizeable health edge despite losing the force economy.
	if (!lowForceAntiDrain &&
		(forceDisadvantage < 20 || totalHealthDelta < 30 || ourForce < 20))
	{
		return 0;
	}

	weight = (int)((antiDrainBias / 100.0f) * 45.0f);
	if (enemyHealth <= 30)
	{
		weight += 20;
	}
	if (ourForce <= 40)
	{
		weight += 10;
	}
	if (NewBotAI_IsWithinLightningRange(bs))
	{
		weight += 30;
	}

	if (weight < 0)
	{
		weight = 0;
	}
	else if (weight > 100)
	{
		weight = 100;
	}

	return weight;
}

static float BotGetLightningMaxDistance(bot_state_t *bs)
{
	if (!bs)
	{
		return 2048.0f;
	}

	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_LIGHTNING)) ||
		bs->cur_ps.fd.forcePowerLevel[FP_LIGHTNING] <= FORCE_LEVEL_0)
	{
		return 0.0f;
	}

	return 2048.0f;
}

static float BotGetLightningStartDistance(bot_state_t *bs)
{
	float startDistance = bot_lightningdistance.value;
	const float maxDistance = BotGetLightningMaxDistance(bs);

	if (startDistance < 0.0f)
	{
		startDistance = 0.0f;
	}
	if (startDistance > maxDistance)
	{
		startDistance = maxDistance;
	}

	return startDistance;
}

static qboolean NewBotAI_IsWithinLightningRange(bot_state_t *bs)
{
	const float maxDistance = BotGetLightningMaxDistance(bs);

	if (!bs)
	{
		return qfalse;
	}
	if (maxDistance <= 0.0f)
	{
		return qfalse;
	}

	return (bs->frame_Enemy_Len >= BotGetLightningStartDistance(bs) &&
		bs->frame_Enemy_Len <= maxDistance) ? qtrue : qfalse;
}

static int NewBotAI_GetLightningWeight(bot_state_t *bs)
{
	float lightningBias;
	float aggressionBias;
	float startDistance;
	float distanceFactor;
	float defensiveFactor;
	int weight;
	const float maxDistance = BotGetLightningMaxDistance(bs);
	vec3_t a_fo;

	if (g_forcePowerDisable.integer & (1 << FP_LIGHTNING))
	{
		return 0;
	}
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_LIGHTNING)))
	{
		return 0;
	}
	if (bs->cur_ps.fd.forcePowerLevel[FP_LIGHTNING] <= FORCE_LEVEL_0)
	{
		return 0;
	}
	if (!bs->frame_Enemy_Vis || bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
	{
		return 0;
	}
	if (bs->cur_ps.fd.forcePower <= 40)
	{
		return 0;
	}

	lightningBias = BotGetChanceBiasPercent(bot_lightningbias.value);
	if (lightningBias <= 0.0f)
	{
		return 0;
	}
	if (bs->currentEnemy && bs->currentEnemy->health > 0 && bs->currentEnemy->health < 9 &&
		bs->currentEnemy->client->ps.stats[STAT_ARMOR] <= 0)
	{
		return 100;
	}

	aggressionBias = BotGetAggressionBias(bs);

	startDistance = BotGetLightningStartDistance(bs);
	if (!NewBotAI_IsWithinLightningRange(bs))
	{
		return 0;
	}

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
	vectoangles(a_fo, a_fo);
	if (!InFieldOfVision(bs->viewangles, 50, a_fo))
	{
		return 0;
	}

	if (maxDistance <= startDistance)
	{
		distanceFactor = 1.0f;
	}
	else
	{
		distanceFactor = (bs->frame_Enemy_Len - startDistance) / (maxDistance - startDistance);
		if (distanceFactor < 0.0f)
		{
			distanceFactor = 0.0f;
		}
		else if (distanceFactor > 1.0f)
		{
			distanceFactor = 1.0f;
		}
	}

	//Item 11/12: the overall weight of lightningbias is increased immensely so it comes
	//out frequently once we are past bot_lightningdistance - aggression no longer gates
	//it off, and a defensive lean only makes it stronger. The scalar is raised again
	//(255 -> 400) since bots were still rarely winning the force-power comparison
	//against pull/drain/grip even with a bias set.
	defensiveFactor = 1.0f;
	if (aggressionBias < 0.0f)
	{
		defensiveFactor += -aggressionBias;
	}
	weight = (int)(defensiveFactor * distanceFactor * (lightningBias / 100.0f) * 400.0f);

	//Antidrain at long range should primarily channel into lightning pressure.
	if (NewBotAI_IsWithinLightningRange(bs))
	{
		weight += NewBotAI_GetAntiDrainWeight(bs) * 2;
	}
	if (weight > 100)
	{
		weight = 100;
	}

	return weight;
}

static qboolean NewBotAI_ShouldDisengageLongRangeLightningTrade(bot_state_t *bs)
{
	int ourHealth;
	int enemyHealth;
	float aggressionBias;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}
	ourHealth = g_entities[bs->client].health;
	enemyHealth = bs->currentEnemy->health;
	aggressionBias = BotGetAggressionBias(bs);
	if (!bs->frame_Enemy_Vis || !NewBotAI_IsWithinLightningRange(bs))
	{
		return qfalse;
	}
	if (bs->frame_Enemy_Len <= BotGetLightningStartDistance(bs))
	{
		return qfalse;
	}
	if (!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_LIGHTNING)) ||
		bs->cur_ps.electrifyTime < level.time)
	{
		return qfalse;
	}

	return (ourHealth < enemyHealth || aggressionBias <= 0.0f) ? qtrue : qfalse;
}

static int NewBotAI_GetSpeedAttackWeight(bot_state_t *bs)
{
	float speedBias;
	float aggressionBias;
	int ourHealth;
	int enemyHealth;
	int enemyArmor;
	int ourForce;
	int enemyForce;
	int forceLead;
	int weight;
	qboolean beingGripped;
	qboolean enemyGripActive;
	qboolean enemyDrainActive;
	qboolean speedFinisherWindow;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return 0;
	}
	beingGripped = (bs->cur_ps.fd.forceGripBeingGripped > level.time) ? qtrue : qfalse;
	enemyGripActive = (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_GRIP)) ? qtrue : qfalse;
	enemyDrainActive = (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN)) ? qtrue : qfalse;

	speedBias = BotGetChanceBiasPercent(bot_speedbias.value);
	aggressionBias = BotGetAggressionBias(bs);
	ourHealth = g_entities[bs->client].health;
	enemyHealth = bs->currentEnemy->health;
	enemyArmor = bs->currentEnemy->client->ps.stats[STAT_ARMOR];
	ourForce = bs->cur_ps.fd.forcePower;
	enemyForce = bs->currentEnemy->client->ps.fd.forcePower;
	forceLead = ourForce - enemyForce;
	speedFinisherWindow = NewBotAI_IsSpeedFinisherWindow(enemyHealth, enemyArmor) ? qtrue : qfalse;

	if (speedBias <= 0.0f)
	{
		return 0;
	}
	if (g_forcePowerDisable.integer & (1 << FP_SPEED))
	{
		return 0;
	}
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_SPEED)))
	{
		return 0;
	}
	if (!speedFinisherWindow)
	{
		return 0;
	}
	if (!NewBotAI_PassesSpeedAttackResourceLeadGate(
		ourHealth,
		bs->cur_ps.stats[STAT_ARMOR],
		enemyHealth,
		enemyArmor,
		aggressionBias,
		ourForce,
		enemyForce))
	{
		return 0;
	}
	if (ourForce < 70 || !bs->frame_Enemy_Vis || bs->frame_Enemy_Len < 96.0f || bs->frame_Enemy_Len > 640.0f)
	{
		return 0;
	}
	if (NewBotAI_ShouldAbortSpeedAttack(
		beingGripped ? 1 : 0,
		enemyGripActive ? 1 : 0,
		enemyDrainActive ? 1 : 0,
		ourForce,
		forceLead,
		bs->frame_Enemy_Len,
		MAX_DRAIN_DISTANCE))
	{
		return 0;
	}

	weight = (int)((speedBias / 100.0f) * 70.0f);
	weight += NewBotAI_GetAntiDrainWeight(bs) / 2;
	if (NewBotAI_GetPTKWeight(bs) > 0)
	{
		weight += 20;
	}
	if (ourForce >= 90)
	{
		weight += 15;
	}
	if (weight > 100)
	{
		weight = 100;
	}
	return weight;
}

static int NewBotAI_GetPTKWeight(bot_state_t *bs)
{
	const int ourHealth = g_entities[bs->client].health;
	const int ourForce = bs->cur_ps.fd.forcePower;
	const int hisHealth = bs->currentEnemy->health;
	const int hisForce = bs->currentEnemy->client->ps.fd.forcePower;
	const int enemyArmor = bs->currentEnemy->client->ps.stats[STAT_ARMOR];
	const int forceLead = ourForce - hisForce;
	const qboolean freePullkickWindow = NewBotAI_HasFreePullkickWindow(bs);
	const int fpDifference = ourForce - hisForce;
	const int hpDifference = ourHealth - hisHealth;
	const int aggressionWeight = BotGetChanceBiasPercent(bot_ptk_aggressionbias.value);
	const qboolean enemySwinging = (BG_SaberInAttack(bs->currentEnemy->client->ps.saberMove) ||
		PM_SaberInStart(bs->currentEnemy->client->ps.saberMove) ||
		PM_SaberInTransition(bs->currentEnemy->client->ps.saberMove)) ? qtrue : qfalse;
	const qboolean stabilizeVsSaberThrow = NewBotAI_ShouldStabilizeAgainstEnemySaberThrow(bs);
	int weight = 0;

	if (!NewBotAI_IsEnemyPullable(bs) || !g_flipKick.integer)
	{
		return 0;
	}
	if (g_entities[bs->client].health < 51 &&
		(bs->currentEnemy->client->ps.saberInFlight || NewBotAI_IsEnemySaberThreatImminent(bs)))
	{
		return 0;
	}
	if (NewBotAI_IsStandingRoll(bs->cur_ps.legsAnim) && !NewBotAI_ShouldEmergencyDrainRollSaberThrow(bs))
	{
		return 0;
	}

	if (bs->cur_ps.weapon != WP_SABER && bs->cur_ps.weapon != WP_MELEE)
	{
		return 0;
	}

	//PTK spends about 40 FP (20 pull + 20 throw). Below 26 FP it is too far away to plan
	//for; from 26-39 FP we still weight the setup so movement/force-conservation can bank
	//up for the combo before spending.
	if (ourForce <= 25)
	{
		return 0;
	}

	if (!NewBotAI_IsPullkickOpportunity(bs))
	{
		return 0;
	}
	if (bs->currentEnemy->client->ps.saberInFlight && stabilizeVsSaberThrow)
	{
		return 0;
	}
	if (!NewBotAI_IsStablePTKCommitWindow(bs) && hisHealth > 30)
	{
		return 0;
	}

	//Item 6: a knocked-down opponent is the prime PTK window (they cannot defend the pull
	//or the kick while getting up), so weight it heavily.
	if (BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim))
	{
		weight += 80;
	}

	if (enemySwinging && bs->frame_Enemy_Len < 256)
	{
		weight += 45;
	}

	if (hisForce < 20)
	{
		//Low-force opponents are especially vulnerable to quick drain taps and
		//pullkick follow-up chains, so PTK gets an extra nudge when their FP is nearly spent.
		weight += (int)(aggressionWeight * 0.6f) + 20;
	}

	if (hisHealth < 45)
	{
		//Low-health targets are prime PTK finishers: add a strong bias when the enemy is
		//close to dropping so the bot chases the finish with a pullkick sequence.
		weight += 35;
	}
	else if (hisHealth < 70)
	{
		weight += 15;
	}

	weight = NewBotAI_AdjustPTKWeightForArmor(weight, enemyArmor, forceLead);

	if (ourHealth > 70)
	{
		const float fanBias = BotGetChanceBiasPercent(bot_fanbias.value);

		if (ourForce < hisForce)
		{
			//When the bot is healthy but behind in force, a fan-heavy approach keeps them
			//pressuring the enemy without overcommitting to a drain or a desperate chase.
			weight += (int)(fanBias * 0.7f) + 15;
		}
		else if (ourForce > hisForce)
		{
			//A healthy force edge is the ideal moment to commit to the pullkick follow-up
			//instead of chancing a low-value exchange.
			weight += (int)(fanBias * 0.4f) + 10;
		}
	}

	if (bs->cur_ps.weapon == WP_SABER && BG_SaberInAttack(bs->cur_ps.saberMove) && bs->frame_Enemy_Len < 200)
	{
		//Immediately follow a successful saber exchange with more PTK intent so the
		//bot can chain into a pullkick while the target is still staggered or exposed.
		weight += 30;
	}

	if (bs->currentEnemy->client->ps.saberInFlight)
	{
		const qboolean enemySaberReturning = NewBotAI_IsEnemySaberReturning(bs);

		//The enemy has already committed their saber to a throw - punish the opening
		//with PTK only once a real drainlock/free-pullkick window already exists.
		//Before that, keep the weight modest so drain/retreat/flipkick pressure can
		//set the force advantage first instead of yanking straight into an early pull.
		weight += NewBotAI_GetSaberThrowPTKBonus(
			freePullkickWindow ? 1 : 0,
			enemySaberReturning ? 1 : 0,
			ourHealth,
			ourForce,
			hisForce);
	}

	if (NewBotAI_IsBeingPulledTowardEnemy(bs) &&
		bs->currentEnemy->client->ps.weapon == WP_SABER)
	{
		weight += NewBotAI_GetPulledTowardEnemyPTKBonus(
			freePullkickWindow ? 1 : 0,
			bs->frame_Enemy_Len);
	}

	//Pressed forward past our own thrown saber and now the closer, saberless one -
	//lean on the pullkick/PTK combo, and especially on antidrain if the enemy still
	//has force to drain us with while we're exposed.
	if (NewBotAI_IsBetweenOwnSaberAndEnemy(bs))
	{
		weight += 25;
		if (bs->currentEnemy->client->ps.fd.forcePower > 20)
		{
			weight += NewBotAI_GetAntiDrainWeight(bs);
		}
	}

	if (fpDifference >= bot_ptk_fpdifference.integer)
	{
		weight += (int)(aggressionWeight * 0.4f);
	}

	if (hpDifference >= bot_ptk_hpdifference.integer)
	{
		weight += (int)(aggressionWeight * 0.35f);
	}

	//Conversion discipline: don't keep forcing PTK plans when we are low on reserve and
	//not materially ahead in force. This reduces panic spend loops that end in low-force deaths.
	if (ourForce < 35 && hisForce >= ourForce)
	{
		weight -= 20;
	}
	if (ourHealth < 45 && ourForce < 45)
	{
		weight -= 15;
	}
	if (bs->cur_ps.fd.forcePowerSelected == FP_PULL &&
		bs->lastGripkickSuccessTime < level.time - 3500)
	{
		weight -= 10;
	}
	if (bs->cur_ps.fd.forcePowerSelected == FP_PULL &&
		bs->lastGripkickSuccessTime < level.time - 5000 &&
		NewBotAI_IsEnemyCollapsePressure(bs))
	{
		/* Anti-counter adaptation: stop re-forcing stale pull entries into fast punish lanes. */
		weight -= 25;
	}

	weight += BotGetAggressionWeightedBonus(bs, aggressionWeight, 35, qtrue);
	weight += NewBotAI_GetAntiDrainWeight(bs);
	if (bs->lastGripkickSuccessTime > level.time - 3000)
	{
		weight += 25;
	}

	if (weight < 0)
	{
		weight = 0;
	}

	if (bs->currentEnemy->client->ps.saberInFlight &&
		!NewBotAI_IsEnemySaberReturning(bs) &&
		!freePullkickWindow &&
		weight > 55)
	{
		weight = 55;
	}

	return weight;
}

static qboolean NewBotAI_IsSaberSwingStartWindow(bot_state_t *bs)
{
	if (!bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (bs->cur_ps.weapon != WP_SABER)
	{
		return qfalse;
	}

	if (!bs->frame_Enemy_Vis || bs->frame_Enemy_Len > 256 || bs->hitSpotted)
	{
		return qfalse;
	}

	return qtrue;
}

// Fan-chain phases (see NewBotAI_PrepareHorizontalSwingStart): HOLD owns exclusive
// left/right strafe plus attack long enough to start the horizontal swing, then DWELL
// frees movement before the next alternating hold. Skill 7+ skips DWELL and links the next
// swing on the frame the current one ends. The chain ends after 3 seconds unless it is
// still landing hits (NewBotAI_FanChainMayContinue), or when the bot drops below 70 HP.
#define NEWBOTAI_FAN_CHAIN_MIN_HEALTH 70
#define NEWBOTAI_FAN_LINK_LEAD_MS 60

// Predicted 2D footing for a swing that would start this frame (see NewBotAI_GetSwingFooting).
// 2D range to the enemy now, predicted at the swing peak, and the closing speed (>0 closing).
static qboolean NewBotAI_GetEnemyRangeKinematics(bot_state_t *bs, float *range, float *predicted, float *radial)
{
	vec3_t diff;
	float relVx, relVy;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->cur_ps.origin, diff);
	diff[2] = 0.0f;
	*range = VectorLength(diff);
	relVx = bs->currentEnemy->client->ps.velocity[0] - bs->cur_ps.velocity[0];
	relVy = bs->currentEnemy->client->ps.velocity[1] - bs->cur_ps.velocity[1];
	*predicted = NewBotAI_PredictRange2D(diff[0], diff[1], relVx, relVy, NEWBOTAI_SWING_PEAK_LEAD_MS);
	*radial = 0.0f;
	if (*range > 1.0f)
	{
		//Positive when the gap is shrinking.
		*radial = -((relVx * diff[0] + relVy * diff[1]) / *range);
	}
	return qtrue;
}

static newbotai_swing_footing_t NewBotAI_GetCurrentSwingFooting(bot_state_t *bs, qboolean linkedSwing)
{
	float range, predicted, radial;
	newbotai_swing_footing_t footing;

	if (!NewBotAI_GetEnemyRangeKinematics(bs, &range, &predicted, &radial))
	{
		bs->swingFootingInReach = qfalse;
		return NEWBOTAI_SWING_FOOTING_HOLD;
	}

	predicted = NewBotAI_SaberRangeWithHysteresis(predicted, NEWBOTAI_SABER_STEP_IN_SWING_RANGE,
		NEWBOTAI_SABER_RANGE_HYSTERESIS, bs->swingFootingInReach);
	footing = NewBotAI_GetSwingFooting(range, predicted, radial, linkedSwing ? 1 : 0,
		(bs->cur_ps.fd.saberAnimLevel == SS_STAFF) ? 1 : 0);
	bs->swingFootingInReach = (predicted <= NEWBOTAI_SABER_STEP_IN_SWING_RANGE) ? qtrue : qfalse;
	bs->swingFootingCommitForward = (footing == NEWBOTAI_SWING_FOOTING_START &&
		predicted > NEWBOTAI_SABER_COMMIT_PEAK_RANGE) ? qtrue : qfalse;
	return footing;
}

// Per-decision skill mistake chance (percent): full band for skills 1-6, a small roll that
// reaches 0 at skill 10 for 7+.
static int NewBotAI_GetDecisionMistakeChance(bot_state_t *bs)
{
	return NewBotAI_GetSaberTacticMistakeChance((int)bs->settings.skill,
		(int)BotGetChanceBiasPercent(bot_mistakebias.value));
}

// Gate for starting a NEW saber swing (chain holds use NewBotAI_SwingChainFootingAllows).
// Humans that landed swings started them when the range predicted at the swing peak was
// 70u or less (or 70-100u while closing faster than ~150u/s), stepped in first when further
// (always beyond ~130u), and never swung while backing off from 100u+.
// Returns qtrue when attack may be pressed; on STEP_IN it walks forward instead.
static qboolean NewBotAI_SwingStartFootingAllows(bot_state_t *bs)
{
	newbotai_swing_footing_t footing;
	qboolean enemyDown;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
		return qfalse;

	enemyDown = BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim) ? qtrue : qfalse;

	if (level.time - bs->footingMistakeRollTime > 500)
	{
		//Lower levels keep the observed mistakes: long-range and backpedal swings.
		bs->footingMistakeRollTime = level.time;
		bs->footingMistake = (bs->settings.skill < NEWBOTAI_SABER_BACKPEDAL_SWING_MAX_SKILL &&
			Q_irand(1, 100) <= NewBotAI_GetDecisionMistakeChance(bs) / 2) ? qtrue : qfalse;
	}

	//Pull -> swing only lands when the pull already closed the gap (humans: +7.3 net,
	//68% win when closing); otherwise follow a pull with a kick or a throw instead.
	if (!enemyDown && !bs->footingMistake &&
		G_BotLearnRecentToken(bs->client, BOTLEARN_TOK_PULL, 600))
	{
		float range, predicted, radial;

		if (!NewBotAI_GetEnemyRangeKinematics(bs, &range, &predicted, &radial) ||
			radial < NEWBOTAI_SWING_BACKING_SPEED)
			return qfalse;
	}

	footing = NewBotAI_GetCurrentSwingFooting(bs, qfalse);
	if (footing == NEWBOTAI_SWING_FOOTING_START || bs->footingMistake)
	{
		//Commit with the swing: keep stepping in until the peak lands at ~40-55u.
		if (footing == NEWBOTAI_SWING_FOOTING_START && bs->swingFootingCommitForward)
			trap->EA_MoveForward(bs->client);
		return qtrue;
	}
	if (footing == NEWBOTAI_SWING_FOOTING_STEP_IN)
		trap->EA_MoveForward(bs->client);
	return qfalse;
}

// Gate for holding attack through a swing so the engine chains the next one. Keeps walking
// forward while the peak is still out of reach and drops the chain when backing off.
static qboolean NewBotAI_SwingChainFootingAllows(bot_state_t *bs)
{
	const newbotai_swing_footing_t footing = NewBotAI_GetCurrentSwingFooting(bs, qtrue);

	if (footing == NEWBOTAI_SWING_FOOTING_HOLD && !bs->footingMistake)
		return qfalse;
	if (footing == NEWBOTAI_SWING_FOOTING_STEP_IN ||
		(footing == NEWBOTAI_SWING_FOOTING_START && bs->swingFootingCommitForward))
		trap->EA_MoveForward(bs->client);
	return qtrue;
}

static float NewBotAI_GetEnemyDistance2D(bot_state_t *bs)
{
	vec3_t diff;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return 0.0f;
	}
	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->cur_ps.origin, diff);
	diff[2] = 0.0f;
	return VectorLength(diff);
}

static int NewBotAI_GetMsSinceSaberContactOnEnemy(bot_state_t *bs)
{
	if (!bs || !bs->currentEnemy || bs->lastSaberContactTime <= 0 ||
		bs->lastSaberContactTargetNum != bs->currentEnemy->s.number)
	{
		return -1;
	}
	return level.time - bs->lastSaberContactTime;
}

static void NewBotAI_ResetFanChain(bot_state_t *bs)
{
	bs->fanPhase = FAN_PHASE_INACTIVE;
	bs->fanPackage = FAN_PACKAGE_NONE;
	bs->fanAttackDir = 0;
	bs->fanAttackTime = 0;
	bs->fanPhaseStartTime = 0;
	bs->fanChainStartTime = 0;
	bs->fanChainStartHealth = 0;
	bs->fanSwingCount = 0;
	bs->fanSwingStarted = 0;
	bs->fanDwellYawOffset = 0.0f;
	bs->fanWobbleStartTime = 0;
	bs->fanLinkMove = 0;
}

static int NewBotAI_GetFanPackage(bot_state_t *bs)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return FAN_PACKAGE_NONE;
	}

	if (g_entities[bs->client].client->ps.fd.saberAnimLevel == SS_STAFF)
	{
		return FAN_PACKAGE_STAFF_PRESSURE;
	}

	return FAN_PACKAGE_YELLOW_PRESSURE;
}

static const char *NewBotAI_GetFanPackageName(int packageId)
{
	switch (packageId)
	{
	case FAN_PACKAGE_YELLOW_PRESSURE:
		return "yellow_pressure";
	case FAN_PACKAGE_STAFF_PRESSURE:
		return "staff_pressure";
	default:
		break;
	}

	return "none";
}

//Fan pressure should be strongest while healthy and pressing an advantage; outside of
//that (neutral/defensive, recently hurt, or low health), scale it down so bots don't
//overcommit into vulnerable saber exchanges.
static float NewBotAI_GetFanBiasPercent(bot_state_t *bs)
{
	float fanBias = BotGetChanceBiasPercent(bot_fanbias.value);
	const int ourHealth = g_entities[bs->client].health;
	const qboolean pressingAdvantage = NewBotAI_ShouldPressAdvantage(bs);
	const qboolean clearAdvantage = NewBotAI_HasClearAdvantage(bs);
	const qboolean recentlyHurt = (bs->lastHurtTime > level.time - 1200) ? qtrue : qfalse;

	if (fanBias <= 0.0f)
	{
		return 0.0f;
	}

	if (NewBotAI_IsSaberOnlyDuel(bs))
	{
		//Saber-only duels have no force fallback, so fanning stays available at medium
		//health and is scaled by the HP+armor difference instead of switched off.
		return NewBotAI_ScaleSaberDuelFanBias(fanBias, NewBotAI_GetTotalHealthDelta(bs));
	}

	if (ourHealth < 55)
	{
		return 0.0f;
	}
	else if (ourHealth < 70)
	{
		fanBias *= 0.35f;
	}
	else if (!pressingAdvantage)
	{
		fanBias *= clearAdvantage ? 0.75f : 0.55f;
	}

	if (recentlyHurt)
	{
		fanBias *= 0.6f;
	}
	if (bs->currentEnemy->client->ps.saberInFlight || bs->frame_Enemy_Len > 224.0f)
	{
		fanBias *= 0.35f;
	}
	if (NewBotAI_ShouldPreDefenseAgainstCollapse(bs))
	{
		fanBias *= 0.2f;
	}

	if (pressingAdvantage && ourHealth >= 85)
	{
		if (fanBias < 80.0f)
		{
			fanBias = 80.0f;
		}
	}
	else if (clearAdvantage && ourHealth >= 75)
	{
		if (fanBias < 65.0f)
		{
			fanBias = 65.0f;
		}
	}

	if (fanBias > 0.0f && NewBotAI_IsDrainlockAdvantage(bs) && pressingAdvantage &&
		ourHealth >= 80 && fanBias < 70.0f)
	{
		fanBias = 70.0f;
	}

	if (fanBias < 0.0f)
	{
		fanBias = 0.0f;
	}

	return fanBias;
}

// Item 2: fan/fanning is a left-right (or right-left) alternating horizontal swing chain.
// Each cycle is: HOLD (bot_fanhold ms, exclusive strafe+attack in one lateral direction)
// -> DWELL (bot_firstfandwell once after the first swing, then bot_fandwell thereafter,
// free movement) -> next HOLD in the opposite direction.
static void NewBotAI_PrepareHorizontalSwingStart(bot_state_t *bs)
{
	const float fanBias = NewBotAI_GetFanBiasPercent(bs);
	const int holdMs = Com_Clampi(10, 3000, bot_fanhold.integer);
	//Human fan chains link with no dwell; the configured dwell now shrinks with skill and
	//fan bias and is zero at skill 7+.
	const int firstDwellMs = NewBotAI_GetFanDwellMs(Com_Clampi(10, 3000, bot_firstfandwell.integer),
		bs->settings.skill, fanBias);
	const int dwellMs = NewBotAI_GetFanDwellMs(Com_Clampi(10, 3000, bot_fandwell.integer),
		bs->settings.skill, fanBias);

	if (bs->saberTechniqueCandidate)
	{
		NewBotAI_ResetFanChain(bs);
		return;
	}
	if (!NewBotAI_IsSaberSwingStartWindow(bs))
	{
		NewBotAI_ResetFanChain(bs);
		return;
	}
	if (bs->fanPhase == FAN_PHASE_INACTIVE && !NewBotAI_HasTimedFanEntryWindow(bs))
	{
		NewBotAI_ResetFanChain(bs);
		return;
	}
	if (bs->fanPhase != FAN_PHASE_INACTIVE &&
		(NewBotAI_ShouldStabilizeAgainstEnemySaberThrow(bs) ||
		 NewBotAI_IsEnemySaberThreatImminent(bs) ||
		 NewBotAI_ShouldPreDefenseAgainstCollapse(bs)))
	{
		NewBotAI_ResetFanChain(bs);
		return;
	}

	if (bs->fanPhase != FAN_PHASE_INACTIVE &&
		g_entities[bs->client].health < NEWBOTAI_FAN_CHAIN_MIN_HEALTH)
	{
		//Item 13: the chain only breaks early once the bot drops below 70 HP - chip damage
		//at high health no longer interrupts the held attack/swing input.
		NewBotAI_ResetFanChain(bs);
		return;
	}

	if (bs->fanPhase != FAN_PHASE_INACTIVE &&
		!NewBotAI_FanChainMayContinue(level.time - bs->fanChainStartTime,
			NewBotAI_GetMsSinceSaberContactOnEnemy(bs)))
	{
		NewBotAI_ResetFanChain(bs);
		return;
	}

	switch (bs->fanPhase)
	{
	case FAN_PHASE_HOLD:
	{
		const int currentMove = bs->cur_ps.saberMove;
		const qboolean inHorizontalSwingWindow =
			(BG_SaberInAttack(currentMove) ||
			currentMove == LS_A_L2R ||
			currentMove == LS_A_R2L) ? qtrue : qfalse;
		const int msSinceHit = NewBotAI_GetMsSinceSaberContactOnEnemy(bs);
		//Still landing hits: link straight into the next swing and keep driving in.
		const int nextDwellMs = NewBotAI_GetFanLinkDwellMs(
			(bs->fanSwingCount <= 0) ? firstDwellMs : dwellMs, msSinceHit);
		qboolean linkNow;

		//A swing only counts as "started" for this HOLD once it is a new move - not the
		//tail of the swing we already linked out of.
		if (inHorizontalSwingWindow && currentMove != bs->fanLinkMove)
		{
			bs->fanSwingStarted = 1;
			bs->fanLinkMove = 0;
		}
		else if (!inHorizontalSwingWindow)
		{
			bs->fanLinkMove = 0;
		}
		//The engine picks the next chained move from the movement held as the current
		//swing runs out, so a no-dwell link flips the strafe on the last frame of the swing.
		linkNow = (nextDwellMs <= 0 && bs->fanSwingStarted && inHorizontalSwingWindow &&
			bs->cur_ps.weaponTime > 0 && bs->cur_ps.weaponTime <= NEWBOTAI_FAN_LINK_LEAD_MS) ? qtrue : qfalse;

		if (bs->fanSwingStarted && (!inHorizontalSwingWindow || linkNow))
		{
			//Weight the next link by jundon's transitions (R2L->L2R 101, L2R->R2L 67)
			//unless the chain is still landing hits.
			if (!NewBotAI_FanChainIsLanding(msSinceHit) &&
				Q_irand(1, 100) > NewBotAI_FanLinkChance(bs->fanAttackDir))
			{
				NewBotAI_ResetFanChain(bs);
				break;
			}
			bs->fanSwingCount++;
			bs->fanSwingStarted = 0;
			if (nextDwellMs <= 0)
			{
				//Link on the swing-change frame: flip the strafe together with the swing
				//while attack stays held, like the L2R/R2L chains in sessions 34-48.
				bs->fanAttackDir = -bs->fanAttackDir;
				bs->fanPhase = FAN_PHASE_HOLD;
				bs->fanPhaseStartTime = level.time;
				bs->fanAttackTime = level.time + holdMs;
				bs->fanLinkMove = inHorizontalSwingWindow ? currentMove : 0;
			}
			else
			{
				bs->fanPhase = FAN_PHASE_DWELL;
				bs->fanPhaseStartTime = level.time;
				bs->fanAttackTime = level.time + nextDwellMs;
			}
		}
		else if (bs->fanSwingStarted)
		{
			if (level.time > bs->fanPhaseStartTime + holdMs + 1000)
			{
				NewBotAI_ResetFanChain(bs);
			}
		}
		else
		{
			const newbotai_swing_footing_t footing =
				NewBotAI_GetCurrentSwingFooting(bs, (bs->fanSwingCount > 0) ? qtrue : qfalse);

			if (footing == NEWBOTAI_SWING_FOOTING_HOLD && !NewBotAI_FanChainIsLanding(msSinceHit))
			{
				//Never fan while backing off from 100u+ - those swings never landed.
				NewBotAI_ResetFanChain(bs);
			}
			else if ((footing == NEWBOTAI_SWING_FOOTING_STEP_IN ||
				footing == NEWBOTAI_SWING_FOOTING_HOLD) &&
				level.time < bs->fanPhaseStartTime + holdMs + 600)
			{
				//Step in first; the swing starts once the predicted peak range is in reach.
				bs->fanAttackTime = level.time + 50;
			}
			else if (bs->fanAttackTime <= level.time)
			{
				NewBotAI_ResetFanChain(bs);
			}
		}
		break;
	}

	case FAN_PHASE_DWELL:
		if (bs->fanAttackTime <= level.time)
		{
			bs->fanAttackDir = -bs->fanAttackDir;
			bs->fanSwingStarted = 0;
			bs->fanPhase = FAN_PHASE_HOLD;
			bs->fanPhaseStartTime = level.time;
			bs->fanAttackTime = level.time + holdMs;
		}
		break;

	case FAN_PHASE_INACTIVE:
	default:
	{
		//Start a new chain, preferring whichever direction we're already strafing.
		int startDir = 0;

		//Even when we already have a lateral strafe direction, only convert it into a fan
		//chain when fanBias is enabled (>0), so bot_fanbias 0 remains a strict "off".
		if (fanBias > 0.0f && bs->randomStrafeEndTime > level.time && bs->randomStrafeDir)
		{
			startDir = bs->randomStrafeDir;
		}
		else if (fanBias > 0.0f)
		{
			//Learned outcomes of opening with a swing in this context nudge the fan entry
			//chance up or down (capped by G_BotLearnBonus, 0 without enough data).
			const int learnedEntry = G_BotLearnBonus(&g_entities[bs->client], bs->currentEnemy,
				NewBotAI_GetEnemyStimulusToken(bs), BOTLEARN_TOK_SWING, BOTLEARN_TOK_NONE, bs->settings.skill);

			if (Q_irand(1, 100) <= (int)fanBias + learnedEntry)
				startDir = NewBotAI_FanStartDirection(Q_irand(1, 100));
		}

		if (startDir)
		{
			bs->fanPackage = NewBotAI_GetFanPackage(bs);
			bs->fanAttackDir = startDir;
			bs->fanPhase = FAN_PHASE_HOLD;
			bs->fanPhaseStartTime = level.time;
			bs->fanAttackTime = level.time + holdMs;
			bs->fanChainStartTime = level.time;
			bs->fanChainStartHealth = g_entities[bs->client].health;
			bs->fanSwingCount = 0;
			bs->fanSwingStarted = 0;
			if (bot_fan_debug.integer)
			{
				Com_Printf("fan_debug: bot %i package=%s dir=%i enemy=%i range=%.1f hp=%i fp=%i\n",
					bs->client,
					NewBotAI_GetFanPackageName(bs->fanPackage),
					startDir,
					bs->currentEnemy ? bs->currentEnemy->s.number : -1,
					bs->frame_Enemy_Len,
					g_entities[bs->client].health,
					bs->cur_ps.fd.forcePower);
			}
		}
		break;
	}
	}
}

// HOLD is exclusive strafe-only movement so the horizontal swing starts with no
// forward/back input. DWELL is free movement for approach/positioning and leaves normal
// steering unchanged.
static void NewBotAI_ApplyHorizontalSwingMove(bot_state_t *bs)
{
	if (bs->saberTechniqueCandidate)
		return;
	if (bs->fanPhase == FAN_PHASE_DWELL)
	{
		trap->EA_MoveForward(bs->client);
		return;
	}

	if (bs->fanPhase != FAN_PHASE_HOLD || !bs->fanAttackDir)
	{
		return;
	}

	trap->EA_Move(bs->client, vec3_origin, 0);
	if (bs->fanAttackDir > 0)
	{
		trap->EA_MoveRight(bs->client);
	}
	else
	{
		trap->EA_MoveLeft(bs->client);
	}
	//The swing-start frame stays pure strafe so the engine picks a horizontal L2R/R2L;
	//once the swing is running, step forward like human winners did while it landed.
	//Before the swing starts, close the gap until the predicted peak range is in reach.
	if (NewBotAI_FanHoldUsesForward(bs->fanSwingStarted, NewBotAI_GetEnemyDistance2D(bs)) ||
		(bs->fanSwingStarted && NewBotAI_FanChainIsLanding(NewBotAI_GetMsSinceSaberContactOnEnemy(bs))) ||
		(!bs->fanSwingStarted && !NewBotAI_FanHoldMayStartSwing(bs)))
	{
		trap->EA_MoveForward(bs->client);
	}
}

static qboolean NewBotAI_FanHoldMayStartSwing(bot_state_t *bs)
{
	if (bs->fanSwingStarted || BG_SaberInAttack(bs->cur_ps.saberMove))
	{
		return qtrue;
	}
	return (NewBotAI_GetCurrentSwingFooting(bs, (bs->fanSwingCount > 0) ? qtrue : qfalse) ==
		NEWBOTAI_SWING_FOOTING_START) ? qtrue : qfalse;
}


static qboolean NewBotAI_CanUseSaberThrowDefenseBreakForce(bot_state_t *bs, qboolean preferPull)
{
	const int forcePower = bs->cur_ps.fd.forcePower;
	const qboolean canPull = (!(g_forcePowerDisable.integer & (1 << FP_PULL)) &&
		(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PULL)) &&
		forcePower >= 20 &&
		!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))) ? qtrue : qfalse;
	const qboolean canPush = (!(g_forcePowerDisable.integer & (1 << FP_PUSH)) &&
		(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH)) &&
		forcePower >= 20) ? qtrue : qfalse;

	if (preferPull && canPull)
	{
		return qtrue;
	}
	if (!preferPull && canPush)
	{
		return qtrue;
	}
	return (canPull || canPush) ? qtrue : qfalse;
}

static qboolean NewBotAI_SaberThrowTrace(bot_state_t *bs, const vec3_t angles,
	newbotai_throw_phase_t phase, vec3_t endpoint)
{
	playerState_t *ps = &g_entities[bs->client].client->ps;
	gentity_t *saber;
	vec3_t eye, forward, farPoint;
	vec3_t viewDelta, targetDelta;
	trace_t viewTrace, flightTrace;

	if (!bs->currentEnemy || !bs->currentEnemy->client ||
		ps->saberEntityNum <= 0 || ps->saberEntityNum >= ENTITYNUM_WORLD)
		return qfalse;
	saber = &g_entities[ps->saberEntityNum];
	if (!saber->inuse)
		return qfalse;
	VectorCopy(ps->origin, eye);
	eye[2] += ps->viewheight;
	AngleVectors(angles, forward, NULL, NULL);
	VectorMA(eye, 4096.0f, forward, farPoint);
	// Match the engine's level-dependent steering endpoint, then test the
	// actual saber-to-endpoint segment (not the owner's shorter aim-point ray).
	JP_Trace(&viewTrace, eye, NULL, NULL, farPoint, bs->client,
		ps->fd.forcePowerLevel[FP_SABERTHROW] >= 3 ? MASK_PLAYERSOLID : MASK_SOLID,
		qfalse, 0, 0);
	VectorCopy(viewTrace.endpos, endpoint);
	JP_Trace(&flightTrace, saber->r.currentOrigin, saber->r.mins, saber->r.maxs,
		endpoint, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
	VectorSubtract(flightTrace.endpos, endpoint, farPoint);
	VectorSubtract(endpoint, eye, viewDelta);
	VectorSubtract(bs->currentEnemy->client->ps.origin, eye, targetDelta);
	return NewBotAI_SaberThrowTraceSafe(viewTrace.startsolid || viewTrace.allsolid,
		flightTrace.startsolid || flightTrace.allsolid,
		flightTrace.fraction < 1.0f && !(viewTrace.entityNum == ENTITYNUM_WORLD &&
			flightTrace.entityNum == ENTITYNUM_WORLD && VectorLengthSquared(farPoint) <= 4096.0f &&
			VectorLength(viewDelta) > VectorLength(targetDelta) + 192.0f),
		flightTrace.entityNum == bs->currentEnemy->s.number, phase) ? qtrue : qfalse;
}

static qboolean NewBotAI_SaberThrowWaypointSafe(bot_state_t *bs, const vec3_t aim,
	newbotai_throw_phase_t phase)
{
	playerState_t *ps = &g_entities[bs->client].client->ps;
	gentity_t *saber = &g_entities[ps->saberEntityNum];
	trace_t route;
	vec3_t target;

	JP_Trace(&route, saber->r.currentOrigin, saber->r.mins, saber->r.maxs,
		aim, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
	if (!NewBotAI_SaberThrowTraceSafe(0, route.startsolid || route.allsolid,
		route.fraction < 1.0f, route.entityNum == bs->currentEnemy->s.number, phase))
		return qfalse;
	if (phase != NEWBOTAI_THROW_BYPASS && phase != NEWBOTAI_THROW_REAR)
		return qtrue;
	// A clear overhead/rear waypoint is not enough: its eventual cut-through
	// must also be reachable by the saber's hull, rather than through a wall.
	VectorCopy(bs->currentEnemy->client->ps.origin, target);
	target[2] += bs->currentEnemy->client->ps.viewheight * 0.55f;
	JP_Trace(&route, aim, saber->r.mins, saber->r.maxs, target,
		bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
	return NewBotAI_SaberThrowTraceSafe(0, route.startsolid || route.allsolid,
		route.fraction < 1.0f, route.entityNum == bs->currentEnemy->s.number,
		NEWBOTAI_THROW_CUT_THROUGH) ? qtrue : qfalse;
}

static void NewBotAI_AdjustSaberThrowLead(bot_state_t *bs)
{
	playerState_t *ps = &g_entities[bs->client].client->ps;
	playerState_t *enemy;
	gentity_t *saber;
	vec3_t facing, right, angles, aim, eye, direction, relative, endpoint;
	int cadence, i;
	int phase;
	int guarded;
	qboolean heading;

	if (!bs->currentEnemy || !bs->currentEnemy->client || !ps->saberInFlight ||
		NewBotAI_HasDroppedOwnSaber(bs) ||
		ps->saberEntityNum <= 0 || ps->saberEntityNum >= ENTITYNUM_WORLD)
		return;
	saber = &g_entities[ps->saberEntityNum];
	if (bs->saberThrowStartTime <= 0)
		bs->saberThrowStartTime = ps->saberDidThrowTime > 0 ? ps->saberDidThrowTime : level.time;
	if (!saber->inuse || saber->think == saberBackToOwner)
	{
		if (bs->saberThrowPhase != NEWBOTAI_THROW_RECALL)
			NewBotAI_RecordSaberThrowDecision(bs, level.time - bs->saberThrowStartTime, qfalse,
				saber->inuse ? "return" : "target");
		bs->saberThrowPhase = NEWBOTAI_THROW_RECALL;
		return;
	}
	if (bs->saberThrowPhase == NEWBOTAI_THROW_RECALL)
		return;
	enemy = &bs->currentEnemy->client->ps;
	cadence = NewBotAI_SaberThrowSteerCadence(ps->fd.forcePowerLevel[FP_SABERTHROW]);
	guarded = NewBotAI_SaberThrowTargetGuarded(enemy->weapon == WP_SABER,
		NewBotAI_SaberPrimaryBladeAvailable(enemy->saberHolstered), enemy->saberInFlight,
		enemy->saberBlocked == BLOCKED_PARRY_BROKEN || PM_SaberInBrokenParry(enemy->saberMove),
		BG_InKnockDown(enemy->legsAnim) || enemy->forceHandExtend == HANDEXTEND_KNOCKDOWN,
		BG_SaberInAttack(enemy->saberMove) || PM_SaberInStart(enemy->saberMove) ||
		PM_SaberInTransition(enemy->saberMove) || enemy->forceHandExtend != HANDEXTEND_NONE ||
		(enemy->fd.forcePowersActive & ((1 << FP_GRIP) | (1 << FP_DRAIN) | (1 << FP_LIGHTNING))));
	if (bs->saberThrowTargetNum != bs->currentEnemy->s.number || !bs->saberThrowPhaseTime)
	{
		bs->saberThrowTargetNum = bs->currentEnemy->s.number;
		bs->saberThrowPhase = NewBotAI_SaberThrowTargetPhase(NEWBOTAI_THROW_LAUNCH, guarded, cadence);
		bs->saberThrowPhaseTime = level.time;
		bs->saberThrowSteerTime = 0;
		bs->saberThrowPassedTarget = qfalse;
		VectorSet(angles, 0, enemy->viewangles[YAW], 0);
		AngleVectors(angles, facing, right, NULL);
		VectorSubtract(saber->r.currentOrigin, enemy->origin, relative);
		bs->saberThrowLane = DotProduct(relative, right) < 0.0f ? -1 : 1;
	}
	if (!cadence)
		return; // Level one has no engine steering.
	// An exposed target should get a direct intercept immediately, not wait
	// out the previous guarded route's steering cadence.
	if (!guarded && bs->saberThrowPhase != NEWBOTAI_THROW_CUT_THROUGH)
		bs->saberThrowSteerTime = 0;
	if (bs->saberThrowSteerTime > level.time)
	{
		VectorCopy(bs->saberThrowAim, bs->goalAngles);
		return;
	}
	VectorSet(angles, 0, enemy->viewangles[YAW], 0);
	AngleVectors(angles, facing, right, NULL);
	VectorSubtract(saber->r.currentOrigin, enemy->origin, relative);
	NewBotAI_UpdateSaberThrowPass(bs, &heading);
	phase = NewBotAI_SaberThrowNextPhase((newbotai_throw_phase_t)bs->saberThrowPhase,
		level.time - bs->saberThrowPhaseTime, cadence, DotProduct(relative, facing));
	phase = NewBotAI_SaberThrowTargetPhase((newbotai_throw_phase_t)phase, guarded, cadence);
	if (phase != bs->saberThrowPhase)
	{
		bs->saberThrowPhase = phase;
		bs->saberThrowPhaseTime = level.time;
	}
	VectorCopy(ps->origin, eye);
	eye[2] += ps->viewheight;
	for (i = 0; i < ((phase == NEWBOTAI_THROW_BYPASS || phase == NEWBOTAI_THROW_REAR) ? 4 : 1); ++i)
	{
		const int side = i % 2 ? -bs->saberThrowLane : bs->saberThrowLane;
		float rearOffset, sideOffset, heightOffset;
		VectorCopy(enemy->origin, aim);
		aim[2] += enemy->viewheight * 0.55f;
		VectorMA(aim, cadence * 0.001f, enemy->velocity, aim);
		NewBotAI_SaberThrowLaneOffsets((newbotai_throw_phase_t)phase, side, i >= 2,
			&rearOffset, &sideOffset, &heightOffset);
		VectorMA(aim, sideOffset, right, aim);
		VectorMA(aim, rearOffset, facing, aim);
		aim[2] += heightOffset;
		VectorSubtract(aim, eye, direction);
		vectoangles(direction, angles);
		if (NewBotAI_SaberThrowWaypointSafe(bs, aim, (newbotai_throw_phase_t)phase) &&
			NewBotAI_SaberThrowTrace(bs, angles, (newbotai_throw_phase_t)phase, endpoint))
		{
			bs->saberThrowLane = side;
			bs->saberThrowSteerTime = level.time + cadence;
			VectorCopy(angles, bs->saberThrowAim);
			VectorCopy(angles, bs->goalAngles);
			return;
		}
	}
	NewBotAI_RecordSaberThrowDecision(bs, level.time - bs->saberThrowStartTime, qfalse, "route");
	bs->saberThrowPhase = NEWBOTAI_THROW_RECALL;
}

static qboolean NewBotAI_SaberThrowShouldHold(bot_state_t *bs, int heldMs, qboolean hardRecall)
{
	static int learnedAt[MAX_CLIENTS];
	static int learnedForEnemy[MAX_CLIENTS];
	static int learnedBonus[MAX_CLIENTS];
	playerState_t *ps = &g_entities[bs->client].client->ps;
	playerState_t *enemy;
	qboolean heading, passed;
	int ourHealth, enemyHealth, safetyRecall, softRecall, learnedWeight = 0;
	int drainlock, lethal, forceAllowed, returning, hold;
	int safeLearnedContext;
	float aggression;
	const char *reason;

	if (hardRecall || bs->saberThrowPhase == NEWBOTAI_THROW_RECALL ||
		!bs->currentEnemy || !bs->currentEnemy->client ||
		!bs->frame_Enemy_Vis || bs->currentEnemy->health <= 0 ||
		ps->saberEntityNum <= 0 || ps->saberEntityNum >= ENTITYNUM_WORLD ||
		!g_entities[ps->saberEntityNum].inuse)
	{
		if (bs->saberThrowPhase != NEWBOTAI_THROW_RECALL)
			NewBotAI_RecordSaberThrowDecision(bs, heldMs, qfalse, "target");
		return qfalse;
	}
	enemy = &bs->currentEnemy->client->ps;
	ourHealth = g_entities[bs->client].health;
	enemyHealth = bs->currentEnemy->health;
	aggression = BotGetAggressionBias(bs);
	forceAllowed = !(g_forcePowerDisable.integer & (1 << FP_SABERTHROW)) &&
		(ps->fd.forcePowersKnown & (1 << FP_SABERTHROW)) &&
		ps->fd.forcePowerLevel[FP_SABERTHROW] > 0 &&
		NewBotAI_CanUseForcePowerNow(bs, FP_SABERTHROW);
	returning = g_entities[ps->saberEntityNum].think == saberBackToOwner;
	drainlock = NewBotAI_ShouldSuppressDrainlockSaberThrow(bs) || NewBotAI_WouldThrowInviteDrainlock(bs);
	lethal = NewBotAI_IsIncomingSaberThrowLethal(bs) || NewBotAI_IsLethalEnemySwingImminent(bs) ||
		(ourHealth + ps->stats[STAT_ARMOR] <= NEWBOTAI_SABER_CRITICAL_TOTAL_HEALTH &&
			enemyHealth > ourHealth);
	safetyRecall = !forceAllowed || returning || drainlock || lethal;
	softRecall = ourHealth < enemyHealth || aggression <= 0.0f ||
		(enemyHealth > 30 && enemy->fd.forcePower >= ps->fd.forcePower + 50);
	passed = NewBotAI_UpdateSaberThrowPass(bs, &heading);
	safeLearnedContext = bs->settings.skill > 2 && ourHealth >= 60 &&
		ourHealth + ps->stats[STAT_ARMOR] >= enemyHealth + enemy->stats[STAT_ARMOR] &&
		ps->fd.forcePower >= 50 && enemy->fd.forcePower < ps->fd.forcePower + 50 &&
		aggression >= 0.0f && enemy->groundEntityNum != ENTITYNUM_NONE;
	if (!safetyRecall && softRecall && safeLearnedContext &&
		heldMs >= NEWBOTAI_THROW_MIN_HOLD_MS && heldMs < 900 &&
		!NewBotAI_SaberThrowHoldProtected(heldMs, passed, heading) &&
		!NewBotAI_SaberThrowRoutingHold(ps->fd.forcePowerLevel[FP_SABERTHROW],
			(newbotai_throw_phase_t)bs->saberThrowPhase, heldMs))
	{
		// The early controller and final input must share the same sampled
		// learned weight on this frame, including the learner's skill noise.
		if (learnedAt[bs->client] != level.time ||
			learnedForEnemy[bs->client] != bs->currentEnemy->s.number)
		{
			learnedAt[bs->client] = level.time;
			learnedForEnemy[bs->client] = bs->currentEnemy->s.number;
			learnedBonus[bs->client] = G_BotLearnBonus(&g_entities[bs->client], bs->currentEnemy,
				NewBotAI_GetEnemyStimulusToken(bs), BOTLEARN_TOK_THROW, BOTLEARN_TOK_NONE, bs->settings.skill);
		}
		learnedWeight = learnedBonus[bs->client];
	}
	hold = NewBotAI_SaberThrowWantsHold(ps->fd.forcePowerLevel[FP_SABERTHROW],
		(newbotai_throw_phase_t)bs->saberThrowPhase, heldMs, passed, heading,
		safetyRecall, softRecall, learnedWeight, safeLearnedContext);
	if (!hold)
		reason = !forceAllowed ? "force" : returning ? "return" : drainlock ? "drain" :
			lethal ? "lethal" : NewBotAI_SaberThrowRecallDue(ps->fd.forcePowerLevel[FP_SABERTHROW],
				heldMs, 0, 0, 1) ? "limit" : "soft";
	else
		reason = heldMs < NEWBOTAI_THROW_MIN_HOLD_MS ? "min" :
			NewBotAI_SaberThrowRoutingHold(ps->fd.forcePowerLevel[FP_SABERTHROW],
				(newbotai_throw_phase_t)bs->saberThrowPhase, heldMs) ? "lane" :
			NewBotAI_SaberThrowHoldProtected(heldMs, passed, heading) ? "pass" :
			softRecall ? "learn" : "press";
	NewBotAI_RecordSaberThrowDecision(bs, heldMs, hold ? qtrue : qfalse, reason);
	return hold ? qtrue : qfalse;
}

static void NewBotAI_RecordSaberThrowDecision(bot_state_t *bs, int heldMs, qboolean hold, const char *reason)
{
	static struct
	{
		int throwStart, enemy, at;
		char note[32];
	} recorded[MAX_CLIENTS];
	char note[32];
	qboolean heading;
	const qboolean passed = NewBotAI_UpdateSaberThrowPass(bs, &heading);

	Com_sprintf(note, sizeof(note), "st:%s:%s:p%i:x%i:h%i", hold ? "H" : "R", reason,
		bs->saberThrowPhase, passed ? 1 : 0, heading ? 1 : 0);
	if (!bs->currentEnemy || !bs->currentEnemy->client)
		return;
	if (!NewBotAI_SaberThrowDecisionSampleDue(
		recorded[bs->client].throwStart == bs->saberThrowStartTime &&
			recorded[bs->client].enemy == bs->currentEnemy->s.number,
		!strcmp(recorded[bs->client].note, note), level.time - recorded[bs->client].at))
		return;
	recorded[bs->client].throwStart = bs->saberThrowStartTime;
	recorded[bs->client].enemy = bs->currentEnemy->s.number;
	recorded[bs->client].at = level.time;
	Q_strncpyz(recorded[bs->client].note, note, sizeof(recorded[bs->client].note));
	G_RecordTrackedDuelDecision(&g_entities[bs->client], bs->currentEnemy, heldMs, note);
}

static void NewBotAI_ApplySaberThrowInput(bot_state_t *bs, bot_input_t *bi)
{
	playerState_t *ps = &g_entities[bs->client].client->ps;
	vec3_t endpoint;
	int hardRecall;
	int wantsHold;

	if (!g_newBotAI.integer || bi->weapon != WP_SABER || !ps->saberInFlight ||
		NewBotAI_HasDroppedOwnSaber(bs))
		return;
	if (bs->saberThrowStartTime <= 0)
		bs->saberThrowStartTime = ps->saberDidThrowTime > 0 ? ps->saberDidThrowTime : level.time;
	hardRecall = !bs->currentEnemy || !bs->currentEnemy->client || !bs->frame_Enemy_Vis ||
		bs->currentEnemy->health <= 0 || bs->saberThrowPhase == NEWBOTAI_THROW_RECALL ||
		ps->saberEntityNum <= 0 || ps->saberEntityNum >= ENTITYNUM_WORLD ||
		!g_entities[ps->saberEntityNum].inuse;
	wantsHold = NewBotAI_SaberThrowShouldHold(bs, level.time - bs->saberThrowStartTime, hardRecall);
	if (ps->fd.forcePowerLevel[FP_SABERTHROW] >= 2 && bs->saberThrowSteerTime > 0 &&
		wantsHold && !NewBotAI_HasExclusiveFlipkickMovement(bs) && !bs->gripkickActive &&
		!(ps->fd.forcePowersActive & ((1 << FP_GRIP) | (1 << FP_DRAIN) | (1 << FP_LIGHTNING))) &&
		ps->forceHandExtend == HANDEXTEND_NONE && bs->escapeYawOverrideUntil <= level.time)
	{
		vec3_t forward, right, angles;
		int forwardMove = (bi->actionflags & ACTION_MOVEFORWARD) ? 1 :
			(bi->actionflags & ACTION_MOVEBACK) ? -1 : 0;
		int rightMove = (bi->actionflags & ACTION_MOVERIGHT) ? 1 :
			(bi->actionflags & ACTION_MOVELEFT) ? -1 : 0;
		if (forwardMove || rightMove)
		{
			VectorSet(angles, 0, bi->viewangles[YAW], 0);
			AngleVectors(angles, forward, right, NULL);
			VectorScale(forward, (float)forwardMove, bi->dir);
			VectorMA(bi->dir, (float)rightMove, right, bi->dir);
			VectorNormalize(bi->dir);
			bi->speed = 400.0f;
			bi->actionflags &= ~(ACTION_MOVEFORWARD | ACTION_MOVEBACK | ACTION_MOVELEFT | ACTION_MOVERIGHT);
		}
		// Emit the tested steering endpoint at the engine's 100/400ms cadence,
		// preserving locomotion in world space across this independent aim change.
		VectorCopy(bs->saberThrowAim, bi->viewangles);
		VectorCopy(bi->viewangles, bs->viewangles);
	}
	if (wantsHold && ps->fd.forcePowerLevel[FP_SABERTHROW] >= 2)
		hardRecall = !NewBotAI_SaberThrowTrace(bs, bi->viewangles,
			(newbotai_throw_phase_t)bs->saberThrowPhase, endpoint);
	if (wantsHold && hardRecall)
		NewBotAI_RecordSaberThrowDecision(bs, level.time - bs->saberThrowStartTime, qfalse, "route");
	if (wantsHold && !hardRecall)
	{
		bi->actionflags |= ACTION_ALT_ATTACK;
		return;
	}
	bs->saberThrowPhase = NEWBOTAI_THROW_RECALL;
	bi->actionflags &= ~ACTION_ALT_ATTACK;
	bs->doAltAttack = 0;
}

// True once the enemy's own thrown saber has started heading back toward their hand
// rather than still flying out towards us - a positive dot between the saber's current
// flight direction and the vector from the saber to its owner means the saber is closing
// that distance (returning) rather than opening it (still outbound).
static qboolean NewBotAI_IsEnemySaberReturning(bot_state_t *bs)
{
	gentity_t *saberEnt;
	vec3_t saberToEnemy;

	if (!bs->currentEnemy || !bs->currentEnemy->client ||
		!bs->currentEnemy->client->ps.saberInFlight ||
		!bs->currentEnemy->client->ps.saberEntityNum)
	{
		return qfalse;
	}

	saberEnt = &g_entities[bs->currentEnemy->client->ps.saberEntityNum];
	VectorSubtract(bs->currentEnemy->client->ps.origin, saberEnt->s.pos.trBase, saberToEnemy);

	return (DotProduct(saberEnt->s.pos.trDelta, saberToEnemy) > 0.0f) ? qtrue : qfalse;
}

static qboolean NewBotAI_GetEnemySaberFlightThreat(bot_state_t *bs, float *forwardDistOut, float *saberSpeedOut, qboolean *isReturningOut)
{
	gentity_t *saberEnt;
	vec3_t saberOrigin, saberVelocity, saberToUs, saberDir, closestPoint;
	float forwardDist;
	float lateralDistSq;
	float saberSpeed;
	float threatRadius;
	int saberEntNum;
	qboolean isReturning;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (!bs->currentEnemy->client->ps.saberInFlight)
	{
		return qfalse;
	}

	isReturning = NewBotAI_IsEnemySaberReturning(bs);
	saberEntNum = bs->currentEnemy->client->ps.saberEntityNum;
	if (saberEntNum == 0)
	{
		return qfalse;
	}
	if (saberEntNum <= 0 || saberEntNum >= ENTITYNUM_WORLD || saberEntNum == ENTITYNUM_NONE)
	{
		return qfalse;
	}

	saberEnt = &g_entities[saberEntNum];
	BG_EvaluateTrajectory(&saberEnt->s.pos, level.time, saberOrigin);
	BG_EvaluateTrajectoryDelta(&saberEnt->s.pos, level.time, saberVelocity);

	VectorSubtract(bs->cur_ps.origin, saberOrigin, saberToUs);
	if (VectorLengthSquared(saberToUs) > (200.0f * 200.0f))
	{
		return qfalse;
	}

	VectorCopy(saberVelocity, saberDir);
	saberSpeed = VectorNormalize(saberDir);
	if (saberSpeed < 64.0f)
	{
		return qfalse;
	}

	forwardDist = DotProduct(saberToUs, saberDir);
	if (forwardDist <= 0.0f || forwardDist > (isReturning ? 48.0f : 96.0f))
	{
		return qfalse;
	}

	VectorMA(saberOrigin, forwardDist, saberDir, closestPoint);
	VectorSubtract(bs->cur_ps.origin, closestPoint, closestPoint);
	lateralDistSq = VectorLengthSquared(closestPoint);
	threatRadius = RadiusFromBounds(g_entities[bs->client].r.mins, g_entities[bs->client].r.maxs) + 16.0f;

	if (lateralDistSq > (threatRadius * threatRadius))
	{
		return qfalse;
	}

	if (forwardDistOut)
	{
		*forwardDistOut = forwardDist;
	}
	if (saberSpeedOut)
	{
		*saberSpeedOut = saberSpeed;
	}
	if (isReturningOut)
	{
		*isReturningOut = isReturning;
	}

	return qtrue;
}

static qboolean NewBotAI_IsEnemySaberThreatImminent(bot_state_t *bs)
{
	vec3_t a_fo;
	float forwardDist;
	float saberSpeed;
	float timeToImpactMs;
	qboolean isReturning;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	//Pre-throw anticipation: only treat it as imminent when replicated player state
	//matches saber-throw windup, not generic saber states.
	if (NewBotAI_ShouldAnticipateSaberThrow(NewBotAI_IsSaberOnlyDuel(bs),
		bs->currentEnemy->client->ps.saberInFlight,
		BG_SaberInAttack(bs->currentEnemy->client->ps.saberMove) ||
		PM_SaberInStart(bs->currentEnemy->client->ps.saberMove) ||
		PM_SaberInTransition(bs->currentEnemy->client->ps.saberMove)) &&
		bs->currentEnemy->client->ps.weapon == WP_SABER &&
		(bs->currentEnemy->client->ps.weaponstate == WEAPON_CHARGING_ALT ||
		 bs->currentEnemy->client->ps.weaponstate == WEAPON_FIRING) &&
		bs->frame_Enemy_Vis &&
		bs->frame_Enemy_Len < 220)
	{
		VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
		vectoangles(a_fo, a_fo);
		if (InFieldOfVision(bs->viewangles, 75, a_fo))
		{
			return qtrue;
		}
	}

	if (!NewBotAI_GetEnemySaberFlightThreat(bs, &forwardDist, &saberSpeed, &isReturning))
	{
		return qfalse;
	}

	timeToImpactMs = (forwardDist / saberSpeed) * 1000.0f;

	return (timeToImpactMs <= 120.0f ||
		forwardDist <= (isReturning ? 18.0f : 28.0f)) ? qtrue : qfalse;
}

//Damage an enemy saber deals on the outbound pass and on the return pass (see
//SABER_THROWN_HIT_DAMAGE / SABER_THROWN_RETURN_HIT_DAMAGE in w_saber.c).
#define NEWBOTAI_SABER_THROW_HIT_DAMAGE 30
#define NEWBOTAI_SABER_THROW_RETURN_DAMAGE 5

//Shared lethality predicate: is the saber throw that is currently coming at us going to
//kill us outright? Deliberately independent of whether drain (or any other power) happens
//to be available, so it can veto hand-extend powers that would drop our block, and so
//defensive rolls can be restricted to genuinely lethal windows.
static qboolean NewBotAI_IsIncomingSaberThrowLethal(bot_state_t *bs)
{
	float forwardDist;
	float saberSpeed;
	qboolean isReturning;
	int expectedDamage;
	int ourTotalHealth;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (!bs->currentEnemy->client->ps.saberInFlight)
	{
		return qfalse;
	}

	if (!NewBotAI_GetEnemySaberFlightThreat(bs, &forwardDist, &saberSpeed, &isReturning))
	{
		return qfalse;
	}

	//Protect soaks the hit, so it is not lethal while it is up.
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT))
	{
		return qfalse;
	}

	expectedDamage = isReturning ?
		NEWBOTAI_SABER_THROW_RETURN_DAMAGE : NEWBOTAI_SABER_THROW_HIT_DAMAGE;
	ourTotalHealth = g_entities[bs->client].health + bs->cur_ps.stats[STAT_ARMOR];

	return (ourTotalHealth <= expectedDamage) ? qtrue : qfalse;
}

//Base damage of a connecting enemy saber swing (SABER_HITDAMAGE in w_saber.c, before the
//g_saberDamageScale multiplier that CheckSaberDamage applies).
#define NEWBOTAI_SABER_SWING_BASE_DAMAGE 35
//A swing only threatens us once the enemy blade can actually reach us.
#define NEWBOTAI_SABER_SWING_LETHAL_RANGE 112.0f

//Companion to NewBotAI_IsIncomingSaberThrowLethal for melee: the enemy is mid-swing, close
//enough to connect this swing, and the hit would take our remaining health+armor pool to
//zero.
static qboolean NewBotAI_IsLethalEnemySwingImminent(bot_state_t *bs)
{
	int expectedDamage;
	int ourTotalHealth;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (bs->currentEnemy->client->ps.weapon != WP_SABER ||
		!BG_SaberInAttack(bs->currentEnemy->client->ps.saberMove))
	{
		return qfalse;
	}

	if (!bs->frame_Enemy_Vis || bs->frame_Enemy_Len > NEWBOTAI_SABER_SWING_LETHAL_RANGE)
	{
		return qfalse;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT))
	{
		return qfalse;
	}

	expectedDamage = (int)(NEWBOTAI_SABER_SWING_BASE_DAMAGE * g_saberDamageScale.value);
	if (expectedDamage <= 0)
	{
		return qfalse;
	}

	ourTotalHealth = g_entities[bs->client].health + bs->cur_ps.stats[STAT_ARMOR];

	return (ourTotalHealth <= expectedDamage) ? qtrue : qfalse;
}

//The single "we are certainly going to die if we stand here" test. Defensive rolls are
//gated on this: a roll gives up our block and our facing for the whole animation, so it is
//only ever worth it when staying put is lethal.
static qboolean NewBotAI_IsCertainDeathWindow(bot_state_t *bs)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	return (NewBotAI_IsIncomingSaberThrowLethal(bs) ||
		NewBotAI_IsLethalEnemySwingImminent(bs)) ? qtrue : qfalse;
}

//Squared speed at which PM_CheckDuck lets a crouch turn into a roll. The engine uses
//30000 when g_fixRoll is 1 and 40000 otherwise (see bg_pmove.c PM_TryRoll callers), so
//gate on the lower of the two to catch every case.
#define NEWBOTAI_ROLL_SPEED_SQ 30000.0f

//Final input-stage gate for defensive rolls. A backward roll needs crouch held while the
//bot is running backwards on the ground, and it costs us our block and our facing for the
//entire animation - which is exactly how bots were dying to follow-up pressure. Strip the
//crouch unless staying put is certainly lethal, so rolls only happen as a true last resort.
//Sideways escape rolls (the drain roll) and non-combat crouching are untouched.
static void NewBotAI_FilterDefensiveRollInput(bot_state_t *bs, bot_input_t *bi)
{
	float horizontalSpeedSq;

	if (!bs || !bi || !g_newBotAI.integer)
	{
		return;
	}

	if (!(bi->actionflags & ACTION_CROUCH) || !(bi->actionflags & ACTION_MOVEBACK))
	{
		return;
	}

	if (!bs->currentEnemy || !bs->currentEnemy->client)
	{
		return;
	}

	//Only grounded, running-speed backward movement can roll.
	if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE)
	{
		return;
	}

	horizontalSpeedSq = (bs->cur_ps.velocity[0] * bs->cur_ps.velocity[0]) +
		(bs->cur_ps.velocity[1] * bs->cur_ps.velocity[1]);
	if (g_fixRoll.integer <= 1 && horizontalSpeedSq < NEWBOTAI_ROLL_SPEED_SQ)
	{
		return;
	}

	if (NewBotAI_IsCertainDeathWindow(bs) ||
		NewBotAI_ShouldEmergencyDrainRollSaberThrow(bs))
	{
		return;
	}

	//Survivable: keep the saber up and retreat on foot instead of rolling.
	bi->actionflags &= ~ACTION_CROUCH;
}

static qboolean NewBotAI_ShouldPlaySafeDrainVsSaberThrow(bot_state_t *bs)
{
	vec3_t a_fo;
	int ourTotalHealth;
	int totalHealthDelta;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (!bs->currentEnemy->client->ps.saberInFlight)
	{
		return qfalse;
	}

	if (bs->cur_ps.weapon != WP_SABER)
	{
		return qfalse;
	}

	if (bs->cur_ps.saberInFlight ||
		(g_forcePowerDisable.integer & (1 << FP_DRAIN)) ||
		!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_DRAIN)) ||
		bs->cur_ps.fd.forcePower < 25)
	{
		return qfalse;
	}

	if (!bs->frame_Enemy_Vis || bs->frame_Enemy_Len > MAX_DRAIN_DISTANCE)
	{
		return qfalse;
	}

	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
	{
		return qfalse;
	}

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
	vectoangles(a_fo, a_fo);

	if (!InFieldOfVision(bs->viewangles, 60, a_fo))
	{
		return qfalse;
	}

	totalHealthDelta = NewBotAI_GetTotalHealthDelta(bs);
	ourTotalHealth = g_entities[bs->client].health + bs->cur_ps.stats[STAT_ARMOR];

	if (ourTotalHealth <= BotGetHealthBiasThreshold())
	{
		return qtrue;
	}

	if (totalHealthDelta < 0)
	{
		return qtrue;
	}

	return qfalse;
}

static qboolean NewBotAI_ShouldJumpDrainVsSaberThrow(bot_state_t *bs)
{
	float forwardDist;
	float saberSpeed;
	float timeToImpactMs;
	qboolean isReturning;
	const int ourHealth = g_entities[bs->client].health;
	const int totalHealthDelta = NewBotAI_GetTotalHealthDelta(bs);
	const qboolean losingHealthWar = (totalHealthDelta < 0) ? qtrue : qfalse;
	const qboolean canEatTheThrow = (ourHealth > 30 && !losingHealthWar) ? qtrue : qfalse;
	qboolean forceImmediateHop;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (!NewBotAI_GetEnemySaberFlightThreat(bs, &forwardDist, &saberSpeed, &isReturning))
	{
		return qfalse;
	}

	//If the thrower is already near death, they cannot break saber defense with a pull.
	//Stay grounded for normal advance/retreat defense unless we're ready to cash a flipkick.
	if (bs->currentEnemy->client->ps.saberInFlight &&
		bs->currentEnemy->health < 20 &&
		!NewBotAI_IsFlipkickSetupReady(bs))
	{
		return qfalse;
	}

	if (ourHealth <= 20)
	{
		return qfalse;
	}

	timeToImpactMs = (forwardDist / saberSpeed) * 1000.0f;
	forceImmediateHop = NewBotAI_ShouldForceImmediateSaberThrowHop(
		timeToImpactMs, forwardDist, isReturning, bs->settings.skill) ? qtrue : qfalse;

	//Healthy bots can afford to keep pressing or repositioning against most throws instead
	//of bunny-hopping the moment the saber is merely on line; only hop when the impact is
	//truly immediate or when we're already behind on total health.
	if (canEatTheThrow && !forceImmediateHop)
	{
		return qfalse;
	}

	return NewBotAI_ShouldUseCombatHop(bs, forceImmediateHop);
}

// Low-health throw panic: when a saber throw is imminent and HP is below 20, roll
// sideways and yaw while attempting drain instead of taking the direct line hit.
static qboolean NewBotAI_ShouldEmergencyDrainRollSaberThrow(bot_state_t *bs)
{
	float forwardDist;
	float saberSpeed;
	float timeToImpactMs;
	qboolean isReturning;
	int ourHealth;
	int ourTotalHealth;
	qboolean canEmergencyDrainRoll;

	if (!bs || bs->client < 0 || bs->client >= MAX_CLIENTS ||
		!bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}
	ourHealth = g_entities[bs->client].health;
	ourTotalHealth = ourHealth + bs->cur_ps.stats[STAT_ARMOR];
	canEmergencyDrainRoll = (bs->currentEnemy->client->ps.saberInFlight &&
		NewBotAI_IsEnemySaberThreatImminent(bs) &&
		ourHealth < 25 &&
		ourTotalHealth <= 40 &&
		bs->frame_Enemy_Len < 220) ? qtrue : qfalse;
	if (!canEmergencyDrainRoll)
	{
		return qfalse;
	}
	//Tighten to only truly immediate lethal windows with computable flight timing.
	if (!NewBotAI_GetEnemySaberFlightThreat(bs, &forwardDist, &saberSpeed, &isReturning) || saberSpeed <= 0.0f)
	{
		return qfalse;
	}
	(void)isReturning;
	if (forwardDist < 0.0f)
	{
		forwardDist = -forwardDist;
	}
	timeToImpactMs = (forwardDist / saberSpeed) * 1000.0f;
	if (timeToImpactMs > 300.0f)
	{
		return qfalse;
	}

	return qtrue;
}

static void NewBotAI_ApplySidewaysDrainRoll(bot_state_t *bs, qboolean moveBack)
{
	vec3_t a_fo;
	vec3_t yawTarget;
	float yawBlend;
	float yawOffset;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return;
	}

	if (bs->drainRollYawStart <= 0 || bs->drainRollYawStart > level.time)
	{
		bs->drainRollDir = Q_irand(0, 1) ? 1 : -1;
		bs->drainRollYawStart = level.time;
	}
	NewBotAI_StartEscapeYawOverride(bs, NEWBOTAI_ESCAPE_YAW_OVERRIDE_MS);

	level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
	trap->EA_ForcePower(bs->client);

	if (moveBack)
	{
		trap->EA_MoveBack(bs->client);
	}
	if (bs->drainRollDir < 0)
	{
		trap->EA_MoveLeft(bs->client);
	}
	else
	{
		trap->EA_MoveRight(bs->client);
	}

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
	vectoangles(a_fo, a_fo);
	yawBlend = (float)(level.time - bs->drainRollYawStart) / 200.0f;
	if (yawBlend > 1.0f)
	{
		yawBlend = 1.0f;
	}
	yawOffset = (bs->drainRollDir < 0) ? -90.0f : 90.0f;
	VectorCopy(bs->ideal_viewangles, yawTarget);
	yawTarget[YAW] = AngleNormalize360(a_fo[YAW] + (yawOffset * yawBlend));
	bs->ideal_viewangles[YAW] = yawTarget[YAW];
	bs->goalAngles[YAW] = yawTarget[YAW];
}

static qboolean NewBotAI_ShouldUseSafePushWindowWhilePulled(bot_state_t *bs)
{
	float timeToKickRange;
	int timingMode;
	qboolean pullActive;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}
	pullActive = (bs->cur_ps.forceHandExtend == HANDEXTEND_FORCEPULL ||
		bs->cur_ps.powerups[PW_PULL] > level.time) ? qtrue : qfalse;
	if (!pullActive)
	{
		return qfalse;
	}
	if ((g_forcePowerDisable.integer & (1 << FP_PUSH)) ||
		!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH)) ||
		bs->cur_ps.fd.forcePower < 20)
	{
		return qfalse;
	}
	if (bs->currentEnemy->client->ps.saberInFlight ||
		NewBotAI_IsEnemySaberThreatImminent(bs))
	{
		return qfalse;
	}
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
	{
		return qfalse;
	}

	timeToKickRange = NewBotAI_GetPullkickTimeToKickRange(bs, qtrue, &timingMode);
	if (timingMode == NEWBOTAI_PULL_TIMING_IMMEDIATE)
	{
		return qtrue;
	}
	if (timeToKickRange < 0.0f)
	{
		return qfalse;
	}

	return (timeToKickRange <= NewBotAI_GetPullkickDefensiveReactionWindowMs()) ? qtrue : qfalse;
}

static qboolean NewBotAI_ShouldForcePulledFlipkickOverride(bot_state_t *bs, int *timeToKickRangeMsOut, int *timingModeOut)
{
	float timeToKickRange;
	int timingMode;
	const int reactionWindowMs = NewBotAI_GetPullkickDefensiveReactionWindowMs();
	const int pullFlipkickWindowMs = reactionWindowMs + 120;

	if (timeToKickRangeMsOut)
	{
		*timeToKickRangeMsOut = -1;
	}
	if (timingModeOut)
	{
		*timingModeOut = NEWBOTAI_PULL_TIMING_NONE;
	}
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}
	if (!NewBotAI_IsBeingPulledTowardEnemy(bs))
	{
		return qfalse;
	}
	if (!g_flipKick.integer || !NewBotAI_CanAttemptFlipkick(bs) || !NewBotAI_IsPullkickOpportunity(bs))
	{
		return qfalse;
	}
	if (bs->currentEnemy->client->ps.weapon != WP_SABER ||
		bs->currentEnemy->client->ps.saberInFlight ||
		NewBotAI_IsEnemySaberThreatImminent(bs))
	{
		return qfalse;
	}
	if (bs->frame_Enemy_Len > 240.0f)
	{
		return qfalse;
	}
	if (NewBotAI_ShouldAvoidFlipkickForSafety(bs) && !NewBotAI_IsImmediateFlipkickContact(bs))
	{
		return qfalse;
	}

	timeToKickRange = NewBotAI_GetPullkickTimeToKickRange(bs, qtrue, &timingMode);
	if (timingModeOut)
	{
		*timingModeOut = timingMode;
	}

	if (timingMode == NEWBOTAI_PULL_TIMING_IMMEDIATE)
	{
		if (timeToKickRangeMsOut)
		{
			*timeToKickRangeMsOut = 0;
		}
		return qtrue;
	}
	if (timeToKickRange < 0.0f || timeToKickRange > pullFlipkickWindowMs)
	{
		return qfalse;
	}

	if (timeToKickRangeMsOut)
	{
		*timeToKickRangeMsOut = (int)timeToKickRange;
	}
	return qtrue;
}

static qboolean NewBotAI_HandleRecoveryRollForcepower(bot_state_t *bs)
{
	int drainWeight;
	int gripWeight;
	int minWeight = 0;
	qboolean useTheForce = qfalse;

	if (!NewBotAI_IsKnockdownRecoveryRoll(bs->cur_ps.legsAnim))
	{
		return qfalse;
	}

	if (bs->cur_ps.fd.forceSide == FORCE_DARKSIDE)
	{
		drainWeight = NewBotAI_GetDrain(bs);
		gripWeight = NewBotAI_GetGrip(bs);
		if (drainWeight > minWeight && drainWeight >= gripWeight)
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
			useTheForce = qtrue;
		}
		else if (gripWeight > minWeight)
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_GRIP;
			useTheForce = qtrue;
		}
		if (useTheForce)
		{
			trap->EA_ForcePower(bs->client);
		}
	}

	//Recovery rolls are intentionally restricted to drain or grip only. If neither
	//dark-side option is currently viable, or this bot is lightside and cannot use them,
	//do not fall through to the normal force choosers.
	return qtrue;
}

// True when our own thrown saber is still out (in flight, not knocked away) and we are
// positioned between it and the enemy while also being closer to the enemy than the
// saber currently is - i.e. we've pressed forward past our own saber's flight path and
// are now the exposed, saberless one closing on the opponent. See NewBotAI_GetPull and
// NewBotAI_GetAntiDrainWeight for how this weights the pullkick/antidrain response.
static qboolean NewBotAI_IsBetweenOwnSaberAndEnemy(bot_state_t *bs)
{
	gentity_t *saberEnt;
	vec3_t saberToEnemy, saberToUs, usToEnemy;

	if (!bs->currentEnemy || !bs->currentEnemy->client ||
		!bs->cur_ps.saberInFlight || !bs->cur_ps.saberEntityNum)
	{
		return qfalse;
	}

	saberEnt = &g_entities[bs->cur_ps.saberEntityNum];

	VectorSubtract(bs->currentEnemy->client->ps.origin, saberEnt->s.pos.trBase, saberToEnemy);
	VectorSubtract(bs->cur_ps.origin, saberEnt->s.pos.trBase, saberToUs);

	//Only "between" if we sit on the same side of the saber as the enemy does.
	if (DotProduct(saberToEnemy, saberToUs) <= 0.0f)
	{
		return qfalse;
	}

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->cur_ps.origin, usToEnemy);

	return (VectorLengthSquared(usToEnemy) < VectorLengthSquared(saberToEnemy)) ? qtrue : qfalse;
}

static qboolean NewBotAI_ShouldCloseGapVsEnemySaberThrow(bot_state_t *bs)
{
	gentity_t *saberEnt;
	vec3_t saberOrigin;
	float saberDist;
	int saberEntNum;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client ||
		!bs->currentEnemy->client->ps.saberInFlight ||
		!bs->currentEnemy->client->ps.saberEntityNum)
	{
		return qfalse;
	}

	saberEntNum = bs->currentEnemy->client->ps.saberEntityNum;
	if (saberEntNum <= 0 || saberEntNum >= ENTITYNUM_WORLD || saberEntNum == ENTITYNUM_NONE)
	{
		return qfalse;
	}

	saberEnt = &g_entities[saberEntNum];
	BG_EvaluateTrajectory(&saberEnt->s.pos, level.time, saberOrigin);
	saberDist = Distance(bs->cur_ps.origin, saberOrigin);

	if (saberDist <= bs->frame_Enemy_Len)
	{
		return qtrue;
	}

	return (NewBotAI_IsEnemySaberReturning(bs) &&
		saberDist + 32.0f <= bs->frame_Enemy_Len) ? qtrue : qfalse;
}

static void NewBotAI_TrySaberThrowDefenseBreak(bot_state_t *bs)
{
	vec3_t a_fo;
	qboolean preferPull;
	int ownThrowTime;

	if (!bs->currentEnemy || !bs->currentEnemy->client || !bs->cur_ps.saberInFlight)
	{
		return;
	}

	if (!bs->frame_Enemy_Vis ||
		bs->cur_ps.groundEntityNum == ENTITYNUM_NONE ||
		bs->currentEnemy->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		return;
	}

	//Throw -> pull: jundon pulls ~66ms after the release, from anywhere the throw reaches.
	//Lower skills follow up later (see NewBotAI_GetComboGapMs).
	if (bs->frame_Enemy_Len < 96 || bs->frame_Enemy_Len > 640)
	{
		return;
	}
	ownThrowTime = G_BotLearnLastTokenTime(bs->client, BOTLEARN_TOK_THROW);
	if (ownThrowTime > 0 && level.time - ownThrowTime < NewBotAI_GetComboGapMs(NEWBOTAI_COMBO_THROW_PULL,
		bs->settings.skill, 0, NewBotAI_GetDecisionMistakeChance(bs)))
	{
		return;
	}

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
	vectoangles(a_fo, a_fo);
	if (!InFieldOfVision(bs->viewangles, 60, a_fo))
	{
		return;
	}

	//Throw -> pull won the exchange in the duel data, so skill 7+ always pulls; lower
	//skills pull when PTK-weighted (aggressive bias) and push to break the guard otherwise.
	preferPull = (bs->settings.skill >= 7.0f || BotGetAggressionBias(bs) > 0.0f) ? qtrue : qfalse;

	if (!NewBotAI_CanUseSaberThrowDefenseBreakForce(bs, preferPull))
	{
		return;
	}

	if (preferPull &&
		!(g_forcePowerDisable.integer & (1 << FP_PULL)) &&
		(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PULL)) &&
		bs->cur_ps.fd.forcePower >= 20 &&
		!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB)))
	{
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
		NewBotAI_ApplyPullMistake(bs);
		if (level.framenum % 2)
		{
			trap->EA_ForcePower(bs->client);
		}
	}
	else if (!(g_forcePowerDisable.integer & (1 << FP_PUSH)) &&
		(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH)) &&
		bs->cur_ps.fd.forcePower >= 20)
	{
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
		if (level.framenum % 2)
		{
			trap->EA_ForcePower(bs->client);
		}
	}
}

// Airborne swings hit 8% vs 17% grounded in the duel data and bot jump attacks landed 0 of 29:
// only start one into a knocked-down/recovering enemy, or as a per-jump low-skill mistake roll.
static qboolean NewBotAI_MayStartAirborneSwing(bot_state_t *bs)
{
	int enemyAnim;
	qboolean enemyDown;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
		return qfalse;
	enemyAnim = bs->currentEnemy->client->ps.legsAnim;
	enemyDown = (BG_InKnockDown(enemyAnim) || NewBotAI_IsGetupAnim(enemyAnim) ||
		NewBotAI_IsForceGetupAnim(enemyAnim)) ? qtrue : qfalse;
	if (bs->airSwingRollTime != bs->jumpAttackGateTime || !bs->airSwingRollTime)
	{
		bs->airSwingRollTime = bs->jumpAttackGateTime ? bs->jumpAttackGateTime : level.time;
		bs->airSwingAllowed = NewBotAI_AllowAirborneSwingStart(0,
			NewBotAI_GetDecisionMistakeChance(bs) / 2, Q_irand(1, 100)) ? qtrue : qfalse;
	}
	return (NewBotAI_AllowAirborneSwingStart(enemyDown ? 1 : 0, 0, 0) || bs->airSwingAllowed) ? qtrue : qfalse;
}

static void NewBotAI_ApplyJumpAttackGate(bot_state_t *bs)
{
	usercmd_t *cmd;
	qboolean jumpInputActive;
	qboolean airborneRising;
	qboolean jumpStateActive;

	if (!bs || bs->cur_ps.weapon != WP_SABER)
	{
		return;
	}

	cmd = &level.clients[bs->client].pers.cmd;
	jumpInputActive = (cmd->upmove > 0 ||
		bs->jumpTime > level.time ||
		bs->jumpHoldTime > level.time ||
		bs->flipkickInputTime > level.time) ? qtrue : qfalse;
	airborneRising = (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE &&
		bs->cur_ps.velocity[2] > 0.0f) ? qtrue : qfalse;
	jumpStateActive = (jumpInputActive || airborneRising) ? qtrue : qfalse;

	if (jumpStateActive && !bs->jumpAttackGateJumping)
	{
		bs->jumpAttackGateTime = level.time;
	}
	bs->jumpAttackGateJumping = jumpStateActive;

	if (NewBotAI_IsJumpAttackSuppressionWindowActive(
		level.time,
		bs->jumpAttackGateTime,
		NEWBOTAI_JUMP_ATTACK_GATE_MS))
	{
		NewBotAI_ApplyJumpAttackSuppression(1, &bs->doAttack, &bs->doAltAttack);
	}
}

static void NewBotAI_BlockAccidentalSaberSpecialMoves(bot_state_t *bs)
{
	usercmd_t *cmd;
	int effectiveRightMove;
	int effectiveUpMove;
	qboolean saberBusy;
	qboolean attackPressed;

	if (!bs || bs->cur_ps.weapon != WP_SABER)
	{
		return;
	}

	cmd = &level.clients[bs->client].pers.cmd;
	effectiveRightMove = NewBotAI_GetEffectiveMoveInput(cmd->rightmove, bs->forceMove_Right);
	effectiveUpMove = NewBotAI_GetEffectiveMoveInput(cmd->upmove, bs->forceMove_Up);
	saberBusy = (BG_SaberInAttack(bs->cur_ps.saberMove) ||
		PM_SaberInStart(bs->cur_ps.saberMove) ||
		PM_SaberInTransition(bs->cur_ps.saberMove)) ? qtrue : qfalse;
	attackPressed = (bs->doAttack || (cmd->buttons & BUTTON_ATTACK)) ? qtrue : qfalse;

	if (!NewBotAI_ShouldBlockOrthogonalSaberSpecialInput(
		effectiveUpMove,
		effectiveRightMove,
		(cmd->forwardmove != 0 || bs->forceMove_Forward != 0),
		bs->cur_ps.groundEntityNum != ENTITYNUM_NONE,
		saberBusy,
		attackPressed))
	{
		return;
	}

	if (bs->forceMove_Right)
	{
		bs->forceMove_Right = 0;
	}
	else
	{
		cmd->rightmove = 0;
	}

	if (bs->forceMove_Up)
	{
		bs->forceMove_Up = 0;
	}
	else
	{
		cmd->upmove = 0;
	}
}

static int BotGetStrafeFrequencyPercent(void)
{
	int frequency = bot_strafefrequency.integer;

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

static int NewBotAI_GetContextualStrafeFrequencyPercent(bot_state_t *bs)
{
	int frequency = BotGetStrafeFrequencyPercent();
	int activeNonSpeedPowers;

	if (!bs)
	{
		return frequency;
	}

	activeNonSpeedPowers = bs->cur_ps.fd.forcePowersActive &
		((1 << FP_GRIP) |
		 (1 << FP_DRAIN) |
		 (1 << FP_PULL) |
		 (1 << FP_PUSH) |
		 (1 << FP_LIGHTNING) |
		 (1 << FP_HEAL) |
		 (1 << FP_TEAM_HEAL) |
		 (1 << FP_TEAM_FORCE));

	frequency = NewBotAI_GetContextualStrafeFrequency(
		frequency,
		(bs->conserveUntil > level.time) ? 1 : 0,
		(bs->cur_ps.weapon == WP_SABER) ? 1 : 0,
		(bs->doAttack || bs->doAltAttack) ? 1 : 0,
		activeNonSpeedPowers ? 1 : 0);

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_LIGHTNING))
	{
		frequency += Com_Clampi(0, 100, bot_lightningstrafebonus.integer);
		if (frequency > 100)
		{
			frequency = 100;
		}
	}

	return frequency;
}

static int BotRollStrafeDurationMs(void)
{
	float normalizedDuration = bot_strafeduration.value / 100.0f;
	float minDuration;
	float maxDuration;
	float roll;
	float exponent;
	float scaled;

	if (normalizedDuration < 0.0f)
	{
		normalizedDuration = 0.0f;
	}
	else if (normalizedDuration > 1.0f)
	{
		normalizedDuration = 1.0f;
	}

	minDuration = 80.0f + (normalizedDuration * 420.0f);
	maxDuration = 300.0f + (normalizedDuration * 2200.0f);

	roll = Q_flrand(0.0f, 1.0f);
	exponent = 1.0f - ((normalizedDuration - 0.5f) * 2.0f);
	if (exponent < 0.35f)
	{
		exponent = 0.35f;
	}
	else if (exponent > 2.5f)
	{
		exponent = 2.5f;
	}

	scaled = powf(roll, exponent);

	return (int)(minDuration + (scaled * (maxDuration - minDuration)));
}

static void NewBotAI_ClearRandomStrafeOverlay(bot_state_t *bs)
{
	if (!bs)
	{
		return;
	}

	bs->randomStrafeDir = 0;
	bs->randomStrafeEndTime = 0;
	bs->randomStrafeMode = 0;
}

static void NewBotAI_RollRandomStrafeOverlay(bot_state_t *bs, int minDuration, int maxDuration, qboolean retreating)
{
	int diagonalRoll;
	int lateralRoll;

	if (!bs)
	{
		return;
	}

	if (minDuration < 0)
	{
		minDuration = 0;
	}
	if (maxDuration < minDuration)
	{
		maxDuration = minDuration;
	}
	if (NewBotAI_IsDuelStrafeSuppressed(bs))
	{
		NewBotAI_ClearRandomStrafeOverlay(bs);
		return;
	}

	lateralRoll = Q_irand(1, 100);
	if (lateralRoll <= 45)
	{
		bs->randomStrafeDir = -1;
	}
	else if (lateralRoll <= 90)
	{
		bs->randomStrafeDir = 1;
	}
	else
	{
		bs->randomStrafeDir = 0;
	}
	bs->randomStrafeEndTime = level.time + Q_irand(minDuration, maxDuration);
	if (bs->randomStrafeEndTime <= level.time)
	{
		bs->randomStrafeEndTime = level.time + 1;
	}
	if (!bs->randomStrafeDir)
	{
		bs->randomStrafeMode = 0;
		return;
	}
	diagonalRoll = Q_irand(1, 100);
	if (diagonalRoll <= 60)
	{
		bs->randomStrafeMode = retreating ? -1 : 1;
	}
	else if (diagonalRoll <= 90)
	{
		bs->randomStrafeMode = retreating ? 1 : -1;
	}
	else
	{
		bs->randomStrafeMode = 0;
	}
}

static void NewBotAI_ApplyRandomStrafePattern(bot_state_t *bs)
{
	if (!bs)
	{
		return;
	}
	if (NewBotAI_IsDuelStrafeSuppressed(bs))
	{
		return;
	}

	if (bs->randomStrafeMode > 0)
	{
		trap->EA_MoveForward(bs->client);
	}
	else if (bs->randomStrafeMode < 0)
	{
		trap->EA_MoveBack(bs->client);
	}

	if (bs->randomStrafeDir > 0)
	{
		trap->EA_MoveRight(bs->client);
	}
	else if (bs->randomStrafeDir < 0)
	{
		trap->EA_MoveLeft(bs->client);
	}
}

static void NewBotAI_ApplyRandomStrafeOverlay(bot_state_t *bs)
{
	int frequency;

	if (!bs->currentEnemy || bs->beStill > level.time || bs->doingFallback)
	{
		NewBotAI_ClearRandomStrafeOverlay(bs);
		return;
	}
	if (NewBotAI_IsRecoveryMovementActive(bs) || NewBotAI_HasExclusiveFlipkickMovement(bs))
	{
		NewBotAI_ClearRandomStrafeOverlay(bs);
		return;
	}
	if (NewBotAI_IsDuelStrafeSuppressed(bs))
	{
		NewBotAI_ClearRandomStrafeOverlay(bs);
		return;
	}
	if (bs->saberTechniqueOwnsInputs || bs->saberTechniqueCandidate)
	{
		NewBotAI_ClearRandomStrafeOverlay(bs);
		return;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_GRIP))
	{
		//No lateral overlay during a gripkick - movement is exclusively straight forward
		//into the flipkick or exclusively straight back while the yaw jerk swings the
		//target back around in front of us.
		NewBotAI_ClearRandomStrafeOverlay(bs);
		return;
	}

	if (bs->flipkickInputTime > level.time || bs->pullKickJumpTime != 0)
	{
		//Flipkick and scheduled pullkick jumps need exclusive forward input.
		NewBotAI_ClearRandomStrafeOverlay(bs);
		return;
	}

	if (bs->fanPhase != FAN_PHASE_INACTIVE)
	{
		//Fan chain owns strafe timing/direction itself.
		NewBotAI_ClearRandomStrafeOverlay(bs);
		return;
	}

	if (bs->combatAction == BOT_COMBAT_ACTION_RETREAT_DEFENSE || bs->runningLikeASissy)
	{
		//Directed retreat movement owns its inputs separately from the free-move strafe overlay.
		NewBotAI_ClearRandomStrafeOverlay(bs);
		return;
	}

	if (NewBotAI_IsSaberSwingStartWindow(bs))
	{
		return;
	}
	if (bs->frame_Enemy_Vis && bs->frame_Enemy_Len < 220 && VectorLengthSquared(bs->cur_ps.velocity) < 10000)
	{
		NewBotAI_ClearRandomStrafeOverlay(bs);
		trap->EA_MoveForward(bs->client);
		return;
	}

	frequency = NewBotAI_GetContextualStrafeFrequencyPercent(bs);
	if (!frequency)
	{
		NewBotAI_ClearRandomStrafeOverlay(bs);
		return;
	}

	if (bs->randomStrafeEndTime <= level.time)
	{
		if (Q_irand(1, 100) <= frequency)
		{
			const int duration = BotRollStrafeDurationMs();
			NewBotAI_RollRandomStrafeOverlay(
				bs,
				duration,
				duration,
				(bs->combatAction == BOT_COMBAT_ACTION_RETREAT_DEFENSE || bs->runningLikeASissy) ? qtrue : qfalse);
		}
		else
		{
			NewBotAI_ClearRandomStrafeOverlay(bs);
		}
	}

	if (bs->randomStrafeEndTime > level.time)
	{
		//Item 2C: lateral overlay while touching a wall becomes a diagonal wallrun
		//once a jump lands on top of it - strip the lateral component unless we are
		//critically low and escaping.
		if (NewBotAI_ShouldAvoidDiagonalWallrun(bs))
		{
			return;
		}
		NewBotAI_ApplyRandomStrafePattern(bs);
	}
}

qboolean NewBotAI_IsEnemyPullable(bot_state_t *bs) {
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return qfalse;
	if (bs->cur_ps.fd.forcePower < 20)
		return qfalse;

	if (BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim)) 
		return qtrue;
	if (bs->currentEnemy->client->ps.groundEntityNum != ENTITYNUM_NONE - 1)
		return qtrue;
	if (bs->currentEnemy->client->ps.fd.forcePower < 20)
		return qtrue;
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_DRAIN))
		return qtrue;
	if (BG_InRoll3(bs->currentEnemy->client->ps.legsAnim))
		return qtrue;
	if (bs->currentEnemy->client->ps.saberInFlight) //no blade in hand to defend the pull
		return qtrue;

	return qfalse;
}

int NewBotAI_GetPull(bot_state_t *bs) {
	const int ourHealth = g_entities[bs->client].health, hisHealth = bs->currentEnemy->health, ourForce = bs->cur_ps.fd.forcePower;
	const int hisForce = bs->currentEnemy->client->ps.fd.forcePower;
	const float drainlockBias = BotGetChanceBiasPercent(bot_drainlockbias.value);
	const qboolean freePullkickWindow = NewBotAI_HasFreePullkickWindow(bs);
	const qboolean stabilizeVsSaberThrow = NewBotAI_ShouldStabilizeAgainstEnemySaberThrow(bs);
	int healthDiff = ourHealth - hisHealth;
	float weight = (float)healthDiff;
	int ptkWeight = 0;
	if (g_forcePowerDisable.integer & (1 << FP_PULL))
		return 0;
	if  (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PULL)))
		return 0;
	if (bs->frame_Enemy_Len > 640) //Check pull range..
		return 0;
	if (bs->frame_Enemy_Len < 50)
		return 0; //dont need to pull, we are so close
	if (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE)
		return 0; //pull-kicks must be initiated from the ground
	if (NewBotAI_ShouldPreDefenseAgainstCollapse(bs) && ourForce < 55)
		return 0;
	if (g_flipKick.integer && NewBotAI_CanAttemptFlipkick(bs) && bs->frame_Enemy_Len <= NEWBOTAI_IMMEDIATE_FLIPKICK_RANGE)
		return 0; //already in immediate flipkick range: don't overshoot by pulling
	if (!bs->frame_Enemy_Vis)
		return 0;
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return 0;
	if (ourForce < 21)
		return 0;
	if (stabilizeVsSaberThrow)
		return 0;
	//Same rule as push: never trade our block for a pull while a lethal throw is inbound.
	if (NewBotAI_IsIncomingSaberThrowLethal(bs))
		return 0;
	if (bs->currentEnemy->client->ps.saberInFlight && !freePullkickWindow)
		return 0;
	if (NewBotAI_ShouldPlaySafeDrainVsSaberThrow(bs) &&
		NewBotAI_IsEnemySaberThreatImminent(bs) &&
		!freePullkickWindow)
		return 0;
	if (ourHealth < 51 &&
		(bs->currentEnemy->client->ps.saberInFlight || NewBotAI_IsEnemySaberThreatImminent(bs)))
		return 0;
	if (NewBotAI_IsStandingRoll(bs->cur_ps.legsAnim) && !NewBotAI_ShouldEmergencyDrainRollSaberThrow(bs))
		return 0;

	if (weight < 1)
		weight = 1;

	ptkWeight = NewBotAI_GetPTKWeight(bs);
	if (ptkWeight > 0 && NewBotAI_ShouldSkipPullForNaturalFlipkickPTK(bs))
	{
		//PTK is already likely to land from current proximity/momentum, so keep the
		//20 FP pull for follow-up pressure and let kick/throw paths handle the damage.
		return 0;
	}
	if (ptkWeight > 0 && ourForce > 25 && ourForce < NEWBOTAI_PTK_FORCE_BUDGET)
	{
		//PTK-weighted window but not enough bank for the full pull+throw budget yet:
		//conserve briefly and re-enter PTK once we cross the budget threshold.
		return 0;
	}
	if (ptkWeight > 0 && !NewBotAI_IsStablePTKCommitWindow(bs) && hisHealth > 25)
	{
		/* Commit-quality gate: only force PTK when range/timing lane is stable. */
		return 0;
	}
	if (ptkWeight <= 0 && ourForce <= 30 && hisForce >= ourForce)
	{
		//No strong PTK conversion window and reserve is low: keep FP for exits/recovery.
		return 0;
	}

	if (bs->currentEnemy->client->ps.saberInFlight) {
		const qboolean enemySaberReturning = NewBotAI_IsEnemySaberReturning(bs);

		//They've committed to a saber throw and have no blade in hand to defend a pull -
		//this is the ideal pullkick (pull + flipkick, no throw of our own) window. While
		//the saber is coming back to their hand, PTK should be the first choice and a plain
		//pullkick the second - keep the base pull high here and let PTK's own weight stack on
		//top below when it is available.
		if (enemySaberReturning) {
			weight = (ourHealth > 30 && ourForce > bs->currentEnemy->client->ps.fd.forcePower) ? 90.0f : 75.0f;
		}
		else {
			weight = (ourHealth > 30 && ourForce > bs->currentEnemy->client->ps.fd.forcePower) ? 75.0f : 65.0f;
		}
	}

	if (NewBotAI_IsBeingPulledTowardEnemy(bs) && bs->frame_Enemy_Len <= NEWBOTAI_PULL_STUN_ONLY_RANGE)
	{
		weight += freePullkickWindow ? 75.0f : 35.0f;
	}
	else if (NewBotAI_IsBeingPulledTowardEnemy(bs))
	{
		float timeToKickRange = NewBotAI_GetPullkickTimeToKickRange(bs, qtrue, NULL);
		if (timeToKickRange >= 0.0f &&
			timeToKickRange <= NewBotAI_GetPullkickDefensiveReactionWindowMs())
		{
			weight += freePullkickWindow ? 55.0f : 25.0f;
		}
	}

	//We've pressed forward past our own thrown saber and are now the closer, saberless
	//one on the approach to the enemy - lean into pullkick (and antidrain, see
	//NewBotAI_GetAntiDrainWeight) especially if the enemy still has force to drain us
	//with, otherwise still favor the pullkick if they're not at full health.
	if (NewBotAI_IsBetweenOwnSaberAndEnemy(bs)) {
		if (bs->currentEnemy->client->ps.fd.forcePower > 20) {
			weight += 40.0f;
		}
		else if (hisHealth < 70) {
			weight += 25.0f;
		}
	}

	if ((bs->currentEnemy->client->ps.saberMove > 1) && bs->currentEnemy->client->ps.fd.saberAnimLevel != SS_STRONG)
		weight *= 0.2f; //dont pull red vert swings into us unless we its really important

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)
		weight *= 0.1f; //Dont cancel a charge unless its important

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB)) //less weight if we don't regen fp..
		weight *= 0.5f;
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT))
		weight *= 0.5f;

	if (bs->currentEnemy->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		weight += 35.0f;
		if (ourForce > hisForce)
		{
			weight += 25.0f;
		}
	}

	if (bs->lastGripkickSuccessTime > level.time - 3000)
	{
		weight += 35.0f;
	}
	if (bs->cur_ps.fd.forcePowerSelected == FP_PULL &&
		bs->lastGripkickSuccessTime < level.time - 3500)
	{
		weight *= 0.85f;
	}
	if (ourForce < 35 && hisForce >= ourForce && hisHealth > 30)
	{
		weight *= 0.75f;
	}

	if ((hisForce < 20 || NewBotAI_IsPullkickDrainWindow(bs)) && ourForce > hisForce)
	{
		weight += 20.0f + (drainlockBias * 0.35f);
	}

	if (bs->frame_Enemy_Len < 200 && ourForce >= 20) { //Pulling their weapon should be top priority always
		if (bs->currentEnemy->client->ps.weapon >= WP_BLASTER)
			return 100;
	}

	if (NewBotAI_IsEnemyPullable(bs) && (bs->cur_ps.weapon == WP_SABER || bs->cur_ps.weapon == WP_MELEE) && g_flipKick.integer) {
		if (hisHealth <= 20 && bs->frame_Enemy_Len < 250) {//Check for the insta kill, this should be better maybe... on ground pullablable should be a diff range than in air pullable
			//Com_Printf("pullable 2\n");
			return 100;
		}
		//Item 5: drainlock finisher - the enemy's force is already below the free-pullkick
		//threshold and they're in range, so land the free pullkick right now instead of
		//tapping drain again or doing anything else. This is what lets the bot alternate
		//drain (bring them back under 20) and pullkick (cash it in) until they're dead or
		//the force advantage is lost.
		if (bs->currentEnemy->client->ps.fd.forcePower < 20 && bs->frame_Enemy_Len < 250) {
			return 100;
		}
		if (BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim)) {
			//Item 9: a knocked-down target about to drop (<19 HP) should be finished with a
			//pullkick alone - no saber throw - but only once they are actually within
			//pullkick range (pull range is 640).
			if (hisHealth < 19 && bs->frame_Enemy_Len < 640) {
				return 100;
			}
			//Com_Printf("pullable 3\n");
			return (int)(weight * 2) + ptkWeight;
		}
		//Com_Printf("pullable 1\n");
		if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE) {
			if (bs->frame_Enemy_Len < 250 && ourForce > 32)
				return (int)weight + ptkWeight;
		}
		else
			return (int)weight + ptkWeight;
	}
	else { //When should we pull stun?
		//Lets say they should be on the same plane roughly..
		float heightDiff = bs->cur_ps.origin[2] - bs->currentEnemy->client->ps.origin[2]; //Us - them.  Positive means we are higher.
		if (heightDiff > -20 && heightDiff < 40) {//If we are less than 20 above or less than 40 below)
			if (bs->frame_Enemy_Len < 100 && ourForce >= 60) { //Close enough and enough force
				weight = (float)ourForce * 0.1f;
				//Com_Printf("weight: %i\n", weight);
				return (int)weight + ptkWeight;
			}
		}

	}

	return 0;
}

int NewBotAI_GetPush(bot_state_t *bs) {
	const int ourHealth = g_entities[bs->client].health, ourForce = bs->cur_ps.fd.forcePower;
	const int healthLead = ourHealth - bs->currentEnemy->health;
	const qboolean absorbAboutToEnd =
		((bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB)) &&
		 (ourForce <= 35 ||
		  (bs->cur_ps.fd.forcePowerDuration[FP_ABSORB] > level.time &&
		   bs->cur_ps.fd.forcePowerDuration[FP_ABSORB] - level.time <= NEWBOTAI_ABSORB_BAIT_WINDOW_MS))) ? qtrue : qfalse;

	if (g_forcePowerDisable.integer & (1 << FP_PUSH))
		return 0;
	if  (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH)))
		return 0;
	if (bs->frame_Enemy_Len > 640)
		return 0;
	if (!bs->frame_Enemy_Vis)
		return 0;
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return 0;
	if (ourForce < 20)
		return 0;
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT)) //we can tank the dmg..
		return 0;
	if (NewBotAI_ShouldPlaySafeDrainVsSaberThrow(bs) &&
		NewBotAI_IsEnemySaberThreatImminent(bs))
		return 0;
	//A throw that would kill us must never be answered with a hand-extend: spending push
	//here drops our block for the whole animation. Keep the saber up and evade instead.
	if (NewBotAI_IsIncomingSaberThrowLethal(bs))
		return 0;

	if (NewBotAI_GetAntiDarkPushBonus(
		bs->currentEnemy->client->ps.fd.forceSide == FORCE_DARKSIDE,
		absorbAboutToEnd,
		healthLead,
		bs->frame_Enemy_Len,
		NEWBOTAI_PULL_STUN_ONLY_RANGE) > 0)
	{
		return NewBotAI_GetAntiDarkPushBonus(
			bs->currentEnemy->client->ps.fd.forceSide == FORCE_DARKSIDE,
			absorbAboutToEnd,
			healthLead,
			bs->frame_Enemy_Len,
			NEWBOTAI_PULL_STUN_ONLY_RANGE);
	}

	if (NewBotAI_IsEnemyPullable(bs) && (ourHealth < 25) && (bs->frame_Enemy_Len < 160) && (bs->currentEnemy->client->ps.weapon == WP_SABER)) {
		if (bs->currentEnemy->client->ps.groundEntityNum == ENTITYNUM_NONE)		//improve this, only if they are coming at us or in air?
			return 100; //Only time we should push atm is to get them off us.. to prevent the flipkick
	}

	return 0;
}

// Updates the latched heal-driven drainlock state. Once triggered by a health disadvantage,
// it persists on the same enemy until the bot tops off or becomes reckless enough to give
// the heal up early.
static void NewBotAI_UpdateHealDrainlockState(bot_state_t *bs)
{
	float aggressionBias;
	int ourHealth;
	int enemyNum;

	if (!bs)
	{
		return;
	}

	if (!bs->currentEnemy || !bs->currentEnemy->client)
	{
		bs->healDrainlockActive = qfalse;
		bs->healDrainlockTargetNum = ENTITYNUM_NONE;
		return;
	}

	aggressionBias = BotGetAggressionBias(bs);
	ourHealth = g_entities[bs->client].health;
	enemyNum = bs->currentEnemy->s.number;

	if (ourHealth >= 100)
	{
		bs->healDrainlockActive = qfalse;
		bs->healDrainlockTargetNum = ENTITYNUM_NONE;
		return;
	}

	//Only extremely aggressive bots should give up a heal-driven drainlock before topping off.
	if (aggressionBias >= 0.75f)
	{
		bs->healDrainlockActive = qfalse;
		bs->healDrainlockTargetNum = ENTITYNUM_NONE;
		return;
	}

	if (bs->healDrainlockActive && bs->healDrainlockTargetNum == enemyNum)
	{
		return;
	}

	if (NewBotAI_GetTotalHealthDelta(bs) < 0)
	{
		bs->healDrainlockActive = qtrue;
		bs->healDrainlockTargetNum = enemyNum;
		return;
	}

	bs->healDrainlockActive = qfalse;
	bs->healDrainlockTargetNum = ENTITYNUM_NONE;
}

static qboolean NewBotAI_ShouldHealDrainlock(bot_state_t *bs)
{
	if (!bs || !bs->healDrainlockActive || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	return (bs->healDrainlockTargetNum == bs->currentEnemy->s.number) ? qtrue : qfalse;
}

// True when either a heal-driven drainlock should commit to a deep drain regardless of
// cvar bias, or a big enough force lead (>=40 FP over the enemy) plus bot_drainlockbias
// should push the bot into the same long-held, efficient deep-drain targeting. See
// NewBotAI_GetDrainTapTargetCost; IsPullkickDrainWindow/IsDrainlockAdvantage still gate
// the follow-up pullkick separately.
static qboolean NewBotAI_ShouldDrainlockDeep(bot_state_t *bs)
{
	const float drainlockBias = BotGetChanceBiasPercent(bot_drainlockbias.value);
	const int ourForce = bs->cur_ps.fd.forcePower;
	const int hisForce = bs->currentEnemy->client->ps.fd.forcePower;
	int requiredLead;

	NewBotAI_UpdateHealDrainlockState(bs);
	if (NewBotAI_ShouldHealDrainlock(bs))
	{
		return qtrue;
	}

	if (drainlockBias <= 0.0f)
	{
		return qfalse;
	}

	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_DRAIN)))
	{
		return qfalse;
	}

	requiredLead = 80 - (int)(drainlockBias * 0.4f);
	if (requiredLead < 40)
	{
		requiredLead = 40;
	}

	return ((ourForce - hisForce) >= requiredLead) ? qtrue : qfalse;
}

// Computes exactly how many drain ticks a targeted drain tap should hold against the
// current enemy. Normally this targets landing them safely under the free-pullkick
// threshold (19 FP, with a 1 FP margin for their regen tick ticking in slightly later
// than ours) - the "20, 19, 18..." thresholds. Each 5 FP we spend draining removes 4
// enemy FP (3 with FT_DRAINDMGNERF), so e.g. 15 FP spent removes 12, 20 removes 16, and
// so on. When we hold a big enough force lead instead (see NewBotAI_ShouldDrainlockDeep /
// bot_drainlockbias), the bot commits to a long, deep drain that pushes the enemy as
// close to 0 FP as it can in whole ticks - without wasting any of a tick's removal by
// stopping short at a fixed target - landing anywhere from 0 up to (fpPerTick-1) FP left
// (e.g. as low as 3 with the standard 4 FP/tick rate) rather than always 18. Returns 0
// when the enemy is already below the relevant threshold or the values can't be
// determined.
static int NewBotAI_GetDrainTapTargetTicks(bot_state_t *bs)
{
	int hisForce;
	int fpPerTick;
	int fpToRemove;
	int ticks;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return 0;
	}

	hisForce = bs->currentEnemy->client->ps.fd.forcePower;
	fpPerTick = (g_tweakForce.integer & FT_DRAINDMGNERF) ? 3 : 4;

	if (NewBotAI_ShouldDrainlockDeep(bs))
	{
		if (hisForce <= 0)
		{
			return 0;
		}

		ticks = (hisForce + fpPerTick - 1) / fpPerTick;
		return ticks;
	}

	if (hisForce < 19)
	{
		return 0;
	}

	fpToRemove = hisForce - 18; //land them at 18, safely below 19 to survive their regen tick
	ticks = (fpToRemove + fpPerTick - 1) / fpPerTick;

	return ticks;
}

static int NewBotAI_GetDrainTapTargetCost(bot_state_t *bs)
{
	return NewBotAI_GetDrainTapTargetTicks(bs) * 5; //drain self-cost is 5 FP per tick
}

// True when this bot is in "force bias with pullkick weights" mode: aggressive, PTK-weighted,
// knows pull, and has enough FP to pay the drain tap needed to bring the enemy below 19 while
// keeping the 20 FP required for the pull follow-up. In that window draining is the efficient
// setup for a free pullkick, and the bot should keep re-tapping drain to hold the enemy in
// this "drainlocked" state (see NewBotAI_GetSaberthrow's suppression of throws in this window)
// for as long as it retains its aggression bias/force advantage.
static qboolean NewBotAI_IsPullkickDrainWindow(bot_state_t *bs)
{
	const int drainTapTargetCost = NewBotAI_GetDrainTapTargetCost(bs);
	const int hisForce = (bs && bs->currentEnemy && bs->currentEnemy->client) ? bs->currentEnemy->client->ps.fd.forcePower : 0;

	if (drainTapTargetCost <= 0)
	{
		return qfalse;
	}

	if (hisForce < 20)
	{
		return qfalse;
	}

	if (BotGetAggressionBias(bs) <= -0.35f)
	{
		return qfalse;
	}

	if (NewBotAI_GetPTKWeight(bs) <= 0)
	{
		return qfalse;
	}

	if (g_forcePowerDisable.integer & (1 << FP_PULL))
	{
		return qfalse;
	}

	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PULL)))
	{
		return qfalse;
	}

	if (bs->cur_ps.fd.forcePower <= hisForce)
	{
		return qfalse;
	}

	if (bs->cur_ps.fd.forcePower < drainTapTargetCost + 20)
	{
		return qfalse;
	}

	return qtrue;
}

// Item 5: true while the bot should be actively holding a drainlock on the current enemy -
// chasing them down and keeping them tapped under 20 FP so every pull is a free pullkick,
// alternating drain and pullkick (see NewBotAI_GetDrain/NewBotAI_GetPull) until the enemy is
// defeated or the bot loses its force advantage. Covers both halves of that loop: the enemy
// is already under the free-pullkick threshold (cash in the pullkick now), or we still hold
// enough of a force/aggression edge to drain them back below it (NewBotAI_IsPullkickDrainWindow).
static qboolean NewBotAI_IsDrainlockAdvantage(bot_state_t *bs)
{
	const int ourForce = bs->cur_ps.fd.forcePower;
	const int hisForce = bs->currentEnemy->client->ps.fd.forcePower;

	if (!bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (BotGetAggressionBias(bs) <= -0.35f)
	{
		return qfalse;
	}

	if (ourForce <= hisForce)
	{
		return qfalse;
	}

	return (hisForce < 20 || NewBotAI_IsPullkickDrainWindow(bs)) ? qtrue : qfalse;
}

int NewBotAI_GetDrain(bot_state_t *bs) {
	const int ourHealth = g_entities[bs->client].health, ourForce = bs->cur_ps.fd.forcePower, hisForce = bs->currentEnemy->client->ps.fd.forcePower;
	const int hisHealth = bs->currentEnemy->health;
	const int totalHealthDelta = NewBotAI_GetTotalHealthDelta(bs);
	const qboolean safeDrainVsThrow = NewBotAI_ShouldPlaySafeDrainVsSaberThrow(bs);
	const qboolean pressureDrainVsThrow = (bs->currentEnemy->client->ps.saberInFlight &&
		NewBotAI_IsEnemySaberReturning(bs) &&
		!BG_SaberInAttack(bs->currentEnemy->client->ps.saberMove) &&
		totalHealthDelta >= 30) ? qtrue : qfalse;
	const qboolean returnWindowPTKAvailable =
		(pressureDrainVsThrow && NewBotAI_GetPTKWeight(bs) > 0) ? qtrue : qfalse;
	int drainTapTargetCost;
	qboolean healDrainlock;
	qboolean continuingLatchedHealDrain;
	int weight = 100;
	vec3_t a_fo;

	if (g_forcePowerDisable.integer & (1 << FP_DRAIN))
		return 0;
	if  (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_DRAIN)))
		return 0;
	if (bs->cur_ps.saberInFlight)
		return 0; //never drain while our saber is mid-flight
	if (bs->frame_Enemy_Len > MAX_DRAIN_DISTANCE)
		return 0;
	if (NewBotAI_ShouldPreDefenseAgainstCollapse(bs) && !NewBotAI_IsPullkickDrainWindow(bs))
		return 0;
	if (ourForce < 25)
		return 0;
	if (hisForce == 0)
		return 0;
	if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT && bs->cur_ps.weaponChargeTime > 700) //don't drain if we are at a charge
		return 0;

	NewBotAI_UpdateHealDrainlockState(bs);
	healDrainlock = NewBotAI_ShouldHealDrainlock(bs);
	drainTapTargetCost = NewBotAI_GetDrainTapTargetCost(bs);
	continuingLatchedHealDrain = (healDrainlock && bs->drainHoldTime > 0) ? qtrue : qfalse;
	if (NewBotAI_IsPullkickDrainWindow(bs))
	{
		if (NewBotAI_IsEnemySaberThreatImminent(bs) || bs->currentEnemy->client->ps.saberInFlight)
			return 0;
		//Force-biased, PTK-weighted bot with enough FP: drain exactly enough to put them
		//below 19 so the follow-up pullkick is free. Strong weight so this beats other powers.
		return 90;
	}

	if (safeDrainVsThrow)
	{
		if (hisForce < 20 && !NewBotAI_ShouldPreferFlipkickOverThrow(bs))
			return 0;
		weight = 110;
		if (NewBotAI_ShouldPreferFlipkickOverThrow(bs))
			weight += 15;
		if (hisForce >= 20)
			weight += 10;
		return weight;
	}
	if (pressureDrainVsThrow)
	{
		//During the saber's return-to-hand window the enemy is at their most pull-vulnerable:
		//prefer PTK first and plain pullkick second, instead of spending the turn on a drain
		//that leaves the opening unused. Lower the drain weight here instead of disabling it
		//outright so it still remains a fallback when pull loses later comparisons.
		if (!bs->frame_Enemy_Vis)
			return 0;
		VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
		vectoangles(a_fo, a_fo);
		if (!InFieldOfVision(bs->viewangles, 60, a_fo))
			return 0;
		if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
			return 0;
		if (NewBotAI_IsEnemySaberThreatImminent(bs))
			return 0;
		weight = returnWindowPTKAvailable ? 55 : 95;
		if (hisForce >= 20)
			weight += returnWindowPTKAvailable ? 5 : 10;
		return weight;
	}
	if (bs->frame_Enemy_Len < 120 &&
		(BG_SaberInAttack(bs->currentEnemy->client->ps.saberMove) ||
		 NewBotAI_IsEnemySaberThreatImminent(bs) ||
		 bs->currentEnemy->client->ps.saberInFlight))
		return 0;
	if (!bs->frame_Enemy_Vis)
		return 0;
	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
	vectoangles(a_fo, a_fo);
	if (!InFieldOfVision(bs->viewangles, 60, a_fo))
		return 0;
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return 0;
	if (NewBotAI_IsEnemySaberThreatImminent(bs))
		return 0;
	if (bs->currentEnemy->client->ps.saberInFlight)
		return 0;
	if (!healDrainlock && ourForce < 35 && hisForce >= ourForce)
		return 0;

	if (healDrainlock && drainTapTargetCost > 0 &&
		(ourForce >= drainTapTargetCost || continuingLatchedHealDrain))
	{
		weight = 100 + (-totalHealthDelta);
		if (weight > 140)
		{
			weight = 140;
		}
		return weight;
	}

	if (NewBotAI_GetAntiDarkDrainBonus(
		bs->currentEnemy->client->ps.fd.forceSide == FORCE_DARKSIDE,
		(bs->currentEnemy->client->ps.fd.forcePowersKnown & (1 << FP_HEAL)) ? 1 : 0,
		ourHealth - hisHealth,
		(hisHealth < bs->currentEnemy->client->ps.stats[STAT_MAX_HEALTH]) ? 1 : 0) > 0)
	{
		weight = NewBotAI_GetAntiDarkDrainBonus(
			bs->currentEnemy->client->ps.fd.forceSide == FORCE_DARKSIDE,
			(bs->currentEnemy->client->ps.fd.forcePowersKnown & (1 << FP_HEAL)) ? 1 : 0,
			ourHealth - hisHealth,
			(hisHealth < bs->currentEnemy->client->ps.stats[STAT_MAX_HEALTH]) ? 1 : 0);
		if (hisForce >= 25)
		{
			weight += 10;
		}
		if (weight > 120)
		{
			weight = 120;
		}
		if (bs->cur_ps.fd.forcePowerSelected == FP_DRAIN && ourForce < 45 && hisForce < 25)
		{
			weight -= 20;
		}
		return weight;
	}

	if (ourHealth < 100)
	{
		weight = ((weight - ourHealth) + 20); //Eeee  //100 - 25 + 20 = 95
		if (ourHealth <= BotGetHealthBiasThreshold())
		{
			weight += 70 + (BotGetHealthBiasThreshold() - ourHealth);
			if (weight < 100)
			{
				weight = 100;
			}
			return weight;
		}
		weight -= 25;
		if (weight < 0)
		{
			weight = 0;
		}
		if (NewBotAI_ShouldPressAdvantage(bs))
		{
			weight -= 45;
			if (weight < 0)
			{
				weight = 0;
			}
		}
		return weight;
	}

	return 0;
}

/*
int NewBotAI_GetWait(bot_state_t *bs) { //Sometimes the best attack is nothing, like when they are trying to saberthrow you and you want to focus on blocking
	int weight = 0;
	if (bs->currentEnemy->client->ps.saberInFlight && bs->frame_Enemy_Len > 200)
		weight = 10;
	return weight;
}
*/

//Weight returned by NewBotAI_GetGrip when the enemy is speeding or mid force-jump: those
//opponents are the most exposed to a gripkick, so it must outrank every other force choice.
#define NEWBOTAI_GRIPKICK_PRIORITY_WEIGHT 400

static qboolean NewBotAI_IsEnemySpeedingOrForceJumping(bot_state_t *bs)
{
	const playerState_t *eps;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
		return qfalse;
	eps = &bs->currentEnemy->client->ps;
	if (eps->fd.forcePowersActive & (1 << FP_SPEED))
		return qtrue;
	if ((eps->fd.forcePowersActive & (1 << FP_LEVITATION)) &&
		eps->groundEntityNum == ENTITYNUM_NONE)
		return qtrue;
	return qfalse;
}

int NewBotAI_GetGrip(bot_state_t *bs) {
	//Dominant-position thresholds: a bot this healthy with this much of a force lead
	//should heavily favor gripkicking over trading swings.
	#define NEWBOTAI_GRIPKICK_DOMINANT_HEALTH 80
	#define NEWBOTAI_GRIPKICK_DOMINANT_FORCE_LEAD 50
	const int gripForceRequired = forcePowerNeeded[bs->cur_ps.fd.forcePowerLevel[FP_GRIP]][FP_GRIP];
	const int saberThrowCounterMinForce = 50;
	const int saberThrowCounterMinForceLead = 20;
	const int healthBiasThreshold = BotGetHealthBiasThreshold();
	const int ourHealth = g_entities[bs->client].health, hisHealth = bs->currentEnemy->health, ourForce = bs->cur_ps.fd.forcePower, hisForce = bs->currentEnemy->client->ps.fd.forcePower;
	const int enemySaberEntNum = bs->currentEnemy->client->ps.saberEntityNum;
	const qboolean enemyKnockedDown = BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim) ? qtrue : qfalse;
	const qboolean enemyCommittedSaberThrow = (bs->currentEnemy->client->ps.saberInFlight &&
		enemySaberEntNum > 0 &&
		enemySaberEntNum < ENTITYNUM_WORLD &&
		g_entities[enemySaberEntNum].inuse &&
		g_entities[enemySaberEntNum].s.weapon == WP_SABER &&
		g_entities[enemySaberEntNum].r.ownerNum == bs->currentEnemy->s.number &&
		g_entities[enemySaberEntNum].s.eType == ET_MISSILE &&
		bs->currentEnemy->client->saberKnockedTime <= level.time) ? qtrue : qfalse;
	int weight = 100;
	const float gripkickBias = BotGetChanceBiasPercent(bot_gripkickbias.value);
	const int aggressionBonus = BotGetAggressionWeightedBonus(bs, gripkickBias, 35, qtrue);

	if (g_forcePowerDisable.integer & (1 << FP_GRIP))
		return 0;
	if  (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_GRIP)))
		return 0;
	if (bs->frame_Enemy_Len > MAX_GRIP_DISTANCE)
		return 0;
	if (!bs->frame_Enemy_Vis)
		return 0;
	if (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB))
		return 0;
	if (ourForce <= gripForceRequired)
		return 0;
	if (NewBotAI_IsStandingRoll(bs->cur_ps.legsAnim) && !NewBotAI_ShouldEmergencyDrainRollSaberThrow(bs))
		return 0;

	//A speeding or force-jumping opponent can't dodge or answer a grip well (speed bleeds their
	//force and a mid-air target can't block or roll out): gripkick is the top choice.
	if (NewBotAI_IsEnemySpeedingOrForceJumping(bs) &&
		!BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim) &&
		!bs->cur_ps.saberInFlight)
		return NEWBOTAI_GRIPKICK_PRIORITY_WEIGHT + aggressionBonus;

	if (ourForce < saberThrowCounterMinForce)
		return 0;

	if (ourHealth <= healthBiasThreshold &&
		!(g_forcePowerDisable.integer & (1 << FP_HEAL)) &&
		(bs->cur_ps.fd.forcePowersKnown & (1 << FP_HEAL)) &&
		ourForce >= 50)
	{
		//Low-health healing takes precedence over committing into grip.
		return 0;
	}

	if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)
		weight *= 0.9f; //Dont cancel a charge unless its important

	if (hisForce < 20) {
		if (((bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_LEVITATION))) || (bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_SPEED)) || (bs->currentEnemy->client->saberKnockedTime > level.time )) {
			if (hisHealth < 52)
				return 100 + aggressionBonus;
			return (weight - 10 + aggressionBonus);
		}
	}
	
	if ((bs->currentEnemy->client->ps.saberMove > 1) && (bs->currentEnemy->client->ps.fd.saberAnimLevel == SS_STRONG && !(bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)))
		return (ourHealth - hisForce + aggressionBonus);

	//A dominant bot (high health, big force lead) should heavily favor gripkicking
	//rather than trading swings - this is our strongest, safest finisher when we can
	//clearly afford it. Weighted independently of bot_gripkickbias being left at its
	//default of 0 (that cvar only adds/removes a smaller aggression bonus on top).
	if (ourHealth > NEWBOTAI_GRIPKICK_DOMINANT_HEALTH && ourForce > hisForce + NEWBOTAI_GRIPKICK_DOMINANT_FORCE_LEAD)
		return 100 + aggressionBonus;

	//An enemy who has already committed their saber to a throw is wide open to a gripkick.
	//As long as they are still inside grip range and we are healthy enough to risk it,
	//weight the counter heavily instead of waiting for the old dominant-health threshold.
	if (!enemyKnockedDown && enemyCommittedSaberThrow &&
		!bs->cur_ps.saberInFlight && !NewBotAI_HasDroppedOwnSaber(bs) &&
		bs->frame_Enemy_Len <= MAX_GRIP_DISTANCE)
	{
		//Against committed saber throws: if we are too weak, play the safe retreat/drain
		//game; otherwise heavily favor closing and gripkicking.
		if (ourHealth <= 30 || ourForce < 45 || ourForce <= hisForce + 5)
		{
			return 0;
		}
		if (ourForce > hisForce + saberThrowCounterMinForceLead)
		{
			return (bs->frame_Enemy_Len < 240 ? 145 : 130) + aggressionBonus;
		}
	}

	if (ourForce > 65 && ourHealth > 55 && hisHealth < 80)
		return 45 + aggressionBonus;

	//Item 5: outside all of the specific rules above, still let a high bot_gripkickbias
	//show up more often as a purely random pick whenever we hold both a health and
	//force advantage - the specific rules remain the strongest/most reliable triggers,
	//this just gives bias itself real weight instead of only ever tacking on a small
	//aggression bonus to those rules.
	if (gripkickBias > 0.0f && ourHealth > hisHealth && ourForce > hisForce)
	{
		if (Q_irand(1, 100) <= (int)gripkickBias)
		{
			return (int)(gripkickBias * 0.6f) + aggressionBonus;
		}
	}

	return 0;
}

int NewBotAI_GetTeamEnergize(bot_state_t* bs) {
	int i, weight = 0, force;
	vec3_t diff;

	if (g_forcePowerDisable.integer & (1 << FP_TEAM_FORCE))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_TEAM_FORCE)))
		return 0;
	if (!BG_IsTeamGame(g_gametype.integer))
		return 0;

	g_entities[bs->client].client->ps.fd.forcePowerLevel[FP_TEAM_FORCE] = 3;//hack

	for (i = 0; i < MAX_CLIENTS; i++) {
		if (i == bs->client)
			continue;
		if (!&g_entities[i] || !g_entities[i].client || !g_entities[i].inuse || g_entities[i].health <= 0)
			continue;
		if (g_entities[i].client->ps.fd.forcePower > bs->cur_ps.fd.forcePower)
			continue;
		if (g_entities[i].health > g_entities[bs->client].health)
			continue;

		VectorSubtract(bs->cur_ps.origin, g_entities[i].client->ps.origin, diff);
		if (VectorLengthSquared(diff) > 512 * 512) //out of range
			continue;

		force = 100 - g_entities[i].client->ps.fd.forcePower;

		weight += force * 0.5f; //bots together strong
	}
	return weight;
}

static void NewBotAI_ConfigureSaberThrow(bot_state_t *bs)
{
	playerState_t *ps = &g_entities[bs->client].client->ps;
	const int disabled = (g_forcePowerDisable.integer & (1 << FP_SABERTHROW)) != 0;
	const int allowed = NewBotAI_CanUseForcePowerNow(bs, FP_SABERTHROW);
	const int throwLevel = NewBotAI_SaberThrowConfiguredLevel(bs->settings.skill,
		ps->fd.forcePowerLevel[FP_SABERTHROW], disabled, allowed);

	if (bs->settings.skill <= 2 || disabled || !allowed)
		return;
	ps->fd.forcePowerLevel[FP_SABERTHROW] = throwLevel;
	ps->fd.forcePowersKnown |= (1 << FP_SABERTHROW);
	bs->cur_ps.fd.forcePowerLevel[FP_SABERTHROW] = throwLevel;
	bs->cur_ps.fd.forcePowersKnown |= (1 << FP_SABERTHROW);
}

int NewBotAI_GetSaberthrow(bot_state_t* bs) {
	const int knockdownFinishMinHealth = 18;
	const int knockdownFinishMaxHealth = 30;
	const int knockdownBaseForceThreshold = 30;
	const int knockdownHeavyForceThreshold = 40;
	const int knockdownHeavyHealthThreshold = 50;
	const int knockdownPreGetupWeight = 120;
	const int knockdownHeavyRawHealthThreshold = 31;
	const int knockdownHeavyWeight = 100;
	const int knockdownPressureWeight = 90;
	const int knockdownBaseWeight = 85;
	const int armorForceBonusBase = 6;
	const int armorForceBonusStrongLead = 25;
	const int armorForceBonusHeavyArmor = 50;
	const int armorForceBonusStep = 4;
	const int ourHealth = g_entities[bs->client].health;
	const int ourForce = bs->cur_ps.fd.forcePower;
	const int hisForce = bs->currentEnemy->client->ps.fd.forcePower;
	const int enemyHealth = bs->currentEnemy->health;
	const int enemyArmor = bs->currentEnemy->client->ps.stats[STAT_ARMOR];
	const int enemyTotalHealth = NewBotAI_GetEnemyTotalHealth(bs);
	const int forceLead = ourForce - hisForce;
	const qboolean enemyKnockedDown = BG_InKnockDown(bs->currentEnemy->client->ps.legsAnim) ? qtrue : qfalse;
	const qboolean enemyPreGetupResponse = NewBotAI_IsEnemyPreGetupKnockdownState(bs);
	const qboolean enemyAirborne = (bs->currentEnemy->client->ps.groundEntityNum == ENTITYNUM_NONE) ? qtrue : qfalse;
	const float saberthrowBias = BotGetChanceBiasPercent(bot_saberthrowbias.value);
	const int antiDrainWeight = NewBotAI_GetAntiDrainWeight(bs);
	int weight = 0;
	qboolean counterThrow = qfalse;
	qboolean gripThrowCombo = qfalse;

	//Check if we should saberthrow I guess.
	if (bs->cur_ps.weapon != WP_SABER || bs->frame_Enemy_Len >= 400 || bs->cur_ps.saberInFlight)
		return 0;
	if ((g_forcePowerDisable.integer & (1 << FP_SABERTHROW)) ||
		bs->cur_ps.fd.forcePowerLevel[FP_SABERTHROW] <= 0 ||
		!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_SABERTHROW)))
		return 0;
	//Saber-only duels (and other restricted modes) refuse the throw outright - pressing
	//alt-attack there just wastes the frame instead of swinging.
	if (!NewBotAI_CanUseForcePowerNow(bs, FP_SABERTHROW))
		return 0;
	if (!bs->frame_Enemy_Vis)
		return 0;
	//The enemy is already committed to a saber throw. While it is about to hit us keep our
	//own saber in hand to block it; otherwise they are saberless and the counter-throw was
	//the best answer in the duel data (net +14..+27, vs -17 for a push or a jump dodge).
	//Lower levels keep the old hold-the-saber habit as a mistake.
	if (bs->currentEnemy->client->ps.saberInFlight)
	{
		const int enemyThrowKey = G_BotLearnLastTokenTime(bs->currentEnemy->s.number, BOTLEARN_TOK_THROW);

		if (bs->counterThrowRollKey != enemyThrowKey)
		{
			bs->counterThrowRollKey = enemyThrowKey;
			bs->counterThrowMistake = (Q_irand(1, 100) <= NewBotAI_GetDecisionMistakeChance(bs)) ? qtrue : qfalse;
		}
		if (NewBotAI_IsEnemySaberThreatImminent(bs) || bs->counterThrowMistake)
			return 0;
		counterThrow = qtrue;
	}
	//Out of force: a throw that cannot be sustained just drops the saber. Human throws hit
	//at every force level, so only the real floor is vetoed here.
	if (ourForce < 30)
		return 0;
	//Too hurt to risk going saberless while the enemy holds a big force lead: even when
	//we still hold the health advantage, a throw here gives them the opening their force
	//edge needs to flip the fight. Hold the saber instead.
	if (ourHealth < 50 && (hisForce - ourForce) > 40)
		return 0;
	//If their getup push just interrupted our throw charge, drop throw priority and
	//transition into close pullkick/flipkick pressure instead.
	if (NewBotAI_ShouldAbortChargedThrowForGetupPush(bs))
		return 0;
	//Item 1: while the opponent is drainlocked (actively tapped below 19 FP for a free
	//pullkick, or already under the free-pullkick threshold), weight the pullkick over
	//the saber throw - throwing the saber away just gives up the drainlock's
	//guaranteed-hit setup for a throw they can dodge/block. The only exception is the
	//throw that is a very clear kill: otherwise keep the saber in hand and cash the
	//force advantage in with drain taps and pullkicks instead of extending the throw.
	if (NewBotAI_ShouldSuppressDrainlockSaberThrow(bs))
	{
		return 0;
	}
	//Don't hand the opponent a drain lock: if the throw's force budget would leave us
	//without enough banked to push/pull free, keep the saber (and the force) in hand.
	if (NewBotAI_WouldThrowInviteDrainlock(bs))
	{
		return 0;
	}
	if (!NewBotAI_HasSafeSaberThrowClearance(bs))
	{
		return 0;
	}
	if (bs->cur_ps.fd.saberAnimLevel == SS_STAFF)
	{
		//A staff cannot initiate saber throw. Select a throw-capable style
		//through the normal saber transition logic.
		if (bs->saberThrowTime < level.time)
		{
			Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
			bs->saberThrowTime = level.time + 350;
		}
		return 0;
	}

	//Grip -> throw: throw ~100ms (skill 7+) into the target we are holding.
	if ((bs->cur_ps.fd.forcePowersActive & (1 << FP_GRIP)) &&
		level.time - G_BotLearnLastTokenTime(bs->client, BOTLEARN_TOK_GRIP) >=
			NewBotAI_GetComboGapMs(NEWBOTAI_COMBO_GRIP_THROW, bs->settings.skill, 0,
				NewBotAI_GetDecisionMistakeChance(bs)))
	{
		gripThrowCombo = qtrue;
	}

	if (enemyKnockedDown) {
		if (enemyPreGetupResponse &&
			(enemyArmor > 0 || (enemyHealth > 0 && enemyHealth < knockdownHeavyRawHealthThreshold))) {
			weight = knockdownPreGetupWeight;
		}
		//A knocked-down opponent is the best saber-throw punish; bias heavily toward it,
		//especially when they are already under 31 raw health and the throw can cash the
		//knockdown in immediately instead of letting them recover. We keep the older
		//total-health band alongside that raw-health execute so armored targets still use
		//the broader finisher window even when their HP alone is not yet in execute range.
		else if ((enemyHealth > 0 && enemyHealth < knockdownHeavyRawHealthThreshold) ||
			(enemyTotalHealth >= knockdownFinishMinHealth && enemyTotalHealth <= knockdownFinishMaxHealth)) {
			weight = knockdownHeavyWeight;
		}
		else if (ourForce >= knockdownHeavyForceThreshold &&
			enemyTotalHealth <= knockdownHeavyHealthThreshold &&
			(forceLead > 0 || enemyArmor > 0)) {
			weight = knockdownHeavyWeight;
		}
		else if (ourForce >= knockdownHeavyForceThreshold && (forceLead > 0 || enemyArmor > 0)) {
			weight = knockdownPressureWeight;
		}
		else if (ourForce >= knockdownBaseForceThreshold) {
			weight = knockdownBaseWeight;
		}
	}

	//Never charge/hold a throw once the enemy has closed into flipkick striking range -
	//a free flipkick should always win out over sitting in an alt-attack charge that
	//just gets the two bots colliding with each other.
	if ((saberthrowBias > 0.0f || antiDrainWeight > 0) &&
		ourForce > 20 && bs->frame_Enemy_Len > 120 &&
		!NewBotAI_ShouldPreferFlipkickOverThrow(bs))
	{
		int aggressionBonus = BotGetAggressionWeightedBonus(bs, saberthrowBias, 45, qtrue);
		int finishingBonus = 0;
		int armorForceBonus = 0;
		const qboolean aggressiveFinishWindow = ((enemyHealth > 0 && enemyHealth <= 35) ||
			(enemyTotalHealth > 0 && enemyTotalHealth <= 45) ||
			(enemyKnockedDown && enemyTotalHealth <= 70)) ? qtrue : qfalse;
		const qboolean flipkickUnavailable = (!NewBotAI_CanAttemptFlipkick(bs)) ? qtrue : qfalse;
		const qboolean enemyAttacking = BG_SaberInAttack(bs->currentEnemy->client->ps.saberMove) ? qtrue : qfalse;
		const qboolean enemyUsingForce = (bs->currentEnemy->client->ps.fd.forcePowersActive != 0) ? qtrue : qfalse;
		const qboolean enemyThrowing = (bs->currentEnemy->client->ps.weapon == WP_SABER &&
			(bs->currentEnemy->client->ps.weaponstate == WEAPON_CHARGING_ALT ||
			 bs->currentEnemy->client->ps.weaponstate == WEAPON_FIRING) &&
			!BG_SaberInAttack(bs->currentEnemy->client->ps.saberMove)) ? qtrue : qfalse;
		const qboolean enemyDefenseDown = (enemyAttacking || enemyUsingForce || enemyThrowing) ? qtrue : qfalse;
		const qboolean closePTKNoPull = (NewBotAI_GetPTKWeight(bs) > 0 &&
			NewBotAI_ShouldSkipPullForNaturalFlipkickPTK(bs)) ? qtrue : qfalse;
		const qboolean enemyStableDefense = NewBotAI_IsEnemyReadyToBlockFreshSaberThrow(bs);

		if (enemyTotalHealth <= 70)
		{
			finishingBonus = 15;
		}
		if (ourHealth > 55)
		{
			finishingBonus += 10;
		}
		if (flipkickUnavailable && bs->frame_Enemy_Len > 140)
		{
			finishingBonus += 20;
		}
		//An enemy already committed to a swing/force action/throw windup has weaker defense
		//against saber throw. This is especially valuable when PTK would otherwise spend
		//20 force on an unnecessary pull despite natural flipkick proximity/momentum.
		if (enemyDefenseDown)
		{
			finishingBonus += 15;
		}
		if (closePTKNoPull && enemyDefenseDown)
		{
			finishingBonus += 20;
		}
		if ((enemyKnockedDown && enemyPreGetupResponse) ||
			(enemyAirborne && forceLead >= 0 && enemyTotalHealth <= 60))
		{
			finishingBonus += 20;
		}
		if (aggressiveFinishWindow && (enemyDefenseDown || enemyAirborne || enemyKnockedDown))
		{
			finishingBonus += 20;
		}
		if (forceLead > 0 && enemyArmor > 0)
		{
			armorForceBonus = armorForceBonusBase;
			if (forceLead >= armorForceBonusStrongLead)
			{
				armorForceBonus += armorForceBonusStep;
			}
			if (enemyArmor >= armorForceBonusHeavyArmor)
			{
				armorForceBonus += armorForceBonusStep;
			}
		}
		if (enemyStableDefense && !aggressiveFinishWindow)
		{
			return 0;
		}

		if (aggressionBonus + finishingBonus + antiDrainWeight + armorForceBonus > weight)
		{
			weight = aggressionBonus + finishingBonus + antiDrainWeight + armorForceBonus;
		}
	}

	if (counterThrow && weight < 40)
		weight = 40;
	if (gripThrowCombo && weight < 50)
		weight = 50;

	if (weight > 0)
	{
		//Situational read from the duel data: throws into a jumping target or right after
		//our own pull/swing/kick rarely landed; throws at a committed target and at our own
		//force <= 50 did. Plus the opponent reaction table and the learned sequence bonus.
		const int stimulus = NewBotAI_GetEnemyStimulusToken(bs);
		const qboolean ownActionRecent = (G_BotLearnRecentToken(bs->client, BOTLEARN_TOK_PULL, 400) ||
			G_BotLearnRecentToken(bs->client, BOTLEARN_TOK_SWING, 400) ||
			G_BotLearnRecentToken(bs->client, BOTLEARN_TOK_KICK, 400)) ? qtrue : qfalse;
		const qboolean enemyCommitted = (BG_SaberInAttack(bs->currentEnemy->client->ps.saberMove) ||
			bs->currentEnemy->client->ps.fd.forcePowersActive != 0 ||
			bs->currentEnemy->client->ps.saberInFlight) ? qtrue : qfalse;
		const float skillScale = BotLearn_SkillScale(bs->settings.skill);

		weight += (int)(NewBotAI_GetSaberThrowSituationBonus(ourForce, bs->frame_Enemy_Len,
			enemyAirborne ? 1 : 0, enemyKnockedDown ? 1 : 0, ownActionRecent ? 1 : 0,
			enemyCommitted ? 1 : 0) * skillScale);
		weight += (int)(NewBotAI_GetReactionBonus(stimulus, BOTLEARN_TOK_THROW) * skillScale);
		weight += G_BotLearnBonus(&g_entities[bs->client], bs->currentEnemy, stimulus,
			BOTLEARN_TOK_THROW, BOTLEARN_TOK_NONE, bs->settings.skill);
		if (weight < 0)
			weight = 0;
	}

	return weight;
}

// The opponent's most recent action (what we are reacting to), IDLE when nothing recent.
static int NewBotAI_GetEnemyStimulusToken(bot_state_t *bs)
{
	int token;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
		return BOTLEARN_TOK_IDLE;
	token = G_BotLearnLatestToken(bs->currentEnemy->s.number, BOTLEARN_RESPONSE_WINDOW_MS);
	return (token == BOTLEARN_TOK_NONE) ? BOTLEARN_TOK_IDLE : token;
}

// Additive weight for answering the current situation with `response`:
//  - reaction table from the duel data (scaled down for lower skills),
//  - force economy (pull while the enemy still has force, drain when they are low),
//  - drain -> pull follow-up once the skill-scaled combo gap has passed,
//  - learned sequence bonus (bot_learning).
static int NewBotAI_GetForceDecisionBonus(bot_state_t *bs, int response)
{
	const int stimulus = NewBotAI_GetEnemyStimulusToken(bs);
	const float skillScale = BotLearn_SkillScale(bs->settings.skill);
	int bonus;
	int ownDrainTime;

	if (!bs->currentEnemy || !bs->currentEnemy->client)
		return 0;

	bonus = (int)(NewBotAI_GetReactionBonus(stimulus, response) * skillScale);
	bonus += (int)(NewBotAI_GetForceEconomyBonus(response, bs->currentEnemy->client->ps.fd.forcePower) * skillScale);

	ownDrainTime = G_BotLearnLastTokenTime(bs->client, BOTLEARN_TOK_DRAIN);
	if (response == BOTLEARN_TOK_PULL && ownDrainTime > 0 &&
		level.time - ownDrainTime >= NewBotAI_GetComboGapMs(NEWBOTAI_COMBO_DRAIN_FOLLOWUP,
			bs->settings.skill, 0, NewBotAI_GetDecisionMistakeChance(bs)) &&
		level.time - ownDrainTime <= 600)
	{
		bonus += 20;
	}

	//A pull we can cash in with a kick is scored as the pull -> kick sequence.
	bonus += G_BotLearnBonus(&g_entities[bs->client], bs->currentEnemy, stimulus, response,
		(response == BOTLEARN_TOK_PULL && NewBotAI_IsPullkickOpportunity(bs)) ? BOTLEARN_TOK_KICK : BOTLEARN_TOK_NONE,
		bs->settings.skill);
	return bonus;
}

void NewBotAI_GetDSForcepower(bot_state_t *bs)
{
	vec3_t a_fo;
	qboolean useTheForce = qfalse;
	qboolean firedImmediatePull = qfalse;
	int pushWeight, pullWeight, lightningWeight, drainWeight, gripWeight;//, doNothingWeight;
	int minWeight = 0;
	qboolean longRangeLightningOnly = qfalse;
	const qboolean drainlockAdvantage = NewBotAI_IsDrainlockAdvantage(bs);
	const qboolean pullkickDrainWindow = NewBotAI_IsPullkickDrainWindow(bs);

	//Disengaged in a bot_conservation window - hold off on spending any force so it regens.
	if (bs->conserveUntil > level.time)
		return;
	if (NewBotAI_HandleRecoveryRollForcepower(bs))
		return;

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
	vectoangles(a_fo, a_fo);

	drainWeight = NewBotAI_GetDrain(bs);
	gripWeight = NewBotAI_GetGrip(bs);
	if (NewBotAI_IsEnemySaberThreatImminent(bs) &&
		NewBotAI_ShouldEmergencyDrainRollSaberThrow(bs))
	{
		NewBotAI_ApplySidewaysDrainRoll(bs, qtrue);
		return;
	}
	if (NewBotAI_IsEnemySaberThreatImminent(bs) &&
		NewBotAI_ShouldPlaySafeDrainVsSaberThrow(bs))
	{
		if (drainWeight > minWeight)
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
			trap->EA_ForcePower(bs->client);
		}
		return;
	}
	if (NewBotAI_IsEnemySaberThreatImminent(bs))
	{
		NewBotAI_ClearLightningBurst(bs);
		return;
	}
	pullWeight = NewBotAI_GetPull(bs);
	pushWeight = NewBotAI_GetPush(bs);
	lightningWeight = NewBotAI_GetLightningWeight(bs);
	if (NewBotAI_CanContinueLightningBurst(bs))
	{
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_LIGHTNING;
		trap->EA_ForcePower(bs->client);
		return;
	}
	if (bs->lightningHoldUntil > 0 && !NewBotAI_CanContinueLightningBurst(bs))
	{
		NewBotAI_ClearLightningBurst(bs);
	}
	longRangeLightningOnly = (bs->frame_Enemy_Len > BotGetLightningStartDistance(bs) &&
		NewBotAI_IsWithinLightningRange(bs)) ? qtrue : qfalse;
	if (longRangeLightningOnly)
	{
		//Beyond the effective range of pull/push/grip/drain, force selection should be
		//lightning-only.
		pullWeight = 0;
		pushWeight = 0;
		gripWeight = 0;
		drainWeight = 0;
	}
	//doNothingWeight = NewBotAI_GetWait(bs);

	//Counters/combos from the duel data plus the learned sequence weights. Only adjusts
	//powers that are already available (weight > 0) so every existing gate still applies.
	if (!longRangeLightningOnly)
	{
		if (pullWeight > 0)
			pullWeight += NewBotAI_GetForceDecisionBonus(bs, BOTLEARN_TOK_PULL);
		if (pushWeight > 0)
			pushWeight += NewBotAI_GetForceDecisionBonus(bs, BOTLEARN_TOK_PUSH);
		if (gripWeight > 0)
			gripWeight += NewBotAI_GetForceDecisionBonus(bs, BOTLEARN_TOK_GRIP);
		if (drainWeight > 0)
			drainWeight += NewBotAI_GetForceDecisionBonus(bs, BOTLEARN_TOK_DRAIN);
	}

	//Gripkick against a speeding / force-jumping enemy outranks every other force choice,
	//including the drainlock pull/drain package.
	if (!longRangeLightningOnly && gripWeight >= NEWBOTAI_GRIPKICK_PRIORITY_WEIGHT)
	{
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_GRIP;
		useTheForce = qtrue;
	}

	if (!useTheForce && !longRangeLightningOnly && bs->currentEnemy && bs->currentEnemy->client)
	{
		newbotai_drainlock_force_context_t drainlockForceContext;

		drainlockForceContext.minWeight = minWeight;
		drainlockForceContext.drainlockAdvantage = drainlockAdvantage;
		drainlockForceContext.pullkickDrainWindow = pullkickDrainWindow;
		drainlockForceContext.drainWeight = drainWeight;
		drainlockForceContext.pullWeight = pullWeight;
		drainlockForceContext.enemyForce = bs->currentEnemy->client->ps.fd.forcePower;

		switch (NewBotAI_GetDrainlockForceChoice(drainlockForceContext))
		{
		case NEWBOTAI_DRAINLOCK_FORCE_PULL:
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
			NewBotAI_ApplyPullMistake(bs);
			useTheForce = qtrue;
			NewBotAI_SchedulePullkickJump(bs);
			if (NewBotAI_IsPullkickOpportunity(bs) &&
				bs->frame_Enemy_Len <= NEWBOTAI_IMMEDIATE_FLIPKICK_RANGE &&
				NewBotAI_IsFlipkickSetupReady(bs))
			{
				trap->EA_ForcePower(bs->client);
				firedImmediatePull = qtrue;
				NewBotAI_Flipkick(bs);
			}
			break;
		case NEWBOTAI_DRAINLOCK_FORCE_DRAIN:
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
			useTheForce = qtrue;
			break;
		default:
			break;
		}
	}

	if (!useTheForce && gripWeight > minWeight &&
		gripWeight > pushWeight && gripWeight > drainWeight &&
		gripWeight > pullWeight)
	{
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_GRIP;
		useTheForce = qtrue;
	}
	else if (!useTheForce && pushWeight > pullWeight && pushWeight > drainWeight && pushWeight > gripWeight && pushWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
		useTheForce = qtrue;

		//trap->Print("Pushing -- Pull: %i, Push: %i, Drain: %i, Grip: %i\n", pullWeight, pushWeight, drainWeight, gripWeight);
	}
	else if (!useTheForce && pullWeight > pushWeight && pullWeight > drainWeight && pullWeight > gripWeight && pullWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
		NewBotAI_ApplyPullMistake(bs);
		useTheForce = qtrue;
		//Always arm the pullkick follow-through after a pull so the pull itself can create the
		//close-range window; only fire the immediate kick when the window already exists now.
		NewBotAI_SchedulePullkickJump(bs);
		if (NewBotAI_IsPullkickOpportunity(bs) &&
			bs->frame_Enemy_Len <= NEWBOTAI_IMMEDIATE_FLIPKICK_RANGE &&
			NewBotAI_IsFlipkickSetupReady(bs))
		{
			trap->EA_ForcePower(bs->client);
			firedImmediatePull = qtrue;
			NewBotAI_Flipkick(bs);
		}

		//trap->Print("Pulling -- Pull: %i, Push: %i, Drain: %i, Grip: %i\n", pullWeight, pushWeight, drainWeight, gripWeight);
	}
	else if (!useTheForce && lightningWeight > pushWeight && lightningWeight > pullWeight && lightningWeight > drainWeight && lightningWeight > gripWeight && lightningWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_LIGHTNING;
		NewBotAI_StartLightningBurst(bs);
		useTheForce = qtrue;
	}
	//Explicit tie-break: when drain ties for best weight, prefer drain over grip/push/pull
	//but not over lightning.
	else if (!useTheForce && drainWeight > lightningWeight && drainWeight >= pushWeight && drainWeight >= pullWeight && drainWeight >= gripWeight && drainWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
		useTheForce = qtrue;

		//trap->Print("Draining -- Pull: %i, Push: %i, Drain: %i, Grip: %i\n", pullWeight, pushWeight, drainWeight, gripWeight);
	}
	else if (!useTheForce && gripWeight > pushWeight && gripWeight > pullWeight && gripWeight > drainWeight && gripWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_GRIP;
		useTheForce = qtrue;

		//trap->Print("Gripping -- Pull: %i, Push: %i, Drain: %i, Grip: %i\n", pullWeight, pushWeight, drainWeight, gripWeight);
	}

	if (!useTheForce && NewBotAI_GetSpeedAttackWeight(bs) > minWeight)
	{
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_SPEED;
		useTheForce = qtrue;
	}

	//Never rage - bots don't use force rage at all anymore.

	if (!useTheForce && NewBotAI_GetTeamEnergize(bs) > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_TEAM_FORCE;
		useTheForce = qtrue;
	}

	if ((!useTheForce ||
		level.clients[bs->client].ps.fd.forcePowerSelected != FP_LIGHTNING) &&
		!(bs->cur_ps.fd.forcePowersActive & (1 << FP_LIGHTNING)))
	{
		NewBotAI_ClearLightningBurst(bs);
	}

	if (!longRangeLightningOnly)
	{
		if (NewBotAI_TryAbortChargedThrowIntoPullkick(bs))
		{
			return;
		}
	}

	//A free flipkick always beats holding/charging a throw once the enemy has closed
	//into kick range - otherwise the two bots just collide while we sit on the charge.
	if (!longRangeLightningOnly &&
		!NewBotAI_ShouldSuppressDrainlockSaberThrow(bs) &&
		!drainlockAdvantage && !pullkickDrainWindow &&
		NewBotAI_GetSaberthrow(bs) > minWeight && !NewBotAI_ShouldPreferFlipkickOverThrow(bs)) {
		trap->EA_Alt_Attack(bs->client);
		//Pre-select pull or push so it fires as the saber approaches the target. Pull when
		//aggressive (PTK setup), push when defensive (break their guard). This runs after the
		//normal force-power selection above so it can override a less useful pick.
		{
			const qboolean ptkWeighted = (BotGetAggressionBias(bs) > 0.0f) ? qtrue : qfalse;
			if (ptkWeighted && NewBotAI_HasFreePullkickWindow(bs) &&
				!(g_forcePowerDisable.integer & (1 << FP_PULL)) &&
				(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PULL)) &&
				bs->cur_ps.groundEntityNum != ENTITYNUM_NONE &&
				bs->cur_ps.fd.forcePower >= NEWBOTAI_PTK_FORCE_BUDGET &&
				!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB)) &&
				bs->frame_Enemy_Len >= 96 && bs->frame_Enemy_Len <= 640 &&
				!NewBotAI_ShouldSkipPullForNaturalFlipkickPTK(bs))
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
				NewBotAI_ApplyPullMistake(bs);
				useTheForce = qtrue;
				//PTK: time the kick jump for when the pulled enemy will actually be in
				//kick range instead of hopping the moment pull is selected.
				NewBotAI_SchedulePullkickJump(bs);
			}
			else if (!ptkWeighted && !(g_forcePowerDisable.integer & (1 << FP_PUSH)) &&
				(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH)) &&
				bs->cur_ps.fd.forcePower >= NEWBOTAI_PTK_FORCE_BUDGET &&
				bs->frame_Enemy_Len >= 96 && bs->frame_Enemy_Len <= 640)
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
				useTheForce = qtrue;
			}
		}
	}

	if (!firedImmediatePull &&
		useTheForce &&
		NewBotAI_CanUseForcePowerNow(bs, (forcePowers_t)level.clients[bs->client].ps.fd.forcePowerSelected) &&
		bs->currentEnemy && bs->currentEnemy->client &&
		bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT &&
		(level.framenum % 2) &&
		(!bs->currentEnemy->client->invulnerableTimer || (bs->currentEnemy->client->invulnerableTimer <= level.time)))
		trap->EA_ForcePower(bs->client);
}

int NewBotAI_GetHeal(bot_state_t* bs) {
	const int ourForce = bs->cur_ps.fd.forcePower;
	const int ourHealth = g_entities[bs->client].health;
	const int healthBiasThreshold = BotGetHealthBiasThreshold();
	const qboolean pressingAdvantage = NewBotAI_ShouldPressAdvantage(bs);
	int diff = ((ourForce - ourHealth) + 101) * 0.5f; //Range is 0-100?

	if (g_forcePowerDisable.integer & (1 << FP_HEAL))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_HEAL)))
		return 0;
	//Higher diff is, higher weight to heal?

	if (ourHealth >= 100)
		return 0;
	if (ourForce < 50)
		return 0;
	if (ourHealth <= healthBiasThreshold)
	{
		diff += 40 + (healthBiasThreshold - ourHealth);
		if (pressingAdvantage)
		{
			diff = (int)(diff * 0.5f);
		}
		else if (diff < 80)
		{
			diff = 80;
		}
		if (diff < 0)
		{
			diff = 0;
		}
		else if (diff > 100)
		{
			diff = 100;
		}
		return diff;
	}
	if (pressingAdvantage)
		return 0;
	return diff;
}

int NewBotAI_GetTeamHeal(bot_state_t *bs) {
	int i, weight = 0, health;
	vec3_t diff;

	if (g_forcePowerDisable.integer & (1 << FP_TEAM_HEAL))
		return 0;
	if (!(bs->cur_ps.fd.forcePowersKnown & (1 << FP_TEAM_HEAL)))
		return 0;
	if (!BG_IsTeamGame(g_gametype.integer))
		return 0;

	for (i = 0; i < MAX_CLIENTS; i++) {
		if (i == bs->client)
			continue;
		if (!&g_entities[i] || !g_entities[i].client || !g_entities[i].inuse || g_entities[i].health <= 0)
			continue;
		if (g_entities[i].client->sess.sessionTeam != g_entities[bs->client].client->sess.sessionTeam)
			continue;
		if (g_entities[i].client->ps.fd.forcePower > bs->cur_ps.fd.forcePower)
			continue;
		if (g_entities[i].health > g_entities[bs->client].health)
			continue;

		VectorSubtract(bs->cur_ps.origin, g_entities[i].client->ps.origin, diff);
		if (VectorLengthSquared(diff) > 512 * 512) //out of range
			continue;

		health = g_entities[i].health;
		if (health > 100 || health <= 0)
			health = 100;
		weight += health * 0.5f; //bots together strong
	}
	return weight;
}

void NewBotAI_GetLSForcepower(bot_state_t *bs)
{
	vec3_t a_fo;
	qboolean useTheForce = qfalse;
	int pullWeight, pushWeight, absorbWeight, protectWeight, healWeight;
	int minWeight = 0;

	//Disengaged in a bot_conservation window - hold off on spending any force so it regens.
	if (bs->conserveUntil > level.time)
		return;
	if (NewBotAI_HandleRecoveryRollForcepower(bs))
		return;
	if (NewBotAI_IsEnemySaberThreatImminent(bs))
		return;

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
	vectoangles(a_fo, a_fo);

	pullWeight = NewBotAI_GetPull(bs);
	pushWeight = NewBotAI_GetPush(bs);
	absorbWeight = NewBotAI_GetAbsorb(bs); //why doesn't he absorb when he should
	protectWeight = NewBotAI_GetProtect(bs);
	healWeight = NewBotAI_GetHeal(bs);
	//get weights

	if (pushWeight > pullWeight && pushWeight > absorbWeight && pushWeight > protectWeight && pushWeight > healWeight && pushWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
		useTheForce = qtrue;
		//trap->Print("Push - Weights -- Pull: %i, Push: %i, Absorb: %i, Protect: %i, Heal %i\n", pullWeight, pushWeight, absorbWeight, protectWeight, healWeight);
	}
	else if (pullWeight > pushWeight && pullWeight > absorbWeight && pullWeight > protectWeight && pullWeight > healWeight && pullWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
		NewBotAI_ApplyPullMistake(bs);
		useTheForce = qtrue;
		//Always arm the pullkick follow-through after a pull so the pull itself can create the
		//close-range window; only fire the immediate kick when the window already exists now.
		NewBotAI_SchedulePullkickJump(bs);
		if (NewBotAI_IsPullkickOpportunity(bs) &&
			bs->frame_Enemy_Len <= NEWBOTAI_IMMEDIATE_FLIPKICK_RANGE &&
			NewBotAI_IsFlipkickSetupReady(bs))
			NewBotAI_Flipkick(bs);
		//trap->Print("Pull - Weights -- Pull: %i, Push: %i, Absorb: %i, Protect: %i, Heal %i\n", pullWeight, pushWeight, absorbWeight, protectWeight, healWeight);
	}
	else if (absorbWeight > pushWeight && absorbWeight > pullWeight && absorbWeight > protectWeight && absorbWeight > healWeight && absorbWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB;
		useTheForce = qtrue;
		//trap->Print("Absorb - Weights -- Pull: %i, Push: %i, Absorb: %i, Protect: %i, Heal %i\n", pullWeight, pushWeight, absorbWeight, protectWeight, healWeight);
	}
	else if (protectWeight > pushWeight && protectWeight > pullWeight && protectWeight > absorbWeight && protectWeight > healWeight && protectWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_PROTECT;
		useTheForce = qtrue;
		//trap->Print("Protect - Weights -- Pull: %i, Push: %i, Absorb: %i, Protect: %i, Heal %i\n", pullWeight, pushWeight, absorbWeight, protectWeight, healWeight);
	}
	//Explicit tie-break: only under health-bias threshold, prefer heal on ties.
	else if (((healWeight > protectWeight && healWeight > pushWeight && healWeight > pullWeight && healWeight > absorbWeight) ||
		(g_entities[bs->client].health <= BotGetHealthBiasThreshold() &&
			healWeight >= protectWeight && healWeight >= pushWeight && healWeight >= pullWeight && healWeight >= absorbWeight)) &&
		healWeight > minWeight) {
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_HEAL;
		useTheForce = qtrue;
		//trap->Print("Heal - Weights -- Pull: %i, Push: %i, Absorb: %i, Protect: %i, Heal %i\n", pullWeight, pushWeight, absorbWeight, protectWeight, healWeight);
	}
	if (!useTheForce && NewBotAI_GetSpeedAttackWeight(bs) > minWeight)
	{
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_SPEED;
		useTheForce = qtrue;
	}
	//Speed, team heal,

	if (!useTheForce && NewBotAI_GetTeamHeal(bs) > minWeight) { //make this use the weight
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_TEAM_HEAL; //ideally loop through in range people and see if they need it
		useTheForce = qtrue;
	}

	if (NewBotAI_TryAbortChargedThrowIntoPullkick(bs))
		return;

	//Check if we should saberthrow I guess.
	//A free flipkick always beats holding/charging a throw once the enemy has closed
	//into kick range - otherwise the two bots just collide while we sit on the charge.
	if (NewBotAI_GetSaberthrow(bs) > minWeight && !NewBotAI_ShouldPreferFlipkickOverThrow(bs)) {
		trap->EA_Alt_Attack(bs->client);
		//Pre-select pull or push so it fires as the saber approaches the target. Pull when
		//aggressive (PTK setup), push when defensive (break their guard). This runs after the
		//normal force-power selection above so it can override a less useful pick.
		{
			const qboolean ptkWeighted = (BotGetAggressionBias(bs) > 0.0f) ? qtrue : qfalse;
			if (ptkWeighted && NewBotAI_HasFreePullkickWindow(bs) &&
				!(g_forcePowerDisable.integer & (1 << FP_PULL)) &&
				(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PULL)) &&
				bs->cur_ps.groundEntityNum != ENTITYNUM_NONE &&
				bs->cur_ps.fd.forcePower >= NEWBOTAI_PTK_FORCE_BUDGET &&
				!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB)) &&
				bs->frame_Enemy_Len >= 96 && bs->frame_Enemy_Len <= 640 &&
				!NewBotAI_ShouldSkipPullForNaturalFlipkickPTK(bs))
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
				NewBotAI_ApplyPullMistake(bs);
				useTheForce = qtrue;
				//PTK: time the kick jump for when the pulled enemy will actually be in
				//kick range instead of hopping the moment pull is selected.
				NewBotAI_SchedulePullkickJump(bs);
			}
			else if (!ptkWeighted && !(g_forcePowerDisable.integer & (1 << FP_PUSH)) &&
				(bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH)) &&
				bs->cur_ps.fd.forcePower >= NEWBOTAI_PTK_FORCE_BUDGET &&
				bs->frame_Enemy_Len >= 96 && bs->frame_Enemy_Len <= 640)
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
				useTheForce = qtrue;
			}
		}
		if (useTheForce && (level.framenum % 2) &&
			NewBotAI_CanUseForcePowerNow(bs, (forcePowers_t)level.clients[bs->client].ps.fd.forcePowerSelected) &&
			(!bs->currentEnemy->client->invulnerableTimer || (bs->currentEnemy->client->invulnerableTimer <= level.time)))
			trap->EA_ForcePower(bs->client);
		return;
	}

	//if (bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT && (level.clients[bs->client].ps.fd.forcePowerSelected == FP_PULL) && random() > 0.5)
		//useTheForce = qfalse;

	if (useTheForce && bs->cur_ps.weaponstate != WEAPON_CHARGING_ALT &&
		NewBotAI_CanUseForcePowerNow(bs, (forcePowers_t)level.clients[bs->client].ps.fd.forcePowerSelected) &&
		(level.framenum % 2) && (!bs->currentEnemy->client->invulnerableTimer || (bs->currentEnemy->client->invulnerableTimer <= level.time))) {
		trap->EA_ForcePower(bs->client);
		//Com_Printf("Using force\n");
	}
}

void NewBotAI_DSvDS(bot_state_t *bs)
{
	NewBotAI_GetAim(bs); //If a saber is the closest entity and it is in flight, aim at it?

	if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN) {
		NewBotAI_Getup(bs);
		return;
	}

	if (bs->cur_ps.fd.forceGripBeingGripped > level.time) {//We are being gripped //bs->cur_ps.fd.forceGripCripple
		NewBotAI_ReactToBeingGripped(bs);
		return;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_GRIP)) {
		NewBotAI_Gripkick(bs);
		return;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_DRAIN)) {
		NewBotAI_Draining(bs);
		return;
	}
	if (NewBotAI_ShouldDisengageLongRangeLightningTrade(bs))
	{
		if (BotGetAggressionBias(bs) <= 0.0f && NewBotAI_HasWaypointNavigation())
		{
			NewBotAI_PrepareWaypointHandoff(bs, qfalse);
			NewBotAI_RunNavigationOrAlone(bs, 0.0f);
			return;
		}
		trap->EA_MoveForward(bs->client);
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED)) {
		NewBotAI_Speeding(bs);
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_RAGE)) {
		NewBotAI_Raging(bs);
	}

	if (bs->cur_ps.saberInFlight) {
		NewBotAI_SaberThrowing(bs);
	}

	NewBotAI_GetMovement(bs);
	if (g_forcePowerDisable.integer != 163837 && g_forcePowerDisable.integer != 163839)
		NewBotAI_GetDSForcepower(bs);
	NewBotAI_GetAttack(bs);
}

void NewBotAI_DSvLS(bot_state_t *bs)
{
	NewBotAI_GetAim(bs);

	if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN) {
		NewBotAI_Getup(bs);
		return;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_GRIP)) {
		NewBotAI_Gripkick(bs);
		return;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_DRAIN)) {
		NewBotAI_Draining(bs);//y return? y not getmovement?
		return;
	}
	if (NewBotAI_ShouldDisengageLongRangeLightningTrade(bs))
	{
		if (BotGetAggressionBias(bs) <= 0.0f && NewBotAI_HasWaypointNavigation())
		{
			NewBotAI_PrepareWaypointHandoff(bs, qfalse);
			NewBotAI_RunNavigationOrAlone(bs, 0.0f);
			return;
		}
		trap->EA_MoveForward(bs->client);
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED)) {
		NewBotAI_Speeding(bs);
	}

	NewBotAI_GetMovement(bs);
	if (g_forcePowerDisable.integer != 163837 && g_forcePowerDisable.integer != 163839)
		NewBotAI_GetDSForcepower(bs);
	NewBotAI_GetAttack(bs);
}

void NewBotAI_LSvDS(bot_state_t *bs)
{
	NewBotAI_GetAim(bs);

	if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN) {
		NewBotAI_Getup(bs);
		return;
	}
	if (bs->cur_ps.fd.forceGripBeingGripped > level.time) {
		NewBotAI_ReactToBeingGripped(bs);
		return;
	}
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED))
		NewBotAI_Speeding(bs);
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT))
		NewBotAI_Protecting(bs);
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB))
		NewBotAI_Absorbing(bs);
	if (bs->cur_ps.saberInFlight) {
		NewBotAI_SaberThrowing(bs);
	}

	NewBotAI_GetMovement(bs);
	if (g_forcePowerDisable.integer != 163837 && g_forcePowerDisable.integer != 163839)
		NewBotAI_GetLSForcepower(bs);
	NewBotAI_GetAttack(bs);
}

void NewBotAI_LSvLS(bot_state_t *bs)
{
	NewBotAI_GetAim(bs);

	if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN) {
		NewBotAI_Getup(bs);
		return;
	}

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED))
		NewBotAI_Speeding(bs);
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT))
		NewBotAI_Protecting(bs);
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB))
		NewBotAI_Absorbing(bs);

	NewBotAI_GetMovement(bs);
	if (g_forcePowerDisable.integer != 163837 && g_forcePowerDisable.integer != 163839)
		NewBotAI_GetLSForcepower(bs);
	NewBotAI_GetAttack(bs);
}

static void NewBotAI_UpdateSaberDefense(bot_state_t *bs, int time)
{
	playerState_t *ps = &g_entities[bs->client].client->ps;
	const int enemyNum = bs->currentEnemy ? bs->currentEnemy->s.number : ENTITYNUM_NONE;
	const int broken = NewBotAI_SaberDefenseCategory(
		ps->saberBlocked == BLOCKED_PARRY_BROKEN || PM_SaberInBrokenParry(ps->saberMove),
		ps->saberBlocked == BLOCKED_ATK_BOUNCE, ps->saberBlocked != BLOCKED_NONE) ==
		NEWBOTAI_SABER_DEFENSE_BROKEN;
	int enemyAttacking = 0;

	if (!g_newBotAI.integer || ps->weapon != WP_SABER || ps->pm_type != PM_NORMAL ||
		g_entities[bs->client].health <= 0 || !bs->currentEnemy || !bs->currentEnemy->client ||
		bs->currentEnemy->health <= 0 || bs->saberDefenseEnemyNum != enemyNum)
	{
		bs->saberDefenseActive = qfalse;
		bs->saberDefenseRecoveryUntil = bs->saberDefenseFollowupUntil = 0;
		bs->saberDefenseEnemyNum = enemyNum;
		bs->saberFootingValid = qfalse;
	}
	if (!g_newBotAI.integer || ps->weapon != WP_SABER || ps->pm_type != PM_NORMAL ||
		g_entities[bs->client].health <= 0 || !bs->currentEnemy || !bs->currentEnemy->client ||
		bs->currentEnemy->health <= 0)
		return;
	if (broken)
	{
		int remaining = ps->weaponTime > ps->torsoTimer ? ps->weaponTime : ps->torsoTimer;
		if (!bs->saberDefenseActive)
		{
			bs->saberFootingValid = qfalse;
			bs->saberTechniqueJumpTime = 0;
			bs->saberDefenseFollowupUntil = time + NEWBOTAI_SABER_FOLLOWUP_CLEAR_MS;
		}
		bs->saberDefenseActive = qtrue;
		if (remaining < NEWBOTAI_SABER_BROKEN_RECOVERY_MS)
			remaining = NEWBOTAI_SABER_BROKEN_RECOVERY_MS;
		bs->saberDefenseRecoveryUntil = time + remaining;
	}
	if (!bs->saberDefenseActive)
		return;
	enemyAttacking = BG_SaberInAttack(bs->currentEnemy->client->ps.saberMove) ||
		PM_SaberInStart(bs->currentEnemy->client->ps.saberMove) ||
		PM_SaberInTransition(bs->currentEnemy->client->ps.saberMove);
	if (enemyAttacking)
	{
		vec3_t relative, velocity, predicted;
		VectorSubtract(bs->currentEnemy->client->ps.origin, ps->origin, relative);
		VectorSubtract(bs->currentEnemy->client->ps.velocity, ps->velocity, velocity);
		relative[2] = velocity[2] = 0.0f;
		VectorMA(relative, NEWBOTAI_SABER_FOLLOWUP_CLEAR_MS * 0.001f, velocity, predicted);
		if (NewBotAI_SaberDefenseFollowupThreat(enemyAttacking, VectorLength(relative), VectorLength(predicted)))
			bs->saberDefenseFollowupUntil = time + NEWBOTAI_SABER_FOLLOWUP_CLEAR_MS;
	}
	if (NewBotAI_SaberDefenseReady(time, bs->saberDefenseRecoveryUntil,
		bs->saberDefenseFollowupUntil, broken, ps->weaponTime))
	{
		bs->saberDefenseActive = qfalse;
		bs->saberTechniqueReentryUntil = time + NEWBOTAI_SABER_REENTRY_MS;
		bs->saberFootingValid = qfalse;
	}
	else
	{
		bs->doAttack = bs->doAltAttack = 0;
		bs->saberTacticChainLength = 0;
		bs->saberTacticUntil = 0;
		bs->saberTechniqueReentryUntil = 0;
		bs->saberTechniqueYawTime = 0;
		bs->saberTechniqueYawOffset = 0.0f;
		bs->saberTechniqueFamilyUntil = 0;
		bs->combatAction = BOT_COMBAT_ACTION_RETREAT_DEFENSE;
	}
}

static qboolean NewBotAI_CanControlSaber(bot_state_t *bs)
{
	playerState_t *ps = &g_entities[bs->client].client->ps;
	bot_input_t queued;
	const qboolean engineBusy = (g_entities[bs->client].health <= 0 || ps->pm_type != PM_NORMAL ||
		ps->forceHandExtend != HANDEXTEND_NONE || BG_InKnockDown(ps->legsAnim) ||
		BG_InRoll(ps, ps->legsAnim) || BG_InSpecialJump(ps->legsAnim) ||
		BG_SaberInSpecialAttack(ps->torsoAnim) || BG_SaberInSpecial(ps->saberMove) ||
		ps->saberInFlight || !NewBotAI_SaberPrimaryBladeAvailable(ps->saberHolstered) ||
		ps->saberLockTime > level.time ||
		ps->m_iVehicleNum || bs->escapeYawOverrideUntil > level.time ||
		NewBotAI_IsEnemySaberThreatImminent(bs)) ? qtrue : qfalse;
	qboolean forceMovement = (NewBotAI_HasExclusiveFlipkickMovement(bs) ||
		bs->gripkickActive || bs->runningLikeASissy ||
		(ps->fd.forcePowersActive & ((1 << FP_GRIP) | (1 << FP_DRAIN) | (1 << FP_LIGHTNING))) ||
		ps->fd.forceGripBeingGripped > level.time || bs->conserveUntil > level.time ||
		bs->doAltAttack || ps->weaponstate == WEAPON_CHARGING_ALT) ? qtrue : qfalse;

	trap->EA_GetInput(bs->client, bs->thinktime, &queued);
	if ((queued.actionflags & ACTION_ALT_ATTACK) ||
		((queued.actionflags & ACTION_FORCEPOWER) &&
		 (ps->fd.forcePowerSelected == FP_GRIP || ps->fd.forcePowerSelected == FP_DRAIN ||
		  ps->fd.forcePowerSelected == FP_LIGHTNING || ps->fd.forcePowerSelected == FP_LEVITATION)))
		forceMovement = qtrue;

	return NewBotAI_SaberCanOwnInputs(ps->weapon == WP_SABER && bs->saberTechniqueCandidate,
		NewBotAI_HasValidCurrentEnemy(bs) && bs->frame_Enemy_Vis &&
		!(bs->currentEnemy->client->invulnerableTimer > level.time),
		engineBusy, forceMovement, bs->navObstacleUntil > level.time ||
		NewBotAI_IsRecoveryMovementActive(bs)) ? qtrue : qfalse;
}

static qboolean NewBotAI_SaberSafeFootworkTrace(bot_state_t *bs, const vec3_t origin, float yaw,
	int forwardMove, int rightMove, qboolean jump);

// Trace the whole body and several landing probes, not just the backward ray.
// This ordinary, briefly pressed jump never charges levitation or wallruns.
// The current opponent's body is not an obstacle: walking into them is the point of a
// committed swing, and treating them as a wall froze bots at saber's length (dueltracks2:
// 45% no-input at 40-60u vs 5% for humans). Walls, ledges and hazards still block.
static qboolean NewBotAI_SaberSafeFootwork(bot_state_t *bs, const vec3_t origin, float yaw,
	int forwardMove, int rightMove, qboolean jump)
{
	gentity_t *enemy = (bs && bs->currentEnemy && bs->currentEnemy->client &&
		bs->currentEnemy->inuse) ? bs->currentEnemy : NULL;
	int enemyContents = 0;
	qboolean safe;

	if (enemy)
	{
		enemyContents = enemy->r.contents;
		enemy->r.contents = 0;
	}
	safe = NewBotAI_SaberSafeFootworkTrace(bs, origin, yaw, forwardMove, rightMove, jump);
	if (enemy)
		enemy->r.contents = enemyContents;
	return safe;
}

static qboolean NewBotAI_SaberSafeFootworkTrace(bot_state_t *bs, const vec3_t origin, float yaw,
	int forwardMove, int rightMove, qboolean jump)
{
	vec3_t angles, forward, right, direction, end, probe, mins, maxs;
	trace_t trace, floorTrace;
	int i;
	float floorProbeZ = origin[2] + 16.0f;

	VectorSet(angles, 0, yaw, 0);
	AngleVectors(angles, forward, right, NULL);
	VectorScale(forward, (float)forwardMove, direction);
	VectorMA(direction, (float)rightMove, right, direction);
	if (VectorNormalize(direction) == 0.0f)
		return qtrue;
	VectorCopy(g_entities[bs->client].r.mins, mins);
	VectorCopy(g_entities[bs->client].r.maxs, maxs);
	if (g_entities[bs->client].client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		VectorCopy(origin, end);
		end[2] -= 512.0f;
		JP_Trace(&floorTrace, origin, NULL, NULL, end, bs->client,
			MASK_PLAYERSOLID | CONTENTS_LAVA | CONTENTS_SLIME, qfalse, 0, 0);
		if (floorTrace.startsolid || floorTrace.allsolid || floorTrace.fraction == 1.0f ||
			(floorTrace.contents & (CONTENTS_LAVA | CONTENTS_SLIME)))
			return qfalse;
		floorProbeZ = floorTrace.endpos[2] + 40.0f;
	}
	VectorMA(origin, jump ? 180.0f : 72.0f, direction, end);
	JP_Trace(&trace, origin, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
	if (trace.startsolid || trace.allsolid || trace.fraction < 1.0f)
		return qfalse;
	if (jump)
	{
		VectorCopy(origin, probe);
		probe[2] += 64.0f;
		JP_Trace(&trace, origin, mins, maxs, probe, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
		if (trace.startsolid || trace.allsolid || trace.fraction < 1.0f)
			return qfalse;
		end[2] += 64.0f;
		JP_Trace(&trace, probe, mins, maxs, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
		if (trace.startsolid || trace.allsolid || trace.fraction < 1.0f)
			return qfalse;
	}
	for (i = 1; i <= 3; i++)
	{
		VectorMA(origin, (jump ? 180.0f : 72.0f) * i / 3.0f, direction, probe);
		probe[2] = floorProbeZ;
		VectorCopy(probe, end);
		end[2] -= 80.0f;
		JP_Trace(&floorTrace, probe, NULL, NULL, end, bs->client,
			MASK_PLAYERSOLID | CONTENTS_LAVA | CONTENTS_SLIME, qfalse, 0, 0);
		if (floorTrace.startsolid || floorTrace.allsolid || floorTrace.fraction == 1.0f ||
			floorTrace.plane.normal[2] < 0.7f ||
			(floorTrace.contents & (CONTENTS_LAVA | CONTENTS_SLIME)) ||
			floorTrace.entityNum != ENTITYNUM_WORLD)
			return qfalse;
	}
	return qtrue;
}

static newbotai_saber_yaw_phase_t NewBotAI_ObserveSaberAnimation(bot_state_t *bs, playerState_t *ps)
{
	const int basicAttack = ps->saberMove >= LS_A_TL2BR && ps->saberMove <= LS_A_T2B;
	const int preparing = PM_SaberInStart(ps->saberMove) || PM_SaberInTransition(ps->saberMove);
	const int remaining = ps->torsoTimer;

	if (NewBotAI_SaberMoveAccepted(ps->saberMove, bs->saberTacticLastMove, basicAttack))
	{
		const int endQuad = saberMoveData[ps->saberMove].endQuad;
		if (NewBotAI_SaberCommittedLearningDecision(1, bs->saberTechniqueCommand.attack,
			bs->saberDefenseActive) && bs->saberLearnedStimulus != BOTLEARN_TOK_NONE)
			G_BotLearnDecision(&g_entities[bs->client], bs->currentEnemy,
				bs->saberLearnedStimulus, BOTLEARN_TOK_SWING, bs->saberLearnedFollow,
				bs->saberLearnedPreference);
		bs->saberTacticChainLength++;
		bs->saberTechniqueActualDir = NewBotAI_SaberHorizontalDirection(ps->saberMove, LS_A_L2R, LS_A_R2L);
		// Engine-imposed attacks (e.g. parry responses) can differ from our request.
		if (bs->saberTechniqueActualDir)
			bs->saberTacticStrafeDir = NewBotAI_SaberNextHorizontalDirection(
				bs->saberTechniqueActualDir, bs->saberTacticStrafeDir);
		else if (endQuad == Q_R || endQuad == Q_TR || endQuad == Q_BR)
			bs->saberTacticStrafeDir = -1;
		else if (endQuad == Q_L || endQuad == Q_TL || endQuad == Q_BL)
			bs->saberTacticStrafeDir = 1;
		bs->saberTechniqueAcceptedTime = level.time;
	}
	if (bs->saberTechniqueAnim != ps->torsoAnim ||
		bs->saberTechniqueAnimMove != ps->saberMove ||
		remaining > bs->saberTechniqueAnimRemaining || bs->saberTechniqueAnimDuration <= 0)
	{
		bs->saberTechniqueAnim = ps->torsoAnim;
		bs->saberTechniqueAnimMove = ps->saberMove;
		// Use the observed scaled timer span. Late reacquisition conservatively
		// prepares again rather than assuming a stance-independent animation duration.
		bs->saberTechniqueAnimDuration = remaining;
	}
	bs->saberTechniqueAnimRemaining = remaining;
	bs->saberTacticLastMove = ps->saberMove;
	return NewBotAI_SaberYawPhase(basicAttack, preparing,
		basicAttack && ps->weaponTime <= 0 ? 0 : remaining, bs->saberTechniqueAnimDuration);
}

static void NewBotAI_RunSaberTechniques(bot_state_t *bs)
{
	newbotai_saber_tactic_context_t context;
	newbotai_saber_tactic_t tactic;
	playerState_t *ps = &g_entities[bs->client].client->ps;
	playerState_t *enemy;
	const int basicAttack = ps->saberMove >= LS_A_TL2BR && ps->saberMove <= LS_A_T2B;
	int escapePhase;
	int attackLegal;
	newbotai_saber_yaw_phase_t phase;

	NewBotAI_UpdateSaberDefense(bs, level.time);
	if (!NewBotAI_CanControlSaber(bs))
		return;
	enemy = &bs->currentEnemy->client->ps;
	if (bs->saberTacticEnemyNum != bs->currentEnemy->s.number ||
		(bs->saberTechniqueAcceptedTime < level.time - 1800 && !basicAttack &&
		 !PM_SaberInStart(ps->saberMove) && !PM_SaberInTransition(ps->saberMove)))
	{
		bs->saberTacticEnemyNum = bs->currentEnemy->s.number;
		bs->saberTacticChainLength = 0;
		bs->saberTacticEnemyHealth = 0;
		bs->saberTacticLastHitTime = 0;
		bs->saberTacticLastMove = LS_NONE;
		bs->saberTechniqueAcceptedTime = level.time;
		bs->saberTacticGradeUntil = 0;
		bs->saberTechniqueFamilyUntil = 0;
		bs->saberTacticUntil = 0;
		bs->saberTechniqueReentryUntil = 0;
		bs->saberTechniqueAirExitUntil = 0;
		bs->saberTechniqueActualDir = 0;
		bs->saberTechniqueAnimDuration = 0;
		bs->saberTechniqueYawTime = 0;
		bs->saberFootingValid = qfalse;
		bs->saberLearnedStimulus = BOTLEARN_TOK_NONE;
	}
	if (!bs->saberTacticStrafeDir)
		bs->saberTacticStrafeDir = NewBotAI_FanStartDirection(Q_irand(1, 100));
	phase = NewBotAI_ObserveSaberAnimation(bs, ps);
	memset(&context, 0, sizeof(context));
	context.saberOnlyDuel = NewBotAI_IsSaberOnlyDuel(bs);
	context.saberCombat = 1;
	context.fanStance = ps->fd.saberAnimLevel == SS_MEDIUM || ps->fd.saberAnimLevel == SS_STAFF;
	context.skill = Com_Clampi(1, 10, (int)bs->settings.skill);
	context.ourTotalHealth = g_entities[bs->client].health + ps->stats[STAT_ARMOR];
	context.enemyTotalHealth = bs->currentEnemy->health + enemy->stats[STAT_ARMOR];
	if (bs->saberTacticEnemyHealth > context.enemyTotalHealth &&
		enemy->persistant[PERS_ATTACKER] == bs->client && basicAttack)
		bs->saberTacticLastHitTime = level.time;
	bs->saberTacticEnemyHealth = context.enemyTotalHealth;
	context.recentlyHurt = bs->lastHurtTime > 0 &&
		bs->lastHurtTime > level.time - NEWBOTAI_SABER_COUNTER_WINDOW_MS;
	context.counterReady = level.time - bs->lastHurtTime >= 120 + (10 - context.skill) * 20;
	context.enemyAttacking = BG_SaberInAttack(enemy->saberMove) ||
		PM_SaberInStart(enemy->saberMove) || PM_SaberInTransition(enemy->saberMove);
	context.enemyRecovering = !context.enemyAttacking && enemy->weaponTime > 0;
	context.enemyVulnerable = BG_InKnockDown(enemy->legsAnim) ||
		enemy->forceHandExtend == HANDEXTEND_KNOCKDOWN ||
		(enemy->saberInFlight && !NewBotAI_IsEnemySaberThreatImminent(bs));
	context.selfAttacking = basicAttack || PM_SaberInStart(ps->saberMove) ||
		PM_SaberInTransition(ps->saberMove);
	context.selfBlocked = ps->saberBlocked != BLOCKED_NONE;
	context.chainLength = bs->saberTacticChainLength;
	context.landedHit = bs->saberTacticLastHitTime > 0 &&
		bs->saberTacticLastHitTime > level.time - NEWBOTAI_SABER_LANDED_HIT_WINDOW_MS;
	{
		float range2D, predicted2D, radial2D;

		//Plan on the 2D range predicted at the swing peak; humans that landed swings started
		//them when the peak range would be 60u or less.
		if (NewBotAI_GetEnemyRangeKinematics(bs, &range2D, &predicted2D, &radial2D))
		{
			context.enemyDistance = predicted2D;
			context.closingKnown = 1;
			context.closingSpeed = radial2D;
			context.currentDistance = range2D;
		}
		else
			context.enemyDistance = bs->frame_Enemy_Len;
		context.enemyDistance = NewBotAI_SaberRangeWithHysteresis(context.enemyDistance,
			NEWBOTAI_SABER_STEP_IN_SWING_RANGE, NEWBOTAI_SABER_RANGE_HYSTERESIS, bs->saberTechniqueInReach);
		bs->saberTechniqueInReach = context.enemyDistance <= NEWBOTAI_SABER_STEP_IN_SWING_RANGE;
	}
	//Hold the choice grade through the current swing instead of re-rolling mid-swing.
	if (NewBotAI_SaberGradeShouldReroll(level.time, bs->saberTacticGradeUntil, context.selfAttacking))
	{
		bs->saberTacticGrade = NewBotAI_GetSaberChoiceGrade(context.skill,
			(int)BotGetChanceBiasPercent(bot_mistakebias.value), Q_irand(1, 100));
		bs->saberTacticGradeUntil = level.time;
	}
	if (!bs->saberDefenseActive && (!bs->saberTechniqueFamilyUntil ||
		bs->saberTechniqueFamilyUntil != bs->saberTechniqueAcceptedTime))
	{
		bs->saberLearnedStimulus = context.enemyAttacking || context.enemyRecovering ?
			BOTLEARN_TOK_SWING : BOTLEARN_TOK_IDLE;
		bs->saberLearnedFollow = context.chainLength ? BOTLEARN_TOK_SWING : BOTLEARN_TOK_NONE;
		bs->saberLearnedPreference = NewBotAI_SaberNeedsSafetyExit(context) ? 0 : Com_Clampi(-15, 15,
			G_BotLearnBonus(&g_entities[bs->client], bs->currentEnemy,
				bs->saberLearnedStimulus, BOTLEARN_TOK_SWING, bs->saberLearnedFollow,
				bs->settings.skill));
		bs->saberTechniqueFamily = NewBotAI_SelectSaberFamily(context,
			bot_fanbias.value > 0.0f ? Com_Clampi(0, 100,
				(int)BotGetChanceBiasPercent(bot_fanbias.value) + bs->saberLearnedPreference) : 0,
			Q_irand(1, 100));
		bs->saberTechniqueFamilyUntil = bs->saberTechniqueAcceptedTime;
	}
	tactic = NewBotAI_GetCorrectSaberTactic(context);
	if (!NewBotAI_SaberNeedsSafetyExit(context) && !context.enemyAttacking &&
		context.enemyRecovering && context.counterReady && bs->saberLearnedPreference > 0 &&
		context.enemyDistance <= NEWBOTAI_SABER_SWING_START_MAX_RANGE)
		tactic = NEWBOTAI_SABER_TACTIC_COUNTER;
	if (tactic != NEWBOTAI_SABER_TACTIC_RESET)
		tactic = NewBotAI_ApplySaberChoiceGrade(context, tactic,
			(newbotai_saber_grade_t)bs->saberTacticGrade);
	if (NewBotAI_SaberBurstComplete((newbotai_saber_family_t)bs->saberTechniqueFamily, context.chainLength,
		context.enemyAttacking || context.recentlyHurt || context.selfBlocked ||
		(context.ourTotalHealth < context.enemyTotalHealth && !context.landedHit)))
		tactic = NEWBOTAI_SABER_TACTIC_RESET;
	if (bs->saberTacticUntil > 0 && bs->saberTacticUntil <= level.time &&
		(ps->groundEntityNum != ENTITYNUM_NONE || bs->saberTechniqueAirExitUntil <= level.time))
	{
		bs->saberTacticChainLength = 0;
		context.chainLength = 0;
		bs->saberTacticUntil = 0;
		bs->saberTechniqueAirExitUntil = 0;
		bs->saberTechniqueReentryUntil = level.time + NEWBOTAI_SABER_REENTRY_MS;
		bs->saberTechniqueFamilyUntil = 0;
	}
	escapePhase = NewBotAI_SaberEscapePhase(level.time, bs->saberTacticUntil,
		bs->saberTechniqueReentryUntil, ps->groundEntityNum == ENTITYNUM_NONE,
		bs->saberTechniqueAirExitUntil);
	if (bs->saberDefenseActive)
	{
		tactic = NEWBOTAI_SABER_TACTIC_RESET;
		escapePhase = -1;
	}
	if (!escapePhase && tactic == NEWBOTAI_SABER_TACTIC_RESET)
	{
		bs->saberTacticUntil = level.time + NEWBOTAI_SABER_EXIT_MS;
		bs->saberTechniqueAirExitUntil = level.time + NEWBOTAI_SABER_AIR_EXIT_MAX_MS;
		bs->saberTechniqueReentryUntil = 0;
		escapePhase = -1;
	}
	attackLegal = !bs->saberDefenseActive && !context.selfBlocked && (ps->weaponTime <= 0 || context.selfAttacking) &&
		!(ps->saberMove >= LS_R_TL2BR && ps->saberMove <= LS_R_T2B) && ps->fd.forceJumpCharge == 0 &&
		!(ps->pm_flags & PMF_JUMP_HELD) &&
		NewBotAI_SaberOrdinaryAttackSafe(ps->pm_flags & PMF_DUCKED, ps->velocity[2] > 0, 0) &&
		!NewBotAI_IsJumpAttackSuppressionWindowActive(level.time,
			bs->jumpAttackGateTime, NEWBOTAI_JUMP_ATTACK_GATE_MS);
	bs->saberTechniqueCommand = NewBotAI_PlanSaberCommand(context, tactic,
		(newbotai_saber_family_t)bs->saberTechniqueFamily, context.chainLength,
		bs->saberTacticStrafeDir, ps->groundEntityNum != ENTITYNUM_NONE,
		!NewBotAI_SaberCanApplyPressure(phase, basicAttack, ps->weaponTime),
		attackLegal, escapePhase);
	if (bs->saberTechniqueCommand.attack &&
		!NewBotAI_SaberCanApplyPressure(phase, basicAttack, ps->weaponTime))
	{
		if (PM_SaberInStart(ps->saberMove))
			NewBotAI_SaberPreparationInputs(ps->saberMove - LS_S_TL2BR,
				&bs->saberTechniqueCommand.forward, &bs->saberTechniqueCommand.right);
		else if (PM_SaberInTransition(ps->saberMove))
			NewBotAI_SaberPreparationInputs(saberMoveData[ps->saberMove].chain_attack - LS_A_TL2BR,
				&bs->saberTechniqueCommand.forward, &bs->saberTechniqueCommand.right);
		NewBotAI_SaberNoStrafeFallback(NewBotAI_IsDuelStrafeSuppressed(bs),
			PM_SaberInStart(ps->saberMove) || PM_SaberInTransition(ps->saberMove),
			&bs->saberTechniqueCommand.forward, &bs->saberTechniqueCommand.right);
	}
	NewBotAI_SaberSuppressStrafe(&bs->saberTechniqueCommand, NewBotAI_IsDuelStrafeSuppressed(bs));
	if (escapePhase < 0 && bs->saberTechniqueCommand.forward < 0 &&
		NewBotAI_SaberCanEscapeJump(ps->groundEntityNum != ENTITYNUM_NONE,
			!(ps->pm_flags & PMF_JUMP_HELD) && bs->lastucmd.upmove <= 0,
			!context.selfAttacking && ps->weaponTime <= 0 && !context.selfBlocked &&
			!PM_SaberInBrokenParry(ps->saberMove) &&
			!NewBotAI_TouchingWallNotEnemy(bs), ps->fd.forceJumpCharge == 0 &&
			ps->fd.forcePower >= 10 && !(ps->fd.forcePowersActive & ((1 << FP_SPEED) | (1 << FP_LEVITATION))) &&
			ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1] <= 160000.0f,
			NewBotAI_SaberSafeFootwork(bs, ps->origin, bs->viewangles[YAW],
				bs->saberTechniqueCommand.forward, bs->saberTechniqueCommand.right, qtrue),
			bs->saberTechniqueJumpCooldown <= level.time))
	{
		bs->saberTechniqueJumpTime = level.time + 80;
		bs->saberTechniqueJumpCooldown = level.time + 1400;
		bs->jumpAttackGateTime = level.time;
	}
	bs->saberTechniqueCommand.jump = bs->saberTechniqueJumpTime > level.time;
	bs->saberTacticAction = tactic;
	bs->combatAction = escapePhase < 0 ? BOT_COMBAT_ACTION_RETREAT_DEFENSE : BOT_COMBAT_ACTION_AGGRESSION;
	bs->saberTechniqueOwnsInputs = qtrue;
	bs->doAttack = bs->saberTechniqueCommand.attack;
	bs->doAltAttack = 0;
	NewBotAI_ResetFanChain(bs);
	NewBotAI_ClearRandomStrafeOverlay(bs);
}

static void NewBotAI_SelectSaberFooting(bot_state_t *bs, playerState_t *ps,
	newbotai_saber_yaw_phase_t phase, newbotai_saber_command_t *command)
{
	vec3_t toEnemy;
	float yaw, range, predicted, radial;
	int side;
	int i;

	if (NewBotAI_SaberFootingNeedsUpdate(bs->saberFootingValid,
		bs->saberFootingPhase, phase, bs->saberFootingMove, ps->saberMove,
		bs->saberFootingTactic, bs->saberTacticAction))
	{
		bs->saberFootingPhase = phase;
		bs->saberFootingMove = ps->saberMove;
		bs->saberFootingTactic = bs->saberTacticAction;
		bs->saberFootingForward = command->forward;
		bs->saberFootingRight = command->right;
		if (bs->saberDefenseActive)
		{
			bs->saberFootingForward = -1;
			bs->saberFootingRight = bs->saberTacticStrafeDir < 0 ? -1 : 1;
		}
		else if (phase == NEWBOTAI_SABER_YAW_RECOVER &&
			NewBotAI_ShouldSidestepAfterSwing(PM_SaberInReturn(ps->saberMove), command->attack,
				NewBotAI_GetMsSinceSaberContactOnEnemy(bs),
				bs->saberTacticAction == NEWBOTAI_SABER_TACTIC_RESET))
		{
			bs->saberFootingForward = -1;
			bs->saberFootingRight = bs->saberTacticStrafeDir;
		}
		else if (phase == NEWBOTAI_SABER_YAW_ACTIVE && command->attack)
		{
			bs->saberFootingForward = NewBotAI_SaberAttackForward(
				(newbotai_saber_tactic_t)bs->saberTacticAction,
				NewBotAI_GetEnemyRangeKinematics(bs, &range, &predicted, &radial) ?
				predicted : bs->frame_Enemy_Len);
			bs->saberFootingRight = bs->saberTacticStrafeDir;
		}
		if (NewBotAI_IsDuelStrafeSuppressed(bs))
			bs->saberFootingRight = 0;
		VectorSubtract(bs->currentEnemy->r.currentOrigin, ps->origin, toEnemy);
		yaw = vectoyaw(toEnemy);
		NewBotAI_SaberWorldDirection(yaw, bs->saberFootingForward, bs->saberFootingRight,
			&bs->saberFootingWorldDir[0], &bs->saberFootingWorldDir[1]);
		bs->saberFootingWorldDir[2] = 0.0f;
		bs->saberFootingValid = qtrue;
	}
	if (VectorLengthSquared(bs->saberFootingWorldDir) < 0.01f ||
		NewBotAI_SaberSafeFootwork(bs, ps->origin, vectoyaw(bs->saberFootingWorldDir),
			1, 0, command->jump))
		return;
	// Keep the chosen lane until blocked, then try the other safe diagonal/lateral lane.
	VectorSubtract(bs->currentEnemy->r.currentOrigin, ps->origin, toEnemy);
	yaw = vectoyaw(toEnemy);
	side = bs->saberFootingRight < 0 ? -1 : 1;
	for (i = 0; i < 5; ++i)
	{
		const int forward = i < 2 ? -1 : i < 4 ? 0 : -1;
		const int right = NewBotAI_IsDuelStrafeSuppressed(bs) || i == 4 ? 0 :
			(i % 2 ? side : -side);
		if (command->attack && forward < 0 && !bs->saberDefenseActive)
			continue;
		if (NewBotAI_SaberSafeFootwork(bs, ps->origin, yaw, forward, right, command->jump))
		{
			bs->saberFootingForward = forward;
			bs->saberFootingRight = right;
			NewBotAI_SaberWorldDirection(yaw, forward, right,
				&bs->saberFootingWorldDir[0], &bs->saberFootingWorldDir[1]);
			return;
		}
	}
	VectorClear(bs->saberFootingWorldDir);
	bs->saberFootingValid = qfalse;
	command->jump = command->attack = 0;
}

static void NewBotAI_ApplySaberTechniqueInput(bot_state_t *bs, bot_input_t *bi, int time)
{
	newbotai_saber_command_t command;
	int plannedFlags = 0;
	const int ownedMask = ACTION_MOVEFORWARD | ACTION_MOVEBACK | ACTION_MOVELEFT |
		ACTION_MOVERIGHT | ACTION_JUMP | ACTION_DELAYEDJUMP | ACTION_CROUCH |
		ACTION_ATTACK | ACTION_ALT_ATTACK;
	playerState_t *ps;
	newbotai_saber_yaw_phase_t phase;
	int actualDirection;
	int selectedForward, selectedRight;
	int selecting;
	float offset;
	float pokeYaw = 0.0f;
	float wiggleYaw = 0.0f, wigglePitch = 0.0f;
	newbotai_saber_final_context_t finalContext;

	if (bs->saberTechniqueClearQueuedAttack)
		bi->actionflags = NewBotAI_SaberOwnedActionFlags(bi->actionflags, ACTION_ATTACK, 0);
	if (!bs->saberTechniqueOwnsInputs || !g_newBotAI.integer || bi->weapon != WP_SABER)
	{
		if (bs->saberTechniqueOwnsInputs)
		{
			bi->actionflags = NewBotAI_SaberOwnedActionFlags(bi->actionflags, ACTION_ATTACK, 0);
			bs->saberTechniqueClearQueuedAttack = qtrue;
			bs->saberTechniqueOwnsInputs = qfalse;
			bs->doAttack = 0;
		}
		bs->saberTechniqueYawTime = 0;
		bs->saberTechniqueAnimDuration = 0;
		return;
	}
	if (!NewBotAI_CanControlSaber(bs))
	{
		// EA_Attack may still contain our last think's request. Remove that request
		// until EA_ResetInput, but preserve the new owner's movement/force/view.
		bi->actionflags = NewBotAI_SaberOwnedActionFlags(bi->actionflags, ACTION_ATTACK, 0);
		bs->saberTechniqueClearQueuedAttack = qtrue;
		bs->doAttack = 0;
		memset(&bs->saberTechniqueCommand, 0, sizeof(bs->saberTechniqueCommand));
		bs->saberTechniqueJumpTime = 0;
		bs->saberTechniqueYawTime = 0;
		bs->saberTechniqueAnimDuration = 0;
		bs->saberTechniqueOwnsInputs = qfalse;
		return;
	}
	ps = &g_entities[bs->client].client->ps;
	phase = NewBotAI_ObserveSaberAnimation(bs, ps);
	actualDirection = (ps->saberMove == LS_A_L2R || ps->saberMove == LS_A_R2L) ?
		bs->saberTechniqueActualDir : 0;
	selecting = !NewBotAI_SaberCanApplyPressure(phase,
		ps->saberMove >= LS_A_TL2BR && ps->saberMove <= LS_A_T2B, ps->weaponTime);
	NewBotAI_SaberSelectionInputs((newbotai_saber_family_t)bs->saberTechniqueFamily,
		bs->saberTacticChainLength, bs->saberTacticStrafeDir, &selectedForward, &selectedRight);
	NewBotAI_SaberFanStanceInputs(ps->fd.saberAnimLevel == SS_MEDIUM || ps->fd.saberAnimLevel == SS_STAFF,
		(newbotai_saber_family_t)bs->saberTechniqueFamily, bs->saberTacticChainLength,
		bs->saberTacticStrafeDir, &selectedForward, &selectedRight);
	if (PM_SaberInStart(ps->saberMove))
		NewBotAI_SaberPreparationInputs(ps->saberMove - LS_S_TL2BR, &selectedForward, &selectedRight);
	else if (PM_SaberInTransition(ps->saberMove))
		NewBotAI_SaberPreparationInputs(saberMoveData[ps->saberMove].chain_attack - LS_A_TL2BR,
			&selectedForward, &selectedRight);
	else if (selecting)
		NewBotAI_SaberNoStrafeFallback(NewBotAI_IsDuelStrafeSuppressed(bs), 0,
			&selectedForward, &selectedRight);
	command = bs->saberTechniqueCommand;
	if (bs->saberDefenseActive)
	{
		command.attack = 0;
		command.forward = -1;
		command.right = bs->saberTacticStrafeDir < 0 ? -1 : 1;
	}
	command.jump = bs->saberTechniqueJumpTime > time && ps->groundEntityNum != ENTITYNUM_NONE;
	finalContext.attackSafe = !(bs->saberDefenseActive || command.jump || ps->groundEntityNum == ENTITYNUM_NONE ||
		ps->saberBlocked != BLOCKED_NONE ||
		(ps->pm_flags & PMF_JUMP_HELD) || ps->fd.forceJumpCharge > 0 ||
		ps->saberInFlight || !NewBotAI_SaberPrimaryBladeAvailable(ps->saberHolstered) ||
		NewBotAI_IsJumpAttackSuppressionWindowActive(time, bs->jumpAttackGateTime, NEWBOTAI_JUMP_ATTACK_GATE_MS)) &&
		NewBotAI_SaberOrdinaryAttackSafe(ps->pm_flags & PMF_DUCKED, ps->velocity[2] > 0,
		selecting && selectedForward > 0 && !selectedRight &&
		ps->fd.saberAnimLevel == SS_STRONG && (g_tweakSaber.integer & ST_JK2RDFA) &&
		ps->saberMove >= LS_A_TL2BR && ps->saberMove <= LS_A_T2B);
	finalContext.selecting = selecting;
	finalContext.selectedForward = selectedForward;
	finalContext.selectedRight = selectedRight;
	{
		float range2D, predicted2D, radial2D;
		finalContext.pressureForward = NewBotAI_SaberAttackForward(
			(newbotai_saber_tactic_t)bs->saberTacticAction,
			NewBotAI_GetEnemyRangeKinematics(bs, &range2D, &predicted2D, &radial2D) ?
			predicted2D : bs->frame_Enemy_Len);
	}
	finalContext.pressureRight = bs->saberTacticStrafeDir;
	finalContext.strafeSuppressed = NewBotAI_IsDuelStrafeSuppressed(bs);
	// Refresh at command frequency, not just at the slower AI decision boundary.
	command = NewBotAI_FinalizeSaberCommand(command, finalContext);
	if (!bs->saberDefenseActive && ps->saberBlocked == BLOCKED_NONE &&
		((command.attack && (command.right || bs->saberTechniqueYawTime)) ||
		(bs->saberTechniqueYawTime && ps->saberMove >= LS_R_TL2BR && ps->saberMove <= LS_R_T2B)) &&
		ps->groundEntityNum != ENTITYNUM_NONE &&
		bs->saberTacticAction != NEWBOTAI_SABER_TACTIC_RESET &&
		bs->saberTacticAction != NEWBOTAI_SABER_TACTIC_REPOSITION &&
		(bs->saberTechniqueFamily != NEWBOTAI_SABER_BASIC ||
		 (bs->saberTechniqueYawTime && fabs(bs->saberTechniqueYawOffset) > 0.001f)))
	{
		int preparationDirection = selectedForward == 0 ? selectedRight : 0;
		float sweepAmplitude;
		if (PM_SaberInStart(ps->saberMove))
			preparationDirection = NewBotAI_SaberHorizontalDirection(
				LS_A_TL2BR + ps->saberMove - LS_S_TL2BR, LS_A_L2R, LS_A_R2L);
		else if (PM_SaberInTransition(ps->saberMove))
			preparationDirection = NewBotAI_SaberHorizontalDirection(
				saberMoveData[ps->saberMove].chain_attack, LS_A_L2R, LS_A_R2L);
		sweepAmplitude = NewBotAI_GetSaberSweepAmplitude(bs->settings.skill,
			BotGetChanceBiasPercent(bot_fanbias.value));
		offset = NewBotAI_SaberYawOffsetScaled(phase,
			NewBotAI_SaberAnimationProgress(ps->torsoTimer, bs->saberTechniqueAnimDuration),
			actualDirection, preparationDirection, bs->saberTechniqueFamily != NEWBOTAI_SABER_BASIC,
			sweepAmplitude);
		if (!bs->saberTechniqueYawTime)
			bs->saberTechniqueYawOffset = 0.0f;
		bs->saberTechniqueYawOffset = NewBotAI_SaberStepYawOffsetScaled(bs->saberTechniqueYawOffset,
			offset, bs->saberTechniqueYawTime ? time - bs->saberTechniqueYawTime : 0,
			sweepAmplitude, NewBotAI_GetSaberSweepRateScaled(bs->settings.skill,
				BotGetChanceBiasPercent(bot_fanbias.value)));
		bs->saberTechniqueYawTime = time;
	}
	else
	{
		bs->saberTechniqueYawTime = 0;
		bs->saberTechniqueYawOffset = 0.0f;
	}
	if (!bs->saberDefenseActive && ps->saberBlocked == BLOCKED_NONE &&
		ps->groundEntityNum != ENTITYNUM_NONE &&
		ps->saberMove >= LS_A_TL2BR && ps->saberMove <= LS_A_T2B)
	{
		pokeYaw = NewBotAI_SaberPokeCounterYaw(phase,
			NewBotAI_SaberAnimationProgress(ps->torsoTimer, bs->saberTechniqueAnimDuration),
			actualDirection);
		NewBotAI_GetSaberActiveWiggle(phase, time - bs->saberTechniqueAcceptedTime,
			Com_Clampi(0, 2000, bot_wobbledelay.integer), bot_wobbleyaw.value,
			bot_wobblepitch.value, bot_wobblespeed.value, &wiggleYaw, &wigglePitch);
	}
	bs->saberTechniqueAppliedYaw = Com_Clamp(-60.0f, 60.0f,
		bs->saberTechniqueYawOffset + pokeYaw + wiggleYaw);
	bs->saberTechniqueAppliedPitch = Com_Clamp(-89.0f, 89.0f,
		AngleNormalize180(bi->viewangles[PITCH]) + wigglePitch) - AngleNormalize180(bi->viewangles[PITCH]);
	bi->viewangles[YAW] = NewBotAI_SaberApplyYawOffset(bi->viewangles[YAW],
		bs->saberTechniqueAppliedYaw);
	bi->viewangles[PITCH] = Com_Clamp(-89.0f, 89.0f,
		AngleNormalize180(bi->viewangles[PITCH]) + wigglePitch);
	bs->viewangles[YAW] = bi->viewangles[YAW];
	bs->viewangles[PITCH] = bi->viewangles[PITCH];
	// The final phase, side and yaw may differ from the last AI think's plan.
	// Trace that exact live command before translating it to action flags.
	VectorClear(bi->dir);
	bi->speed = 0;
	if (command.attack && selecting)
	{
		NewBotAI_SaberApplyMovementSafety(&command,
			NewBotAI_SaberSafeFootwork(bs, ps->origin, bi->viewangles[YAW],
				command.forward, command.right, command.jump));
		if (command.forward > 0) plannedFlags |= ACTION_MOVEFORWARD;
		if (command.forward < 0) plannedFlags |= ACTION_MOVEBACK;
		if (command.right > 0) plannedFlags |= ACTION_MOVERIGHT;
		if (command.right < 0) plannedFlags |= ACTION_MOVELEFT;
	}
	else
	{
		NewBotAI_SelectSaberFooting(bs, ps, phase, &command);
		VectorCopy(bs->saberFootingWorldDir, bi->dir);
		bi->speed = VectorLengthSquared(bi->dir) > 0.01f ? 400.0f : 0.0f;
	}
	if (command.jump) plannedFlags |= ACTION_JUMP;
	if (command.attack) plannedFlags |= ACTION_ATTACK;
	// EA_Move(0) does not clear directional action bits; EA_ResetInput would also
	// discard legal force/use inputs. Replace only the owned subset at the final boundary.
	bi->actionflags = NewBotAI_SaberOwnedActionFlags(bi->actionflags, ownedMask, plannedFlags);
}

// Longitudinal intent (-1 back, 0 none, 1 forward) of a final bot input.
static int NewBotAI_GetInputForward(bot_input_t *bi)
{
	vec3_t angles, forward;
	float along;

	if (bi->actionflags & ACTION_MOVEFORWARD)
		return 1;
	if (bi->actionflags & ACTION_MOVEBACK)
		return -1;
	if (bi->speed <= 0.0f)
		return 0;
	VectorSet(angles, 0, bi->viewangles[YAW], 0);
	AngleVectors(angles, forward, NULL, NULL);
	along = DotProduct(bi->dir, forward);
	return along > 0.3f ? 1 : along < -0.3f ? -1 : 0;
}

static void NewBotAI_SetInputForward(bot_input_t *bi)
{
	vec3_t angles, forward;
	float along;

	bi->actionflags &= ~ACTION_MOVEBACK;
	bi->actionflags |= ACTION_MOVEFORWARD;
	if (bi->speed <= 0.0f)
		return;
	VectorSet(angles, 0, bi->viewangles[YAW], 0);
	AngleVectors(angles, forward, NULL, NULL);
	along = DotProduct(bi->dir, forward);
	if (along < 0.0f)
		VectorMA(bi->dir, -along, forward, bi->dir);
}

// Final input boundary for saber duels: carry the previous owner's forward intent across a
// controller <-> force/fan ownership change, and keep high-skill swing starts off the
// backpedal (humans never started a swing while backpedalling).
static void NewBotAI_ApplySaberHandover(bot_state_t *bs, bot_input_t *bi, int time)
{
	playerState_t *ps;
	const int owner = bs->saberTechniqueOwnsInputs ? 1 : 0;
	int forward;
	int adjusted;
	qboolean escape;

	if (bs->saberTechniqueOwnsInputs || bs->saberDefenseActive ||
		!g_newBotAI.integer || bi->weapon != WP_SABER || !bs->currentEnemy ||
		!bs->currentEnemy->client || bs->currentEnemy->health <= 0 ||
		//Fan holds need pure strafe to select the horizontal swing; kick/grip
		//sequences own their movement exclusively.
		bs->fanPhase == FAN_PHASE_HOLD || NewBotAI_HasExclusiveFlipkickMovement(bs) ||
		(g_entities[bs->client].client->ps.fd.forcePowersActive & (1 << FP_GRIP)))
	{
		bs->saberHandoverOwner = owner;
		bs->saberHandoverTime = 0;
		bs->saberLastForward = 0;
		return;
	}
	ps = &g_entities[bs->client].client->ps;
	forward = NewBotAI_GetInputForward(bi);
	if (owner != bs->saberHandoverOwner)
	{
		bs->saberHandoverOwner = owner;
		bs->saberHandoverTime = time;
		bs->saberHandoverForward = bs->saberLastForward;
	}
	escape = (bs->combatAction == BOT_COMBAT_ACTION_RETREAT_DEFENSE || bs->runningLikeASissy ||
		ps->forceHandExtend == HANDEXTEND_KNOCKDOWN || BG_InKnockDown(ps->legsAnim) ||
		NewBotAI_IsEnemySaberThreatImminent(bs) ||
		(owner && (bs->saberTacticAction == NEWBOTAI_SABER_TACTIC_RESET ||
			bs->saberTacticAction == NEWBOTAI_SABER_TACTIC_REPOSITION))) ? qtrue : qfalse;
	adjusted = forward;
	if (bs->saberHandoverTime > 0)
		adjusted = NewBotAI_SaberHandoverForward(bs->saberHandoverForward, adjusted,
			time - bs->saberHandoverTime, escape);
	if (bi->actionflags & ACTION_ATTACK)
		adjusted = NewBotAI_SaberSwingStartForward((int)bs->settings.skill,
			ps->weaponTime <= 0 && !BG_SaberInAttack(ps->saberMove) &&
			!PM_SaberInStart(ps->saberMove) && !PM_SaberInTransition(ps->saberMove),
			adjusted, escape);
	if (adjusted > forward)
		NewBotAI_SetInputForward(bi);
	bs->saberLastForward = adjusted;
}

// Final saber footwork: keep closing on a saber opponent through our own windup, apex and
// cooldown and between swings (see NewBotAI_SaberAdvanceForward), handing the stick back to
// the planned direction just before a chained swing is picked so the swing direction holds.
static void NewBotAI_ApplySaberAdvance(bot_state_t *bs, bot_input_t *bi)
{
	playerState_t *ps;
	const playerState_t *eps;
	newbotai_saber_phase_t phase;
	float range, predicted, radial;
	int planned, adjusted;
	qboolean escape, startingSwing, chaining, enemyAttacking;

	if (bs->saberTechniqueOwnsInputs || bs->saberDefenseActive ||
		!g_newBotAI.integer || bi->weapon != WP_SABER || !bs->currentEnemy ||
		!bs->currentEnemy->client || bs->currentEnemy->health <= 0 ||
		bs->currentEnemy->client->ps.weapon != WP_SABER ||
		NewBotAI_HasExclusiveFlipkickMovement(bs) || bs->gripkickActive)
		return;
	ps = &g_entities[bs->client].client->ps;
	eps = &bs->currentEnemy->client->ps;
	if (ps->saberInFlight || ps->groundEntityNum == ENTITYNUM_NONE ||
		(ps->fd.forcePowersActive & ((1 << FP_GRIP) | (1 << FP_DRAIN) | (1 << FP_LIGHTNING))) ||
		ps->fd.forceGripBeingGripped > level.time || BG_InRoll(ps, ps->legsAnim) ||
		BG_InSpecialJump(ps->legsAnim) || ps->saberLockTime > level.time ||
		(bi->actionflags & (ACTION_JUMP | ACTION_CROUCH | ACTION_ALT_ATTACK)))
		return;
	if (!NewBotAI_GetEnemyRangeKinematics(bs, &range, &predicted, &radial))
		return;

	if (PM_SaberInStart(ps->saberMove))
		phase = NEWBOTAI_SABER_PHASE_WINDUP;
	else if (BG_SaberInAttack(ps->saberMove))
		phase = NEWBOTAI_SABER_PHASE_APEX;
	else if (PM_SaberInReturn(ps->saberMove) || PM_SaberInTransition(ps->saberMove))
		phase = NEWBOTAI_SABER_PHASE_COOLDOWN;
	else
		phase = NEWBOTAI_SABER_PHASE_IDLE;

	escape = (bs->combatAction == BOT_COMBAT_ACTION_RETREAT_DEFENSE || bs->runningLikeASissy ||
		bs->conserveUntil > level.time ||
		ps->forceHandExtend != HANDEXTEND_NONE || BG_InKnockDown(ps->legsAnim) ||
		NewBotAI_IsEnemySaberThreatImminent(bs)) ? qtrue : qfalse;
	chaining = (bi->actionflags & ACTION_ATTACK) ? qtrue : qfalse;
	//Short sidestep-back at the end of an unproductive, unchained swing (jundon's pattern).
	if (PM_SaberInReturn(ps->saberMove) && ps->saberMove != bs->saberSidestepMove)
	{
		bs->saberSidestepMove = ps->saberMove;
		if (!NewBotAI_IsDuelStrafeSuppressed(bs) &&
			NewBotAI_ShouldSidestepAfterSwing(1, chaining, NewBotAI_GetMsSinceSaberContactOnEnemy(bs), escape))
		{
			bs->saberSidestepUntil = level.time + NEWBOTAI_SABER_SIDESTEP_MS;
			bs->saberSidestepDir = Q_irand(0, 1) ? 1 : -1;
		}
	}
	else if (!PM_SaberInReturn(ps->saberMove))
		bs->saberSidestepMove = 0;
	if (bs->saberSidestepUntil > level.time)
	{
		if (chaining || escape)
		{
			bs->saberSidestepUntil = 0;
			return;
		}
		bi->speed = 0;
		VectorClear(bi->dir);
		bi->actionflags &= ~(ACTION_MOVEFORWARD | ACTION_MOVELEFT | ACTION_MOVERIGHT);
		bi->actionflags |= ACTION_MOVEBACK |
			(bs->saberSidestepDir > 0 ? ACTION_MOVERIGHT : ACTION_MOVELEFT);
		return;
	}
	startingSwing = (chaining && phase == NEWBOTAI_SABER_PHASE_IDLE && ps->weaponTime <= 0) ? qtrue : qfalse;
	enemyAttacking = (BG_SaberInAttack(eps->saberMove) || PM_SaberInStart(eps->saberMove)) ? qtrue : qfalse;
	planned = NewBotAI_GetInputForward(bi);
	adjusted = NewBotAI_SaberAdvanceForward(phase, range, planned, chaining, ps->weaponTime,
		startingSwing, enemyAttacking, escape);
	if (adjusted > planned)
		NewBotAI_SetInputForward(bi);
}

void NewBotAI_NF(bot_state_t *bs)
{
	NewBotAI_GetAim(bs);
	if (bs->cur_ps.forceHandExtend == HANDEXTEND_KNOCKDOWN)
		NewBotAI_Getup(bs);
}

void G_Kill(gentity_t *ent);
qboolean NewBotAI_CapRoute(bot_state_t *bs, float thinktime)
{
	int activeCapRoute, activeCapRouteSequence; //sequence,
	vec3_t newSpot = { 0 };

	if (level.gametype != GT_CTF || !g_entities[bs->client].client || !g_entities[bs->client].client->pers.activeCapRoute)
		return qfalse;

	if (level.clients[bs->client].sess.sessionTeam == TEAM_RED) {
		activeCapRoute = g_entities[bs->client].client->pers.activeCapRoute;
		activeCapRouteSequence = g_entities[bs->client].client->activeCapRouteSequence;
		//Com_Printf("Seq %i max %i\n", activeCapRouteSequence, redRouteList[g_entities[bs->client].client->activeCapRoute].length);
		if (activeCapRouteSequence >= redRouteList[g_entities[bs->client].client->pers.activeCapRoute-1].length) {
			g_entities[bs->client].client->pers.activeCapRoute = 0;
			G_Kill(&g_entities[bs->client]);
			return qfalse;//route over.  self kill?
		}
		newSpot[0] = redRouteList[activeCapRoute-1].pos[activeCapRouteSequence][0];
		newSpot[1] = redRouteList[activeCapRoute-1].pos[activeCapRouteSequence][1];
		newSpot[2] = redRouteList[activeCapRoute-1].pos[activeCapRouteSequence][2];
		g_entities[bs->client].client->activeCapRouteSequence++;
	}
	else if (level.clients[bs->client].sess.sessionTeam == TEAM_BLUE) {
		activeCapRoute = g_entities[bs->client].client->pers.activeCapRoute;
		activeCapRouteSequence = g_entities[bs->client].client->activeCapRouteSequence;
		//Com_Printf("Seq %i max %i\n", activeCapRouteSequence, blueRouteList[g_entities[bs->client].client->activeCapRoute].length);
		if (activeCapRouteSequence >= blueRouteList[g_entities[bs->client].client->pers.activeCapRoute-1].length) {
			g_entities[bs->client].client->pers.activeCapRoute = 0;
			G_Kill(&g_entities[bs->client]);
			return qfalse;//route over.  self kill?
		}

		//Com_Printf("^5Setting origin for route %i seq %i\n", activeCapRoute, activeCapRouteSequence);
		newSpot[0] = blueRouteList[activeCapRoute-1].pos[activeCapRouteSequence][0];
		newSpot[1] = blueRouteList[activeCapRoute-1].pos[activeCapRouteSequence][1];
		newSpot[2] = blueRouteList[activeCapRoute-1].pos[activeCapRouteSequence][2];
		g_entities[bs->client].client->activeCapRouteSequence++;
		}
	else {
		return qfalse;
	}

	trap->EA_Action(bs->client, ACTION_SKI);
	bs->ideal_viewangles[YAW] = vectoyaw(g_entities[bs->client].client->ps.velocity);

	if (g_entities[bs->client].client->ps.velocity[2]) {
		if (level.time % 1000 > 500) { //what the fuck /sad hack to make it lok like they are jetting
			trap->EA_Jump(bs->client);
			trap->EA_MoveUp(bs->client);
		}
		else {
			trap->EA_MoveDown(bs->client); 
			trap->EA_Crouch(bs->client);
		}
	}

	{
		trace_t tr;
		vec3_t playerMins = { -15, -15, DEFAULT_MINS_2 };
		vec3_t playerMaxs = { 15, 15, DEFAULT_MAXS_2 };

		JP_Trace(&tr, g_entities[bs->client].client->ps.origin, playerMins, playerMaxs, newSpot, bs->client, CONTENTS_BODY, qfalse, 0, 0);

		if (tr.fraction != 1) { //Hit someone else
			g_entities[bs->client].client->pers.activeCapRoute = 0;
			return qfalse;
		}

		VectorSubtract(newSpot, g_entities[bs->client].client->ps.origin, g_entities[bs->client].client->ps.velocity);
		VectorScale(g_entities[bs->client].client->ps.velocity, 40.0f, g_entities[bs->client].client->ps.velocity);//sv_fps ?
		//Com_Printf("Vel is %.1f\n", len);
		//VectorClear(g_entities[bs->client].client->ps.velocity);

		//VectorCopy(newSpot, g_entities[bs->client].client->ps.origin);
	}

	return qtrue;
}

void NewBotAI_Tribes(bot_state_t *bs, float thinktime)
{
	//For capper bot. need to keep track of what spot we are at.
	//How do we we store which route the bot is currently on?
	//Bot->capRouteSequence starts at 0
	//Set origin to the selected route[sequence]
	//increment sequence

	//Make bot abandon route if knockedback and have him fight instead?
	if (bs->cur_ps.eFlags & EF_JETPACK_FLAMING || bs->cur_ps.eFlags & EF_JETPACK_ACTIVE) {
		//if (!(bs->cur_ps.pm_flags & PMF_JUMP_HELD))
		//{
			//bs->jumpTime = level.time + 200;
			//bs->jumpHoldTime = level.time + 200;
		//}
		//Com_Printf("2 Still going up\n");
		trap->EA_Jump(bs->client);
	}
	else if (bs->cur_ps.fd.forcePower > 98) {
		//Com_Printf("1 Going up\n");
		//bs->jumpTime = level.time + 200;
		//bs->jumpHoldTime = level.time + 200;

		trap->EA_Jump(bs->client);
	}
	else {
		//Com_Printf("3 Flaming? %i, Active? %i, FP: %i\n", (bs->cur_ps.eFlags & EF_JETPACK_FLAMING), bs->cur_ps.eFlags & EF_JETPACK_ACTIVE, bs->cur_ps.fd.forcePower);
	}

	/*
	if (bs->jumpTime > level.time && bs->jDelay < level.time)
	{
	if (bs->jumpHoldTime > level.time)
	{
	trap->EA_Jump(bs->client);
	if (bs->wpCurrent)
	{
	if ((bs->wpCurrent->origin[2] - bs->origin[2]) < 64)
	{
	trap->EA_MoveForward(bs->client);
	}
	}
	else
	{
	trap->EA_MoveForward(bs->client);
	}
	if (g_entities[bs->client].client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
	g_entities[bs->client].client->ps.pm_flags |= PMF_JUMP_HELD;
	}
	}
	else if (!(bs->cur_ps.pm_flags & PMF_JUMP_HELD))
	{
	trap->EA_Jump(bs->client);
	}
		*/
	trap->EA_Action(bs->client, ACTION_SKI);

	StandardBotAI(bs, thinktime);
	NewBotAI_GetAim(bs);
	NewBotAI_GetAttack(bs);
	//NewBotAI_GetMovement(bs);
	//NewBotAI_GetAttack(bs);
}

void NewBotAI_StrafeJump(bot_state_t *bs, float distance)
{
	qboolean aimright = qfalse;
//	float xyspeed, optimalAngle, frametime = 0.05f, baseSpeed = g_speed.integer;

	NewBotAI_GetAim(bs);

	NewBotAI_Flipkick(bs);

	if (level.time % 1000 > 500) //Switch sides twice a second
		aimright = qtrue;

	/*
	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED))
		baseSpeed *= 1.7f;

	xyspeed = sqrt(bs->cur_ps.velocity[0] * bs->cur_ps.velocity[0] + bs->cur_ps.velocity[1] * bs->cur_ps.velocity[1]);

	optimalAngle = acos((double) ((baseSpeed - (baseSpeed * frametime)) / xyspeed)) * (180.0f/M_PI) - 45.0f;
	*/

	trap->EA_MoveForward(bs->client);

	if (aimright) {
		//bs->ideal_viewangles[YAW] += optimalAngle;
		trap->EA_MoveRight(bs->client);
	}
	else {
		//bs->ideal_viewangles[YAW] -= optimalAngle;
		trap->EA_MoveLeft(bs->client);
	}
}

void NewBotAI_DoAloneStuff(bot_state_t *bs, float thinktime) {
	qboolean useTheForce = qfalse;
	int numEnts, i, radiusEnts[256];
	vec3_t mins = {-1024, -1024, -256}, maxs = {1024, 1024, 256}, waypoint, temp;
	gentity_t	*hit;
	qboolean destination = qfalse;

	if ((bs->cur_ps.weapon != WP_SABER) && BotWeaponSelectable(bs, WP_SABER))
		BotSelectWeapon(bs->client, WP_SABER);

	if (bs->cur_ps.fd.forcePowersActive & (1 << FP_SPEED)) { 
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_SPEED; //Turn off speed
		useTheForce = qtrue;
	}

	else if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE) {
		if (bs->cur_ps.fd.forcePowersActive & (1 << FP_PROTECT)) { 
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_PROTECT; //Turn off protect
			useTheForce = qtrue;
		}
		else if (bs->cur_ps.fd.forcePowersActive & (1 << FP_ABSORB)) { 
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB; //Turn off absorb
			useTheForce = qtrue;
		}
		else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_HEAL)) && (g_entities[bs->client].health < 100) && (bs->cur_ps.fd.forcePower > 50)) { //Heal if needed
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_HEAL;
			useTheForce = qtrue;
		}
	}

	else if (bs->cur_ps.fd.forceSide == FORCE_DARKSIDE) {
		if (bs->cur_ps.fd.forcePowersActive & (1 << FP_RAGE)) {
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_RAGE; //Turn off rage
			useTheForce = qtrue;
		}
	}

	if (useTheForce && (level.time % 1000 > 500)) //autism)
		trap->EA_ForcePower(bs->client);	//Why wont this work?? Heal is selected, this is sent.. yet no healing??	

	VectorAdd(maxs, bs->origin, maxs);
	VectorAdd(mins, bs->origin, mins);

	numEnts = trap->EntitiesInBox(mins, maxs, radiusEnts, 256);
	for (i = 0; i < numEnts; i++) {
		int weapon, ammo;
		hit = &g_entities[radiusEnts[i]];

		if (hit->s.eType != ET_ITEM)
			continue;

		if (hit->nextthink) //Its respawning still
			continue;

		if (hit->item->giType == IT_POWERUP) {
			if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE) {
				if (hit->item->giTag == PW_FORCE_ENLIGHTENED_DARK)
					continue;
			}
			else {
				if (hit->item->giTag == PW_FORCE_ENLIGHTENED_LIGHT)
					continue;
			}
		}
		
		if (hit->item->giType == IT_WEAPON) {
			weapon = hit->item->giTag;

			if (bs->cur_ps.stats[STAT_WEAPONS] & (1 << weapon))
				continue;
		}

		if (hit->item->giType == IT_AMMO) {
			ammo = hit->item->giTag;

			switch (ammo) {
				case AMMO_BLASTER:
				case AMMO_POWERCELL:
				case AMMO_METAL_BOLTS:
					if (bs->cur_ps.ammo[ammo] >= 300)
						continue;
					break;
				case AMMO_ROCKETS:
					if (bs->cur_ps.ammo[ammo] >= 25)
						continue;
					break;
				default:
					continue;
			}
		}

		if ((hit->item->giType == IT_HEALTH) && (g_entities[bs->client].health >= 100))
			continue;

		if ((hit->item->giType == IT_ARMOR) && (bs->cur_ps.stats[STAT_ARMOR] >= 100))
			continue;

		if (hit->item->giType == IT_HOLDABLE) //just ignore bacta/shield etc i gues..
			continue; 		//ideally, check if we already have the item, like a bacta or shields etc

		/*
		bs->wpSwitchTime++;

		if (bs->wpSwitchTime > 5000) {//Give up if we are stuck..
			bs->wpSwitchTime = 0;
			continue;
		}
		*/
			
		//trap->Print("Item: %i\n", hit->item->giType);

		VectorCopy(hit->s.origin, waypoint);
		destination = qtrue;
		break;

		
	}

	if (!destination) {
		//StandardBotAI(bs, thinktime);
		trap->EA_MoveForward(bs->client);
		bs->ideal_viewangles[YAW] += Q_irand(-2,4);
		return;
	}

	VectorSubtract(waypoint, bs->origin, temp);
	vectoangles(temp, temp);
	VectorCopy(temp, bs->ideal_viewangles);
	bs->ideal_viewangles[YAW] += 1;

	if (Q_irand(1, 10) < 2)
		trap->EA_MoveRight(bs->client);
	else if (Q_irand(1, 10) < 2)
		trap->EA_MoveLeft(bs->client);

	if (Q_irand(1, 100) < 2)
		bs->ideal_viewangles[YAW] += 90;
	if (Q_irand(1, 100) < 2)
		bs->ideal_viewangles[YAW] -= 90;

	if ((bs->origin[2] + STEPSIZE) < waypoint[2] && (level.time % 1000 > 500)) //autism)) //Its above us
		NewBotAI_Flipkick(bs);
	trap->EA_MoveForward(bs->client);

	//Get closest weapon we dont already have
	//Run to it

	//Entities in box.. for each..
	//Check if we have it..
	//Run to it..
}

static qboolean NewBotAI_IsCombatProgressStalled(bot_state_t *bs)
{
	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	if (bs->frame_Enemy_Len < 128.0f || bs->cur_ps.groundEntityNum == ENTITYNUM_NONE)
	{
		VectorCopy(bs->origin, bs->combatStuckOrigin);
		bs->combatStuckSince = level.time;
		return qfalse;
	}

	if (!bs->combatStuckSince)
	{
		VectorCopy(bs->origin, bs->combatStuckOrigin);
		bs->combatStuckSince = level.time;
		return qfalse;
	}

	if (DistanceSquared(bs->origin, bs->combatStuckOrigin) > NEWBOTAI_COMBAT_STUCK_DISTANCE_SQ)
	{
		VectorCopy(bs->origin, bs->combatStuckOrigin);
		bs->combatStuckSince = level.time;
		return qfalse;
	}

	return (bs->combatStuckSince <= level.time - NEWBOTAI_COMBAT_STUCK_TIME_MS) ? qtrue : qfalse;
}

static void NewBotAI_RunNavigationOrAlone(bot_state_t *bs, float thinktime)
{
	if (NewBotAI_HasWaypointNavigation())
	{
		bs->navObstacleUntil = 0;
		StandardBotAI(bs, thinktime);
	}
	else
	{
		NewBotAI_DoAloneStuff(bs, thinktime);
	}
}

int NewBotAI_ScanForEnemies(bot_state_t* bs) {
	vec3_t a;
	float distcheck;
	float closest;
	float lowFruitClosest;
	float startingClosest = 999999;
	int bestindex;
	int lowFruitBestIndex;
	int i;
	int pass;
	float hasEnemyDist = 0;
	qboolean noAttackNonJM = qfalse;
	qboolean allowLowHangingFruit;
	int ourHealth = g_entities[bs->client].health;
	int targetMode;
	int lowHangingFruitHP;
	float lowHangingFruitDistance;
	const float targetDistanceLimit = BotGetTargetDistanceLimit();
	const float targetHealthFloor = 0.25f;
	const float targetHealthCeil = 1.0f;
	const float selfLowHealthBiasScale = 0.005f;
	const float nominalFullHealth = 100.0f;

	targetMode = BotGetNewBotAITargetMode();
	lowHangingFruitHP = BotGetLowHangingFruitHP();
	lowHangingFruitDistance = BotGetLowHangingFruitDistance();
	allowLowHangingFruit = ((level.gametype == GT_FFA || level.gametype == GT_TEAM) &&
		lowHangingFruitHP > 0 &&
		lowHangingFruitDistance > 0.0f) ? qtrue : qfalse;

	//While actively engaged with a valid target, keep target commitment stable so
	//close-range fights don't jitter between nearby candidates every few thinks.
	if (bs->currentEnemy && bs->currentEnemy->client &&
		PassStandardEnemyChecks(bs, bs->currentEnemy) &&
		NewBotAI_IsActivelyEngagedInCombat(bs))
	{
		return bs->currentEnemy->s.number;
	}

	if (bs->currentEnemy) { //only switch to a new enemy if he's significantly closer
		if (bs->currentEnemy->client && PassStandardEnemyChecks(bs, bs->currentEnemy))
		{
			const int currentEnemyHealth = Com_Clampi(1, (int)nominalFullHealth, bs->currentEnemy->health);
			float normalizedHealth;
			VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a);
			hasEnemyDist = VectorLength(a);
			//Map enemy health into a distance multiplier before self-health bias:
			//1 HP -> 0.25x, 100 HP -> 1.0x. Lower-HP enemies are treated as effectively
			//closer (more attractive); our own low health nudges the multiplier upward.
			normalizedHealth = targetHealthFloor +
				(currentEnemyHealth - 1) * (targetHealthCeil - targetHealthFloor) /
				(nominalFullHealth - 1.0f);
			normalizedHealth += (nominalFullHealth - ourHealth) * selfLowHealthBiasScale;
			if (normalizedHealth < targetHealthFloor)
			{
				normalizedHealth = targetHealthFloor;
			}
			if (normalizedHealth > targetHealthCeil)
			{
				normalizedHealth = targetHealthCeil;
			}
			if (targetMode == NEWBOTAI_TARGET_PREFER_HUMANS)
			{
				normalizedHealth = targetHealthCeil;
			}
			hasEnemyDist *= normalizedHealth;
		}
		else
		{
			hasEnemyDist = startingClosest;
		}
	}

	if (bs->currentEnemy && bs->currentEnemy->client && bs->currentEnemy->client->ps.isJediMaster) { //The Jedi Master must die.
		return -1;
	}

	if (level.gametype == GT_JEDIMASTER) {
		if (G_ThereIsAMaster() && !bs->cur_ps.isJediMaster) { //if friendly fire is on in jedi master we can attack people that bug us
			if (!g_friendlyFire.value) {
				noAttackNonJM = qtrue;
			}
			else {
				startingClosest = 128; //only get mad at people if they get close enough to you to anger you, or hurt you
			}
		}
	}

	for (pass = 0; pass < (BotTargetModePrefersHumansThenBots(targetMode) ? 2 : 1); pass++)
	{
		const qboolean preferHumansOnly = (BotTargetModePrefersHumansThenBots(targetMode) && pass == 0);

		closest = startingClosest;
		lowFruitClosest = startingClosest;
		bestindex = -1;
		lowFruitBestIndex = -1;

		//for (i = 0; i < level.numConnectedClients; i++) { //Go through each client, see if they are "afk", if everyone is afk, fuck this then.
		for (i = 0; i < MAX_CLIENTS; i++) { //Go through each client, see if they are "afk", if everyone is afk, fuck this then.
			//gentity_t* ent = &g_entities[level.sortedClients[i]];
			gentity_t* ent = &g_entities[i];

			if (bs->duelBlacklistUntil > level.time && i == bs->duelBlacklistIndex)
			{
				continue;
			}

			if (ent && ent->inuse && PassStandardEnemyChecks(bs, ent) && BotPVSCheck(ent->client->ps.origin, bs->eye) && PassLovedOneCheck(bs, ent)) {
				float normalizedHealth = 0.25 + (ent->health - 1) * (1 - 0.25) / (100 - 1); //Range .25 to 1
				qboolean isLowHangingFruit = qfalse;
				float enemyDist;

				if (!BotTargetModePassesScanFilter(targetMode, ent, preferHumansOnly))
				{
					continue;
				}

				if (NewBotAI_IsUnavailableDuelBot(bs, ent, targetMode))
				{
					continue;
				}

				if (ent->client->ps.fd.forceGripEntityNum == bs->cur_ps.clientNum) { //always aim at whos gripping us
					return i;
				}

				VectorSubtract(ent->client->ps.origin, bs->eye, a);
				enemyDist = VectorLength(a);

				normalizedHealth += (100 - ourHealth) * 0.005; //Bring normalizedhealth closer to 1 the lower HP we ourselves are?, up to +0.5?
				if (normalizedHealth > 1)
					normalizedHealth = 1;

				if (targetMode == NEWBOTAI_TARGET_PREFER_HUMANS) {
					//-3 fights the nearest target like -1 - skip the health weighting
					//entirely while keeping the bot/human duel-offer logic untouched.
					normalizedHealth = 1.0f;
				}

				distcheck = enemyDist * normalizedHealth;
				vectoangles(a, a);

				if (ent->client->ps.isJediMaster) { //make us think the Jedi Master is close so we'll attack him above all
					distcheck = 1;
				}

				if (allowLowHangingFruit && ent->health <= lowHangingFruitHP && enemyDist <= lowHangingFruitDistance)
				{
					isLowHangingFruit = qtrue;
				}

				if (BotMindTricked(bs->client, i)) {
					if (!(distcheck < 256 || (level.time - ent->client->dangerTime) < 100)) {
						continue;
					}
				}

				if (hasEnemyDist && distcheck >= (hasEnemyDist - 128))
				{
					continue;
				}

				if (noAttackNonJM && !ent->client->ps.isJediMaster)
				{
					continue;
				}

				if (targetDistanceLimit > 0.0f && enemyDist > targetDistanceLimit)
				{
					continue;
				}

				if (isLowHangingFruit)
				{
					if (distcheck < lowFruitClosest)
					{
						lowFruitClosest = distcheck;
						lowFruitBestIndex = i;
					}
				}
				else if (distcheck < closest)
				{
					closest = distcheck;
					bestindex = i;
				}
			}
		}

		if (lowFruitBestIndex != -1)
		{
			return lowFruitBestIndex;
		}

		if (bestindex != -1 || !BotTargetModePrefersHumansThenBots(targetMode))
		{
			if (bestindex != -1)
			{
				return bestindex;
			}
			return -1;
		}
	}

	return -1;
}

#define _ADVANCEDBOTSHIT 1

//Returns a client currently challenging this bot. Human challenges win over bot
//challenges and use the engine's full acceptance window (the challenger's duelTime
//plus 2s, see Cmd_EngageDuel_f); they also ignore the request throttle and the FFA
//explore window so a human offer is never missed. Bot challenges in -3/-4 must be
//force duels.
static gentity_t *NewBotAI_GetPendingDuelChallenger(bot_state_t *bs, int targetMode, int *duelTypeOut)
{
	gentity_t *botChallenger = NULL;
	int botChallengerType = -1;
	qboolean botGated;
	int i;

	if (duelTypeOut)
		*duelTypeOut = -1;
	if (!bot_honorableduelacceptance.integer || !g_privateDuel.integer || !bs || bs->cur_ps.duelInProgress)
		return NULL;
	botGated = (bs->botDuelRequestThrottleUntil > level.time ||
		NewBotAI_InFFAExploreWindow(bs, targetMode)) ? qtrue : qfalse;

	for (i = 0; i < MAX_CLIENTS; i++)
	{
		gentity_t *challenger = &g_entities[i];
		int duelType;
		const qboolean challengerIsBot = (challenger->r.svFlags & SVF_BOT) ? qtrue : qfalse;

		if (!challenger->inuse || !challenger->client || i == bs->client)
			continue;
		if (challenger->health < 1 || challenger->client->ps.duelInProgress ||
			challenger->client->ps.duelIndex != bs->client ||
			challenger->client->ps.duelTime <= 0 ||
			challenger->client->ps.duelTime + 2000 < level.time)
			continue;

		duelType = dueltypes[challenger->client->ps.clientNum];
		if (!challengerIsBot)
		{
			if (duelTypeOut)
				*duelTypeOut = duelType;
			return challenger;
		}
		if (botGated || botChallenger || challenger->client->ps.duelTime <= level.time)
			continue;
		if (!BotTargetModeAllowsBotDuelChallenges(targetMode))
			continue;
		if (duelType != 1)
			continue;
		botChallenger = challenger;
		botChallengerType = duelType;
	}

	if (botChallenger && duelTypeOut)
		*duelTypeOut = botChallengerType;
	return botChallenger;
}

static void NewBotAI_RememberHumanDuelType(bot_state_t *bs, gentity_t *challenger, int duelType)
{
	if (!bs || !challenger || (challenger->r.svFlags & SVF_BOT))
		return;
	if (duelType == 0)
		bs->humanDuelTypePref = 1;
	else if (duelType == 1)
		bs->humanDuelTypePref = 2;
}

//Accepts (or walks into range to accept) a pending duel challenge. Returns qtrue when
//the bot spent this think on the challenge.
static qboolean NewBotAI_AcceptDuelFrom(bot_state_t *bs, gentity_t *challenger, int duelType)
{
	gentity_t *self;
	const qboolean human = (challenger && !(challenger->r.svFlags & SVF_BOT)) ? qtrue : qfalse;
	vec3_t toChallenger;

	if (!bs || !challenger || !challenger->client)
		return qfalse;
	self = &g_entities[bs->client];
	NewBotAI_RememberHumanDuelType(bs, challenger, duelType);

	bs->currentEnemy = challenger;
	bs->doAttack = 0;
	bs->doAltAttack = 0;
	bs->duelOfferHoldUntil = 0;
	NewBotAI_FaceEntityImmediately(bs, challenger);
	if (duelType <= 1 && bs->cur_ps.weapon == WP_SABER && bs->cur_ps.saberHolstered)
	{
		if (!bs->cur_ps.saberInFlight && self->client->ps.weaponTime < 1)
		{
			Cmd_ToggleSaber_f(self);
			if (!human)
				bs->botDuelRequestThrottleUntil = level.time + NEWBOTAI_DUEL_REQUEST_MIN_INTERVAL_MS;
			bs->duelNoStrafeUntil = level.time + Com_Clampi(0, 10000, bot_duel_nostrafetime.integer);
			bs->beStill = level.time + (human ? 200 : 2500);
		}
		return human;
	}

	//Cmd_EngageDuel_f traces 256 units along the view: walk into range first.
	VectorSubtract(challenger->client->ps.origin, self->client->ps.origin, toChallenger);
	if (VectorLength(toChallenger) > NEWBOTAI_DUEL_ACCEPT_RANGE ||
		!OrgVisible(bs->eye, challenger->client->ps.origin, bs->client))
	{
		if (!human)
			return qfalse;
		trap->EA_MoveForward(bs->client);
		return qtrue;
	}

	//Our own pending offer blocks Cmd_EngageDuel_f; a human's offer takes priority.
	if (human && self->client->ps.duelTime >= level.time)
		self->client->ps.duelTime = 0;

	Cmd_EngageDuel_f(self, duelType);

	if (!human)
		bs->timeToReact = level.time + BotGetReflexScaledResponseDelayMs(bs);
	bs->botDuelRequestThrottleUntil = level.time + NEWBOTAI_DUEL_REQUEST_MIN_INTERVAL_MS;
	bs->duelNoStrafeUntil = level.time + Com_Clampi(0, 10000, bot_duel_nostrafetime.integer);
	bs->beStill = level.time + (human ? 500 : 2500);
	return qtrue;
}

static qboolean BotTryAcceptAnyDuelChallenge(bot_state_t *bs, int targetMode)
{
	gentity_t *challenger;
	int duelType;

	challenger = NewBotAI_GetPendingDuelChallenger(bs, targetMode, &duelType);
	if (!challenger)
	{
		return qfalse;
	}
	if (!(challenger->r.svFlags & SVF_BOT))
	{
		return NewBotAI_AcceptDuelFrom(bs, challenger, duelType);
	}

	bs->currentEnemy = challenger;
	NewBotAI_FaceEntityImmediately(bs, challenger);
	if (duelType <= 1 && bs->cur_ps.weapon == WP_SABER && bs->cur_ps.saberHolstered)
	{
		if (!bs->cur_ps.saberInFlight && g_entities[bs->client].client->ps.weaponTime < 1)
		{
			Cmd_ToggleSaber_f(&g_entities[bs->client]);
			bs->botDuelRequestThrottleUntil = level.time + NEWBOTAI_DUEL_REQUEST_MIN_INTERVAL_MS;
			bs->duelNoStrafeUntil = level.time + Com_Clampi(0, 10000, bot_duel_nostrafetime.integer);
			bs->beStill = level.time + 2500;
		}
		return qfalse;
	}
	Cmd_EngageDuel_f(&g_entities[bs->client], duelType);

	bs->doAttack = 0;
	bs->doAltAttack = 0;
	bs->timeToReact = level.time + BotGetReflexScaledResponseDelayMs(bs);
	bs->botDuelRequestThrottleUntil = level.time + NEWBOTAI_DUEL_REQUEST_MIN_INTERVAL_MS;
	bs->duelNoStrafeUntil = level.time + Com_Clampi(0, 10000, bot_duel_nostrafetime.integer);
	bs->beStill = level.time + 2500;
	return qtrue;
}

//After issuing a duel offer, stay passive (still, facing the target, no attacks) for
//NEWBOTAI_DUEL_OFFER_HOLD_MS so the target can accept. Draining is still allowed when
//recently hurt or low on health. Returns qtrue while the hold owns this think.
static qboolean NewBotAI_RunDuelOfferHold(bot_state_t *bs)
{
	gentity_t *self;
	gentity_t *target;

	if (!bs || bs->duelOfferHoldUntil <= level.time)
		return qfalse;
	self = &g_entities[bs->client];
	target = (bs->duelOfferTargetNum >= 0 && bs->duelOfferTargetNum < MAX_CLIENTS) ?
		&g_entities[bs->duelOfferTargetNum] : NULL;
	if (bs->cur_ps.duelInProgress || !target || !target->inuse || !target->client ||
		target->health < 1 || target->client->ps.duelInProgress ||
		self->client->ps.duelIndex != bs->duelOfferTargetNum ||
		self->client->ps.duelTime + 2000 < level.time ||
		self->health <= bs->duelOfferHoldHealth - NEWBOTAI_DUEL_OFFER_HOLD_DAMAGE_CANCEL)
	{
		bs->duelOfferHoldUntil = 0;
		return qfalse;
	}

	bs->currentEnemy = target;
	bs->doAttack = 0;
	bs->doAltAttack = 0;
	bs->beStill = level.time + 100;
	NewBotAI_FaceEntityImmediately(bs, target);
	if ((bs->lastHurtTime > level.time - 1000 || self->health < 40) &&
		bot_forcepowers.integer &&
		!(g_forcePowerDisable.integer & (1 << FP_DRAIN)) &&
		(bs->cur_ps.fd.forcePowersKnown & (1 << FP_DRAIN)))
	{
		level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
		trap->EA_ForcePower(bs->client);
	}
	return qtrue;
}

static qboolean NewBotAI_BotCanChallengeNow(bot_state_t *bs, int targetMode)
{
	return (bs && bs->botChallengingTime <= level.time &&
		!NewBotAI_InFFAExploreWindow(bs, targetMode)) ? qtrue : qfalse;
}

//-4 scan filter: skip a bot that neither of us can currently challenge, so arriving
//bots look for a free partner instead of joining a pile of idle bots.
static qboolean NewBotAI_IsUnavailableDuelBot(bot_state_t *bs, gentity_t *ent, int targetMode)
{
	bot_state_t *otherBS;

	if (!BotTargetModeIsForceDuelOnly(targetMode) || !ent || !ent->client ||
		!(ent->r.svFlags & SVF_BOT) || ent->s.number < 0 || ent->s.number >= MAX_CLIENTS)
		return qfalse;
	if (ent->client->ps.duelInProgress)
		return qtrue;
	otherBS = botstates[ent->s.number];
	if (!otherBS)
		return qfalse;
	if (otherBS->duelBlacklistUntil > level.time && otherBS->duelBlacklistIndex == bs->client)
		return qtrue;
	return (!NewBotAI_BotCanChallengeNow(bs, targetMode) &&
		!NewBotAI_BotCanChallengeNow(otherBS, targetMode)) ? qtrue : qfalse;
}

//-4 stalemate watchdog: two bots idling next to each other without a duel or offer
//for NEWBOTAI_DUEL_STALEMATE_MS blacklist each other and roam apart.
static void NewBotAI_UpdateDuelStalemate(bot_state_t *bs, int targetMode)
{
	gentity_t *enemy = bs->currentEnemy;
	bot_state_t *otherBS;
	qboolean pending;

	if (!BotTargetModeIsForceDuelOnly(targetMode) || bs->cur_ps.duelInProgress ||
		!enemy || !enemy->client || !(enemy->r.svFlags & SVF_BOT) ||
		bs->frame_Enemy_Len > NEWBOTAI_DUEL_STALEMATE_RADIUS ||
		enemy->s.number < 0 || enemy->s.number >= MAX_CLIENTS)
	{
		bs->duelStalemateOtherNum = -1;
		bs->duelStalemateSince = 0;
		return;
	}
	pending = ((g_entities[bs->client].client->ps.duelIndex == enemy->s.number &&
			g_entities[bs->client].client->ps.duelTime + 2000 >= level.time) ||
		(enemy->client->ps.duelIndex == bs->client &&
			enemy->client->ps.duelTime + 2000 >= level.time)) ? qtrue : qfalse;
	if (pending || bs->duelStalemateOtherNum != enemy->s.number || bs->duelStalemateSince <= 0 ||
		bs->duelStalemateSince > level.time)
	{
		bs->duelStalemateOtherNum = enemy->s.number;
		bs->duelStalemateSince = level.time;
		return;
	}
	if (level.time - bs->duelStalemateSince < NEWBOTAI_DUEL_STALEMATE_MS)
		return;

	otherBS = botstates[enemy->s.number];
	bs->duelBlacklistIndex = enemy->s.number;
	bs->duelBlacklistUntil = level.time + NEWBOTAI_DUEL_STALEMATE_BLACKLIST_MS;
	bs->duelRoamUntil = level.time + NEWBOTAI_DUEL_STALEMATE_ROAM_MS;
	bs->duelStalemateOtherNum = -1;
	bs->duelStalemateSince = 0;
	NewBotAI_ClearCurrentEnemyLock(bs);
	if (otherBS)
	{
		otherBS->duelBlacklistIndex = bs->client;
		otherBS->duelBlacklistUntil = bs->duelBlacklistUntil;
		otherBS->duelRoamUntil = bs->duelRoamUntil;
		otherBS->duelStalemateOtherNum = -1;
		otherBS->duelStalemateSince = 0;
		if (otherBS->currentEnemy == &g_entities[bs->client])
			NewBotAI_ClearCurrentEnemyLock(otherBS);
	}
	if (bot_learning_debug.integer)
	{
		Com_Printf("^3[duel]^7 %s and %s stalemated; roaming apart\n",
			g_entities[bs->client].client->pers.netname, enemy->client->pers.netname);
	}
}

static void NewBotAI_RunForceDuelOnly(bot_state_t *bs)
{
	bs->doAttack = 0;
	bs->doAltAttack = 0;

	NewBotAI_GetAim(bs);

	if (!bs->currentEnemy || !bs->currentEnemy->client)
	{
		NewBotAI_RunNavigationOrAlone(bs, 0.0f);
		return;
	}

	if (bs->frame_Enemy_Vis && bs->frame_Enemy_Len > 96.0f)
	{
		trap->EA_MoveForward(bs->client);
	}
	else if (!bs->frame_Enemy_Vis && NewBotAI_HasWaypointNavigation())
	{
		NewBotAI_RunNavigationOrAlone(bs, 0.0f);
	}
}

static void NewBotAI_FaceEntityImmediately(bot_state_t *bs, gentity_t *target)
{
	vec3_t toTarget;
	vec3_t targetAngles;

	if (!bs || !target || !target->client)
		return;

	VectorSubtract(target->client->ps.origin, bs->origin, toTarget);
	toTarget[2] += target->client->ps.viewheight - bs->cur_ps.viewheight;
	vectoangles(toTarget, targetAngles);
	VectorCopy(targetAngles, bs->goalAngles);
	VectorCopy(targetAngles, bs->ideal_viewangles);
	SetClientViewAngle(&g_entities[bs->client], targetAngles);
}

static qboolean NewBotAI_HasSafeSaberThrowClearance(bot_state_t *bs)
{
	trace_t tr;
	vec3_t start;
	vec3_t end;
	vec3_t toEnemy;
	vec3_t forward;
	vec3_t right;
	vec3_t traceEnds[3];
	int i;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
		return qfalse;

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
	if (VectorNormalize(toEnemy) <= 0.0f)
		return qfalse;

	VectorCopy(bs->origin, start);
	start[2] += (float)(bs->cur_ps.viewheight - 8);
	VectorCopy(toEnemy, forward);
	VectorSet(right, -forward[1], forward[0], 0.0f);
	if (VectorNormalize(right) <= 0.0f)
	{
		VectorSet(right, 0.0f, 1.0f, 0.0f);
	}

	VectorMA(start, 48.0f, forward, traceEnds[0]);
	VectorMA(traceEnds[0], 16.0f, right, traceEnds[1]);
	VectorMA(traceEnds[0], -16.0f, right, traceEnds[2]);

	for (i = 0; i < 3; i++)
	{
		JP_Trace(&tr, start, NULL, NULL, traceEnds[i], bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
		if (tr.fraction < 1.0f && tr.entityNum != bs->currentEnemy->s.number)
			return qfalse;
	}

	end[0] = start[0];
	end[1] = start[1];
	end[2] = start[2] + 12.0f;
	JP_Trace(&tr, start, NULL, NULL, end, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);
	return (tr.fraction == 1.0f) ? qtrue : qfalse;
}

static qboolean NewBotAI_IsEnemyReadyToBlockFreshSaberThrow(bot_state_t *bs)
{
	vec3_t toBot;
	vec3_t toBotAngles;
	const playerState_t *enemyPS;

	if (!bs || !bs->currentEnemy || !bs->currentEnemy->client)
		return qfalse;

	enemyPS = &bs->currentEnemy->client->ps;
	if (enemyPS->weapon != WP_SABER || enemyPS->saberInFlight)
		return qfalse;
	if (BG_InKnockDown(enemyPS->legsAnim) || BG_InRoll((playerState_t *)enemyPS, enemyPS->legsAnim))
		return qfalse;
	if (BG_SaberInAttack(enemyPS->saberMove) || PM_SaberInStart(enemyPS->saberMove) ||
		PM_SaberInTransition(enemyPS->saberMove))
		return qfalse;
	if (enemyPS->fd.forcePowersActive != 0)
		return qfalse;
	if (enemyPS->weaponstate != WEAPON_READY && enemyPS->weaponstate != WEAPON_IDLE)
		return qfalse;
	if (bs->frame_Enemy_Len > 256.0f)
		return qfalse;

	VectorSubtract(bs->origin, bs->currentEnemy->client->ps.origin, toBot);
	vectoangles(toBot, toBotAngles);
	if (fabs(AngleDelta(enemyPS->viewangles[YAW], toBotAngles[YAW])) > 35.0f)
		return qfalse;
	if (fabs(AngleDelta(enemyPS->viewangles[PITCH], toBotAngles[PITCH])) > 20.0f)
		return qfalse;

	return qtrue;
}

static qboolean NewBotAI_IsDirectPathToEnemyBlocked(bot_state_t *bs)
{
	vec3_t toEnemy, trTo, mins, maxs;
	trace_t tr;

	if (!bs->currentEnemy || !bs->currentEnemy->client)
	{
		return qfalse;
	}

	VectorSubtract(bs->currentEnemy->client->ps.origin, bs->origin, toEnemy);
	if (VectorNormalize(toEnemy) < 96.0f)
	{
		return qfalse;
	}

	trTo[0] = bs->origin[0] + toEnemy[0] * 64.0f;
	trTo[1] = bs->origin[1] + toEnemy[1] * 64.0f;
	trTo[2] = bs->origin[2] + toEnemy[2] * 64.0f;

	mins[0] = -15;
	mins[1] = -15;
	mins[2] = 0;
	maxs[0] = 15;
	maxs[1] = 15;
	maxs[2] = 32;

	JP_Trace(&tr, bs->origin, mins, maxs, trTo, bs->client, MASK_PLAYERSOLID, qfalse, 0, 0);

	if (tr.fraction < 1.0f && tr.entityNum != bs->currentEnemy->s.number)
	{
		return qtrue;
	}

	return qfalse;
}

static qboolean NewBotAI_ShouldFallbackToWaypoints(bot_state_t *bs)
{
	const qboolean progressStalled = NewBotAI_IsCombatProgressStalled(bs);

	if (!NewBotAI_HasWaypointNavigation())
	{
		return qfalse;
	}

	if (!NewBotAI_HasValidCurrentEnemy(bs))
	{
		bs->combatStuckSince = 0;
		return qfalse;
	}

	if (!bs->frame_Enemy_Vis)
	{
		return NewBotAI_ShouldPursueTargetThroughWaypoints(bs, bs->currentEnemy);
	}

	if (NewBotAI_IsDirectPathToEnemyBlocked(bs))
	{
		return qtrue;
	}

	return progressStalled;
}

void NewBotAI(bot_state_t *bs, float thinktime) //BOT START
{
	int closestID = -1;
	int i;
	int responseDelay;
	int targetMode;
	int pendingDuelType = -1;
	qboolean someonesHere = qfalse;
	vec3_t headlevel;
	gentity_t *oldEnemy = bs->currentEnemy;
	gentity_t *pendingDuelChallenger = NULL;

	bs->isCamper = 0; //reset this

	if (bs->cur_ps.stats[STAT_RACEMODE])
		return;

	if (g_entities[bs->client].health < 1) { //We are dead, so respawn!
		trap->EA_Attack(bs->client);
		return;
	}

	if (g_entities[bs->client].client->pers.amfreeze) //No AI if we are frozen
		return;

	NewBotAI_ConfigureSaberThrow(bs);

	targetMode = BotGetNewBotAITargetMode();
	someonesHere = BotHasActiveHumanPlayers();

	//Count completed duels: once we hit bot_duelcountmax, -3/-4 go back to FFA for a
	//while and stop immediately re-locking the same bot after a duel ends.
	if (bs->wasDuelInProgress && !bs->cur_ps.duelInProgress &&
		BotTargetModeAllowsBotDuelChallenges(targetMode))
	{
		gentity_t *duelEndedEnemy = oldEnemy;

		NewBotAI_ClearCurrentEnemyLock(bs);
		bs->lastVisibleEnemyIndex = -1;
		bs->lastVisibleEnemyTime = 0;
		bs->enemySeenTime = 0;
		bs->enemyWaypointFallbackIndex = -1;
		bs->enemyWaypointFallbackTime = 0;
		bs->enemyWaypointFallbackEnemyNum = -1;

		if (duelEndedEnemy && (g_entities[bs->client].r.svFlags & SVF_BOT) && (duelEndedEnemy->r.svFlags & SVF_BOT))
		{
			bot_state_t *oldEnemyBS = NULL;
			const int blacklistUntil = level.time + NEWBOTAI_DUEL_TARGET_BLACKLIST_MS;
			const int duelCooldownMs = NewBotAI_GetDuelRequestCooldownMs(
				NEWBOTAI_DUEL_REQUEST_COOLDOWN_MS,
				NEWBOTAI_DUEL_REQUEST_BOT_VS_BOT_COOLDOWN_MS,
				BotTargetModeUsesExtendedBotDuelCooldown(targetMode) ? 1 : 0,
				1,
				1);
			if (duelEndedEnemy->s.number >= 0 && duelEndedEnemy->s.number < MAX_CLIENTS)
			{
				oldEnemyBS = botstates[duelEndedEnemy->s.number];
			}
			bs->duelBlacklistIndex = duelEndedEnemy->s.number;
			bs->duelBlacklistUntil = blacklistUntil;
			if (bs->currentEnemy == duelEndedEnemy)
			{
				NewBotAI_ClearCurrentEnemyLock(bs);
			}
			if (oldEnemyBS)
			{
				oldEnemyBS->duelBlacklistIndex = bs->client;
				oldEnemyBS->duelBlacklistUntil = blacklistUntil;
				if (oldEnemyBS->currentEnemy == &g_entities[bs->client])
				{
					NewBotAI_ClearCurrentEnemyLock(oldEnemyBS);
				}
			}
			if (duelCooldownMs > 0)
			{
				const int cooldownUntil = level.time + duelCooldownMs;
				if (cooldownUntil > bs->botChallengingTime)
				{
					bs->botChallengingTime = cooldownUntil;
				}
				if (oldEnemyBS && cooldownUntil > oldEnemyBS->botChallengingTime)
				{
					oldEnemyBS->botChallengingTime = cooldownUntil;
				}
			}
		}

		bs->duelCompletedCount++;
		if (bs->duelCompletedCount >= bot_duelcountmax.integer)
		{
			if (duelEndedEnemy && duelEndedEnemy->client)
			{
				bs->duelBlacklistIndex = duelEndedEnemy->s.number;
				bs->duelBlacklistUntil = level.time + NEWBOTAI_DUEL_TARGET_BLACKLIST_MS;
			}
			if (bot_ffaexploretime.integer > 0)
			{
				bs->ffaExploreUntil = level.time + bot_ffaexploretime.integer;
			}
		}
	}
	bs->wasDuelInProgress = bs->cur_ps.duelInProgress;

	if (!someonesHere && targetMode == NEWBOTAI_TARGET_HUMANS_ONLY)
		return;

	if (targetMode < 0)
		closestID = NewBotAI_ScanForEnemies(bs); //This has been modified to take health into account, and ignore FOV, mindtrick, etc, when newBotAI is being used.
	else {
		gclient_t	*cl;
		closestID = targetMode;

		if (closestID < 0 || closestID >= level.maxclients)
			closestID = -1;

		cl = &level.clients[closestID];
		if (!cl || cl->pers.connected != CON_CONNECTED)//Or in spectate? or?
			closestID = -1;
	}

	if (g_movementStyle.integer == MV_TRIBES) { //&& CAPPING?
		if (closestID == -1 && (!g_entities[bs->client].client || !g_entities[bs->client].client->pers.activeCapRoute)) { //if we have no active route and no1 near, suicid
			if ((redRouteList[0].length && g_entities[bs->client].client->sess.sessionTeam == TEAM_RED) || (blueRouteList[0].length && g_entities[bs->client].client->sess.sessionTeam == TEAM_BLUE)) { //only if the map actually has cap routes do we behave like they have cap routes
				G_Kill(&g_entities[bs->client]);
				return;
			}
		}
		if (NewBotAI_CapRoute(bs, thinktime))
			return;
	}

	if (closestID == -1) {//Its just us, or they are too far away.
		if (bs->currentEnemy && !NewBotAI_HasValidCurrentEnemy(bs))
		{
			NewBotAI_ClearCurrentEnemyLock(bs);
		}
		if (NewBotAI_InFFAExploreWindow(bs, targetMode)) {
			//Exploring after a few duels: keep roaming instead of dropping out of the
			//FFA window just because nobody is in range this think.
			NewBotAI_RunNavigationOrAlone(bs, thinktime);
		}
		else {
			//Found nobody - the next scan is a fresh search, so any duel streak and
			//explore window reset.
			bs->duelCompletedCount = 0;
			bs->ffaExploreUntil = 0;
			NewBotAI_RunNavigationOrAlone(bs, thinktime);
		}
		return;
	}

	bs->frame_Enemy_Vis = 0;
	VectorCopy(g_entities[closestID].client->ps.origin, headlevel);
	headlevel[2] += g_entities[closestID].client->ps.viewheight - 24;

	{
		const qboolean enemyVisible = OrgVisible(bs->eye, g_entities[closestID].client->ps.origin, bs->client) ? qtrue : qfalse;

		if ((bs->cur_ps.weapon == WP_DEMP2 && g_entities[bs->client].client->forcedFireMode != 1) ||
			(targetMode >= 0) || enemyVisible || !NewBotAI_HasWaypointNavigation()) { //We can see or dmg our closest enemy
			bs->currentEnemy = &g_entities[closestID];
			bs->frame_Enemy_Vis = enemyVisible ? 1 : 0;
			if (enemyVisible)
			{
				bs->lastVisibleEnemyIndex = closestID;
				bs->lastVisibleEnemyTime = level.time;
			}
			if (!bs->cur_ps.duelInProgress && NewBotAI_InFFAExploreWindow(bs, targetMode)) {
				//Exploring after a few duels: a new non-dueling opponent ends the search,
				//and the duel streak starts over.
				bs->duelCompletedCount = 0;
				bs->ffaExploreUntil = 0;
			}
		}
		else { //we can't see our closest enemy, use last attacker
			const int attacker = g_entities[bs->client].client->ps.persistant[PERS_ATTACKER];
			if (PassStandardEnemyChecks(bs, &g_entities[attacker])) {
				bs->currentEnemy = &g_entities[attacker];

				VectorCopy(bs->currentEnemy->client->ps.origin, headlevel);
				headlevel[2] += bs->currentEnemy->client->ps.viewheight - 24;
				if (OrgVisible(bs->eye, headlevel, bs->client))
					bs->frame_Enemy_Vis = 1;
				bs->lastVisibleEnemyIndex = attacker;
				bs->lastVisibleEnemyTime = level.time;
			}
			else {
				if (NewBotAI_ShouldRetainLostSightTarget(bs, oldEnemy))
				{
					bs->currentEnemy = oldEnemy;
				}
				else {
					NewBotAI_PrepareWaypointHandoff(bs, qtrue);
					NewBotAI_RunNavigationOrAlone(bs, thinktime);
					return;
				}
			}
		}
	}

	if (bs->frame_Enemy_Vis)
	{
		bs->enemySeenTime = level.time + ENEMY_FORGET_MS;
	}
	else if (NewBotAI_ShouldRetainLostSightTarget(bs, bs->currentEnemy))
	{
		const int targetTimeoutMs = BotGetTargetTimeoutMs();
		bs->enemySeenTime = bs->lastVisibleEnemyTime + ((targetTimeoutMs > 0) ? targetTimeoutMs : ENEMY_FORGET_MS);
	}
	else if (NewBotAI_ShouldKeepWaypointPursuitTarget(bs, bs->currentEnemy))
	{
		bs->enemySeenTime = level.time + ENEMY_FORGET_MS;
	}
	else
	{
		bs->enemySeenTime = level.time + ENEMY_FORGET_MS;
	}
	bs->frame_Enemy_Len = NewBotAI_GetDist(bs);
	if (!bs->currentEnemy || bs->enemyWaypointFallbackEnemyNum != bs->currentEnemy->s.number)
	{
		bs->enemyWaypointFallbackIndex = -1;
		bs->enemyWaypointFallbackTime = 0;
		bs->enemyWaypointFallbackEnemyNum = bs->currentEnemy ? bs->currentEnemy->s.number : -1;
	}
	if (bs->currentEnemy && bs->currentEnemy->client)
	{
		qboolean activeCombatIntent;
		const qboolean recentlyHurt = (bs->lastHurtTime > level.time - 750) ? qtrue : qfalse;

		activeCombatIntent = (bs->frame_Enemy_Vis ||
			bs->doAttack || bs->doAltAttack ||
			bs->cur_ps.weaponstate == WEAPON_FIRING ||
			bs->cur_ps.weaponstate == WEAPON_CHARGING ||
			bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT ||
			recentlyHurt) ? qtrue : qfalse;

		if (activeCombatIntent)
		{
			bs->combatNavHoldUntil = level.time + NEWBOTAI_COMBAT_DISENGAGE_COOLDOWN_MS;
		}
	}
	if (!(bs->cur_ps.fd.forcePowersActive & (1 << FP_GRIP)))
	{
		bs->gripkickActive = qfalse;
		bs->gripkickJerkUntil = 0;
		bs->gripkickJerkCount = 0;
		bs->gripkickKickCount = 0;
		bs->gripkickJerkDirection = 0;
		bs->gripkickJerkPitch = 0.0f;
		bs->gripkickPitchVariant = 0;
		bs->gripkickAttemptTime = 0;
		bs->gripkickDwellUntil = 0;
		bs->gripkickLookDownUntil = 0;
		bs->gripkickRestackDir = 0;
	}
	//An enemy swap drops any pending pk/ptk kick jump - the schedule was computed for
	//the old opponent's approach.
	if (bs->currentEnemy != oldEnemy)
	{
		bs->pullKickJumpTime = 0;
		bs->enemyWaypointFallbackIndex = -1;
		bs->enemyWaypointFallbackTime = 0;
		bs->enemyWaypointFallbackEnemyNum = bs->currentEnemy ? bs->currentEnemy->s.number : -1;
	}
	if (!bs->cur_ps.saberInFlight)
	{
		bs->saberThrowStartTime = 0;
		bs->saberThrowPassedTarget = qfalse;
		bs->saberThrowPhase = NEWBOTAI_THROW_LAUNCH;
		bs->saberThrowPhaseTime = 0;
		bs->saberThrowSteerTime = 0;
	}
	pendingDuelChallenger = NewBotAI_GetPendingDuelChallenger(bs, targetMode, &pendingDuelType);

	//Human duel offers are accepted right away, ahead of the reaction delay and any
	//pending offer of our own.
	if (pendingDuelChallenger && !(pendingDuelChallenger->r.svFlags & SVF_BOT) &&
		NewBotAI_AcceptDuelFrom(bs, pendingDuelChallenger, pendingDuelType))
	{
		return;
	}
	if (NewBotAI_RunDuelOfferHold(bs))
	{
		return;
	}

	responseDelay = BotGetReflexScaledResponseDelayMs(bs);
	if (responseDelay > 0 && bs->currentEnemy && bs->currentEnemy->client)
	{
	if (bs->currentEnemy != oldEnemy)
	{
		bs->timeToReact = level.time + responseDelay;
	}
	if (bs->timeToReact > level.time)
	{
		bs->doAttack = 0;
		bs->doAltAttack = 0;
		bs->beStill = level.time + 50;
		return;
	}
	}
	if (pendingDuelChallenger &&
	pendingDuelType <= 1 &&
	bs->cur_ps.weapon == WP_SABER &&
	!bs->cur_ps.saberInFlight &&
	bs->cur_ps.saberHolstered)
	{
	if (g_entities[bs->client].client->ps.weaponTime < 1)
	{
		NewBotAI_FaceEntityImmediately(bs, pendingDuelChallenger);
		Cmd_ToggleSaber_f(&g_entities[bs->client]);
		bs->doAttack = 0;
		bs->doAltAttack = 0;
		bs->botDuelRequestThrottleUntil = level.time + NEWBOTAI_DUEL_REQUEST_MIN_INTERVAL_MS;
		bs->duelNoStrafeUntil = level.time + Com_Clampi(0, 10000, bot_duel_nostrafetime.integer);
		bs->beStill = level.time + 2500;
		return;
	}
	}
	if (BotTryAcceptAnyDuelChallenge(bs, targetMode))
	{
	return;
	}

	if (NewBotAI_TryIssueBotDuelChallenge(bs, targetMode))
	{
		return;
	}
	if (BotTargetModeIsForceDuelOnly(targetMode) && !bs->cur_ps.duelInProgress)
	{
		//Exploring for a new partner, or breaking up a stalemate: roam instead of
		//walking up to the current target and idling in front of it.
		if (NewBotAI_InFFAExploreWindow(bs, targetMode) || bs->duelRoamUntil > level.time)
		{
			bs->doAttack = 0;
			bs->doAltAttack = 0;
			NewBotAI_RunNavigationOrAlone(bs, thinktime);
			return;
		}
		NewBotAI_UpdateDuelStalemate(bs, targetMode);
		if (bs->duelRoamUntil > level.time)
		{
			bs->doAttack = 0;
			bs->doAltAttack = 0;
			NewBotAI_RunNavigationOrAlone(bs, thinktime);
			return;
		}
		NewBotAI_RunForceDuelOnly(bs);
		return;
	}
	if (bs->botChallengingTime > level.time)
	{
		for (i = 0; i < MAX_CLIENTS; i++)
		{
			gentity_t *challenger = &g_entities[i];
			if (!challenger->inuse || !challenger->client || challenger->health < 1 || i == bs->client)
			{
				continue;
			}
			if (challenger->client->ps.duelIndex == bs->client && challenger->client->ps.duelTime > level.time)
			{
				bs->currentEnemy = challenger;
				break;
			}
		}
		bs->ideal_viewangles[YAW] = AngleNormalize360(bs->ideal_viewangles[YAW] + 24);
	}

	if (NewBotAI_HasWaypointNavigation() &&
		NewBotAI_ShouldFallbackToWaypoints(bs))
	{
		if (!bs->frame_Enemy_Vis)
		{
			NewBotAI_PrepareWaypointHandoff(
				bs,
				NewBotAI_ShouldKeepWaypointPursuitTarget(bs, bs->currentEnemy) ? qfalse : qtrue);
		}
		bs->navObstacleUntil = 0;
		StandardBotAI(bs, thinktime);
		return;
	}
	else if (!NewBotAI_HasWaypointNavigation())
	{
		bs->navObstacleUntil = 0;
	}
	if (NewBotAI_IsDuelStrafeSuppressed(bs) &&
		!(bs->cur_ps.weapon == WP_SABER && bs->frame_Enemy_Vis && NewBotAI_HasValidCurrentEnemy(bs)))
	{
		if (NewBotAI_HasValidCurrentEnemy(bs))
			NewBotAI_GetAim(bs);
		bs->doAttack = 0;
		bs->doAltAttack = 0;
		if (bs->frame_Enemy_Vis && bs->frame_Enemy_Len > 96.0f)
			trap->EA_MoveForward(bs->client);
		return;
	}
	NewBotAI_ResetRecoveryMovement(bs);

	if (!NewBotAI_HasValidCurrentEnemy(bs))
	{
		bs->frame_Enemy_Vis = 0;
		NewBotAI_PrepareWaypointHandoff(bs, qtrue);
		NewBotAI_RunNavigationOrAlone(bs, thinktime);
		return;
	}

	if (!bs->frame_Enemy_Vis &&
		NewBotAI_HasWaypointNavigation())
	{
		const qboolean forceLostSightReset = NewBotAI_ShouldForceLostSightWaypointReset(bs);
		const qboolean keepWaypointPursuitTarget =
			(NewBotAI_ShouldPursueTargetThroughWaypoints(bs, bs->currentEnemy) &&
			 !forceLostSightReset) ? qtrue : qfalse;
		if ((NewBotAI_ShouldRetainLostSightTarget(bs, bs->currentEnemy) &&
			!forceLostSightReset) ||
			keepWaypointPursuitTarget)
		{
			NewBotAI_ClearLostSightCombatInput(bs);
			NewBotAI_GetAim(bs);
			NewBotAI_RunNavigationOrAlone(bs, thinktime);
			return;
		}

		{
			if (bs->frame_Enemy_Len > 8096)
			{
				bs->frame_Enemy_Len = 0.0f;
			}
			NewBotAI_PrepareWaypointHandoff(bs, qtrue);
		}
		NewBotAI_RunNavigationOrAlone(bs, thinktime);
		return;
	}

	if (!bs->frame_Enemy_Vis && !NewBotAI_HasWaypointNavigation() && bs->frame_Enemy_Len > 8096) {
		NewBotAI_ClearCurrentEnemyLock(bs);
		bs->frame_Enemy_Len = 0.0f;
		NewBotAI_PrepareWaypointHandoff(bs, qtrue);
		NewBotAI_RunNavigationOrAlone(bs, thinktime);
		return;
	}

	/*
	if (NewBotAI_GetDist(bs) > 1024) { //Chase mode, only do this if
		NewBotAI_StrafeJump(bs);
		return;
	}
	*/

	if (g_movementStyle.integer == MV_TRIBES && (g_startingItems.integer & (1 << HI_JETPACK))) {
		NewBotAI_Tribes(bs, thinktime);
		NewBotAI_ApplyRandomStrafeOverlay(bs);
		return;
	}

	bs->saberTechniqueCandidate = bs->cur_ps.weapon == WP_SABER &&
		bs->frame_Enemy_Vis && !bs->cur_ps.saberInFlight &&
		!(g_gunGame.integer && g_entities[bs->client].client->forcedFireMode == 2);
	if (bs->saberTechniqueCandidate)
		NewBotAI_ResetFanChain(bs);

	if (!NewBotAI_UsesSaberDuelPath(NewBotAI_IsSaberOnlyDuel(bs), g_forcePowerDisable.integer, g_flipKick.integer,
		bs->cur_ps.weapon == WP_SABER)) {
		if (bs->currentEnemy->client->ps.fd.forceSide == FORCE_LIGHTSIDE) { // They are LS.
			if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE)
				NewBotAI_LSvLS(bs);
			else
				NewBotAI_DSvLS(bs);
		}
		else { // A sith is amongst us!
			if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE)
				NewBotAI_LSvDS(bs);
			else
				NewBotAI_DSvDS(bs);
		}
	}
	else {//Ruh roh, NF with no kick (or a private saber-only duel)!
		NewBotAI_NF(bs);
		if (NewBotAI_IsSaberOnlyDuel(bs))
		{
			//Saber throw is refused in saber-only duels, so never queue alt-attack there.
			bs->doAltAttack = 0;
		}
	}

	NewBotAI_RunSaberTechniques(bs);
	NewBotAI_ApplyRandomStrafeOverlay(bs);
}

//the main AI loop.
//please don't be too frightened.
void StandardBotAI(bot_state_t *bs, float thinktime)
{
	int wp, enemy;
	int desiredIndex;
	int goalWPIndex;
	int doingFallback = 0;
	int fjHalt;
	vec3_t a, ang, headlevel, eorg, noz_x, noz_y, dif, a_fo;
	float reaction;
	float bLeadAmount;
	int meleestrafe = 0;
	int useTheForce = 0;
	int forceHostile = 0;
	gentity_t *friendInLOF = 0;
	float mLen;
	int visResult = 0;
	int selResult = 0;
	int mineSelect = 0;
	int detSelect = 0;
	vec3_t preFrameGAngles;

	if (gDeactivated)
	{
		bs->wpCurrent = NULL;
		bs->currentEnemy = NULL;
		bs->wpDestination = NULL;
		bs->wpDirection = 0;
		return;
	}

	if (g_entities[bs->client].inuse &&
		g_entities[bs->client].client &&
		g_entities[bs->client].client->sess.sessionTeam == TEAM_SPECTATOR)
	{
		bs->wpCurrent = NULL;
		bs->currentEnemy = NULL;
		bs->wpDestination = NULL;
		bs->wpDirection = 0;
		return;
	}


#ifndef FINAL_BUILD
	if (bot_getinthecarrr.integer)
	{ //stupid vehicle debug, I tire of having to connect another client to test passengers.
		gentity_t *botEnt = &g_entities[bs->client];

		if (botEnt->inuse && botEnt->client && botEnt->client->ps.m_iVehicleNum)
		{ //in a vehicle, so...
			bs->noUseTime = level.time + 5000;

			if (bot_getinthecarrr.integer != 2)
			{
				trap->EA_MoveForward(bs->client);

				if (bot_getinthecarrr.integer == 3)
				{ //use alt fire
					trap->EA_Alt_Attack(bs->client);
				}
			}
		}
		else
		{ //find one, get in
			int i = 0;
			gentity_t *vehicle = NULL;
			//find the nearest, manned vehicle
			while (i < MAX_GENTITIES)
			{
				vehicle = &g_entities[i];

				if (vehicle->inuse && vehicle->client && vehicle->s.eType == ET_NPC &&
					vehicle->s.NPC_class == CLASS_VEHICLE && vehicle->m_pVehicle &&
					(vehicle->client->ps.m_iVehicleNum || bot_getinthecarrr.integer == 2))
				{ //ok, this is a vehicle, and it has a pilot/passengers
					break;
				}
				i++;
			}
			if (i != MAX_GENTITIES && vehicle)
			{ //broke before end so we must've found something
				vec3_t v;

				VectorSubtract(vehicle->client->ps.origin, bs->origin, v);
				VectorNormalize(v);
				vectoangles(v, bs->goalAngles);
				MoveTowardIdealAngles(bs);
				trap->EA_Move(bs->client, v, 5000.0f);

				if (bs->noUseTime < (level.time-400))
				{
					bs->noUseTime = level.time + 500;
				}
			}
		}

		return;
	}
#endif

	if (bot_forgimmick.integer)
	{
		bs->wpCurrent = NULL;
		bs->currentEnemy = NULL;
		bs->wpDestination = NULL;
		bs->wpDirection = 0;

		if (bot_forgimmick.integer == 2)
		{ //for debugging saber stuff, this is handy
			trap->EA_Attack(bs->client);
		}

		if (bot_forgimmick.integer == 3)
		{ //for testing cpu usage moving around rmg terrain without AI
			vec3_t mdir;

			VectorSubtract(bs->origin, vec3_origin, mdir);
			VectorNormalize(mdir);
			trap->EA_Attack(bs->client);
			trap->EA_Move(bs->client, mdir, 5000);
		}

		if (bot_forgimmick.integer == 4)
		{ //constantly move toward client 0
			if (g_entities[0].client && g_entities[0].inuse)
			{
				vec3_t mdir;

				VectorSubtract(g_entities[0].client->ps.origin, bs->origin, mdir);
				VectorNormalize(mdir);
				trap->EA_Move(bs->client, mdir, 5000);
			}
		}

		if (bs->forceMove_Forward)
		{
			if (bs->forceMove_Forward > 0)
			{
				trap->EA_MoveForward(bs->client);
			}
			else
			{
				trap->EA_MoveBack(bs->client);
			}
		}
		if (bs->forceMove_Right)
		{
			if (bs->forceMove_Right > 0)
			{
				trap->EA_MoveRight(bs->client);
			}
			else
			{
				trap->EA_MoveLeft(bs->client);
			}
		}
		if (bs->forceMove_Up)
		{
			trap->EA_Jump(bs->client);
		}
		return;
	}

	if (!bs->lastDeadTime)
	{ //just spawned in?
		bs->lastDeadTime = level.time;
	}

	if (g_entities[bs->client].health < 1)
	{
		bs->lastDeadTime = level.time;

		if (!bs->deathActivitiesDone && bs->lastHurt && bs->lastHurt->client && bs->lastHurt->s.number != bs->client)
		{
			BotDeathNotify(bs);
			if (PassLovedOneCheck(bs, bs->lastHurt))
			{
				//CHAT: Died
				bs->chatObject = bs->lastHurt;
				bs->chatAltObject = NULL;
				BotDoChat(bs, "Died", 0);
			}
			else if (!PassLovedOneCheck(bs, bs->lastHurt) &&
				botstates[bs->lastHurt->s.number] &&
				PassLovedOneCheck(botstates[bs->lastHurt->s.number], &g_entities[bs->client]))
			{ //killed by a bot that I love, but that does not love me
				bs->chatObject = bs->lastHurt;
				bs->chatAltObject = NULL;
				BotDoChat(bs, "KilledOnPurposeByLove", 0);
			}

			bs->deathActivitiesDone = 1;
		}

		bs->wpCurrent = NULL;
		bs->currentEnemy = NULL;
		bs->wpDestination = NULL;
		bs->wpCamping = NULL;
		bs->wpCampingTo = NULL;
		bs->wpStoreDest = NULL;
		bs->wpDestIgnoreTime = 0;
		bs->wpDestSwitchTime = 0;
		bs->wpSeenTime = 0;
		bs->wpDirection = 0;

		if (rand()%10 < 5 &&
			(!bs->doChat || bs->chatTime < level.time))
		{
			trap->EA_Attack(bs->client);
		}

		return;
	}

	VectorCopy(bs->goalAngles, preFrameGAngles);

	bs->doAttack = 0;
	bs->doAltAttack = 0;
	//reset the attack states

	if (bs->isSquadLeader)
	{
		CommanderBotAI(bs);
	}
	else
	{
		BotDoTeamplayAI(bs);
	}

	if (!bs->currentEnemy)
	{
		bs->frame_Enemy_Vis = 0;
	}

	if (bs->revengeEnemy && bs->revengeEnemy->client &&
		bs->revengeEnemy->client->pers.connected != CON_CONNECTED && bs->revengeEnemy->client->pers.connected != CON_CONNECTING)
	{
		bs->revengeEnemy = NULL;
		bs->revengeHateLevel = 0;
	}

	if (bs->currentEnemy && bs->currentEnemy->client &&
		bs->currentEnemy->client->pers.connected != CON_CONNECTED && bs->currentEnemy->client->pers.connected != CON_CONNECTING)
	{
		bs->currentEnemy = NULL;
	}

	fjHalt = 0;

#ifndef FORCEJUMP_INSTANTMETHOD
	if (bs->forceJumpChargeTime > level.time)
	{
		useTheForce = 1;
		forceHostile = 0;
	}

	if (bs->currentEnemy && bs->currentEnemy->client && bs->frame_Enemy_Vis && bs->forceJumpChargeTime < level.time)
#else
	if (bs->currentEnemy && bs->currentEnemy->client && bs->frame_Enemy_Vis)
#endif
	{
		VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
		vectoangles(a_fo, a_fo);

		//do this above all things
		//Item 5: no instant push-out while gripped here - that path fired regardless of the
		//bot's (slow) low-skill aim and let level 1-2 bots break free immediately. Escaping
		//an opponent's grip is now left to NewBotAI_ReactToBeingGripped, which is gated by
		//bot_gkmistakebias. A scripted force push (doForcePush) is still honored.
		if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_PUSH)) && bs->doForcePush > level.time && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_PUSH]][FP_PUSH] /*&& InFieldOfVision(bs->viewangles, 50, a_fo)*/)
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_PUSH;
			useTheForce = 1;
			forceHostile = 1;
		}
		else if (bs->cur_ps.fd.forceSide == FORCE_DARKSIDE)
		{ //try dark side powers
		  //in order of priority top to bottom
			if (NewBotAI_CanContinueLightningBurst(bs))
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_LIGHTNING;
				useTheForce = 1;
				forceHostile = 1;
			}
			else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_GRIP)) && (bs->cur_ps.fd.forcePowersActive & (1 << FP_GRIP)) && InFieldOfVision(bs->viewangles, 50, a_fo))
			{ //already gripping someone, so hold it
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_GRIP;
				useTheForce = 1;
				forceHostile = 1;
			}
			else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_LIGHTNING)) &&
				bs->cur_ps.fd.forcePowerLevel[FP_LIGHTNING] > FORCE_LEVEL_0 &&
				!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB)) &&
				bs->frame_Enemy_Len > BotGetLightningStartDistance(bs) &&
				level.clients[bs->client].ps.fd.forcePower > 50 &&
				!NewBotAI_IsEnemySaberThreatImminent(bs) &&
				InFieldOfVision(bs->viewangles, 50, a_fo))
			{ //At long range, prioritize lightning regardless of other dark-side options.
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_LIGHTNING;
				NewBotAI_StartLightningBurst(bs);
				useTheForce = 1;
				forceHostile = 1;
			}
			else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_LIGHTNING)) &&
				bs->cur_ps.fd.forcePowerLevel[FP_LIGHTNING] > FORCE_LEVEL_0 &&
				!(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_ABSORB)) &&
				bs->frame_Enemy_Len >= BotGetLightningStartDistance(bs) &&
				bs->frame_Enemy_Len <= BotGetLightningMaxDistance(bs) &&
				level.clients[bs->client].ps.fd.forcePower > 50 &&
				InFieldOfVision(bs->viewangles, 50, a_fo))
			{ //Use lightning from the configured range out; point-blank zaps still waste force.
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_LIGHTNING;
				NewBotAI_StartLightningBurst(bs);
				useTheForce = 1;
				forceHostile = 1;
			}
			else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_GRIP)) && bs->frame_Enemy_Len < MAX_GRIP_DISTANCE && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_GRIP]][FP_GRIP] && InFieldOfVision(bs->viewangles, 50, a_fo))
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_GRIP;
				useTheForce = 1;
				forceHostile = 1;
			}
			else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_RAGE)) && g_entities[bs->client].health < 25 && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_RAGE]][FP_RAGE])
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_RAGE;
				useTheForce = 1;
				forceHostile = 0;
			}
			else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_DRAIN)) && bs->frame_Enemy_Len < MAX_DRAIN_DISTANCE && level.clients[bs->client].ps.fd.forcePower > 50 && InFieldOfVision(bs->viewangles, 50, a_fo) && bs->currentEnemy->client->ps.fd.forcePower > 10 && bs->currentEnemy->client->ps.fd.forceSide == FORCE_LIGHTSIDE)
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_DRAIN;
				useTheForce = 1;
				forceHostile = 1;
			}
		}
		else if (bs->cur_ps.fd.forceSide == FORCE_LIGHTSIDE)
		{ //try light side powers
			if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_ABSORB)) && bs->cur_ps.fd.forceGripCripple &&
				 level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_ABSORB]][FP_ABSORB])
			{ //absorb to get out
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB;
				useTheForce = 1;
				forceHostile = 0;
			}
			else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_ABSORB)) && bs->cur_ps.electrifyTime >= level.time &&
				 level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_ABSORB]][FP_ABSORB])
			{ //absorb lightning
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB;
				useTheForce = 1;
				forceHostile = 0;
			}
			else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_TELEPATHY)) && bs->frame_Enemy_Len < MAX_TRICK_DISTANCE && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_TELEPATHY]][FP_TELEPATHY] && InFieldOfVision(bs->viewangles, 50, a_fo) && !(bs->currentEnemy->client->ps.fd.forcePowersActive & (1 << FP_SEE)))
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_TELEPATHY;
				useTheForce = 1;
				forceHostile = 1;
			}
			else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_ABSORB)) && g_entities[bs->client].health < 75 && bs->currentEnemy->client->ps.fd.forceSide == FORCE_DARKSIDE && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_ABSORB]][FP_ABSORB])
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_ABSORB;
				useTheForce = 1;
				forceHostile = 0;
			}
			else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_PROTECT)) && g_entities[bs->client].health < 35 && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_PROTECT]][FP_PROTECT])
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_PROTECT;
				useTheForce = 1;
				forceHostile = 0;
			}
		}

		if (!useTheForce)
		{ //try neutral powers
			//Item 5: the gripped->push escape here also bypassed the low-skill slow aim
			//(and bot_mistakebias) to instantly break the opponent's grip - removed so
			//level 1-2 bots actually stay gripped unless the NewBotAI grip-escape lets
			//them out.
			if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_SPEED)) && g_entities[bs->client].health < 25 && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_SPEED]][FP_SPEED])
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_SPEED;
				useTheForce = 1;
				forceHostile = 0;
			}
			else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_SEE)) && BotMindTricked(bs->client, bs->currentEnemy->s.number) && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_SEE]][FP_SEE])
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_SEE;
				useTheForce = 1;
				forceHostile = 0;
			}
			else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_PULL)) && bs->frame_Enemy_Len < 256 && level.clients[bs->client].ps.fd.forcePower > 75 && InFieldOfVision(bs->viewangles, 50, a_fo))
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_PULL;
				useTheForce = 1;
				forceHostile = 1;
			}
		}
	}

	if (!useTheForce)
	{ //try powers that we don't care if we have an enemy for
		if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_HEAL)) && g_entities[bs->client].health < 50 && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_HEAL]][FP_HEAL] && bs->cur_ps.fd.forcePowerLevel[FP_HEAL] > FORCE_LEVEL_1)
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_HEAL;
			useTheForce = 1;
			forceHostile = 0;
		}
		else if ((bs->cur_ps.fd.forcePowersKnown & (1 << FP_HEAL)) && g_entities[bs->client].health < 50 && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_HEAL]][FP_HEAL] && !bs->currentEnemy && bs->isCamping > level.time)
		{ //only meditate and heal if we're camping
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_HEAL;
			useTheForce = 1;
			forceHostile = 0;
		}
		else if (!bs->currentEnemy && bs->wpDestination &&
			(bs->cur_ps.fd.forcePowersKnown & (1 << FP_SPEED)) &&
			level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_SPEED]][FP_SPEED])
		{ //Not targeting anyone and far from our waypoint destination - use force speed to
		  //close the distance through the waypoint network faster instead of only relying
		  //on it defensively/offensively during combat.
			vec3_t toDestination;

			VectorSubtract(bs->wpDestination->origin, bs->origin, toDestination);
			if (VectorLengthSquared(toDestination) > (512.0f * 512.0f))
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_SPEED;
				useTheForce = 1;
				forceHostile = 0;
			}
		}
	}

	if (useTheForce && forceHostile)
	{
		if (bs->currentEnemy && bs->currentEnemy->client &&
			!ForcePowerUsableOn(&g_entities[bs->client], bs->currentEnemy, level.clients[bs->client].ps.fd.forcePowerSelected))
		{
			useTheForce = 0;
			forceHostile = 0;
		}
	}

	doingFallback = 0;

	bs->deathActivitiesDone = 0;

	if (BotUseInventoryItem(bs))
	{
		if (rand()%10 < 5)
		{
			trap->EA_Use(bs->client);
		}
	}

	if (bs->cur_ps.ammo[weaponData[bs->cur_ps.weapon].ammoIndex] < weaponData[bs->cur_ps.weapon].energyPerShot)
	{
		if (BotTryAnotherWeapon(bs))
		{
			return;
		}
	}
	else
	{
		if (bs->currentEnemy && bs->lastVisibleEnemyIndex == bs->currentEnemy->s.number &&
			bs->frame_Enemy_Vis && bs->forceWeaponSelect /*&& bs->plantContinue < level.time*/)
		{
			bs->forceWeaponSelect = 0;
		}

		if (bs->plantContinue > level.time)
		{
			bs->doAttack = 1;
			bs->destinationGrabTime = 0;
		}

		if (!bs->forceWeaponSelect && bs->cur_ps.hasDetPackPlanted && bs->plantKillEmAll > level.time)
		{
			bs->forceWeaponSelect = WP_DET_PACK;
		}

		if (bs->forceWeaponSelect)
		{
			selResult = BotSelectChoiceWeapon(bs, bs->forceWeaponSelect, 1);
		}

		if (selResult)
		{
			if (selResult == 2)
			{ //newly selected
				return;
			}
		}
		else if (BotSelectIdealWeapon(bs))
		{
			return;
		}
	}
	/*if (BotSelectMelee(bs))
	{
		return;
	}*/

	reaction = BotGetReflexScaledResponseDelayMs(bs);

	if (reaction < 0)
	{
		reaction = 0;
	}
	if (reaction > 2000)
	{
		reaction = 2000;
	}

	if (!bs->currentEnemy)
	{
		bs->timeToReact = level.time + reaction;
	}

	if (bs->cur_ps.weapon == WP_DET_PACK && bs->cur_ps.hasDetPackPlanted && bs->plantKillEmAll > level.time)
	{
		bs->doAltAttack = 1;
	}

	if (bs->wpCamping)
	{
		if (bs->isCamping < level.time)
		{
			bs->wpCamping = NULL;
			bs->isCamping = 0;
		}

		if (bs->currentEnemy && bs->frame_Enemy_Vis)
		{
			bs->wpCamping = NULL;
			bs->isCamping = 0;
		}
	}

	if (bs->wpCurrent)
	{
		bs->wpNoPathSince = 0;
	}

	if (bs->wpCurrent &&
		(bs->wpSeenTime < level.time || bs->wpTravelTime < level.time))
	{
		bs->lastWPIndex = bs->wpCurrent->index;
		bs->lastWPDir = bs->wpDirection;
		bs->wpCurrent = NULL;
	}

	if (bs->currentEnemy)
	{
		if (bs->enemySeenTime < level.time ||
			!PassStandardEnemyChecks(bs, bs->currentEnemy))
		{
			if (bs->revengeEnemy == bs->currentEnemy &&
				bs->currentEnemy->health < 1 &&
				bs->lastAttacked && bs->lastAttacked == bs->currentEnemy)
			{
				//CHAT: Destroyed hated one [KilledHatedOne section]
				bs->chatObject = bs->revengeEnemy;
				bs->chatAltObject = NULL;
				BotDoChat(bs, "KilledHatedOne", 1);
				bs->revengeEnemy = NULL;
				bs->revengeHateLevel = 0;
			}
			else if (bs->currentEnemy->health < 1 && PassLovedOneCheck(bs, bs->currentEnemy) &&
				bs->lastAttacked && bs->lastAttacked == bs->currentEnemy)
			{
				//CHAT: Killed
				bs->chatObject = bs->currentEnemy;
				bs->chatAltObject = NULL;
				BotDoChat(bs, "Killed", 0);
			}

			bs->currentEnemy = NULL;
		}
	}

	BotTryAcceptAnyDuelChallenge(bs, BotGetNewBotAITargetMode());
	//Apparently this "allows you to cheese" when fighting against bots. I'm not sure why you'd want to con bots
	//into an easy kill, since they're bots and all. But whatever.

	if (!bs->wpCurrent)
	{
		wp = BotNav_NearestVisibleFreshWP(bs);

		if (wp != -1)
		{
			//If we have a live objective or enemy, keep waypoint direction ordered toward
			//that target so we don't bounce between a local triangle of nearby points.
			if (bs->wpDestination &&
				bs->wpDestination->index >= 0 && bs->wpDestination->index < gWPNum &&
				gWPArray[bs->wpDestination->index] && gWPArray[bs->wpDestination->index]->inuse &&
				gWPArray[wp] && gWPArray[wp]->inuse)
			{
				NewBotAI_SelectWaypointDirectionTowardTarget(bs, wp, bs->wpDestination->index);
			}
			else if (bs->currentEnemy && bs->currentEnemy->client && gWPArray[wp] && gWPArray[wp]->inuse)
			{
				int enemyWP = bs->currentEnemy->waypoint;
				if (enemyWP < 0 || enemyWP >= gWPNum || !gWPArray[enemyWP] || !gWPArray[enemyWP]->inuse)
				{
					enemyWP = -1;
				}
				if (enemyWP != -1)
				{
					NewBotAI_SelectWaypointDirectionTowardTarget(bs, wp, enemyWP);
				}
			}

			bs->wpCurrent = gWPArray[wp];
			bs->wpSeenTime = level.time + 1500;
			bs->wpTravelTime = level.time + 10000; //never take more than 10 seconds to travel to a waypoint
			bs->lastWPIndex = bs->wpCurrent->index;
			bs->lastWPDir = bs->wpDirection;
		}
	}

	{
		const float targetDistanceLimit = BotGetTargetDistanceLimit();
		const qboolean shouldRescanForCloserTarget =
			(bs->currentEnemy && bs->frame_Enemy_Vis &&
			 (targetDistanceLimit > 0.0f) && (bs->frame_Enemy_Len > targetDistanceLimit)) ? qtrue : qfalse;
		if (bs->enemySeenTime < level.time || !bs->frame_Enemy_Vis || !bs->currentEnemy || shouldRescanForCloserTarget)
		{
			const qboolean keepWaypointPursuitTarget =
				(!bs->frame_Enemy_Vis &&
				 NewBotAI_ShouldKeepWaypointPursuitTarget(bs, bs->currentEnemy)) ? qtrue : qfalse;

			enemy = ScanForEnemies(bs);

			if (enemy != -1)
			{
				bs->currentEnemy = &g_entities[enemy];
				bs->enemySeenTime = level.time + ENEMY_FORGET_MS;
			}
			else if (bs->currentEnemy &&
				(bs->enemySeenTime < level.time || shouldRescanForCloserTarget) &&
				!keepWaypointPursuitTarget)
			{
				bs->currentEnemy = NULL;
				bs->enemySeenTime = 0;
				bs->lastVisibleEnemyIndex = ENTITYNUM_NONE;
				bs->lastVisibleEnemyTime = 0;
				bs->enemyWaypointFallbackIndex = -1;
				bs->enemyWaypointFallbackTime = 0;
				bs->enemyWaypointFallbackEnemyNum = -1;
				NewBotAI_ResetRecoveryMovement(bs);
				NewBotAI_ClearLostSightCombatInput(bs);
			}
		}
	}

	if (!bs->squadLeader && !bs->isSquadLeader)
	{
		BotScanForLeader(bs);
	}

	if (!bs->squadLeader && bs->squadCannotLead < level.time)
	{ //if still no leader after scanning, then become a squad leader
		bs->isSquadLeader = 1;
	}

	if (bs->isSquadLeader && bs->squadLeader)
	{ //we don't follow anyone if we are a leader
		bs->squadLeader = NULL;
	}

	//ESTABLISH VISIBILITIES AND DISTANCES FOR THE WHOLE FRAME HERE
	if (bs->wpCurrent)
	{
		if (RMG.integer)
		{ //this is somewhat hacky, but in RMG we don't really care about vertical placement because points are scattered across only the terrain.
			vec3_t vecB, vecC;

			vecB[0] = bs->origin[0];
			vecB[1] = bs->origin[1];
			vecB[2] = bs->origin[2];

			vecC[0] = bs->wpCurrent->origin[0];
			vecC[1] = bs->wpCurrent->origin[1];
			vecC[2] = vecB[2];


			VectorSubtract(vecC, vecB, a);
		}
		else
		{
			VectorSubtract(bs->wpCurrent->origin, bs->origin, a);
		}
		bs->frame_Waypoint_Len = VectorLength(a);

		visResult = WPOrgVisible(&g_entities[bs->client], bs->origin, bs->wpCurrent->origin, bs->client);

		if (visResult == 2)
		{
			bs->frame_Waypoint_Vis = 0;
			bs->wpSeenTime = 0;
		}
		else if (visResult)
		{
			bs->frame_Waypoint_Vis = 1;
		}
		else
		{
			bs->frame_Waypoint_Vis = 0;
		}
	}

	if (bs->currentEnemy)
	{
		if (bs->currentEnemy->client)
		{
			VectorCopy(bs->currentEnemy->client->ps.origin, eorg);
			eorg[2] += bs->currentEnemy->client->ps.viewheight;
		}
		else
		{
			VectorCopy(bs->currentEnemy->s.origin, eorg);
		}

		VectorSubtract(eorg, bs->eye, a);
		bs->frame_Enemy_Len = VectorLength(a);

		if (OrgVisible(bs->eye, eorg, bs->client))
		{
			bs->frame_Enemy_Vis = 1;
			VectorCopy(eorg, bs->lastEnemySpotted);
			VectorCopy(bs->origin, bs->hereWhenSpotted);
			bs->lastVisibleEnemyIndex = bs->currentEnemy->s.number;
			bs->lastVisibleEnemyTime = level.time;
			//VectorCopy(bs->eye, bs->lastEnemySpotted);
			bs->hitSpotted = 0;
		}
		else
		{
			bs->frame_Enemy_Vis = 0;
		}
	}
	else
	{
		bs->lastVisibleEnemyIndex = ENTITYNUM_NONE;
		bs->lastVisibleEnemyTime = 0;
	}
	//END

	if (bs->frame_Enemy_Vis)
	{
		bs->enemySeenTime = level.time + ENEMY_FORGET_MS;
	}

	if (bs->wpCurrent)
	{
		int wpTouchDist = BOT_WPTOUCH_DISTANCE;
		wpobject_t *beforeAdvance = bs->wpCurrent;
		BotSFJ_AdvancePassedWaypoints(bs);
		if (bs->wpCurrent != beforeAdvance)
		{
			VectorSubtract(bs->wpCurrent->origin, bs->origin, a);
			if (RMG.integer)
				a[2] = 0.0f;
			bs->frame_Waypoint_Len = VectorLength(a);
			visResult = WPOrgVisible(&g_entities[bs->client],
				bs->origin, bs->wpCurrent->origin, bs->client);
			bs->frame_Waypoint_Vis = BotSFJ_WaypointVisible(visResult);
			if (visResult == 2)
				bs->wpSeenTime = 0;
		}
		WPConstantRoutine(bs);

		if (!bs->wpCurrent)
		{ //WPConstantRoutine has the ability to nullify the waypoint if it fails certain checks, so..
			return;
		}

		if (bs->frame_Waypoint_Vis || (bs->wpCurrent->flags & WPFLAG_NOVIS))
		{
			if (RMG.integer)
			{
				bs->wpSeenTime = level.time + 5000; //if we lose sight of the point, we have 1.5 seconds to regain it before we drop it
			}
			else
			{
				bs->wpSeenTime = level.time + 1500; //if we lose sight of the point, we have 1.5 seconds to regain it before we drop it
			}
		}
		BotNav_CheckProgress(bs);
		if (!bs->wpCurrent)
			return;
		VectorCopy(bs->wpCurrent->origin, bs->goalPosition);
		if (bs->sfjCorridorForwardGoal)
			VectorCopy(bs->sfjIntentDestination, bs->goalPosition);
		/* Recorded strafe routes take precedence over waypoints while being followed. */
		BotSFJ_GetTrackRouteGoal(bs, bs->goalPosition);
		if (bs->wpDirection)
		{
			goalWPIndex = bs->wpCurrent->index-1;
		}
		else
		{
			goalWPIndex = bs->wpCurrent->index+1;
		}

		if (bs->wpCamping)
		{
			VectorSubtract(bs->wpCampingTo->origin, bs->origin, a);
			vectoangles(a, ang);
			VectorCopy(ang, bs->goalAngles);

			VectorSubtract(bs->origin, bs->wpCamping->origin, a);
			if (VectorLength(a) < 64)
			{
				VectorCopy(bs->wpCamping->origin, bs->goalPosition);
				bs->beStill = level.time + 1000;

				if (!bs->campStanding)
				{
					bs->duckTime = level.time + 1000;
				}
			}
		}
		else if (gWPArray[goalWPIndex] && gWPArray[goalWPIndex]->inuse &&
			!(gLevelFlags & LEVELFLAG_NOPOINTPREDICTION))
		{
			VectorSubtract(gWPArray[goalWPIndex]->origin, bs->origin, a);
			vectoangles(a, ang);
			VectorCopy(ang, bs->goalAngles);
		}
		else
		{
			VectorSubtract(bs->wpCurrent->origin, bs->origin, a);
			vectoangles(a, ang);
			VectorCopy(ang, bs->goalAngles);
		}

		if (bs->destinationGrabTime < level.time /*&& (!bs->wpDestination || (bs->currentEnemy && bs->frame_Enemy_Vis))*/)
		{
			GetIdealDestination(bs);
		}

		if (bs->wpCurrent && bs->wpDestination)
		{
			if (TotalTrailDistance(bs->wpCurrent->index, bs->wpDestination->index, bs) == -1)
			{
				bs->wpDestination = NULL;
				bs->destinationGrabTime = level.time + 10000;
			}
		}

		if (RMG.integer)
		{
			if (bs->frame_Waypoint_Vis)
			{
				if (bs->wpCurrent && !bs->wpCurrent->flags)
				{
					wpTouchDist *= 3;
				}
			}
		}

		if (!bs->sfjCorridorForwardGoal &&
			(bs->frame_Waypoint_Len < wpTouchDist ||
				(RMG.integer && bs->frame_Waypoint_Len < wpTouchDist*2)))
		{
			const qboolean activeCombatLock = (bs->currentEnemy && bs->frame_Enemy_Vis) ? qtrue : qfalse;
			const qboolean canSkipAhead = !activeCombatLock;
			const int maxWaypointSkip = Com_Clampi(0, 16, bot_waypointskip.integer);
			int skipStep;
			if (activeCombatLock && maxWaypointSkip > 0)
				BotSFJ_DebugReject(bs, "waypoint skip: visible enemy combat lock");
			WPTouchRoutine(bs);

			if (!bs->wpDirection)
			{
				desiredIndex = bs->wpCurrent->index+1;
			}
			else
			{
				desiredIndex = bs->wpCurrent->index-1;
			}

			if (desiredIndex >= 0 && desiredIndex < gWPNum &&
				gWPArray[desiredIndex] &&
				gWPArray[desiredIndex]->inuse &&
				PassWayCheck(bs, desiredIndex))
			{
				if (canSkipAhead && maxWaypointSkip > 0)
				{
					int prevIndex = bs->wpCurrent->index;
					for (skipStep = 0; skipStep < maxWaypointSkip; skipStep++)
					{
						wpobject_t *currentWP = gWPArray[desiredIndex];
						int nextIndex = -1;
						int n;
						float bestScore = 0.0f;
						vec3_t preferredDir;
						float preferredLen;

						if (!currentWP)
						{
							break;
						}
						if (currentWP->flags || currentWP == bs->wpDestination ||
							bs->wpCurrent->flags)
						{
							BotSFJ_DebugReject(bs, "waypoint skip: required waypoint or destination");
							break;
						}
						if (prevIndex < 0 || prevIndex >= gWPNum || !gWPArray[prevIndex])
						{
							break;
						}

						VectorSubtract(currentWP->origin, gWPArray[prevIndex]->origin, preferredDir);
						preferredDir[2] = 0.0f;
						preferredLen = VectorNormalize(preferredDir);
						if (preferredLen <= 0.0f)
						{
							break;
						}

						for (n = 0; n < (currentWP->neighbornum > 0 ? currentWP->neighbornum : 1); n++)
						{
							const int neighborIndex = currentWP->neighbornum > 0 ?
								currentWP->neighbors[n].num : desiredIndex + (bs->wpDirection ? -1 : 1);
							vec3_t neighborDir;
							float score;

							if (neighborIndex < 0 || neighborIndex >= gWPNum ||
								!gWPArray[neighborIndex] || !gWPArray[neighborIndex]->inuse ||
								neighborIndex == prevIndex ||
								!PassWayCheck(bs, neighborIndex))
							{
								continue;
							}

							VectorSubtract(gWPArray[neighborIndex]->origin, currentWP->origin, neighborDir);
							neighborDir[2] = 0.0f;
							if (VectorNormalize(neighborDir) <= 0.0f)
							{
								continue;
							}
							score = DotProduct(preferredDir, neighborDir);
							if (!BotSFJ_WaypointSkipAllows(gWPArray[neighborIndex]->flags,
								gWPArray[neighborIndex]->origin[2] - currentWP->origin[2],
								score, 1))
							{
								continue;
							}

							if (nextIndex == -1 || score > bestScore)
							{
								nextIndex = neighborIndex;
								bestScore = score;
							}
						}

						if (nextIndex == -1)
						{
							BotSFJ_DebugReject(bs, "waypoint skip: no straight eligible link");
							break;
						}
						if (!BotWaypointSkipPathSafe(bs, gWPArray[nextIndex]->origin, 640.0f))
						{
							BotSFJ_DebugReject(bs, "waypoint skip: blocked path or unsafe floor");
							break;
						}

						prevIndex = desiredIndex;
						desiredIndex = nextIndex;
					}
				}
				bs->lastWPIndex = bs->wpCurrent->index;
				bs->lastWPDir = bs->wpDirection;
				bs->wpCurrent = gWPArray[desiredIndex];
			}
			else
			{
				if (bs->wpDestination)
				{
					bs->wpDestination = NULL;
					bs->destinationGrabTime = level.time + 10000;
				}

				bs->lastWPIndex = bs->wpCurrent->index;

				if (bs->wpDirection)
				{
					bs->wpDirection = 0;
				}
				else
				{
					bs->wpDirection = 1;
				}

				bs->lastWPDir = bs->wpDirection;
			}
		}
	}
	else //We can't find a waypoint, going to need a fallback routine.
	{
		doingFallback = BotFallbackNavigation(bs);
	}

	if (RMG.integer)
	{ //for RMG if the bot sticks around an area too long, jump around randomly some to spread to a new area (horrible hacky method)
		vec3_t vSubDif;

		VectorSubtract(bs->origin, bs->lastSignificantAreaChange, vSubDif);
		if (VectorLength(vSubDif) > 1500)
		{
			VectorCopy(bs->origin, bs->lastSignificantAreaChange);
			bs->lastSignificantChangeTime = level.time + 20000;
		}

		if (bs->lastSignificantChangeTime < level.time)
		{
			bs->iHaveNoIdeaWhereIAmGoing = level.time + 17000;
		}
	}

	if (bs->iHaveNoIdeaWhereIAmGoing > level.time && !bs->currentEnemy)
	{
		VectorCopy(preFrameGAngles, bs->goalAngles);
		bs->wpCurrent = NULL;
		bs->wpSwitchTime = level.time + 150;
		doingFallback = BotFallbackNavigation(bs);
		bs->lastSignificantChangeTime = level.time + 25000;
	}

	if (bs->wpCurrent && RMG.integer)
	{
		qboolean doJ = qfalse;

		if (bs->wpCurrent->origin[2]-192 > bs->origin[2])
		{
			doJ = qtrue;
		}
		else if ((bs->wpTravelTime - level.time) < 5000 && bs->wpCurrent->origin[2]-64 > bs->origin[2])
		{
			doJ = qtrue;
		}
		else if ((bs->wpTravelTime - level.time) < 7000 && (bs->wpCurrent->flags & WPFLAG_RED_FLAG))
		{
			if ((level.time - bs->jumpTime) > 200)
			{
				bs->jumpTime = level.time + 100;
				bs->jumpHoldTime = level.time + 100;
				bs->jDelay = 0;
			}
		}
		else if ((bs->wpTravelTime - level.time) < 7000 && (bs->wpCurrent->flags & WPFLAG_BLUE_FLAG))
		{
			if ((level.time - bs->jumpTime) > 200)
			{
				bs->jumpTime = level.time + 100;
				bs->jumpHoldTime = level.time + 100;
				bs->jDelay = 0;
			}
		}
		else if (bs->wpCurrent->index > 0)
		{
			if ((bs->wpTravelTime - level.time) < 7000)
			{
				if ((gWPArray[bs->wpCurrent->index-1]->flags & WPFLAG_RED_FLAG) ||
					(gWPArray[bs->wpCurrent->index-1]->flags & WPFLAG_BLUE_FLAG))
				{
					if ((level.time - bs->jumpTime) > 200)
					{
						bs->jumpTime = level.time + 100;
						bs->jumpHoldTime = level.time + 100;
						bs->jDelay = 0;
					}
				}
			}
		}

		if (doJ)
		{
			bs->jumpTime = level.time + 1500;
			bs->jumpHoldTime = level.time + 1500;
			bs->jDelay = 0;
		}
	}

	if (doingFallback)
	{
		bs->doingFallback = qtrue;
	}
	else
	{
		bs->doingFallback = qfalse;
	}

	if (bs->timeToReact < level.time && bs->currentEnemy && bs->enemySeenTime > level.time + (ENEMY_FORGET_MS - (ENEMY_FORGET_MS*0.2)))
	{
		if (bs->frame_Enemy_Vis)
		{
			CombatBotAI(bs, thinktime);
		}
		else if (bs->cur_ps.weaponstate == WEAPON_CHARGING_ALT)
		{ //keep charging in case we see him again before we lose track of him
			bs->doAltAttack = 1;
		}
		else if (bs->cur_ps.weaponstate == WEAPON_CHARGING)
		{ //keep charging in case we see him again before we lose track of him
			bs->doAttack = 1;
		}

		if (bs->destinationGrabTime > level.time + 100)
		{
			bs->destinationGrabTime = level.time + 100; //assures that we will continue staying within a general area of where we want to be in a combat situation
		}

		if (bs->currentEnemy->client)
		{
			VectorCopy(bs->currentEnemy->client->ps.origin, headlevel);
			headlevel[2] += bs->currentEnemy->client->ps.viewheight - 24;
		}
		else
		{
			VectorCopy(bs->currentEnemy->client->ps.origin, headlevel);
		}

		if (!bs->frame_Enemy_Vis)
		{
			//if (!bs->hitSpotted && VectorLength(a) > 256)
			if (OrgVisible(bs->eye, bs->lastEnemySpotted, -1))
			{
				VectorCopy(bs->lastEnemySpotted, headlevel);
				VectorSubtract(headlevel, bs->eye, a);
				vectoangles(a, ang);
				VectorCopy(ang, bs->goalAngles);

				if (bs->cur_ps.weapon == WP_FLECHETTE &&
					bs->cur_ps.weaponstate == WEAPON_READY &&
					bs->currentEnemy && bs->currentEnemy->client)
				{
					mLen = VectorLength(a) > 128;
					if (mLen > 128 && mLen < 1024)
					{
						VectorSubtract(bs->currentEnemy->client->ps.origin, bs->lastEnemySpotted, a);

						if (VectorLength(a) < 300)
						{
							bs->doAltAttack = 1;
						}
					}
				}
			}
		}
		else
		{
			bLeadAmount = BotWeaponCanLead(bs);
			if ((bs->skills.accuracy/bs->settings.skill) <= 8 &&
				bLeadAmount)
			{
				BotAimLeading(bs, headlevel, bLeadAmount);
			}
			else
			{
				VectorSubtract(headlevel, bs->eye, a);
				vectoangles(a, ang);
				VectorCopy(ang, bs->goalAngles);
			}

			BotAimOffsetGoalAngles(bs);
		}
	}

	if (bs->cur_ps.saberInFlight)
	{
		if (bs->saberThrowStartTime <= 0)
			bs->saberThrowStartTime = bs->cur_ps.saberDidThrowTime > 0 ?
				bs->cur_ps.saberDidThrowTime : level.time;
		bs->saberThrowTime = level.time + Q_irand(4000, 10000);
	}

	if (bs->currentEnemy)
	{
		if (BotGetWeaponRange(bs) == BWEAPONRANGE_SABER)
		{
			int saberRange = SABER_ATTACK_RANGE;

			VectorSubtract(bs->currentEnemy->client->ps.origin, bs->eye, a_fo);
			vectoangles(a_fo, a_fo);

			if (bs->saberPowerTime < level.time)
			{ //Don't just use strong attacks constantly, switch around a bit
				if (Q_irand(1, 10) <= 5)
				{
					bs->saberPower = qtrue;
				}
				else
				{
					bs->saberPower = qfalse;
				}

				bs->saberPowerTime = level.time + Q_irand(3000, 15000);
			}

			if ( g_entities[bs->client].client->ps.fd.saberAnimLevel != SS_STAFF
				&& g_entities[bs->client].client->ps.fd.saberAnimLevel != SS_DUAL )
			{
				if (bs->currentEnemy->health > 75
					&& g_entities[bs->client].client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE] > 2)
				{
					if (g_entities[bs->client].client->ps.fd.saberAnimLevel != SS_STRONG
						&& bs->saberPower)
					{ //if we are up against someone with a lot of health and we have a strong attack available, then h4q them
						Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
					}
				}
				else if (bs->currentEnemy->health > 40
					&& g_entities[bs->client].client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE] > 1)
				{
					if (g_entities[bs->client].client->ps.fd.saberAnimLevel != SS_MEDIUM)
					{ //they're down on health a little, use level 2 if we can
						Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
					}
				}
				else
				{
					if (g_entities[bs->client].client->ps.fd.saberAnimLevel != SS_FAST)
					{ //they've gone below 40 health, go at them with quick attacks
						Cmd_SaberAttackCycle_f(&g_entities[bs->client]);
					}
				}
			}

			if (level.gametype == GT_SINGLE_PLAYER)
			{
				saberRange *= 3;
			}

			if (bs->frame_Enemy_Len <= saberRange)
			{
				SaberCombatHandling(bs);

				if (bs->frame_Enemy_Len < 80)
				{
					meleestrafe = 1;
				}
			}
			else if (bs->saberThrowTime < level.time && !bs->cur_ps.saberInFlight &&
				(bs->cur_ps.fd.forcePowersKnown & (1 << FP_SABERTHROW)) &&
				InFieldOfVision(bs->viewangles, 30, a_fo) &&
				bs->frame_Enemy_Len < BOT_SABER_THROW_RANGE &&
				bs->cur_ps.fd.saberAnimLevel != SS_STAFF)
			{
				bs->doAltAttack = 1;
				bs->doAttack = 0;
			}
			else if (bs->cur_ps.saberInFlight && bs->frame_Enemy_Len > 300 && bs->frame_Enemy_Len < BOT_SABER_THROW_RANGE)
			{
				bs->doAltAttack = 1;
				bs->doAttack = 0;
			}
		}
		else if (BotGetWeaponRange(bs) == BWEAPONRANGE_MELEE)
		{
			if (bs->frame_Enemy_Len <= MELEE_ATTACK_RANGE)
			{
				MeleeCombatHandling(bs);
				meleestrafe = 1;
			}
		}
	}

	if (doingFallback && bs->currentEnemy) //just stand and fire if we have no idea where we are
	{
		VectorCopy(bs->origin, bs->goalPosition);
	}

	if (bs->forceJumping > level.time)
	{
		VectorCopy(bs->origin, noz_x);
		VectorCopy(bs->goalPosition, noz_y);

		noz_x[2] = noz_y[2];

		VectorSubtract(noz_x, noz_y, noz_x);

		if (VectorLength(noz_x) < 32)
		{
			fjHalt = 1;
		}
	}

	if (bs->doChat && bs->chatTime > level.time && (!bs->currentEnemy || !bs->frame_Enemy_Vis))
	{
		return;
	}
	else if (bs->doChat && bs->currentEnemy && bs->frame_Enemy_Vis)
	{
		//bs->chatTime = level.time + bs->chatTime_stored;
		bs->doChat = 0; //do we want to keep the bot waiting to chat until after the enemy is gone?
		bs->chatTeam = 0;
	}
	else if (bs->doChat && bs->chatTime <= level.time)
	{
		if (bs->chatTeam)
		{
			trap->EA_SayTeam(bs->client, bs->currentChat);
			bs->chatTeam = 0;
		}
		else
		{
			trap->EA_Say(bs->client, bs->currentChat);
		}
		if (bs->doChat == 2)
		{
			BotReplyGreetings(bs);
		}
		bs->doChat = 0;
	}

	CTFFlagMovement(bs);

	if (/*bs->wpDestination &&*/ bs->shootGoal &&
		/*bs->wpDestination->associated_entity == bs->shootGoal->s.number &&*/
		bs->shootGoal->health > 0 && bs->shootGoal->takedamage)
	{
		dif[0] = (bs->shootGoal->r.absmax[0]+bs->shootGoal->r.absmin[0])/2;
		dif[1] = (bs->shootGoal->r.absmax[1]+bs->shootGoal->r.absmin[1])/2;
		dif[2] = (bs->shootGoal->r.absmax[2]+bs->shootGoal->r.absmin[2])/2;

		if (!bs->currentEnemy || bs->frame_Enemy_Len > 256)
		{ //if someone is close then don't stop shooting them for this
			VectorSubtract(dif, bs->eye, a);
			vectoangles(a, a);
			VectorCopy(a, bs->goalAngles);

			if (InFieldOfVision(bs->viewangles, 30, a) &&
				EntityVisibleBox(bs->origin, NULL, NULL, dif, bs->client, bs->shootGoal->s.number))
			{
				bs->doAttack = 1;
			}
		}
	}

	if (bs->cur_ps.hasDetPackPlanted)
	{ //check if our enemy gets near it and detonate if he does
		BotCheckDetPacks(bs);
	}
	else if (bs->currentEnemy && bs->lastVisibleEnemyIndex == bs->currentEnemy->s.number && !bs->frame_Enemy_Vis && bs->plantTime < level.time &&
		!bs->doAttack && !bs->doAltAttack)
	{
		VectorSubtract(bs->origin, bs->hereWhenSpotted, a);

		if (bs->plantDecided > level.time || (bs->frame_Enemy_Len < BOT_PLANT_DISTANCE*2 && VectorLength(a) < BOT_PLANT_DISTANCE))
		{
			mineSelect = BotSelectChoiceWeapon(bs, WP_TRIP_MINE, 0);
			detSelect = BotSelectChoiceWeapon(bs, WP_DET_PACK, 0);
			if (bs->cur_ps.hasDetPackPlanted)
			{
				detSelect = 0;
			}

			if (bs->plantDecided > level.time && bs->forceWeaponSelect &&
				bs->cur_ps.weapon == bs->forceWeaponSelect)
			{
				bs->doAttack = 1;
				bs->plantDecided = 0;
				bs->plantTime = level.time + BOT_PLANT_INTERVAL;
				bs->plantContinue = level.time + 500;
				bs->beStill = level.time + 500;
			}
			else if (mineSelect || detSelect)
			{
				if (BotSurfaceNear(bs))
				{
					if (!mineSelect)
					{ //if no mines use detpacks, otherwise use mines
						mineSelect = WP_DET_PACK;
					}
					else
					{
						mineSelect = WP_TRIP_MINE;
					}

					detSelect = BotSelectChoiceWeapon(bs, mineSelect, 1);

					if (detSelect && detSelect != 2)
					{ //We have it and it is now our weapon
						bs->plantDecided = level.time + 1000;
						bs->forceWeaponSelect = mineSelect;
						return;
					}
					else if (detSelect == 2)
					{
						bs->forceWeaponSelect = mineSelect;
						return;
					}
				}
			}
		}
	}
	else if (bs->plantContinue < level.time)
	{
		bs->forceWeaponSelect = 0;
	}

	if (level.gametype == GT_JEDIMASTER && !bs->cur_ps.isJediMaster && bs->jmState == -1 && gJMSaberEnt && gJMSaberEnt->inuse)
	{
		vec3_t saberLen;
		float fSaberLen = 0;

		VectorSubtract(bs->origin, gJMSaberEnt->r.currentOrigin, saberLen);
		fSaberLen = VectorLength(saberLen);

		if (fSaberLen < 256)
		{
			if (OrgVisible(bs->origin, gJMSaberEnt->r.currentOrigin, bs->client))
			{
				VectorCopy(gJMSaberEnt->r.currentOrigin, bs->goalPosition);
			}
		}
	}

	if (bs->beStill < level.time && !WaitingForNow(bs, bs->goalPosition) && !fjHalt)
	{
		VectorSubtract(bs->goalPosition, bs->origin, bs->goalMovedir);
		if (bs->sfjCorridorValid && bs->sfjOwnsInput && bs->wpCurrent &&
			!bs->wpCurrent->flags && !bs->wpCurrent->forceJumpTo &&
			!(bs->currentEnemy && bs->frame_Enemy_Vis) &&
			(VectorCompare(bs->goalPosition, bs->wpCurrent->origin) ||
				(bs->sfjCorridorForwardGoal &&
					VectorCompare(bs->goalPosition, bs->sfjIntentDestination))) &&
			BotSFJ_CanOwnInput(bot_strafejumps.integer,
				BotSFJ_IntentIsFresh(level.time, bs->sfjIntentTime),
				bot_strafejumpfrequency.integer > 0 && bs->sfjSafetyUntil >= level.time,
				bs->sfjPhase))
		{
			/* Trail elevation must not queue a downward input during flight. */
			bs->goalMovedir[2] = 0.0f;
		}
		VectorNormalize(bs->goalMovedir);

		//Falling hazard awareness: don't walk off ledges or into lava/death pits.
		//If the movement direction leads to a dangerous drop, stop and let the
		//waypoint system re-path on the next think.
		if (BotNav_CheckFallingHazard(bs, bs->goalMovedir,
			(bs->currentEnemy && bs->frame_Enemy_Vis) ? qtrue : qfalse))
		{
			bs->beStill = level.time + 100;
			//Prefer the next waypoint along the route over a full re-path, which
			//tends to pick a point behind the bot and send it walking back.
			BotNav_AvoidHazardWaypoint(bs);
		}
		else if (bs->jumpTime > level.time && bs->jDelay < level.time &&
			level.clients[bs->client].pers.cmd.upmove > 0)
		{
		//	trap->EA_Move(bs->client, bs->origin, 5000);
			bs->beStill = level.time + 200;
		}
		else
		{
			trap->EA_Move(bs->client, bs->goalMovedir, 5000);
		}

		if (meleestrafe && !NewBotAI_IsRecoveryMovementActive(bs) && !NewBotAI_HasExclusiveFlipkickMovement(bs))
		{
			StrafeTracing(bs);
		}

		if (!NewBotAI_IsDuelStrafeSuppressed(bs) &&
			!NewBotAI_IsRecoveryMovementActive(bs) &&
			!NewBotAI_HasExclusiveFlipkickMovement(bs) &&
			meleestrafe && bs->meleeStrafeDisable < level.time)
		{
			if (bs->meleeStrafeDir < 0)
			{
				trap->EA_MoveLeft(bs->client);
			}
			else if (bs->meleeStrafeDir > 0)
			{
				trap->EA_MoveRight(bs->client);
			}
		}

		if (BotTrace_Jump(bs, bs->goalPosition))
		{
			bs->jumpTime = level.time + 100;
			bs->duckTime = 0; //never crouch-jump over an obstacle
		}
		else if (bs->jumpTime <= level.time && BotTrace_Duck(bs, bs->goalPosition))
		{
			bs->duckTime = level.time + 100;
		}
#ifdef BOT_STRAFE_AVOIDANCE
		else if ((!bs->frame_Enemy_Vis || !bs->currentEnemy || bs->frame_Enemy_Len > 512) &&
			!NewBotAI_HasWaypointNavigation() &&
			!NewBotAI_IsRecoveryMovementActive(bs) &&
			!NewBotAI_HasExclusiveFlipkickMovement(bs))
		{
			//Only strafe around obstacles when not actively engaged in close combat.
			//When fighting at saber/melee range, lateral obstacle avoidance causes the
			//bot to stutter and glitch about walls instead of focusing on the fight.
			int strafeAround = BotTrace_Strafe(bs, bs->goalPosition);

			if (!NewBotAI_IsDuelStrafeSuppressed(bs) && strafeAround == STRAFEAROUND_RIGHT)
			{
				trap->EA_MoveRight(bs->client);
			}
			else if (!NewBotAI_IsDuelStrafeSuppressed(bs) && strafeAround == STRAFEAROUND_LEFT)
			{
				trap->EA_MoveLeft(bs->client);
			}
		}
#endif
	}

#ifndef FORCEJUMP_INSTANTMETHOD
	if (bs->forceJumpChargeTime > level.time)
	{
		bs->jumpTime = 0;
	}
#endif

	if (bs->jumpPrep > level.time)
	{
		bs->forceJumpChargeTime = 0;
	}

	if (bs->forceJumpChargeTime > level.time)
	{
		bs->jumpHoldTime = ((bs->forceJumpChargeTime - level.time)/2) + level.time;
		bs->forceJumpChargeTime = 0;
	}

	if (bs->jumpHoldTime > level.time)
	{
		bs->jumpTime = bs->jumpHoldTime;
	}

	if (bs->jumpTime > level.time && bs->jDelay < level.time)
	{
		if (bs->jumpHoldTime > level.time)
		{
			trap->EA_Jump(bs->client);
			if (bs->wpCurrent)
			{
				if ((bs->wpCurrent->origin[2] - bs->origin[2]) < 64)
				{
					trap->EA_MoveForward(bs->client);
				}
			}
			else
			{
				trap->EA_MoveForward(bs->client);
			}
			if (g_entities[bs->client].client->ps.groundEntityNum == ENTITYNUM_NONE)
			{
				g_entities[bs->client].client->ps.pm_flags |= PMF_JUMP_HELD;
			}
		}
		else if (!(bs->cur_ps.pm_flags & PMF_JUMP_HELD))
		{
			trap->EA_Jump(bs->client);
		}
	}

	if (bs->duckTime > level.time)
	{
		trap->EA_Crouch(bs->client);
	}

	if ( bs->dangerousObject && bs->dangerousObject->inuse && bs->dangerousObject->health > 0 &&
		bs->dangerousObject->takedamage && (!bs->frame_Enemy_Vis || !bs->currentEnemy) &&
		(BotGetWeaponRange(bs) == BWEAPONRANGE_MID || BotGetWeaponRange(bs) == BWEAPONRANGE_LONG) &&
		bs->cur_ps.weapon != WP_DET_PACK && bs->cur_ps.weapon != WP_TRIP_MINE &&
		!bs->shootGoal )
	{
		float danLen;

		VectorSubtract(bs->dangerousObject->r.currentOrigin, bs->eye, a);

		danLen = VectorLength(a);

		if (danLen > 256)
		{
			vectoangles(a, a);
			VectorCopy(a, bs->goalAngles);

			if (Q_irand(1, 10) < 5)
			{
				bs->goalAngles[YAW] += Q_irand(0, 3);
				bs->goalAngles[PITCH] += Q_irand(0, 3);
			}
			else
			{
				bs->goalAngles[YAW] -= Q_irand(0, 3);
				bs->goalAngles[PITCH] -= Q_irand(0, 3);
			}

			if (InFieldOfVision(bs->viewangles, 30, a) &&
				EntityVisibleBox(bs->origin, NULL, NULL, bs->dangerousObject->r.currentOrigin, bs->client, bs->dangerousObject->s.number))
			{
				bs->doAttack = 1;
			}
		}
	}

	if (PrimFiring(bs) ||
		AltFiring(bs))
	{
		friendInLOF = CheckForFriendInLOF(bs);

		if (friendInLOF)
		{
			if (PrimFiring(bs))
			{
				KeepPrimFromFiring(bs);
			}
			if (AltFiring(bs))
			{
				KeepAltFromFiring(bs);
			}
			if (useTheForce && forceHostile)
			{
				useTheForce = 0;
			}

			if (!useTheForce && friendInLOF->client)
			{ //we have a friend here and are not currently using force powers, see if we can help them out
				if (friendInLOF->health <= 50 && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_TEAM_HEAL]][FP_TEAM_HEAL])
				{
					level.clients[bs->client].ps.fd.forcePowerSelected = FP_TEAM_HEAL;
					useTheForce = 1;
					forceHostile = 0;
				}
				else if (friendInLOF->client->ps.fd.forcePower <= 50 && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_TEAM_FORCE]][FP_TEAM_FORCE])
				{
					level.clients[bs->client].ps.fd.forcePowerSelected = FP_TEAM_FORCE;
					useTheForce = 1;
					forceHostile = 0;
				}
			}
		}
	}
	else if (BG_IsTeamGame(level.gametype))
	{ //still check for anyone to help..
		friendInLOF = CheckForFriendInLOF(bs);

		if (!useTheForce && friendInLOF)
		{
			if (friendInLOF->health <= 50 && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_TEAM_HEAL]][FP_TEAM_HEAL])
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_TEAM_HEAL;
				useTheForce = 1;
				forceHostile = 0;
			}
			else if (friendInLOF->client->ps.fd.forcePower <= 50 && level.clients[bs->client].ps.fd.forcePower > forcePowerNeeded[level.clients[bs->client].ps.fd.forcePowerLevel[FP_TEAM_FORCE]][FP_TEAM_FORCE])
			{
				level.clients[bs->client].ps.fd.forcePowerSelected = FP_TEAM_FORCE;
				useTheForce = 1;
				forceHostile = 0;
			}
		}
	}

	if (bs->doAttack && bs->cur_ps.weapon == WP_DET_PACK &&
		bs->cur_ps.hasDetPackPlanted)
	{ //maybe a bit hackish, but bots only want to plant one of these at any given time to avoid complications
		bs->doAttack = 0;
	}

	if (bs->doAttack && bs->cur_ps.weapon == WP_SABER &&
		bs->saberDefending && bs->currentEnemy && bs->currentEnemy->client &&
		BotWeaponBlockable(bs->currentEnemy->client->ps.weapon) &&
		!(bs->cur_ps.saberInFlight && !bs->cur_ps.saberEntityNum))
	{ //never suppress the recall toggle below just because we were blocking - without the
	  //saber in hand there is nothing to defend with anyway
		bs->doAttack = 0;
	}

	if (bs->cur_ps.saberLockTime > level.time)
	{
		if (rand()%10 < 5)
		{
			bs->doAttack = 1;
		}
		else
		{
			bs->doAttack = 0;
		}
	}

	{
		const qboolean hasDroppedOwnSaber = NewBotAI_HasDroppedOwnSaber(bs);
		const qboolean enemySaberThreatImminent = NewBotAI_IsEnemySaberThreatImminent(bs);

		if (enemySaberThreatImminent)
		{
			bs->doAttack = 0;
			bs->doAltAttack = 0;
			useTheForce = 0;
		}

		if (hasDroppedOwnSaber && !enemySaberThreatImminent)
		{
			//saber knocked away: the engine only recalls the saber on a fresh +attack edge, and
			//a held button counts as one press forever. Toggle a genuine press/release edge -
			//held 20ms, released 5ms - so repeated +attack inputs keep firing until the saber
			//returns, instead of a single held button that only counts once.
			bs->doAltAttack = 0;
			if (bs->saberRetrieveSpamTime <= level.time)
			{
				bs->saberRetrieveSpamHeld = !bs->saberRetrieveSpamHeld;
				bs->saberRetrieveSpamTime = level.time + (bs->saberRetrieveSpamHeld ? 20 : 5);
			}
			bs->doAttack = bs->saberRetrieveSpamHeld ? 1 : 0;
		}
		else
		{
			if (enemySaberThreatImminent)
			{
				bs->doAttack = 0;
				bs->doAltAttack = 0;
			}
			//Saber is back (or not ours to recall): reset the toggle so the next knock-away
			//starts with a fresh press.
			bs->saberRetrieveSpamTime = 0;
			bs->saberRetrieveSpamHeld = qfalse;
		}
	}

	if (bs->doAttack && bs->cur_ps.weapon == WP_SABER)
	{
		const qboolean hasDroppedOwnSaber = NewBotAI_HasDroppedOwnSaber(bs);
		const qboolean airborneStart = (bs->cur_ps.groundEntityNum == ENTITYNUM_NONE &&
			!BG_SaberInAttack(bs->cur_ps.saberMove)) ? qtrue : qfalse;

		if (bs->currentEnemy && bs->currentEnemy->client)
		{
			if (NewBotAI_GetTotalHealthDelta(bs) < 0 &&
				!BG_SaberInAttack(bs->cur_ps.saberMove) &&
				!hasDroppedOwnSaber)
			{
				bs->doAttack = 0;
			}
		}

		if (airborneStart && !hasDroppedOwnSaber && !NewBotAI_MayStartAirborneSwing(bs))
		{
			bs->doAttack = 0;
		}
	}

	NewBotAI_ApplyJumpAttackGate(bs);
	NewBotAI_BlockAccidentalSaberSpecialMoves(bs);

	if (bs->doAttack)
	{
		trap->EA_Attack(bs->client);
	}
	else if (bs->doAltAttack)
	{
		trap->EA_Alt_Attack(bs->client);
	}

	if ((!useTheForce ||
		level.clients[bs->client].ps.fd.forcePowerSelected != FP_LIGHTNING) &&
		!(bs->cur_ps.fd.forcePowersActive & (1 << FP_LIGHTNING)))
	{
		NewBotAI_ClearLightningBurst(bs);
	}

	if (useTheForce)
	{
#ifndef FORCEJUMP_INSTANTMETHOD
		if (bs->forceJumpChargeTime > level.time)
		{
			level.clients[bs->client].ps.fd.forcePowerSelected = FP_LEVITATION;
			trap->EA_ForcePower(bs->client);
		}
		else
		{
#endif
			if (bot_forcepowers.integer && !g_forcePowerDisable.integer)
			{
				trap->EA_ForcePower(bs->client);
			}
#ifndef FORCEJUMP_INSTANTMETHOD
		}
#endif
	}

	MoveTowardIdealAngles(bs);
}

int gUpdateVars = 0;

/*
==================
BotAIStartFrame
==================
*/
int BotAIStartFrame(int time) {
	int i;
	int elapsed_time, thinktime;
	static int local_time;
//	static int botlib_residual;
	static int lastbotthink_time;
	const int perfStart = trap->Milliseconds();

	if (gUpdateVars < level.time)
	{
		trap->Cvar_Update(&bot_camp);
		trap->Cvar_Update(&bot_attachments);
#ifndef FINAL_BUILD
		trap->Cvar_Update(&bot_getinthecarrr);
#endif
		gUpdateVars = level.time + 1000;
	}

	G_CheckBotSpawn();

	//rww - addl bot frame functions
	if (gBotEdit)
	{
		trap->Cvar_Update(&bot_wp_info);
		BotWaypointRender();
	}

	UpdateEventTracker();
	//end rww

	//cap the bot think time
	//if the bot think time changed we should reschedule the bots
	if (BOT_THINK_TIME != lastbotthink_time) {
		lastbotthink_time = BOT_THINK_TIME;
		BotScheduleBotThink();
	}

	elapsed_time = time - local_time;
	local_time = time;

	if (elapsed_time > BOT_THINK_TIME) thinktime = elapsed_time;
	else thinktime = BOT_THINK_TIME;

	// execute scheduled bot AI
	for( i = 0; i < MAX_CLIENTS; i++ ) {
		if( !botstates[i] || !botstates[i]->inuse ) {
			continue;
		}
		//
		botstates[i]->botthink_residual += elapsed_time;
		//
		if ( botstates[i]->botthink_residual >= thinktime ) {
			botstates[i]->botthink_residual -= thinktime;

			if (g_entities[i].client->pers.connected == CON_CONNECTED) {
				BotAI(i, (float) thinktime / 1000);
			}
		}
	}

	// execute bot user commands every frame
	for( i = 0; i < MAX_CLIENTS; i++ ) {
		if( !botstates[i] || !botstates[i]->inuse ) {
			continue;
		}
		if( g_entities[i].client->pers.connected != CON_CONNECTED ) {
			continue;
		}

		BotUpdateInput(botstates[i], time, elapsed_time);
		trap->BotUserCommand(botstates[i]->client, &botstates[i]->lastucmd);
	}

	G_PerfWarn("bot AI frame", trap->Milliseconds() - perfStart);
	return qtrue;
}

/*
==============
BotAISetup
==============
*/
int BotAISetup( int restart ) {
	//rww - new bot cvars..
#ifndef FINAL_BUILD
	trap->Cvar_Register(&bot_getinthecarrr, "bot_getinthecarrr", "0", 0);
#endif

#ifdef _DEBUG
	trap->Cvar_Register(&bot_nogoals, "bot_nogoals", "0", CVAR_CHEAT);
	trap->Cvar_Register(&bot_debugmessages, "bot_debugmessages", "0", CVAR_CHEAT);
#endif

	trap->Cvar_Register(&bot_attachments, "bot_attachments", "1", 0);
	trap->Cvar_Register(&bot_camp, "bot_camp", "1", 0);

	trap->Cvar_Register(&bot_wp_info, "bot_wp_info", "1", 0);
	trap->Cvar_Register(&bot_wp_edit, "bot_wp_edit", "0", CVAR_CHEAT);
	trap->Cvar_Register(&bot_wp_clearweight, "bot_wp_clearweight", "1", 0);
	trap->Cvar_Register(&bot_wp_distconnect, "bot_wp_distconnect", "1", 0);
	trap->Cvar_Register(&bot_wp_visconnect, "bot_wp_visconnect", "1", 0);

	//end rww

	//if the game is restarted for a tournament
	if (restart) {
		return qtrue;
	}

	//initialize the bot states
	memset( botstates, 0, sizeof(botstates) );

	if (!trap->BotLibSetup())
	{
		return qfalse; //wts?!
	}

	return qtrue;
}

/*
==============
BotAIShutdown
==============
*/
int BotAIShutdown( int restart ) {

	int i;

	//if the game is restarted for a tournament
	if ( restart ) {
		//shutdown all the bots in the botlib
		for (i = 0; i < MAX_CLIENTS; i++) {
			if (botstates[i] && botstates[i]->inuse) {
				BotAIShutdownClient(botstates[i]->client, restart);
			}
		}
		//don't shutdown the bot library
	}
	else {
		trap->BotLibShutdown();
	}
	return qtrue;
}
