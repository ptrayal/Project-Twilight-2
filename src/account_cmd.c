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
        add_buf( buf, "  (no characters — use \tW[C]\tn to create one)\n\r" );

    add_buf( buf,
        "\tY====================================================================\tn\n\r" );

    if ( IS_SET(acct->mod_flags, MOD_FROZEN) )
    {
        add_buf( buf,
            "  \tD[P] Play a Character\tn       \tW[C]\tn Create New Character\n\r"
            "  \tW[U]\tn Unlock Features          \tW[A]\tn Account Information\n\r"
            "  \tW[L]\tn Point Ledger             \tW[Q]\tn Quit\n\r" );
    }
    else
    {
        add_buf( buf,
            "  \tW[P]\tn Play a Character         \tW[C]\tn Create New Character\n\r"
            "  \tW[U]\tn Unlock Features          \tW[A]\tn Account Information\n\r"
            "  \tW[L]\tn Point Ledger             \tW[Q]\tn Quit\n\r" );
    }

    add_buf( buf,
        "\tY====================================================================\tn\n\r"
        "Enter choice: " );

    write_to_buffer( d, buf_string(buf), 0 );
    free_buf( buf );
}

/* =========================================================================
 * show_account_unlocks — display the unlock store.
 * Implemented in Phase 16.
 * ========================================================================= */
void show_account_unlocks( DESCRIPTOR_DATA *d )
{
    if ( !d )
        return;

    write_to_buffer( d,
        "\tY====================================================================\tn\n\r"
        "\tB  UNLOCK STORE\tn\n\r"
        "\tY====================================================================\tn\n\r"
        "  Unlock store coming soon.\n\r"
        "\tY====================================================================\tn\n\r"
        "Enter choice: ", 0 );
}

/* =========================================================================
 * do_account — in-game account command dispatcher.
 * Implemented in Phase 17.
 * ========================================================================= */
void do_account( CHAR_DATA *ch, char *argument )
{
    ACCOUNT_DATA *acct;
    long          balance;

    if ( !ch || !ch->desc || !ch->desc->account )
    {
        send_to_char( "No account is attached to this session.\n\r", ch );
        return;
    }

    acct    = ch->desc->account;
    balance = acct->points_earned - acct->points_spent;

    if ( IS_NULLSTR(argument) || !str_cmp(argument, "info") )
    {
        send_to_char( Format(
            "\tBAccount Information\tn\n\r"
            "\tY--------------------------------------------------------------------\tn\n\r"
            "  \tWAccount  :\tn %s\n\r"
            "  \tWID       :\tn %ld\n\r"
            "  \tWBalance  :\tn %ld points\n\r"
            "  \tWEarned   :\tn %ld  \tWSpent:\tn %ld\n\r"
            "  \tWEmail    :\tn %s\n\r"
            "  \tWCreated  :\tn %s\n\r",
            acct->name,
            acct->account_id,
            balance,
            acct->points_earned, acct->points_spent,
            IS_NULLSTR(acct->email) ? "(not set)" : acct->email,
            ctime( &acct->created_on ) ), ch );
        return;
    }

    if ( !str_cmp(argument, "points") )
    {
        send_to_char( Format(
            "Account \tW%s\tn: \tG%ld\tn points available "
            "(%ld earned, %ld spent).\n\r",
            acct->name, balance,
            acct->points_earned, acct->points_spent ), ch );
        return;
    }

    send_to_char(
        "Account commands: \tWinfo\tn, \tWpoints\tn, \tWledger\tn, \tWunlocks\tn\n\r", ch );
}
