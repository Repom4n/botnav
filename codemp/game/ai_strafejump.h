#ifndef AI_STRAFEJUMP_H
#define AI_STRAFEJUMP_H

#include <math.h>

#define BOT_SFJ_INTENT_MAX_AGE_MS 250
#define BOT_SFJ_ABORT_COOLDOWN_MS 500
#define BOT_SFJ_PURSUIT_START_DISTANCE 512.0f
#define BOT_SFJ_PURSUIT_STOP_DISTANCE 384.0f
#define BOT_SFJ_PURSUIT_START_RATE 80.0f
#define BOT_SFJ_PURSUIT_STOP_RATE 20.0f
#define BOT_SFJ_JUMP_VELOCITY 225.0f
#define BOT_SFJ_MAX_ARC_STEPS 256

static inline int BotSFJ_WaypointBudget(int budget)
{
	return budget < 1 ? 1 : (budget > 512 ? 512 : budget);
}

static inline int BotSFJ_WaypointVisible(int visibility)
{
	return visibility != 0 && visibility != 2;
}

static inline int BotSFJ_UseClippedForwardTarget(int passed, float endpointProgress,
	float firstTargetDistance)
{
	return passed && (endpointProgress < 0.0f || firstTargetDistance > 640.0f);
}

/* Horizontal progress only: a validated jump may be well above its trail. */
static inline int BotSFJ_WaypointPassed(const float *origin, const float *waypoint,
	const float *direction, float halfWidth)
{
	const float x = origin[0] - waypoint[0];
	const float y = origin[1] - waypoint[1];
	const float progress = x * direction[0] + y * direction[1];
	const float lateral = x * direction[1] - y * direction[0];

	return progress >= 0.0f && lateral * lateral <= halfWidth * halfWidth;
}

static inline int BotSFJ_AdvanceLinkAllows(int requiredFlags, int linked,
	int passable, float heightDelta, float baseHeightDelta, float alignment,
	float endpointProgress)
{
	return !requiredFlags && linked && passable &&
		fabsf(heightDelta) <= 32.0f && fabsf(baseHeightDelta) <= 64.0f &&
		alignment >= 0.9f && endpointProgress >= 0.0f;
}

static inline int BotSFJ_StartIntervalMs(int frequency)
{
	if (frequency <= 0)
		return 0;
	if (frequency > 1000)
		frequency = 1000;
	return 100000 / frequency;
}

static inline int BotSFJ_WaypointSkipAllows(int requiredFlags, float heightDelta,
	float alignment, int clearPath)
{
	return !requiredFlags && fabsf(heightDelta) <= 32.0f &&
		alignment >= 0.9f && clearPath;
}

typedef enum
{
	BOT_SFJ_PHASE_OFF = 0,
	BOT_SFJ_PHASE_PREPARE,
	BOT_SFJ_PHASE_TAKEOFF,
	BOT_SFJ_PHASE_AIR,
	BOT_SFJ_PHASE_LANDING,
	BOT_SFJ_PHASE_REJUMP,
	BOT_SFJ_PHASE_ABORT
} bot_sfj_phase_t;

typedef enum
{
	BOT_SFJ_INTENT_NONE = 0,
	BOT_SFJ_INTENT_NAVIGATION,
	BOT_SFJ_INTENT_RETREAT,
	BOT_SFJ_INTENT_PURSUIT
} bot_sfj_intent_t;

static inline int BotSFJ_IntentIsFresh(int now, int intentTime)
{
	return intentTime > 0 && now >= intentTime &&
		now - intentTime <= BOT_SFJ_INTENT_MAX_AGE_MS;
}

static inline int BotSFJ_UpdatePursuitLatchEx(int latched, float distance,
	float previousDistance, int elapsedMs, float startDistance, float stopDistance)
{
	float separationRate;

	if (elapsedMs <= 0 || elapsedMs > 1000 || previousDistance <= 0.0f)
		return distance >= stopDistance ? latched : 0;
	separationRate = (distance - previousDistance) * 1000.0f / (float)elapsedMs;
	if (latched)
		return distance > stopDistance &&
			separationRate > BOT_SFJ_PURSUIT_STOP_RATE;
	return distance >= startDistance &&
		separationRate >= BOT_SFJ_PURSUIT_START_RATE;
}

static inline int BotSFJ_UpdatePursuitLatch(int latched, float distance, float previousDistance,
	int elapsedMs)
{
	return BotSFJ_UpdatePursuitLatchEx(latched, distance, previousDistance, elapsedMs,
		BOT_SFJ_PURSUIT_START_DISTANCE, BOT_SFJ_PURSUIT_STOP_DISTANCE);
}

/* Stop distance used with a configurable start distance (bot_minstrafe). */
static inline float BotSFJ_StopDistanceFor(float startDistance)
{
	if (startDistance <= 0.0f)
		return 0.0f;
	return startDistance * (BOT_SFJ_PURSUIT_STOP_DISTANCE / BOT_SFJ_PURSUIT_START_DISTANCE);
}

static inline int BotSFJ_CanOwnInput(int enabled, int freshIntent, int eligible,
	bot_sfj_phase_t phase)
{
	return enabled && freshIntent && eligible &&
		phase != BOT_SFJ_PHASE_OFF && phase != BOT_SFJ_PHASE_ABORT;
}

static inline bot_sfj_phase_t BotSFJ_NextPhase(bot_sfj_phase_t phase, int enabled,
	int freshIntent, int eligible, int grounded, int phaseAgeMs)
{
	if (!enabled)
		return BOT_SFJ_PHASE_OFF;
	if (!freshIntent || !eligible)
		return BOT_SFJ_PHASE_ABORT;

	switch (phase)
	{
	case BOT_SFJ_PHASE_PREPARE:
		return grounded && phaseAgeMs > 0 ? BOT_SFJ_PHASE_TAKEOFF : phase;
	case BOT_SFJ_PHASE_TAKEOFF:
	case BOT_SFJ_PHASE_REJUMP:
		return grounded ? phase : BOT_SFJ_PHASE_AIR;
	case BOT_SFJ_PHASE_AIR:
		return grounded ? BOT_SFJ_PHASE_LANDING : phase;
	case BOT_SFJ_PHASE_LANDING:
		return grounded && phaseAgeMs > 0 ? BOT_SFJ_PHASE_REJUMP : phase;
	default:
		return phase;
	}
}

static inline float BotSFJ_AngleDelta(float angle1, float angle2)
{
	float delta = fmodf(angle1 - angle2, 360.0f);
	if (delta > 180.0f)
		delta -= 360.0f;
	else if (delta < -180.0f)
		delta += 360.0f;
	return delta;
}

/*
 * PM_AirMove uses PM_Accelerate(wishdir, ps->speed, pm_airaccelerate). For
 * forward-only input, aim at that acceleration boundary and clamp toward the
 * route so low speed never produces acos-domain or zero-vector failures.
 */
static inline float BotSFJ_CommandYaw(float routeYaw, float velocityX, float velocityY,
	float wishSpeed, float airAccelerate, float commandSeconds, int side)
{
	const float radiansToDegrees = 57.29577951308232f;
	float speed = sqrtf(velocityX * velocityX + velocityY * velocityY);
	float velocityYaw;
	float accelSpeed;
	float cosine;
	float offset;
	float yaw;

	if (speed < 32.0f || wishSpeed <= 0.0f)
		return routeYaw;
	if (commandSeconds < 0.001f)
		commandSeconds = 0.001f;
	else if (commandSeconds > 0.1f)
		commandSeconds = 0.1f;
	accelSpeed = airAccelerate * commandSeconds * wishSpeed;
	cosine = (wishSpeed - accelSpeed) / speed;
	if (cosine < -1.0f)
		cosine = -1.0f;
	else if (cosine > 1.0f)
		cosine = 1.0f;
	offset = acosf(cosine) * radiansToDegrees;
	if (offset > 70.0f)
		offset = 70.0f;
	velocityYaw = atan2f(velocityY, velocityX) * radiansToDegrees;
	yaw = velocityYaw + (side < 0 ? -offset : offset);
	if (BotSFJ_AngleDelta(yaw, routeYaw) > 70.0f)
		yaw = routeYaw + 70.0f;
	else if (BotSFJ_AngleDelta(yaw, routeYaw) < -70.0f)
		yaw = routeYaw - 70.0f;
	return yaw;
}

static inline int BotSFJ_SelectSide(float routeYaw, float velocityX, float velocityY,
	int fallbackSide)
{
	const float radiansToDegrees = 57.29577951308232f;
	float speed = sqrtf(velocityX * velocityX + velocityY * velocityY);
	float velocityYaw;
	float routeDelta;

	if (speed < 32.0f)
		return fallbackSide < 0 ? -1 : 1;
	velocityYaw = atan2f(velocityY, velocityX) * radiansToDegrees;
	routeDelta = BotSFJ_AngleDelta(routeYaw, velocityYaw);
	if (routeDelta > 5.0f)
		return 1;
	if (routeDelta < -5.0f)
		return -1;
	return fallbackSide < 0 ? -1 : 1;
}

static inline int BotSFJ_JumpPressed(bot_sfj_phase_t phase, int grounded)
{
	return grounded &&
		(phase == BOT_SFJ_PHASE_TAKEOFF || phase == BOT_SFJ_PHASE_REJUMP);
}

/*
 * A grounded JKA jump calls PM_CheckJump once in PM_WalkMove and again from
 * PM_AirMove in the same slice.  The second call applies this levitation
 * launch velocity even when the next user command releases jump.
 */
static inline float BotSFJ_JKALaunchVelocity(int forceLevel, float height,
	int forceEligible)
{
	static const float jumpHeight[] = {32.0f, 96.0f, 192.0f, 384.0f};
	static const float jumpStrength[] = {225.0f, 420.0f, 590.0f, 840.0f};

	if (!forceEligible || forceLevel <= 0 || forceLevel > 3)
		return BOT_SFJ_JUMP_VELOCITY;
	if (height < 0.0f)
		height = 0.0f;
	return ((jumpHeight[forceLevel] - height) / jumpHeight[forceLevel]) *
		jumpStrength[forceLevel] / 10.0f + BOT_SFJ_JUMP_VELOCITY;
}

static inline void BotSFJ_PredictReleasedVertical(int forceLevel, int forceEligible,
	int hasForcePower, float gravity, int commandMsec, int sliceMsec,
	float *height, float *velocity)
{
	int remaining = commandMsec;
	float z = 0.0f;
	float vz = BOT_SFJ_JUMP_VELOCITY;

	if (sliceMsec < 1)
		sliceMsec = 1;
	while (remaining > 0)
	{
		const int msec = remaining < sliceMsec ? remaining : sliceMsec;
		const float seconds = (float)msec / 1000.0f;
		float nextVz;

		if (forceEligible && forceLevel > 0 && (z <= 32.0f || hasForcePower))
			vz = BotSFJ_JKALaunchVelocity(forceLevel, z, 1);
		else if (vz > BOT_SFJ_JUMP_VELOCITY)
			vz = BOT_SFJ_JUMP_VELOCITY;
		nextVz = vz - gravity * seconds;
		z += (vz + nextVz) * 0.5f * seconds;
		vz = nextVz;
		remaining -= msec;
	}
	if (height)
		*height = z;
	if (velocity)
		*velocity = vz;
}

static inline int BotSFJ_PmoveSliceMsec(int commandMsec, int fixed,
	int fixedMsec, int raceChopped)
{
	if (commandMsec < 1)
		commandMsec = 1;
	if (raceChopped && commandMsec > 8)
		return 8;
	if (fixed)
	{
		if (fixedMsec < 1)
			fixedMsec = 1;
		else if (fixedMsec > 66)
			fixedMsec = 66;
		return commandMsec < fixedMsec ? commandMsec : fixedMsec;
	}
	return commandMsec < 66 ? commandMsec : 66;
}

/*
 * Mirrors BotInputToUserCommand's horizontal projection and directional-bit
 * override order.  Override values are the resulting usercmd axis values.
 */
static inline float BotSFJ_EffectiveMovement(float dirX, float dirY,
	float inputSpeed, float viewYaw, int hasForwardOverride, int forwardOverride,
	int hasRightOverride, int rightOverride, float *worldX, float *worldY)
{
	const float radians = viewYaw * 0.017453292519943295f;
	const float c = cosf(radians);
	const float s = sinf(radians);
	const float scaledSpeed = inputSpeed * 127.0f / 400.0f;
	float forward = c * dirX + s * dirY;
	float right = s * dirX - c * dirY;
	float maximum = fabsf(forward) > fabsf(right) ? fabsf(forward) : fabsf(right);
	float length;

	if (maximum > 0.0f)
	{
		forward *= scaledSpeed / maximum;
		right *= scaledSpeed / maximum;
	}
	if (hasForwardOverride)
		forward = (float)forwardOverride;
	if (hasRightOverride)
		right = (float)rightOverride;
	*worldX = c * forward + s * right;
	*worldY = s * forward - c * right;
	length = sqrtf(*worldX * *worldX + *worldY * *worldY);
	if (length > 0.0f)
	{
		*worldX /= length;
		*worldY /= length;
	}
	return length;
}

static inline int BotSFJ_LandingWithinCorridor(float startX, float startY,
	float endX, float endY, float landingX, float landingY,
	float halfWidth, float endTolerance)
{
	const float routeX = endX - startX;
	const float routeY = endY - startY;
	const float routeLengthSquared = routeX * routeX + routeY * routeY;
	float progress;
	float lateralX;
	float lateralY;

	if (routeLengthSquared <= 1.0f)
		return 0;
	progress = ((landingX - startX) * routeX +
		(landingY - startY) * routeY) / routeLengthSquared;
	if (progress < 0.0f)
		return 0;
	if (progress > 1.0f + endTolerance / sqrtf(routeLengthSquared))
		return 0;
	lateralX = landingX - (startX + progress * routeX);
	lateralY = landingY - (startY + progress * routeY);
	return lateralX * lateralX + lateralY * lateralY <= halfWidth * halfWidth;
}

static inline int BotSFJ_UseIsConflict(int deliberateActionUse,
	int commandButtonUse, int previousUseWasRandom)
{
	return deliberateActionUse ||
		(commandButtonUse && !previousUseWasRandom);
}

static inline int BotSFJ_RouteSafetyAllows(int arcClear, int hazardFree,
	int staticLanding, float landingNormalZ, float routeContinuity)
{
	return arcClear && hazardFree && staticLanding &&
		landingNormalZ >= 0.7f && routeContinuity >= 0.8f;
}

/*
 * Hand-authored .botroute strafe-jump hints (start -> end).  A bot is "on" a
 * hint when it is within startRadius of the start, or alongside the route
 * (within halfWidth laterally, heightTolerance of the interpolated route
 * height) before maxProgress of its length.  *progress receives the fraction
 * travelled (0 at the start, negative just behind it).
 */
#define BOT_SFJ_ROUTE_HINT_START_RADIUS 96.0f
#define BOT_SFJ_ROUTE_HINT_HALF_WIDTH 96.0f
#define BOT_SFJ_ROUTE_HINT_HEIGHT_TOLERANCE 64.0f
#define BOT_SFJ_ROUTE_HINT_MAX_PROGRESS 0.85f
#define BOT_SFJ_ROUTE_HINT_SPEED_GATE_PROGRESS 0.5f

static inline int BotSFJ_RouteHintProgress(const float *origin, const float *start,
	const float *end, float startRadius, float halfWidth, float heightTolerance,
	float maxProgress, float *progress)
{
	const float routeX = end[0] - start[0];
	const float routeY = end[1] - start[1];
	const float routeLengthSquared = routeX * routeX + routeY * routeY;
	float routeLength;
	float t;
	float lateralX;
	float lateralY;
	float routeZ;

	if (routeLengthSquared <= 1.0f)
		return 0;
	routeLength = sqrtf(routeLengthSquared);
	t = ((origin[0] - start[0]) * routeX + (origin[1] - start[1]) * routeY) /
		routeLengthSquared;
	if (t < -startRadius / routeLength || t > maxProgress)
		return 0;
	if (t < 0.0f)
	{
		const float dx = origin[0] - start[0];
		const float dy = origin[1] - start[1];

		if (dx * dx + dy * dy > startRadius * startRadius ||
			fabsf(origin[2] - start[2]) > heightTolerance)
			return 0;
	}
	else
	{
		lateralX = origin[0] - (start[0] + t * routeX);
		lateralY = origin[1] - (start[1] + t * routeY);
		routeZ = start[2] + t * (end[2] - start[2]);
		if (lateralX * lateralX + lateralY * lateralY > halfWidth * halfWidth ||
			fabsf(origin[2] - routeZ) > heightTolerance)
			return 0;
	}
	if (progress)
		*progress = t;
	return 1;
}

/*
 * min_speed from a route hint: the first part of the route is for building
 * speed, but past the gate a grounded bot slower than min_speed must not take
 * off on the hint (it would come up short of the gap).
 */
static inline int BotSFJ_RouteHintSpeedAllows(float progress, int grounded,
	float horizontalSpeed, float minSpeed)
{
	if (minSpeed <= 0.0f || !grounded ||
		progress < BOT_SFJ_ROUTE_HINT_SPEED_GATE_PROGRESS)
		return 1;
	return horizontalSpeed >= minSpeed;
}

#endif
