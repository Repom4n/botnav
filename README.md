# Botnav

An attempt at improving bot AI and navigation in Jedi Academy/OpenJK.

## Overview

This is an OpenJK-derived C-based project focused on enhancements to bot behavior, navigation systems, and related gameplay improvements for Jedi Academy multiplayer and singleplayer modes.

## What's in this repository

- **Bot AI & Navigation** (`codemp/botlib/`) – bot state management, entity tracking, pathfinding
- **Gameplay & Game Logic** (`codemp/game/`) – game rules, NPCs, force abilities, item handling
- **Server Systems** (`codemp/server/`) – server management, heartbeat, master server communication
- **Multiplayer Code** (`codemp/`) – client/server engine, rendering, mod code modules
- **Rendering** (`shared/rd-rend2/`) – graphics post-processing, tone mapping, tone mapping
- **Supporting Libraries** (`lib/`) – zlib, libjpeg, GSL-lite, and other dependencies
- **Documentation** (`docs/`) – gameplay commands, mod configuration, server options
- **Build & Deployment** – Dockerfile for containerized server runtime

## Recent Changes

Key improvements documented in `CHANGELOG.md`:

### Bot & AI
- Improved bot logging and entity state tracking
- Enhanced navigation and entity linking systems

### Server & Gameplay
- Better dedicated server console with color and tab completion
- New server commands: `kickall`, `kickbots`, `kicknum`, weapon/force toggles
- Improved `globalservers` master server support (up to 5 masters)
- Player tracking via `ja_guid` userinfo field
- Server-side demo recording from client perspective

### UI & Client
- New HUD styles and overlay options
- Speedometer with configurable jump counts
- Pitch angle helper for aiming
- Movement key display styles (updated visual feedback)
- Keyboard modifier support (Ctrl, Shift, Alt key combinations)

### Rendering & Graphics
- Improved shader file ordering and precedence
- Fixed radar and rocket locking rendering
- Support for widescreen levelshots (16:9)
- Vulkan renderer option included

### Bug Fixes
- Fixed memory leaks (NPC navigation, clipboard pasting)
- Fixed buffer overflows in filesystem code
- Fixed various out-of-bounds memory access issues
- Improved console/chat field completion

For a complete list of changes, see **[CHANGELOG.md](CHANGELOG.md)**.

## Getting Started

### Building
- [Compilation guide](https://github.com/JACoders/OpenJK/wiki/Compilation-guide)
- [Debugging guide](https://github.com/JACoders/OpenJK/wiki/Debugging)

### Running (Containerized)
The project includes a `Dockerfile` for easy deployment of a dedicated server:

```bash
docker build -t botnav-server .
docker run -it botnav-server
```

### Contributing
1. [Fork](https://github.com/Repom4n/botnav/fork) the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## License

Licensed under GPLv2 as free software. See [LICENSE.txt](LICENSE.txt) for details.

## Upstream Projects

This project is maintained against:
- [OpenJK](https://github.com/JACoders/OpenJK)
- [jaPRO](https://github.com/videoP/jaPRO)
- [EternalJK](https://github.com/eternalcodes/EternalJK)
