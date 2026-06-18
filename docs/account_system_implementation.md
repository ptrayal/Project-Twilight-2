# Account System — Implementation Notes
**Version:** 1.5  
**Date:** 2026-06-05  
**Status:** All phases complete — 1–10, 12, 13, 14, 15, 16, 17, 18, 19

This document covers implementation decisions, known gotchas, file locations, and remaining work. For the architecture and policy decisions, see `account_system_design.md`.

---

## File Locations

| Purpose | Path |
|---------|------|
| Account files | `account/[lowercase-name]` |
| Account ID counter | `account/next_id` |
| Hard-deleted accounts | `account/deleted/` |
| Audit log | `log/account/audit.log` |
| Point ledger | `log/account/ledger.log` |
| Orphan log | `log/account/orphan.log` |
| Hard-deleted pfiles | `player/deleted/` |

All four directories are created automatically at boot via `mkdir()` calls in `boot_db()`.

---

## Source Files

| File | Role |
|------|------|
| `src/account.c` | Core lifecycle, bcrypt, economy, audit logging, query helpers |
| `src/account_cmd.c` | Hub display, `do_account` dispatcher |
| `src/include/account.h` | All typedefs, constants, function declarations |
| `src/include/crypt_blowfish.h` | Internal bcrypt header |
| `src/include/crypt_gensalt.h` | Internal gensalt header |
| `src/crypt_blowfish.c` | Openwall bcrypt implementation |
| `src/crypt_gensalt.c` | Openwall salt generation |

---

## Critical Implementation Decisions

### bcrypt: Use `_crypt_blowfish_rn`, NOT `crypt_ra`

`crypt_ra(plaintext, salt, NULL, NULL)` **crashes** — it dereferences the `void **data` and `int *size` pointers immediately to manage its internal heap allocation. Passing NULL causes a segfault.

Use the internal functions directly:

```c
/* Correct — fixed stack buffers, no null pointer issues */
_crypt_gensalt_blowfish_rn("$2b$", 12, rnd, sizeof(rnd), salt_buf, sizeof(salt_buf));
_crypt_blowfish_rn(plaintext, salt_buf, hash_buf, sizeof(hash_buf));
```

### `crypt()` macro preserved for legacy character passwords

`twilight.h` has `#define crypt(s1, s2) (s1)` which makes the old character password system a passthrough (passwords stored as plaintext). This is intentional for the migration window. `comm.c` and `act_info.c` use this for `CON_GET_OLD_PASSWORD`. Do not remove until all characters are migrated.

### `char_to_room()` before `reset_char()` in `account_enter_game()`

`load_char_obj()` sets `ch->in_room` but does not link the character into the room's people list. `reset_char()` calls `char_from_room()` internally, which requires the character to already be in the room's people list. Missing this produces:

```
ch not found in ch->in_room->people.
```

Always call `char_to_room(ch, ch->in_room)` before `reset_char()` when loading via the account system.

### Account names are lowercase

Account files are stored as `account/[lowercase-name]`. The name prompt lowercases input before lookup. This avoids case-collision issues on Linux filesystems. Character names retain their normal capitalization.

### `fread_word` includes trailing `~`

When reading the `Characters` and `Notes` blocks from account files, `fread_word()` includes the trailing `~` delimiter as part of the string. Strip it manually:

```c
int len = strlen(cw);
if (len > 0 && cw[len-1] == '~')
    cw_clean[len-1] = '\0';
```

This is already handled in `load_account()` in `account.c`.

### `CON_ACCT_NEW_PASS` state machine

The state machine uses `d->account != NULL` (not `d->repeat`) to distinguish "past Y/N, collecting password" from "waiting for Y/N response". The `d->repeat` field was unreliable because it gets reset by the game loop between nanny calls.

### Null-password warning suppressed for account users

`CON_READ_MOTD` originally warned about null `pcdata->pwd`. This is suppressed when `d->account` is set, since account-authenticated characters legitimately have no character password.

**Critical:** The original check was `ch->pcdata->pwd[0] == '\0'` which crashes when `pwd` is NULL (new characters created via hub never have a password set). The check must use `IS_NULLSTR(ch->pcdata->pwd)` which is null-safe. This was a segfault that manifested at `comm.c:3645` during new character creation.

### `save_char_obj` must not be called before `reset_char` and `char_to_room`

The Phase 19 auto-link hook in `CON_READ_MOTD` must not call `save_char_obj()`. For new characters, `ch->in_room` is NULL at that point — `save_char_obj` → `fwrite_char` dereferences `ch->in_room->contents` immediately and crashes.

The pfile is saved correctly by the `do_save` call at the end of `CON_READ_MOTD` after `reset_char()` and `char_to_room()` have both run. Only `save_account()` is called inside the Phase 19 hook.

### New character race prompt must be sent explicitly

When `[C]` routes to `CON_GET_NAME` with `acct_creating_char = TRUE`, the code skips `CON_CONFIRM_NEW_NAME` and sets `d->connected = CON_GET_NEW_RACE`. However, `CON_GET_NEW_RACE` is a passive case — it only runs when the player sends input. Without sending the race prompt text before returning, the player sits at a blank screen waiting.

The fix: write the race list and prompt directly before setting `CON_GET_NEW_RACE` and returning. This is done in `comm.c` in the `acct_creating_char` branch of `CON_GET_NAME`.

---

## Connection Flow (Implemented)

```
Connect
  → CON_GET_ACCT_NAME     validate account name, lookup or start creation
  → CON_GET_ACCT_PASS     bcrypt verify, banned/frozen/soft-delete checks
  → CON_ACCT_MENU         hub display and choice routing
      [P] → load_char_obj → account_enter_game → CON_PLAYING
      [C] → CON_GET_NAME (acct_creating_char=TRUE) → race/creation → CON_READ_MOTD
      [Q] → close_socket
  → CON_ACCT_TOKEN        (if password_reset_pending) token entry
  → CON_ACCT_NEW_PASS     Y/N then password entry
  → CON_ACCT_CONFIRM_PASS confirm → bcrypt hash → create_account or update hash
```

### Migration path (Phase 10)

Unlinked characters logging in via `CON_GET_OLD_PASSWORD` are intercepted after password verification and redirected to `CON_GET_ACCT_NAME`. After account creation/login, `CON_READ_MOTD` fires the Phase 19 hook which links the character automatically.

### New character auto-link (Phase 19)

In `CON_READ_MOTD`: if `d->account` is set and `ch->pcdata->acct_login` is empty, the character is linked to the account by:
1. Writing `acct_login` and `acct_id` to `ch->pcdata`
2. Adding an `ACCOUNT_CHARACTER` entry to `d->account->characters`
3. Saving the account file only (NOT the pfile — see gotcha above)
4. Writing an audit log entry `CHAR_LINKED`

The pfile (`AcLg`/`AcId` fields) is saved by the `do_save` call at the end of `CON_READ_MOTD` once the character is fully initialized. **Tested and working as of 2026-05-30.**

### Quit returns to hub

`do_quit` in `act_comm.c` detects `d->account` and calls `show_account_hub(d)` + sets `CON_ACCT_MENU` instead of `close_socket(d)`.

### Copyover (hotboot) support

Account name is saved as the 4th field on the descriptor line in the copyover temp file. On recovery, `load_account(saved_name)` reloads the account and reattaches it to the descriptor.

---

## `new_descriptor()` initialization

`recycle.c::new_descriptor()` initializes:
```c
d->connected        = CON_GET_ACCT_NAME;  /* not CON_GET_NAME */
d->account          = NULL;
d->acct_temp_pass   = NULL;
d->acct_is_reset    = FALSE;
d->acct_creating_char = FALSE;
```

---

## Account File Format

Text keyword/value, same conventions as pfiles. Strings terminated with `~`. Stored at `account/[name]`.

```
#ACCOUNT
Id        1001
Name      nyx~
PassHash  $2b$12$...~
Email     ~
Flags     0
ModFlags  0
TrustCeil 5
Earned    185
Spent     100
CreatedOn 1716933600
LastLogin 1716933600
SoftDel   0
RstTok    ~
RstPend   0
RstIssd   0
WeekSec   0
WeekRst   1716933600
MonGrants 0
GrantsRst 1716933600

IpLog
  1716933600 192.168.1.1~
EndIpLog

Characters
  Rayal~ 1042 1716933600 0
EndCharacters

Unlocks
EndUnlocks

Notes
  SYSTEM~ 0 1716933600 Account created.~
EndNotes

#END
```

**Character line format:** `name~  char_id  last_played  soft_deleted_on`  
The `~` on the name is stripped on load — do not manually remove it from files.

---

## Completed Phases

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | `twilight.h` — structs, CON_ constants, descriptor/pcdata fields | ✅ Complete |
| 2 | `account.h` — all declarations | ✅ Complete |
| 3 | `recycle.c/h` — allocation functions | ✅ Complete |
| 4 | `account.c` — file I/O, bcrypt, economy, logging | ✅ Complete |
| 5 | `save.c` — AcLg/AcId pfile fields | ✅ Complete |
| 6 | bcrypt integration (crypt_blowfish) | ✅ Complete |
| 7 | Migration — manual file editing documented | ✅ Partial (auto-migration via Phase 10) |
| 8 | `comm.c` — close_socket lifecycle, copyover | ✅ Complete |
| 9 | nanny() — all 5 account states | ✅ Complete |
| 10 | Migration flow — unlinked characters redirected automatically | ✅ Complete |
| 12 | Weekly activity pulse — `char_update()` hook in `update.c` | ✅ Complete |
| 13 | `MOD_RESTRICTED` whitelist enforcement in `interpret()` | ✅ Complete |
| 14 | Unlock store — clan unlocks, char slot unlocks, hub `[U]` | ✅ Complete |
| 15 | Staff commands — full `do_account` dispatcher | ✅ Complete |
| 16 | Orphan scanner — `account_boot_scan()` in `account.c` | ✅ Complete |
| 17 | Hard-delete scan — boot scan + `account purge` command | ✅ Complete |
| 18 | Hub `[D]` delete character — soft-delete with name confirmation | ✅ Complete |
| 19 | New character auto-link to account | ✅ Complete |

---

## MOD_RESTRICTED Whitelist

When `MOD_RESTRICTED` is set on an account (`mod_flags` bit 2, value 4), characters on that account are limited to a defined whitelist of commands. All other commands return "Your account is restricted. Contact staff."

### How it works

The check is in `interp.c`, `interpret()`, after command lookup succeeds and after the "Huh?" check, before any gameplay flag checks (astral, earthmeld, etc.):

```c
if ( !IS_NPC(ch)
    && ch->desc
    && ch->desc->account
    && IS_SET(ch->desc->account->mod_flags, MOD_RESTRICTED)
    && !account_cmd_is_whitelisted(command) )
{
    send_to_char( "Your account is restricted. Contact staff.\n\r", ch );
    return;
}
```

`account_cmd_is_whitelisted()` does a `str_prefix` scan of the static whitelist in `account.c`.

### Current whitelist

```c
/* src/account.c — restricted_cmd_whitelist[] */
static const char *restricted_cmd_whitelist[] =
{
    "say",
    "quit",
    "look",
    "north", "south", "east", "west", "up", "down",
    "help",
    "who",
    "account",
    NULL
};
```

### Adding commands to the whitelist

1. Open `src/account.c`
2. Find the `restricted_cmd_whitelist[]` array near the top of the file
3. Add the new command name as a string before the `NULL` terminator
4. Recompile — no other file needs to change

**Example** — to allow `emote` and `score`:
```c
    "account",
    "emote",
    "score",
    NULL
```

**Notes:**
- Matching uses `str_prefix` so `"north"` matches `"no"`, `"nor"`, `"nort"`, `"north"` — use the full command name to avoid unintended matches
- Commands are matched against `command` (the first word of input) not the full argument string
- The whitelist applies to all characters on the account, regardless of their individual trust level
- Staff (`IS_ADMIN`) characters bypass the check since their trust level prevents `MOD_RESTRICTED` from being set below IMPLEMENTOR level (`MOD_ADMIN_LOCKED`)

### Setting restriction in-game (staff)

Use the in-game command (requires STORYTELLER trust or above):

```
account restrict <accountname>
account unrestrict <accountname>
```

Or set it manually by editing the account file:

```bash
nano ~/Documents/Muds/Project-Twilight-2/account/[accountname]
```

Change `ModFlags 0` to `ModFlags 4` (bit 2 = `MOD_RESTRICTED`). Reboot or wait for next login.

---

## Staff Commands Reference

All staff commands are accessed via `account <subcommand>` in-game. All actions are written to `log/account/audit.log`.

### Trust level requirements

| Trust | Level | Commands |
|-------|-------|----------|
| Player | 0 | `info`, `points`, `ledger`, `unlocks` |
| STORYTELLER | 3 | `info <name>`, `note list/add`, `grant`, `freeze/unfreeze`, `restrict/unrestrict`, `scan` |
| MASTER | 4 | `ban/unban`, `transfer`, `reset password`, `purge` |
| IMPLEMENTOR | 5 | `trust_ceiling`, `admin_lock/admin_unlock` |

### Command syntax

```
account info [name]                        — own info or target account detail
account points                             — own balance
account ledger                             — own transaction history
account unlocks                            — own unlocks list

account note list <name>                   — view notes (filtered by caller trust)
account note add  <name> <vis> <text>      — add note  (vis: 0=system 1=admin 2=staff 3=owner)
account grant     <name> <amount> <reason> — grant points (monthly cap: trust×10)
account freeze    <name>                   — block from playing (can still reach hub)
account unfreeze  <name>
account restrict  <name>                   — limit to command whitelist
account unrestrict <name>
account scan                               — orphan check (Phase 16, stub)

account ban       <name> [reason]          — block login entirely
account unban     <name>
account transfer  <charname> <from> <to>   — move character between accounts
account reset password <name>              — generate 12-char single-use token
account purge                              — hard-delete scan (Phase 17, stub)

account trust_ceiling <name> <level>       — cap trust for all chars on account
account admin_lock   <name>                — prevent modification below IMPLEMENTOR
account admin_unlock <name>
```

### Grant caps

Monthly grant limit resets every 30 days per granting character's account:

| Trust | Monthly cap |
|-------|-------------|
| STORYTELLER (3) | 30 points |
| MASTER (4) | 40 points |
| IMPLEMENTOR (5) | Unlimited |

### MOD_ADMIN_LOCKED

When set on an account, all moderation flags (`freeze`, `restrict`, `ban`) can only be changed by IMPLEMENTOR (trust 5). Use `account admin_lock <name>` to protect staff accounts from peer modification.

### Note visibility levels

| Value | Label | Visible to |
|-------|-------|-----------|
| 0 | system | IMPLEMENTOR only |
| 1 | admin | MASTER and above |
| 2 | staff | STORYTELLER and above |
| 3 | owner | Account holder only |

---

## Phase 18 — Delete Character: Implementation Notes

### State flags (not `d->repeat`)

The delete flow uses two dedicated boolean fields on `DESCRIPTOR_DATA` in `twilight.h`:

```c
bool  acct_delete_select;   /* TRUE = awaiting numeric char selection */
bool  acct_delete_confirm;  /* TRUE = awaiting name confirmation */
```

**Do not use `d->repeat` for multi-step hub states.** The input loop in `comm.c` resets `d->repeat = 0` on every new command (line ~1572) before `nanny()` is called, so any state stored there is lost before it can be checked.

Both flags are initialized to `FALSE` in `recycle.c::new_descriptor()`.

### Flow

1. Player presses `[D]` → character list shown → `acct_delete_select = TRUE`
2. Player types a number → selected char name stored in `d->acct_temp_pass` → `acct_delete_select = FALSE`, `acct_delete_confirm = TRUE`
3. Player types the character name → `str_cmp` against `acct_temp_pass` → if match, `soft_deleted_on = time(NULL)`, account saved, slot freed immediately
4. Hub re-displays; `account_char_count()` skips entries where `soft_deleted_on != 0`

Both state checks happen **before** the `switch (UPPER(argument[0]))` in `CON_ACCT_MENU` so character names starting with hub menu letters (C, D, P, Q, etc.) are handled correctly.

---

## Phases 16 & 17 — Boot Scan: Implementation Notes

Both phases are implemented in `account_boot_scan()` in `account.c`. The function runs once at boot (called from `db.c` after areas load) and can also be triggered in-game via `account scan` (STORYTELLER+) or `account purge` (MASTER+).

### Phase 16 — Orphan Scanner

Walks `../player/`, reads each pfile looking for `AcLg` and `AcId` lines. For any pfile that has account linkage fields:
- Verifies the account file `../account/<acct_login>` exists
- If it exists, verifies the character name appears in the `Characters` block
- Logs any mismatch to `../log/account/orphan.log` and the boot log

Skips: hidden files (`.`), files with extensions (`.bak`), and the `deleted`/`backup` subdirectory entries.

### Phase 17 — Hard-Delete Scan

Walks `../account/`, loads each account file. For each soft-deleted character (`soft_deleted_on != 0`) where 60+ days have elapsed (5,184,000 seconds):
- Moves `../player/<CharName>` → `../player/deleted/<CharName>` via `rename()`
- Logs to `../log/account/audit.log` and `../log/account/orphan.log`

Also checks whether the account itself has been soft-deleted for 60+ days:
- Moves `../account/<name>` → `../account/deleted/<name>`

### Key details

- 60 days = 5,184,000 seconds (hardcoded constant)
- Uses `rename()` — atomic on same filesystem, no data loss
- If the pfile is already gone (previously cleaned up), logs `CHAR_HARD_DELETE_SKIP` and continues
- Both `../player/deleted/` and `../account/deleted/` are created at boot by `db.c`

---

## Phase 14 — Unlock Store: Implementation Notes

### Architecture

The store is driven by a single table, `unlock_defs[]` in `account.c`. Each row has:
- `unlock_id` — unique ID stored on the account (`ACCOUNT_UNLOCK.unlock_id`)
- `category` — `UNLOCK_CAT_SLOT`, `UNLOCK_CAT_CLAN`, etc.
- `cost` — point cost (edit and recompile to change)
- `repeatable` — TRUE means the player can buy it more than once (char slots)
- `name` / `description` — shown in the store

**To add a new unlock:** add one row to `unlock_defs[]` in `account.c` and recompile. No other file needs to change.

### Clan gating

Free clans (Camarilla 7 + Caitiff): brujah, gangrel, malkavian, nosferatu, toreador, tremere, ventrue, and "none" (Caitiff index 1). These are NOT in `unlock_defs[]` and are always available.

Locked clans and costs:

| Clan | Cost |
|------|------|
| Assamite | 100 pts |
| Giovanni | 100 pts |
| Settite | 100 pts |
| Lasombra | 200 pts |
| Ravnos | 200 pts |
| Tzimisce | 200 pts |

Clan unlock IDs = `UNLOCK_CLAN_BASE` (100) + clan_table[] index.

The gate is enforced in two places in `CON_GET_NEW_CLAN` in `comm.c`:
1. **Display**: locked clans shown grayed out with "Locked — purchase from [U] Unlocks"
2. **Selection**: if the player types a locked clan name directly, rejected with an error message

`account_clan_requires_unlock(clan_index)` returns TRUE if a clan is in the table. `account_clan_unlocked(acct, clan_index)` returns TRUE for free clans or owned unlocks.

### Hub store flow

`[U]` sets `d->acct_store_browsing = TRUE` and calls `show_account_unlocks(d)`. The pre-switch handler in `CON_ACCT_MENU` intercepts all input while this flag is set (same pattern as the delete flow — avoids the `d->repeat` reset problem). Entering `0` returns to the hub.

### Adding future unlock categories

1. Add a `UNLOCK_CAT_*` constant to `account.h`
2. Add rows to `unlock_defs[]` in `account.c`
3. Add a display section to `show_account_unlocks()` in `account_cmd.c`
4. Add any enforcement gate at the appropriate point (character creation, command use, etc.)

---

## All Phases Complete

The account system is fully implemented. Phase 14 (unlock store) was the final phase.

---

## Manually Linking a Character to an Account

If a character file needs to be manually linked (e.g. for testing or recovery):

**1. Get the account ID:**
```bash
grep "^Id" account/[accountname]
```

**2. Get the character ID:**
```bash
grep "^Id " player/[Charname]
```

**3. Edit the character pfile** — add before the `Pass` line:
```
AcLg accountname~
AcId 1001
```

**4. Edit the account file** — add to the `Characters` block:
```
Characters
  Charname~ char_id 0 0
EndCharacters
```
(The two trailing zeros are `last_played` and `soft_deleted_on`.)

**5.** Run `account scan` in-game or reboot to validate.
