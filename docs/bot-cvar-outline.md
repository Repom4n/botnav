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
| `bot_learning` | `1` | Enables learned sequence weights. Compatible `LocalBotLearnedSequence` rows enter an in-memory table on map load and between duels; force reactions, combo follow-ups, throw decisions and saber-controller attack preferences receive bounded bonuses. `0` keeps built-in choices only (recording still happens while duel tracking is on). |
| `bot_learningstrength` | `1` | Scales the learned bonus (`0` disables it). After scaling by strength and skill the bonus is capped at ±30 weight, so data can tip a close choice but cannot override hard safety gates. |
| `bot_learninghumansonly` | `0` | `0` learns from every tracked participant, but bot-performed rows count a quarter of a human row so poor bot execution does not teach that an action loses. `1` loads only human-performed sequences. |
| `bot_learningminsamples` | `4` | Minimum recorded samples before a context/response entry influences choices. Below this the bot pools similar contexts (neighbouring HP+armor/force buckets, see Learned Sequences), then falls back to the coarse (force/range only) context, then to built-in weights. |
| `bot_learning_debug` | `0` | `1` prints the top learned responses per context to the console after each map load (only at map load or `botlearn reload`). Use the `botlearn` server command to print the table at any time. While set, it also prints each retreat wall-escape choice and `-4` duel stalemate break-ups. |

The human-technique controller owns saber movement, primary inputs and technique yaw while eligible. Horizontal selection holds lateral-only input through starts and transitions; forward pressure resumes during the accepted active swing. Next-swing direction follows the accepted engine move, including engine-imposed responses, rather than flipping on an attack-button request. Yaw preparation, active counter-yaw and recovery follow animation progress instead of legacy dwell timers. Legacy fan hold/dwell/yaw controls and `bot_fan_debug` still describe the legacy path; they do not schedule the new controller's attacks. The controller also uses the existing wobble amplitude, delay and speed controls for bounded active-swing yaw/pitch wiggle, separately from its counter-yaw. Random strafe overlays do not overwrite its selected footwork. Legal force actions, knockdown recovery, navigation, and saber retrieval can temporarily take priority.

Broken-parry recovery remains active through engine lockout and nearby enemy follow-up, rather than ending solely when the ordinary exit timer expires. Follow-up pressure uses current and short-horizon predicted range; distant swings no longer prolong a successful disengagement indefinitely. It suppresses offensive inputs and aim offsets, but permits a safety-checked escape jump once the engine is ready, even while waiting for nearby follow-up to clear. Safe retreat/lateral lanes and approach footing are held through meaningful phase boundaries; outside engine-sensitive swing selection, movement remains world-relative even while the view is offset. Bounded poke counter-yaw follows the accepted horizontal swing direction, rather than an arbitrary strafe direction on vertical attacks, and is composed separately from sweep smoothing and wiggle. These control curves are conservative approximations, not measured human motor templates.

Steerable throws use launch, bypass, rear placement, cut-through and recall phases. Ready saber defense selects a bypass immediately; broken defense, knockdown, an unavailable blade or a different weapon selects direct interception. Rear placement is relative to the opponent's facing. Both side lanes are tried before raised, narrower overhead alternatives. Candidate lanes must pass both the actual 4096u, level-dependent view trace and the saber-to-endpoint route check; unsafe or stale opportunities request recall. A deliberate bypass/rear detour can remain held when flight points away from the opponent, within the existing level-dependent timeout and safety gates. Cut-through redirects through the opponent, not farther behind them. Level 2 steering follows the engine's 400ms cadence and level 3 its 100ms cadence; level 1 is not steerable. Existing force, drainlock, aggression and lethal-danger restrictions still apply. Validate guarded targets near obstacles before increasing offsets or hold durations.

Historical saber tuning (dueltracks2, capture v8; newer recovery/aim/throw behavior is described above):
- Fan chain (`bot_fanbias`): the pause between L2R/R2L swings shrinks with skill and fan bias and reaches zero at skill 7+. The next swing is linked on the last frames of the current swing (strafe direction flips together with the swing direction), so the engine chains it immediately, as in the long human fan sessions. The chain runs past 3s (up to 6s) while swings keep landing within 700ms. While a horizontal swing is active the aim sweeps through the swing (up to ±80° half-arc at close range, scaled by skill/fan bias, faded out by 200u) instead of a slow fixed turn.
- Footing: swing starts use predicted 2D range (the range ~150ms ahead, at the swing's damage peak). Inside 60u (70u staff), or inside ~100u while not backing off, the bot steps in and swings together, holding forward until the peak would land at ~40–55u; above ~100u it steps in first. A 10u buffer keeps the in-reach decision from flickering. Swings are blocked while the bot is backing away from 100u or more. Chained swings keep forward pressure, and a fan chain that is still landing hits links straight into the next swing and keeps driving forward.
- Movement safety ignores the current opponent's body (walls, ledges and hazards are still checked), so the opponent standing in front no longer cancels the step or the attack.
- Decisiveness: the saber choice grade is held through the current swing (at least 500ms, at most 1s) instead of being re-rolled every 300ms. When control passes between the saber controller and the force/fan code, forward intent carries over for 200ms unless the new owner is deliberately escaping. Skill 7+ bots never start a swing while backpedalling outside a deliberate escape (humans: 0%); backpedal swings remain a low-skill mistake.
- Aim: controller swings start ~20° off the target on the swing's starting side and sweep ~50–70° per swing at 200–450°/s (scaled by skill and `bot_fanbias`). Approaching without attacking, the aim is held 10–15° off centre like humans.
- Saber throw: the throw is held at least 600ms and, while it is still heading toward the target, through its first pass (up to 1.5s). Early-release rules (enemy force lead, health, aggression) apply only after that; the drain-lock rules and lethal danger (HP+armor ≤30 and behind) can still release early. The aim trails the target's sideways motion by 5–10°, then switches ~9° to the other side once the saber has passed so the return cuts back through the target.
- Dodging: when an enemy swing starts within ~130u, skill 7+ bots strafe or jump sideways. Backpedalling is reserved for mistake rolls at lower skill. Jump attacks are discouraged unless the enemy is knocked down or getting up.
- Combos: at skill 7+ pull→kick has no gap, throw→pull ~66ms, grip→throw ~100ms, drain→pull ~66ms. Lower skills add 100–250ms plus a `bot_mistakebias` delay. Pull→swing is replaced by pull→kick or pull→throw unless the bot is already closing in on the target.
- Reactions: enemy drain → pull (or throw); enemy grip → throw, pull or jump rather than a late push; enemy throw → counter-throw; enemy pull → throw or pull back; enemy push → drain. Saber throws are favoured when the bot's own force is ~50 or less or as part of throw→pull. Mid-range throws at high force are discouraged. Pull is preferred while the enemy has force; drain is kept for when the enemy is low.
- Skill gradient: skill 7+ takes the best choice except for a small mistake roll that disappears at skill 10. Skills 1–5 draw from mistakes observed in the data: backpedal swings, long-range swings, late pushes and jump-dodging throws.

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
| `bot_strafejumps` | `1` | Enables forward-only strafe jumping (optimal-yaw air acceleration with a circle-jump start and bhop chaining) on validated, open corridors for navigation, chase and escape. Corridors look ahead up to ~1024 units along linked waypoints, or straight toward/away from the enemy when chasing/escaping directly. In CTF, carrying a flag counts as escape and chasing an enemy flag carrier counts as chase. |
| `bot_minstrafe` | `320` | Minimum opponent distance (units) before a bot starts a chase or escape strafe jump; the jump is cancelled once the gap shrinks below 75% of this. Lower it (even `0`) to let bots chase or retreat with a strafe as soon as they land after an engagement. Navigation strafes with a visible enemy are still held back inside close-combat range (~384 units) or when the enemy is closing fast. |
| `bot_strafejumps_debug` | `0` | `1` prints (rate-limited, once per second per bot) why a strafe jump was rejected, e.g. no straight corridor, unsafe arc, enemy too close. `2` also prints each strafe start and its purpose. |
| `bot_hopfrequency` | `0` | Scales how often the bot schedules its next hop while close to a saber enemy, covering both random ambient hops and non-emergency combat hops such as saber-throw counter jumps. The interval is only re-rolled once the bot lands from its previous hop, and is a random 0.5-8 second wait divided by this value as a percentage (100 = 0.5-8s; higher = longer/less frequent hops, lower = shorter/more frequent, 0 = disables discretionary hops). The wide range makes most hops occasional singles while an occasional short roll chains one hop straight into the next, keeping the bot unpredictable. |
| `bot_waypointskip` | `2` | Max number of same-direction waypoints the linear navigation helper may look ahead to avoid immediate backtrack ping-pong when wandering without an active combat/objective target. |
| `bot_redirectcooldown` | `800` | Cooldown (ms) after a wall-triggered redirect reaction. During this window the bot keeps the chosen redirect heading instead of immediately rerolling another wall reaction, reducing tight-space spin loops and repeated reaction hopping. |

`bot_strafejumps` supports only the effective JKA/co-op-JKA physics selected by `PM_GetMovePhysics`; special/ramp/super-jump styles and force-speed/rage modifiers fall back to normal AI movement. Its conservative predictor uses live speed/gravity, the command's actual pmove slicing, JKA air acceleration, and the level 0-3 levitation launch adjustment made by the second `PM_CheckJump` call in the takeoff slice. It approximates future released-command air movement; it does not run pmove, inject velocity, or change shared physics. Close combat, attacks, deliberate use, force use/jumps, saber techniques/defense, flipkicks, knockdowns, rolls, mounted/water/use states, forced movement, special jumps, steep or moving launch surfaces, and map-required interaction/jump/duck waypoint segments retain priority. Opportunistic random-use presses are suppressed only while this controller owns the command. Airborne jump is released and each landing gets a release command before a fresh press.

Arc validation performs bounded swept player-hull integration (coarse 50 ms steps after the jump command) until a real static, walkable contact; unresolved falls are rejected rather than vertically probing for a floor. Walls, ceilings, dynamic blockers, lava, slime, no-drop/void areas, and instant-kill `trigger_hurt` volumes along the full arc reject the intent. The landing must fall inside the look-ahead corridor (64 units either side, up to 96 units past its end). To keep the cost down, the full arc is simulated at most once per 100 ms per bot while deciding, and once per takeoff/rejump; while airborne a single hull trace along the predicted velocity watches for a wall, liquid, or kill volume and cleanly releases control if one comes up. Instant-kill triggers are cached per map instead of being queried with an area search for every sample. Selection and final command ownership both require the AI's own queued movement to be aligned with the corridor.

Hand-authored strafe-jump routes: put a `botroutes/<map>.botroute` file next to the map's `.wnt` waypoint file (`routes/<map>.botroute` is also read), e.g. `botroutes/mp/ffa1.botroute`. Up to 64 routes are read once per map (64 KB max):

```
// comments are allowed
strafejump
{
    start_pos   1200.5 -450.0 128.0
    end_pos     1850.0 -450.0 128.0
    min_speed   450
}
```

A bot uses a route when it is within 96 units of `start_pos`, or alongside the route (within 96 units sideways, 64 units of the route's height) before 85% of its length, and is moving toward `end_pos`. Routes take priority over the waypoint corridor and do not need straight waypoints, but every takeoff still passes the same arc/landing/hazard checks and enemy rules. `min_speed` (optional): the first half of the route is for building speed; past halfway, a grounded bot slower than `min_speed` will not take off on the route. `src_area`/`dest_area` from AAS-based route files are accepted and ignored (these bots use waypoints, not AAS). Use `/viewpos` in-game to read coordinates. With `bot_strafejumps_debug 1`, a speed-gated route prints `route hint: below min_speed past the speed gate`.

Live-map validation checklist:
- Test long, level waypoint corridors at low and high accumulated speed and with variable server frame times.
- Confirm jump is released during flight and for one command after landing; verify no force jump or flipkick occurs.
- Place doors, movers, players, low ceilings, walls, ledges, lava/slime, death triggers, and voids along or below the predicted arc and confirm immediate abort.
- Exercise navigation, retreat, and increasing-separation pursuit; confirm direct close combat and an enemy off the routed corridor never start a jump.
- Toggle `bot_strafejumps` off during each phase and confirm ordinary movement/jumps resume without yaw or input ownership.

Optional future work may use dedicated, opt-in strafe-jump demonstrations for tuning. Dueltrack CSV capture and runtime database logging are deliberately outside this feature.

## Retreat Wall Escapes

When a retreating bot (hard/soft retreat, or backing off from a fast incoming enemy) is about to back into a wall, a probe along its actual movement direction spots the wall shortly before contact. The bot then commits (~0.9 s) to one of: a vertical wallrun, flipkick then drain, drain then flipkick, a roll around the opponent, or a hop around/over the opponent. Each option is only offered when it is possible right now (flipkick enabled and in range, drain known and allowed, open space beside or above the enemy, levitation for the wallrun). The choice is uniformly random at first and then weighted by bot learning (`wallrun`, `roll`, `hop` learning tokens plus the existing kick/drain sequences); during tracked duels each choice is recorded as a decision so its outcome feeds the learned weights.

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
| `bot_perfwarn` | `0` | Performance diagnostics: when `> 0`, logs any game frame, bot AI frame, or duel-save/learning-cache step that takes at least this many milliseconds. Use it to confirm that duel-end saves no longer cause lag spikes. |

Notes:
- `-4` bots fight normally once a duel actually starts (`duelInProgress`); the force-duel-only approach only applies while finding/challenging.
- `-3` bots target the true nearest enemy (no health weighting), like `-1`, while still issuing/accepting duels.
- Bots throttle self-initiated duel requests to one every 7 seconds by default. While humans are active, bot-vs-bot offers in `-3`/`-4` are throttled to once every 2 minutes so human duel opportunities are not crowded out; with no humans around the normal 7 second cooldown applies so bot force-duel Elo keeps building.
- After offering a duel, a bot stays passive for 3 seconds: it stands still, faces the target, and does not attack, so the offer can be accepted. It may drain if it was just hurt or is low on health; the hold ends early if the duel starts or the bot takes heavy damage.
- Human duel offers are accepted immediately (ahead of the reaction delay, request throttle, FFA explore window, and the bot's own pending offer), using the engine's full 4 second acceptance window; the bot walks into the 256-unit challenge range first if needed.
- Each bot remembers the last duel type (saber or force) a human offered it and uses that type for its own offers to humans until a human offers a different one. Bot-vs-bot offers and acceptances are always force duels.
- `-4` bots roam via waypoints during the explore window instead of walking up to the nearest bot and idling. If two bots stand near each other for ~4 seconds without a duel or an offer, they blacklist each other for 20 seconds and roam apart; bots that neither side can currently challenge are skipped when picking a target, so newcomers don't join idle groups.
- After a bot-vs-bot duel ends, both bots temporarily blacklist each other so they search for a new opponent instead of immediately rematching.
- Recorded duels are saved in small steps over several frames after the duel ends (summary, events in chunks, aggregates, learning), on one persistent database connection in WAL mode, so the end of a duel no longer causes a lag spike. Pending saves are flushed on map change or shutdown. The bot-learning cache reload is also deferred to quiet frames.
- Ranked bot-vs-bot ELO progression is limited to 5 ranked duels per bot level per UTC day/session bucket; extra bot-vs-bot duels still run but are logged unranked.

## Miscellaneous

| Cvar | Default | Description |
|------|---------|-------------|
| `bot_nochat` | `0` | Disable bot chat. |
| `bot_tutorial` | `0` | Enable concise trainer tells from bots after tracked duels; at `1` tells are private, at `2+` tells may broadcast publicly. Messages are capped to 3 per burst with a 7s cooldown between sends, and unregistered reminders are sent once per session. |
| `bot_learninglog` | `0` | Opt-in recording of accepted public chat in the existing events export, independently of tutorial chat and learned weight consumption. Private tells, team, clan and admin chat are excluded. Human explanations remain untrusted annotations, not automatic behavior rules. |
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
  - `LocalBotLearnedSequence` (`learned` CSV: source kind, skill band, context key, stimulus, response, follow-ups, samples, wins, net damage, average response time)
  - `LocalBotLearnedEvidence` (`learned_evidence` CSV: individual extracted examples with source duel, participant, action index, outcome window and recording provenance)
- Every exported CSV is capped below 25MB so it can be uploaded to GitHub. A larger export is split into `<name>.csv`, `<name>_part2.csv`, `<name>_part3.csv`, ...; each part repeats the header row and whole rows are never split across files. Leftover parts from an earlier, larger export are removed.
- `capture_version` and `capture_revision` identify the recording implementation and build revision for each session, also included in event and participant exports. Imported/historical sessions without provenance remain `0`/blank; an export schema version alone does not establish recording provenance.
- `input_start` records an attack-button request, not a successful swing; historical `attack_start` records retain their original meaning. `swing_start` records an accepted saber attack animation, and `attack_chain` marks accepted continuations through legal transitions, including same-direction links, already counted by `swing_start`. `swing_end` preserves measured signed yaw sweep, and `swing_damage` records confirmed in-hand saber damage. Count distinct swings containing hits, not raw hit events. `damage_dealt`/`damage` record actual attributed resource loss; `damage_source` is the engine means-of-death value (`MOD_*`), and `damage_attacker_key` identifies the actual attacker. Environmental damage must not be credited as opponent saber damage.
- New event diagnostics distinguish controller enablement from actual ownership: `controller_enabled` records `g_newBotAI`, `controller_candidate` identifies a bot routed toward the technique controller, and `controller_owns_inputs` records actual ownership. Candidate `1` with ownership `0` identifies refused ownership. `controller_family` identifies the selected family (`0` basic, `1` horizontal, `2` diagonal/vertical, `3` counter-entry, `4` finish, `5` burst). `controller_fanbias`, `controller_mistakebias` and `controller_skill` record configured biases and raw bot skill. Humans/no bot state have family/skill `-1` and candidate/ownership `0`; migrated old diagnostic fields are unknown (`-1`).

Saber combat verification:
- Enable `g_newBotAI 1` to exercise the shared technique controller; the legacy StandardBotAI fallback is unchanged.
- Validate saber-only duels separately from full-force and arcade matches. Human techniques guide contextual attack and escape choices, not blind replay of recorded commands.
- With `bot_fanbias 0`, bots should still issue primary attacks, start real swings, and link legal transitions. Compare against `bot_fanbias 100` with the same stance and skill: horizontal L2R/R2L frequency and phase-coordinated yaw should increase, not merely the number of vertical attacks.
- Saber footwork (dueltracks3/jundon): fresh swings start when the predicted peak range is within 70u, or 70–100u while closing faster than ~150u/s; beyond ~130u bots keep stepping in. Yellow and staff alternate R2L/L2R (R2L opens ~54% of chains; an L2R→R2L link is taken about two-thirds as often as R2L→L2R unless the chain is landing hits) and use T2B only as a finisher. An unchained swing that did not land ends with a ~200ms sidestep-back. Against an enemy swing within 130u bots advance about 65% of the time and dodge back/aside otherwise, and may counter-swing 100–350ms into the enemy swing when inside 90u.
- Backward/lateral jumping exits should avoid unsafe terrain and return to engagement after landing; saber-only bots cannot heal by waiting.
- Compare primary input requests with accepted swing starts and damaging swings. Define chain frequency as accepted linked swings divided by all accepted swings (count each swing once), and compare damage per second within the same duel mode and stance.
- Capture version 8 grows event storage on demand, up to 8,192 records per participant, instead of stopping at 128. At the bound (or after a growth allocation failure), compaction prioritizes damage/knockdown events, preserves the opening record and recent tail, and samples older records. Capture continues through later exchanges; it is not unlimited or lossless.
- Participant exports expose `event_total`, `event_retained`, `event_dropped`, `event_critical_dropped`, `event_compactions` and `event_allocation_failures`. Event indices remain monotonic across compaction, so gaps identify missing records. Any capture loss disables outcome/sequence ranking for that participant: attack quality becomes `unknown` and good/bad sequence coaching is withheld. Do not compute complete-match event rates from compacted samples. Aggregate damage/force totals remain independent of retained events. Migrated old records have unknown coverage counters (`-1`), and old version-7 recordings remain truncated at their original limit.
- Confirm the deployed build with fresh `capture_revision` values before comparing results. Old rows can be re-exported by a newer build without acquiring movement measurements or proving that build was deployed.
- Compare defense resets separately from ordinary retreats: measure damage taken after a broken parry, whether the bot leaves immediate swing reach, and whether its next entry happens after recovery rather than while the opponent is still following up.
- Verify pursuit during offset aim with world-space displacement, not `forwardmove` alone. Local movement commands can change when the view turns even though the intended path stays the same. Starts and transitions must still preserve the movement inputs that select the accepted swing.
- Test poke and wiggle separately: counter-yaw should improve early contact; alternating yaw/pitch should be evaluated by repeated contact and net damage, not by visual oscillation or the largest recorded sweep.
- Test throw routing against guarded opponents near walls and under low ceilings, with both throw levels 2 and 3. Check the actual view-trace endpoint and saber route; a successful wide release alone does not prove that the saber passed behind the block.
- Use matched stance, skill, map, opponent and duel mode for baseline comparisons. Record first-hit timing, damage per accepted swing, recovery damage, safe re-entry and throw hit rate; do not combine duplicate damage summaries or treat aggregate sequence samples as independent duels.

Technique evidence and limitations:
- Sugar Kane's saber-only duel 162 includes L2R at 25.377s, R2L at 25.707s, another L2R at 26.763s, and a recorded 40-damage finishing punish at 26.994s. This supports alternating horizontal pressure and finishing continuation, not a universal instruction to hold attack forever.
- BK's full-force duel 563 includes R2L/L2R/R2L at 10.300s/10.600s/10.900s, two recorded 40-damage successes, then 40 damage taken at 10.950s. Use this as a burst-and-counter-risk example, not a saber-only training outcome. His backward/lateral rising movement in duel 565 at 8.300s supports the escape shape, not unconditional jumping or a guaranteed successful exit.
- Duel 625 (capture version 7, revision `509427374`) confirms accepted bot attacks but records 10 vertical, 8 diagonal and only 1 horizontal swing in the retained prefix. The human used staff while the bot used red stance; their attack timing is not directly interchangeable.
- Historical geometry is event-sampled, and many old rows lack measured swing sweep. Direction/sequence choices are evidence-backed; bounded phase-aware yaw is a controller approximation, not an exact replay or a proven optimal early/extra-damage curve. Validate new yaw trajectories and damage trades in fresh, stance-matched recordings before tuning them.
- The same Sugar Kane yellow-stance sequence has only two geometry samples per interval: approximately -15.8 degrees over 25.377–25.707s and +31.5 degrees over 25.707–25.938s. BK's full-force yellow sequence has seven samples per 300ms interval and approximately -190.6/+238.8 degrees of accumulated yaw. These different shapes are not interchangeable; sparse endpoints cannot establish exact active-blade timing. The initial controller used a conservative 18-degree offset envelope and a 240-degrees/second turn limit. Neither that fallback nor the newer bounded counter-yaw is a replay of BK's large spins.

## Learned Sequences

At the end of every tracked duel, each participant's events are broken into short sequences of 2–4 tokens: the opponent's move (or `idle` when the participant acted on their own), the participant's response within 700ms, then up to two follow-ups within 1s. Tokens are `push`, `pull`, `grip`, `drain`, `throw`, `swing`, `kick`, `jump`, `knockdown`. Repeated holds of the same power within 400ms are merged; swings are not merged, so fan chains count as chains.

- Context key: HP+armor and force buckets (four each) for both sides, range bucket (<128u, <384u, beyond) and stance at the start of the sequence, so similar (not identical) HP/AP/FP/range snapshots already share a key. When the exact key has fewer than `bot_learningminsamples` samples, contexts up to two bucket steps away in HP+armor/force (same range, stances and safety dimensions) are pooled with weight 0.5 per step (1 step 0.5, 2 steps 0.25). A coarse key drops health buckets but retains both force buckets, range, stances and safety dimensions.
- Outcome: net damage (dealt − taken) over the 2s after the response, plus whether the participant won the duel.
- Storage: `LocalBotLearnedSequence` in `dueltracks.db`, keyed by source kind (human/bot), skill band (human, bot 1–3, 4–5, 6–7, 8–10), context and token sequence. Rows accumulate samples, wins, net damage and response time. `resetdueltrack` does not clear these aggregates; it clears individual evidence linked to the deleted duel records.
- Map load: rows are loaded into memory, filtered by `bot_learninghumansonly` (bot rows are weighted ×0.25). Each context/response gets a confidence-weighted score based mainly on net damage; wins only count relative to the source's usual win rate (humans win most tracked duels, so a raw win rate would inflate every human row). With few samples the score is pulled towards zero, and the learned bonus only approaches its ±30 cap once a context has many samples (half strength at 20).
- Server command `botlearn` (or `botlearn print [lines]`) prints learned responses without a map restart. Finished duels mark the cache for automatic refresh between duels; `botlearn reload` also defers if a duel is active, so weights do not change halfway through an exchange.
- In game: the same lookup feeds force reactions, combo follow-ups (e.g. pull→kick), the saber-throw decision and fan entry. The technique controller consults capped initiative/counter/chain preferences at selection boundaries and records the bonus when its requested attack is accepted. Learned preferences never override legal-action, movement-safety or defense-recovery gates.
- New contexts separate saber-only from full-force duels and retain defensive, recovery, airborne and self-footing dimensions through exact, neighboring and coarse lookup. Footing distinguishes still, advance, retreat and lateral movement using the actor's own horizontal velocity toward the opponent, not relative closing speed. Legacy aggregates remain exportable but lack these dimensions and do not influence the new contexts. Fresh compatible recordings must reach the sample threshold; old CSV aggregates alone do not supply motor imitation.
- `LocalBotLearnedEvidence` preserves each extracted example's duel, participant, action index, outcome window, recording version/revision and extraction time. Use it to inspect examples behind aggregate scores rather than infer build-to-build improvement from an aggregate's latest `capture_version`.

Duel capture version 9:
- `throw_start` is logged when a saber throw is released: amount = 2D distance to the opponent at release. In version 9, `throw_yaw_offset` uses saber flight direction when available and can be reused on subsequent rows; it is not a continuous camera-steering measurement. (Older builds required a saber state the engine never set at release, so no `throw_start` rows were exported.) `throw_end` is logged when the saber is back in hand: amount = damage the throw dealt in flight, note `hit` or `miss`. The interval includes return flight and is not necessarily the duration of held alt attack.
- `dodge` is logged for the defender when an enemy swing ends within close range without damaging them (note `air`, `blocked` or `evaded`).
- Knockdown notes record `knockdown_by_opponent` or `knockdown_self`, and the damage attacker key identifies who caused it.
- Duel winners no longer get `low_force` as their primary issue for deliberately spending force.
- `force` events whose power could only be guessed from the selected power (force lost to an enemy drain, or the cost of a saber throw) carry the note `selected_fallback`. Leaving the ground with jump held and upward velocity is noted `jump` instead of `airborne`. Learning ignores both guessed force spends and non-jump launches, so victims are not credited with actions they never took.
- `LocalDuelTrackAggregate` rows carry `capture_version` and `capture_revision` of the latest update (aggregate export format 3).

Current duel capture:
- Bounded attack/throw/recovery windows add `attack_sample` events at a minimum 50ms interval (limited by server frame cadence). Samples are low-priority during compaction; damage and defensive transitions retain higher priority. This is not unlimited, continuous motion capture.
- Event telemetry adds self/opponent pitch, target-relative yaw/pitch errors, saber position when valid and the live view-trace endpoint. Historical release-offset and cumulative-sweep fields retain their meanings.
- Saber defense is classified separately from absorb/protect buffs: none, ordinary parry, attack bounce, broken parry, knockdown or lost saber. Raw `saberBlocked` is also exported, and normal return-animation recovery is separate.
- Self/opponent footing identifies actual movement independently of the legacy relative-speed label. It is an execution context, not proof of deliberate movement intent.
- `torso_anim` and `torso_timer`, alongside accepted attack elapsed time, align samples with engine animation/recovery. Historical missing animation fields are `-1`; a full normalized animation curve cannot be reconstructed from these fields alone.
- `decision` records identify the accepted controller action and its learned context, stimulus, response, follow-up and applied bonus. Missing historical decisions remain unknown rather than appearing to be zero-bonus choices.

Interpreting historical recordings:
- `yaw_delta` compares the participants' facing directions, not aim against the bearing to the opponent.
- `yaw_sweep` is accumulated signed rotation since swing start. Opposing rotations can cancel; this field alone cannot establish wiggle frequency or sustained saber contact.
- `radial_speed` is relative closing speed, including both participants' velocities. It cannot by itself establish which participant deliberately advanced.
- Event-sampled geometry cannot establish continuous pitch motion, a throw's overhead/rear path, or the steering trace endpoint when those fields were not captured. Do not treat successful damage as proof of a particular bypass technique.
- Learned rows are cumulative sequence samples. `wins` describes the containing duel's outcome, not an independent action success; `avg_response_ms = 0` with stimulus `idle` means initiative, not zero reaction latency.

Public teaching annotations:
- Enable `bot_learninglog 1` before a demonstration session. `exportDuelTrack` includes `public_chat` records in its existing events CSV; no separate chat file is needed.
- Active participants' messages link to their tracked duel when it is persisted. Messages from the same participant within 15s of completion can be marked `recent_inferred`; spectators and unmatched messages remain session annotations instead of being assigned to another participant's duel.
- Chat rows include bounded full text, speaker type, map/session timing and association metadata. Public tutorial broadcasts are bot annotations; private tutorial tells are not recorded.
- Chat is excluded from combat sequence scoring. Review a human explanation against the associated combat window before treating it as a correction; the engine does not interpret arbitrary chat as commands or trusted training labels.

Recommended data: record duels against bots at skill 6+ so the skill gradient can be checked against data; the dueltracks2 set had no bots above skill 5.

Saber-only data: 392 of the 397 duels in the dueltracks3 upload were force-enabled, so saber-only footwork (swing start window, sidestep-back, advance/dodge split, counter-swing timing) is currently tuned from force-duel saber exchanges. To tune saber-only behaviour separately, record dedicated saber-only duels (force powers disabled for the duel) against humans and bots at skill 6+ in each stance, ideally a few dozen per stance, and export them with `exportDuelTrack` so the saber-only subset can be compared with the full-force set.

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
  |     +-- bot_strafejumps
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
