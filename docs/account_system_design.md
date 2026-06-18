# Project Twilight Account System — Implementation Design Document
**Version:** 1.2 — Implementation Ready  
**Status:** All decisions finalized; all implementation gaps resolved

---

## 1. Purpose

This document defines the architecture and implementation plan for the Project Twilight Account System. It is written to be directly implementable against the existing codebase and supersedes the earlier conceptual design document.

The system transitions Project Twilight from ROM's character-centric authentication into an account-centric platform layer supporting centralized authentication, meta-progression, entitlement unlocks, moderation tooling, and long-term extensibility.

This is a pre-production implementation. Migration concerns are limited to development testing continuity.

---

## 2. Design Goals

| Goal | Decision |
|------|----------|
| Account-centric identity | Players authenticate to accounts; characters are owned entities |
| Persistent meta-progression | Points and unlocks persist across all characters on an account |
| Administrative accountability | All sensitive actions fully logged and auditable |
| Extensibility | Table-driven unlocks; no hardcoded bit allocations |
| Flat-file compatibility | New `account/` directory; `player/` pfiles untouched |
| Crash safety | Log before apply; flush before save |

---

## 3. Identity Hierarchy

```
DESCRIPTOR_DATA
      ↓  (d->account)
ACCOUNT_DATA  ←→  account_list  (global linked list)
      ↓  (account->characters linked list)
ACCOUNT_CHARACTER  →  loads from  →  player/[Name]  (CHAR_DATA / PC_DATA)
```

`DESCRIPTOR_DATA` gains one new field: `ACCOUNT_DATA *account`.  
Characters are still stored in `player/` as today. No character data is duplicated into the account file.

**Global list:** `extern ACCOUNT_DATA *account_list` — declared in `account.h`, defined in `account.c`. Accounts are linked into this list when loaded from disk and removed only when the last descriptor referencing them disconnects. This list is iterated by the weekly reward pulse and the simultaneous-login conflict check.

---

## 4. File System Layout

```
ProjectTwilight2/
    account/               ← NEW: one file per account, lowercase name
        nyx                ← account file for "Nyx"
        next_id            ← monotonic account ID counter
        deleted/           ← hard-deleted account files (post 60-day window)
    log/
        account/           ← NEW: append-only account audit and ledger logs
    player/                ← unchanged
        deleted/           ← hard-deleted character pfiles (post 60-day window)
    src/
```

**Account file naming:** lowercase of the account name. `account/nyx`, not `account/Nyx`. Avoids case-collision issues on case-sensitive filesystems.

**Account ID counter:** `account/next_id` — plain text file containing a single integer. Read at boot, incremented and flushed before assigning each new ID.

**Directory creation:** At the top of `boot_db()`, before any account operations:
```c
mkdir("../account",         0755);  /* EEXIST is safe to ignore */
mkdir("../account/deleted", 0755);
mkdir("../log/account",     0755);
mkdir("../player/deleted",  0755);
```
`mkdir()` on an already-existing directory returns `EEXIST` which is silently ignored. Called unconditionally every boot.

---

## 5. Authentication Flow

```
Socket Connect
      ↓
CON_GET_ACCT_NAME
      ↓  (account found)              ↓  (account not found)
CON_GET_ACCT_PASS               prompt "Create new account? (Y/N)"
      ↓  (reset pending)                    ↓  (Y)
CON_ACCT_TOKEN              CON_ACCT_NEW_PASS
      ↓  (token match)             ↓
CON_ACCT_NEW_PASS      CON_ACCT_CONFIRM_PASS
      ↓                       ↓
CON_ACCT_CONFIRM_PASS    CON_ACCT_MENU
      ↓
CON_ACCT_MENU
      ↓
  [hub choice: play / create / unlock / info / ledger / notes / delete / quit]
      ↓
CON_GET_NAME  (existing char states, gated by acct_creating_char flag)
      ↓
CON_PLAYING
```

**Character passwords are deprecated.** The `pcdata->pwd` field and `Pass` pfile keyword are kept read-only during the migration window. Authentication is account-only from first boot.

---

## 6. Password Security

**Library:** `crypt_blowfish` — a self-contained C implementation of bcrypt requiring no external runtime dependency. Drop `crypt_blowfish.c` into `src/` and `crypt_blowfish.h` into `src/include/`. The makefile's `C_FILES = ${wildcard *.c}` glob picks it up automatically — no makefile changes needed.

**Why not OpenSSL:** Adds a package dependency and link flags (`-lssl -lcrypto`) to a project that currently has none. `crypt_blowfish` is a drop-in with no runtime requirements.

**Hash format:** Standard bcrypt `$2b$` prefix, cost factor 12. Stored in the account file only.

**Password validation rules:**
- Minimum 8 characters
- No tilde (`~`) character
- At least one letter and one digit

Implemented in a dedicated `check_account_password(const char *pwd)` function in `account.c`.

---

## 7. Account Name Validation

Account names use a dedicated `check_account_name(const char *name)` function in `account.c`, separate from the existing `check_parse_name()` which is designed for character names.

**Rules:**
- Minimum 3 characters, maximum 12 characters
- Letters and digits only (no spaces, punctuation, or special characters)
- Digits permitted anywhere including the first character
- The following reserved names are blocked (case-insensitive): `admin`, `system`, `staff`, `god`, `implementor`, `null`

---

## 8. ACCOUNT_DATA Structure

```c
/* src/include/account.h */

typedef struct account_data       ACCOUNT_DATA;
typedef struct account_character  ACCOUNT_CHARACTER;
typedef struct account_unlock     ACCOUNT_UNLOCK;
typedef struct account_note       ACCOUNT_NOTE;
typedef struct account_ip_entry   ACCOUNT_IP_ENTRY;

struct account_data
{
    /* Identity */
    long            account_id;
    char           *name;                   /* login name, lowercase */
    char           *password_hash;          /* bcrypt $2b$ hash */
    char           *email;                  /* optional; staff reference only */

    /* Access control */
    long            account_flags;          /* ACCT_* flag bits */
    long            mod_flags;              /* MOD_* flag bits */
    int             trust_ceiling;          /* caps ch->trust for all chars on account */

    /* Point economy — all stored; balance is derived, never stored */
    long            points_earned;          /* monotonically increasing */
    long            points_spent;           /* monotonically increasing */

    /* Timestamps */
    time_t          created_on;
    time_t          last_login;
    time_t          soft_deleted_on;        /* 0 = not deleted; non-zero = deleted */

    /* Password reset */
    char           *reset_token;            /* NULL if no reset pending */
    bool            password_reset_pending;
    time_t          reset_issued_on;        /* token expires after 24 hours */

    /* Weekly activity tracking */
    int             weekly_active_seconds;  /* non-idle seconds this week */
    time_t          weekly_reset_on;        /* timestamp of last weekly reset */

    /* Staff grant tracking — resets monthly */
    int             monthly_grants_used;
    time_t          grants_reset_on;

    /* Dirty flag — set on any in-memory change; cleared after save */
    bool            dirty;

    /* IP history — linked list, capped at 10, most recent first */
    ACCOUNT_IP_ENTRY *ip_log;

    /* Owned characters */
    ACCOUNT_CHARACTER *characters;

    /* Unlocks */
    ACCOUNT_UNLOCK   *unlocks;

    /* Notes */
    ACCOUNT_NOTE     *notes;

    /* List linkage */
    ACCOUNT_DATA     *next;                 /* account_list */
};

struct account_character
{
    char               *char_name;          /* lookup key into player/ */
    long                char_id;            /* permanent ID from ch->id */
    time_t              last_played;
    time_t              soft_deleted_on;    /* 0 = active; non-zero = deleted */
    ACCOUNT_CHARACTER  *next;
};

struct account_unlock
{
    int                 unlock_id;          /* index into unlock_definition_table[] */
    int                 quantity;           /* for repeatable unlocks */
    time_t              purchased_on;
    ACCOUNT_UNLOCK     *next;
};

struct account_note
{
    char               *author;             /* character name or "SYSTEM" */
    char               *body;
    time_t              written_on;
    int                 visibility;         /* NOTE_VIS_* */
    ACCOUNT_NOTE       *next;
};

struct account_ip_entry
{
    char               *ip;
    time_t              seen_on;
    ACCOUNT_IP_ENTRY   *next;
};
```

**Notes on specific fields:**

**`trust_ceiling`:** Enforced in `attach_character()`. If `ch->trust > account->trust_ceiling`, force `ch->trust = account->trust_ceiling` and write an audit log entry. Default value: `MAX_LEVEL` (no restriction). Set and cleared only by `IMPLEMENTOR` trust.

**`soft_deleted_on`:** Replaces a flag bit. `soft_deleted_on != 0` means deleted. The timestamp is also the start of the 60-day retention window. Same pattern used on `account_character`.

**`dirty`:** Set `true` whenever any field changes in memory. `free_account_from_desc()` checks this before deciding whether to save. Cleared to `false` after a successful `save_account()` write.

**`email`:** Stored, optional, not verified. Used as a staff reference for manual recovery only. Not surfaced in any automated flow in V1.

---

## 9. DESCRIPTOR_DATA Changes

```c
/* src/include/twilight.h — add to struct descriptor_data */
ACCOUNT_DATA   *account;            /* NULL until CON_GET_ACCT_PASS succeeds */
char           *acct_temp_pass;     /* temp storage across CON_ACCT_NEW_PASS states */
bool            acct_is_reset;      /* TRUE = password reset flow; FALSE = new account */
bool            acct_creating_char; /* TRUE = creating char from hub, skip pwd states */
```

**`acct_temp_pass`:** Allocated with `str_dup()` in `CON_ACCT_NEW_PASS`, freed in `CON_ACCT_CONFIRM_PASS` after hashing, and freed in `free_account_from_desc()` as a safety net.

**`acct_is_reset`:** Set `true` before entering `CON_ACCT_NEW_PASS` from the token path; `false` from the new-account path. `CON_ACCT_CONFIRM_PASS` checks it to branch between `create_account()` and `update_password_hash()`.

**`acct_creating_char`:** Set `true` when `[C]` is chosen from the hub. In `CON_GET_NAME`, if this flag is set, skip `CON_GET_OLD_PASSWORD`, `CON_GET_NEW_PASSWORD`, and `CON_CONFIRM_NEW_PASSWORD` and jump directly to `CON_GET_NEW_RACE`.

All three new descriptor fields are cleared in `free_account_from_desc()`.

---

## 10. PC_DATA Changes

```c
/* src/include/twilight.h — PC_DATA */
char           *acct_login;     /* account name (field already exists as stub) */
long            acct_id;        /* account_id — add this alongside acct_login */
```

`acct_login` already exists in the struct. `acct_id` is added alongside it.

Pfile keywords:
```
AcLg  accountname~
AcId  1001
```

---

## 11. Flag and Constant Definitions

### Account Flags (`account_flags`)
```c
#define ACCT_NO_MULTI       (1 << 0)    /* flagged for suspected multi-accounting */
/* soft_deleted_on != 0 replaces a ACCT_SOFT_DELETED flag bit — use timestamp directly */
```

### Moderation Flags (`mod_flags`)
```c
#define MOD_BANNED          (1 << 0)    /* account banned entirely */
#define MOD_FROZEN          (1 << 1)    /* can log in to hub but cannot play */
#define MOD_RESTRICTED      (1 << 2)    /* whitelist-only commands in game */
#define MOD_ADMIN_LOCKED    (1 << 3)    /* can only be modified by IMPLEMENTOR */
```

### Note Visibility
```c
#define NOTE_VIS_SYSTEM     0   /* visible to IMPLEMENTOR only */
#define NOTE_VIS_ADMIN      1   /* visible to MASTER and above */
#define NOTE_VIS_STAFF      2   /* visible to STORYTELLER and above */
#define NOTE_VIS_OWNER      3   /* visible to the account holder only */
```

**Visibility access rule** — implemented as a lookup, not a formula:

| Viewer | Sees |
|--------|------|
| Account holder (any trust) | `NOTE_VIS_OWNER` only |
| STORYTELLER (trust 3) | `NOTE_VIS_STAFF` and `NOTE_VIS_OWNER` |
| MASTER (trust 4) | `NOTE_VIS_ADMIN`, `NOTE_VIS_STAFF`, `NOTE_VIS_OWNER` |
| IMPLEMENTOR (trust 5) | All four levels |

The account holder sees their own `NOTE_VIS_OWNER` notes regardless of their character's trust level. The check is `d->account->account_id == target_account_id`, not a trust comparison.

### Character Slot Policy
```c
#define ACCT_BASE_SLOTS     2
#define ACCT_MAX_SLOTS      6
```

### Configurable Economy Constants
```c
/* src/account.c — change and recompile to adjust */
#define ACCT_WEEKLY_SECONDS_REQUIRED    3600  /* 60 minutes of non-idle play */
#define ACCT_WEEKLY_REWARD_POINTS       10
#define ACCT_NEW_ACCOUNT_BONUS          100
#define ACCT_BAN_CONTACT                "Discord: discord.gg/yourserver"
```

---

## 12. CON_ State Constants

```c
/* src/include/twilight.h — replace existing stubs */
#define CON_GET_ACCT_NAME       32
#define CON_GET_ACCT_PASS       33
#define CON_ACCT_MENU           34   /* was CON_ACCT_MENUCHOICE */
#define CON_ACCT_NEW_PASS       35
#define CON_ACCT_CONFIRM_PASS   36
#define CON_ACCT_TOKEN          37

#define MAX_CON_STATE           38
```

---

## 13. nanny() State Flow Detail

**`CON_GET_ACCT_NAME`**
- Empty input → `close_socket(d)`
- Run `check_account_name(argument)` — fail → re-prompt with reason
- Lowercase the input
- Call `load_account(argument)`:
  - Found → `d->account = acct` → go to `CON_GET_ACCT_PASS`
  - Not found → prompt "No account with that name exists. Create one? (Y/N)"
    - Y → go to `CON_ACCT_NEW_PASS` (with `acct_is_reset = false`)
    - N → re-prompt name
    - Empty/other → re-prompt

**`CON_GET_ACCT_PASS`**
- Echo off
- Check `password_reset_pending`: if true and `reset_issued_on + 86400 > current_time`, redirect to `CON_ACCT_TOKEN`; if expired, clear token fields, save, tell player to request a new reset, close socket
- bcrypt verify against `d->account->password_hash`
- Failure → increment attempt counter; at 3 failures close socket
- Check `MOD_BANNED` → show ban message (using `ACCT_BAN_CONTACT`), write audit log `[LOGIN_BANNED]`, close socket
- Check `MOD_FROZEN` → allow through to hub but `[P]` disabled (flag is checked in hub display, not here)
- Check `soft_deleted_on != 0` → compute days remaining → show:
  ```
  This account is scheduled for deletion in N days.
  Restore it? (Y/N):
  ```
  - Y → clear `soft_deleted_on`, set `dirty`, save, audit log `[ACCOUNT_RESTORED]`, continue
  - N → close socket (account stays in soft-delete state)
- Update `last_login`, prepend current IP to `ip_log` (drop oldest if count > 10), set `dirty`, save account
- → `CON_ACCT_MENU`

**`CON_ACCT_TOKEN`**
- Echo off
- Prompt: "A password reset has been issued. Enter your reset token:"
- Constant-time string compare against `reset_token`
- Match:
  - Clear `password_reset_pending`, free and NULL `reset_token`, set `dirty`, save
  - Audit log `[TOKEN_USED]`
  - Set `acct_is_reset = true`
  - → `CON_ACCT_NEW_PASS`
- No match → increment attempt counter; at 3 failures: audit log `[TOKEN_FAILED]`, close socket

**`CON_ACCT_NEW_PASS`**
- Echo off
- Run `check_account_password(argument)` — fail → explain requirement, re-prompt
- `d->acct_temp_pass = str_dup(argument)`
- → `CON_ACCT_CONFIRM_PASS`

**`CON_ACCT_CONFIRM_PASS`**
- Echo off
- Compare `argument` against `d->acct_temp_pass`
- Mismatch → free `acct_temp_pass`, set to NULL, re-prompt from `CON_ACCT_NEW_PASS`
- Match:
  - bcrypt hash `d->acct_temp_pass`
  - Free `d->acct_temp_pass`, set to NULL
  - If `acct_is_reset == false`:
    - Call `create_account()` → `assign_account_id()` → store hash → `account_grant_points(ACCT_NEW_ACCOUNT_BONUS, "New account bonus")` → save → audit log `[ACCOUNT_CREATED]`
  - If `acct_is_reset == true`:
    - Update `d->account->password_hash` → set `dirty` → save → audit log `[PASSWORD_CHANGED]`
  - Clear `acct_is_reset`
  - → `CON_ACCT_MENU`

**`CON_ACCT_MENU`**
- On entry: run weekly reward check (see §19)
- Display hub (see §14)
- Parse single-character choice; route to sub-flow

---

## 14. Account Hub Interface

```
====================================================================
                       PROJECT TWILIGHT
====================================================================
  Account : Nyx                        Points : 85
  Slots   : 2 / 6                      Status : Active
====================================================================
  Characters
  ----------
  [1] Lucien          Last Played: 3 days ago
  [2] Isabella        Last Played: 12 days ago

====================================================================
  [P] Play a Character       [C] Create New Character
  [U] Unlock Features        [A] Account Information
  [L] Point Ledger           [N] Account Notes
  [D] Delete a Character     [Q] Quit
====================================================================
Enter choice:
```

**[P]** If one character: attach directly. If multiple: prompt `Which character? (1-N):` → `attach_character()`.

**[C]** Check `account_char_count(account) < account_max_slots(account)`. If full: "All character slots are in use. Purchase more with [U]." Otherwise: set `acct_creating_char = true` on descriptor, hand off to `CON_GET_NAME`. The `CON_GET_NAME` handler skips all password states when `acct_creating_char` is set.

**[U]** Show full unlock definition table. See §15 for display and purchase flow.

**[A]** Show: account name, ID, email (masked to `a***@***.***` if set, otherwise "(not set)"), created date, last login, IP count (not the IPs themselves).

**[L]** Show point ledger, most recent first, paginated with `page_to_char()`.

**[N]** Show notes where `visibility == NOTE_VIS_OWNER`.

**[D]** Character soft-delete flow:
1. List non-soft-deleted characters with numbers and last-played dates
2. "Which character do you wish to delete? (1-N, or 0 to cancel):"
3. Show: "You have selected [Name]. This cannot be undone by you. Type the character's name to confirm:"
4. Case-insensitive name match
5. On match: `account_character->soft_deleted_on = current_time`, set `dirty`, save account, audit log `[CHAR_SOFT_DELETE]`
6. Pfile is untouched. Recoverable by staff for 60 days.
7. Return to hub display.

**[Q]** `close_socket(d)` — `free_account_from_desc()` is called from `close_socket()`.

**`MOD_FROZEN` display:** Replace `[P]` with `\tD[P] Play a Character (SUSPENDED)\tn`. Selecting P shows: "Your account has been suspended. Contact staff." and returns to menu.

---

## 15. Unlock System

### Definition Table

```c
/* src/tables.c */
struct unlock_definition
{
    int     id;             /* UNLOCK_* constant */
    char   *name;           /* display name */
    char   *description;    /* shown in [U] menu */
    int     category;       /* UNLOCK_CAT_* */
    int     cost;           /* in account points */
    bool    repeatable;     /* can be purchased more than once */
    int     max_count;      /* cap for repeatable; 0 = unlimited */
    int     prerequisite;   /* UNLOCK_* id required first; -1 = none */
};
```

### Unlock IDs and Categories
```c
#define UNLOCK_CHAR_SLOT     1    /* repeatable; max_count = ACCT_MAX_SLOTS - ACCT_BASE_SLOTS */
#define UNLOCK_CLAN_BASE   100    /* one per clan; clan N = UNLOCK_CLAN_BASE + clan_index */

#define UNLOCK_CAT_SLOT      0
#define UNLOCK_CAT_CLAN      1
#define UNLOCK_CAT_MERIT     2    /* future */
#define UNLOCK_CAT_FLAW      3    /* future */
#define UNLOCK_CAT_BLOODLINE 4    /* future */
#define UNLOCK_CAT_PRESTIGE  5    /* future */
#define UNLOCK_CAT_COSMETIC  6    /* future */
```

### Runtime Helpers
```c
int  account_count_unlock(ACCOUNT_DATA *acct, int unlock_id);  /* total quantity owned */
bool account_has_unlock(ACCOUNT_DATA *acct, int unlock_id);    /* quantity >= 1 */
int  account_char_count(ACCOUNT_DATA *acct);                   /* non-deleted chars */
int  account_max_slots(ACCOUNT_DATA *acct);                    /* base + slot unlocks, capped */
```

### [U] Display Format

```
====================================================================
  UNLOCK STORE                                        Balance: 85 pts
====================================================================
  [1] \tWCharacter Slot\tn       +1 character slot (max 6)
      Cost: 25 pts                                    [Affordable]

  [2] \tDGangrel Clan Unlock\tn  Unlocks Gangrel for all characters
      Cost: 50 pts                              [Need 15 more pts]

  [3] \tGBrujah Clan Unlock\tn   Unlocks Brujah for all characters
      Cost: 50 pts                                       [Owned]
====================================================================
  Enter unlock number to purchase, or 0 to cancel:
```

- **Affordable** (`balance >= cost`, not at max): shown in `\tW` (white), `[Affordable]` in `\tG`
- **Unaffordable** (`balance < cost`): shown in `\tD` (dark/dim), `[Need N more pts]` in `\tY`
- **Owned / maxed**: shown in `\tG` (green), `[Owned]` or `[Maxed]` label

### Purchase Flow

After number entry, validate:
- In range
- Not already owned (non-repeatable) or not at `max_count` (repeatable)
- Sufficient balance: `points_earned - points_spent >= cost`

Show confirmation:
```
Purchase [Character Slot] for 25 points?
You will have 60 points remaining. (Y/N):
```

On Y: `account_spend_points()` → add/update `ACCOUNT_UNLOCK` entry → set `dirty` → save → audit log `[UNLOCK_PURCHASE]` → return to updated store display.  
On N: return to store display.

---

## 16. Account File Format

Canonical reference. Stored in `account/[lowercase-name]`.

```
#ACCOUNT
Id        1001
Name      nyx~
PassHash  $2b$12$abcdefghijklmnopqrstuvwxyz012345678901234~
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
  1716800000 192.168.1.2~
EndIpLog

Characters
  Lucien   1042  1716933600  0~
  Isabella 1078  1716800000  0~
EndCharacters
```
*Character line format: `name  char_id  last_played  soft_deleted_on` — fourth field is 0 when not deleted.*

```
Unlocks
  1  1  1716800000~
  101 1 1716750000~
EndUnlocks

Notes
  SYSTEM 0 1716933600 Account created.~
EndNotes

#END
```

**String fields** use `~` terminator consistent with pfile convention.  
**Timestamps** stored as `time_t` (long integer). `0` means "not set."  
**IP log** capped at 10 entries; oldest dropped when a new one is prepended.

---

## 17. Account ID Generation

```c
/* src/account.c */
long next_account_id = 1001;    /* default if next_id file missing */

void load_account_next_id(void)
{
    FILE *fp = fopen("../account/next_id", "r");
    if (fp) { fscanf(fp, "%ld", &next_account_id); fclose(fp); }
    /* if file missing, starts at 1001 and creates the file on first assign */
}

long assign_account_id(void)
{
    long id = next_account_id++;
    FILE *fp = fopen("../account/next_id", "w");
    if (fp) { fprintf(fp, "%ld\n", next_account_id); fflush(fp); fclose(fp); }
    return id;
}
```

Called from `boot_db()` after `mkdir()` calls, before any account operations.

---

## 18. Memory Lifecycle — `load_account()` and `free_account()`

### `load_account(const char *name)`

```c
ACCOUNT_DATA *load_account(const char *name)
{
    ACCOUNT_DATA *acct;

    /* Search in-memory list first — prevents duplicate structs */
    for (acct = account_list; acct; acct = acct->next)
        if (!str_cmp(acct->name, name))
            return acct;

    /* Not in memory — load from disk */
    acct = new_account();           /* recycle.c allocation */
    /* ... parse account file ... */
    acct->next   = account_list;
    account_list = acct;
    return acct;
}
```

### `free_account(ACCOUNT_DATA *acct)`

Frees all heap inside the struct (all linked sub-lists, all strings), unlinks from `account_list`, then calls the recycle.c free function. Analogous to `free_char()`.

### `free_account_from_desc(DESCRIPTOR_DATA *d)`

```c
void free_account_from_desc(DESCRIPTOR_DATA *d)
{
    if (!d->account) return;

    if (d->account->dirty)
        save_account(d->account);

    /* Clear descriptor-side temp fields */
    PURGE_DATA(d->acct_temp_pass);
    d->acct_is_reset      = false;
    d->acct_creating_char = false;

    /* Only free the account struct if no other descriptor references it */
    /* (check descriptor_list for any d2->account == d->account) */
    bool still_referenced = false;
    DESCRIPTOR_DATA *d2;
    for (d2 = descriptor_list; d2; d2 = d2->next)
        if (d2 != d && d2->account == d->account)
            { still_referenced = true; break; }

    if (!still_referenced)
        free_account(d->account);

    d->account = NULL;
}
```

`free_account_from_desc()` is called from `close_socket()` and any other path that tears down a descriptor that may have an account attached.

---

## 19. Recycle.c Integration

Add account types to `recycle.c` and `recycle.h` following the existing pattern:

```c
/* recycle.h */
ACCOUNT_DATA      *new_account(void);
void               free_account(ACCOUNT_DATA *acct);

ACCOUNT_CHARACTER *new_account_character(void);
void               free_account_character(ACCOUNT_CHARACTER *ac);

ACCOUNT_UNLOCK    *new_account_unlock(void);
void               free_account_unlock(ACCOUNT_UNLOCK *au);

ACCOUNT_NOTE      *new_account_note(void);
void               free_account_note(ACCOUNT_NOTE *an);

ACCOUNT_IP_ENTRY  *new_account_ip_entry(void);
void               free_account_ip_entry(ACCOUNT_IP_ENTRY *ai);
```

Each `new_*` function uses the existing free-list pooling pattern. Each `free_*` function walks and frees all heap fields, then returns the struct to the free list. `free_account()` calls `free_account_character()` for each entry in `characters`, `free_account_unlock()` for each in `unlocks`, etc., before freeing itself.

---

## 20. Reconnect Logic

The existing `check_reconnect()` in `comm.c` finds characters by name in `char_list` and is unchanged.

**Account-aware reconnect sequence:**

1. Player authenticates to account → `CON_ACCT_MENU`
2. Player selects character N → `attach_character(d, char_name)` called
3. `attach_character()` scans `char_list` for the name:
   - Found (linkdead): call `check_reconnect(d, char_name, TRUE)` — takes over existing session
   - Not found: load pfile from `player/[Name]`, verify `pcdata->acct_id == d->account->account_id` (ownership check), then enter play
4. `attach_character()` then checks `trust_ceiling` and enforces if needed

**Same-account simultaneous login:**

Before attaching, scan `descriptor_list`:
```c
for (d2 = descriptor_list; d2; d2 = d2->next)
{
    if (d2 != d
        && d2->account
        && d2->account->account_id == d->account->account_id
        && d2->connected == CON_PLAYING)
    {
        /* offer takeover */
    }
}
```
If found: "Your account is already active as [CharName]. Take over that session? (Y/N)". On Y: save the active character, `close_socket(d2)`, then proceed with attach. On N: return to `CON_ACCT_MENU`.

---

## 21. Copyover (Hotboot) Handling

The copyover save loop writes descriptor state to a temp file before `exec()`. Account information must survive across exec.

**Add to the copyover save record** (wherever character name and fd are written):
```c
fprintf(fp, "%d %s %s\n", d->descriptor,
        d->character ? d->character->name : "",
        d->account   ? d->account->name   : "");
```

**In `CON_COPYOVER_RECOVER`** in `nanny()`:
```c
/* After restoring character name, restore account */
if (!IS_NULLSTR(saved_acct_name))
    d->account = load_account(saved_acct_name);
```

`load_account()` searches `account_list` first; subsequent descriptors restoring the same account get the already-loaded pointer. No duplicate structs.

---

## 22. Dual-Reference Ownership

**Account file stores** for each character:
```
name   char_id   last_played   soft_deleted_on
```

**Character pfile stores:**
```
AcLg  accountname~
AcId  1001
```

**Ownership verification** in `attach_character()`: if `pcdata->acct_id != d->account->account_id`, refuse to attach, log `[OWNERSHIP_MISMATCH]`, send player back to hub. This catches file corruption and transfer errors.

---

## 23. Orphan Detection

**At boot:** After accounts are loaded, scan `player/` directory. For each pfile with `AcId` set, verify the account exists and lists that character. For each account, verify each listed character's pfile exists and references back. Discrepancies written to `log/account/orphan.log`.

**On demand:** `account scan` staff command triggers the same check at runtime.

**Repair:** `account repair <char_name> <acct_name>` — MASTER trust. Updates the character pfile and account file to agree. Full audit log entry.

---

## 24. Point Economy

### Storage
```c
long points_earned;   /* monotonically increasing — never decremented */
long points_spent;    /* monotonically increasing — never decremented */
/* balance = points_earned - points_spent; computed at point of use */
```

### Transaction Safety
```
1. Append transaction to log/account/ledger.log  (open append, write, fflush, close)
2. Update points_earned or points_spent in memory
3. Set dirty = true
4. save_account()
```

If the server crashes after step 1 but before step 4, the ledger is the authoritative record. A recovery tool can replay it against the account file.

### Ledger Log Format (`log/account/ledger.log` — append-only)
```
1716933600 GRANT  +100  1001  SYSTEM         New account bonus
1716935000 SPEND  -25   1001  Lucien         Purchased character slot
1716940000 GRANT  +10   1001  SYSTEM         Weekly activity reward
1716941000 GRANT  +20   1001  Storyteller3   Staff event reward
```
Fields: `timestamp  type  amount  account_id  actor  reason`

### API
```c
void account_grant_points(ACCOUNT_DATA *acct, long amount,
                          const char *actor, const char *reason);
void account_spend_points(ACCOUNT_DATA *acct, long amount,
                          const char *actor, const char *reason);
/* Both write to ledger first, then update memory, then set dirty */
```

### Reward Sources

| Source | Amount | Trigger |
|--------|--------|---------|
| New account | +100 | `create_account()` |
| Weekly activity | +10 | 60 min non-idle in past 7 days; checked on hub entry |
| Staff grant | Variable | `account grant` command; capped per month |

### Weekly Activity Threshold

**Accumulation:** Called from `char_update()` (fires once per real second, already in ROM's pulse system). For each character in `char_list`: if `ch->desc && ch->desc->connected == CON_PLAYING && ch->timer == 0 && ch->desc->account`, increment `ch->desc->account->weekly_active_seconds` by 1 and set `dirty = true`.

**Check on hub entry** (`CON_ACCT_MENU`): if `current_time > account->weekly_reset_on + 604800` (7 days):
- If `weekly_active_seconds >= ACCT_WEEKLY_SECONDS_REQUIRED`: call `account_grant_points(+10, "SYSTEM", "Weekly activity reward")`
- Reset `weekly_active_seconds = 0` and update `weekly_reset_on = current_time` regardless of whether threshold was met
- Save

This catch-up-on-login approach means offline players are not penalized for the timing of the server's clock. The ledger entry records exactly when the reward was issued.

### Staff Grant Cap

Monthly cap formula: `ch->trust * 10` points per month per granting character.

```c
/* At grant time: */
if (current_time > target_account->grants_reset_on + 2592000) /* 30 days */
{
    target_account->monthly_grants_used = 0;
    target_account->grants_reset_on = current_time;
}
if (granting_char_account->monthly_grants_used + amount
        > get_trust(ch) * 10)
{
    send_to_char("That would exceed your monthly grant limit.\n\r", ch);
    return;
}
granting_char_account->monthly_grants_used += amount;
```

Trust values: STORYTELLER=3 (30/mo), MASTER=4 (40/mo), IMPLEMENTOR=5 (50/mo).

---

## 25. Audit Logging

**File:** `log/account/audit.log` — append-only, never rewritten or rotated by the game.

**Format:**
```
1716933600 [ACCOUNT_CREATED]      acct=1001 ip=192.168.1.1
1716933700 [LOGIN_BANNED]         acct=1002 ip=192.168.1.5
1716934000 [UNLOCK_PURCHASE]      acct=1001 unlock=CHAR_SLOT qty=1 cost=25 balance=60
1716934100 [TRANSFER]             char=Isabella from_acct=1001 to_acct=1003 actor=Impl1
1716934200 [TRUST_CEILING]        acct=1001 old=5 new=2 actor=Impl1
1716934300 [NOTE_ADD]             acct=1001 vis=STAFF author=Storyteller2
1716934400 [POINT_GRANT]          acct=1001 amount=20 actor=ST2 monthly_used=20/30
1716934500 [PASSWORD_RESET_ISSUED] acct=1001 actor=Master1
1716934600 [TOKEN_USED]           acct=1001
1716934700 [TOKEN_FAILED]         acct=1001 ip=192.168.1.9
1716934800 [CHAR_SOFT_DELETE]     acct=1001 char=Isabella
1716934900 [ACCOUNT_RESTORED]     acct=1001
1716935000 [OWNERSHIP_MISMATCH]   char=Lucien claimed_acct=1001 file_acct=1002
1716935100 [TRUST_ADJUSTED]       acct=1001 char=Lucien old_trust=5 new_trust=2
```

**API:**
```c
void account_audit_log(const char *event, long account_id, const char *detail);
```
Opens in append mode, writes one line, `fflush()`, closes. Never buffered. `detail` is a free-form key=value string.

---

## 26. Moderation

**`MOD_BANNED`:** Checked after successful password auth. Show:
```
This account has been suspended.
Contact staff: [ACCT_BAN_CONTACT]
```
Write audit log `[LOGIN_BANNED]`, close socket.

**`MOD_FROZEN`:** Account reaches `CON_ACCT_MENU` but `[P]` is visually disabled and selecting it returns the frozen message. All other hub options remain available.

**`MOD_RESTRICTED`:** Character enters `CON_PLAYING` normally but command whitelist is enforced. Whitelist defined as a static table in `account.c`:

```c
static const char *restricted_cmd_whitelist[] =
{
    "say", "quit", "look",
    "north", "south", "east", "west", "up", "down",
    "help", "who", "account",
    NULL
};

bool account_cmd_is_whitelisted(const char *cmd)
{
    int i;
    for (i = 0; restricted_cmd_whitelist[i]; i++)
        if (!str_prefix(cmd, restricted_cmd_whitelist[i]))
            return true;
    return false;
}
```

Hook in `interpret()` after trust and race checks:
```c
if (ch->desc
    && ch->desc->account
    && IS_SET(ch->desc->account->mod_flags, MOD_RESTRICTED)
    && !account_cmd_is_whitelisted(command))
{
    send_to_char("Your account is restricted. Contact staff.\n\r", ch);
    return;
}
```

**`MOD_ADMIN_LOCKED`:** Any attempt to modify `mod_flags` or `trust_ceiling` on this account by a character with trust < IMPLEMENTOR fails silently with "You cannot modify that account."

---

## 27. Password Recovery

### Staff-Issued Reset

**Command:** `account reset password <acct_name>` — requires MASTER trust.

**Process:**
1. Generate 12-character token:
```c
static void generate_reset_token(char *buf, int len)
{
    static const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    /* omits I, O, 0, 1 — visually ambiguous characters */
    int i;
    for (i = 0; i < len - 1; i++)
        buf[i] = charset[rand() % (sizeof(charset) - 1)];
    buf[len - 1] = '\0';
}
/* generate_reset_token(buf, 13) → 12-char token */
```
2. Store in `account->reset_token` (plaintext, ephemeral, single-use)
3. Set `password_reset_pending = true`, `reset_issued_on = current_time`
4. Set `dirty`, save immediately
5. Audit log `[PASSWORD_RESET_ISSUED]`
6. Display token to issuing staff member — staff delivers out-of-band (tell, Discord)

**Token expiry:** 24 hours (`reset_issued_on + 86400`). Expired tokens are refused and cleared at `CON_GET_ACCT_PASS` — player must request a new reset.

### Console-Level Recovery

Offline script `scripts/acct_reset.sh` — reads and writes account files directly while the MUD is not running. For IMPLEMENTOR-level account compromise only. Requires shell access.

---

## 28. Character Transfer

**Command:** `account transfer <char_name> <from_acct> <to_acct>` — requires MASTER trust.

Steps (all-or-nothing; if any step fails, abort and log):
1. Verify both accounts exist and neither is `MOD_BANNED`
2. Verify destination account has an available slot
3. Verify `from_acct` actually owns the character
4. Remove `ACCOUNT_CHARACTER` entry from `from_acct->characters`
5. Add new `ACCOUNT_CHARACTER` entry to `to_acct->characters`
6. Load character pfile, update `AcLg` and `AcId`, save pfile
7. Save both account files
8. Audit log `[TRANSFER]`

---

## 29. Data Retention

| State | Trigger | Duration | Action |
|-------|---------|----------|--------|
| Active | — | — | Normal |
| Soft-deleted (account) | Player request or staff action | 60 days | `soft_deleted_on = current_time`; account hidden from normal login but file intact |
| Soft-deleted (character) | Player `[D]` or staff action | 60 days | `account_character->soft_deleted_on = current_time`; hidden from hub display |
| Hard-deleted | Boot scan, 60 days after soft-delete | — | Files moved to `deleted/` directories |

**Hard-delete scan** runs in `boot_db()` after `load_account_next_id()`:
- Walk `account/` directory; for each file, check `soft_deleted_on`. If `current_time - soft_deleted_on > 5184000`, rename to `account/deleted/`.
- For each `ACCOUNT_CHARACTER` with `soft_deleted_on` in window: when account is loaded, check each character entry. If past window: rename pfile to `player/deleted/`, remove entry from account, save account.

Staff command `account purge` triggers the same scan without a reboot.

Account can be restored by staff within 60 days: clear `soft_deleted_on`, save, audit log `[ACCOUNT_RESTORED]`.

---

## 30. Migration Flow

For each character without `AcLg`/`AcId` in their pfile:

1. Character authenticates via legacy `CON_GET_NAME` → `CON_GET_OLD_PASSWORD` (still functional during migration window)
2. After successful password auth, detect `pcdata->acct_login == NULL`
3. Send:
   ```
   Welcome to the new account system.
   All characters now require an account to play.
   Please create a new account or link to an existing one.

   Enter an account name (or type the name of an existing account):
   ```
4. Redirect to `CON_GET_ACCT_NAME` with a flag indicating migration context
5. After account creation or login: add character to account (`account_char_count` must have room), write `AcLg`/`AcId` to pfile, save both
6. Enter `CON_ACCT_MENU`

Once migration is complete: remove legacy `CON_GET_OLD_PASSWORD` character-password path and the `Pass` pfile keyword from new writes.

---

## 31. In-Game `account` Command

`do_account()` operates in two contexts: from `CON_ACCT_MENU` and from `CON_PLAYING`. Both contexts use `d->account`.

Guard at the top:
```c
if (!ch->desc || !ch->desc->account)
{
    send_to_char("No account is attached to this session.\n\r", ch);
    return;
}
```

**Subcommands available in-game (read-only, own account):**

| Subcommand | Effect |
|------------|--------|
| `account info` | Show name, ID, created date, last login, email status |
| `account points` | Show balance, earned, spent |
| `account ledger` | Paginated ledger history |
| `account unlocks` | List owned unlocks |

All write operations (purchase, delete, etc.) are hub-only.

### Staff Subcommands and Trust Requirements

| Command | Trust Required |
|---------|---------------|
| `account info <name>` | STORYTELLER (3) |
| `account note add <name> <vis> <text>` | STORYTELLER (3) |
| `account note list <name>` | STORYTELLER (3) |
| `account grant <name> <amount> <reason>` | STORYTELLER (3) |
| `account freeze <name>` / `unfreeze` | STORYTELLER (3) |
| `account restrict <name>` / `unrestrict` | STORYTELLER (3) |
| `account ban <name> <reason>` | MASTER (4) |
| `account unban <name>` | MASTER (4) |
| `account transfer <char> <from> <to>` | MASTER (4) |
| `account reset password <name>` | MASTER (4) |
| `account scan` | MASTER (4) |
| `account purge` | MASTER (4) |
| `account unlock grant <name> <unlock>` | MASTER (4) |
| `account trust_ceiling <name> <level>` | IMPLEMENTOR (5) |
| `account admin_lock <name>` / `unlock` | IMPLEMENTOR (5) |

---

## 32. New Source Files

| File | Contents |
|------|----------|
| `src/account.c` | All account lifecycle functions, point economy, weekly pulse hook, whitelist, audit log, `assign_account_id()`, `generate_reset_token()` |
| `src/account_cmd.c` | `do_account()` dispatcher and all subcommand handlers |
| `src/crypt_blowfish.c` | bcrypt implementation (drop-in) |
| `src/include/account.h` | All `ACCOUNT_DATA` typedefs, `extern account_list`, constants, function declarations |
| `src/include/crypt_blowfish.h` | bcrypt header |

**Existing files modified:**

| File | Change |
|------|--------|
| `src/include/twilight.h` | Typedefs (forward-declared before first use), `CON_ACCT_*` constants, `MAX_CON_STATE=38`, four new `DESCRIPTOR_DATA` fields, `acct_id` in `PC_DATA` |
| `src/comm.c` | Replace three nanny stubs with full implementations; add `CON_ACCT_NEW_PASS`, `CON_ACCT_CONFIRM_PASS`, `CON_ACCT_TOKEN` cases; update `close_socket()` to call `free_account_from_desc()`; add copyover account save/restore |
| `src/save.c` | Add `AcLg`/`AcId` read/write in `fwrite_char()`/`fread_char()` |
| `src/recycle.c` / `recycle.h` | Add `new_account()`, `free_account()` and four sub-type pairs |
| `src/db.c` | `mkdir()` calls + `load_account_next_id()` + hard-delete boot scan in `boot_db()` |
| `src/interp.c` | Register `do_account` with trust level 0 (gated internally) |
| `src/interp.h` | Declare `do_account` |
| `src/makefile` | No changes required |

---

## 33. Build Order (Implementation Phases)

| Phase | Work | Dependency |
|-------|------|------------|
| 1 | Forward typedefs + `CON_ACCT_*` constants + `MAX_CON_STATE=38` in `twilight.h`; four new `DESCRIPTOR_DATA` fields; `acct_id` in `PC_DATA` | none |
| 2 | `account.h` — all constants, flag defines, function declarations | Phase 1 |
| 3 | `recycle.c/h` — `new_account()`, `free_account()`, and four sub-type pairs | Phase 2 |
| 4 | `account.c` — `load_account()`, `save_account()`, `create_account()`, `free_account_from_desc()`, `assign_account_id()`, `load_account_next_id()` | Phase 3 |
| 5 | `db.c` — `mkdir()` calls + `load_account_next_id()` boot hook + hard-delete scan | Phase 4 |
| 6 | `save.c` — `AcLg`/`AcId` read/write | Phase 1 |
| 7 | `crypt_blowfish.c/h` drop-in + `check_account_password()` + `generate_reset_token()` | Phase 1 |
| 8 | `comm.c` — `free_account_from_desc()` call in `close_socket()`; copyover save/restore | Phase 4 |
| 9 | `comm.c` — Replace nanny stubs: `CON_GET_ACCT_NAME`, `CON_GET_ACCT_PASS`, `CON_ACCT_TOKEN`, `CON_ACCT_NEW_PASS`, `CON_ACCT_CONFIRM_PASS` | Phases 4, 7, 8 |
| 10 | `comm.c` — `CON_ACCT_MENU` hub display + all choice routing including `[D]` soft-delete | Phase 9 |
| 11 | `attach_character()` / `detach_character()` + reconnect integration + same-account takeover | Phases 4, 10 |
| 12 | Migration flow — detect unlinked characters on legacy login, redirect | Phases 9–11 |
| 13 | `account.c` — `account_audit_log()` + `account_grant_points()` + `account_spend_points()` + ledger log | Phase 4 |
| 14 | `account.c` — weekly activity accumulation hook in `char_update()` + on-hub-entry reward check | Phase 13 |
| 15 | `account.c` — `MOD_RESTRICTED` whitelist + `account_cmd_is_whitelisted()` + hook in `interpret()` | Phase 11 |
| 16 | Unlock system — definition table in `tables.c`; `account_has_unlock()`, `account_count_unlock()`; `[U]` display and purchase flow | Phase 13 |
| 17 | `account_cmd.c` — `do_account()` dispatcher + all player and staff subcommands | Phases 13–16 |
| 18 | `interp.c/h` — register `do_account` | Phase 17 |
| 19 | Orphan scanner + `account scan` + `account repair` + `account purge` | Phase 12 |

Phase 12 (migration) before Phase 13 onward — linked characters must exist before the point economy runs against them.

---

## 34. Decisions Record

| # | Question | Decision | Section |
|---|----------|----------|---------|
| Q1 | Minimum password length | 8 characters | §6 |
| Q2 | Soft-deleted account login | Self-service Y/N restore prompt | §13 |
| Q3 | Unlock store presentation | Show all; gray out unaffordable | §15 |
| Q4 | Character deletion from hub | Player self-service; name-confirmation required | §14 |
| Q5 | Weekly activity threshold | 60 min non-idle; configurable `#define`; catch-up on login | §24 |
| Q6 | MOD_RESTRICTED behavior | Static whitelist in `account.c`; recompile to change | §26 |
| Q7 | Password reset token | Random 12-char token; `CON_ACCT_TOKEN` state; 24h expiry | §13, §27 |
| G1 | `account_list` global | Yes; `load_account()` checks list before disk | §3, §18 |
| G2 | Temp password on descriptor | `acct_temp_pass` field on `DESCRIPTOR_DATA` | §9 |
| G3 | Distinguishing reset vs new-account in confirm state | `acct_is_reset` bool on `DESCRIPTOR_DATA` | §9 |
| G4 | `[C]` bypassing character password states | `acct_creating_char` bool on `DESCRIPTOR_DATA` | §9 |
| G5 | Pulse accumulation math | From `char_update()` (1/sec), increment by 1 | §24 |
| G6 | Simultaneous load race | `load_account()` searches list first | §18 |
| G7 | Copyover handling | Save/restore account name in copyover temp file | §21 |
| G8 | `free_account` contract | Explicit; reference-counted before freeing | §18 |
| G9 | Weekly reward timing | Catch-up on hub entry, not server-side pulse | §24 |
| G10 | `soft_deleted_on` on characters | Added to `account_character` struct | §8 |
| G11 | Redundant soft-delete flag | `ACCT_SOFT_DELETED` removed; use `soft_deleted_on != 0` | §8, §11 |
| G12 | Hard-delete trigger | Boot scan in `boot_db()` + `account purge` command | §29 |
| G13 | Directory creation | `mkdir()` calls unconditionally at boot start | §4 |
| G14 | Recycle.c pattern | All account types use `new_*/free_*` pool pattern | §19 |
| G15 | File format completeness | `WeekSec`, `WeekRst`, character `soft_deleted_on` all in §16 | §16 |
| G16 | `account_char_count` / `account_max_slots` | Runtime functions declared in `account.h` | §15 |
| G17 | Note visibility direction | Lookup table; OWNER = account holder only | §11 |
| G18 | `[U]` purchase interaction | Numbered selection + Y/N confirmation | §15 |
| G19 | Staff command trust levels | Full table defined | §31 |
| G20 | In-game `account` command | Read-only in-game; writes hub-only | §31 |
| G21 | Account name validation | Dedicated `check_account_name()`; 3–12 chars; reserved word list | §7 |
| G22 | Ban message content | Fixed string + `ACCT_BAN_CONTACT` define | §26 |

---

## 35. Not In This Document

The following are intentionally deferred and not part of V1 implementation:

- Web account management interface
- Discord integration
- Email verification or self-service password recovery
- Achievement system
- External moderation tooling
- Analytics
- Merits, flaws, bloodlines, prestige unlocks (table infrastructure supports them; definitions and UI deferred)
