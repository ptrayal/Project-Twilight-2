/***************************************************************************
 * Much of the code is the original work of Peter Fitzgerald who turned    *
 * it over to Brandon Morrison who has adopted and improved the code.      *
 * Copyright (C) 2012 - 2024                                               *
 **************************************************************************/

#include <sys/types.h>
#include <stdlib.h>
#include <time.h>
#include "twilight.h"
#include "magic.h"
#include "recycle.h"
#include "tables.h"
#include "lookup.h"
#include "interp.h"


/*
 * Auspex powers
 * Level 1:  Heightened Senses
 */

void do_auspex1 ( CHAR_DATA * ch, char * string )
{
	AFFECT_DATA af;

	CheckCH(ch);

	if( IS_AFFECTED(ch, AFF_HSENSES) )
	{
		affect_strip ( ch, gsn_hsenses );
		REMOVE_BIT   ( ch->affected_by, AFF_HSENSES );
		send_to_char("Your senses return to their normal state.\n\r", ch);
		ch->power_timer = 1;
		return;
	}

	if( (!IS_VAMPIRE(ch) || ch->disc[DISC_AUSPEX] < 1)
		&& (!IS_WEREWOLF(ch) || (ch->shape <= SHAPE_HUMAN && !IS_SET(ch->powers[0], C))) )
	{
		send_to_char("\tRWARNING: You do not know Heightened Senses\tn.\n\r", ch);
		return;
	}

	if (!has_enough_power(ch))
		return;

	af.where     = TO_AFFECTS;
	af.type      = skill_lookup("heighten");
	if(ch->race == race_lookup("vampire"))
	{
		af.level     = ch->disc[DISC_AUSPEX];
		af.duration  = ch->disc[DISC_AUSPEX] * 5;
		af.modifier  = ch->disc[DISC_AUSPEX];
	}
	else if(ch->race == race_lookup("werewolf"))
	{
		af.level     = ch->max_GHB;
		af.duration  = 5 * ch->max_GHB;
		af.modifier  = UMAX(1, number_fuzzy(ch->max_GHB));
	}
	af.location  = APPLY_PER;
	af.bitvector = AFF_HSENSES;
	affect_to_char( ch, &af );
	send_to_char("Your senses expand, drawing in greater detail.\n\r", ch);
	ch->power_timer = 1;
}

/*
 *  Level 2: Aura Perception
 */

void do_auspex2 ( CHAR_DATA * ch, char * string )
{
	CHAR_DATA *victim;
	char arg[MAX_INPUT_LENGTH]={'\0'};
	int power_stat = 0;
	int power_ability = 0;
	int difficulty = 0;

	CheckCH(ch);

	power_stat = get_curr_stat(ch, STAT_PER);
	power_ability = ch->ability[EMPATHY].value;
	difficulty = 8;

	if( (!IS_VAMPIRE(ch) && !IS_WEREWOLF(ch))
		|| (IS_VAMPIRE(ch) && ch->disc[DISC_AUSPEX] < 2)
		|| (IS_WEREWOLF(ch) && !IS_SET(ch->powers[1], E) ) )
	{
		send_to_char("\tRWARNING: You do not know Aura Perception\tn.\n\r", ch);
		return;
	}

	if (!has_enough_power(ch))
		return;

	one_argument(string, arg);

	if(IS_NULLSTR(arg))
	{
		send_to_char("Whose aura do you wish to read?\n\r", ch);
		return;
	}

	if( (victim = get_char_room(ch, arg)) != NULL )
	{
		int success = 0;
		char buf[MSL]={'\0'};
		size_t len = 0;
		const char *vname = IS_NPC(victim) ? victim->short_descr : victim->name;

		ch->power_timer = 2;
		success = dice_rolls(ch, power_stat + power_ability, difficulty);

		if (success <= 0)
		{
			if( IS_WEREWOLF(ch) )
				act("\tRFailure\tn: You cannot smell $N's aura.", ch, NULL, victim, TO_CHAR, 1);
			else
				act("\tRFailure\tn: You cannot see $N's aura.", ch, NULL, victim, TO_CHAR, 1);
			act("$n stares at $N intently for a moment.", ch, NULL, victim, TO_ROOM, 1);
			return;
		}

		len = snprintf(buf, sizeof(buf), "\tGSuccess\tn: %s is surrounded by", vname);
		if(success >= 1 && len < sizeof(buf))
		{
			switch(victim->race)
			{
				case RACE_WEREWOLF:
					len += snprintf(buf + len, sizeof(buf) - len, " an intense and vibrant aura which moves like flames");
					break;
				case RACE_CHANGELING:
					len += snprintf(buf + len, sizeof(buf) - len, " a bright iridescent aura");
					break;
				case RACE_VAMPIRE:
					len += snprintf(buf + len, sizeof(buf) - len, " a pale aura");
					break;
				case RACE_HUMAN:
					len += snprintf(buf + len, sizeof(buf) - len, " a rosy aura");
					break;
				default:
					len += snprintf(buf + len, sizeof(buf) - len, " a hazy, indistinct aura");
					break;
			}
			if(success == 1 && len < sizeof(buf))
			{
				len += snprintf(buf + len, sizeof(buf) - len, ".\n\r");
			}
		}

		if(success >= 2 && len < sizeof(buf))
		{
			if(victim->condition[COND_ANGER] > 50 && len < sizeof(buf))
				len += snprintf(buf + len, sizeof(buf) - len, " with red bursts");
			if(victim->condition[COND_PAIN] > 50 && len < sizeof(buf))
				len += snprintf(buf + len, sizeof(buf) - len, " with patches of red and purples");
			if(victim->condition[COND_FEAR] > 50 && len < sizeof(buf))
				len += snprintf(buf + len, sizeof(buf) - len, " with orange skittering lines");
			if(victim->condition[COND_FRENZY] > 50 && len < sizeof(buf))
				len += snprintf(buf + len, sizeof(buf) - len, " that is rapidly rippling");
			if(victim->condition[COND_ANGER] < 50
				&& victim->condition[COND_FEAR] < 50
				&& victim->condition[COND_FRENZY] < 50
				&& victim->condition[COND_PAIN] < 50
				&& len < sizeof(buf))
				len += snprintf(buf + len, sizeof(buf) - len, " that is a mild blue color");

			if(len < sizeof(buf))
				len += snprintf(buf + len, sizeof(buf) - len, ".\n\r");
		}

		if(success >= 3 && len < sizeof(buf))
		{
			len += snprintf(buf + len, sizeof(buf) - len, "You also see color patterns");

			if(len < sizeof(buf))
			{
				if(victim->clan == clan_lookup("brujah"))
					len += snprintf(buf + len, sizeof(buf) - len, " in purple and violet");
				else if(victim->clan == clan_lookup("gangrel"))
					len += snprintf(buf + len, sizeof(buf) - len, " of green and brown");
				else if(victim->clan == clan_lookup("malkavian"))
					len += snprintf(buf + len, sizeof(buf) - len, " of yellow");
				else if(victim->clan == clan_lookup("nosferatu"))
					len += snprintf(buf + len, sizeof(buf) - len, " in a sickly green");
				else if(victim->clan == clan_lookup("toreador"))
					len += snprintf(buf + len, sizeof(buf) - len, " of green");
				else if(victim->clan == clan_lookup("tremere"))
					len += snprintf(buf + len, sizeof(buf) - len, " in brown and light green with a myriad of sparkles");
				else if(victim->clan == clan_lookup("ventrue"))
					len += snprintf(buf + len, sizeof(buf) - len, " in lavender");
				else if(victim->clan == clan_lookup("lasombra"))
					len += snprintf(buf + len, sizeof(buf) - len, " of pulsating and writhing black and grey");
				else if(victim->clan == clan_lookup("tzimisce"))
					len += snprintf(buf + len, sizeof(buf) - len, " in a myriad of sparkling green");
				else if(victim->clan == clan_lookup("assamite"))
					len += snprintf(buf + len, sizeof(buf) - len, " of blue with black arteries");
				else if(victim->clan == clan_lookup("settite"))
					len += snprintf(buf + len, sizeof(buf) - len, " in a deep red and gold");
				else if(victim->clan == clan_lookup("giovanni"))
					len += snprintf(buf + len, sizeof(buf) - len, " in a deep red and green");
				else if(victim->clan == clan_lookup("ravnos"))
					len += snprintf(buf + len, sizeof(buf) - len, " in sharp, flickering black and violet");
				else if(victim->clan == clan_lookup("child of cacophony"))
					len += snprintf(buf + len, sizeof(buf) - len, " in a mottled silver and yellow");
				else if(victim->clan == clan_lookup("bone"))
					len += snprintf(buf + len, sizeof(buf) - len, " of gold and light green");
				else if(victim->clan == clan_lookup("glass"))
					len += snprintf(buf + len, sizeof(buf) - len, " of gold and deep red");
				else if(victim->clan == clan_lookup("malk"))
					len += snprintf(buf + len, sizeof(buf) - len, " with hypnotic swirling colors");
				else if(victim->clan == clan_lookup("soc. of Leopold"))
					len += snprintf(buf + len, sizeof(buf) - len, " with bright gold");
				else if(victim->clan == clan_lookup("silver"))
					len += snprintf(buf + len, sizeof(buf) - len, " of brown and gold");
				/*
				else if(victim->clan == clan_lookup("silent"))
					len += snprintf(buf + len, sizeof(buf) - len, " of gold and silver");
				*/
				else
					len += snprintf(buf + len, sizeof(buf) - len, " of white");
			}

			if(success == 3 && len < sizeof(buf))
				len += snprintf(buf + len, sizeof(buf) - len, ".\n\r");
		}

		if(success >= 4 && len < sizeof(buf))
		{
			if(victim->GHB > 7)
				len += snprintf(buf + len, sizeof(buf) - len, " with golden flecks.\n\r");
			else if(victim->GHB < 3)
				len += snprintf(buf + len, sizeof(buf) - len, " with white flecks.\n\r");
			else
				len += snprintf(buf + len, sizeof(buf) - len, ".\n\r");
		}

		send_to_char(buf, ch);
		return;
	}
	else
	{
		send_to_char("They're not here.\n\r", ch);
		return;
	}
}

/*
 *  Level 3: Spirit's Touch
 */

void do_auspex3 ( CHAR_DATA * ch, char * argument )
{
	OBJ_DATA *obj;
	int success = 0;
	int power_stat = 0;
	int power_ability = 0;
	int difficulty = 0;

	CheckCH(ch);

	if( !IS_VAMPIRE(ch) || ch->disc[DISC_AUSPEX] < 3 )
	{
		send_to_char("\tRWARNING: You do not know Spirit's Touch\tn.\n\r", ch);
		return;
	}

	if (!has_enough_power(ch))
		return;

	if(IS_NULLSTR(argument))
	{
		send_to_char("What are you trying to learn about?\n\r", ch);
		return;
	}

	if( (obj = get_obj_carry(ch, argument, ch)) == NULL )
	{
		send_to_char("You don't have that object.\n\r", ch);
		return;
	}

	difficulty = 8;
	power_stat = get_curr_stat(ch, STAT_PER);
	power_ability = ch->ability[EMPATHY].value;
	success = dice_rolls(ch, power_stat + power_ability, difficulty);

	if(success < 0)
	{
		send_to_char("\tRCritical Failure\tn: You are overwhelmed by the psychic impressions on this object and it gives you a massive headache!\n\r", ch);
		ch->power_timer = 6;
	}
	else if(success == 0)
	{
		send_to_char("\tRFailure\tn: You get nothing off of this object.\n\r", ch);
		ch->power_timer = 3;
	}
	else
	{
		if(obj->owner == NULL || IS_NULLSTR(obj->owner) || IS_BUILDERMODE(obj))
			send_to_char("No-one has touched this object.\n\r", ch);
		else
		{
			char msg[MSL];
			snprintf(msg, sizeof(msg), "You receive an image of %s holding %s.\n\r", obj->owner, obj->short_descr);
			send_to_char(msg, ch);
		}
		ch->power_timer = 3;
	}
}

/*
 *  Level 4: Telepathy
 */

void do_auspex4(CHAR_DATA *ch, char *argument)
{
	CheckCH(ch);

	if( !IS_VAMPIRE(ch) || ch->disc[DISC_AUSPEX] < 4)
	{
		send_to_char("\tRWARNING: You do not know Telepathy\tn.\n\r", ch);
		return;
	}

	if(IS_AFFECTED(ch,AFF_AUSPEX4))
	{
		REMOVE_BIT(ch->affected_by, AFF_AUSPEX4);
		send_to_char("You stop listening in on the thoughts of others.\n\r", ch);
		return;
	}

	if (!has_enough_power(ch))
		return;

	send_to_char( "You focus to listen to thoughts.\n\r", ch );
	SET_BIT(ch->affected_by, AFF_AUSPEX4);
	ch->power_timer = 1;
}

/*
 *  Level 5: Astral Projection
 */

void do_auspex5(CHAR_DATA *ch, char *argument)
{
	int success = 0;
	int power_stat = 0;
	int power_ability = 0;
	int difficulty = 0;

	CheckCH(ch);

	if( !IS_VAMPIRE(ch) || ch->disc[DISC_AUSPEX] < 5)
	{
		send_to_char("\tRWARNING: You do not know Astral Projection\tn.\n\r", ch);
		return;
	}

	if(ch->power_timer > 0 && !IS_SET(ch->act2, ACT2_ASTRAL))
	{
		send_to_char("Your powers have not rejuvenated yet.\n\r", ch);
		return;
	}

	/* Power costs blood only when projecting out, not when returning. */
	if(!IS_SET(ch->act2, ACT2_ASTRAL) && !has_enough_power(ch))
		return;

	difficulty = 8;
	power_stat = get_curr_stat(ch, STAT_PER);
	power_ability = ch->ability[OCCULT].value;

	success = dice_rolls(ch, power_stat + power_ability, difficulty);

	ch->power_timer = 2;
	if(success > 0 || IS_SET(ch->parts, PART_THREAD))
	{
		if(!IS_SET(ch->act2, ACT2_ASTRAL))
		{
			do_function(ch, &do_sleep, "");
			if(ch->position != P_SLEEP)
			{
				send_to_char( "You can't get comfortable enough to concentrate.\n\r", ch);
				return;
			}

			SET_BIT(ch->act2, ACT2_ASTRAL);
			char_to_listeners(ch, ch->in_room);
			SET_BIT(ch->parts, PART_THREAD);
			send_to_char("You leave your body behind.\n\r", ch);
		}
		else
		{
			if(IS_SET(ch->parts, PART_THREAD))
			{
				REMOVE_BIT(ch->act2, ACT2_ASTRAL);
				REMOVE_BIT(ch->parts, PART_THREAD);
				char_from_listeners(ch);
				send_to_char("You return to your body.\n\r", ch);
				do_function(ch, &do_wake, "");
			}
			else
			{
				if(ch->listening == ch->in_room)
				{
					REMOVE_BIT(ch->act2, ACT2_ASTRAL);
					char_from_listeners(ch);
					send_to_char("You return to your body.\n\r", ch);
					do_function(ch, &do_wake, "");
				}
				else
				{
					send_to_char( "You can't find your way back to your body!\n\r", ch);
				}
			}
			return;
		}

	}
	else if(success <= 0)
	{
		send_to_char("\tRFailure\tn: Nothing happens.\n\r", ch);
	}
}
