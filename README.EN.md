# Auto-Hunt (rAthena)

Auto-Hunt system for rAthena — automatically find monsters, walk to them, attack, loot items, and use potions.

## Features

- Find monsters within configurable range (up to 150 cells)
- Walk to and attack monsters automatically using a configured skill or normal attack
- Pick up items from the ground automatically
- Use potions when HP drops below threshold
- Pause when HP/SP is too low
- Use Fly Wing when stuck or no targets found
- Configure via NPC menu or `@autohunt` / `@ah` commands
- Works alongside AI Companion system

## Installation

### Quick Install (Copy-Paste)

1. Copy `copy-to-server/` contents to your rAthena root directory
2. Add source files to Visual Studio project (Win32 platform)
3. Add registration code to `map.cpp` and `atcommand.cpp`
4. Add `autohunt: true` to player group in `conf/groups.yml`
5. Build (Win32 Release), deploy, restart

### Patch Install

```bash
git apply patches/autohunt-all.patch
```

Then follow steps 2-5 above.

## Commands

| Command | Effect |
|---|---|
| `@autohunt` / `@ah` | Toggle auto-hunt on/off |
| `@autohunt start` | Start auto-hunting |
| `@autohunt stop` | Stop auto-hunting |
| `@autohunt status` | Show current status |
| `@autohunt config skill auto` | Auto-detect skill from hotbar |
| `@autohunt config skill 0` | Normal attack only |
| `@autohunt config skill <id> <lv>` | Set specific skill |
| `@autohunt config hp <1-100>` | HP threshold (%) |
| `@autohunt config sp <1-100>` | SP threshold (%) |
| `@autohunt config range <1-150>` | Search range (cells) |
| `@autohunt config potion <id\|0>` | Auto-potion item (0=off) |

## Default Settings

| Setting | Default | Max |
|---|---|---|
| Search Range | 30 cells | 150 |
| HP Threshold | 30% | 100% |
| SP Threshold | 10% | 100% |
| Skill | Normal attack | — |
| Auto-Potion | Disabled | — |

## Developer

- **Idea:** KBKJ
- **Code:** AI Opencode — Model Big Pickle

## License

Free for personal and commercial use under [rAthena License](https://github.com/rathena/rathena/blob/master/COPYING)
