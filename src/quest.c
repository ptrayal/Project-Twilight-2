/***************************************************************************
 * Much of the code is the original work of Peter Fitzgerald who turned    *
 * it over to Brandon Morrison who has adopted and improved the code.      *
 * Copyright (C) 2012 - 2024                                               *
 **************************************************************************/

#include <sys/types.h>
#include <time.h>
#include "twilight.h"
#include "tables.h"

QUEST_DATA *quest_list;

DECLARE_DO_FUN( do_at		);

char *format_obj_to_char( OBJ_DATA *obj, CHAR_DATA *ch, bool fShort );

DECLARE_QUEST_FUN( quest_none		);
DECLARE_QUEST_FUN( quest_courier	);
DECLARE_QUEST_FUN( quest_hitman		);
DECLARE_QUEST_FUN( quest_thief		);
DECLARE_QUEST_FUN( quest_bodyguard	);
DECLARE_QUEST_FUN( quest_guard		);
DECLARE_QUEST_FUN( quest_rescue		);

/* Quest recycling functions */
QUEST_DATA *	new_quest  args( () );
void		free_quest args( (QUEST_DATA * quest) );

/* Description generator. */
void		desc_gen   args( (CHAR_DATA * ch) );

const   struct  qt_type  quest_table[] =
{
		/* { time_offset, quest_function }, */
		{	0,	quest_none	},
		{	0,	quest_courier	},
		{	0,	quest_hitman	},
		{	0,	quest_thief	},
		{	0,	quest_bodyguard	},
		{	0,	quest_guard	},
		{	0,	quest_rescue	}
};

CHAR_DATA * evil_twin (CHAR_DATA *ch)
{
    CHAR_DATA *twin = create_mobile(get_mob_index(MOB_VNUM_BLANKY));
    int i = 0;

    twin->name		= str_dup(ch->name);
    twin->short_descr	= str_dup(ch->name);
    twin->act		= ACT_TWIN | ch->act;
    twin->act2		= ch->act2;
    for(i = 0; i < MAX_STATS; i++)
    {
        twin->perm_stat[i]	= ch->perm_stat[i];
    }
    for(i = 0; i < MAX_STATS; i++)
    {
        twin->mod_stat[i]	= ch->mod_stat[i];
    }
    twin->shape		= ch->shape;
    twin->blood_timer	= 10;
    twin->material	= ch->material;
    twin->race		= ch->race;
    twin->clan		= ch->clan;
    twin->size		= ch->size;
    twin->dam_type	= ch->dam_type;
    twin->affected_by	= ch->affected_by;
    twin->affected_by2	= ch->affected_by2;
    twin->imm_flags	= ch->imm_flags;
    twin->vuln_flags	= ch->vuln_flags;
    twin->res_flags	= ch->res_flags;
    twin->form		= ch->form;
    twin->parts		= ch->parts;
    for(i = 0; i < MAX_ABIL; i++)
    {
        twin->ability[i].value = ch->ability[i].value;
    }
    for(i = 0; i < MAX_DISC; i++)
    {
        twin->disc[i]	= ch->disc[i];
    }
    twin->health	= MAX_HEALTH;
    twin->agghealth	= MAX_HEALTH;
    twin->position	= P_STAND;
    twin->in_room	= get_room_index(10000);
    twin->RBPG		= ch->max_RBPG;
    twin->max_RBPG	= ch->max_RBPG;
    twin->GHB		= ch->max_GHB;
    twin->max_GHB	= ch->max_GHB;
    twin->willpower	= ch->max_willpower;
    twin->max_willpower = ch->max_willpower;
    twin->saving_throw	= ch->saving_throw;
    twin->sex		= ch->sex;
    twin->trust		= ch->trust;
    for(i = 0; i < (MAX_VIRTUES - 1); i++)
    {
        twin->virtues[i] = ch->virtues[i];
    }
    desc_gen(twin);

    return twin;
}

/* Mob should not be able to be a PC. */
CHAR_DATA * get_random_mob(CHAR_DATA *ch)
{
    CHAR_DATA *mob = NULL;
    int count = 0;
    int target, safety;

    /* Count valid NPCs first */
    for(mob = char_list; mob != NULL; mob = mob->next)
    {
        if(IS_NPC(mob) && mob != ch && mob->fighting == NULL
                && SAME_PLANE(ch, mob) && !IS_ANIMAL(mob))
            count++;
    }

    if(count == 0)
        return NULL;

    /* Pick a random one */
    target = number_range(0, count - 1);
    safety = 0;

    for(mob = char_list; mob != NULL; mob = mob->next)
    {
        if(IS_NPC(mob) && mob != ch && mob->fighting == NULL
                && SAME_PLANE(ch, mob) && !IS_ANIMAL(mob))
        {
            if(safety == target)
                return mob;
            safety++;
        }
    }

    return NULL;
}

OBJ_DATA * get_random_obj()
{
	OBJ_INDEX_DATA *pobj = NULL;
	OBJ_DATA *obj = NULL;
	int count = 0;
	int target, i;

	for(obj = object_list; obj != NULL; obj = obj->next)
		count++;

	if(count == 0)
		return NULL;

	target = number_range(0, count - 1);
	i = 0;

	for(obj = object_list; obj != NULL; obj = obj->next)
	{
		if(i == target)
			break;
		i++;
	}

	if(obj == NULL || obj->pIndexData == NULL)
		return NULL;

	pobj = obj->pIndexData;
	obj = create_object(pobj);

	return obj;
}

ROOM_INDEX_DATA * random_room()
{
	ROOM_INDEX_DATA *rm = NULL;
	int count = 0;
	int target, i;

	for(rm = get_room_index(10000); rm != NULL; rm = rm->next)
	{
		if(rm->vnum > 15 && !IS_SET(rm->room_flags, ROOM_NO_RECALL)
				&& !IS_SET(rm->room_flags, ROOM_IMP_ONLY)
				&& !IS_SET(rm->room_flags, ROOM_ADMIN)
				&& !IS_SET(rm->room_flags, ROOM_NOWHERE))
			count++;
	}

	if(count == 0)
		return NULL;

	target = number_range(0, count - 1);
	i = 0;

	for(rm = get_room_index(10000); rm != NULL; rm = rm->next)
	{
		if(rm->vnum > 15 && !IS_SET(rm->room_flags, ROOM_NO_RECALL)
				&& !IS_SET(rm->room_flags, ROOM_IMP_ONLY)
				&& !IS_SET(rm->room_flags, ROOM_ADMIN)
				&& !IS_SET(rm->room_flags, ROOM_NOWHERE))
		{
			if(i == target)
				return rm;
			i++;
		}
	}

	return NULL;
}

/* @@@@@ In some cases the object should not be random. */
OBJ_DATA * gen_quest_object()
{
	return get_random_obj();
}

CHAR_DATA * get_questor()
{
	CHAR_DATA *ch;

	for(ch = char_list; ch != NULL; ch = ch->next)
	{
		if(!IS_NPC(ch) && ch->quest == NULL
				&& IS_SET(ch->act2, ACT2_RP_ING) && number_range(0, 100) == 50)
			return ch;
	}

	return NULL;
}

long gen_quest_flags()
{
	int num = 0;
	long result = 0;

	if((num = number_percent()) < 30)
		result = Q_EASY;
	else if(num > 70)
		result = Q_HARD;

	if((num = number_percent()) < 20)
		result = result|Q_ARREST;

	if((num = number_percent()) < 10 && !IS_SET(result, Q_HARD))
		result = result|Q_SAFE;
	else if(num > 80 && !IS_SET(result, Q_EASY))
		result = result|Q_DEADLY;

	if((num = number_percent()) < 30 && !IS_SET(result, Q_EASY))
		result = result|Q_PUZZLE;

	if((num = number_percent()) < 15 && !IS_SET(result, Q_EASY) && !IS_SET(result, Q_SAFE))
		result = result|Q_HUNTER;

	return result;
}

/* @@@@@ Need to finish these two functions.
 * In some cases, the mob should not be entirely random.
 * It is possible for duplicates (mob = questor for instance)
 */
CHAR_DATA * gen_quest_victim(QUEST_DATA *pq)
{
	CHAR_DATA *mob;
	int safety = 0;

	mob = get_random_mob(pq->questor);
	if(mob == NULL)
		return NULL;

	while((IS_SET(mob->act2, ACT2_NOQUEST) || IS_SET(mob->act2, ACT2_NOQUESTVICT))
			&& safety < 100)
	{
		mob = get_random_mob(pq->questor);
		if(mob == NULL)
			return NULL;
		safety++;
	}

	if(safety >= 100)
		return NULL;

	return mob;
}

CHAR_DATA * gen_quest_aggressor(QUEST_DATA *pq)
{
	CHAR_DATA *mob;
	int safety = 0;

	mob = get_random_mob(pq->questor);
	if(mob == NULL)
		return NULL;

	while((IS_SET(mob->act2, ACT2_NOQUEST) || IS_SET(mob->act2, ACT2_NOQUESTAGGR))
			&& safety < 100)
	{
		mob = get_random_mob(pq->questor);
		if(mob == NULL)
			return NULL;
		safety++;
	}

	if(safety >= 100)
		return NULL;

	return mob;
}

int gen_time_limit(QUEST_DATA *q, int fAccepted)
{
	int result = 0;

	if(q == NULL)
		return -1;

	if(!fAccepted)
	{
		result = number_range(15, 30);
		return result;
	}
	else
	{
		result = 3000 + 10*quest_table[q->quest_type].time_offset;
		if(IS_SET(q->quest_flags, Q_EASY))
			result += 1500;
		if(IS_SET(q->quest_flags, Q_HARD))
			result -= 600;
		if(IS_SET(q->quest_flags, Q_SAFE))
			result -= 1000;
		if(IS_SET(q->quest_flags, Q_DEADLY))
			result += 500;
		if(IS_SET(q->quest_flags, Q_ARREST))
			result -= 1000;
		if(IS_SET(q->quest_flags, Q_PUZZLE))
			result += 3000;
		if(IS_SET(q->quest_flags, Q_HUNTER))
			result -= 1500;
		return result;
	}
	return 100;
}

void gen_quest (bool forced, CHAR_DATA *vch)
{
	QUEST_DATA *pquest;

	pquest = new_quest();

	if(!forced)
	{
		if((pquest->questor = get_questor()) == NULL)
		{
			free_quest(pquest);
			return;
		}
	}
	else
	{
		pquest->questor = vch;
	}

	pquest->quest_type = number_range(0, MAX_QUESTS - 1);

	pquest->quest_flags = gen_quest_flags();

	pquest->victim = gen_quest_victim(pquest);
	if(pquest->victim == NULL || pquest->victim == pquest->questor)
	{
		free_quest(pquest);
		return;
	}

	pquest->aggressor = gen_quest_aggressor(pquest);
	if(pquest->aggressor == NULL
			|| pquest->aggressor == pquest->questor
			|| pquest->aggressor == pquest->victim)
	{
		free_quest(pquest);
		return;
	}

	pquest->aggressor->quest = pquest;
	pquest->victim->quest = pquest;

	pquest->time_limit = gen_time_limit(pquest, 0);
	pquest->obj = gen_quest_object();
	pquest->questor->quest = pquest;

	LINK_SINGLE(pquest, next, quest_list);

	(*quest_table[pquest->quest_type].q_fun) (pquest->questor, 0);
}

/*
 * Quest type names for display.
 * Keep in sync with Q_NONE..Q_RESCUE defines in twilight.h.
 */
static const char *quest_type_name(int type)
{
    switch(type)
    {
        case Q_COURIER:   return "Courier";
        case Q_HITMAN:    return "Hitman";
        case Q_THIEF:     return "Theft";
        case Q_BODYGUARD: return "Bodyguard";
        case Q_GUARD:     return "Guard";
        case Q_RESCUE:    return "Rescue";
        default:          return "Unknown";
    }
}

/*
 * Quest difficulty label for display.
 */
static const char *quest_difficulty_label(long flags)
{
    if(IS_SET(flags, Q_DEADLY)) return "\tRDeadly\tn";
    if(IS_SET(flags, Q_HARD))   return "\tOHard\tn";
    if(IS_SET(flags, Q_EASY))   return "\tGEasy\tn";
    return "\tYNormal\tn";
}

/*
 * quest_reward — Award XP and cash based on quest difficulty flags.
 *
 * Reward scaling:
 *   Q_EASY:   1 XP,  $5
 *   Normal:   2 XP, $10
 *   Q_HARD:   4 XP, $25
 *   Q_DEADLY: 6 XP, $50
 *
 * Bonuses (additive):
 *   Q_PUZZLE: +1 XP (quest required extra thinking)
 *   Q_HUNTER: +1 XP (quest put the player at risk of being hunted)
 *
 * HOW TO EXPAND:
 *   - To add item rewards, create an item table and roll against it here.
 *     Example: if Q_DEADLY, number_percent() < 10 → give rare item from a
 *     reward_item_table[] using create_object/obj_to_char.
 *   - To add influence rewards, modify ch->influences[INFL_XXX] here.
 *   - To add reputation or faction rewards, add the logic after the cash block.
 *   - To scale rewards by quest type (not just difficulty), switch on
 *     ch->quest->quest_type before applying multipliers.
 */
QUEST_LOG_ENTRY *new_quest_log_entry args( (void) );
void free_quest_log_entry args( (QUEST_LOG_ENTRY *entry) );

/*
 * add_quest_log — Record a quest completion/failure/abandon in the player's log.
 * Trims oldest entries when MAX_QUEST_LOG is exceeded.
 */
static void add_quest_log(CHAR_DATA *ch, int result, int xp, int cash)
{
    QUEST_LOG_ENTRY *entry, *cur;
    int count;

    if(IS_NPC(ch) || !ch->pcdata || !ch->quest)
        return;

    entry = new_quest_log_entry();
    if(!entry) return;

    entry->quest_type   = ch->quest->quest_type;
    entry->quest_flags  = ch->quest->quest_flags;
    entry->result       = result;
    entry->xp_earned    = xp;
    entry->cash_earned  = cash;
    entry->completed_on = time(NULL);

    if(ch->quest->aggressor != NULL)
        entry->employer = str_dup(IS_NPC(ch->quest->aggressor)
            ? ch->quest->aggressor->short_descr : ch->quest->aggressor->name);
    if(ch->quest->victim != NULL)
        entry->target = str_dup(IS_NPC(ch->quest->victim)
            ? ch->quest->victim->short_descr : ch->quest->victim->name);

    entry->next         = ch->pcdata->quest_log;
    ch->pcdata->quest_log = entry;

    /* Trim oldest entries if over MAX_QUEST_LOG */
    count = 0;
    for(cur = ch->pcdata->quest_log; cur != NULL; cur = cur->next)
    {
        count++;
        if(count == MAX_QUEST_LOG && cur->next != NULL)
        {
            QUEST_LOG_ENTRY *old = cur->next;
            cur->next = NULL;
            while(old)
            {
                QUEST_LOG_ENTRY *tmp = old->next;
                free_quest_log_entry(old);
                old = tmp;
            }
            break;
        }
    }
}

void quest_reward(CHAR_DATA *ch)
{
    int xp = 2;
    int cash = 10;
    long flags;

    if(ch->quest == NULL)
        return;

    flags = ch->quest->quest_flags;

    /* Base rewards by difficulty */
    if(IS_SET(flags, Q_EASY))        { xp = 1;  cash = 5;  }
    else if(IS_SET(flags, Q_DEADLY)) { xp = 6;  cash = 50; }
    else if(IS_SET(flags, Q_HARD))   { xp = 4;  cash = 25; }

    /* Bonus XP for special conditions */
    if(IS_SET(flags, Q_PUZZLE)) xp += 1;
    if(IS_SET(flags, Q_HUNTER)) xp += 1;

    /* Apply rewards */
    ch->exp += xp;
    ch->dollars += cash;

    send_to_char(Format("\tGReward:\tn %d XP and $%d.\n\r", xp, cash), ch);

    add_quest_log(ch, 2, xp, cash);

    /*
     * RARE ITEM REWARD HOOK (currently disabled)
     *
     * To enable rare item drops on Q_DEADLY quests, uncomment and
     * populate a reward item vnum table:
     *
     * if(IS_SET(flags, Q_DEADLY) && number_percent() < 10)
     * {
     *     int reward_vnums[] = { 1234, 5678, 9012 };
     *     int idx = number_range(0, 2);
     *     OBJ_DATA *reward = create_object(get_obj_index(reward_vnums[idx]));
     *     if(reward)
     *     {
     *         obj_to_char(reward, ch);
     *         act("You receive $p as a bonus!", ch, reward, NULL, TO_CHAR, 1);
     *     }
     * }
     */
}

/*
 * do_quest — Player command to view quest status.
 *
 * Usage:
 *   quest          — Show current quest status
 *   quest status   — Same as above
 *
 * HOW TO EXPAND:
 *   Add new subcommands here (e.g., "quest log", "quest abandon").
 */
void do_quest(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];

    CheckCH(ch);

    one_argument(argument, arg);

    if(arg[0] != '\0' && !str_prefix(arg, "rescue"))
    {
        if(ch->quest == NULL || ch->quest->quest_type != Q_RESCUE || ch->quest->state < 1)
        {
            send_to_char("You're not on a rescue mission.\n\r", ch);
            return;
        }

        if(ch->quest->victim == NULL)
        {
            send_to_char("The person you were looking for is gone.\n\r", ch);
            quest_none(ch, 0);
            return;
        }

        if(ch->in_room != ch->quest->victim->in_room)
        {
            send_to_char("The person you're looking for isn't here.\n\r", ch);
            return;
        }

        (*quest_table[ch->quest->quest_type].q_fun)(ch, 2);
        return;
    }

    if(arg[0] != '\0' && !str_prefix(arg, "abandon"))
    {
        if(ch->quest == NULL)
        {
            send_to_char("You are not currently on a quest.\n\r", ch);
            return;
        }

        send_to_char("\tR[Quest Abandoned]\tn You walk away from the job.\n\r", ch);
        add_quest_log(ch, -1, 0, 0);
        if(ch->quest->obj != NULL)
        {
            extract_obj(ch->quest->obj);
            ch->quest->obj = NULL;
        }
        quest_none(ch, 0);
        return;
    }

    if(arg[0] != '\0' && !str_prefix(arg, "log"))
    {
        QUEST_LOG_ENTRY *qle;
        int num = 1;
        char datebuf[32];

        if(IS_NPC(ch) || !ch->pcdata || !ch->pcdata->quest_log)
        {
            send_to_char("Your quest log is empty.\n\r", ch);
            return;
        }

        /* quest log <number> — detail view */
        if(!IS_NULLSTR(argument) && is_number(argument))
        {
            int target = atoi(argument);
            const char *diff, *result_vis;

            for(qle = ch->pcdata->quest_log; qle != NULL; qle = qle->next, num++)
            {
                if(num == target)
                    break;
            }

            if(!qle)
            {
                send_to_char("That entry doesn't exist in your quest log.\n\r", ch);
                return;
            }

            if(IS_SET(qle->quest_flags, Q_DEADLY))   diff = "Deadly";
            else if(IS_SET(qle->quest_flags, Q_HARD)) diff = "Hard";
            else if(IS_SET(qle->quest_flags, Q_EASY)) diff = "Easy";
            else                                       diff = "Normal";

            if(qle->result == 2)       result_vis = "\tGCompleted\tn";
            else if(qle->result == 3)  result_vis = "\tRFailed\tn";
            else if(qle->result == -1) result_vis = "\tYAbandoned\tn";
            else                       result_vis = "Unknown";

            strftime(datebuf, sizeof(datebuf), "%b %d, %Y %H:%M", localtime(&qle->completed_on));

            send_to_char("\tY+---------------------------------------------------------------------------+\tn\n\r", ch);
            send_to_char(Format("\tY|\tn \tBQuest Log Entry #%d\tn%*s \tY|\tn\n\r", target, 54 - (target > 9 ? 1 : 0), ""), ch);
            send_to_char("\tY+---------------------------------------------------------------------------+\tn\n\r", ch);
            send_to_char(Format("\tY|\tn \tWType:\tn       %-62s \tY|\tn\n\r", quest_type_name(qle->quest_type)), ch);
            send_to_char(Format("\tY|\tn \tWDifficulty:\tn %-62s \tY|\tn\n\r", diff), ch);

            {
                const char *rvis = qle->result == 2 ? "Completed" : qle->result == 3 ? "Failed" : qle->result == -1 ? "Abandoned" : "Unknown";
                int rpad = 62 - (int)strlen(rvis);
                if(rpad < 0) rpad = 0;
                send_to_char(Format("\tY|\tn \tWResult:\tn     %s%*s \tY|\tn\n\r", result_vis, rpad, ""), ch);
            }

            send_to_char(Format("\tY|\tn \tWDate:\tn       %-62s \tY|\tn\n\r", datebuf), ch);

            if(qle->employer)
                send_to_char(Format("\tY|\tn \tWEmployer:\tn   %-62s \tY|\tn\n\r", qle->employer), ch);
            if(qle->target)
                send_to_char(Format("\tY|\tn \tWTarget:\tn     %-62s \tY|\tn\n\r", qle->target), ch);

            if(qle->xp_earned > 0 || qle->cash_earned > 0)
            {
                char reward[64];
                snprintf(reward, sizeof(reward), "%d XP, $%d", qle->xp_earned, qle->cash_earned);
                send_to_char(Format("\tY|\tn \tWRewards:\tn    %-62s \tY|\tn\n\r", reward), ch);
            }

            /*
             * BONUS ITEM DISPLAY HOOK
             *
             * When item rewards are implemented, they will display here:
             * if(qle->bonus_item)
             *     send_to_char(Format("| Bonus Item: %-62s |", qle->bonus_item), ch);
             */
            if(qle->bonus_item)
                send_to_char(Format("\tY|\tn \tWBonus Item:\tn %-62s \tY|\tn\n\r", qle->bonus_item), ch);

            {
                char flags_buf[MIL] = {'\0'};
                if(IS_SET(qle->quest_flags, Q_SAFE))    strncat(flags_buf, "Safe ", sizeof(flags_buf) - strlen(flags_buf) - 1);
                if(IS_SET(qle->quest_flags, Q_ARREST))  strncat(flags_buf, "Arrest-Risk ", sizeof(flags_buf) - strlen(flags_buf) - 1);
                if(IS_SET(qle->quest_flags, Q_PUZZLE))  strncat(flags_buf, "Puzzle ", sizeof(flags_buf) - strlen(flags_buf) - 1);
                if(IS_SET(qle->quest_flags, Q_HUNTER))  strncat(flags_buf, "Hunter ", sizeof(flags_buf) - strlen(flags_buf) - 1);
                if(flags_buf[0])
                    send_to_char(Format("\tY|\tn \tWFlags:\tn      %-62s \tY|\tn\n\r", flags_buf), ch);
            }

            send_to_char("\tY+---------------------------------------------------------------------------+\tn\n\r", ch);
            return;
        }

        /* quest log — table view */
        send_to_char("\tY+----+-----------+------------+-----------+------------+---------------------+\tn\n\r", ch);
        send_to_char("\tY|\tn \tB# \tn \tY|\tn \tBType\tn      \tY|\tn \tBDifficulty\tn \tY|\tn \tBResult\tn    \tY|\tn \tBReward\tn     \tY|\tn \tBDate\tn                \tY|\tn\n\r", ch);
        send_to_char("\tY+----+-----------+------------+-----------+------------+---------------------+\tn\n\r", ch);

        for(qle = ch->pcdata->quest_log; qle != NULL && num <= 20; qle = qle->next, num++)
        {
            const char *diff;
            const char *result_vis;
            char reward[32];

            if(IS_SET(qle->quest_flags, Q_DEADLY))   diff = "Deadly";
            else if(IS_SET(qle->quest_flags, Q_HARD)) diff = "Hard";
            else if(IS_SET(qle->quest_flags, Q_EASY)) diff = "Easy";
            else                                       diff = "Normal";

            if(qle->result == 2)       result_vis = "Completed";
            else if(qle->result == 3)  result_vis = "Failed";
            else if(qle->result == -1) result_vis = "Abandoned";
            else                       result_vis = "Unknown";

            if(qle->xp_earned > 0 || qle->cash_earned > 0)
                snprintf(reward, sizeof(reward), "%dXP $%d%s",
                    qle->xp_earned, qle->cash_earned,
                    qle->bonus_item ? "+" : "");
            else
                snprintf(reward, sizeof(reward), "---");

            strftime(datebuf, sizeof(datebuf), "%b %d, %Y", localtime(&qle->completed_on));

            {
                const char *result_col;
                int rpad;

                if(qle->result == 2)       result_col = "\tGCompleted\tn";
                else if(qle->result == 3)  result_col = "\tRFailed\tn";
                else if(qle->result == -1) result_col = "\tYAbandoned\tn";
                else                       result_col = "Unknown";

                rpad = 9 - (int)strlen(result_vis);
                if(rpad < 0) rpad = 0;

                send_to_char(Format("\tY|\tn %2d \tY|\tn %-9s \tY|\tn %-10s \tY|\tn %s%*s \tY|\tn %-10s \tY|\tn %-19s \tY|\tn\n\r",
                    num,
                    quest_type_name(qle->quest_type),
                    diff,
                    result_col, rpad, "",
                    reward,
                    datebuf), ch);
            }
        }

        send_to_char("\tY+----+-----------+------------+-----------+------------+---------------------+\tn\n\r", ch);
        send_to_char("  Type '\tWquest log <#>\tn' for details on a specific entry.\n\r", ch);
        return;
    }

    if(arg[0] != '\0' && str_prefix(arg, "status") && str_prefix(arg, "log")
            && str_prefix(arg, "abandon") && str_prefix(arg, "rescue"))
    {
        send_to_char("Usage: \tWquest\tn, \tWquest status\tn, \tWquest log\tn, \tWquest abandon\tn, \tWquest rescue\tn\n\r", ch);
        return;
    }

    if(ch->quest == NULL)
    {
        send_to_char("You are not currently on a quest.\n\r", ch);
        return;
    }

    send_to_char("\tY+---------------------------------------------------------------------------+\tn\n\r", ch);
    send_to_char(Format("\tY|\tn \tBCurrent Quest\tn%*s \tY|\tn\n\r", 60, ""), ch);
    send_to_char("\tY+---------------------------------------------------------------------------+\tn\n\r", ch);

    send_to_char(Format("\tY|\tn \tWType:\tn       %-62s \tY|\tn\n\r",
        quest_type_name(ch->quest->quest_type)), ch);

    {
        const char *diff_vis;
        int pad;

        if(IS_SET(ch->quest->quest_flags, Q_DEADLY))      diff_vis = "Deadly";
        else if(IS_SET(ch->quest->quest_flags, Q_HARD))    diff_vis = "Hard";
        else if(IS_SET(ch->quest->quest_flags, Q_EASY))    diff_vis = "Easy";
        else                                                diff_vis = "Normal";

        pad = 62 - (int)strlen(diff_vis);
        if(pad < 0) pad = 0;

        send_to_char(Format("\tY|\tn \tWDifficulty:\tn %s%*s \tY|\tn\n\r",
            quest_difficulty_label(ch->quest->quest_flags), pad, ""), ch);
    }

    {
        const char *status_vis;
        const char *status_col;
        int pad;

        if(ch->quest->state == 0) { status_vis = "Offered (type 'accept' to begin)"; status_col = "\tYOffered\tn (type '\tWaccept\tn' to begin)"; }
        else if(ch->quest->state == 1) { status_vis = "Accepted - In Progress"; status_col = "\tCAccepted - In Progress\tn"; }
        else if(ch->quest->state == 2) { status_vis = "Completed"; status_col = "\tGCompleted\tn"; }
        else { status_vis = "Failed"; status_col = "\tRFailed\tn"; }

        pad = 62 - (int)strlen(status_vis);
        if(pad < 0) pad = 0;

        send_to_char(Format("\tY|\tn \tWStatus:\tn     %s%*s \tY|\tn\n\r", status_col, pad, ""), ch);
    }

    if(ch->quest->aggressor != NULL)
    {
        send_to_char(Format("\tY|\tn \tWEmployer:\tn   %-62s \tY|\tn\n\r",
            ch->quest->aggressor->short_descr ? ch->quest->aggressor->short_descr
            : ch->quest->aggressor->name), ch);
    }

    if(ch->quest->victim != NULL && ch->quest->state >= 1)
    {
        send_to_char(Format("\tY|\tn \tWTarget:\tn     %-62s \tY|\tn\n\r",
            ch->quest->victim->short_descr ? ch->quest->victim->short_descr
            : ch->quest->victim->name), ch);
    }

    if(ch->quest->time_limit > 0)
    {
        send_to_char(Format("\tY|\tn \tWTime Left:\tn  %-62d \tY|\tn\n\r",
            ch->quest->time_limit), ch);
    }

    if(ch->quest->state >= 1 && ch->quest->progress > 0
            && (ch->quest->quest_type == Q_GUARD || ch->quest->quest_type == Q_BODYGUARD))
    {
        char progbuf[64];
        snprintf(progbuf, sizeof(progbuf), "%d / 30 ticks", ch->quest->progress);
        send_to_char(Format("\tY|\tn \tWProgress:\tn   %-62s \tY|\tn\n\r", progbuf), ch);
    }

    if(ch->quest->target_room != NULL && ch->quest->state >= 1)
    {
        send_to_char(Format("\tY|\tn \tWLocation:\tn   %-62s \tY|\tn\n\r",
            ch->quest->target_room->name), ch);
    }

    {
        char flags_buf[MIL] = {'\0'};

        if(IS_SET(ch->quest->quest_flags, Q_SAFE))
            strncat(flags_buf, "Safe ", sizeof(flags_buf) - strlen(flags_buf) - 1);
        if(IS_SET(ch->quest->quest_flags, Q_ARREST))
            strncat(flags_buf, "Arrest-Risk ", sizeof(flags_buf) - strlen(flags_buf) - 1);
        if(IS_SET(ch->quest->quest_flags, Q_PUZZLE))
            strncat(flags_buf, "Puzzle ", sizeof(flags_buf) - strlen(flags_buf) - 1);
        if(IS_SET(ch->quest->quest_flags, Q_HUNTER))
            strncat(flags_buf, "Hunter ", sizeof(flags_buf) - strlen(flags_buf) - 1);

        if(flags_buf[0] != '\0')
        {
            send_to_char(Format("\tY|\tn \tWFlags:\tn      %-62s \tY|\tn\n\r", flags_buf), ch);
        }
    }

    send_to_char("\tY+---------------------------------------------------------------------------+\tn\n\r", ch);
}

void do_accept(CHAR_DATA *ch, char * argument)
{
	CheckCH(ch);

	if(ch->quest == NULL)
	{
		send_to_char("Nobody's offered you any jobs.\n\r", ch);
		return;
	}

	if(ch->quest->state > 0)
	{
		send_to_char("Aren't you still on a mission?\n\r", ch);
		return;
	}

	(*quest_table[ch->quest->quest_type].q_fun) (ch, 1);
}

void remove_quest(QUEST_DATA *q)
{
	UNLINK_SINGLE(q, next, QUEST_DATA, quest_list);
}

void quest_none (CHAR_DATA *ch, int flag)
{
	QUEST_DATA *pq = ch->quest;

	if(pq == NULL)
		return;

	if(pq->victim != NULL && pq->victim->quest == pq)
		pq->victim->quest = NULL;
	if(pq->aggressor != NULL && pq->aggressor->quest == pq)
		pq->aggressor->quest = NULL;
	if(pq->questor != NULL && pq->questor->quest == pq)
		pq->questor->quest = NULL;

	remove_quest(pq);
	free_quest(pq);
	ch->quest = NULL;
}

void quest_courier (CHAR_DATA *ch, int flag)
{
    OBJ_DATA *new_obj;
    OBJ_DATA *old_obj;

    switch(flag)
    {
    case 0 :
        if(ch->quest->obj == NULL)
        {
            quest_none(ch, 0);
            break;
        }
        old_obj = ch->quest->obj;
        new_obj = create_object(old_obj->pIndexData);
        clone_object(old_obj, new_obj);
        extract_obj(old_obj);
        ch->quest->obj = new_obj;
        SET_BIT(ch->quest->obj->extra2, OBJ_PACKAGED);
        if(!IS_SET(ch->quest->obj->wear_flags, ITEM_TAKE))
            SET_BIT(ch->quest->obj->wear_flags, ITEM_TAKE);
        if(ch->quest->obj != NULL
                && ch->quest->victim->carry_weight + get_obj_weight(ch->quest->obj)
                > can_carry_w(ch->quest->victim))
        {
            extract_obj(ch->quest->obj);
            ch->quest->obj = NULL;
        }
        if(ch->quest->aggressor != NULL)
        {
            send_to_char("\tC[Quest Offer]\tn ", ch);
            act("$N asks, \"Can you run $p to someone for me?\"", ch, ch->quest->obj, ch->quest->aggressor, TO_CHAR, 1);
        }
        else quest_none(ch, 0);
        break;
    case 1 :
        if(ch->quest->aggressor != NULL)
        {
            send_to_char("\tG[Quest Accepted]\tn You accept the courier job.\n\r", ch);
            if(ch->quest->victim != NULL && ch->quest->obj != NULL)
            {
                act("$N says, \"I need you to run this to $t.\"", ch, PERS( ch->quest->victim, ch ), ch->quest->aggressor, TO_CHAR, 1);
                act("$N gives you $p.", ch, ch->quest->obj, ch->quest->aggressor, TO_CHAR, 1);
                obj_to_char(ch->quest->obj, ch);
            }
            else
            {
                act("$N says, \"It doesn't matter anymore.\"", ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
            }
        }
        else
        {
            send_to_char("\tY[Quest Expired]\tn The offer doesn't seem to be open anymore.\n\r", ch);
            quest_none(ch, 0);
        }
        break;
    case 2 :
        if(ch->quest->victim != NULL)
        {
            act("\tG[Quest Complete]\tn $N thanks you, giving you a COD payment.", ch, NULL, ch->quest->victim, TO_CHAR, 1);
        }
        else send_to_char("\tG[Quest Complete]\tn You succeed.\n\r", ch);
        quest_reward(ch);
        if(ch->quest->obj != NULL)
            extract_obj(ch->quest->obj);
        ch->quest->obj = NULL;
        quest_none(ch, 0);
        break;
    case 3 :
        if(ch->quest->state == 0)
        {
            if(ch->quest->aggressor != NULL)
            {
                act("\tY[Quest Expired]\tn $N says, \"I guess I'll just find someone else to do it.\"", ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
            }
        }
        else { send_to_char("\tR[Quest Failed]\tn You fail to deliver the package.\n\r", ch); add_quest_log(ch, 3, 0, 0); }
        if(ch->quest->obj != NULL)
            extract_obj(ch->quest->obj);
        ch->quest->obj = NULL;
        quest_none(ch, 0);
        break;
    }

    if(ch->quest != NULL) ch->quest->state = flag;
}

void quest_hitman (CHAR_DATA *ch, int flag)
{
    switch(flag)
    {
    case 0 :
        if(ch->quest->aggressor != NULL)
        {
            send_to_char("\tC[Quest Offer]\tn ", ch);
            act("$N asks, \"I need a professional hit... interested?\"", ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
        }
        else quest_none(ch, 0);
        break;
    case 1 :
        if(ch->quest->aggressor != NULL)
        {
            send_to_char("\tG[Quest Accepted]\tn You accept the hitman job.\n\r", ch);
            if(ch->quest->victim != NULL)
            {
                act("$N says, \"I need you to rub out $t.\"", ch, PERS( ch->quest->victim, ch ), ch->quest->aggressor, TO_CHAR, 1);
            }
            else
            {
                act("$N says, \"It doesn't matter anymore.\"", ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
            }
        }
        else
        {
            send_to_char("\tY[Quest Expired]\tn The offer doesn't seem to be open anymore.\n\r", ch);
            quest_none(ch, 0);
        }
        break;
    case 2 :
        if(ch->quest->aggressor != NULL)
        {
            act("\tG[Quest Complete]\tn $N thanks you, delivering payment into your account.", ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
        }
        else send_to_char("\tG[Quest Complete]\tn You succeed.\n\r", ch);
        quest_reward(ch);
        quest_none(ch, 0);
        break;
    case 3 :
        if(ch->quest->state == 0)
        {
            if(ch->quest->aggressor != NULL)
            {
                act("\tY[Quest Expired]\tn $N says, \"I guess I'll just find someone else to do it.\"", ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
            }
        }
        else { send_to_char("\tR[Quest Failed]\tn You fail to eliminate the target.\n\r", ch); add_quest_log(ch, 3, 0, 0); }
        quest_none(ch, 0);
        break;
    }

    if(ch->quest != NULL) ch->quest->state = flag;
}

void quest_thief (CHAR_DATA *ch, int flag)
{
    OBJ_DATA *new_obj;
    OBJ_DATA *old_obj;

    switch(flag)
    {
    case 0 :
        if(ch->quest->obj == NULL)
        {
            quest_none(ch, 0);
            break;
        }
        old_obj = ch->quest->obj;
        new_obj = create_object(old_obj->pIndexData);
        clone_object(old_obj, new_obj);
        extract_obj(old_obj);
        ch->quest->obj = new_obj;
        SET_BIT(ch->quest->obj->extra2, OBJ_PACKAGED);
        if(!IS_SET(ch->quest->obj->wear_flags, ITEM_TAKE))
            SET_BIT(ch->quest->obj->wear_flags, ITEM_TAKE);
        if(ch->quest->obj != NULL
                && ch->quest->victim->carry_weight + get_obj_weight(ch->quest->obj)
                > can_carry_w(ch->quest->victim))
        {
            extract_obj(ch->quest->obj);
            ch->quest->obj = NULL;
        }
        /*aggressor makes offer;*/
        if(ch->quest->aggressor != NULL)
        {
            send_to_char("\tC[Quest Offer]\tn ", ch);
            act("$N asks, \"Can you... acquire something for me?\"", ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
        }
        else quest_none(ch, 0);
        break;
    case 1 :
        if(ch->quest->aggressor != NULL)
        {
            send_to_char("\tG[Quest Accepted]\tn You accept the theft job.\n\r", ch);
            if(ch->quest->victim != NULL && ch->quest->obj != NULL)
            {
                char buf[MSL]={'\0'};
                snprintf(buf, sizeof(buf), "$N says, \"I need you to steal %s from $t.\"", format_obj_to_char( ch->quest->obj, ch, TRUE ));
                act(buf, ch, PERS( ch->quest->victim, ch ), ch->quest->aggressor, TO_CHAR, 1);
                act("$N gives you $p.", ch->quest->victim, ch->quest->obj, ch->quest->aggressor, TO_CHAR, 1);
                obj_to_char(ch->quest->obj, ch->quest->victim);
            }
            else
            {
                act("$N says, \"It doesn't matter anymore.\"", ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
            }
        }
        else
        {
            send_to_char("\tY[Quest Expired]\tn The offer doesn't seem to be open anymore.\n\r", ch);
            quest_none(ch, 0);
        }
        break;
    case 2 :
        if(ch->quest->aggressor != NULL)
        {
            act("\tG[Quest Complete]\tn $N thanks you, giving you a COD payment.", ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
        }
        else send_to_char("\tG[Quest Complete]\tn You steal the item!\n\r", ch);
        quest_reward(ch);
        if(ch->quest->obj != NULL)
            extract_obj(ch->quest->obj);
        ch->quest->obj = NULL;
        quest_none(ch, 0);
        break;
    case 3 :
        if(ch->quest->state == 0)
        {
            if(ch->quest->aggressor != NULL)
            {
                act("\tY[Quest Expired]\tn $N says, \"I guess I'll just find someone else to do it.\"", ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
            }
        }
        else { send_to_char("\tR[Quest Failed]\tn You fail to acquire the item.\n\r", ch); add_quest_log(ch, 3, 0, 0); }
        if(ch->quest->obj != NULL)
            extract_obj(ch->quest->obj);
        ch->quest->obj = NULL;
        quest_none(ch, 0);
        break;
    }

    if(ch->quest != NULL) ch->quest->state = flag;
}

/*
 * quest_bodyguard — Escort/protect the victim NPC.
 *
 * Mechanic: Stay in the same room as the victim for 30 ticks.
 * Progress is tracked in char_update (update.c). If the player
 * leaves the victim's room, progress pauses (but doesn't reset).
 * Timer expiry = failure.
 *
 * Race-specific dialogue:
 *   Vampire: protect a ghoul from rival agents
 *   Werewolf: guard kinfolk from Wyrm creatures
 *   Human: protect a witness from criminals
 */
void quest_bodyguard (CHAR_DATA *ch, int flag)
{
    switch(flag)
    {
    case 0 :
        if(ch->quest->victim == NULL) { quest_none(ch, 0); break; }

        if(IS_VAMPIRE(ch))
        {
            send_to_char("\tC[Quest Offer]\tn ", ch);
            act("$N whispers, \"My sire's enemies are circling. I need someone to keep me safe until dawn.\"",
                ch, NULL, ch->quest->victim, TO_CHAR, 1);
        }
        else if(IS_WEREWOLF(ch))
        {
            send_to_char("\tC[Quest Offer]\tn ", ch);
            act("$N says, \"The Wyrm's servants are hunting our kin. Stand watch over me.\"",
                ch, NULL, ch->quest->victim, TO_CHAR, 1);
        }
        else
        {
            send_to_char("\tC[Quest Offer]\tn ", ch);
            act("$N says, \"I saw something I shouldn't have. They're coming for me. Please, stay close.\"",
                ch, NULL, ch->quest->victim, TO_CHAR, 1);
        }
        break;

    case 1 :
        if(ch->quest->victim == NULL)
        {
            send_to_char("\tY[Quest Expired]\tn The person you were supposed to protect is gone.\n\r", ch);
            quest_none(ch, 0);
            break;
        }
        send_to_char("\tG[Quest Accepted]\tn You agree to serve as bodyguard.\n\r", ch);
        act("Stay near $N and keep them safe. You'll need to remain close for a while.",
            ch, NULL, ch->quest->victim, TO_CHAR, 1);
        send_to_char("Type '\tWquest status\tn' to check your progress.\n\r", ch);
        ch->quest->progress = 0;
        break;

    case 2 :
        send_to_char("\tG[Quest Complete]\tn Your charge is safe. The danger has passed.\n\r", ch);
        quest_reward(ch);
        quest_none(ch, 0);
        break;

    case 3 :
        if(ch->quest->state == 0)
        {
            send_to_char("\tY[Quest Expired]\tn The offer for protection has lapsed.\n\r", ch);
        }
        else
        {
            send_to_char("\tR[Quest Failed]\tn You ran out of time. Your charge is on their own now.\n\r", ch);
            add_quest_log(ch, 3, 0, 0);
        }
        quest_none(ch, 0);
        break;
    }
    if(ch->quest != NULL) ch->quest->state = flag;
}

/*
 * quest_guard — Hold a specific room for 30 ticks.
 *
 * Mechanic: On accept, target_room is set to the player's current room.
 * Progress ticks up each char_update while player is in target_room.
 * Leaving the room = immediate failure.
 *
 * Race-specific dialogue:
 *   Vampire: guard a haven entrance during day
 *   Werewolf: defend a caern or sacred site
 *   Human: guard a crime scene or stakeout position
 */
void quest_guard (CHAR_DATA *ch, int flag)
{
    switch(flag)
    {
    case 0 :
        if(ch->quest->aggressor == NULL) { quest_none(ch, 0); break; }

        if(IS_VAMPIRE(ch))
        {
            send_to_char("\tC[Quest Offer]\tn ", ch);
            act("$N says, \"I need someone to watch my door while I rest. No one enters.\"",
                ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
        }
        else if(IS_WEREWOLF(ch))
        {
            send_to_char("\tC[Quest Offer]\tn ", ch);
            act("$N says, \"The bawn must be defended. Hold this ground, no matter what.\"",
                ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
        }
        else
        {
            send_to_char("\tC[Quest Offer]\tn ", ch);
            act("$N says, \"Keep eyes on this location. Don't move until I tell you.\"",
                ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
        }
        break;

    case 1 :
        send_to_char("\tG[Quest Accepted]\tn You take up your guard position.\n\r", ch);
        send_to_char("Hold this room. Do not leave until the job is done.\n\r", ch);
        send_to_char("Type '\tWquest status\tn' to check your progress.\n\r", ch);
        ch->quest->target_room = ch->in_room;
        ch->quest->progress = 0;
        break;

    case 2 :
        send_to_char("\tG[Quest Complete]\tn Your watch is over. Well done.\n\r", ch);
        quest_reward(ch);
        quest_none(ch, 0);
        break;

    case 3 :
        if(ch->quest->state == 0)
        {
            if(ch->quest->aggressor != NULL)
                act("\tY[Quest Expired]\tn $N says, \"I'll find someone more reliable.\"",
                    ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
        }
        else
        {
            send_to_char("\tR[Quest Failed]\tn You abandoned your post.\n\r", ch);
            add_quest_log(ch, 3, 0, 0);
        }
        quest_none(ch, 0);
        break;
    }
    if(ch->quest != NULL) ch->quest->state = flag;
}

/*
 * quest_rescue — Find and retrieve the victim NPC.
 *
 * Mechanic: On accept, victim is moved to a random room.
 * Player must go to that room and type 'quest rescue' (handled
 * in do_quest) to extract them. Timer expiry = failure.
 *
 * Race-specific dialogue:
 *   Vampire: rescue a kindred from hunters
 *   Werewolf: free a packmate from Pentex
 *   Human: find a missing person
 */
void quest_rescue (CHAR_DATA *ch, int flag)
{
    switch(flag)
    {
    case 0 :
        if(ch->quest->victim == NULL || ch->quest->aggressor == NULL)
        { quest_none(ch, 0); break; }

        if(IS_VAMPIRE(ch))
        {
            send_to_char("\tC[Quest Offer]\tn ", ch);
            act("$N says, \"One of ours has been taken. The Society of Leopold, perhaps. Find $t.\"",
                ch, PERS(ch->quest->victim, ch), ch->quest->aggressor, TO_CHAR, 1);
        }
        else if(IS_WEREWOLF(ch))
        {
            send_to_char("\tC[Quest Offer]\tn ", ch);
            act("$N says, \"They've taken one of the pack. The scent leads downtown. Find $t.\"",
                ch, PERS(ch->quest->victim, ch), ch->quest->aggressor, TO_CHAR, 1);
        }
        else
        {
            send_to_char("\tC[Quest Offer]\tn ", ch);
            act("$N says, \"My partner missed check-in. Something's wrong. Find $t.\"",
                ch, PERS(ch->quest->victim, ch), ch->quest->aggressor, TO_CHAR, 1);
        }
        break;

    case 1 :
    {
        ROOM_INDEX_DATA *rm = random_room();

        if(ch->quest->victim == NULL || rm == NULL)
        {
            send_to_char("\tY[Quest Expired]\tn The trail has gone cold.\n\r", ch);
            quest_none(ch, 0);
            break;
        }

        send_to_char("\tG[Quest Accepted]\tn You set out to find the missing person.\n\r", ch);

        char_from_room(ch->quest->victim);
        char_to_room(ch->quest->victim, rm);
        ch->quest->target_room = rm;

        send_to_char(Format("They were last seen near \tW%s\tn.\n\r", rm->name), ch);
        send_to_char("Find them and type '\tWquest rescue\tn' when you reach them.\n\r", ch);
        break;
    }

    case 2 :
        send_to_char("\tG[Quest Complete]\tn You found them! They're safe now.\n\r", ch);
        quest_reward(ch);
        quest_none(ch, 0);
        break;

    case 3 :
        if(ch->quest->state == 0)
        {
            if(ch->quest->aggressor != NULL)
                act("\tY[Quest Expired]\tn $N says, \"Too late. I'll ask someone else.\"",
                    ch, NULL, ch->quest->aggressor, TO_CHAR, 1);
        }
        else
        {
            send_to_char("\tR[Quest Failed]\tn You ran out of time. The trail has gone cold.\n\r", ch);
            add_quest_log(ch, 3, 0, 0);
        }
        quest_none(ch, 0);
        break;
    }
    if(ch->quest != NULL) ch->quest->state = flag;
}

void goal_obtain_item(CHAR_DATA *ch, int flag)
{

    switch(flag)
    {
	case 0 :
	    /*"associated" mob makes offer;*/
	    send_to_char("You are asked to obtain an item of quality.\n\r", ch);
	    break;
	case 1 :
	    /*acceptance of offer;*/
	    send_to_char("You accept the request.\n\r", ch);
	    break;
	case 2 :
	    /*mission success;*/
	    send_to_char("You succeed.\n\r", ch);
	    quest_none(ch, 0);
	    break;
	case 3 :
	    /*mission fail;*/
	    send_to_char("You fail.\n\r", ch);
	    quest_none(ch, 0);
	    break;
    }

    if(ch->quest != NULL) ch->quest->state = flag;

}

/*
 * do_questgen — Staff command to generate a quest for testing.
 *
 * Usage:
 *   questgen           — generate a random quest type
 *   questgen <type>    — generate a specific type
 *   questgen list      — show available types
 *
 * Valid types: courier, hitman, thief, bodyguard, guard, rescue
 */
void do_questgen(CHAR_DATA *ch, char *argument)
{
    QUEST_DATA *pquest;
    int qtype = -1;
    char arg[MAX_INPUT_LENGTH];

    CheckCH(ch);

    one_argument(argument, arg);

    if(ch->quest != NULL)
    {
        send_to_char("You already have an active quest. Use '\tWquest abandon\tn' first.\n\r", ch);
        return;
    }

    if(!str_cmp(arg, "list"))
    {
        send_to_char("\tBAvailable quest types:\tn\n\r", ch);
        send_to_char("  \tWcourier\tn   — Deliver a package to an NPC\n\r", ch);
        send_to_char("  \tWhitman\tn    — Eliminate a target\n\r", ch);
        send_to_char("  \tWthief\tn     — Steal an item from an NPC\n\r", ch);
        send_to_char("  \tWbodyguard\tn — Escort and protect an NPC\n\r", ch);
        send_to_char("  \tWguard\tn     — Hold a room for a period of time\n\r", ch);
        send_to_char("  \tWrescue\tn    — Find and retrieve a missing NPC\n\r", ch);
        return;
    }

    if(arg[0] == '\0')
    {
        qtype = number_range(Q_COURIER, Q_RESCUE);
    }
    else if(!str_prefix(arg, "courier"))   qtype = Q_COURIER;
    else if(!str_prefix(arg, "hitman"))    qtype = Q_HITMAN;
    else if(!str_prefix(arg, "thief"))     qtype = Q_THIEF;
    else if(!str_prefix(arg, "bodyguard")) qtype = Q_BODYGUARD;
    else if(!str_prefix(arg, "guard"))     qtype = Q_GUARD;
    else if(!str_prefix(arg, "rescue"))    qtype = Q_RESCUE;
    else
    {
        send_to_char("Unknown quest type. Type '\tWquestgen list\tn' for options.\n\r", ch);
        return;
    }

    pquest = new_quest();
    if(!pquest)
    {
        send_to_char("Error: could not create quest.\n\r", ch);
        return;
    }

    pquest->questor = ch;
    pquest->quest_type = qtype;
    pquest->quest_flags = gen_quest_flags();

    pquest->victim = gen_quest_victim(pquest);
    if(pquest->victim == NULL || pquest->victim == ch)
    {
        send_to_char("\tRFailed:\tn Could not find a suitable victim NPC. Try again.\n\r", ch);
        free_quest(pquest);
        return;
    }

    pquest->aggressor = gen_quest_aggressor(pquest);
    if(pquest->aggressor == NULL || pquest->aggressor == ch || pquest->aggressor == pquest->victim)
    {
        send_to_char("\tRFailed:\tn Could not find a suitable employer NPC. Try again.\n\r", ch);
        free_quest(pquest);
        return;
    }

    pquest->aggressor->quest = pquest;
    pquest->victim->quest = pquest;

    pquest->time_limit = gen_time_limit(pquest, 0);
    pquest->obj = gen_quest_object();
    pquest->questor->quest = pquest;

    LINK_SINGLE(pquest, next, quest_list);

    send_to_char(Format("\tY[Staff]\tn Generated %s quest (flags: %ld, time: %d).\n\r",
        quest_type_name(qtype), pquest->quest_flags, pquest->time_limit), ch);

    (*quest_table[pquest->quest_type].q_fun)(pquest->questor, 0);
}

void do_random_at(CHAR_DATA *ch, char *argument)
{
	char arg2[MAX_INPUT_LENGTH]={'\0'};
	char buf[MSL]={'\0'};
	CHAR_DATA *mob = get_random_mob(ch);

	if(IS_NULLSTR(argument))
	{
		return;
	}

	if(mob == NULL)
	{
		send_to_char("Could not find a suitable target.\n\r", ch);
		return;
	}

	one_argument(mob->name, arg2);
	snprintf(buf, sizeof(buf), "%s %s", arg2, argument);
	do_at(ch, buf);
}
