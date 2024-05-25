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

	if( !IS_VAMPIRE(ch) || ch->disc[DISC_AUSPEX] < 1 )
	{
		send_to_char("\tRWARNING: You do not know Heightened Senses\tn.\n\r", ch);
		return;
	}

	if (!has_enough_power(ch))
		return;

	if( ( IS_VAMPIRE(ch) && ch->disc[DISC_AUSPEX] >= 1 )
		|| ( IS_WEREWOLF(ch) && (ch->shape > SHAPE_HUMAN
			|| IS_SET(ch->powers[0], C) ) ) )
	{
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
	else
	{
		send_to_char("\tRWARNING: You do not know Heightened Senses\tn.\n\r", ch);
	}
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

	if( (victim = get_char_room(ch, string)) != NULL )
	{
		int success = 0;
		char buf[MSL]={'\0'};
		ch->power_timer = 2;
		success = dice_rolls(ch, power_stat + power_ability, difficulty);

		if (success <= 0)
		{
			if( IS_WEREWOLF(ch) )
				act("\tYFailure\tn: You cannot smell $N's aura.", ch, NULL, victim, TO_CHAR, 1);
			else
				act("\tYFailure\tn: You cannot see $N's aura.", ch, NULL, victim, TO_CHAR, 1);
			return;
		}

		send_to_char(Format("\tGSuccess\tn: %s is surrounded by", IS_NPC( victim ) ? victim->short_descr : victim->name), ch);
		if(success >= 1)
		{
			switch(victim->race)
			{
				case RACE_WEREWOLF:
				strncat(buf,	" an intense and vibrant aura which moves like flames", sizeof(buf) - strlen(buf) - 1);
				break;
				case RACE_CHANGELING:
				strncat(buf, " a bright iridescent aura", sizeof(buf) - strlen(buf) - 1);
				break;
				case RACE_VAMPIRE:
				strncat(buf, " a pale aura", sizeof(buf) - strlen(buf) - 1);
				break;
				case RACE_HUMAN:
				strncat(buf, " a rosy aura", sizeof(buf) - strlen(buf) - 1);
				break;
			}
			if(success == 1)
			{
				strncat(buf, ".\n\r", sizeof(buf) - strlen(buf) - 1);
			}

		}
		if(success >= 2)
		{
			if(victim->condition[COND_ANGER] > 50)
				strncat(buf, " with red bursts", sizeof(buf) - strlen(buf) - 1);
			if(victim->condition[COND_PAIN] > 50)
				strncat(buf, " with patches of red and purples", sizeof(buf) - strlen(buf) - 1);
			if(victim->condition[COND_FEAR] > 50)
				strncat(buf, " with orange skittering lines", sizeof(buf) - strlen(buf) - 1);
			if(victim->condition[COND_FRENZY] > 50)
				strncat(buf, " that is rapidly rippling", sizeof(buf) - strlen(buf) - 1);
			if(victim->condition[COND_ANGER] < 50
				&& victim->condition[COND_FEAR] < 50
				&& victim->condition[COND_FRENZY] < 50
				&& victim->condition[COND_PAIN] < 50)
				strncat(buf, " that is mild blue color", sizeof(buf) - strlen(buf) - 1);
			if(success >= 2)
			{
				strncat(buf, ".\n\r", sizeof(buf) - strlen(buf) - 1);
			}
		}
		if(success >= 3)
		{
			strncat(buf, "You also see color patterns", sizeof(buf) - strlen(buf) - 1);

			if(victim->clan == clan_lookup("brujah"))
				strncat(buf, " in purple and violet", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("gangrel"))
				strncat(buf, " of green and brown", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("malkavian"))
				strncat(buf, " of yellow", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("nosferatu"))
				strncat(buf, " in a sickly green", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("toreador"))
				strncat(buf, " of green", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("tremere"))
				strncat(buf, " in brown and light green", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("ventrue"))
				strncat(buf, " in lavender",sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("lasombra"))
				strncat(buf, " of pulsating and writhing black and grey", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("tzimisce"))
				strncat(buf, " in a myriad of sparkling green", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("assamite"))
				strncat(buf, " of blue with black arteries", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("settite"))
				strncat(buf, " in a deep red and gold", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("giovanni"))
				strncat(buf, " in a deep red and green", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("ravnos"))
				strncat(buf, " in sharp, flickering black and violet", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("child of cacophony"))
				strncat(buf, " in a mottled silver and yellow", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("bone"))
				strncat(buf, " of gold and light green", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("glass"))
				strncat(buf, " of gold and deep red", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("malk"))
				strncat(buf, " with hypnotic swirling colors.\n\r", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("tremere"))
				strncat(buf, " with a myriad of sparkles.\n\r", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("soc. of Leopold"))
				strncat(buf, " with bright gold.\n\r", sizeof(buf) - strlen(buf) - 1);

			else if(victim->clan == clan_lookup("silver"))
				strncat(buf, " of brown and gold", sizeof(buf) - strlen(buf) - 1);
			/*
			else if(victim->clan == clan_lookup("silent"))
				strncat(buf, " of gold and silver", sizeof(buf) - strlen(buf) - 1);
			 */
			else
				strncat(buf, " of white", sizeof(buf) - strlen(buf) - 1);

			if(success == 3)
			{
				strncat(buf, ".\n\r", sizeof(buf) - strlen(buf) - 1);
			}
		}

		if(success >= 4)
		{
			if(victim->GHB > 7)
				strncat(buf, " with golden flecks.\n\r", sizeof(buf) - strlen(buf) - 1);
			else if(victim->GHB < 3)
				strncat(buf, " with white flecks.\n\r", sizeof(buf) - strlen(buf) - 1);
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

	difficulty = 8;
	power_stat = get_curr_stat(ch, STAT_PER);
	power_ability = ch->ability[EMPATHY].value;

	success = dice_rolls(ch, power_stat + power_ability, difficulty);

	if( !IS_VAMPIRE(ch) || ch->disc[DISC_AUSPEX] < 3 )
	{
		send_to_char("\tRWARNING: You do not know Spirit's Touch\tn.\n\r", ch);
		return;
	}

	if (!has_enough_power(ch))
		return;

	if( (obj = get_obj_carry(ch, argument, ch)) == NULL )
	{
		send_to_char("You don't have that object.\n\r", ch);
		log_string(LOG_GAME, (char *)Format("Tried to spirittouch %s", argument));
		return;
	}

	if(IS_NULLSTR(argument))
	{
		send_to_char("What are you trying to learn about?\n\r", ch);
		return;
	}

	if(success<0)
	{
		send_to_char("You are overwhelmed by the psychic impressions on this object and it gives you a massive headache!", ch);
		ch->power_timer = 3;
	}

	if(success==0)
	{
		send_to_char("You get nothing off of this object.", ch);
		ch->power_timer = 3;
	}

	if(success>0)
	{
		if(IS_NULLSTR(obj->owner) || IS_BUILDERMODE(obj))
			send_to_char("No-one has touched this object.\n\r", ch);
		else
		{
			send_to_char( Format("You receive an image of %s holding %s.\n\r", obj->owner, obj->short_descr), ch );
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

	difficulty = 8;
	power_stat = get_curr_stat(ch, STAT_PER);
	power_ability = ch->ability[OCCULT].value;

	success = dice_rolls(ch, power_stat + power_ability, difficulty);

	if( !IS_VAMPIRE(ch) || ch->disc[DISC_AUSPEX] < 5)
	{
		send_to_char("\tRWARNING: You do not know Psychic Projection\tn.\n\r", ch);
		return;
	}

	if(ch->power_timer > 0 && !IS_SET(ch->act2, ACT2_ASTRAL))
	{
		send_to_char("Your powers have not rejuvenated yet.\n\r", ch);
		return;
	}

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
		send_to_char("\tYFailure\tn: Nothing happens.\n\r", ch);
	}
}
