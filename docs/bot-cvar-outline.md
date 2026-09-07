# Bot AI Cvar Outline

This document describes all cvars added for the NewBotAI system and how they relate to one another.

## Core Enable

| Cvar | Default | Description |
|------|---------|-------------|
| `g_newBotAI` | `0` | Master switch. 0 = legacy bot AI, 1 = NewBotAI combat logic. |
| `g_flipKick` | `0` | Engine-level flipkick enable. 1 = JA+ style, 2 = flood-protected, 3 = JK2 style. Required for flipkicks and PTK combos. |
| `bot_navigation` | `1` | Enable waypoint-based navigation fallback when no enemy is nearby. |

## Targeting

| Cvar | Default | Description |
|------|---------|-------------|
| `g_newBotAITarget` | `-1` | Target selection mode. `-1` = default (closest), `-2` = humans only, `-3` = prefer humans then bots and offer force duels to either while continuing combat, `-4` = prefer humans then bots but only offer force duels and retreat/heal instead of attacking, `>=0` = force specific client index. |
| `bot_targetdistance` | `4096` | Max distance at which bots will engage targets. |
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
- Lightning: only fires at/above `bot_lightningdistance` on defensive bias.

## Combat Behavior Biases

These are all percentage-based (0-100) chance weights that gate specific behaviors. They feed through `BotGetChanceBiasPercent()` (clamped 0-100) and then through `BotGetAggressionWeightedBonus()`.

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_saberthrowbias` | `0` | Chance weight for saber throw decisions. Higher = more throws. Feeds into `NewBotAI_GetSaberthrow()`. |
| `bot_gripkickbias` | `0` | Chance weight for grip-kick combo initiation. Feeds into `NewBotAI_GetGrip()`. |
| `bot_fanbias` | `0` | Chance weight for fan-chain attack patterns (horizontal swing chains). Used in `NewBotAI_PrepareHorizontalSwingStart()`. A committed chain holds attack for its whole duration (up to a 3s cap) and breaks only after taking more than 4 damage total. |
| `bot_drainbias` | `0` | Scales how long bots hold drain. Higher = longer drain holds. Feeds `BotGetDrainHoldBiasMs()`. |
| `bot_antidrainbias` | `0` | Weight bonus for attacking drain-users. When enemy can drain and is low HP, bots prioritize killing them. Feeds `NewBotAI_GetAntiDrainWeight()`. |
| `bot_lightningbias` | `0` | Chance weight for using lightning. Only fires on defensive aggression bias. |
| `bot_lightningdistance` | `400` | Minimum range for lightning usage. Bot must be at least this far from the enemy. |

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
| `bot_aimspeed` | `0` | 0 = use per-bot `.jkb` turnspeed_combat/reflex. 1-10 = server-selected aim quality blended with bot personality. 9-10 = near-perfect aim. |
| `bot_delayresponsetime` | `0` | Additional response delay in ms before reacting to a new enemy. 0 = use `.jkb` reflex only. |
| `bot_responseTimeDelay` | `0` | Alias for `bot_delayresponsetime`. |

## Movement & Strafe

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_strafefrequency` | `0` | Percentage chance (0-100) per think tick to enter a random strafe. 0 = disabled. |
| `bot_strafeduration` | `50` | Duration scale (0-100) for random strafes. 50 = 80-2500ms range. |
| `bot_strafeOffset` | `0` | Legacy strafe offset. |
| `bot_hopfrequency` | `0` | Scales how often the bot schedules its next random ambient hop while close to a saber enemy. The interval is only re-rolled once the bot lands from its previous hop, and is a random 0.5-8 second wait divided by this value as a percentage (100 = 0.5-8s; higher = longer/less frequent hops, lower = shorter/more frequent, 0 = disabled). The wide range makes most hops occasional singles while an occasional short roll chains one hop straight into the next, keeping the bot unpredictable. |

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

## Miscellaneous

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_nochat` | `0` | Disable bot chat. |
| `bot_yawswitch` | `10` | Waypoint yaw switch threshold. |
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
  |     +-- bot_delayresponsetime / bot_responseTimeDelay
  |
  +-- Movement
  |     +-- bot_strafefrequency / bot_strafeduration
  |     +-- bot_hopfrequency
  |     +-- bot_navigation
  |
  +-- Gripkick
  |     +-- bot_gripkickdwell
  |     +-- bot_bully
  |
  +-- Misc
        +-- bot_nochat, bot_forcepowers, bot_maxbots, etc.
```
