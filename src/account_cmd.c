/***************************************************************************
 * Much of the code is the original work of Peter Fitzgerald who turned    *
 * it over to Brandon Morrison who has adopted and improved the code.      *
 * Copyright (C) 2012 - 2024                                               *
 **************************************************************************/

/*
 * account_cmd.c — Account hub display and in-game account command.
 *
 * Phase 8 / Phase 17 stubs. Functions declared in account.h are stubbed
 * here so the project compiles cleanly during earlier phases.
 */

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "twilight.h"
#include "recycle.h"
#include "account.h"

/*
 * show_account_hub — display the account hub screen.
 * Implemented in Phase 8.
 */
void show_account_hub( DESCRIPTOR_DATA *d )
{
    if ( !d )
        return;

    write_to_buffer( d,
        "\n\r====================================================================\n\r"
        "                       PROJECT TWILIGHT\n\r"
        "====================================================================\n\r"
        "  Account hub coming soon.\n\r"
        "====================================================================\n\r"
        "Enter choice: ", 0 );
}

/*
 * show_account_unlocks — display the unlock store.
 * Implemented in Phase 16.
 */
void show_account_unlocks( DESCRIPTOR_DATA *d )
{
    if ( !d )
        return;

    write_to_buffer( d, "  Unlock store coming soon.\n\r", 0 );
}

/*
 * do_account — in-game account command dispatcher.
 * Implemented in Phase 17.
 */
void do_account( CHAR_DATA *ch, char *argument )
{
    if ( !ch || !ch->desc || !ch->desc->account )
    {
        send_to_char( "No account is attached to this session.\n\r", ch );
        return;
    }

    send_to_char( "Account command system coming soon.\n\r", ch );
}
