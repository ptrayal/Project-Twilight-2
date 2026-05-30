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
 * Use the internal crypt_blowfish functions directly.
 * These take fixed stack buffers and have no null-pointer issues unlike crypt_ra.
 * Declarations match crypt_blowfish.h exactly.
 */
#include "crypt_blowfish.h"

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
 * account_hash_password — hash a plaintext password with bcrypt cost 12.
 * Uses _crypt_gensalt_blowfish_rn and _crypt_blowfish_rn directly so we
 * never pass NULL pointers into crypt_ra.
 * Returns a str_dup'd hash string; caller must PURGE_DATA it when done.
 * Returns NULL on failure.
 */
char *account_hash_password( const char *plaintext )
{
    char salt_buf[64];
    char hash_buf[64];
    char rnd_buf[16];
    int  i;
    char *result;

    if ( !plaintext )
        return NULL;

    /* Build 16 bytes of pseudo-random input for the salt */
    {
        unsigned long t = (unsigned long)time(NULL);
        unsigned long r = (unsigned long)rand();
        for ( i = 0; i < 8; i++ )
            rnd_buf[i]   = (char)((t >> (i * 8)) & 0xFF);
        for ( i = 0; i < 8; i++ )
            rnd_buf[8+i] = (char)((r >> (i * 8)) & 0xFF);
    }

    /* Generate a $2b$12$ salt into salt_buf */
    if ( !_crypt_gensalt_blowfish_rn( "$2b$", 12,
                                      rnd_buf, sizeof(rnd_buf),
                                      salt_buf, sizeof(salt_buf) ) )
        return NULL;

    /* Hash into hash_buf */
    if ( !_crypt_blowfish_rn( plaintext, salt_buf,
                              hash_buf, sizeof(hash_buf) ) )
        return NULL;

    result = str_dup( hash_buf );
    return result;
}

/*
 * account_verify_password — verify plaintext against a stored bcrypt hash.
 * Returns TRUE if the password matches.
 */
bool account_verify_password( const char *plaintext, const char *hash )
{
    char result_buf[64];

    if ( !plaintext || !hash || hash[0] == '\0' )
        return FALSE;

    /* Re-hash plaintext using the stored hash as the setting (contains the salt) */
    if ( !_crypt_blowfish_rn( plaintext, hash, result_buf, sizeof(result_buf) ) )
        return FALSE;

    /* Constant-time compare to resist timing attacks */
    {
        int diff = 0, i;
        int len  = strlen( hash );
        for ( i = 0; i < len; i++ )
            diff |= (unsigned char)result_buf[i] ^ (unsigned char)hash[i];
        if ( result_buf[len] != '\0' )
            diff |= 1;
        return ( diff == 0 );
    }
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
 * Section 9: Account file I/O
 * ========================================================================= */

/*
 * save_account — write an account file to account/[name].
 * Follows the same text keyword/value pattern as character pfiles.
 */
void save_account( ACCOUNT_DATA *acct )
{
    FILE              *fp;
    char               path[256];
    ACCOUNT_CHARACTER *ac;
    ACCOUNT_UNLOCK    *au;
    ACCOUNT_NOTE      *an;
    ACCOUNT_IP_ENTRY  *ai;
    int                ip_count = 0;

    if ( !acct || IS_NULLSTR(acct->name) )
        return;

    snprintf( path, sizeof(path), "../account/%s", acct->name );

    fp = fopen( path, "w" );
    if ( !fp )
    {
        log_string( LOG_BUG, Format("save_account: cannot open %s for writing.", path) );
        return;
    }

    fprintf( fp, "#ACCOUNT\n" );
    fprintf( fp, "Id        %ld\n",  acct->account_id );
    WriteToFile( fp, true, "Name",     acct->name );
    WriteToFile( fp, true, "PassHash", IS_NULLSTR(acct->password_hash) ? "" : acct->password_hash );
    WriteToFile( fp, true, "Email",    IS_NULLSTR(acct->email) ? "" : acct->email );
    fprintf( fp, "Flags     %ld\n",  acct->account_flags );
    fprintf( fp, "ModFlags  %ld\n",  acct->mod_flags );
    fprintf( fp, "TrustCeil %d\n",   acct->trust_ceiling );
    fprintf( fp, "Earned    %ld\n",  acct->points_earned );
    fprintf( fp, "Spent     %ld\n",  acct->points_spent );
    fprintf( fp, "CreatedOn %ld\n",  (long)acct->created_on );
    fprintf( fp, "LastLogin %ld\n",  (long)acct->last_login );
    fprintf( fp, "SoftDel   %ld\n",  (long)acct->soft_deleted_on );
    WriteToFile( fp, true, "RstTok", IS_NULLSTR(acct->reset_token) ? "" : acct->reset_token );
    fprintf( fp, "RstPend   %d\n",   acct->password_reset_pending ? 1 : 0 );
    fprintf( fp, "RstIssd   %ld\n",  (long)acct->reset_issued_on );
    fprintf( fp, "WeekSec   %d\n",   acct->weekly_active_seconds );
    fprintf( fp, "WeekRst   %ld\n",  (long)acct->weekly_reset_on );
    fprintf( fp, "MonGrants %d\n",   acct->monthly_grants_used );
    fprintf( fp, "GrantsRst %ld\n",  (long)acct->grants_reset_on );

    /* IP log — most recent first, cap at ACCT_IP_LOG_MAX */
    fprintf( fp, "IpLog\n" );
    for ( ai = acct->ip_log; ai && ip_count < ACCT_IP_LOG_MAX; ai = ai->next, ip_count++ )
        fprintf( fp, "  %ld %s~\n", (long)ai->seen_on, IS_NULLSTR(ai->ip) ? "" : ai->ip );
    fprintf( fp, "EndIpLog\n" );

    /* Characters */
    fprintf( fp, "Characters\n" );
    for ( ac = acct->characters; ac; ac = ac->next )
        fprintf( fp, "  %s~ %ld %ld %ld\n",
                 IS_NULLSTR(ac->char_name) ? "" : ac->char_name,
                 ac->char_id,
                 (long)ac->last_played,
                 (long)ac->soft_deleted_on );
    fprintf( fp, "EndCharacters\n" );

    /* Unlocks */
    fprintf( fp, "Unlocks\n" );
    for ( au = acct->unlocks; au; au = au->next )
        fprintf( fp, "  %d %d %ld\n", au->unlock_id, au->quantity, (long)au->purchased_on );
    fprintf( fp, "EndUnlocks\n" );

    /* Notes */
    fprintf( fp, "Notes\n" );
    for ( an = acct->notes; an; an = an->next )
        fprintf( fp, "  %s~ %d %ld %s~\n",
                 IS_NULLSTR(an->author) ? "" : an->author,
                 an->visibility,
                 (long)an->written_on,
                 IS_NULLSTR(an->body) ? "" : an->body );
    fprintf( fp, "EndNotes\n" );

    fprintf( fp, "#END\n" );
    fclose( fp );

    acct->dirty = FALSE;
}

/*
 * load_account — load an account from disk into memory.
 * Searches account_list first to avoid duplicate structs.
 * Returns NULL if the file does not exist.
 */
ACCOUNT_DATA *load_account( const char *name )
{
    FILE              *fp;
    ACCOUNT_DATA      *acct;
    char               path[256];
    const char        *word;
    bool               fMatch;

    if ( IS_NULLSTR(name) )
        return NULL;

    /* Search in-memory list first */
    for ( acct = account_list; acct; acct = acct->next )
        if ( !str_cmp(acct->name, name) )
            return acct;

    snprintf( path, sizeof(path), "../account/%s", name );
    fp = fopen( path, "r" );
    if ( !fp )
        return NULL;

    acct = new_account();
    if ( !acct )
    {
        fclose( fp );
        return NULL;
    }

    /* Read header */
    {
        const char *section = fread_word( fp );
        if ( str_cmp(section, "#ACCOUNT") )
        {
            log_string( LOG_BUG, Format("load_account: bad header in %s", path) );
            free_account( acct );
            fclose( fp );
            return NULL;
        }
    }

    for ( ; ; )
    {
        word   = feof(fp) ? "#END" : fread_word( fp );
        fMatch = FALSE;

        if ( !str_cmp(word, "#END") )
            break;

        /* Single-value fields */
        if ( !str_cmp(word, "Id") )       { acct->account_id            = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "Name") )     { PURGE_DATA(acct->name);
                                             acct->name                  = fread_string(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "PassHash") ) { PURGE_DATA(acct->password_hash);
                                             acct->password_hash         = fread_string(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "Email") )    { PURGE_DATA(acct->email);
                                             acct->email                 = fread_string(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "Flags") )    { acct->account_flags          = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "ModFlags") ) { acct->mod_flags              = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "TrustCeil")) { acct->trust_ceiling          = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "Earned") )   { acct->points_earned          = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "Spent") )    { acct->points_spent           = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "CreatedOn")) { acct->created_on             = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "LastLogin")) { acct->last_login             = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "SoftDel") )  { acct->soft_deleted_on        = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "RstTok") )   { PURGE_DATA(acct->reset_token);
                                             acct->reset_token           = fread_string(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "RstPend") )  { acct->password_reset_pending = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "RstIssd") )  { acct->reset_issued_on        = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "WeekSec") )  { acct->weekly_active_seconds  = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "WeekRst") )  { acct->weekly_reset_on        = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "MonGrants")) { acct->monthly_grants_used    = fread_number(fp); fMatch = TRUE; }
        if ( !str_cmp(word, "GrantsRst")) { acct->grants_reset_on        = fread_number(fp); fMatch = TRUE; }

        /* IP log block */
        if ( !str_cmp(word, "IpLog") )
        {
            fMatch = TRUE;
            for ( ; ; )
            {
                const char *ip_word = feof(fp) ? "EndIpLog" : fread_word(fp);
                if ( !str_cmp(ip_word, "EndIpLog") )
                    break;
                {
                    ACCOUNT_IP_ENTRY *ai = new_account_ip();
                    ai->seen_on = atol(ip_word);
                    ai->ip      = fread_string(fp);
                    ai->next        = acct->ip_log;
                    acct->ip_log    = ai;
                }
            }
        }

        /* Characters block */
        if ( !str_cmp(word, "Characters") )
        {
            fMatch = TRUE;
            for ( ; ; )
            {
                const char *cw = feof(fp) ? "EndCharacters" : fread_word(fp);
                if ( !str_cmp(cw, "EndCharacters") )
                    break;
                {
                    ACCOUNT_CHARACTER *ac = new_account_char();
                    /* fread_word includes the trailing ~ — strip it */
                    {
                        char cw_clean[MAX_INPUT_LENGTH];
                        int  cw_len = strlen(cw);
                        strncpy( cw_clean, cw, sizeof(cw_clean) - 1 );
                        cw_clean[sizeof(cw_clean)-1] = '\0';
                        if ( cw_len > 0 && cw_clean[cw_len-1] == '~' )
                            cw_clean[cw_len-1] = '\0';
                        ac->char_name = str_dup( cw_clean );
                    }
                    ac->char_id         = fread_number(fp);
                    ac->last_played     = fread_number(fp);
                    ac->soft_deleted_on = fread_number(fp);
                    if ( !acct->characters )
                    {
                        acct->characters = ac;
                    }
                    else
                    {
                        ACCOUNT_CHARACTER *tail = acct->characters;
                        while ( tail->next ) tail = tail->next;
                        tail->next = ac;
                    }
                }
            }
        }

        /* Unlocks block */
        if ( !str_cmp(word, "Unlocks") )
        {
            fMatch = TRUE;
            for ( ; ; )
            {
                const char *uw = feof(fp) ? "EndUnlocks" : fread_word(fp);
                if ( !str_cmp(uw, "EndUnlocks") )
                    break;
                {
                    ACCOUNT_UNLOCK *au = new_account_unlock();
                    au->unlock_id    = atoi(uw);
                    au->quantity     = fread_number(fp);
                    au->purchased_on = fread_number(fp);
                    au->next         = acct->unlocks;
                    acct->unlocks    = au;
                }
            }
        }

        /* Notes block */
        if ( !str_cmp(word, "Notes") )
        {
            fMatch = TRUE;
            for ( ; ; )
            {
                const char *nw = feof(fp) ? "EndNotes" : fread_word(fp);
                if ( !str_cmp(nw, "EndNotes") )
                    break;
                {
                    ACCOUNT_NOTE *an = new_account_note();
                    {
                        char nw_clean[MAX_INPUT_LENGTH];
                        int  nw_len = strlen(nw);
                        strncpy( nw_clean, nw, sizeof(nw_clean) - 1 );
                        nw_clean[sizeof(nw_clean)-1] = '\0';
                        if ( nw_len > 0 && nw_clean[nw_len-1] == '~' )
                            nw_clean[nw_len-1] = '\0';
                        an->author = str_dup( nw_clean );
                    }
                    an->visibility = fread_number(fp);
                    an->written_on = fread_number(fp);
                    an->body       = fread_string(fp);
                    an->next       = acct->notes;
                    acct->notes    = an;
                }
            }
        }

        if ( !fMatch )
        {
            log_string( LOG_BUG, Format("load_account: unknown keyword '%s' in %s", word, path) );
            fread_to_eol( fp );
        }
    }

    fclose( fp );

    /* Link into global list */
    acct->next   = account_list;
    account_list = acct;

    return acct;
}

/*
 * create_account — allocate, assign ID, grant new-account bonus, save.
 * password_hash must already be a bcrypt hash string.
 */
ACCOUNT_DATA *create_account( const char *name, const char *password_hash )
{
    ACCOUNT_DATA *acct;

    acct = new_account();
    if ( !acct )
        return NULL;

    acct->account_id    = assign_account_id();
    acct->name          = str_dup( name );
    acct->password_hash = str_dup( IS_NULLSTR(password_hash) ? "" : password_hash );
    acct->created_on    = time( NULL );
    acct->last_login    = time( NULL );
    acct->trust_ceiling = MAX_LEVEL;
    acct->weekly_reset_on = time( NULL );
    acct->grants_reset_on = time( NULL );
    acct->dirty         = TRUE;

    /* Link into global list before granting points (grant calls save) */
    acct->next   = account_list;
    account_list = acct;

    /* New account bonus — writes to ledger log */
    account_grant_points( acct, ACCT_NEW_ACCOUNT_BONUS, "SYSTEM", "New account bonus" );

    /* System note */
    {
        ACCOUNT_NOTE *an = new_account_note();
        an->author     = str_dup( "SYSTEM" );
        an->body       = str_dup( "Account created." );
        an->written_on = time( NULL );
        an->visibility = NOTE_VIS_SYSTEM;
        an->next       = acct->notes;
        acct->notes    = an;
    }

    account_audit_log( "ACCOUNT_CREATED", acct->account_id, "" );
    save_account( acct );

    return acct;
}

void free_account_from_desc( DESCRIPTOR_DATA *d )
{
    DESCRIPTOR_DATA *d2;
    bool             still_referenced;

    if ( !d )
        return;

    PURGE_DATA( d->acct_temp_pass );
    d->acct_is_reset      = FALSE;
    d->acct_creating_char = FALSE;

    if ( !d->account )
        return;

    /* Only free the account struct if no other descriptor references it */
    still_referenced = FALSE;
    for ( d2 = descriptor_list; d2; d2 = d2->next )
    {
        if ( d2 != d && d2->account == d->account )
        {
            still_referenced = TRUE;
            break;
        }
    }

    if ( !still_referenced )
        free_account( d->account );

    d->account = NULL;
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
