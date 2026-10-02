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
| `g_newBotAITarget` | `-1` | Target selection mode. `-1` = default (closest), `-2` = humans only, `-3` = prefer humans then bots and issue/accept force duels while otherwise fighting normally, `-4` = prefer humans then bots but only offer force duels and retreat/heal instead of attacking, `>=0` = force specific client index. |
| `bot_targetdistance` | `4096` | Max distance at which bots will acquire, keep, or keep waypoint-pursuing a target through walls. If the target exceeds this distance, the lock is dropped unless a new in-range target is found. |
| `bot_target_timeout` | `3000` | How long (ms) a bot keeps its current lost-sight combat lock before fully falling back to normal navigation when it is not using waypoint pursuit retention. |
| `g_newBotAITargetDistance` | `4096` | Declared but currently unused (superseded by `bot_targetdistance`). |
| `bot_lowhangingfruitHP` | `40` | HP threshold below which a target is considered "low-hanging fruit" (easy kill). |
| `bot_lowhangingfruitdistance` | `1024` | Preferred cvar for the max distance at which bots will prioritize low-HP targets over a nearer normal target. |
| `bot_lowhanginfruitDistance` | `1024` | Legacy alias for `bot_lowhangingfruitdistance`; spelling intentionally omits the second `g`. |

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

Chance weights are clamped to 0-100 by `BotGetChanceBiasPercent()`. Legacy attack weights can also use `BotGetAggressionWeightedBonus()`; the new saber controller uses `bot_fanbias` directly to weight coordinated horizontal techniques without gating basic attacks.

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_saberthrowbias` | `0` | Chance weight for saber throw decisions. Higher = more throws. Feeds into `NewBotAI_GetSaberthrow()`. |
| `bot_gripkickbias` | `0` | Chance weight for grip-kick combo initiation. Feeds into `NewBotAI_GetGrip()`. |
| `bot_fanbias` | `0` | Chance weight for coordinated human-derived horizontal saber techniques in NewBotAI saber-only duels and general saber combat. Higher values favor L2R/R2L pressure, counters, finishing and burst-exit sequences with bounded swing-phase yaw. Diagonal/vertical attacks remain intentional alternatives. It weights techniques, not permission to attack: `0` still allows primary attacks and useful chains. Legal force tactics remain separate. |
| `bot_fan_debug` | `0` | Print selected fan package, direction, range, HP, and FP when a bot commits to a fan-pressure entry. |
| `bot_fanhold` | `220` | How long (ms) each fan gate holds exclusive left/right strafe plus attack to start the current horizontal swing. |
| `bot_firstfandwell` | `250` | Special dwell (ms) used only between the first and second swings of a fan chain. |
| `bot_fandwell` | `250` | Free-movement dwell (ms) between all later fan swings in the chain. During this dwell the bot applies a triangular yaw offset: it drifts away from center during the first half of the dwell and returns by the end. |
| `bot_fanyawspeed` | `90` | Degrees per second used to build that triangular fan-dwell yaw offset, clamped to `[-360, 360]`. Negative values mirror the offset to the opposite side of the current swing direction. |
| `bot_wobbledelay` | `120` | Delay in ms after fan-chain attack hold begins before aim wobble starts. |
| `bot_wobbleyaw` | `6` | Fan-chain wobble horizontal amplitude in yaw degrees. |
| `bot_wobblepitch` | `2` | Fan-chain wobble vertical amplitude in pitch degrees. |
| `bot_wobblespeed` | `2` | Fan-chain wobble speed in cycles per second. |
| `bot_drainbias` | `0` | Scales ordinary non-drainlock drain holds. Higher = longer opportunistic drain taps. Feeds `BotGetDrainHoldBiasMs()`. |
| `bot_drainlockbias` | `0` | Chance weight for committing to long deep-drain taps when the bot has a big FP lead. Separate from that cvar-driven behavior, bots that are behind on HP will keep heal-driven deep drainlocks until topped off unless aggression becomes extremely reckless. Feeds `NewBotAI_ShouldDrainlockDeep()`. |
| `bot_antidrainbias` | `0` | Weight bonus for attacking drain-users. When enemy can drain and is low HP, bots prioritize killing them. Feeds `NewBotAI_GetAntiDrainWeight()`. |
| `bot_lightningbias` | `0` | Chance weight for using lightning. Applies to bots that know lightning and pass the normal range/visibility/resource checks. |
| `bot_lightningdistance` | `400` | Minimum range for lightning usage. Bot must be at least this far from the enemy. |
| `bot_mistakebias` | `0` | Chance weight (0-100) for imperfect combat decisions. Grip escapes retain their existing mistakes. Saber choices account for spacing, recovery, recent trades, and opponent pressure: countering or continuing after a hit is useful only when the opening supports it. Lower skill and higher bias favor weaker timing, positioning, and combinations rather than persistent inactivity. Level 10 chooses the best supported legal option, even at maximum bias; this is not a guarantee of perfect play. |

The human-technique controller owns saber movement, primary inputs and technique yaw while eligible. Horizontal selection holds lateral-only input through starts and transitions; forward pressure resumes during the accepted active swing. Next-swing direction follows the accepted engine move, including engine-imposed responses, rather than flipping on an attack-button request. Yaw preparation, active sweep and recovery follow animation progress instead of legacy dwell timers. Legacy fan hold/dwell/yaw/wobble controls and `bot_fan_debug` still describe the legacy fan path; they do not schedule the new controller's attacks. Random strafe overlays do not overwrite its selected footwork. Legal force actions, knockdown recovery, navigation, and saber retrieval can temporarily take priority.

During the temporary duel no-strafe gate, free selection boundaries deliberately fall back to an ordinary vertical attack rather than withholding all attacks. A committed start/transition keeps its actual selection; unsafe or suppressed movement must not silently select a different swing. Movement safety is rechecked against the live position and final yaw before emitting technique inputs.

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
| `bot_delay` | `0` | Preferred name for extra response delay (ms). The configured delay is still scaled by each bot’s `.jkb` reflex, then additionally scaled by bot level (1 gets full add-on, 10 gets none). |
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
- After a bot-vs-bot duel ends, both bots temporarily blacklist each other so they search for a new opponent instead of immediately rematching.
- Ranked bot-vs-bot ELO progression is limited to 5 ranked duels per bot level per UTC day/session bucket; extra bot-vs-bot duels still run but are logged unranked.

## Miscellaneous

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_nochat` | `0` | Disable bot chat. |
| `bot_tutorial` | `0` | Enable concise trainer tells from bots after tracked duels; at `1` tells are private, at `2+` tells may broadcast publicly. Messages are capped to 3 per burst with a 7s cooldown between sends, and unregistered reminders are sent once per session. |
| `bot_dueltracking` | `0` | Record duel summaries, per-player force-spend stats, and sequential duel events for human-vs-human and human-vs-bot duels only (bot-vs-bot excluded). |
| `bot_dueltracking_geometry` | `0` | Optional geometry capture for tracked duel events. `1` logs only key spatial events (damage/range/air/knockdown), `2` logs all tracked events. Off by default to keep overhead low. |
| `bot_yawswitch` | `10` | Legacy/unused cvar; current wall-escape yaw behavior is hardcoded in `ai_main.c`. |
| `bot_forcepowers` | `1` | Enable bots using force powers. |
| `bot_forgimmick` | `0` | Force gimmick mode. |
| `bot_honorableduelacceptance` | `0` | Accept duel challenges honorably. |
| `bot_pvstype` | `1` | PVS check type for enemy scanning. |
| `bot_maxbots` | `0` | Max bots allowed (0 = unlimited). |
| `bot_team` | `0` | Force bot team. |
| `g_flipKickDamageScale` | `1` | Scale flipkick damage. |
| `g_movementStyle` | `1` | Movement physics style (affects bot movement code paths). |

Tracked duel data access:
- Stored in `fs_homepath/fs_game/dueltracks.db` (fallback: `fs_game/dueltracks.db`); Elo and account data remain in `data.db`.
- Database paths are printed to the server console on map load. `exportDuelTrack` reports exported CSV filenames and row counts without printing their filesystem paths. If a database cannot be opened or created, the failure is printed with the sqlite error instead of failing silently.
- Tracked rows from older builds (`dueltrack.db`, or `data.db` for the oldest builds, including the old `LocalArcadeTrack*` arcade tables) are not imported automatically. On map load the server prints how many legacy sessions it found. Run server console command `importDuelTrack` once to merge them into `dueltracks.db`: legacy files are only read, duels already present are skipped, aggregates are added only when every legacy duel was new, and each source is imported at most once (until `resetdueltrack` is run).
- Duel Elo for bots is one rating per bot level: `botlvl1` ... `botlvl10`, shared by every bot played at that level. Older per-bot keys (`<bot> [bot LN]`) in `data.db` are folded into `botlvlN` on map load.
- Use server console command `resetdueltrack` to clear duel tracking summaries, participants, events, geometry, and aggregates from `dueltracks.db`. The command reports the path and number of rows cleared, and discards any in-progress duel tracking so those duels cannot repopulate the reset. It also forgets which legacy sources were imported, so `importDuelTrack` can bring them back. It does not reset accounts, Elo duel history, or legacy databases.
- Use server console command `exportDuelTrack [prefix]` to export timestamped CSV files for:
  - `LocalDuelTrackSummary`
  - `LocalDuelTrackParticipant`
  - `LocalDuelTrackEvent` (includes sequence id/label, outcome-ranked quality, buttons, movement commands, saber stance and grounded state, saber moves, yaw delta, self/opponent HP/AP/FP snapshots, swing side, pre-swing strafe direction, radial movement intent/speed, measured yaw sweep, attack elapsed time, saber-throw yaw offset, and damage source/attacker identity)
  - `LocalDuelTrackGeometry` (when `bot_dueltracking_geometry` is enabled)
  - `LocalDuelTrackAggregate`
- Every exported CSV is capped below 25MB so it can be uploaded to GitHub. A larger export is split into `<name>.csv`, `<name>_part2.csv`, `<name>_part3.csv`, ...; each part repeats the header row and whole rows are never split across files. Leftover parts from an earlier, larger export are removed.
- `capture_version` and `capture_revision` identify the recording implementation and build revision for each session, also included in event and participant exports. Imported/historical sessions without provenance remain `0`/blank; an export schema version alone does not establish recording provenance.
- `input_start` records an attack-button request, not a successful swing; historical `attack_start` records retain their original meaning. `swing_start` records an accepted saber attack animation, and `attack_chain` marks accepted continuations through legal transitions, including same-direction links, already counted by `swing_start`. `swing_end` preserves measured signed yaw sweep, and `swing_damage` records confirmed in-hand saber damage. Count distinct swings containing hits, not raw hit events. `damage_dealt`/`damage` record actual attributed resource loss; `damage_source` is the engine means-of-death value (`MOD_*`), and `damage_attacker_key` identifies the actual attacker. Environmental damage must not be credited as opponent saber damage.
- New event diagnostics distinguish controller enablement from actual ownership: `controller_enabled` records `g_newBotAI`, `controller_candidate` identifies a bot routed toward the technique controller, and `controller_owns_inputs` records actual ownership. Candidate `1` with ownership `0` identifies refused ownership. `controller_family` identifies the selected family (`0` basic, `1` horizontal, `2` diagonal/vertical, `3` counter-entry, `4` finish, `5` burst). `controller_fanbias`, `controller_mistakebias` and `controller_skill` record configured biases and raw bot skill. Humans/no bot state have family/skill `-1` and candidate/ownership `0`; migrated old diagnostic fields are unknown (`-1`).

Saber combat verification:
- Enable `g_newBotAI 1` to exercise the shared technique controller; the legacy StandardBotAI fallback is unchanged.
- Validate saber-only duels separately from full-force and arcade matches. Human techniques guide contextual attack and escape choices, not blind replay of recorded commands.
- With `bot_fanbias 0`, bots should still issue primary attacks, start real swings, and link legal transitions. Compare against `bot_fanbias 100` with the same stance and skill: horizontal L2R/R2L frequency and phase-coordinated yaw should increase, not merely the number of vertical attacks.
- Backward/lateral jumping exits should avoid unsafe terrain and return to engagement after landing; saber-only bots cannot heal by waiting.
- Compare primary input requests with accepted swing starts and damaging swings. Define chain frequency as accepted linked swings divided by all accepted swings (count each swing once), and compare damage per second within the same duel mode and stance.
- Capture version 8 grows event storage on demand, up to 8,192 records per participant, instead of stopping at 128. At the bound (or after a growth allocation failure), compaction prioritizes damage/knockdown events, preserves the opening record and recent tail, and samples older records. Capture continues through later exchanges; it is not unlimited or lossless.
- Participant exports expose `event_total`, `event_retained`, `event_dropped`, `event_critical_dropped`, `event_compactions` and `event_allocation_failures`. Event indices remain monotonic across compaction, so gaps identify missing records. Any capture loss disables outcome/sequence ranking for that participant: attack quality becomes `unknown` and good/bad sequence coaching is withheld. Do not compute complete-match event rates from compacted samples. Aggregate damage/force totals remain independent of retained events. Migrated old records have unknown coverage counters (`-1`), and old version-7 recordings remain truncated at their original limit.
- Confirm the deployed build with fresh `capture_revision` values before comparing results. Old rows can be re-exported by a newer build without acquiring movement measurements or proving that build was deployed.

Technique evidence and limitations:
- Sugar Kane's saber-only duel 162 includes L2R at 25.377s, R2L at 25.707s, another L2R at 26.763s, and a recorded 40-damage finishing punish at 26.994s. This supports alternating horizontal pressure and finishing continuation, not a universal instruction to hold attack forever.
- BK's full-force duel 563 includes R2L/L2R/R2L at 10.300s/10.600s/10.900s, two recorded 40-damage successes, then 40 damage taken at 10.950s. Use this as a burst-and-counter-risk example, not a saber-only training outcome. His backward/lateral rising movement in duel 565 at 8.300s supports the escape shape, not unconditional jumping or a guaranteed successful exit.
- Duel 625 (capture version 7, revision `509427374`) confirms accepted bot attacks but records 10 vertical, 8 diagonal and only 1 horizontal swing in the retained prefix. The human used staff while the bot used red stance; their attack timing is not directly interchangeable.
- Historical geometry is event-sampled, and many old rows lack measured swing sweep. Direction/sequence choices are evidence-backed; bounded phase-aware yaw is a controller approximation, not an exact replay or a proven optimal early/extra-damage curve. Validate new yaw trajectories and damage trades in fresh, stance-matched recordings before tuning them.
- The same Sugar Kane yellow-stance sequence has only two geometry samples per interval: approximately -15.8 degrees over 25.377–25.707s and +31.5 degrees over 25.707–25.938s. BK's full-force yellow sequence has seven samples per 300ms interval and approximately -190.6/+238.8 degrees of accumulated yaw. These different shapes are not interchangeable; sparse endpoints cannot establish exact active-blade timing. The initial controller uses a conservative 18-degree offset envelope and a 240-degrees/second turn limit, not BK's large spins.

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
  |           +-- Legacy fan hold/dwell/yaw controls --> legacy fan-chain patterns only
  |           +-- bot_drainbias --> drain hold duration
  |           +-- bot_antidrainbias --> anti-drain priority
  |           +-- bot_lightningbias + bot_lightningdistance --> lightning
  |           +-- bot_ptk_aggressionbias + bot_ptk_fpdifference + bot_ptk_hpdifference --> PTK
  |           +-- Retreat thresholds (health/distance)
  |           +-- Saber throw defense break (pull vs push)
  |
  +-- Shared Saber Techniques
  |     +-- bot_fanbias --> coordinated horizontal family weight (not attack permission)
  |     +-- bot_mistakebias + skill --> contextual timing/positioning choices
  |     +-- Accepted animation + final input ownership --> selection, pressure and bounded yaw
  |     +-- Live movement safety + engine/force/navigation priority --> legal inputs
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
