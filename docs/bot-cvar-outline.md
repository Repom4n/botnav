# Bot AI Cvar Outline

This document describes all cvars added for the NewBotAI system and how they relate to one another.

## Core Enable

| Cvar | Default | Description |
|------|---------|-------------|
| `g_newBotAI` | `0` | Master switch. 0 = legacy bot AI, 1 = NewBotAI combat logic. |
| `g_flipKick` | `0` | Engine-level flipkick enable. 1 = JA+ style, 2 = flood-protected, 3 = JK2 style. Required for flipkicks and PTK combos. |
| `bot_navigation` | `1` | Enable waypoint-based navigation pathing while NewBotAI is active. |

## Targeting

| Cvar | Default | Description |
|------|---------|-------------|
| `g_newBotAITarget` | `-1` | Target selection mode. `-1` = default (closest), `-2` = humans only, `-3` = prefer humans then bots and offer force duels to either while continuing combat, `-4` = prefer humans then bots but only offer force duels and retreat/heal instead of attacking, `>=0` = force specific client index. |
| `bot_targetdistance` | `4096` | Max distance at which bots will engage targets. |
| `bot_target_timeout` | `3000` | How long (ms) a bot keeps its current target lock after losing line of sight through walls/floors before dropping back to normal navigation. |
| `g_newBotAITargetDistance` | `4096` | Declared but currently unused (superseded by `bot_targetdistance`). |
| `bot_lowhangingfruitHP` | `40` | HP threshold below which a target is considered "low-hanging fruit" (easy kill). |
| `bot_lowhanginfruitDistance` | `1024` | Max distance to prioritize low-HP targets. |

## Aggression System

The aggression bias is the central personality axis. It flows through `BotGetAggressionBias()` which combines:

```
BotGetAggressionBias = clamp(bot_aggressionbias + healthComponent*bot_healthbias + forceComponent*bot_forcebias + hateLevelAggressionBias, -1, 1)
```

| Cvar | Default | Range | Description |
|------|---------|-------|-------------|
| `bot_aggressionbias` | `0` | `[-1, 1]` | Base aggression. Positive = aggressive, negative = defensive. Feeds into nearly every combat decision. |
| `bot_healthbias` | `0` | `[-1, 1]` | How much current health shifts aggression. At 1: full HP = +1 aggression, 0 HP = -1. |
| `bot_forcebias` | `0` | `[-1, 1]` | How much force-point advantage shifts aggression. At 1: full FP advantage = +1 aggression. |
| **Per-bot `.jkb` `hatelevel`** | `3` | `1-5` | Not a cvar, but feeds into aggression as `hateLevelAggressionBias`. Higher hatelevel = more aggressive personality. |

**How aggression is consumed:**
- `BotGetAggressionWeightedBonus(bs, biasPercent, maxBonus, aggressiveOnly)` scales a bonus by `aggressionBias * biasPercent/100 * maxBonus`. Only applies when aggression is positive (for `aggressiveOnly=true`).
- Retreat thresholds: `hardRetreatHealth = 30 - aggression*25`, `softRetreatHealth = 60 - aggression*35`.
- Saber throw defense break: `preferPull = (aggression > 0)` — pull when aggressive, push when defensive.
- Lightning: fires at/above `bot_lightningdistance` for bots that know lightning and have a usable lightning level.

## Combat Behavior Biases

These are all percentage-based (0-100) chance weights that gate specific behaviors. They feed through `BotGetChanceBiasPercent()` (clamped 0-100) and then through `BotGetAggressionWeightedBonus()`.

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_saberthrowbias` | `0` | Chance weight for saber throw decisions. Higher = more throws. Feeds into `NewBotAI_GetSaberthrow()`. |
| `bot_gripkickbias` | `0` | Chance weight for grip-kick combo initiation. Feeds into `NewBotAI_GetGrip()`. |
| `bot_fanbias` | `0` | Chance weight for fan-chain attack patterns (horizontal swing chains). Used in `NewBotAI_PrepareHorizontalSwingStart()`. A committed chain holds attack for its whole duration (up to a 3s cap) and breaks only after taking more than 4 damage total. |
| `bot_wobbledelay` | `120` | Delay in ms after fan-chain attack hold begins before aim wobble starts. |
| `bot_wobbleyaw` | `6` | Fan-chain wobble horizontal amplitude in yaw degrees. |
| `bot_wobblepitch` | `2` | Fan-chain wobble vertical amplitude in pitch degrees. |
| `bot_wobblespeed` | `2` | Fan-chain wobble speed in cycles per second. |
| `bot_drainbias` | `0` | Scales ordinary non-drainlock drain holds. Higher = longer opportunistic drain taps. Feeds `BotGetDrainHoldBiasMs()`. |
| `bot_drainlockbias` | `0` | Chance weight for committing to long deep-drain taps when the bot has a big FP lead. Separate from that cvar-driven behavior, bots that are behind on HP will keep heal-driven deep drainlocks until topped off unless aggression becomes extremely reckless. Feeds `NewBotAI_ShouldDrainlockDeep()`. |
| `bot_antidrainbias` | `0` | Weight bonus for attacking drain-users. When enemy can drain and is low HP, bots prioritize killing them. Feeds `NewBotAI_GetAntiDrainWeight()`. |
| `bot_lightningbias` | `0` | Chance weight for using lightning. Applies to bots that know lightning and pass the normal range/visibility/resource checks. |
| `bot_lightningdistance` | `400` | Minimum range for lightning usage. Bot must be at least this far from the enemy. |
| `bot_mistakebias` | `0` | Chance weight (0-100) for grip-escape mistakes when the *bot* is being gripped (never limits a player's own push/pull out of a grip). Lower-skill bots miss more: level 10 is unaffected, levels below it scale up to ~1.4x/ down to ~0.6x of the bias. Per grip session the bot rolls a wide range of failures: a random escape delay (0 up to ~3.6s) before it may pull free, missed pulls (aim offset), a fumbled push-instead-of-pull that shoves the gripper away, and occasionally never escaping the grip at all (kick-struggles until the grip ends). |

## PTK (Pull-Throw-Kick) System

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_ptk_fpdifference` | `20` | Force-point advantage required before PTK weight is boosted. |
| `bot_ptk_hpdifference` | `15` | Health advantage required before PTK weight is boosted. |
| `bot_ptk_aggressionbias` | `0` | Aggression bias scaling for PTK weight. Higher = PTK only when very aggressive. |

**PTK chain flow:**
1. `NewBotAI_GetPTKWeight()` computes a weight based on FP/HP advantages + aggression.
2. This weight feeds into `NewBotAI_GetPull()` — if high enough, the bot pulls.
3. After a successful pull, `NewBotAI_Flipkick()` is called (the "kick" in PTK).
4. When saber is thrown, `NewBotAI_TrySaberThrowDefenseBreak()` selects pull (aggressive) or push (defensive).

## Aim & Response

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_aimspeed` | `0` | 0 = legacy aim speed from per-bot `.jkb` turnspeed_combat. 1-10 = multiplier on legacy combat turning with strict monotonic progression (higher is always faster, 10 fastest). Affects aim turn speed only. |
| `bot_delay` | `0` | Preferred name for extra response delay (ms) added on top of legacy `.jkb` reflex/skill reaction time. Added delay is scaled by bot level (1 gets full add-on, 10 gets none). |
| `bot_delayresponsetime` | `0` | Legacy alias for `bot_delay`. |
| `bot_responseTimeDelay` | `0` | Legacy alias for `bot_delay`. |

## Movement & Strafe

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_strafefrequency` | `0` | Percentage chance (0-100) per think tick to enter a random strafe. 0 = disabled. |
| `bot_strafeduration` | `50` | Duration scale (0-100) for random strafes. 50 = 80-2500ms range. |
| `bot_strafeOffset` | `0` | Legacy strafe offset. |
| `bot_hopfrequency` | `0` | Scales how often the bot schedules its next hop while close to a saber enemy, covering both random ambient hops and non-emergency combat hops such as saber-throw counter jumps. The interval is only re-rolled once the bot lands from its previous hop, and is a random 0.5-8 second wait divided by this value as a percentage (100 = 0.5-8s; higher = longer/less frequent hops, lower = shorter/more frequent, 0 = disables discretionary hops). The wide range makes most hops occasional singles while an occasional short roll chains one hop straight into the next, keeping the bot unpredictable. |
| `bot_waypointskip` | `2` | Max number of same-direction waypoints the linear navigation helper may look ahead to avoid immediate backtrack ping-pong when wandering without an active combat/objective target. |
| `bot_redirectcooldown` | `800` | Cooldown (ms) after a wall-triggered redirect reaction. During this window the bot keeps the chosen redirect heading instead of immediately rerolling another wall reaction, reducing tight-space spin loops and repeated reaction hopping. |

## Gripkick Tuning

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_gripkickdwell` | `100` | Percent scaling of grip phase durations: the aim-down/hold phases (holding the target gripped with forward-only movement before the flipkick approach, and the dwell after a failed kick attempt) run at 1.5x this scaling, while the upward jerk phases run at 1x. 100 = default timings. Higher = longer dwell per phase, lower = faster cycling. Each upward jerk also rolls its own random pitch between 45 and 80 degrees so the swing height of the gripped target varies jerk to jerk. |
| `bot_bully` | `0` | 1 = bully mode: while gripping a knocked-down target that is falling fast enough to splat, release grip early to let them splat; and when the bot's back is to lava or a lethal fall, release grip after the first jerk phase begins so the jerk hurls the target off the edge. 0 (default) = keep holding the grip instead of releasing for the splat/hazard. |

## Duel & FFA Exploration (`g_newBotAITarget` -3 / -4)

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_duelcountmax` | `3` | Completed duels before a -3/-4 bot returns to FFA to find a new opponent: duel issuing/acceptance is suppressed while the bot explores. |
| `bot_ffaexploretime` | `180000` | How long (ms) a -4 bot keeps exploring for a new non-dueling opponent once the post-duel FFA window begins (default 3 minutes), before it may duel again. |

Notes:
- `-4` bots fight normally once a duel actually starts (`duelInProgress`); the force-duel-only approach only applies while finding/challenging.
- `-3` bots target the true nearest enemy (no health weighting), like `-1`, while still issuing/accepting duels.
- Bots throttle self-initiated duel requests to one every 7 seconds by default, but bot-vs-bot offers in `-3`/`-4` are throttled to once every 2 minutes so human duel opportunities are not crowded out.
- Ranked bot-vs-bot ELO progression is limited to 5 ranked duels per bot level per UTC day/session bucket; extra bot-vs-bot duels still run but are logged unranked.

## Miscellaneous

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_nochat` | `0` | Disable bot chat. |
| `bot_yawswitch` | `10` | Legacy/unused cvar; current wall-escape yaw behavior is hardcoded in `ai_main.c`. |
| `bot_forcepowers` | `1` | Enable bots using force powers. |
| `bot_forgimmick` | `0` | Force gimmick mode. |
| `bot_honorableduelacceptance` | `0` | Accept duel challenges honorably. |
| `bot_pvstype` | `1` | PVS check type for enemy scanning. |
| `bot_maxbots` | `0` | Max bots allowed (0 = unlimited). |
| `bot_team` | `0` | Force bot team. |
| `g_flipKickDamageScale` | `1` | Scale flipkick damage. |
| `g_movementStyle` | `1` | Movement physics style (affects bot movement code paths). |

## Skill Tuning (Debug)

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_s1` | `16` | Skill parameter 1. |
| `bot_s2` | `24` | Skill parameter 2. |
| `bot_s3` | `48` | Skill parameter 3. |
| `bot_s4` | `0.5` | Skill parameter 4. |
| `bot_s5` | `0` | Skill parameter 5. |
| `bot_s6` | `64` | Skill parameter 6. |

## Dependency Graph

```
g_newBotAI (master switch)
  |
  +-- g_flipKick (required for flipkick/PTK)
  |
  +-- Targeting
  |     +-- g_newBotAITarget
  |     +-- bot_targetdistance
  |     +-- bot_target_timeout
  |     +-- bot_lowhangingfruitHP / bot_lowhanginfruitDistance
  |
  +-- Aggression Core
  |     +-- bot_aggressionbias (base)
  |     +-- bot_healthbias (health modifier)
  |     +-- bot_forcebias (force modifier)
  |     +-- .jkb hatelevel (personality modifier)
  |     |
  |     +-- Consumed by:
  |           +-- bot_saberthrowbias --> saber throw weight
  |           +-- bot_gripkickbias --> grip initiation weight
  |           +-- bot_fanbias --> fan-chain patterns
  |           +-- bot_drainbias --> drain hold duration
  |           +-- bot_antidrainbias --> anti-drain priority
  |           +-- bot_lightningbias + bot_lightningdistance --> lightning
  |           +-- bot_ptk_aggressionbias + bot_ptk_fpdifference + bot_ptk_hpdifference --> PTK
  |           +-- Retreat thresholds (health/distance)
  |           +-- Saber throw defense break (pull vs push)
  |
  +-- Aim & Response
  |     +-- bot_aimspeed
  |     +-- bot_delay / bot_delayresponsetime / bot_responseTimeDelay
  |
  +-- Movement
  |     +-- bot_strafefrequency / bot_strafeduration
  |     +-- bot_hopfrequency
  |     +-- bot_navigation
  |     +-- bot_waypointskip
  |     +-- bot_redirectcooldown
  |
  +-- Gripkick
  |     +-- bot_gripkickdwell
  |     +-- bot_bully
  |
  +-- Misc
        +-- bot_nochat, bot_forcepowers, bot_maxbots, etc.
```
