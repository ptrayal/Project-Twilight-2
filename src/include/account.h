/***************************************************************************
 * Much of the code is the original work of Peter Fitzgerald who turned    *
 * it over to Brandon Morrison who has adopted and improved the code.      *
 * Copyright (C) 2012 - 2024                                               *
 **************************************************************************/

#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <time.h>

// Do not include <stdbool.h> because twilight.h defines bool as unsigned char
// and must be included before this header everywhere it is used.
// #include <stdbool.h>

/*
 * Forward declarations — full DESCRIPTOR_DATA/CHAR_DATA types come from twilight.h.
 * account.h is included after twilight.h everywhere it is needed.
 */

/* =========================================================================
 * Struct definitions
 * ========================================================================= */

struct account_ip_entry
{
    char               *ip;
    time_t              seen_on;
    ACCOUNT_IP_ENTRY   *next;
};

struct account_character
{
    char               *char_name;      /* lookup key into player/ */
    long                char_id;        /* permanent ID from ch->id */
    time_t              last_played;
    time_t              soft_deleted_on; /* 0 = active; non-zero = soft-deleted */
    ACCOUNT_CHARACTER  *next;
};

struct account_unlock
{
    int                 unlock_id;      /* index into unlock_definition_table[] */
    int                 quantity;       /* for repeatable unlocks */
    time_t              purchased_on;
    ACCOUNT_UNLOCK     *next;
};

struct account_note
{
    char               *author;         /* character name or "SYSTEM" */
    char               *body;
    time_t              written_on;
    int                 visibility;     /* NOTE_VIS_* */
    ACCOUNT_NOTE       *next;
};

struct account_data
{
    /* Identity */
    long                account_id;
    char               *name;               /* login name, lowercase */
    char               *password_hash;      /* bcrypt $2b$ hash */
    char               *email;              /* optional; staff reference only */

    /* Access control */
    long                account_flags;      /* ACCT_* bits */
    long                mod_flags;          /* MOD_* bits */
    int                 trust_ceiling;      /* caps ch->trust for all chars on account */

    /* Point economy — balance = earned - spent; never store derived value */
    long                points_earned;
    long                points_spent;

    /* Timestamps */
    time_t              created_on;
    time_t              last_login;
    time_t              soft_deleted_on;    /* 0 = active; non-zero = soft-deleted */

    /* Password reset */
    char               *reset_token;        /* NULL if no reset pending */
    bool                password_reset_pending;
    time_t              reset_issued_on;    /* token expires after 24 hours */

    /* Weekly activity tracking */
    int                 weekly_active_seconds;
    time_t              weekly_reset_on;

    /* Staff grant tracking — resets monthly */
    int                 monthly_grants_used;
    time_t              grants_reset_on;

    /* Dirty flag — set on any in-memory change; cleared after save */
    bool                dirty;

    /* Sub-lists */
    ACCOUNT_IP_ENTRY   *ip_log;             /* capped at ACCT_IP_LOG_MAX, most recent first */
    ACCOUNT_CHARACTER  *characters;
    ACCOUNT_UNLOCK     *unlocks;
    ACCOUNT_NOTE       *notes;

    /* Global list linkage */
    ACCOUNT_DATA       *next;
};

/* =========================================================================
 * account_flags bits
 * ========================================================================= */
#define ACCT_NO_MULTI           (1 << 0)    /* flagged for suspected multi-accounting */

/* =========================================================================
 * mod_flags bits
 * ========================================================================= */
#define MOD_BANNED              (1 << 0)    /* account banned entirely */
#define MOD_FROZEN              (1 << 1)    /* can reach hub but cannot play */
#define MOD_RESTRICTED          (1 << 2)    /* whitelist-only commands in-game */
#define MOD_ADMIN_LOCKED        (1 << 3)    /* modifiable only by IMPLEMENTOR */

/* =========================================================================
 * Note visibility levels
 * ========================================================================= */
#define NOTE_VIS_SYSTEM         0   /* IMPLEMENTOR only */
#define NOTE_VIS_ADMIN          1   /* MASTER and above */
#define NOTE_VIS_STAFF          2   /* STORYTELLER and above */
#define NOTE_VIS_OWNER          3   /* account holder only */

/* =========================================================================
 * Character slot policy
 * ========================================================================= */
#define ACCT_BASE_SLOTS         2
#define ACCT_MAX_SLOTS          6

/* =========================================================================
 * Unlock IDs and categories
 * ========================================================================= */
#define UNLOCK_CHAR_SLOT        1
#define UNLOCK_CLAN_BASE        100     /* Clan unlock IDs: 100 + clan_table index */

#define UNLOCK_CAT_SLOT         0
#define UNLOCK_CAT_CLAN         1
#define UNLOCK_CAT_MERIT        2   /* future */
#define UNLOCK_CAT_FLAW         3   /* future */
#define UNLOCK_CAT_BLOODLINE    4   /* future */
#define UNLOCK_CAT_PRESTIGE     5   /* future */
#define UNLOCK_CAT_COSMETIC     6   /* future */

/* =========================================================================
 * Unlock definition table — one entry per purchasable item.
 * To add a new unlock: add a row to unlock_defs[] in account.c, no other
 * file needs to change.  Clan unlocks use unlock_id = UNLOCK_CLAN_BASE + clan index.
 * ========================================================================= */
struct unlock_def
{
    int         unlock_id;      /* matches ACCOUNT_UNLOCK.unlock_id */
    int         category;       /* UNLOCK_CAT_* */
    int         cost;           /* point cost */
    bool        repeatable;     /* TRUE = can buy more than once (e.g. char slots) */
    const char *name;           /* short display name */
    const char *description;    /* one-line description shown in store */
};
typedef struct unlock_def UNLOCK_DEF;

extern const UNLOCK_DEF unlock_defs[];  /* defined in account.c, NULL-terminated */
extern const int         num_unlock_defs;

/* =========================================================================
 * Configurable economy constants
 * Edit these values and recompile to adjust game balance.
 * ========================================================================= */
#define ACCT_WEEKLY_SECONDS_REQUIRED    3600    /* 60 minutes of non-idle play */
#define ACCT_WEEKLY_REWARD_POINTS       10
#define ACCT_NEW_ACCOUNT_BONUS          100
#define ACCT_IP_LOG_MAX                 10
#define ACCT_RESET_TOKEN_EXPIRY         86400   /* 24 hours in seconds */
#define ACCT_RETENTION_DAYS             60      /* soft-delete retention window */
#define ACCT_RETENTION_SECONDS          (ACCT_RETENTION_DAYS * 86400)
#define ACCT_GRANT_RESET_SECONDS        2592000 /* 30 days */
#define ACCT_WEEKLY_RESET_SECONDS       604800  /* 7 days */

/* Contact string shown in ban messages — update to match your community */
#define ACCT_BAN_CONTACT                "Contact staff for assistance."

/* =========================================================================
 * Global list
 * ========================================================================= */
extern ACCOUNT_DATA *account_list;
extern long          next_account_id;

/* =========================================================================
 * Lifecycle functions (account.c)
 * ========================================================================= */
ACCOUNT_DATA      *load_account( const char *name );
void               save_account( ACCOUNT_DATA *acct );
ACCOUNT_DATA      *create_account( const char *name, const char *password_hash );
void               free_account( ACCOUNT_DATA *acct );
void               free_account_from_desc( DESCRIPTOR_DATA *d );
void               detach_account( DESCRIPTOR_DATA *d );

long               assign_account_id( void );
void               load_account_next_id( void );

/* =========================================================================
 * Character attach/detach (account.c)
 * ========================================================================= */
void               attach_character( DESCRIPTOR_DATA *d, const char *char_name );
void               detach_character( DESCRIPTOR_DATA *d );

/* =========================================================================
 * Query helpers (account.c)
 * ========================================================================= */
int                account_char_count( ACCOUNT_DATA *acct );
int                account_max_slots( ACCOUNT_DATA *acct );
int                account_count_unlock( ACCOUNT_DATA *acct, int unlock_id );
bool               account_has_unlock( ACCOUNT_DATA *acct, int unlock_id );
ACCOUNT_DATA      *account_by_id( long account_id );

/* =========================================================================
 * Point economy (account.c)
 * ========================================================================= */
void               account_grant_points( ACCOUNT_DATA *acct, long amount,
                       const char *actor, const char *reason );
void               account_spend_points( ACCOUNT_DATA *acct, long amount,
                       const char *actor, const char *reason );
void               account_check_weekly_reward( ACCOUNT_DATA *acct );

/* =========================================================================
 * Unlock store (account.c)
 * ========================================================================= */
const UNLOCK_DEF  *unlock_def_by_id( int unlock_id );
bool               account_can_purchase( ACCOUNT_DATA *acct, int unlock_id );
bool               account_purchase_unlock( ACCOUNT_DATA *acct, int unlock_id );
bool               account_clan_requires_unlock( int clan_index );
bool               account_clan_unlocked( ACCOUNT_DATA *acct, int clan_index );

/* =========================================================================
 * Audit logging (account.c)
 * ========================================================================= */
void               account_audit_log( const char *event, long account_id,
                       const char *detail );

/* =========================================================================
 * Moderation (account.c)
 * ========================================================================= */
bool               account_cmd_is_whitelisted( const char *cmd );

/* =========================================================================
 * Password / token (account.c)
 * ========================================================================= */
bool               check_account_name( const char *name );
bool               check_account_password( const char *pwd );
char              *account_hash_password( const char *plaintext );
bool               account_verify_password( const char *plaintext, const char *hash );
void               generate_reset_token( char *buf, int len );
bool               account_token_verify( ACCOUNT_DATA *acct, const char *token );

/* =========================================================================
 * Boot scan / orphan detection (account.c)
 * ========================================================================= */
void               account_boot_scan( void );

/* =========================================================================
 * Hub display (account_cmd.c)
 * ========================================================================= */
void               show_account_hub( DESCRIPTOR_DATA *d );
void               show_account_unlocks( DESCRIPTOR_DATA *d );

#endif /* ACCOUNT_H */
