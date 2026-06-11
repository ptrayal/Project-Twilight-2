/***************************************************************************
 * stock_events.c — Market News Event Broadcasts                          *
 *                                                                         *
 * Broadcasts flavor-text "market news" to all online players when a       *
 * stock price changes significantly. Minor fluctuations are ignored.      *
 *                                                                         *
 * HOW IT WORKS:                                                           *
 *   After any price change (tick-driven or player-driven), the function   *
 *   broadcast_stock_event() computes the percentage change. If the        *
 *   change is >= 5%, a random news message is selected from the           *
 *   appropriate tier and broadcast to all connected players.              *
 *                                                                         *
 * TIERS:                                                                  *
 *   Moderate (5-9%)   — calm analyst-speak                                *
 *   Notable  (10-19%) — excited or concerned tone                         *
 *   Dramatic (20%+)   — breaking news / crisis                            *
 *                                                                         *
 *   Each tier has separate arrays for RISE and FALL messages.             *
 *                                                                         *
 * HOW TO ADD/REMOVE MESSAGES:                                             *
 *   1. Find the array you want to modify (e.g. moderate_rise_msgs[])     *
 *   2. Add or remove a string entry. Each string must contain exactly    *
 *      one %s placeholder — it will be replaced with the company name.   *
 *   3. Update the corresponding #define count to match the new number    *
 *      of entries (e.g. NUM_MODERATE_RISE).                              *
 *   4. Recompile. That's it — no other files need to change.             *
 *                                                                         *
 * EXAMPLE — adding a new moderate rise message:                           *
 *   1. Add your string to moderate_rise_msgs[]:                          *
 *        "Trading volume for %s ticks upward as buyers enter the market."*
 *   2. Change NUM_MODERATE_RISE from 15 to 16.                           *
 *   3. Recompile.                                                         *
 *                                                                         *
 * EXAMPLE — removing a message:                                           *
 *   1. Delete the line from the array.                                   *
 *   2. Decrease the count #define by 1.                                  *
 *   3. Recompile.                                                         *
 *                                                                         *
 * Copyright (C) 2012 - 2026                                               *
 **************************************************************************/

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "twilight.h"

/* Forward declarations */
int number_range(int from, int to);

/*=========================================================================*
 * MESSAGE ARRAYS                                                          *
 *                                                                         *
 * Each message MUST contain exactly one %s — the company name.            *
 * Update the NUM_ define whenever you add or remove entries.              *
 *=========================================================================*/

/* --- MODERATE RISE (5-9% gain) — calm, analytical tone --- */
#define NUM_MODERATE_RISE 15
static const char *moderate_rise_msgs[NUM_MODERATE_RISE] = {
    "Financial analysts note steady gains in %s shares today.",
    "Investor confidence in %s appears to be growing.",
    "%s stock edges higher on moderate trading volume.",
    "Market watchers report a healthy uptick in %s shares.",
    "Shares of %s climb as the broader market holds steady.",
    "%s posts gains in today's trading session.",
    "Optimistic forecasts lift %s shares modestly.",
    "Quiet momentum builds behind %s as shares inch upward.",
    "A favorable earnings outlook nudges %s shares higher.",
    "Trading desks report steady buying interest in %s.",
    "%s shares gain ground in an otherwise flat market.",
    "Positive sentiment drives modest gains for %s stock.",
    "Fund managers increase positions in %s amid stable outlook.",
    "%s benefits from sector-wide tailwinds today.",
    "Gradual accumulation pushes %s shares to session highs."
};

/* --- NOTABLE RISE (10-19% gain) — excited, enthusiastic tone --- */
#define NUM_NOTABLE_RISE 15
static const char *notable_rise_msgs[NUM_NOTABLE_RISE] = {
    "Shares of %s surge on strong market performance!",
    "%s stock rallies as investors rush to buy in!",
    "Bulls charge ahead as %s posts impressive gains!",
    "%s shares jump sharply on heavy trading volume!",
    "Wall Street buzzes with excitement over %s's rally!",
    "Strong demand propels %s to new session highs!",
    "%s stock breaks out as momentum traders pile in!",
    "Analysts upgrade %s amid surging share price!",
    "A wave of buying pressure sends %s soaring!",
    "%s shares spike as short sellers scramble to cover!",
    "Institutional investors drive %s sharply higher!",
    "Market euphoria lifts %s on blockbuster trading!",
    "%s leads the market higher on a stellar session!",
    "Rumored deal sends %s shares jumping!",
    "Breakout alert: %s surges past key resistance levels!"
};

/* --- DRAMATIC RISE (20%+ gain) — breaking news, frenzy --- */
#define NUM_DRAMATIC_RISE 15
static const char *dramatic_rise_msgs[NUM_DRAMATIC_RISE] = {
    "BREAKING: %s stock skyrockets in an unprecedented rally!",
    "Market frenzy as %s shares explode in value!",
    "ALERT: %s posts historic gains as the market goes wild!",
    "%s shares go parabolic! Trading desks in disbelief!",
    "BREAKING: %s surges in what analysts call a once-in-a-decade move!",
    "Pandemonium on the trading floor as %s rockets upward!",
    "MARKET ALERT: %s shares in a vertical climb!",
    "%s stock erupts — regulators monitoring for unusual activity!",
    "FLASH: %s blasts through all-time highs on massive volume!",
    "The market roars as %s posts jaw-dropping gains!",
    "BREAKING: Unprecedented buying frenzy sends %s into orbit!",
    "History in the making as %s shares shatter expectations!",
    "Trading halted briefly on %s as shares surge beyond limits!",
    "%s mania sweeps the market — every investor wants in!",
    "URGENT: %s delivers staggering returns in a single session!"
};

/* --- MODERATE FALL (5-9% loss) — cautious, measured tone --- */
#define NUM_MODERATE_FALL 15
static const char *moderate_fall_msgs[NUM_MODERATE_FALL] = {
    "Shares of %s dip slightly amid cautious trading.",
    "%s stock sees modest losses as investors take profits.",
    "A soft session for %s as shares ease back.",
    "Light selling pressure pushes %s shares lower.",
    "%s gives back recent gains in quiet trading.",
    "Market caution weighs on %s shares today.",
    "%s drifts lower as traders await fresh catalysts.",
    "Profit-taking trims gains from %s stock.",
    "Mild headwinds drag %s shares down modestly.",
    "%s underperforms the broader market in today's session.",
    "Analysts note a cooling trend in %s share price.",
    "Reduced buying interest leaves %s slightly in the red.",
    "%s eases off highs amid mixed market signals.",
    "A handful of large sellers pressure %s lower.",
    "Subdued trading leaves %s nursing small losses."
};

/* --- NOTABLE FALL (10-19% loss) — concerned, urgent tone --- */
#define NUM_NOTABLE_FALL 15
static const char *notable_fall_msgs[NUM_NOTABLE_FALL] = {
    "%s shares tumble as market confidence wavers!",
    "Investors flee %s amid growing uncertainty!",
    "Heavy selling hammers %s stock lower!",
    "%s plunges as bearish sentiment takes hold!",
    "Red flags fly as %s drops sharply on the day!",
    "Panic selling grips %s as losses accelerate!",
    "%s stock slides amid a wave of negative sentiment!",
    "Analysts sound the alarm as %s continues its descent!",
    "A brutal sell-off sends %s reeling!",
    "%s hemorrhages value as investors head for the exits!",
    "Confidence crumbles as %s posts steep losses!",
    "Margin calls trigger additional selling in %s!",
    "%s drops hard — is this the start of something worse?",
    "Dark clouds gather over %s as the sell-off deepens!",
    "Fund managers slash %s positions amid deteriorating outlook!"
};

/* --- DRAMATIC FALL (20%+ loss) — crisis, breaking news --- */
#define NUM_DRAMATIC_FALL 15
static const char *dramatic_fall_msgs[NUM_DRAMATIC_FALL] = {
    "BREAKING: %s stock in freefall as panic selling spreads!",
    "MARKET CRISIS: %s shares plummet in catastrophic sell-off!",
    "ALERT: %s collapses — losses mount at an alarming rate!",
    "FLASH CRASH: %s nosedives in one of the worst sessions on record!",
    "BREAKING: Investors in shock as %s shares crater!",
    "Emergency meeting called as %s stock implodes!",
    "MARKET ALERT: %s in total meltdown — no buyers in sight!",
    "%s shares obliterated in devastating market rout!",
    "BREAKING: %s wipes out months of gains in a single session!",
    "Chaos on the trading floor as %s enters death spiral!",
    "URGENT: %s plunges — trading halted amid extreme volatility!",
    "Blood in the streets as %s shareholders face catastrophic losses!",
    "CRISIS: %s stock vaporizes as confidence evaporates entirely!",
    "Regulators step in as %s collapse threatens wider market!",
    "BREAKING: Historic crash — %s loses a staggering share of its value!"
};


/*=========================================================================*
 * broadcast_stock_event                                                   *
 *                                                                         *
 * Called after a stock price changes. Computes the percentage move and,   *
 * if significant (>= 5%), broadcasts a random flavor-text news message    *
 * to all connected players.                                               *
 *                                                                         *
 * Parameters:                                                             *
 *   stock    — the STOCKS entry that changed                              *
 *   old_cost — the price (in cents) BEFORE the change                     *
 *   new_cost — the price (in cents) AFTER the change                      *
 *=========================================================================*/
void broadcast_stock_event(STOCKS *stock, int old_cost, int new_cost)
{
    DESCRIPTOR_DATA *d;
    const char **msgs;
    int num_msgs;
    int change;
    int pct;
    const char *selected;
    char buf[MAX_STRING_LENGTH];

    if(old_cost <= 0 || new_cost <= 0)
        return;

    change = new_cost - old_cost;
    if(change == 0)
        return;

    /* Compute percentage change: abs(change) * 100 / old_cost */
    pct = abs(change) * 100 / old_cost;

    if(pct < 5)
        return;

    /* Select the appropriate message tier and direction */
    if(change > 0)
    {
        if(pct >= 20)
        {
            msgs = dramatic_rise_msgs;
            num_msgs = NUM_DRAMATIC_RISE;
        }
        else if(pct >= 10)
        {
            msgs = notable_rise_msgs;
            num_msgs = NUM_NOTABLE_RISE;
        }
        else
        {
            msgs = moderate_rise_msgs;
            num_msgs = NUM_MODERATE_RISE;
        }
    }
    else
    {
        if(pct >= 20)
        {
            msgs = dramatic_fall_msgs;
            num_msgs = NUM_DRAMATIC_FALL;
        }
        else if(pct >= 10)
        {
            msgs = notable_fall_msgs;
            num_msgs = NUM_NOTABLE_FALL;
        }
        else
        {
            msgs = moderate_fall_msgs;
            num_msgs = NUM_MODERATE_FALL;
        }
    }

    /* Pick a random message and format it with the company name */
    selected = msgs[number_range(0, num_msgs - 1)];
    snprintf(buf, sizeof(buf), "\tC[Market News]\tn ");
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), selected, stock->name);
    strncat(buf, "\n\r", sizeof(buf) - strlen(buf) - 1);

    /* Broadcast to all connected, playing characters */
    for(d = descriptor_list; d != NULL; d = d->next)
    {
        if(d->connected == CON_PLAYING && d->character != NULL)
        {
            send_to_char(buf, d->character);
        }
    }
}
