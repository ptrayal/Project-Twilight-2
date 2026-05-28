# Project Twilight 2

A modern urban fantasy MUD set in a World of Darkness-inspired world of vampires, werewolves, and hidden supernatural politics. Built on a heavily modified ROM 2.4 engine, Project Twilight 2 features custom gameplay systems, an online creation toolset, and an immersive role-play environment.

---

## Setting

The game takes place in a contemporary city where the supernatural lurks beneath the surface of everyday life. Players may take on the role of a **Kindred** (vampire) from one of several clans — including Gangrel, Ventrue, Tremere, and others — or a **Garou** (werewolf), each with their own disciplines, rites, and political entanglements.

The world is one of intrigue, factional politics, and occult power. Players navigate clan hierarchies, perform rituals, conduct research, and participate in a living narrative driven by staff-run plots and events.

---

## Features

- **Vampire clans and disciplines** — each clan has unique supernatural abilities, politics, and roleplay culture
- **Werewolf rites** — Garou characters learn and perform multi-step rituals with in-world consequences
- **Ritual system** — XML-driven ritual definitions; in-game `ritedit` editor for staff; grimoire items that reveal ritual lore to players
- **Plot / Event / Script system** — a three-tier storytelling framework for staff to build and run live narrative events
- **Research system** — players gather and develop knowledge through in-game research mechanics
- **Online Creation (OLC)** — full in-game world-building tools for rooms, objects, mobiles, and game systems
- **XML help and data systems** — help files, rituals, and other data stored in human-readable XML for easy editing
- **ANSI color formatting** — rich color output throughout the game interface

---

## Building

Requirements: `gcc`, `make`, standard POSIX build tools (Linux/macOS or WSL on Windows).

```bash
cd src
make pt
```

The compiled binary is `src/project`. Build output increments the version number automatically in `src/include/version.h`.

---

## Running

### Starting the server

```bash
cd scripts
./startup [port]
```

Default port is **9080**. The startup script handles automatic restart after crashes and writes session logs to `log/`.

### Stopping the server

Create a `shutdown.txt` file in the `area/` directory. The startup loop will detect it, clean up, and exit cleanly.

---

## Connecting

Use any MUD client (Mudlet, MUSHclient, TinTin++, etc.) or a plain telnet connection:

```
telnet <server-address> 9080
```

---

## Project Structure

```
area/          Game world data — rooms, objects, mobiles, help, rituals (XML and .are files)
data/          Runtime data — notes, news, research, org data, vote tracking
doc/           Internal developer documentation and system references
docs/          Extended system documentation (ritual system, plot/event system)
scripts/       Server startup, backup, and maintenance scripts
src/           C source code
src/include/   Header files
player/        Saved player character files
```

### Key source files

| File | Purpose |
|---|---|
| `src/db.c` | Boot sequence, area and XML loading |
| `src/magic.c` | Spells, disciplines, rituals |
| `src/act_move.c` | Movement, discipline advancement |
| `src/act_info.c` | Look, examine, player information commands |
| `src/olc.c` / `src/olc_act.c` | Online creation editor |
| `src/tables.c` | Game data tables — skills, items, rituals bootstrap |
| `src/save.c` | Player character save and load |
| `src/interp.c` | Command interpreter and dispatch table |
| `src/lookup.c` | Name and data lookup utilities |

---

## Documentation

| Document | Description |
|---|---|
| `docs/ritual-system.md` | Player help content and full technical reference for the ritual system |
| `docs/plot-event-script-system.md` | Reference and audit of the plot/event/script storytelling system |
| `doc/ritual-system.md` | Developer notes and implementation details |
| `doc/help-reference.md` | Help system reference |

---

## Color Standards (in-game UI)

All in-game formatted output follows these conventions:

| Element | Color |
|---|---|
| Headers | Bright blue |
| Keywords | Bright white |
| Warnings | Bright red |
| Borders | Bright yellow |

---

## License

This codebase is a derivative work. See [LICENSE](LICENSE) for the full terms covering the Diku Mud, Merc Diku Mud, ROM 2.4, and Project Twilight 2 copyright layers.
