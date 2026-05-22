# Project Twilight — Ritual System

This document covers the ritual system from two perspectives: **player-facing help file content** (what to add to `area/help.are` or the XML help loader) and **technical reference** for builders and staff.

---

## Table of Contents

1. [Player Help Files](#player-help-files)
   - [RITUALS](#rituals)
   - [RITUALS LIST](#rituals-list)
   - [RITUALS INTONE](#rituals-intone)
   - [RITUALS KNOWN](#rituals-known)
   - [RITUALS HINT](#rituals-hint)
   - [RITUALS CANCEL](#rituals-cancel)
   - [RITUAL ACTIONS](#ritual-actions)
2. [Staff and Builder Reference](#staff-and-builder-reference)
   - [How the System Works](#how-the-system-works)
   - [Ritual Data](#ritual-data)
   - [Staff Commands](#staff-commands)
   - [ritedit — In-Game Ritual Editor](#ritedit--in-game-ritual-editor)
   - [ritesave — Saving Ritual Data](#ritesave--saving-ritual-data)
   - [ITEM_GRIMOIRE — In-World Ritual Books](#item_grimoire--in-world-ritual-books)
   - [oedit grimoire — Linking a Book to a Ritual](#oedit-grimoire--linking-a-book-to-a-ritual)
   - [Adding New Ritual Functions](#adding-new-ritual-functions)
   - [XML Format Reference](#xml-format-reference)
   - [Boot Sequence](#boot-sequence)
3. [Known Rituals Reference](#known-rituals-reference)

---

## Player Help Files

The entries below are formatted for direct entry into the game's help system.

---

### RITUALS

**Topic:** Rituals  
**Races:** vampire, werewolf  
**See Also:** RITUAL ACTIONS, RITUALS INTONE, RITUALS LIST, RITUALS KNOWN, RITUALS HINT, RITUALS CANCEL

Rituals are occult ceremonies performed through a precise sequence of physical actions. Each ritual consists of two to nine steps performed in a specific order, followed by the intoning command to complete the working.

**Basic usage:**

    rituals <action>
    rituals intone [target]
    rituals cancel
    rituals list rites
    rituals list actions
    rituals known
    rituals hint

**Performing a ritual:**

1. Type `rituals list actions` to see all available actions.
2. Type `rituals list rites` to see rituals available to your character.
3. Perform each step of the ritual in order: `rituals <action>`
4. After the final step, type `rituals intone` to complete the working.

After each action, you will receive feedback telling you how many steps you have completed and how many remain. If you perform an action that does not match any known ritual, the working collapses and you must start over.

**Example:**

    rituals breathe
    rituals focus
    rituals temples
    rituals intone

Some rituals require a target. If a ritual is directed at another character or an object you are carrying, add the target name after `intone`:

    rituals intone <character>
    rituals intone <object>

---

### RITUALS LIST

**Topic:** Rituals List  
**Syntax:** `rituals list rites` / `rituals list actions`

`rituals list actions` displays all physical actions you can use as ritual steps, regardless of race or access.

`rituals list rites` displays all rituals your character is currently eligible to perform, based on race, clan, discipline level, or rank. It does not reveal the sequence required — that must be learned through roleplay, research, or a grimoire.

---

### RITUALS INTONE

**Topic:** Rituals Intone  
**Syntax:** `rituals intone [target]`

Spoken after the final step of a ritual sequence to complete the working. If the sequence is correct and your character meets the requirements, the ritual takes effect.

If the ritual targets another character, name them after `intone`. If it targets an object in your inventory, name the object. Rituals with no target require no argument.

If your action sequence does not match any known ritual, you will be told the working has failed to coalesce and must start over.

---

### RITUALS KNOWN

**Topic:** Rituals Known  
**Syntax:** `rituals known`

Displays rituals your character has successfully completed at least once, along with the full action sequence for each. Characters who have performed a ritual retain this knowledge permanently.

Tremere vampires with Thaumaturgy 1 automatically know the Rite of Introduction upon awakening their discipline; they do not need to perform it first to have it listed here.

This command is available immediately even if your powers have not yet rejuvenated.

---

### RITUALS HINT

**Topic:** Rituals Hint  
**Syntax:** `rituals hint`

Displays the name and step count of every ritual your character is currently eligible to perform. It does not reveal the sequence. Use it to confirm which rituals are available to you before seeking out the knowledge of how to perform them.

This command is available immediately even if your powers have not yet rejuvenated.

---

### RITUALS CANCEL

**Topic:** Rituals Cancel  
**Syntax:** `rituals cancel`

Abandons the current ritual in progress, clearing all accumulated steps. Use this if you have made an error or wish to stop before completing the sequence.

This command is available immediately even if your powers have not yet rejuvenated.

---

### RITUAL ACTIONS

**Topic:** Ritual Actions  
**See Also:** RITUALS

The following actions are available for use in ritual sequences. Each takes one combat round to perform and is visible to others in the room.

| Action | What others see |
|--------|----------------|
| `step` | takes a solemn step |
| `temples` | rubs their temples |
| `circles` | waves hands in circles |
| `focus` | stares intently |
| `raise` | raises hands in the air |
| `point` | points a finger |
| `draw` | waves hands in the air |
| `howl` | howls |
| `chant` | begins to chant in an unknown language |
| `trace` | traces out symbols |
| `entreat` | seems to call on some occult source of power |
| `scratch` | scratches around on the ground |
| `splay` | splays a hand at arm's length |
| `halt` | holds up a hand as if demanding 'Halt!' |
| `dance` | dances around in a circle |
| `breathe` | controls their breathing |
| `blood` | draws some of their own blood |
| `beat` | beats their chest |
| `groan` | groans loudly |

Ritual sequences are learned through in-character means: tutelage, research, or reading a grimoire. The `rituals list rites` command confirms which rituals are available to you; it does not reveal their sequences.

---

## Staff and Builder Reference

---

### How the System Works

The ritual system allows characters to perform multi-step occult ceremonies by entering a sequence of actions followed by an `intone` command. The system tracks partial sequences in real time and gives the player immediate feedback after each step.

**Flow:**

1. Player types `rituals <action>`. The action index is appended to `ch->riteacts[]` and `ch->ritepoint` is incremented.
2. `rite_partial_lookup(ch)` checks whether the current prefix matches any ritual available to the character. If no ritual matches, the working fails immediately and the sequence is cleared.
3. If a match is found, the player is told how many steps are complete and how many remain. If the final step was just performed, they are prompted to intone.
4. Player types `rituals intone [target]`. `rite_lookup(ch)` finds the ritual whose full sequence matches `ch->riteacts[]`. `rite_available()` re-checks eligibility. The ritual's `spell_fun` is called directly.
5. On success, `learn_rite(ch, ritual)` records the ritual's stable ID in `ch->known_rite_ids[]`.

**Power timer:** The `power_timer` check only blocks the action-performing parts of `do_rituals()`. The informational subcommands (`known`, `hint`, `list`, `cancel`) are processed before the check and are always available.

**Key functions:**

| Function | Location | Purpose |
|---|---|---|
| `do_rituals()` | `magic.c` | Entry point for all `rituals` subcommands |
| `rite_available(r, ch)` | `magic.c` | Checks race, disc level, or org rank eligibility |
| `rite_lookup(ch)` | `lookup.c` | Finds a ritual whose full sequence matches current riteacts |
| `rite_partial_lookup(ch)` | `lookup.c` | Finds a ritual that the current prefix is valid for |
| `rite_count_steps(r)` | `lookup.c` | Counts non-(-1) entries in `r->actions[]` |
| `knows_rite(ch, r)` | `lookup.c` | Returns TRUE if the character has `r->id` in `known_rite_ids[]` |
| `learn_rite(ch, r)` | `lookup.c` | Appends `r->id` to `known_rite_ids[]` if not already present |
| `show_grimoire(ch, obj)` | `magic.c` | Generates narrative text from a grimoire object |
| `load_rituals_xml(fp)` | `db.c` | Loads ritual and action data from `rituals.xml` |
| `load_rituals_bootstrap()` | `tables.c` | Copies hardcoded bootstrap data to dynamic structures |
| `do_ritesave(ch, arg)` | `olc_save.c` | Writes current runtime data to `rituals.xml` |

---

### Ritual Data

Ritual data is stored at runtime in two dynamic structures declared in `tables.c` and exported via `tables.h`:

```c
struct ritual_type  *ritual_list;       /* linked list of all rituals */
struct ritemove_type *rite_actions;     /* dynamic array of all actions */
int                   max_rite_actions; /* count of valid entries in rite_actions */
int                   next_ritual_id;   /* next ID to assign when creating a ritual */
```

**`struct ritual_type` fields:**

| Field | Type | Meaning |
|---|---|---|
| `next` | `struct ritual_type *` | Next ritual in linked list |
| `id` | `int` | Stable integer ID — assigned at creation, never reused, survives renaming and reordering |
| `name` | `char *` | Display name (e.g. `"rite of introduction"`) |
| `races` | `char *` | Race or org name required, or `"all"` |
| `disc_test` | `int` | Discipline index required (`-1` = none / use rank) |
| `level` | `int` | Discipline level or rank required |
| `actions[]` | `int[MAX_RITE_STEPS]` | Action indices; `-1` = unused slot |
| `beats` | `int` | Combat wait ticks after a successful intone |
| `target` | `int` | Targeting constant: `TAR_IGNORE`, `TAR_CHAR_DEFENSIVE`, `TAR_OBJ_INV` |
| `spell_fun` | `SPELL_FUN *` | Direct function pointer to the ritual's effect |

**`struct ritemove_type` fields:**

| Field | Type | Meaning |
|---|---|---|
| `name` | `char *` | Action keyword (what the player types) |
| `to_char` | `char *` | Message shown to the performer |
| `to_room` | `char *` | Message shown to others in the room |
| `grimoire_text` | `char *` | Narrative phrase used in grimoire descriptions |
| `beats` | `int` | Wait ticks applied per action step |

**`CHAR_DATA` fields added for this system:**

| Field | Type | Meaning |
|---|---|---|
| `riteacts[]` | `int[MAX_RITE_STEPS]` | Action indices accumulated in current working |
| `ritepoint` | `int` | How many steps have been entered |
| `known_rite_ids[]` | `int[MAX_KNOWN_RITES]` | Array of stable ritual IDs the character has completed |
| `n_known_rites` | `int` | Number of valid entries in `known_rite_ids[]` |

`MAX_KNOWN_RITES` is 64. Known rites are saved in the character file as one `KnRite <id>` line per known ritual.

**Why IDs instead of positions:** Storing the ritual's stable `id` rather than its position in `ritual_list` means that renaming a ritual, reordering the list, or deleting and recreating a ritual does not corrupt existing character data. The ID is assigned at creation (`ritedit create`) or on bootstrap load, is written into `rituals.xml`, and is never reused.

---

### Staff Commands

#### rituals list all

    rituals list all

Admin-only subcommand (requires `IS_ADMIN`). Displays every ritual in the system regardless of availability to the viewer, with full details:

    All rituals (staff view):
      [  1] rite of introduction        races=vampire     disc=18  lvl=1  steps: blood, temples, step
      [  2] rite of introduction        races=werewolf    disc=-1  lvl=1  steps: blood, temples, step
      [  3] rite of attunement          races=werewolf    disc=-1  lvl=1  steps: temples, raise, point, entreat
      ...

Columns: `[id]`, name, races filter, discipline index, required level, full action sequence.

---

### ritedit — In-Game Ritual Editor

`ritedit` is an OLC editor for modifying rituals at runtime without restarting the server. Changes take effect immediately and can be saved to disk with `ritesave`.

**Access:** Requires builder level 2 or higher.

**Entry:**

    ritedit create <name>       — create a new ritual and enter edit mode
    ritedit <name>              — open an existing ritual by name

**Commands inside ritedit:**

| Command | Syntax | Effect |
|---|---|---|
| `show` | `show` | Display all fields of the current ritual, including its ID |
| `name` | `name <text>` | Set the ritual's display name |
| `races` | `races <race \| org \| all>` | Set the required race, org name, or `all` |
| `disc` | `disc <name \| index \| -1>` | Set the discipline check (`-1` = use rank instead) |
| `level` | `level <number>` | Set the discipline level or rank required |
| `beats` | `beats <number>` | Set post-completion wait ticks |
| `target` | `target <0 \| 2 \| 4>` | Set targeting: 0=ignore, 2=char defensive, 4=obj in inventory |
| `effect` | `effect <function_name>` | Set the effect function (must be in `rite_fun_table`) |
| `sequence` | `sequence <action1> [action2] ...` | Set the full action sequence (space-separated action names) |
| `delete` | `delete` | Remove the ritual from the linked list and free memory |
| `done` | `done` | Exit the editor |
| `?` | `?` | List available commands |

**Example session:**

    ritedit create rite of cleansing
    > name rite of cleansing
    > races werewolf
    > disc -1
    > level 2
    > beats 2
    > target 0
    > effect rite_hero
    > sequence breathe focus entreat chant
    > show
    > done
    ritesave

**Notes:**
- `ritedit create` assigns a new stable ID automatically from `next_ritual_id`. That ID is written to `rituals.xml` by `ritesave` and persists across reboots.
- `effect` must be a function name registered in `rite_fun_table[]` in `tables.c`. If the name is not found, the editor will report an error and the change will not be applied. To add new effect functions, see [Adding New Ritual Functions](#adding-new-ritual-functions).
- `sequence` replaces the entire action sequence. Provide all steps in order, space-separated. Up to `MAX_RITE_STEPS` (10) actions are supported.
- `races` accepts a race name (e.g. `vampire`, `werewolf`), the name of an organisation, or `all`.
- Changes made in `ritedit` exist only in memory until `ritesave` is run.

---

### ritesave — Saving Ritual Data

    ritesave

Writes all current runtime ritual data (both actions and rituals) to `rituals.xml` in the server's working directory. This file is loaded automatically on the next server boot. Run `ritesave` after any `ritedit` session to persist changes.

**Access:** Requires staff level 1.

The file is written atomically by closing the reserve file descriptor, opening the XML file for writing, completing the write, then reopening the reserve. If the write fails, an error is logged and the reserve is still reopened.

**Note on file location:** The server runs from the `area/` directory (the startup script does `cd ../area` before launching the binary). `rituals.xml` therefore lives at `area/rituals.xml` on disk, but is opened as `"rituals.xml"` in code — no path prefix.

---

### ITEM_GRIMOIRE — In-World Ritual Books

`ITEM_GRIMOIRE` (item type 35) is a special item type for in-world books and texts that describe a ritual. When a player examines or reads a grimoire, the system dynamically generates a description of the linked ritual based on the object's clarity and condition values.

**Object values:**

| Value | Meaning |
|---|---|
| `v0` | Clarity: 0=Full, 1=Partial, 2=Cryptic |
| `v1` | Condition: 0–100 (higher = more legible) |
| Extra descr `GRIMOIRE_RITUAL` | The name of the linked ritual (set via `oedit grimoire`) |

**Clarity effects:**

- **Full (0):** The text uses the full `grimoire_text` phrase for each action (e.g. *"an offering of blood drawn from the palm"*). Output reads as a narrative sentence: *"The text describes a rite performed in 3 parts: controlled breathing exercises, deep concentration drawing in energies, and finally fingertips pressed firmly to the temples."*
- **Partial (1):** The text uses the bare action name only (e.g. *breathe, focus, temples*). Output reads as: *"The text references 3 steps: breathe, focus, temples."*
- **Cryptic (2):** Each action name is wrapped in ellipses (e.g. *...breathe... ...focus...*). Output reads as: *"Fragments of text speak of ...breathe... ...focus... ...temples..."*

**Condition dropout:**

Each step is individually subject to condition dropout. For each step, the system rolls `1d100` against the condition value. If the roll exceeds the condition, that step is replaced with `[illegible section]` in the output. A condition of 100 means every step is always legible. A condition of 0 means every step is always illegible.

**Triggering the description:**

The grimoire description is generated when the player uses `examine <item>` or `read <item>`. The same output is produced regardless of which command is used.

---

### oedit grimoire — Linking a Book to a Ritual

Inside `oedit` (object editor), use the `grimoire` command to link an ITEM_GRIMOIRE object to a ritual:

    oedit grimoire <ritual name>
    oedit grimoire none

`oedit grimoire <name>` sets (or replaces) the `GRIMOIRE_RITUAL` extra description on the object. The ritual name must exactly match a ritual in `ritual_list`; if it doesn't, the command reports an error and shows a suggestion to use `rituals list rites`. The command is not case-sensitive.

`oedit grimoire none` removes the link entirely. A grimoire with no linked ritual displays a generic message when examined.

**Full setup example:**

    oedit <vnum>
    > type grimoire
    > v0 0
    > v1 85
    > grimoire rite of introduction
    > short a worn leather-bound journal
    > long A worn leather-bound journal lies here, its pages darkened with age.
    > done
    asave changed

---

### Adding New Ritual Functions

To add a new ritual effect, two steps are required in the source code:

**1. Write the function in `magic.c`:**

```c
void rite_my_new_rite(int sn, int level, CHAR_DATA *ch, void *vo, int target)
{
    /* effect code */
}
```

Declare it in `magic.h`:

```c
DECLARE_SPELL_FUN( rite_my_new_rite );
```

**2. Register it in `tables.c`:**

Add a forward declaration near the top of the ritual section:

```c
DECLARE_SPELL_FUN( rite_my_new_rite );
```

Add an entry to `rite_fun_table[]`:

```c
{ "rite_my_new_rite", rite_my_new_rite },
```

After recompiling, the function name `rite_my_new_rite` will be accepted by `ritedit effect` and by the XML loader.

**Targeting constants for the `target` field:**

| Constant | Value | Behaviour |
|---|---|---|
| `TAR_IGNORE` | 0 | No target; `vo` is NULL |
| `TAR_CHAR_DEFENSIVE` | 2 | Targets a character (defaults to self if no arg given) |
| `TAR_OBJ_INV` | 4 | Targets an object the character is carrying |

---

### XML Format Reference

`rituals.xml` stores all action and ritual data. It is written by `ritesave` and read at boot. The format is:

```xml
<?xml version='1.0' encoding='UTF-8'?>
<Rituals>
  <next_id>7</next_id>

  <Action>
    <name>breathe</name>
    <to_char>You perform the breathing exercises.</to_char>
    <to_room>$n starts controlling $s breathing.</to_room>
    <grimoire_text>controlled breathing exercises</grimoire_text>
    <beats>1</beats>
  </Action>

  <!-- ... all 19 actions ... -->

  <Ritual>
    <id>1</id>
    <name>rite of introduction</name>
    <races>vampire</races>
    <disc_test>18</disc_test>
    <level>1</level>
    <beats>2</beats>
    <target>0</target>
    <effect>rite_introduction</effect>
    <sequence>blood temples step</sequence>
  </Ritual>

  <!-- ... other rituals ... -->

</Rituals>
```

**Notes:**
- `<next_id>` appears once at the top of the file. It holds the next ID to assign when `ritedit create` is used. The loader reads it and sets `next_ritual_id` accordingly.
- `<id>` is written per ritual and loaded back into `r->id`. This is the stable identifier that character save files reference. It is never reused or renumbered.
- `<sequence>` stores space-separated action names. The loader resolves them to indices at load time via `riteaction_lookup()`.
- `<effect>` stores the function name string. The loader resolves it to a function pointer via `rite_fun_lookup()`. If the name is not found, a bug is logged and the ritual is still loaded with a NULL `spell_fun`; attempting to intone it will produce an error message and log a bug entry.
- `<disc_test>` stores the discipline index integer, not the name. -1 means no discipline check (rank is used instead).
- `<target>` stores the integer targeting constant directly (0, 2, or 4).
- All text fields are XML-escaped on write (handles `<`, `>`, `&`, `"`, `'`) and unescaped on load.
- All `<Action>` blocks must appear before all `<Ritual>` blocks in the file, since rituals reference actions by name and the action array must be populated first.

---

### Boot Sequence

On server boot, `boot_db()` attempts to open `rituals.xml`. If the file exists, `load_rituals_xml()` reads it into the dynamic structures (`ritual_list` linked list and `rite_actions` array) and sets `next_ritual_id` from the `<next_id>` element.

If the file does not exist (first boot after a fresh install or after deleting the file), `load_rituals_bootstrap()` is called instead, which copies the hardcoded default data from `tables.c` into the same dynamic structures and assigns stable IDs 1 through N automatically.

**Migration from pre-ID XML:** If `rituals.xml` was written by an older version of the code that did not include `<id>` or `<next_id>` fields, the loader detects missing IDs (value remains 0 after parsing) and auto-assigns sequential IDs during load. This is transparent. Run `ritesave` immediately after such a boot to bake the assigned IDs into the file permanently.

The boot sequence logs one of:
- `Rituals loaded from XML.`
- `Rituals loaded from bootstrap (no XML found).`

**After first bootstrap boot:** Run `ritesave` in-game to write `rituals.xml`. All subsequent boots will load from XML.

---

### Character Save Format

Known rituals are stored in the character `.plr` file as one line per ritual:

    KnRite  1
    KnRite  3

Each number is the stable ritual ID. On load, these are appended to `ch->known_rite_ids[]`. Unrecognised IDs (e.g. for rituals that have since been deleted) are silently ignored by `knows_rite()` — the character simply will not match any current ritual for that slot.

**Migration note:** The previous format used a single `KnRites <bitmask>` line. That key is no longer recognised; existing player files with `KnRites` will have that line silently skipped on their first login after the upgrade. Players will start with an empty known-rites list and must re-complete rituals to rebuild it.

---

## Known Rituals Reference

This section documents all rituals in the default (bootstrap) dataset. Sequences are listed here for staff reference only — players are expected to discover them through roleplay. Use `rituals list all` in-game for the authoritative list including any rituals added after bootstrap.

---

### Rite of Introduction

**ID:** 1 (vampire), 2 (werewolf)  
**Available to:** Vampire (Thaumaturgy 1+), Werewolf (rank 1+)  
**Target:** None  
**Steps:** 3  
**Sequence:** `blood` → `temples` → `step`  
**Effect:** `rite_introduction` — broadcasts an introduction to all online characters of the same race, delivering the caster's chosen message (supplied as the `intone` argument). Requires a message of 20 words or fewer: `rituals intone <message>`.

Vampire Tremere characters automatically learn this ritual when they first raise Thaumaturgy to level 1, without needing to complete it first.

---

### Rite of Attunement

**ID:** 3  
**Available to:** Werewolf (rank 1+)  
**Target:** Object in inventory  
**Steps:** 4  
**Sequence:** `temples` → `raise` → `point` → `entreat`  
**Effect:** `rite_attune` — attunes the target object to the werewolf, marking it spiritually.

---

### Rite of Pack Creation

**ID:** 4  
**Available to:** Werewolf (rank 1+)  
**Target:** Character (defensive)  
**Steps:** 9  
**Sequence:** `entreat` → `chant` → `step` → `circles` → `chant` → `raise` → `howl` → `entreat` → `dance`  
**Effect:** `rite_pack` — creates a pack bond between the caster and the target character, establishing pack leadership.

---

### Rite of Recognition

**ID:** 5  
**Available to:** Werewolf (rank 1+)  
**Target:** Character (defensive)  
**Steps:** 9  
**Sequence:** `step` → `raise` → `step` → `raise` → `point` → `splay` → `halt` → `blood` → `point`  
**Effect:** `rite_recognition` — elevates the target character's race status by one rank. The caster must hold at least two ranks above the target. Fails silently (but without negative consequence) if the rank requirement is not met.

---

### Rite of the Returning Hero

**ID:** 6  
**Available to:** Werewolf (rank 1+)  
**Target:** None  
**Steps:** 8  
**Sequence:** `step` → `raise` → `step` → `raise` → `howl` → `beat` → `howl` → `halt`  
**Effect:** `rite_hero` — the caster spends experience equal to `5 × current_rank / 2`. On a successful Occult+Wits or Rituals+Charisma roll against the current rank, the caster's race status increases by one. On failure, the experience is not spent and no promotion occurs.

---

*Document last updated: 2026-05-21. Reflects stable ID system, power_timer fix for informational subcommands, `rituals list all` staff command, and XML migration handling.*
