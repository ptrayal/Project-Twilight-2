/***************************************************************************
 * Much of the code is the original work of Peter Fitzgerald who turned    *
 * it over to Brandon Morrison who has adopted and improved the code.      *
 * Copyright (C) 2012 - 2024                                               *
 **************************************************************************/

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "twilight.h"
#include "recycle.h"
#include "account.h"
#include "interp.h"

/* =========================================================================
 * Helper: format a "time ago" string for last-played display
 * ========================================================================= */
static const char *time_ago( time_t then )
{
    static char buf[64];
    long diff;
    time_t now = time(NULL);

    if ( then == 0 )
    {
        snprintf( buf, sizeof(buf), "never" );
        return buf;
    }

    diff = (long)(now - then);

    if ( diff < 60 )
        snprintf( buf, sizeof(buf), "just now" );
    else if ( diff < 3600 )
        snprintf( buf, sizeof(buf), "%ld min ago", diff / 60 );
    else if ( diff < 86400 )
        snprintf( buf, sizeof(buf), "%ld hr ago", diff / 3600 );
    else
        snprintf( buf, sizeof(buf), "%ld days ago", diff / 86400 );

    return buf;
}

/* =========================================================================
 * Helper: look up an account by name, with error feedback to ch.
 * Returns NULL and sends an error message if not found.
 * ========================================================================= */
static ACCOUNT_DATA *acct_lookup_msg( CHAR_DATA *ch, const char *name )
{
    ACCOUNT_DATA *acct;
    char          lower[MAX_INPUT_LENGTH];
    int           i;

    if ( IS_NULLSTR(name) )
    {
        send_to_char( "Specify an account name.\n\r", ch );
        return NULL;
    }

    strncpy( lower, name, sizeof(lower) - 1 );
    lower[sizeof(lower)-1] = '\0';
    for ( i = 0; lower[i]; i++ )
        lower[i] = LOWER(lower[i]);

    acct = load_account( lower );
    if ( !acct )
    {
        send_to_char( Format("No account named '%s' exists.\n\r", lower), ch );
        return NULL;
    }
    return acct;
}

/* =========================================================================
 * show_account_hub — display the account hub screen.
 * ========================================================================= */
void show_account_hub( DESCRIPTOR_DATA *d )
{
    ACCOUNT_DATA      *acct;
    ACCOUNT_CHARACTER *ac;
    BUFFER            *buf;
    int                n;
    long               balance;
    int                slots_used;
    int                slots_max;

    if ( !d || !d->account )
        return;

    acct       = d->account;
    balance    = acct->points_earned - acct->points_spent;
    slots_used = account_char_count( acct );
    slots_max  = account_max_slots( acct );

    buf = new_buf();

    add_buf( buf,
        "\n\r\tY====================================================================\tn\n\r"
        "\tB                       PROJECT TWILIGHT\tn\n\r"
        "\tY====================================================================\tn\n\r" );

    add_buf( buf, Format(
        "  \tWAccount :\tn %-20s    \tWPoints :\tn %ld\n\r"
        "  \tWSlots   :\tn %d / %-17d    \tWStatus :\tn %s\n\r",
        acct->name,
        balance,
        slots_used, slots_max,
        IS_SET(acct->mod_flags, MOD_FROZEN) ? "\tRSuspended\tn" : "\tGActive\tn" ) );

    add_buf( buf,
        "\tY--------------------------------------------------------------------\tn\n\r"
        "  \tBCharacters\tn\n\r"
        "\tY--------------------------------------------------------------------\tn\n\r" );

    n = 1;
    for ( ac = acct->characters; ac; ac = ac->next )
    {
        if ( ac->soft_deleted_on != 0 )
            continue;
        add_buf( buf, Format(
            "  \tY[%d]\tn %-20s Last played: %s\n\r",
            n, ac->char_name, time_ago(ac->last_played) ) );
        n++;
    }

    if ( n == 1 )
        add_buf( buf, "  (no characters \xe2\x80\x94 use \tW[C]\tn to create one)\n\r" );

    add_buf( buf,
        "\tY====================================================================\tn\n\r" );

    if ( IS_SET(acct->mod_flags, MOD_FROZEN) )
    {
        add_buf( buf,
            "  \tD[P] Play a Character\tn       \tW[C]\tn Create New Character\n\r"
            "  \tW[U]\tn Unlock Features          \tW[A]\tn Account Information\n\r"
            "  \tW[L]\tn Point Ledger             \tW[D]\tn Delete a Character\n\r"
            "  \tW[Q]\tn Quit\n\r" );
    }
    else
    {
        add_buf( buf,
            "  \tW[P]\tn Play a Character         \tW[C]\tn Create New Character\n\r"
            "  \tW[U]\tn Unlock Features          \tW[A]\tn Account Information\n\r"
            "  \tW[L]\tn Point Ledger             \tW[D]\tn Delete a Character\n\r"
            "  \tW[Q]\tn Quit\n\r" );
    }

    add_buf( buf,
        "\tY====================================================================\tn\n\r"
        "Enter choice: " );

    write_to_buffer( d, buf_string(buf), 0 );
    free_buf( buf );
}

/* =========================================================================
 * show_account_unlocks — display the unlock store with numbered items.
 * Called when the player selects [U] from the hub, and after each purchase.
 * ========================================================================= */
void show_account_unlocks( DESCRIPTOR_DATA *d )
{
    ACCOUNT_DATA     *acct;
    int               i;
    int               n = 1;
    long              balance;
    char              line[256];

    if ( !d || !d->account )
        return;

    acct    = d->account;
    balance = acct->points_earned - acct->points_spent;

    write_to_buffer( d,
        "\n\r\tY====================================================================\tn\n\r"
        "\tW                       UNLOCK STORE\tn\n\r"
        "\tY====================================================================\tn\n\r", 0 );

    snprintf( line, sizeof(line),
        "  Your balance: \tY%ld\tn point%s\n\r"
        "\tY--------------------------------------------------------------------\tn\n\r"
        "  \tWSlot Unlocks\tn\n\r",
        balance, balance == 1 ? "" : "s" );
    write_to_buffer( d, line, 0 );

    /* Slots */
    for ( i = 0; unlock_defs[i].name != NULL; i++ )
    {
        if ( unlock_defs[i].category != UNLOCK_CAT_SLOT )
            continue;
        {
            int  owned   = account_count_unlock( acct, unlock_defs[i].unlock_id );
            int  cur_max = account_max_slots( acct );
            bool maxed   = ( cur_max >= ACCT_MAX_SLOTS );
            snprintf( line, sizeof(line),
                "  \tW[%d]\tn %-20s \tY%3d pts\tn  %s\n\r",
                n++,
                unlock_defs[i].name,
                unlock_defs[i].cost,
                maxed   ? "\tR(max slots reached)\tn" :
                owned   ? Format("\tG(owned x%d)\tn", owned) :
                balance >= unlock_defs[i].cost ? "\tG(affordable)\tn" : "\tR(insufficient points)\tn" );
            write_to_buffer( d, line, 0 );
        }
    }

    write_to_buffer( d,
        "\tY--------------------------------------------------------------------\tn\n\r"
        "  \tWClan Unlocks\tn\n\r", 0 );

    /* Clans */
    for ( i = 0; unlock_defs[i].name != NULL; i++ )
    {
        if ( unlock_defs[i].category != UNLOCK_CAT_CLAN )
            continue;
        {
            bool owned = account_has_unlock( acct, unlock_defs[i].unlock_id );
            snprintf( line, sizeof(line),
                "  \tW[%d]\tn %-20s \tY%3d pts\tn  %s\n\r",
                n++,
                unlock_defs[i].name,
                unlock_defs[i].cost,
                owned   ? "\tG(owned)\tn" :
                balance >= unlock_defs[i].cost ? "\tG(affordable)\tn" : "\tR(insufficient points)\tn" );
            write_to_buffer( d, line, 0 );
        }
    }

    write_to_buffer( d,
        "\tY====================================================================\tn\n\r"
        "  Enter a number to purchase, or \tW0\tn to return to the hub.\n\r"
        "  Choice: ", 0 );
}

/* =========================================================================
 * do_account — in-game account command dispatcher.
 *
 * Player subcommands (trust 0):  info, points, ledger, unlocks
 * Staff subcommands (trust 3+):  info <name>, note, grant, freeze/unfreeze,
 *                                restrict/unrestrict, scan
 * Senior staff (trust 4+):       ban/unban, transfer, reset password, purge
 * Implementor only (trust 5):    trust_ceiling, admin_lock/unlock
 * ========================================================================= */
void do_account( CHAR_DATA *ch, char *argument )
{
    ACCOUNT_DATA *acct;
    ACCOUNT_DATA *target;
    long          balance;
    char          arg1[MAX_INPUT_LENGTH];
    char          arg2[MAX_INPUT_LENGTH];
    char          arg3[MAX_INPUT_LENGTH];
    int           trust;

    CheckCH(ch);

    if ( !ch->desc || !ch->desc->account )
    {
        send_to_char( "No account is attached to this session.\n\r", ch );
        return;
    }

    acct  = ch->desc->account;
    trust = get_trust(ch);

    argument = one_argument( argument, arg1 );
    argument = one_argument( argument, arg2 );
    one_argument( argument, arg3 );

    balance = acct->points_earned - acct->points_spent;

    /* ── info ──────────────────────────────────────────────────────────── */
    if ( IS_NULLSTR(arg1) || !str_cmp(arg1, "info") )
    {
        /* Staff: account info <name> — view another account */
        if ( !IS_NULLSTR(arg2) && trust >= STORYTELLER )
        {
            target = acct_lookup_msg( ch, arg2 );
            if ( !target ) return;

            long tbal = target->points_earned - target->points_spent;
            send_to_char( Format(
                "\tBAccount: \tW%s\tn  (ID %ld)\n\r"
                "\tY--------------------------------------------------------------------\tn\n\r"
                "  \tWBalance  :\tn %ld pts  (%ld earned, %ld spent)\n\r"
                "  \tWEmail    :\tn %s\n\r"
                "  \tWCreated  :\tn %s"
                "  \tWLast Login:\tn %s"
                "  \tWFlags    :\tn %ld   \tWMod Flags:\tn %ld\n\r"
                "  \tWTrust Ceil:\tn %d\n\r"
                "  \tWStatus   :\tn %s\n\r",
                target->name, target->account_id,
                tbal, target->points_earned, target->points_spent,
                IS_NULLSTR(target->email) ? "(not set)" : target->email,
                ctime( &target->created_on ),
                target->last_login ? ctime( &target->last_login ) : "never\n",
                target->account_flags, target->mod_flags,
                target->trust_ceiling,
                IS_SET(target->mod_flags, MOD_BANNED)     ? "\tRBanned\tn"     :
                IS_SET(target->mod_flags, MOD_FROZEN)     ? "\tRFrozen\tn"     :
                IS_SET(target->mod_flags, MOD_RESTRICTED) ? "\tYRestricted\tn" :
                "\tGActive\tn" ), ch );

            /* Show characters */
            {
                ACCOUNT_CHARACTER *ac;
                send_to_char( "  \tWCharacters:\tn\n\r", ch );
                for ( ac = target->characters; ac; ac = ac->next )
                {
                    send_to_char( Format( "    %s%s (id %ld, last played: %s)\n\r",
                        ac->char_name,
                        ac->soft_deleted_on ? " \tR[deleted]\tn" : "",
                        ac->char_id,
                        time_ago(ac->last_played) ), ch );
                }
            }
            return;
        }

        /* Own account info */
        send_to_char( Format(
            "\tBAccount Information\tn\n\r"
            "\tY--------------------------------------------------------------------\tn\n\r"
            "  \tWAccount  :\tn %s\n\r"
            "  \tWID       :\tn %ld\n\r"
            "  \tWBalance  :\tn %ld points\n\r"
            "  \tWEarned   :\tn %ld  \tWSpent:\tn %ld\n\r"
            "  \tWEmail    :\tn %s\n\r"
            "  \tWCreated  :\tn %s",
            acct->name,
            acct->account_id,
            balance,
            acct->points_earned, acct->points_spent,
            IS_NULLSTR(acct->email) ? "(not set)" : acct->email,
            ctime( &acct->created_on ) ), ch );
        return;
    }

    /* ── email ──────────────────────────────────────────────────────────── */
    if ( !str_cmp(arg1, "email") )
    {
        ACCOUNT_DATA *target = acct;
        const char   *action = arg2;   /* "set", "clear", or account name for staff */
        const char   *value  = arg3;   /* email address (self) or "set"/"clear" (staff) */

        /*
         * Staff: account email <name> set <address>
         *                       arg2   arg3  argument
         *        account email <name> clear
         * If arg2 looks like an account name (not "set"/"clear") and trust qualifies,
         * shift so target=<name>, action=arg3, value=remaining argument.
         */
        if ( trust >= STORYTELLER && !IS_NULLSTR(arg2)
             && str_cmp(arg2, "set") && str_cmp(arg2, "clear") )
        {
            ACCOUNT_DATA *found = acct_lookup_msg(ch, arg2);
            if ( found )
            {
                target = found;
                action = arg3;
                value  = argument;  /* remainder after arg1+arg2+arg3 */
            }
        }

        /* account email — view */
        if ( IS_NULLSTR(action) )
        {
            send_to_char( Format(
                "\tWEmail address\tn on account \tW%s\tn: %s\n\r",
                target->name,
                IS_NULLSTR(target->email) ? "\tD(not set)\tn"
                                           : Format("\tW%s\tn", target->email) ), ch );
            if ( target == acct )
                send_to_char( "  Use \tWaccount email set <address>\tn to update.\n\r", ch );
            return;
        }

        /* account email clear */
        if ( !str_cmp(action, "clear") )
        {
            PURGE_DATA(target->email);
            target->email = NULL;
            target->dirty = TRUE;
            save_account(target);
            account_audit_log("EMAIL_CLEAR", target->account_id,
                Format("actor=%s", ch->name));
            send_to_char( Format(
                "\tGEmail address cleared\tn on account \tW%s\tn.\n\r",
                target->name ), ch );
            return;
        }

        /* account email set <address> */
        if ( !str_cmp(action, "set") )
        {
            if ( IS_NULLSTR(value) )
            {
                send_to_char( "\tRSyntax:\tn account email set <address>\n\r", ch );
                return;
            }
            if ( !is_valid_email(value) )
            {
                send_to_char( "\tRInvalid email format.\tn Please use a real address (e.g. you@domain.com).\n\r", ch );
                return;
            }
            if ( is_blocked_domain(value) )
            {
                send_to_char( "\tRThat email domain is not accepted.\tn Please use a permanent email address.\n\r", ch );
                return;
            }
            PURGE_DATA(target->email);
            target->email = str_dup(value);
            target->dirty = TRUE;
            save_account(target);
            account_audit_log("EMAIL_SET", target->account_id,
                Format("email=%s actor=%s", value, ch->name));
            send_to_char( Format(
                "\tGEmail address set\tn on account \tW%s\tn: \tW%s\tn\n\r",
                target->name, value ), ch );
            return;
        }

        /* Unknown sub-action */
        send_to_char( "Syntax: account email\n\r"
                      "        account email set <address>\n\r"
                      "        account email clear\n\r", ch );
        return;
    }

    /* ── points ─────────────────────────────────────────────────────────── */
    if ( !str_cmp(arg1, "points") )
    {
        send_to_char( Format(
            "Account \tW%s\tn: \tG%ld\tn points available "
            "(%ld earned, %ld spent).\n\r",
            acct->name, balance,
            acct->points_earned, acct->points_spent ), ch );
        return;
    }

    /* ── ledger ─────────────────────────────────────────────────────────── */
    if ( !str_cmp(arg1, "ledger") )
    {
        FILE  *fp;
        char   line[512];
        char   search[64];
        BUFFER *buf;

        snprintf( search, sizeof(search), " %ld ", acct->account_id );
        fp = fopen( "../log/account/ledger.log", "r" );
        if ( !fp )
        {
            send_to_char( "No ledger entries found.\n\r", ch );
            return;
        }

        buf = new_buf();
        add_buf( buf,
            "\tBPoint Ledger\tn\n\r"
            "\tY--------------------------------------------------------------------\tn\n\r" );

        while ( fgets(line, sizeof(line), fp) )
        {
            if ( strstr(line, search) )
                add_buf( buf, line );
        }
        fclose( fp );

        add_buf( buf, "\tY--------------------------------------------------------------------\tn\n\r" );
        page_to_char( buf_string(buf), ch );
        free_buf( buf );
        return;
    }

    /* ── unlocks ────────────────────────────────────────────────────────── */
    if ( !str_cmp(arg1, "unlocks") )
    {
        ACCOUNT_UNLOCK *au;
        if ( !acct->unlocks )
        {
            send_to_char( "You have no unlocks on this account.\n\r", ch );
            return;
        }
        send_to_char( "\tBUnlocks\tn\n\r"
            "\tY--------------------------------------------------------------------\tn\n\r", ch );
        for ( au = acct->unlocks; au; au = au->next )
            send_to_char( Format( "  Unlock ID %-4d  Qty: %d  Purchased: %s",
                au->unlock_id, au->quantity,
                ctime(&au->purchased_on) ), ch );
        return;
    }

    /* ══════════════════════════════════════════════════════════════════════
     * STAFF COMMANDS — STORYTELLER (trust 3) and above
     * ══════════════════════════════════════════════════════════════════════ */

    /* ── note ───────────────────────────────────────────────────────────── */
    if ( !str_cmp(arg1, "note") )
    {
        if ( trust < STORYTELLER )
        {
            send_to_char( "You don't have permission to do that.\n\r", ch );
            return;
        }

        /* account note list <name> */
        if ( !str_cmp(arg2, "list") )
        {
            ACCOUNT_NOTE *an;
            int vis_floor;

            target = acct_lookup_msg( ch, arg3 );
            if ( !target ) return;

            /* Determine which notes this staff member can see */
            vis_floor = ( trust >= IMPLEMENTOR ) ? NOTE_VIS_SYSTEM :
                        ( trust >= MASTER )      ? NOTE_VIS_ADMIN  :
                                                   NOTE_VIS_STAFF;

            send_to_char( Format(
                "\tBNotes for account: \tW%s\tn\n\r"
                "\tY--------------------------------------------------------------------\tn\n\r",
                target->name ), ch );

            for ( an = target->notes; an; an = an->next )
            {
                if ( an->visibility > vis_floor )
                    continue;
                send_to_char( Format(
                    "  \tW[%s]\tn vis=%d  %s\n\r    %s\n\r",
                    an->author, an->visibility,
                    ctime(&an->written_on),
                    an->body ), ch );
            }
            return;
        }

        /* account note add <name> <vis> <text...>
         * After initial parsing: arg1=note arg2=add arg3=<name>
         * argument holds: <vis> <text...> */
        if ( !str_cmp(arg2, "add") )
        {
            ACCOUNT_NOTE *an;
            int           vis;
            char          vis_buf[MAX_INPUT_LENGTH];

            /* arg3 = account name, argument = "<vis> <text...>" */
            argument = one_argument( argument, vis_buf );
            /* argument now holds the text body */

            if ( IS_NULLSTR(arg3) || IS_NULLSTR(vis_buf) || IS_NULLSTR(argument) )
            {
                send_to_char( "Syntax: account note add <name> <vis> <text>\n\r"
                    "  vis: system(0) admin(1) staff(2) owner(3)\n\r", ch );
                return;
            }

            target = acct_lookup_msg( ch, arg3 );
            if ( !target ) return;

            vis = atoi( vis_buf );
            if ( vis < NOTE_VIS_SYSTEM || vis > NOTE_VIS_OWNER )
            {
                send_to_char( "Visibility must be 0 (system), 1 (admin), "
                    "2 (staff), or 3 (owner).\n\r", ch );
                return;
            }

            /* Staff can't write notes with higher visibility than their level allows */
            if ( ( vis == NOTE_VIS_SYSTEM && trust < IMPLEMENTOR ) ||
                 ( vis == NOTE_VIS_ADMIN  && trust < MASTER ) )
            {
                send_to_char( "You cannot write notes at that visibility level.\n\r", ch );
                return;
            }

            an             = new_account_note();
            an->author     = str_dup( ch->name );
            an->body       = str_dup( argument );
            an->written_on = time( NULL );
            an->visibility = vis;
            an->next       = target->notes;
            target->notes  = an;
            target->dirty  = TRUE;
            save_account( target );

            account_audit_log( "NOTE_ADD", target->account_id,
                Format("author=%s vis=%d", ch->name, vis) );
            send_to_char( Format( "Note added to account \tW%s\tn.\n\r",
                target->name ), ch );
            return;
        }

        send_to_char( "Syntax: account note list <name>\n\r"
            "        account note add  <name> <vis> <text>\n\r", ch );
        return;
    }

    /* ── grant ──────────────────────────────────────────────────────────── */
    if ( !str_cmp(arg1, "grant") )
    {
        long   amount;
        char   reason_buf[MSL];

        if ( trust < STORYTELLER )
        {
            send_to_char( "You don't have permission to do that.\n\r", ch );
            return;
        }

        /* account grant <name> <amount> <reason...> */
        if ( IS_NULLSTR(arg2) || IS_NULLSTR(arg3) )
        {
            send_to_char( "Syntax: account grant <name> <amount> <reason>\n\r", ch );
            return;
        }

        target = acct_lookup_msg( ch, arg2 );
        if ( !target ) return;

        amount = atol( arg3 );
        if ( amount <= 0 )
        {
            send_to_char( "Amount must be a positive number.\n\r", ch );
            return;
        }

        /* Remaining argument after arg3 is the reason */
        snprintf( reason_buf, sizeof(reason_buf), "%s",
            IS_NULLSTR(argument) ? "(no reason given)" : argument );

        /* Check monthly grant cap */
        {
            long cap = (long)trust * 10;

            /* Reset monthly counter if needed */
            if ( time(NULL) > acct->grants_reset_on + ACCT_GRANT_RESET_SECONDS )
            {
                acct->monthly_grants_used = 0;
                acct->grants_reset_on     = time( NULL );
                acct->dirty               = TRUE;
            }

            if ( trust < IMPLEMENTOR
                && acct->monthly_grants_used + amount > cap )
            {
                send_to_char( Format(
                    "That would exceed your monthly grant cap of %ld points "
                    "(%d used so far this month).\n\r",
                    cap, acct->monthly_grants_used ), ch );
                return;
            }

            if ( trust < IMPLEMENTOR )
                acct->monthly_grants_used += (int)amount;
        }

        account_grant_points( target, amount, ch->name, reason_buf );
        save_account( target );
        save_account( acct );   /* save grant tracker on granting account */

        account_audit_log( "POINT_GRANT", target->account_id,
            Format("amount=%ld actor=%s reason=%s monthly_used=%d",
                amount, ch->name, reason_buf, acct->monthly_grants_used) );

        send_to_char( Format(
            "\tGGranted %ld points to account \tW%s\tn.\n\r",
            amount, target->name ), ch );
        return;
    }

    /* ── freeze / unfreeze ──────────────────────────────────────────────── */
    if ( !str_cmp(arg1, "freeze") || !str_cmp(arg1, "unfreeze") )
    {
        bool freezing = !str_cmp(arg1, "freeze");

        if ( trust < STORYTELLER )
        {
            send_to_char( "You don't have permission to do that.\n\r", ch );
            return;
        }

        target = acct_lookup_msg( ch, arg2 );
        if ( !target ) return;

        if ( IS_SET(target->mod_flags, MOD_ADMIN_LOCKED) && trust < IMPLEMENTOR )
        {
            send_to_char( "That account is admin-locked and cannot be modified.\n\r", ch );
            return;
        }

        if ( freezing )
            SET_BIT( target->mod_flags, MOD_FROZEN );
        else
            REMOVE_BIT( target->mod_flags, MOD_FROZEN );

        target->dirty = TRUE;
        save_account( target );
        account_audit_log( freezing ? "FREEZE" : "UNFREEZE", target->account_id,
            Format("actor=%s", ch->name) );
        send_to_char( Format( "Account \tW%s\tn is now %s.\n\r",
            target->name, freezing ? "\tRfrozen\tn" : "\tGunfrozen\tn" ), ch );
        return;
    }

    /* ── restrict / unrestrict ──────────────────────────────────────────── */
    if ( !str_cmp(arg1, "restrict") || !str_cmp(arg1, "unrestrict") )
    {
        bool restricting = !str_cmp(arg1, "restrict");

        if ( trust < STORYTELLER )
        {
            send_to_char( "You don't have permission to do that.\n\r", ch );
            return;
        }

        target = acct_lookup_msg( ch, arg2 );
        if ( !target ) return;

        if ( IS_SET(target->mod_flags, MOD_ADMIN_LOCKED) && trust < IMPLEMENTOR )
        {
            send_to_char( "That account is admin-locked and cannot be modified.\n\r", ch );
            return;
        }

        if ( restricting )
            SET_BIT( target->mod_flags, MOD_RESTRICTED );
        else
            REMOVE_BIT( target->mod_flags, MOD_RESTRICTED );

        target->dirty = TRUE;
        save_account( target );
        account_audit_log( restricting ? "RESTRICT" : "UNRESTRICT", target->account_id,
            Format("actor=%s", ch->name) );
        send_to_char( Format( "Account \tW%s\tn is now %s.\n\r",
            target->name,
            restricting ? "\tYrestricted\tn" : "\tGunrestricted\tn" ), ch );
        return;
    }

    /* ── scan ───────────────────────────────────────────────────────────── */
    if ( !str_cmp(arg1, "scan") )
    {
        if ( trust < STORYTELLER )
        {
            send_to_char( "You don't have permission to do that.\n\r", ch );
            return;
        }
        send_to_char( "Running orphan and hard-delete scan — check log/account/orphan.log and the boot log for results.\n\r", ch );
        account_boot_scan();
        send_to_char( "Scan complete.\n\r", ch );
        return;
    }

    /* ══════════════════════════════════════════════════════════════════════
     * SENIOR STAFF COMMANDS — MASTER (trust 4) and above
     * ══════════════════════════════════════════════════════════════════════ */

    /* ── ban / unban ────────────────────────────────────────────────────── */
    if ( !str_cmp(arg1, "ban") || !str_cmp(arg1, "unban") )
    {
        bool banning = !str_cmp(arg1, "ban");

        if ( trust < MASTER )
        {
            send_to_char( "You don't have permission to do that.\n\r", ch );
            return;
        }

        target = acct_lookup_msg( ch, arg2 );
        if ( !target ) return;

        if ( IS_SET(target->mod_flags, MOD_ADMIN_LOCKED) && trust < IMPLEMENTOR )
        {
            send_to_char( "That account is admin-locked and cannot be modified.\n\r", ch );
            return;
        }

        if ( banning )
        {
            char reason_buf[MSL];
            /* arg3 = first word of reason, argument = rest; combine */
            if ( !IS_NULLSTR(arg3) && !IS_NULLSTR(argument) )
                snprintf( reason_buf, sizeof(reason_buf), "%s %s", arg3, argument );
            else if ( !IS_NULLSTR(arg3) )
                snprintf( reason_buf, sizeof(reason_buf), "%s", arg3 );
            else
                snprintf( reason_buf, sizeof(reason_buf), "(no reason given)" );
            SET_BIT( target->mod_flags, MOD_BANNED );
            account_audit_log( "BAN", target->account_id,
                Format("actor=%s reason=%s", ch->name, reason_buf) );
            send_to_char( Format( "Account \tW%s\tn has been \tRbanned\tn.\n\r",
                target->name ), ch );
        }
        else
        {
            REMOVE_BIT( target->mod_flags, MOD_BANNED );
            account_audit_log( "UNBAN", target->account_id,
                Format("actor=%s", ch->name) );
            send_to_char( Format( "Account \tW%s\tn has been \tGunbanned\tn.\n\r",
                target->name ), ch );
        }

        target->dirty = TRUE;
        save_account( target );
        return;
    }

    /* ── transfer ───────────────────────────────────────────────────────── */
    if ( !str_cmp(arg1, "transfer") )
    {
        /* account transfer <charname> <from_acct> <to_acct> */
        ACCOUNT_DATA      *from_acct;
        ACCOUNT_DATA      *to_acct;
        ACCOUNT_CHARACTER *ac;
        ACCOUNT_CHARACTER *prev;
        char               char_name[MAX_INPUT_LENGTH];
        char               from_name[MAX_INPUT_LENGTH];
        char               to_name[MAX_INPUT_LENGTH];

        if ( trust < MASTER )
        {
            send_to_char( "You don't have permission to do that.\n\r", ch );
            return;
        }

        /* After initial parsing: arg1=transfer arg2=<charname> arg3=<from>
         * argument holds: <to_acct> */
        strncpy( char_name, arg2, sizeof(char_name) - 1 );
        strncpy( from_name, arg3, sizeof(from_name) - 1 );
        one_argument( argument, to_name );

        if ( IS_NULLSTR(char_name) || IS_NULLSTR(from_name) || IS_NULLSTR(to_name) )
        {
            send_to_char( "Syntax: account transfer <charname> <from_acct> <to_acct>\n\r", ch );
            return;
        }

        from_acct = acct_lookup_msg( ch, from_name );
        if ( !from_acct ) return;

        to_acct = acct_lookup_msg( ch, to_name );
        if ( !to_acct ) return;

        if ( IS_SET(from_acct->mod_flags, MOD_BANNED)
            || IS_SET(to_acct->mod_flags, MOD_BANNED) )
        {
            send_to_char( "Cannot transfer to or from a banned account.\n\r", ch );
            return;
        }

        if ( account_char_count(to_acct) >= account_max_slots(to_acct) )
        {
            send_to_char( "Destination account has no free character slots.\n\r", ch );
            return;
        }

        /* Find character in from_acct */
        prev = NULL;
        for ( ac = from_acct->characters; ac; ac = ac->next )
        {
            if ( !str_cmp(ac->char_name, char_name) )
                break;
            prev = ac;
        }

        if ( !ac )
        {
            send_to_char( Format( "Character '%s' not found on account '%s'.\n\r",
                char_name, from_acct->name ), ch );
            return;
        }

        /* Unlink from from_acct */
        if ( prev )
            prev->next = ac->next;
        else
            from_acct->characters = ac->next;
        ac->next = NULL;

        /* Link into to_acct */
        {
            ACCOUNT_CHARACTER *tail = to_acct->characters;
            if ( !tail )
                to_acct->characters = ac;
            else
            {
                while ( tail->next ) tail = tail->next;
                tail->next = ac;
            }
        }

        from_acct->dirty = TRUE;
        to_acct->dirty   = TRUE;
        save_account( from_acct );
        save_account( to_acct );

        /* Update character pfile */
        {
            CHAR_DATA *vch;
            bool       online = FALSE;

            /* Check if character is online */
            for ( vch = char_list; vch; vch = vch->next )
            {
                if ( !IS_NPC(vch) && !str_cmp(vch->name, char_name) )
                {
                    if ( vch->pcdata )
                    {
                        PURGE_DATA( vch->pcdata->acct_login );
                        vch->pcdata->acct_login = str_dup( to_acct->name );
                        vch->pcdata->acct_id    = to_acct->account_id;
                        save_char_obj( vch );
                    }
                    online = TRUE;
                    break;
                }
            }

            if ( !online )
            {
                /* Load pfile offline, update linkage, save, free */
                DESCRIPTOR_DATA tmp_d;
                memset( &tmp_d, 0, sizeof(tmp_d) );
                tmp_d.host = str_dup( "(transfer)" );
                if ( load_char_obj(&tmp_d, char_name, FALSE, FALSE, FALSE)
                    && tmp_d.character && tmp_d.character->pcdata )
                {
                    PURGE_DATA( tmp_d.character->pcdata->acct_login );
                    tmp_d.character->pcdata->acct_login = str_dup( to_acct->name );
                    tmp_d.character->pcdata->acct_id    = to_acct->account_id;
                    save_char_obj( tmp_d.character );
                    free_char( tmp_d.character );
                }
                PURGE_DATA( tmp_d.host );
                if ( tmp_d.outbuf ) free( tmp_d.outbuf );
            }
        }

        account_audit_log( "TRANSFER", to_acct->account_id,
            Format("char=%s from=%s to=%s actor=%s",
                char_name, from_acct->name, to_acct->name, ch->name) );

        send_to_char( Format(
            "Character \tW%s\tn transferred from \tW%s\tn to \tW%s\tn.\n\r",
            char_name, from_acct->name, to_acct->name ), ch );
        return;
    }

    /* ── reset password ─────────────────────────────────────────────────── */
    if ( !str_cmp(arg1, "reset") && !str_cmp(arg2, "password") )
    {
        char token[13];

        if ( trust < MASTER )
        {
            send_to_char( "You don't have permission to do that.\n\r", ch );
            return;
        }

        target = acct_lookup_msg( ch, arg3 );
        if ( !target ) return;

        generate_reset_token( token, sizeof(token) );

        PURGE_DATA( target->reset_token );
        target->reset_token            = str_dup( token );
        target->password_reset_pending = TRUE;
        target->reset_issued_on        = time( NULL );
        target->dirty                  = TRUE;
        save_account( target );

        account_audit_log( "PASSWORD_RESET_ISSUED", target->account_id,
            Format("actor=%s", ch->name) );

        send_to_char( Format(
            "Password reset issued for account \tW%s\tn.\n\r"
            "Reset token (deliver out-of-band): \tY%s\tn\n\r"
            "Token expires in 24 hours.\n\r",
            target->name, token ), ch );
        return;
    }

    /* ── purge ──────────────────────────────────────────────────────────── */
    if ( !str_cmp(arg1, "purge") )
    {
        if ( trust < MASTER )
        {
            send_to_char( "You don't have permission to do that.\n\r", ch );
            return;
        }
        send_to_char( "Running hard-delete scan — check log/account/orphan.log and the boot log for results.\n\r", ch );
        account_boot_scan();
        send_to_char( "Purge complete.\n\r", ch );
        return;
    }

    /* ══════════════════════════════════════════════════════════════════════
     * IMPLEMENTOR-ONLY COMMANDS — trust 5
     * ══════════════════════════════════════════════════════════════════════ */

    /* ── trust_ceiling ──────────────────────────────────────────────────── */
    if ( !str_cmp(arg1, "trust_ceiling") )
    {
        int new_ceil;

        if ( trust < IMPLEMENTOR )
        {
            send_to_char( "You don't have permission to do that.\n\r", ch );
            return;
        }

        if ( IS_NULLSTR(arg2) || IS_NULLSTR(arg3) )
        {
            send_to_char( "Syntax: account trust_ceiling <name> <level>\n\r"
                "  Level 5=IMPLEMENTOR, 4=MASTER, 3=STORYTELLER, 2=IMMORTAL, 0=player\n\r", ch );
            return;
        }

        target = acct_lookup_msg( ch, arg2 );
        if ( !target ) return;

        new_ceil = atoi( arg3 );
        if ( new_ceil < 0 || new_ceil > MAX_LEVEL )
        {
            send_to_char( Format( "Trust ceiling must be 0-%d.\n\r", MAX_LEVEL ), ch );
            return;
        }

        account_audit_log( "TRUST_CEILING", target->account_id,
            Format("old=%d new=%d actor=%s",
                target->trust_ceiling, new_ceil, ch->name) );

        target->trust_ceiling = new_ceil;
        target->dirty         = TRUE;
        save_account( target );

        send_to_char( Format(
            "Trust ceiling for account \tW%s\tn set to \tY%d\tn.\n\r",
            target->name, new_ceil ), ch );
        return;
    }

    /* ── admin_lock / admin_unlock ──────────────────────────────────────── */
    if ( !str_cmp(arg1, "admin_lock") || !str_cmp(arg1, "admin_unlock") )
    {
        bool locking = !str_cmp(arg1, "admin_lock");

        if ( trust < IMPLEMENTOR )
        {
            send_to_char( "You don't have permission to do that.\n\r", ch );
            return;
        }

        target = acct_lookup_msg( ch, arg2 );
        if ( !target ) return;

        if ( locking )
            SET_BIT( target->mod_flags, MOD_ADMIN_LOCKED );
        else
            REMOVE_BIT( target->mod_flags, MOD_ADMIN_LOCKED );

        target->dirty = TRUE;
        save_account( target );
        account_audit_log( locking ? "ADMIN_LOCK" : "ADMIN_UNLOCK", target->account_id,
            Format("actor=%s", ch->name) );
        send_to_char( Format( "Account \tW%s\tn is now %s.\n\r",
            target->name,
            locking ? "\tRadmin-locked\tn" : "\tGadmin-unlocked\tn" ), ch );
        return;
    }

    /* ── help / unknown ─────────────────────────────────────────────────── */
    send_to_char(
        "\tBAccount Commands\tn\n\r"
        "\tY--------------------------------------------------------------------\tn\n\r"
        "\tWPlayer:\tn\n\r"
        "  account info          account points\n\r"
        "  account ledger        account unlocks\n\r"
        "  account email                        (view email address)\n\r"
        "  account email set <address>          (set/update email address)\n\r"
        "  account email clear                  (remove email address)\n\r"
        "\tWStoryteller+:\tn\n\r"
        "  account info <name>   account note list <name>\n\r"
        "  account note add <name> <vis> <text>\n\r"
        "  account grant <name> <amount> <reason>\n\r"
        "  account freeze <name> / unfreeze <name>\n\r"
        "  account restrict <name> / unrestrict <name>\n\r"
        "  account email <name> [set <address>|clear]\n\r"
        "  account scan\n\r"
        "\tWMaster+:\tn\n\r"
        "  account ban <name> [reason]   account unban <name>\n\r"
        "  account transfer <char> <from> <to>\n\r"
        "  account reset password <name>\n\r"
        "  account purge\n\r"
        "\tWImplementor:\tn\n\r"
        "  account trust_ceiling <name> <level>\n\r"
        "  account admin_lock <name> / admin_unlock <name>\n\r", ch );
}
