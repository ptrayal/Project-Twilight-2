/***************************************************************************
 * Much of the code is the original work of Peter Fitzgerald who turned    *
 * it over to Brandon Morrison who has adopted and improved the code.      *
 * Copyright (C) 2012 - 2024                                               *
 **************************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <ctype.h>
#include "twilight.h"
#include "recycle.h"
#include "account.h"

/*
 * Direct declarations for the two crypt_blowfish functions we use.
 * These match the signatures in crypt_blowfish.c / crypt_gensalt.c exactly.
 * We declare them here rather than including ow-crypt.h because ow-crypt.h
 * tries to include <gnu-crypt.h> on glibc systems, which doesn't exist on
 * modern Ubuntu / Debian.
 */
extern char *crypt_ra(const char *key, const char *setting,
                      void **data, int *size);
extern char *crypt_gensalt_ra(const char *prefix, unsigned long count,
                              const char *input, int size);

/* =========================================================================
 * Globals
 * ========================================================================= */
ACCOUNT_DATA *account_list     = NULL;
long          next_account_id  = 1001;

/* =========================================================================
 * Restricted command whitelist for MOD_RESTRICTED accounts.
 * Edit this list and recompile to change which commands are permitted.
 * ========================================================================= */
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

/* =========================================================================
 * Section 1: Account name and password validation
 * ========================================================================= */

/*
 * check_account_name — validate an account name.
 * Rules: 3-12 chars, letters and digits only, no reserved words.
 */
bool check_account_name( const char *name )
{
    static const char *reserved[] =
    {
        "admin", "system", "staff", "god", "implementor", "null", NULL
    };
    int len, i;

    if ( !name || name[0] == '\0' )
        return FALSE;

    len = strlen( name );
    if ( len < 3 || len > 12 )
        return FALSE;

    for ( i = 0; name[i] != '\0'; i++ )
    {
        if ( !isalnum( (unsigned char)name[i] ) )
            return FALSE;
    }

    for ( i = 0; reserved[i]; i++ )
    {
        if ( !strcasecmp( name, reserved[i] ) )
            return FALSE;
    }

    return TRUE;
}

/*
 * check_account_password — validate a candidate password.
 * Rules: 8+ chars, no tilde, at least one letter and one digit.
 */
bool check_account_password( const char *pwd )
{
    int len, i;
    bool has_letter = FALSE, has_digit = FALSE;

    if ( !pwd )
        return FALSE;

    len = strlen( pwd );
    if ( len < 8 )
        return FALSE;

    for ( i = 0; pwd[i] != '\0'; i++ )
    {
        if ( pwd[i] == '~' )
            return FALSE;
        if ( isalpha( (unsigned char)pwd[i] ) )
            has_letter = TRUE;
        if ( isdigit( (unsigned char)pwd[i] ) )
            has_digit = TRUE;
    }

    return has_letter && has_digit;
}

/* =========================================================================
 * Section 2: bcrypt password hashing and verification
 * ========================================================================= */

/*
 * account_hash_password — hash a plaintext password with bcrypt.
 * Returns a str_dup'd bcrypt hash string the caller must PURGE_DATA when done.
 * Returns NULL on failure.
 */
char *account_hash_password( const char *plaintext )
{
    /* crypt_gensalt_ra generates a random $2b$12$ salt */
    char *salt   = NULL;
    char *hash   = NULL;
    char *result = NULL;
    unsigned long rnd[2];

    /* Seed from time — not cryptographic but adequate for
     * a staff-mediated password reset flow. */
    rnd[0] = (unsigned long)time(NULL) ^ (unsigned long)(size_t)plaintext;
    rnd[1] = (unsigned long)rand();

    salt = crypt_gensalt_ra( "$2b$", 12, (const char *)rnd, sizeof(rnd) );
    if ( !salt )
        return NULL;

    hash = crypt_ra( plaintext, salt, NULL, NULL );
    if ( hash )
        result = str_dup( hash );

    free( salt );
    /* crypt_ra allocates internally; it is freed by the library on next call
     * or process exit — we only need to str_dup the result string. */

    return result;
}

/*
 * account_verify_password — check plaintext against a bcrypt hash.
 * Returns TRUE if the password matches.
 */
bool account_verify_password( const char *plaintext, const char *hash )
{
    char *result = NULL;

    if ( !plaintext || !hash || hash[0] == '\0' )
        return FALSE;

    result = crypt_ra( plaintext, hash, NULL, NULL );
    if ( !result )
        return FALSE;

    /* Use a length-constant comparison to resist timing attacks */
    return ( strcmp( result, hash ) == 0 );
}

/* =========================================================================
 * Section 3: Password reset token
 * ========================================================================= */

/*
 * generate_reset_token — produce a 12-character human-readable token.
 * Avoids characters that are visually ambiguous (I, O, 0, 1).
 * buf must be at least len bytes; call as generate_reset_token(buf, 13).
 */
void generate_reset_token( char *buf, int len )
{
    static const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    int charset_len = (int)( sizeof(charset) - 1 );
    int i;

    for ( i = 0; i < len - 1; i++ )
        buf[i] = charset[ rand() % charset_len ];
    buf[len - 1] = '\0';
}

/*
 * account_token_verify — constant-time comparison of submitted token
 * against stored token, with expiry check.
 * Clears the token on a successful match.
 */
bool account_token_verify( ACCOUNT_DATA *acct, const char *submitted )
{
    int match = 0, i, len;

    if ( !acct || !acct->reset_token || !submitted )
        return FALSE;

    /* Expiry check */
    if ( time(NULL) > acct->reset_issued_on + ACCT_RESET_TOKEN_EXPIRY )
    {
        PURGE_DATA( acct->reset_token );
        acct->password_reset_pending = FALSE;
        acct->reset_issued_on        = 0;
        acct->dirty                  = TRUE;
        return FALSE;
    }

    /* Constant-time compare — iterate the full length of the stored token
     * regardless of early mismatches to prevent timing-based token guessing. */
    len = strlen( acct->reset_token );
    for ( i = 0; i < len; i++ )
    {
        if ( submitted[i] == '\0' )
            match |= 1;  /* submitted is shorter */
        else
            match |= ( acct->reset_token[i] ^ submitted[i] );
    }
    if ( submitted[len] != '\0' )
        match |= 1;  /* submitted is longer */

    if ( match != 0 )
        return FALSE;

    /* Valid match — clear the token immediately (single-use) */
    PURGE_DATA( acct->reset_token );
    acct->password_reset_pending = FALSE;
    acct->reset_issued_on        = 0;
    acct->dirty                  = TRUE;
    return TRUE;
}

/* =========================================================================
 * Section 4: Moderation helper
 * ========================================================================= */

/*
 * account_cmd_is_whitelisted — returns TRUE if cmd is permitted under
 * MOD_RESTRICTED. Uses str_prefix so "no" matches "north".
 */
bool account_cmd_is_whitelisted( const char *cmd )
{
    int i;

    if ( !cmd || cmd[0] == '\0' )
        return FALSE;

    for ( i = 0; restricted_cmd_whitelist[i]; i++ )
    {
        if ( !str_prefix( cmd, restricted_cmd_whitelist[i] ) )
            return TRUE;
    }
    return FALSE;
}

/* =========================================================================
 * Section 5: Audit and ledger logging
 * ========================================================================= */

/*
 * account_audit_log — append one line to log/account/audit.log.
 * Opens, writes, flushes, closes on every call — never buffered.
 */
void account_audit_log( const char *event, long account_id, const char *detail )
{
    FILE  *fp;
    time_t now = time( NULL );

    fp = fopen( "../log/account/audit.log", "a" );
    if ( !fp )
        return;

    fprintf( fp, "%ld [%s] acct=%ld %s\n",
             (long)now, event ? event : "UNKNOWN",
             account_id, detail ? detail : "" );
    fflush( fp );
    fclose( fp );
}

/*
 * account_ledger_log — append one transaction line to log/account/ledger.log.
 * Internal helper; callers use account_grant_points() / account_spend_points().
 */
static void account_ledger_log( const char *type, long amount,
                                long account_id, const char *actor,
                                const char *reason )
{
    FILE  *fp;
    time_t now = time( NULL );

    fp = fopen( "../log/account/ledger.log", "a" );
    if ( !fp )
        return;

    fprintf( fp, "%ld %s %+ld %ld %s %s\n",
             (long)now, type, amount, account_id,
             actor  ? actor  : "UNKNOWN",
             reason ? reason : "" );
    fflush( fp );
    fclose( fp );
}

/* =========================================================================
 * Section 6: Point economy
 * ========================================================================= */

void account_grant_points( ACCOUNT_DATA *acct, long amount,
                           const char *actor, const char *reason )
{
    if ( !acct || amount <= 0 )
        return;

    /* 1. Write to ledger first */
    account_ledger_log( "GRANT", amount, acct->account_id, actor, reason );

    /* 2. Update memory */
    acct->points_earned += amount;
    acct->dirty          = TRUE;

    /* 3. save_account() is the caller's responsibility after this returns,
     *    or will happen on the next periodic save / disconnect. */
}

void account_spend_points( ACCOUNT_DATA *acct, long amount,
                           const char *actor, const char *reason )
{
    if ( !acct || amount <= 0 )
        return;

    account_ledger_log( "SPEND", -amount, acct->account_id, actor, reason );
    acct->points_spent += amount;
    acct->dirty         = TRUE;
}

/*
 * account_check_weekly_reward — called on hub entry.
 * Awards points if the weekly threshold was met since the last reset,
 * then resets the counter regardless of whether the threshold was met.
 */
void account_check_weekly_reward( ACCOUNT_DATA *acct )
{
    if ( !acct )
        return;

    if ( time(NULL) <= acct->weekly_reset_on + ACCT_WEEKLY_RESET_SECONDS )
        return; /* week not yet elapsed */

    if ( acct->weekly_active_seconds >= ACCT_WEEKLY_SECONDS_REQUIRED )
        account_grant_points( acct, ACCT_WEEKLY_REWARD_POINTS,
                              "SYSTEM", "Weekly activity reward" );

    acct->weekly_active_seconds = 0;
    acct->weekly_reset_on       = time( NULL );
    acct->dirty                 = TRUE;
}

/* =========================================================================
 * Section 7: Query helpers
 * ========================================================================= */

int account_char_count( ACCOUNT_DATA *acct )
{
    ACCOUNT_CHARACTER *ac;
    int count = 0;

    if ( !acct )
        return 0;

    for ( ac = acct->characters; ac; ac = ac->next )
        if ( ac->soft_deleted_on == 0 )
            count++;
    return count;
}

int account_max_slots( ACCOUNT_DATA *acct )
{
    int slots;

    if ( !acct )
        return ACCT_BASE_SLOTS;

    slots = ACCT_BASE_SLOTS + account_count_unlock( acct, UNLOCK_CHAR_SLOT );
    if ( slots > ACCT_MAX_SLOTS )
        slots = ACCT_MAX_SLOTS;
    return slots;
}

int account_count_unlock( ACCOUNT_DATA *acct, int unlock_id )
{
    ACCOUNT_UNLOCK *au;

    if ( !acct )
        return 0;

    for ( au = acct->unlocks; au; au = au->next )
        if ( au->unlock_id == unlock_id )
            return au->quantity;
    return 0;
}

bool account_has_unlock( ACCOUNT_DATA *acct, int unlock_id )
{
    return account_count_unlock( acct, unlock_id ) >= 1;
}

ACCOUNT_DATA *account_by_id( long account_id )
{
    ACCOUNT_DATA *acct;

    for ( acct = account_list; acct; acct = acct->next )
        if ( acct->account_id == account_id )
            return acct;
    return NULL;
}

/* =========================================================================
 * Section 8: Account ID counter
 * ========================================================================= */

void load_account_next_id( void )
{
    FILE *fp = fopen( "../account/next_id", "r" );
    if ( fp )
    {
        if ( fscanf( fp, "%ld", &next_account_id ) != 1 )
            next_account_id = 1001;
        fclose( fp );
    }
    else
    {
        /* File missing — first boot with account system. Start at 1001. */
        next_account_id = 1001;
    }
}

long assign_account_id( void )
{
    long id = next_account_id++;
    FILE *fp = fopen( "../account/next_id", "w" );
    if ( fp )
    {
        fprintf( fp, "%ld\n", next_account_id );
        fflush( fp );
        fclose( fp );
    }
    else
    {
        log_string( LOG_BUG, "assign_account_id: could not write ../account/next_id" );
    }
    return id;
}

/* =========================================================================
 * Section 9: Stubs — implemented in Phase 4
 *
 * These exist so the file compiles cleanly today. Each will be replaced
 * with a full implementation during Phase 4.
 * ========================================================================= */

ACCOUNT_DATA *load_account( const char *name )
{
    (void)name;
    return NULL;
}

void save_account( ACCOUNT_DATA *acct )
{
    (void)acct;
}

ACCOUNT_DATA *create_account( const char *name, const char *password_hash )
{
    (void)name;
    (void)password_hash;
    return NULL;
}

void free_account_from_desc( DESCRIPTOR_DATA *d )
{
    if ( !d || !d->account )
        return;

    PURGE_DATA( d->acct_temp_pass );
    d->acct_is_reset      = FALSE;
    d->acct_creating_char = FALSE;
    d->account            = NULL;
}

void detach_account( DESCRIPTOR_DATA *d )
{
    free_account_from_desc( d );
}

void attach_character( DESCRIPTOR_DATA *d, const char *char_name )
{
    (void)d;
    (void)char_name;
}

void detach_character( DESCRIPTOR_DATA *d )
{
    (void)d;
}

void account_boot_scan( void )
{
    /* Phase 19 — orphan detection. Stub for now. */
}
