/***************************************************************************
 * Much of the code is the original work of Peter Fitzgerald who turned    *
 * it over to Brandon Morrison who has adopted and improved the code.      *
 * Copyright (C) 2012 - 2024                                               *
 **************************************************************************/

#include <sys/types.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "twilight.h"
#include "tables.h"
#include "olc.h"

RESEARCH_DATA *research_list;

/* Memory management function declarations */
RESEARCH_DATA *         new_research            args( (void) );
void                    free_research           args( (RESEARCH_DATA *research) );
RESEARCH_TIER *         new_research_tier       args( (void) );
void                    free_research_tier      args( (RESEARCH_TIER *tier) );
RESEARCH_MODIFIER *     new_research_modifier   args( (void) );
void                    free_research_modifier  args( (RESEARCH_MODIFIER *mod) );
RESEARCH_COOLDOWN *     new_research_cooldown   args( (void) );
void                    free_research_cooldown  args( (RESEARCH_COOLDOWN *cooldown) );

/* External table declarations */
extern const struct skill_list_entry ability_table[];
extern const struct attrib_type stat_table[];

/* Function declarations */
void                do_rtedit           args( (CHAR_DATA *ch, char *argument) );
RESEARCH_DATA *     find_research       args( (char *keyword) );
int                 lookup_ability      args( (const char *name) );
int                 lookup_stat         args( (const char *name) );
bool                check_cooldown      args( (CHAR_DATA *ch, char *keyword) );
void                set_cooldown        args( (CHAR_DATA *ch, char *keyword) );
int                 roll_wod_dice       args( (int dice_pool, int difficulty) );
void                attempt_research    args( (CHAR_DATA *ch, RESEARCH_DATA *research) );
int                 get_research_modifier args( (CHAR_DATA *ch, RESEARCH_DATA *research) );
RESEARCH_TIER *     get_tier_by_successes args( (RESEARCH_DATA *research, int successes) );
void                save_research_topics args( (void) );
void                load_research_topics args( (void) );
void                sort_research_list   args( (void) );
void                rtedit_interp        args( (CHAR_DATA *ch, char *argument) );

/* OLC helpers */
/* edit_done declared in olc.h (included above) */
extern void string_append( CHAR_DATA *ch, char **pString );

/*
 * Sort research_list alphabetically by title.
 * Uses simple insertion sort — fine for small lists.
 */
void sort_research_list( void )
{
    RESEARCH_DATA *sorted = NULL, *cur, *next, *ins;

    for(cur = research_list; cur != NULL; cur = next)
    {
        next = cur->next;
        cur->next = NULL;

        if(sorted == NULL || str_cmp(cur->title, sorted->title) < 0)
        {
            cur->next = sorted;
            sorted = cur;
        }
        else
        {
            for(ins = sorted; ins->next != NULL; ins = ins->next)
            {
                if(str_cmp(cur->title, ins->next->title) < 0)
                    break;
            }
            cur->next = ins->next;
            ins->next = cur;
        }
    }

    research_list = sorted;
}

/*
 * Check if a player has already discovered a hidden research topic.
 */
static bool has_discovered( CHAR_DATA *ch, const char *keywords )
{
    RESEARCH_COOLDOWN *disc;

    if(IS_NPC(ch) || !ch->pcdata)
        return FALSE;

    for(disc = ch->pcdata->research_discovered; disc != NULL; disc = disc->next)
    {
        if(!str_cmp(disc->keyword, keywords))
            return TRUE;
    }

    return FALSE;
}

/*
 * Record a hidden research topic as discovered by this player.
 */
static void mark_discovered( CHAR_DATA *ch, const char *keywords )
{
    RESEARCH_COOLDOWN *disc;

    disc = new_research_cooldown();
    disc->keyword = str_dup(keywords);
    disc->last_attempt = time(NULL);
    disc->next = ch->pcdata->research_discovered;
    ch->pcdata->research_discovered = disc;
}

/*
 * Find a research topic by keyword
 */
RESEARCH_DATA *find_research( char *keyword )
{
    RESEARCH_DATA *research;

    if ( !keyword || keyword[0] == '\0' )
        return NULL;

    for ( research = research_list; research; research = research->next )
    {
        if ( is_name( keyword, research->keywords ) )
            return research;
    }

    return NULL;
}

/*
 * Check if a character is on cooldown for a specific research keyword
 * Returns TRUE if on cooldown (cannot research), FALSE if available
 */
bool check_cooldown( CHAR_DATA *ch, char *keyword )
{
    RESEARCH_COOLDOWN *cooldown;
    time_t current_time;
    time_t cooldown_duration = 24 * 60 * 60; /* 24 hours in seconds */

    if ( IS_NPC(ch) || !ch->pcdata )
        return FALSE;

    current_time = time(NULL);

    for ( cooldown = ch->pcdata->research_cooldowns; cooldown; cooldown = cooldown->next )
    {
        if ( !str_cmp( cooldown->keyword, keyword ) )
        {
            if ( (current_time - cooldown->last_attempt) < cooldown_duration )
                return TRUE; /* Still on cooldown */
            else
                return FALSE; /* Cooldown expired */
        }
    }

    return FALSE; /* No cooldown found */
}

/*
 * Set or update cooldown for a research keyword
 */
void set_cooldown( CHAR_DATA *ch, char *keyword )
{
    RESEARCH_COOLDOWN *cooldown;
    time_t current_time;

    if ( IS_NPC(ch) || !ch->pcdata )
        return;

    current_time = time(NULL);

    /* Check if cooldown already exists */
    for ( cooldown = ch->pcdata->research_cooldowns; cooldown; cooldown = cooldown->next )
    {
        if ( !str_cmp( cooldown->keyword, keyword ) )
        {
            cooldown->last_attempt = current_time;
            return;
        }
    }

    /* Create new cooldown entry */
    cooldown = new_research_cooldown();
    cooldown->keyword = str_dup( keyword );
    cooldown->last_attempt = current_time;
    cooldown->next = ch->pcdata->research_cooldowns;
    ch->pcdata->research_cooldowns = cooldown;
}

/*
 * Roll dice using World of Darkness mechanics
 * Returns number of successes (accounting for 10s, 1s, and botches)
 */
int roll_wod_dice( int dice_pool, int difficulty )
{
    int i;
    int successes = 0;
    int ones = 0;
    int roll;

    if ( dice_pool <= 0 )
        return -999; /* Botch indicator for zero dice pool */

    if ( difficulty < 2 )
        difficulty = 2;
    if ( difficulty > 10 )
        difficulty = 10;

    for ( i = 0; i < dice_pool; i++ )
    {
        roll = number_range( 1, 10 );

        if ( roll == 10 )
            successes += 2; /* 10s count as 2 successes */
        else if ( roll >= difficulty )
            successes += 1; /* Regular success */
        else if ( roll == 1 )
            ones++; /* 1s subtract from successes */
    }

    /* Subtract 1s from successes */
    successes -= ones;

    /* Check for botch: successes <= 0 AND at least one 1 was rolled */
    if ( successes <= 0 && ones > 0 )
        return -1; /* Botch indicator */

    /* If successes went negative but no ones (shouldn't happen), return 0 */
    if ( successes < 0 )
        return 0;

    return successes;
}

/*
 * Get difficulty modifier based on character's clan/race/tribe
 */
int get_research_modifier( CHAR_DATA *ch, RESEARCH_DATA *research )
{
    RESEARCH_MODIFIER *mod;
    int adjustment = 0;

    if ( !research->modifiers )
        return 0;

    for ( mod = research->modifiers; mod; mod = mod->next )
    {
        if ( !str_cmp( mod->type, "clan" ) )
        {
            if ( ch->clan && !str_cmp( mod->value, clan_table[ch->clan].name ) )
                adjustment += mod->adjustment;
        }
        else if ( !str_cmp( mod->type, "race" ) )
        {
            if ( !str_cmp( mod->value, pc_race_table[ch->race].name ) )
                adjustment += mod->adjustment;
        }
        else if ( !str_cmp( mod->type, "pack" ) )
        {
            if ( ch->pack && !str_cmp( mod->value, ch->pack ) )
                adjustment += mod->adjustment;
        }
    }

    return adjustment;
}

/*
 * Find the appropriate tier based on number of successes
 * Returns the highest tier the character qualifies for
 */
RESEARCH_TIER *get_tier_by_successes( RESEARCH_DATA *research, int successes )
{
    RESEARCH_TIER *tier;
    RESEARCH_TIER *best_tier = NULL;

    if ( successes <= 0 )
        return NULL;

    for ( tier = research->tiers; tier; tier = tier->next )
    {
        if ( successes >= tier->successes_required )
        {
            if ( !best_tier || tier->successes_required > best_tier->successes_required )
                best_tier = tier;
        }
    }

    return best_tier;
}

/*
 * Main research attempt function
 */
void attempt_research( CHAR_DATA *ch, RESEARCH_DATA *research )
{
    int dice_pool;
    int difficulty;
    int successes;
    int stat_value;
    int ability_value;
    RESEARCH_TIER *tier;
    char buf[MAX_STRING_LENGTH];

    if ( IS_NPC(ch) )
    {
        send_to_char( "NPCs cannot research topics.\n\r", ch );
        return;
    }

    /* Get stat value */
    stat_value = get_curr_stat( ch, research->stat );

    /* Get ability value */
    if ( research->ability >= 0 && research->ability < MAX_ABIL )
        ability_value = ch->ability[research->ability].value;
    else
        ability_value = 0;

    /* Calculate dice pool */
    dice_pool = stat_value + ability_value;

    /* Calculate difficulty with modifiers */
    difficulty = research->base_difficulty + get_research_modifier( ch, research );

    /* Ensure difficulty is within valid range */
    if ( difficulty < 2 )
        difficulty = 2;
    if ( difficulty > 10 )
        difficulty = 10;

    /* Roll the dice */
    successes = roll_wod_dice( dice_pool, difficulty );

    /* Handle botch */
    if ( successes == -1 )
    {
        if ( research->failure_text && research->failure_text[0] != '\0' )
        {
            sprintf( buf, "You attempt to research %s, but your investigation goes horribly wrong!\n\r\n\r%s\n\r",
                          research->title, research->failure_text );
        }
        else
        {
            sprintf( buf, "You attempt to research %s, but your investigation goes horribly wrong!\n\r"
                          "You've uncovered misleading or dangerous information.\n\r",
                          research->title );
        }
        send_to_char( buf, ch );
        set_cooldown( ch, research->keywords );
        return;
    }

    /* Handle zero dice pool botch */
    if ( successes == -999 )
    {
        send_to_char( "You lack the knowledge or capability to research this topic.\n\r", ch );
        return;
    }

    /* Find appropriate tier */
    tier = get_tier_by_successes( research, successes );

    if ( !tier )
    {
        sprintf( buf, "You attempt to research %s, but fail to uncover any meaningful information.\n\r"
                      "(%d success%s - not enough to reveal anything)\n\r",
                      research->title, successes, successes == 1 ? "" : "es" );
        send_to_char( buf, ch );
        set_cooldown( ch, research->keywords );
        return;
    }

    /* Success! Display the information */
    sprintf( buf, "You successfully research %s! (%d success%s)\n\r\n\r%s\n\r",
                  research->title, successes, successes == 1 ? "" : "es",
                  tier->info_text );
    send_to_char( buf, ch );

    /* Award discovery XP for hidden topics (one-time only) */
    if ( research->hidden && RESEARCH_DISCOVERY_XP > 0
            && !has_discovered( ch, research->keywords ) )
    {
        mark_discovered( ch, research->keywords );
        ch->exp += RESEARCH_DISCOVERY_XP;
        send_to_char( Format( "\tGYou've uncovered hidden knowledge! (+%d XP)\tn\n\r",
                              RESEARCH_DISCOVERY_XP ), ch );
    }

    /* Set cooldown */
    set_cooldown( ch, research->keywords );
}

/*
 * Player command: research <topic>
 */
void do_research( CHAR_DATA *ch, char *argument )
{
    RESEARCH_DATA *research;
    char arg[MAX_INPUT_LENGTH];
    char buf[MAX_STRING_LENGTH];
    int time_remaining;
    RESEARCH_COOLDOWN *cooldown;

    if ( IS_NPC(ch) )
    {
        send_to_char( "NPCs cannot use the research system.\n\r", ch );
        return;
    }

    argument = one_argument( argument, arg );

    if ( arg[0] == '\0' )
    {
        send_to_char( "Research what topic? Use 'research list' to see available topics.\n\r", ch );
        return;
    }

    /* Handle 'research list' command */
    if ( !str_cmp( arg, "list" ) )
    {
        bool found_any = FALSE;

        if ( !research_list )
        {
            send_to_char( "There are no research topics available.\n\r", ch );
            return;
        }

        send_to_char( "\tB=== Available Research Topics ===\tn\n\r", ch );

        for ( research = research_list; research; research = research->next )
        {
            if ( research->hidden )
                continue;

            found_any = TRUE;
            sprintf( buf, "\tW%-20s\tn - %s\n\r", research->keywords, research->title );
            send_to_char( buf, ch );

            if ( check_cooldown( ch, research->keywords ) )
            {
                for ( cooldown = ch->pcdata->research_cooldowns; cooldown; cooldown = cooldown->next )
                {
                    if ( !str_cmp( cooldown->keyword, research->keywords ) )
                    {
                        time_remaining = (24 * 60 * 60) - (time(NULL) - cooldown->last_attempt);
                        sprintf( buf, "                     (On cooldown for %d more hours)\n\r",
                                      time_remaining / 3600 );
                        send_to_char( buf, ch );
                        break;
                    }
                }
            }
        }

        if ( !found_any )
            send_to_char( "There are no research topics available.\n\r", ch );

        return;
    }

    /* Find the research topic */
    research = find_research( arg );

    if ( !research )
    {
        send_to_char( "That research topic does not exist.\n\r", ch );
        return;
    }

    /* Check cooldown */
    if ( check_cooldown( ch, research->keywords ) )
    {
        /* Find the cooldown to get exact time remaining */
        for ( cooldown = ch->pcdata->research_cooldowns; cooldown; cooldown = cooldown->next )
        {
            if ( !str_cmp( cooldown->keyword, research->keywords ) )
            {
                time_remaining = (24 * 60 * 60) - (time(NULL) - cooldown->last_attempt);
                sprintf( buf, "You must wait %d more hour%s before researching '%s' again.\n\r",
                              time_remaining / 3600,
                              (time_remaining / 3600) == 1 ? "" : "s",
                              research->title );
                send_to_char( buf, ch );
                return;
            }
        }
    }

    /* Attempt the research */
    attempt_research( ch, research );
}

/*
 * Lookup ability by name (case-insensitive prefix match)
 */
int lookup_ability( const char *name )
{
    int i;

    for ( i = 0; i < MAX_ABIL; i++ )
    {
        if ( !str_prefix( name, ability_table[i].name ) )
            return i;
    }

    return -1;
}

/*
 * Lookup stat by name (case-insensitive prefix match)
 */
int lookup_stat( const char *name )
{
    int i;

    for ( i = 0; i < 9; i++ )
    {
        if ( !str_prefix( name, stat_table[i].name ) )
            return i;
    }

    return -1;
}

/*
 * Research Topic Editor (OLC-style command)
 * show_research_detail — Display all fields of a research topic.
 * Used by the OLC editor's show command and on entering the editor.
 */
static void show_research_detail( CHAR_DATA *ch, RESEARCH_DATA *research )
{
    RESEARCH_TIER *tier;
    RESEARCH_MODIFIER *mod;

    send_to_char( "\tB--------------------------< RESEARCH OLC >--------------------------\tn\n\r", ch );
    send_to_char( Format( "Title:       \tW%s\tn\n\r", research->title ), ch );
    send_to_char( Format( "Keywords:    \tW%s\tn\n\r", research->keywords ), ch );
    send_to_char( Format( "Stat:        \tW%s\tn\n\r",
                          research->stat >= 0 && research->stat < 9 ? stat_table[research->stat].name : "invalid" ), ch );
    send_to_char( Format( "Ability:     \tW%s\tn\n\r",
                          research->ability >= 0 && research->ability < MAX_ABIL ? ability_table[research->ability].name : "invalid" ), ch );
    send_to_char( Format( "Difficulty:  \tW%d\tn\n\r", research->base_difficulty ), ch );
    send_to_char( Format( "Hidden:      \tW%s\tn\n\r", research->hidden ? "Yes" : "No" ), ch );
    send_to_char( Format( "Tier Count:  \tW%d\tn\n\r", research->tier_count ), ch );

    if ( research->failure_text && research->failure_text[0] != '\0' )
    {
        send_to_char( "\n\r\tBFailure Text:\tn\n\r", ch );
        send_to_char( research->failure_text, ch );
        send_to_char( "\n\r", ch );
    }

    if ( research->modifiers )
    {
        send_to_char( "\n\r\tBModifiers:\tn\n\r", ch );
        for ( mod = research->modifiers; mod; mod = mod->next )
            send_to_char( Format( "  \tW%s\tn '%s': %+d\n\r", mod->type, mod->value, mod->adjustment ), ch );
    }

    if ( research->tiers )
    {
        send_to_char( "\n\r\tBTiers:\tn\n\r", ch );
        for ( tier = research->tiers; tier; tier = tier->next )
            send_to_char( Format( "  \tW%d\tn successes: %s\n\r", tier->successes_required, tier->info_text ), ch );
    }

    send_to_char( "\tOType 'done' to exit the editor.\tn\n\r", ch );
}

/*
 * rtedit_interp — OLC interpreter for the research topic editor.
 * Called when a player is in ED_RESEARCH mode.
 */
void rtedit_interp( CHAR_DATA *ch, char *argument )
{
    RESEARCH_DATA *research;
    char command[MAX_INPUT_LENGTH];
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    char arg[MAX_INPUT_LENGTH];
    RESEARCH_TIER *tier, *tier_prev;
    RESEARCH_MODIFIER *mod, *mod_prev;
    int value;

    research = (RESEARCH_DATA *)ch->desc->pEdit;

    if ( !research )
    {
        send_to_char( "Editor error: no research topic loaded.\n\r", ch );
        edit_done( ch );
        return;
    }

    smash_tilde( argument );
    strncpy( arg, argument, sizeof(arg) - 1 );
    arg[sizeof(arg) - 1] = '\0';
    argument = one_argument( argument, command );

    if ( !str_cmp( command, "done" ) )
    {
        save_research_topics();
        sort_research_list();
        edit_done( ch );
        return;
    }

    if ( command[0] == '\0' || !str_cmp( command, "show" ) )
    {
        show_research_detail( ch, research );
        return;
    }

    if ( !str_cmp( command, "title" ) )
    {
        if ( argument[0] == '\0' ) { send_to_char( "Syntax: title <text>\n\r", ch ); return; }
        PURGE_DATA( research->title );
        research->title = str_dup( argument );
        send_to_char( Format( "Title set to: %s\n\r", argument ), ch );
        return;
    }

    if ( !str_cmp( command, "keywords" ) )
    {
        if ( argument[0] == '\0' ) { send_to_char( "Syntax: keywords <text>\n\r", ch ); return; }
        PURGE_DATA( research->keywords );
        research->keywords = str_dup( argument );
        send_to_char( Format( "Keywords set to: %s\n\r", argument ), ch );
        return;
    }

    if ( !str_cmp( command, "stat" ) )
    {
        argument = one_argument( argument, arg1 );
        if ( arg1[0] == '\0' )
        {
            int i;
            send_to_char( "\tB=== Available Stats ===\tn\n\r", ch );
            for ( i = 0; i < 9; i++ )
                send_to_char( Format( "  \tW%-15s\tn %s\n\r", stat_table[i].name,
                    research->stat == i ? "\tG(SELECTED)\tn" : "" ), ch );
            return;
        }
        value = lookup_stat( arg1 );
        if ( value < 0 ) { send_to_char( "Invalid stat. Type 'stat' to see list.\n\r", ch ); return; }
        research->stat = value;
        send_to_char( Format( "Stat set to: %s\n\r", stat_table[value].name ), ch );
        return;
    }

    if ( !str_cmp( command, "ability" ) )
    {
        argument = one_argument( argument, arg1 );
        if ( arg1[0] == '\0' )
        {
            int i;
            send_to_char( "\tB=== Available Abilities ===\tn\n\r", ch );
            for ( i = 0; i < MAX_ABIL; i++ )
                if ( ability_table[i].name && ability_table[i].name[0] != '\0' )
                    send_to_char( Format( "  \tW%-20s\tn %s\n\r", ability_table[i].name,
                        research->ability == i ? "\tG(SELECTED)\tn" : "" ), ch );
            return;
        }
        value = lookup_ability( arg1 );
        if ( value < 0 ) { send_to_char( "Invalid ability. Type 'ability' to see list.\n\r", ch ); return; }
        research->ability = value;
        send_to_char( Format( "Ability set to: %s\n\r", ability_table[value].name ), ch );
        return;
    }

    if ( !str_cmp( command, "difficulty" ) )
    {
        argument = one_argument( argument, arg1 );
        if ( arg1[0] == '\0' || !is_number( arg1 ) ) { send_to_char( "Syntax: difficulty <2-10>\n\r", ch ); return; }
        value = atoi( arg1 );
        if ( value < 2 || value > 10 ) { send_to_char( "Difficulty must be 2-10.\n\r", ch ); return; }
        research->base_difficulty = value;
        send_to_char( Format( "Difficulty set to: %d\n\r", value ), ch );
        return;
    }

    if ( !str_cmp( command, "hidden" ) )
    {
        research->hidden = !research->hidden;
        send_to_char( Format( "Hidden set to: %s\n\r", research->hidden ? "Yes" : "No" ), ch );
        return;
    }

    if ( !str_cmp( command, "failure" ) )
    {
        if ( argument[0] == '\0' ) { send_to_char( "Syntax: failure <text>\n\r", ch ); return; }
        PURGE_DATA( research->failure_text );
        research->failure_text = str_dup( argument );
        send_to_char( "Failure text set.\n\r", ch );
        return;
    }

    if ( !str_cmp( command, "editfail" ) )
    {
        string_append( ch, &research->failure_text );
        return;
    }

    if ( !str_cmp( command, "addtier" ) )
    {
        argument = one_argument( argument, arg1 );
        if ( arg1[0] == '\0' || !is_number( arg1 ) || argument[0] == '\0' )
        { send_to_char( "Syntax: addtier <successes> <text>\n\r", ch ); return; }
        value = atoi( arg1 );
        for ( tier = research->tiers; tier; tier = tier->next )
            if ( tier->successes_required == value )
            { send_to_char( "Tier already exists. Use 'edittier' or 'deltier'.\n\r", ch ); return; }
        tier = new_research_tier();
        tier->successes_required = value;
        tier->info_text = str_dup( argument );
        tier->next = research->tiers;
        research->tiers = tier;
        research->tier_count++;
        send_to_char( Format( "Added tier: %d successes.\n\r", value ), ch );
        return;
    }

    if ( !str_cmp( command, "edittier" ) )
    {
        argument = one_argument( argument, arg1 );
        if ( arg1[0] == '\0' || !is_number( arg1 ) )
        { send_to_char( "Syntax: edittier <successes>\n\r", ch ); return; }
        value = atoi( arg1 );
        for ( tier = research->tiers; tier; tier = tier->next )
            if ( tier->successes_required == value )
            { string_append( ch, &tier->info_text ); return; }
        send_to_char( "Tier not found. Create it first with 'addtier'.\n\r", ch );
        return;
    }

    if ( !str_cmp( command, "deltier" ) )
    {
        argument = one_argument( argument, arg1 );
        if ( arg1[0] == '\0' || !is_number( arg1 ) )
        { send_to_char( "Syntax: deltier <successes>\n\r", ch ); return; }
        value = atoi( arg1 );
        tier_prev = NULL;
        for ( tier = research->tiers; tier; tier_prev = tier, tier = tier->next )
        {
            if ( tier->successes_required == value )
            {
                if ( tier_prev ) tier_prev->next = tier->next;
                else research->tiers = tier->next;
                free_research_tier( tier );
                research->tier_count--;
                send_to_char( Format( "Deleted tier: %d successes.\n\r", value ), ch );
                return;
            }
        }
        send_to_char( "Tier not found.\n\r", ch );
        return;
    }

    if ( !str_cmp( command, "addmod" ) )
    {
        argument = one_argument( argument, arg1 );
        argument = one_argument( argument, arg2 );
        if ( arg1[0] == '\0' || arg2[0] == '\0' || argument[0] == '\0' )
        { send_to_char( "Syntax: addmod <type> <value> <adjustment>\n\rType: clan, race, or pack\n\r", ch ); return; }
        if ( str_cmp( arg1, "clan" ) && str_cmp( arg1, "race" ) && str_cmp( arg1, "pack" ) )
        { send_to_char( "Type must be 'clan', 'race', or 'pack'.\n\r", ch ); return; }
        value = atoi( argument );
        mod = new_research_modifier();
        mod->type = str_dup( arg1 );
        mod->value = str_dup( arg2 );
        mod->adjustment = value;
        mod->next = research->modifiers;
        research->modifiers = mod;
        send_to_char( Format( "Added modifier: %s '%s' %+d\n\r", arg1, arg2, value ), ch );
        return;
    }

    if ( !str_cmp( command, "delmod" ) )
    {
        argument = one_argument( argument, arg1 );
        argument = one_argument( argument, arg2 );
        if ( arg1[0] == '\0' || arg2[0] == '\0' )
        { send_to_char( "Syntax: delmod <type> <value>\n\r", ch ); return; }
        mod_prev = NULL;
        for ( mod = research->modifiers; mod; mod_prev = mod, mod = mod->next )
        {
            if ( !str_cmp( mod->type, arg1 ) && !str_cmp( mod->value, arg2 ) )
            {
                if ( mod_prev ) mod_prev->next = mod->next;
                else research->modifiers = mod->next;
                free_research_modifier( mod );
                send_to_char( Format( "Deleted modifier: %s '%s'\n\r", arg1, arg2 ), ch );
                return;
            }
        }
        send_to_char( "Modifier not found.\n\r", ch );
        return;
    }

    if ( !str_cmp( command, "delete" ) )
    {
        RESEARCH_DATA *prev = NULL, *cur;
        for ( cur = research_list; cur; prev = cur, cur = cur->next )
        {
            if ( cur == research )
            {
                if ( prev ) prev->next = cur->next;
                else research_list = cur->next;
                break;
            }
        }
        send_to_char( "Research topic deleted.\n\r", ch );
        save_research_topics();
        ch->desc->pEdit = NULL;
        edit_done( ch );
        free_research( research );
        return;
    }

    interpret( ch, arg );
}

/*
 * do_rtedit — Entry point for the research topic OLC editor.
 *
 * Usage:
 *   rtedit list             — list all topics (outside editor)
 *   rtedit create <keyword> — create topic and enter editor
 *   rtedit <keyword>        — enter editor for existing topic
 */
void do_rtedit( CHAR_DATA *ch, char *argument )
{
    char arg1[MAX_INPUT_LENGTH];
    RESEARCH_DATA *research;

    if ( IS_NPC(ch) )
    {
        send_to_char( "NPCs cannot use the research editor.\n\r", ch );
        return;
    }

    argument = one_argument( argument, arg1 );

    if ( arg1[0] == '\0' )
    {
        send_to_char( "\tB=== Research Topic Editor ===\tn\n\r", ch );
        send_to_char( "  rtedit list             - List all research topics\n\r", ch );
        send_to_char( "  rtedit create \tW<keyword>\tn - Create new topic and enter editor\n\r", ch );
        send_to_char( "  rtedit \tW<keyword>\tn        - Edit existing topic\n\r", ch );
        return;
    }

    if ( !str_cmp( arg1, "save" ) )
    {
        save_research_topics();
        send_to_char( "Research topics saved.\n\r", ch );
        return;
    }

    if ( !str_cmp( arg1, "list" ) )
    {
        if ( !research_list )
        {
            send_to_char( "No research topics exist.\n\r", ch );
            return;
        }

        send_to_char( "\tB=== Research Topics ===\tn\n\r", ch );
        for ( research = research_list; research; research = research->next )
        {
            send_to_char( Format( "\tW%-20s\tn - %s\n\r", research->keywords, research->title ), ch );
            send_to_char( Format( "  Stat: %-12s  Ability: %-20s  Difficulty: %d  Tiers: %d\n\r",
                                  research->stat >= 0 && research->stat < 9 ? stat_table[research->stat].name : "invalid",
                                  research->ability >= 0 && research->ability < MAX_ABIL ? ability_table[research->ability].name : "invalid",
                                  research->base_difficulty, research->tier_count ), ch );
        }
        return;
    }

    /* Create command — create and enter editor */
    if ( !str_cmp( arg1, "create" ) )
    {
        char keyword[MAX_INPUT_LENGTH];

        argument = one_argument( argument, keyword );

        if ( keyword[0] == '\0' )
        {
            send_to_char( "Syntax: rtedit create <keyword>\n\r", ch );
            return;
        }

        if ( find_research( keyword ) )
        {
            send_to_char( "A research topic with that keyword already exists.\n\r", ch );
            return;
        }

        research = new_research();
        research->keywords = str_dup( keyword );
        research->title = str_dup( "New Research Topic" );
        research->stat = STAT_INT;
        research->ability = 0;
        research->base_difficulty = 6;
        research->tier_count = 0;
        research->next = research_list;
        research_list = research;
        sort_research_list();

        ch->desc->pEdit = (void *)research;
        ch->desc->editor = ED_RESEARCH;

        send_to_char( Format( "Created and editing research topic '%s'.\n\r", keyword ), ch );
        show_research_detail( ch, research );
        return;
    }

    /* Default: enter editor for existing topic */
    research = find_research( arg1 );
    if ( !research )
    {
        send_to_char( "Research topic not found. Use 'rtedit list' to see topics.\n\r", ch );
        return;
    }

    ch->desc->pEdit = (void *)research;
    ch->desc->editor = ED_RESEARCH;
    show_research_detail( ch, research );
}

/*
 * XML utility function to escape special characters
 */
char *xml_escape( const char *str )
{
    static char buf[MAX_STRING_LENGTH * 2];
    int i, j;

    if ( !str )
        return "";

    for ( i = 0, j = 0; str[i] != '\0' && j < (MAX_STRING_LENGTH * 2) - 10; i++ )
    {
        switch ( str[i] )
        {
            case '<':  buf[j++] = '&'; buf[j++] = 'l'; buf[j++] = 't'; buf[j++] = ';'; break;
            case '>':  buf[j++] = '&'; buf[j++] = 'g'; buf[j++] = 't'; buf[j++] = ';'; break;
            case '&':  buf[j++] = '&'; buf[j++] = 'a'; buf[j++] = 'm'; buf[j++] = 'p'; buf[j++] = ';'; break;
            case '"':  buf[j++] = '&'; buf[j++] = 'q'; buf[j++] = 'u'; buf[j++] = 'o'; buf[j++] = 't'; buf[j++] = ';'; break;
            case '\'': buf[j++] = '&'; buf[j++] = 'a'; buf[j++] = 'p'; buf[j++] = 'o'; buf[j++] = 's'; buf[j++] = ';'; break;
            default:   buf[j++] = str[i]; break;
        }
    }
    buf[j] = '\0';
    return buf;
}

/*
 * Save all research topics to XML file
 */
void save_research_topics( void )
{
    FILE *fp;
    RESEARCH_DATA *research;
    RESEARCH_TIER *tier;
    RESEARCH_MODIFIER *mod;

    fp = fopen( RESEARCH_FILE, "w" );
    if ( !fp )
    {
        log_string( LOG_ERR, "save_research_topics: Cannot open research.xml for writing." );
        return;
    }

    fprintf( fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" );
    fprintf( fp, "<research_topics>\n" );

    for ( research = research_list; research; research = research->next )
    {
        fprintf( fp, "  <topic>\n" );
        fprintf( fp, "    <title>%s</title>\n", xml_escape(research->title) );
        fprintf( fp, "    <keywords>%s</keywords>\n", xml_escape(research->keywords) );
        fprintf( fp, "    <stat>%d</stat>\n", research->stat );
        fprintf( fp, "    <ability>%d</ability>\n", research->ability );
        fprintf( fp, "    <base_difficulty>%d</base_difficulty>\n", research->base_difficulty );
        fprintf( fp, "    <tier_count>%d</tier_count>\n", research->tier_count );

        if ( research->hidden )
            fprintf( fp, "    <hidden>1</hidden>\n" );

        if ( research->failure_text && research->failure_text[0] != '\0' )
        {
            fprintf( fp, "    <failure_text>%s</failure_text>\n", xml_escape(research->failure_text) );
        }

        /* Save modifiers */
        if ( research->modifiers )
        {
            fprintf( fp, "    <modifiers>\n" );
            for ( mod = research->modifiers; mod; mod = mod->next )
            {
                fprintf( fp, "      <modifier>\n" );
                fprintf( fp, "        <type>%s</type>\n", xml_escape(mod->type) );
                fprintf( fp, "        <value>%s</value>\n", xml_escape(mod->value) );
                fprintf( fp, "        <adjustment>%d</adjustment>\n", mod->adjustment );
                fprintf( fp, "      </modifier>\n" );
            }
            fprintf( fp, "    </modifiers>\n" );
        }

        /* Save tiers */
        if ( research->tiers )
        {
            fprintf( fp, "    <tiers>\n" );
            for ( tier = research->tiers; tier; tier = tier->next )
            {
                fprintf( fp, "      <tier>\n" );
                fprintf( fp, "        <successes_required>%d</successes_required>\n", tier->successes_required );
                fprintf( fp, "        <info_text>%s</info_text>\n", xml_escape(tier->info_text) );
                fprintf( fp, "      </tier>\n" );
            }
            fprintf( fp, "    </tiers>\n" );
        }

        fprintf( fp, "  </topic>\n" );
    }

    fprintf( fp, "</research_topics>\n" );
    fclose( fp );

    log_string( LOG_GAME, "save_research_topics: Research topics saved successfully." );
}

/*
 * XML utility function to unescape special characters
 */
char *xml_unescape( const char *str )
{
    static char buf[MAX_STRING_LENGTH * 2];
    int i, j;

    if ( !str )
        return "";

    for ( i = 0, j = 0; str[i] != '\0' && j < (MAX_STRING_LENGTH * 2) - 1; i++ )
    {
        if ( str[i] == '&' )
        {
            if ( strncmp( &str[i], "&lt;", 4 ) == 0 )
            {
                buf[j++] = '<';
                i += 3;
            }
            else if ( strncmp( &str[i], "&gt;", 4 ) == 0 )
            {
                buf[j++] = '>';
                i += 3;
            }
            else if ( strncmp( &str[i], "&amp;", 5 ) == 0 )
            {
                buf[j++] = '&';
                i += 4;
            }
            else if ( strncmp( &str[i], "&quot;", 6 ) == 0 )
            {
                buf[j++] = '"';
                i += 5;
            }
            else if ( strncmp( &str[i], "&apos;", 6 ) == 0 )
            {
                buf[j++] = '\'';
                i += 5;
            }
            else
            {
                buf[j++] = str[i];
            }
        }
        else
        {
            buf[j++] = str[i];
        }
    }
    buf[j] = '\0';
    return buf;
}

/*
 * Simple XML tag reader
 */
char *read_xml_tag_content( FILE *fp, const char *tag_name )
{
    static char content[MAX_STRING_LENGTH];
    char line[MAX_STRING_LENGTH];
    char open_tag[128];
    char close_tag[128];
    int content_pos = 0;
    bool found_open = FALSE;

    sprintf( open_tag, "<%s>", tag_name );
    sprintf( close_tag, "</%s>", tag_name );

    content[0] = '\0';

    while ( fgets( line, sizeof(line), fp ) )
    {
        char *start = strstr( line, open_tag );
        char *end = strstr( line, close_tag );

        if ( start && end && start < end )
        {
            /* Single line tag */
            start += strlen(open_tag);
            *end = '\0';
            snprintf( content, sizeof(content), "%s", xml_unescape(start) );
            return content;
        }
        else if ( start )
        {
            /* Multi-line tag start */
            found_open = TRUE;
            start += strlen(open_tag);
            content_pos = snprintf( content, sizeof(content), "%s", start );
        }
        else if ( end && found_open )
        {
            /* Multi-line tag end */
            *end = '\0';
            if ( content_pos < MAX_STRING_LENGTH - 1 )
            {
                content_pos += snprintf( content + content_pos,
                                        sizeof(content) - content_pos,
                                        "%s", line );
            }
            return xml_unescape(content);
        }
        else if ( found_open )
        {
            /* Multi-line content */
            if ( content_pos < MAX_STRING_LENGTH - 1 )
            {
                content_pos += snprintf( content + content_pos,
                                        sizeof(content) - content_pos,
                                        "%s", line );
            }
        }
    }

    return NULL;
}

/*
 * Load all research topics from XML file
 */
void load_research_topics( void )
{
    FILE *fp;
    char line[MAX_STRING_LENGTH];
    RESEARCH_DATA *research = NULL;
    RESEARCH_TIER *tier = NULL;
    RESEARCH_MODIFIER *mod = NULL;
    char *content;
    bool in_topic = FALSE;
    bool in_modifiers = FALSE;
    bool in_modifier = FALSE;
    bool in_tiers = FALSE;
    bool in_tier = FALSE;

    fp = fopen( RESEARCH_FILE, "r" );
    if ( !fp )
    {
        log_string( LOG_ERR, "load_research_topics: No research.xml file found. Starting with empty list." );
        research_list = NULL;
        return;
    }

    research_list = NULL;

    while ( fgets( line, sizeof(line), fp ) )
    {
        /* Topic start/end */
        if ( strstr( line, "<topic>" ) )
        {
            research = new_research();
            in_topic = TRUE;
        }
        else if ( strstr( line, "</topic>" ) )
        {
            if ( research )
            {
                research->next = research_list;
                research_list = research;
                research = NULL;
            }
            in_topic = FALSE;
        }
        /* Modifiers section start/end */
        else if ( strstr( line, "<modifiers>" ) )
        {
            in_modifiers = TRUE;
        }
        else if ( strstr( line, "</modifiers>" ) )
        {
            in_modifiers = FALSE;
        }
        /* Modifier start/end */
        else if ( strstr( line, "<modifier>" ) && in_modifiers )
        {
            mod = new_research_modifier();
            in_modifier = TRUE;
        }
        else if ( strstr( line, "</modifier>" ) && in_modifier )
        {
            if ( mod && research )
            {
                mod->next = research->modifiers;
                research->modifiers = mod;
                mod = NULL;
            }
            in_modifier = FALSE;
        }
        /* Tiers section start/end */
        else if ( strstr( line, "<tiers>" ) )
        {
            in_tiers = TRUE;
        }
        else if ( strstr( line, "</tiers>" ) )
        {
            in_tiers = FALSE;
        }
        /* Tier start/end */
        else if ( strstr( line, "<tier>" ) && in_tiers )
        {
            tier = new_research_tier();
            in_tier = TRUE;
        }
        else if ( strstr( line, "</tier>" ) && in_tier )
        {
            if ( tier && research )
            {
                tier->next = research->tiers;
                research->tiers = tier;
                tier = NULL;
            }
            in_tier = FALSE;
        }
        /* Read content tags */
        else if ( in_topic && !in_modifiers && !in_tiers && research )
        {
            if ( (content = strstr( line, "<title>" )) != NULL )
            {
                fseek( fp, -(long)strlen(line), SEEK_CUR );
                content = read_xml_tag_content( fp, "title" );
                if ( content )
                    research->title = str_dup( content );
            }
            else if ( (content = strstr( line, "<keywords>" )) != NULL )
            {
                fseek( fp, -(long)strlen(line), SEEK_CUR );
                content = read_xml_tag_content( fp, "keywords" );
                if ( content )
                    research->keywords = str_dup( content );
            }
            else if ( (content = strstr( line, "<stat>" )) != NULL )
            {
                fseek( fp, -(long)strlen(line), SEEK_CUR );
                content = read_xml_tag_content( fp, "stat" );
                if ( content )
                    research->stat = atoi( content );
            }
            else if ( (content = strstr( line, "<ability>" )) != NULL )
            {
                fseek( fp, -(long)strlen(line), SEEK_CUR );
                content = read_xml_tag_content( fp, "ability" );
                if ( content )
                    research->ability = atoi( content );
            }
            else if ( (content = strstr( line, "<base_difficulty>" )) != NULL )
            {
                fseek( fp, -(long)strlen(line), SEEK_CUR );
                content = read_xml_tag_content( fp, "base_difficulty" );
                if ( content )
                    research->base_difficulty = atoi( content );
            }
            else if ( (content = strstr( line, "<tier_count>" )) != NULL )
            {
                fseek( fp, -(long)strlen(line), SEEK_CUR );
                content = read_xml_tag_content( fp, "tier_count" );
                if ( content )
                    research->tier_count = atoi( content );
            }
            else if ( (content = strstr( line, "<hidden>" )) != NULL )
            {
                fseek( fp, -(long)strlen(line), SEEK_CUR );
                content = read_xml_tag_content( fp, "hidden" );
                if ( content )
                    research->hidden = atoi( content );
            }
            else if ( (content = strstr( line, "<failure_text>" )) != NULL )
            {
                fseek( fp, -(long)strlen(line), SEEK_CUR );
                content = read_xml_tag_content( fp, "failure_text" );
                if ( content )
                    research->failure_text = str_dup( content );
            }
        }
        else if ( in_modifier && mod )
        {
            if ( (content = strstr( line, "<type>" )) != NULL )
            {
                fseek( fp, -(long)strlen(line), SEEK_CUR );
                content = read_xml_tag_content( fp, "type" );
                if ( content )
                    mod->type = str_dup( content );
            }
            else if ( (content = strstr( line, "<value>" )) != NULL )
            {
                fseek( fp, -(long)strlen(line), SEEK_CUR );
                content = read_xml_tag_content( fp, "value" );
                if ( content )
                    mod->value = str_dup( content );
            }
            else if ( (content = strstr( line, "<adjustment>" )) != NULL )
            {
                fseek( fp, -(long)strlen(line), SEEK_CUR );
                content = read_xml_tag_content( fp, "adjustment" );
                if ( content )
                    mod->adjustment = atoi( content );
            }
        }
        else if ( in_tier && tier )
        {
            if ( (content = strstr( line, "<successes_required>" )) != NULL )
            {
                fseek( fp, -(long)strlen(line), SEEK_CUR );
                content = read_xml_tag_content( fp, "successes_required" );
                if ( content )
                    tier->successes_required = atoi( content );
            }
            else if ( (content = strstr( line, "<info_text>" )) != NULL )
            {
                fseek( fp, -(long)strlen(line), SEEK_CUR );
                content = read_xml_tag_content( fp, "info_text" );
                if ( content )
                    tier->info_text = str_dup( content );
            }
        }
    }

    fclose( fp );
    sort_research_list();
    log_string( LOG_GAME, "load_research_topics: Research topics loaded successfully." );
}
