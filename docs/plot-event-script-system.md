# Plot / Event / Script System — Reference and Audit

## Overview

This document describes the three-tier storytelling system built into ProjectTwilight2. It covers what each command is supposed to do, how the runtime works, and where the code is broken or incomplete.

The hierarchy is:

```
PLOT  (narrative container)
  └── EVENT  (a scene with a location and actors)
        └── SCRIPT  (a trigger/reaction pair for one actor)
```

All three levels are edited with online editors similar to the area editor. Data is persisted in `area/plots.are` and loaded at startup.

---

## Data Structures

Defined in `src/include/twilight.h`.

### PLOT_DATA
| Field        | Type         | Purpose                                 |
|--------------|--------------|-----------------------------------------|
| `vnum`       | int          | Unique identifier (1–399)               |
| `author`     | char *       | Who created it (space-separated list)   |
| `title`      | char *       | Display name                            |
| `races`      | long         | Bitmask of allowed player races         |
| `event_list` | EVENT_DATA * | Linked list of events in this plot      |
| `next`       | PLOT_DATA *  | Next plot in global list                |

### EVENT_DATA
| Field          | Type         | Purpose                                            |
|----------------|--------------|----------------------------------------------------|
| `vnum`         | int          | Unique identifier (1–399)                          |
| `plot`         | PLOT_DATA *  | Parent plot                                        |
| `author`       | char *       | Who can edit this event                            |
| `title`        | char *       | Display name                                       |
| `races`        | long         | Bitmask of allowed player races                    |
| `loc`          | int          | Room vnum where the event takes place              |
| `stop`         | int          | When TRUE, halts execution of the event            |
| `actors`       | ACTOR_DATA * | List of mob vnums to teleport to the room          |
| `script_list`  | SCRIPT_DATA* | Linked list of scripts belonging to this event     |
| `next`         | EVENT_DATA * | Next event in global list                          |
| `next_in_plot` | EVENT_DATA * | Next event within the parent plot                  |

### SCRIPT_DATA
| Field          | Type         | Purpose                                                            |
|----------------|--------------|--------------------------------------------------------------------|
| `vnum`         | int          | Unique identifier (1–399)                                          |
| `event`        | EVENT_DATA * | Parent event                                                       |
| `author`       | char *       | Who created it                                                     |
| `actor`        | int          | Mob vnum that executes the reaction                                |
| `trigger`      | char *       | Substring the mob "hears" — matched by `trigger_test()` at runtime |
| `reaction`     | char *       | Exact command the actor mob executes                               |
| `delay`        | int          | PULSE_VIOLENCE cycles before reacting (~1.5 sec each)              |
| `active`       | int          | Set TRUE on the **per-mob copy** (`ch->script`) when pending       |
| `first_script` | int          | If TRUE, this is the entry-point script for the event              |
| `next`         | SCRIPT_DATA* | Next script in global list                                         |
| `next_in_event`| SCRIPT_DATA* | Next script within the parent event                                |

**Note on `active`:** Original scripts in `event->script_list` always have `active=0`. When `trigger_test()` creates a per-mob copy in `ch->script`, it sets `active=TRUE` to mark the copy as pending execution. The two uses are distinct.

### ACTOR_DATA
| Field | Type        | Purpose                        |
|-------|-------------|--------------------------------|
| `mob` | int         | Mob vnum for this actor        |
| `next`| ACTOR_DATA* | Next actor in the list         |

---

## Access Control

The `IS_STORYTELLER(ch, bog)` macro (defined in `src/include/olc.h`) gates all editor commands:

```c
#define IS_STORYTELLER(ch, bog) ( !IS_SWITCHED(ch) &&
    ( ch->trust >= 3
    || strstr(bog->author, ch->name)
    || strstr(bog->author, "All") ) )
```

Access is granted if the character:
- Has trust level 3 or higher, **or**
- Is named in the plot/event/script's `author` field, **or**
- The `author` field contains the string "All"

---

## Editor Commands

### PEDIT — Plot Editor

**Entry:** `src/olc.c`, `do_pedit()` (~line 1854)  
**Subcommands:** `src/olc_act.c` (~line 5446)  
**Registered as:** `pedit` in `interp.c`

#### `pedit create`
Creates a new plot with an auto-assigned vnum (1–399). Sets the author to the current character's name and enters the plot editor.

**Status: Works**, but has a dead-code `IS_NULLSTR` check and calls `IS_STORYTELLER` with a NULL plot pointer for the security check. For characters with trust >= 3 this is harmless (short-circuits on the first `||` branch). Characters with trust < 3 who somehow reach this code would crash the server.

#### `pedit <vnum>`
Opens an existing plot by vnum for editing.

**Status: Works.**

#### `title <string>`
Sets the plot's title. Capitalises the first letter automatically.

**Status: Works.**

#### `races <flag>`
Toggles a race flag on the plot. Type `? races` in-game for the flag list.

**Status: Works.**

#### `addevent`
Creates a new event, links it to the current plot, then automatically switches the editor into `eedit` mode for that event.

**Status: Works.** This is the only correct way to create an event — `eedit create` is commented out.

#### `delevent <event-vnum>`
Removes an event (by vnum) from the current plot. Also removes that event's scripts. The event must belong to this plot.

**Status: Works** (fixed — previously used the wrong EDIT macro and had no vnum argument).

#### `assignevent <name | All>`
Toggles an author name on the current plot's author list. Use `All` to let any storyteller edit the plot.

**Status: Works** (fixed — previously misread the plot pointer as an event pointer). Note: this now manages the *plot's* author field, which is the logical behaviour in PEDIT context.

#### `remove`
Removes the current plot from the global list and frees it, including all child events and their scripts.

**Status: Works** (fixed — previously had a head-of-list bug and did not call `edit_done` before freeing. `free_plot` already cascaded into children correctly).

#### `show` (or just press Enter)
Displays the plot's vnum, title, author, race flags, and a list of its events.

**Status: Works.**

---

### EEDIT — Event Editor

**Entry:** `src/olc.c`, `do_eedit()` (~line 1913)  
**Subcommands:** `src/olc_act.c` (~line 5677)  
**Registered as:** `event_edit` and `eedit` in `interp.c`

Events **cannot be created directly** via `eedit`. The `eedit create` path was commented out. Events must be created from within the plot editor using `pedit addevent`.

#### `eedit <vnum>`
Opens an existing event by vnum.

**Status: Works.**

#### `title <string>`
Sets the event's title.

**Status: Works.**

#### `location <vnum>`
Sets the room vnum where the event takes place. Validates that the room exists.

**Status: Works.**

#### `races <flag>`
Toggles a race flag on the event.

**Status: Works.**

#### `actors <mob-vnum> [mob-vnum ...]`
Adds one or more mob vnums to the event's actor list. Accepts a space-separated list via recursion. Use `actors clear` to remove all actors.

**Status: Works.** Note that adding an actor only records the mob vnum — it does not spawn the mob. The mob must already exist in the game world before `runevent` is called (see runtime section).

#### `scripts`
Creates a new script, links it to the current event, then switches the editor into `sedit` mode for that new script.

**Status: Works.** This is the only correct way to create a script.

#### `delscript <script-vnum>`
Removes a script (by vnum) from the current event. The script must belong to this event.

**Status: Works** (fixed — previously used the wrong EDIT macro and had no vnum argument).

#### `remove`
Removes the current event from the global list and frees it, including all child scripts, and unlinks it from its parent plot's event list.

**Status: Works** (fixed — previously had a head-of-list bug, did not call `edit_done`, and did not unlink from the parent plot. `free_event` already cascaded into scripts correctly).

#### `show` (or just press Enter)
Displays all event fields, including actors and scripts.

**Status: Works** (fixed — added null check on `get_room_index()`; displays "INVALID ROOM" instead of crashing when the location vnum doesn't exist).

---

### SEDIT — Script Editor

**Entry:** `src/olc.c`, `do_sedit()` (~line 1978)  
**Subcommands:** `src/olc_act.c` (~line 5890)  
**Registered as:** `sedit` in `interp.c`

#### `sedit <vnum>`
Opens an existing script by vnum.

**Status: Works.**

#### `sedit create`
Creates a new orphaned script (no event link). The intended workflow is to use `eedit scripts` instead, which creates the script and links it in one step.

**Status: Partially works** — creates the script but with no event link. The command now warns the user to assign it to an event with `event <vnum>`. The security-check dead-code has been removed. `run_script` now guards against NULL event pointers safely.

#### `trigger <string>`
Sets the trigger string — the substring that must appear in a message for the actor mob to respond. Matched by `trigger_test()` against every message an NPC receives (via `act()` in `comm.c`).

**Status: Works** — stored and matched correctly at runtime.

#### `reaction <string>`
Sets the exact command the actor mob will execute when the trigger fires.

**Status: Works** — fired via `mob_interpret()` after the delay expires.

#### `delay <number>`
Sets how many PULSE_VIOLENCE cycles to wait before the reaction fires. One PULSE_VIOLENCE is approximately 1.5 seconds (6 × PULSE_PER_SECOND at 4 pulses/sec). A value of 0 or less is automatically raised to 1 (one cycle minimum).

**Status: Works** — stored and honoured by `script_update()` at runtime.

#### `actor <mob-vnum>`
Sets which mob vnum is the actor for this script. The mob must exist in the prototype table.

**Status: Works.**

#### `event <event-vnum>`
Links an orphaned script to an event and prepends it to the event's script list.

**Status: Works** (fixed — was incorrectly setting `ps->next` instead of `ps->next_in_event`, making scripts unreachable via the event chain).

#### `eventfirst`
Marks this script as the entry point for its event. Clears the `first_script` flag on all other scripts in the same event.

**Status: Works**, provided the script is already linked to an event.

#### `remove`
Removes the script from the global list, unlinks it from its parent event's script chain, and frees it.

**Status: Works** (fixed — previously had a head-of-list bug, did not call `edit_done`, and did not unlink from the parent event's `next_in_event` chain).

#### `show` (or just press Enter)
Displays all script fields.

**Status: Works.**

---

## List Commands

All require storyteller trust (ST in interp.c).

| Command        | Registered As   | Source               | Does it Work? |
|----------------|-----------------|----------------------|---------------|
| `plist`        | `plist`         | `do_plist`, olc.c    | Yes           |
| `event_list`   | `event_list`    | `do_elist`, olc.c    | Yes           |
| `slist`        | `slist`         | `do_slist`, olc.c    | Yes (fixed)   |

### `slist`

**Status: Works** (fixed — the dead `if(pScript->active) continue` filter was removed. The `active` field on original scripts is always 0; the filter was pure noise. All scripts now appear in the list).

---

## Runtime Commands

### `runevent <vnum>`

**Source:** `do_run_event()`, `src/act_wiz.c` ~line 5260  
**Registered as:** `runevent`

What it does:
1. Teleports the storyteller to the event's room (`do_goto`).
2. Calls `do_timestop` (presumably freezes the game clock).
3. Calls `get_actors(event)` — scans the global character list for mobs matching each actor vnum and moves them to the event room. **Only moves existing mobs; does not spawn new ones.**
4. Finds the script with `first_script == TRUE`.
5. Sets `room->event = event` and `event->stop = FALSE`.
6. Calls `run_script(script)` on the first script.

**`run_script` behaviour:**
```c
void run_script(SCRIPT_DATA *s)
{
    for(actor = char_list; ...)
    {
        if(IS_NPC(actor) && actor->in_room->vnum == s->event->loc)
        {
            if(actor->pIndexData->vnum == s->actor)
                mob_interpret(actor, s->reaction);
        }
    }
}
```

`run_script` fires the reaction immediately on the matching mob using `mob_interpret` (the NPC-safe interpreter). It is used for the **entry-point script only** — the reaction fires without delay. Subsequent scripts in the event are driven by the `trigger_test` / `script_update` pipeline described below.

**`runevent` will crash** if the event has no `first_script` set — it sends an error message in that case so it is safe. Scripts with a NULL `event` pointer are now handled safely via a null check and `LOG_BUG` in `run_script`.

### `runscript <vnum>`

**Source:** `do_run_script()`, `src/act_wiz.c` ~line 5299  
**Registered as:** `runscript`

Finds a script by vnum and calls `run_script()` directly. Useful for manually advancing through event scripts.

**Status: Works** for immediate single-script execution. Scripts with a NULL `event` pointer are guarded safely.

### `interrupt [all]`

**Source:** `do_interrupt()`, `src/act_wiz.c` ~line 5535  
**Registered as:** `interrupt`

Sets `event->stop = TRUE` on the event in the current room (or all running events with `interrupt all` for max-trust characters). Does not clear `room->event`, so the event is flagged as stopped but still referenced.

**Status: Works.**

### `event_end`

**Source:** `do_end_event()`, `src/act_wiz.c` ~line 5583  
**Registered as:** `event_end`

Sets `event->stop = TRUE`, clears `room->event = NULL`, then calls `do_timestop` to resume the game clock.

**Status: Works.** Only the event's original author or a max-trust character can end the event.

---

## Script Chaining and Delay Pipeline

After the entry-point script fires via `runevent`, further script chaining is driven automatically by three components that were already present in the codebase.

### 1. `trigger_test()` — `src/handler.c` ~line 4105

Called from `act()` in `src/comm.c` for every NPC that receives a message. Checks whether the message text contains any script's `trigger` string for the event currently active in the NPC's room. When a match is found:

- Creates a per-mob copy of the script via `new_script()` and assigns it to `ch->script`.
- Sets `ch->script->delay` from the matched script (minimum 1 PULSE_VIOLENCE cycle).
- Sets `ch->script->active = TRUE`.
- Breaks out of the loop (only one script fires per message per mob).

Guards in place:
- Skips if `ch->script != NULL` — does not interrupt a reaction already queued.
- Checks `ch->pIndexData != NULL` before dereferencing the mob prototype vnum.
- Skips if `event->stop` is TRUE.

### 2. `script_update()` — `src/update.c` ~line 1614

Called once per `PULSE_VIOLENCE` (~1.5 seconds) from the main update loop. For every character in the world that has a non-NULL `ch->script`:

1. Decrements `ch->script->delay`.
2. When `delay` reaches 0, calls `mob_interpret(ch, ch->script->reaction)` to execute the reaction.
3. Frees `ch->script` and sets it to NULL.

The per-mob copy is a separate allocation from the original script in `event->script_list`, so freeing it is always safe.

### 3. `act()` in `src/comm.c`

Already calls `trigger_test(buf, to, ch)` for all NPCs (no descriptor) that receive a message. No changes were needed here.

### End-to-end flow

```
runevent fires first script  →  mob says something
  →  act() distributes the message to nearby NPCs
  →  trigger_test() runs on each NPC
  →  matching NPC gets ch->script = new_script() with delay set
  →  script_update() fires every ~1.5 sec
  →  when delay reaches 0 → mob_interpret(ch, reaction)
  →  that reaction (e.g. "say ...") generates another message
  →  act() / trigger_test() picks it up → next script queued
```

This is a fully automatic chain. As long as each script's reaction generates output that another script's trigger will match, the event advances on its own.

---

## Save and Load

### Saving — `plotsave`

**Source:** `do_save_plots()`, `src/olc_save.c` ~line 1150  
**Registered as:** `plotsave`

Writes `area/plots.are` in a custom format:

```
#PLOTS
#<plot_vnum>
<title>~
<author>~
<race_flags>
E<event_vnum>
<title>~
<author>~
<race_flags>
<loc_vnum>
A <mob_vnum>     (one per actor)
A 0              (actor list terminator)
S<script_vnum>
<author>~
<actor_mob_vnum>
<delay>
<first_script>
<trigger>~
<reaction>~
S0               (script list terminator)
E0               (event terminator)
#0               (plot list terminator)
#$
```

File path is `AREA_DIR "plots.are"`. `AREA_DIR` is `"../area/"` on all non-Mac platforms (including Windows), so the file saves to `area/plots.are` relative to the server's run directory (`src/`).

### Loading

Plots are loaded as part of the normal area file boot sequence. `olc_save.c` line 105 includes `plots.are` in the area file list. The load format mirrors the save format.

---

## Summary of Known Bugs

All bugs in the original codebase have been fixed.

| Location | Severity | Status | Description |
|----------|----------|--------|-------------|
| `do_pedit` create branch | Low | **FIXED** | Dead `IS_NULLSTR(arg1)` check removed; replaced NULL-plot `IS_STORYTELLER` call with direct `ch->trust < 3` check. |
| `do_sedit` create branch | Low | **FIXED** | Same dead-code / NULL-pointer issue fixed. Unused `value = atoi(argument)` line also removed. |
| `pedit_delevent` | High | **FIXED** | Rewrote to use `EDIT_PLOT`, accept a vnum argument, and correctly unlink from both the global event list and the plot's event list before freeing. |
| `pedit_assignevent` | High | **FIXED** | Changed `EDIT_EVENT` to `EDIT_PLOT`. Now toggles authors on the plot's own author field rather than misreading a plot pointer as an event. |
| `pedit remove` | Medium | **FIXED** | Added head-of-list handling; now calls `edit_done(ch)` before freeing to avoid dangling `pEdit`. `free_plot` cascades into events and their scripts. |
| `eedit_delscript` | High | **FIXED** | Rewrote to use `EDIT_EVENT`, accept a vnum argument, and unlink from both the global script list and the event's `next_in_event` chain before freeing. |
| `eedit_show` | Medium | **FIXED** | Added null check on `get_room_index()` result; displays "INVALID ROOM" instead of crashing when the location vnum doesn't exist. |
| `eedit remove` | Medium | **FIXED** | Added head-of-list handling; calls `edit_done(ch)` before freeing; unlinks from parent plot's `event_list` / `next_in_plot` chain. `free_event` cascades into scripts. |
| `sedit create` | Medium | **FIXED** | Now tells the user to assign the script to an event with `event <vnum>`; `run_script` null check guards the crash case. |
| `sedit event` | Low | **FIXED** | Changed `ps->next = event->script_list` to `ps->next_in_event = event->script_list` so the script is reachable via the event's chain. |
| `sedit remove` | Medium | **FIXED** | Added head-of-list handling; calls `edit_done(ch)` before freeing; unlinks from parent event's `script_list` / `next_in_event` chain. |
| `run_script` — null event | Medium | **FIXED** | Added null check on `s->event`; logs a bug message and returns safely instead of crashing on orphaned scripts. |
| `run_script` — wrong interpreter | Medium | **FIXED** | Changed `interpret(actor, s->reaction)` to `mob_interpret(actor, s->reaction)`. Using the full player interpreter on an NPC is unsafe. |
| `get_actors` | Low | **FIXED** | Now logs a `LOG_BUG` message when a listed actor mob is not found in the world instead of silently doing nothing. |
| `do_slist` | Low | **FIXED** | Removed dead `if(pScript->active) continue` filter — the `active` field on original scripts is always 0. All scripts now appear in the list. |
| `plotsave` path | Medium | **FIXED** | Changed `fopen("plots.are", "w")` to `fopen(AREA_DIR "plots.are", "w")`. File now saves to `area/plots.are` correctly. |
| `trigger_test` — null before deref | High | **FIXED** | Moved `ch->pIndexData != NULL` check before `ch->pIndexData->vnum` dereference to prevent crash on NPCs with no prototype. |
| `trigger_test` — no break | Medium | **FIXED** | Added `break` after a matching script is found; a mob now reacts to only one trigger per message. |
| `trigger_test` — no busy guard | Medium | **FIXED** | Added `ch->script == NULL` guard around the matching block; a mob with a queued reaction can no longer have it overwritten mid-delay. |
| `trigger_test` — zero delay | Low | **FIXED** | Added `if(delay <= 0) delay = 1` to guarantee at least one PULSE_VIOLENCE cycle before firing, matching the behaviour of the mob prototype trigger block. |

---

## What the System Can Actually Do Today

**Works end-to-end:**
- Creating, editing, listing, and saving plots and events.
- Creating and editing scripts (trigger/reaction/delay stored and used correctly).
- `pedit delevent <vnum>` — removes an event (and its scripts) from the current plot.
- `pedit assignevent <name|All>` — toggles authors on the current plot's author list.
- `eedit delscript <vnum>` — removes a script from the current event.
- Safe deletion: `remove` in all three editors correctly unlinks the object from every list it belongs to (global list, parent list) and cascades into children before freeing.
- `plotsave` — saves to `area/plots.are` via `AREA_DIR`.
- Using `runevent` to teleport a storyteller to a room, move pre-loaded mobs there, and fire the first script immediately.
- Using `runscript` to manually fire any individual script.
- Using `event_end` / `interrupt` to clean up after a scene.
- **Automatic script chaining** — after `runevent` fires the first script, subsequent scripts execute automatically as each mob's reaction generates messages that match other scripts' trigger strings.
- **Scripted delays** — the `delay` field (in PULSE_VIOLENCE cycles, ~1.5 sec each) is honoured by `script_update()`. Minimum delay is 1 cycle.

**Limitations / not implemented:**
- `runevent` does not spawn actors — the mobs must already be in the world before the event starts.
- There is no branching or conditional logic — scripts fire in trigger order, not in a scripted sequence.
- There is no automatic `event_end` when the last script fires — the storyteller must call it manually.
