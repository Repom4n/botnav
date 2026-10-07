# Copilot instructions for botnav (TaystJK / OpenJK bot mod)

Jedi Academy multiplayer mod based on OpenJK/JAPro. Almost all work is in the server game module
(`codemp/game`) on the bot AI, duel tracking and bot learning code.

## Build and test

The sandbox has no system OpenGL/JPEG/PNG development libraries. Build only the game module and tests:

```sh
sudo apt-get install -y libboost-test-dev   # only needed for the unit tests
mkdir -p /tmp/build && cd /tmp/build
cmake <repo> -DUseInternalJPEG=ON -DUseInternalPNG=ON -DUseInternalZlib=ON \
  -DBuildMPEngine=OFF -DBuildMPRdVanilla=OFF -DBuildMPRdVulkan=OFF -DBuildMPRend2=OFF \
  -DBuildMPCGame=OFF -DBuildMPUI=OFF -DBuildMPDed=OFF -DBuildDiscordRichPresence=OFF \
  -DBuildTests=ON
make -j8 jampgamex86_64      # game module (codemp/game)
make -j8 UnitTests && ./UnitTests
```

Unit tests (`tests/*.cpp`, Boost.Test) can only reach engine-free code, so put testable
logic in `static inline` helpers in headers (`ai_combat_tuning.h`, `ai_strafejump.h`,
`g_bot_learning.h`, `g_duel_*.h`) and call them from the `.c` files.

## Where things live

| Area | Files |
|------|-------|
| Bot think loop, combat, force, gripkick, saber controller, strafe jumps | `codemp/game/ai_main.c` (very large; search by function name, never by line number) |
| Bot state fields | `codemp/game/ai_main.h` (`bot_state_t`) |
| Pure combat/saber tuning helpers | `codemp/game/ai_combat_tuning.h` (tests: `tests/bot_ai_tuning.cpp`) |
| Strafe-jump (SFJ) helpers | `codemp/game/ai_strafejump.h`; controller is `BotSFJ_*` in `ai_main.c` |
| Learned sequences: tokens, context keys, scoring, baked baseline | `codemp/game/g_bot_learning.h`; cache and `G_BotLearnBonus` in `g_bot_learning.c` (tests: `tests/bot_learning.cpp`) |
| Duel tracking, SQLite persistence, CSV export | `codemp/game/g_account.c` |
| Cvars | `codemp/game/g_xcvar.h` |
| Bot cvar documentation | `docs/bot-cvar-outline.md` |

Key entry points: `StandardBotAI` (per-frame), `CombatBotAI`, `NewBotAI_ReactToBeingGripped`,
`NewBotAI_PrepareHorizontalSwingStart` (fan chains), `BotSFJ_SelectIntent` / `BotSFJ_ApplyInput`.

## Conventions

- Cvars: add `XCVAR_DEF(name, "default", NULL, CVAR_ARCHIVE, qtrue)` to `g_xcvar.h` (it
  declares and registers them). Do not use `trap_Cvar_Register`. Document new cvars in
  `docs/bot-cvar-outline.md`.
- Engine calls go through `trap->...` (for example `trap->EA_Jump`). Bot input is built in
  `bot_input_t` actionflags or `bs->doAttack` / `bs->doAltAttack` / `useTheForce`, which
  `StandardBotAI` dispatches at the end of the frame.
- Force use is gated by `bot_forcepowers` and `g_forcePowerDisable`. Saber-only duel checks
  use `NewBotAI_IsSaberOnlyDuel`.
- Mistake cvars: `bot_mistakebias` covers general combat mistakes, `bot_gkmistakebias`
  covers only gripkick escape mistakes. Level 10 bots make neither.
- Learned preferences are additive, capped (±`BOTLEARN_BONUS_CAP`) bonuses and must never
  override legal-action, movement-safety or defense-recovery gates.
- Strafe-jump safety: never drop hazard, trigger_hurt, lava, slime or nodrop checks when relaxing
  wall or conflict rules.
- C89-style C in `codemp/game` (declarations at the top of blocks, tabs for indentation).
  Keep the code style that is already in the file.

## Duel-track data

- `exportDuelTrack` writes `dueltrack_*.csv` files, split into `_partN` files at 24 MB.
  `dueltrack_learned.csv` / `dueltrack_learned_evidence.csv` hold the aggregated learning.
  Only rows with `(ctx_key & 0x60000000) == 0x60000000` are compatible with the current context
  format. Mode is `(ctx_key >> 16) & 3` (0 saber-only, 1 full force).
- Learned tokens are `none, idle, push, pull, grip, drain, throw, swing, kick, jump, knockdown,
  wallrun, roll, hop` (ids 0-13). They are action tokens, not saber moves or stances.
- Analyse the large CSVs with a script (Python's `csv` module or DuckDB), not by reading them
  directly. Report figures together with the query that produced them.

## Working with handoffs from other assistants

Prefer specs over pasted code. A good handoff states the goal, the evidence (numbers and the
query that produced them), the desired behaviour as when/then rules, what must not break, and
how to check acceptance. Check every function, field and cvar a handoff names against this repo
before using it. Earlier handoffs referenced APIs that do not exist here (`trap_Cvar_Register`,
`bs->enemy`, `NewBotAI_ActivateForcePower`, `code/game/...` paths).
