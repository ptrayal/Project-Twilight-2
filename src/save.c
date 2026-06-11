/***************************************************************************
 * Much of the code is the original work of Peter Fitzgerald who turned    *
 * it over to Brandon Morrison who has adopted and improved the code.      *
 * Copyright (C) 2012 - 2024                                               *
 **************************************************************************/

#if defined(Macintosh)
#include <types.h>
#else
#include <sys/types.h>
#endif
#include <ctype.h>
#include <time.h>
#include <malloc.h>
#include "twilight.h"
#include "recycle.h"
#include "tables.h"
#include "lookup.h"
#include "olc.h"

#if !defined(Macintosh)
extern  int     _filbuf         args( (FILE *) );
#endif


int rename(const char *oldfname, const char *newfname);

/* Strip trailing newlines and spaces from a string - returns new allocated string */
static char *strip_trailing_whitespace(const char *str)
{
	size_t len = 0;
	char *result = NULL;

	if (str == NULL || str[0] == '\0')
	{
		result = malloc(1);
		if (result) result[0] = '\0';
		return result;
	}

	len = strlen(str);

	/* Find last non-whitespace character */
	while (len > 0 && (str[len-1] == '\n' || str[len-1] == '\r' || str[len-1] == ' ' || str[len-1] == '\t'))
		len--;

	/* Allocate and copy */
	result = malloc(len + 1);
	if (result == NULL)
		return str_dup(str); /* Fallback to original on malloc failure */

	strncpy(result, str, len);
	result[len] = '\0';
	return result;
}

/* Safe version: writes flags to provided buffer */
void print_flags_buf(int flag, char *buf, size_t buflen)
{
    int count = 0;
    int pos = 0;

    if (buf == NULL || buflen < 2)
        return;

    for (count = 0; count < 32 && pos < (int)(buflen - 1); count++)
    {
        if (IS_SET(flag,1<<count))
        {
            if (count < 26)
                buf[pos] = 'A' + count;
            else
                buf[pos] = 'a' + (count - 26);
            pos++;
        }
    }

    if (pos == 0)
    {
        buf[pos] = '0';
        pos++;
    }

    buf[pos] = '\0';
}

/* DEPRECATED: Unsafe static buffer version - kept for compatibility, do not use in new code */
char *print_flags(int flag)
{
    static char buf[52]={'\0'};
    print_flags_buf(flag, buf, sizeof(buf));
    return buf;
}


/*
 * Array of containers read for proper re-nesting of objects.
 */
#define MAX_NEST	100
static	OBJ_DATA *	rgObjNest	[MAX_NEST];

/*
 * Local functions.
 */
void	fwrite_char	args( ( CHAR_DATA *ch,  FILE *fp ) );
void	fwrite_obj	args( ( CHAR_DATA *ch,  OBJ_DATA  *obj,
			    FILE *fp, int iNest ) );
void	fwrite_pet	args( ( CHAR_DATA *pet, FILE *fp ) );
void	add_to_resets	args( ( CHAR_DATA *ch ) );
void	fread_char	args( ( CHAR_DATA *ch,  FILE *fp, bool log_load ) );
void    fread_pet	args( ( CHAR_DATA *ch,  FILE *fp ) );
void	fread_obj	args( ( CHAR_DATA *ch,  FILE *fp ) );



/*
 * Save a character and inventory.
 * Would be cool to save NPC's too for quest purposes,
 *   some of the infrastructure is provided.
 */
void save_char_obj( CHAR_DATA *ch )
{
	FILE *fp;
	char strsave[MIL]={'\0'};
	int i = 0;

	if ( ch->desc != NULL && ch->desc->original != NULL )
		ch = ch->desc->original;

	closeReserve();
	if( IS_NPC(ch) )
	{
		snprintf( strsave, sizeof(strsave), "%s%s", NPC_DIR, capitalize( ch->name ) );
	}
	else
	{
		snprintf( strsave, sizeof(strsave), "%s%s", PLAYER_DIR, capitalize( ch->name ) );
	}

	if ( ( fp = fopen( TEMP_FILE, "w" ) ) == NULL )
	{
		log_string(LOG_BUG, "Save_char_obj: fopen");
		perror( strsave );
		openReserve();
		return;
	}

	fwrite_char( ch, fp );
	if ( ch->carrying != NULL )
	{
		if(!IS_NPC(ch))
		{
			fwrite_obj( ch, ch->carrying, fp, 0 );
		}
		else
		{
			add_to_resets( ch );
		}
	}
	if ( ch->home > 0 )
	{
		for(i = 0; i < ch->home; i++)
		{
			if(ch->rooms[i] != NULL && ch->rooms[i]->contents != NULL)
			{
				fwrite_obj( ch, ch->rooms[i]->contents, fp, 0 );
			}
		}
	}
	/* save the pets */
	if (ch->pet != NULL && ch->pet->in_room == ch->in_room)
	{
		fwrite_pet(ch->pet,fp);
	}
	fprintf( fp, "#END\n" );
	fclose( fp );
	rename(TEMP_FILE,strsave);
	openReserve();
	return;
}

void add_to_resets( CHAR_DATA *ch )
{
	/* HARD BIT */
	OBJ_DATA *contents = ch->in_room->contents;
	OBJ_DATA *tmp = ch->in_room->contents;
	CHAR_DATA *people = ch->in_room->people;
	RESET_DATA *reset = NULL;

	while(people != NULL)
	{
		if(people->in_room == ch->in_room)
		{
			if(!IS_NPC(people))
			{
				people = people->next;
			}
			else
			{
				if(ch->in_room == NULL || ch->in_room->area == NULL || ch->in_room->area->reset_last == NULL)
				{
					people = people->next;
					continue;
				}

				reset = new_reset_data();
				ch->in_room->area->reset_last->next = reset;
				reset->next = NULL;
				reset->command = 'M';
				reset->arg1 = people->pIndexData->vnum;
				reset->arg2 = people->in_room->vnum;
				reset->arg3 = 1;
				reset->arg4 = 0;
				if(people->carrying != NULL)
				{
					for(contents = people->carrying;
						((contents != NULL) && (contents->carried_by == people));
						contents = contents->next)
					{
						reset = new_reset_data();
						ch->in_room->area->reset_last->next = reset;
						reset->next = NULL;
						reset->command = 'G';
						reset->arg1 = contents->pIndexData->vnum;
						reset->arg2 = people->pIndexData->vnum;
						reset->arg3 = 1;
						reset->arg4 = 0;
						if(contents->contains != NULL)
						{
							tmp = contents->contains;
							while( (tmp != NULL) && (tmp->in_obj == contents) )
							{
								reset = new_reset_data();
								ch->in_room->area->reset_last->next = reset;
								reset->next = NULL;
								reset->command = 'P';
								reset->arg1 = tmp->pIndexData->vnum;
								reset->arg2 = tmp->in_obj->pIndexData->vnum;
								reset->arg3 = 1;
								reset->arg4 = 0;
								tmp = tmp->next_content;
							}
						}
					}
				}
				people = people->next;
			}
		}
	}
}

void fwrite_exit(FILE *fp, EXIT_DATA *exit, int i)
{
	fprintf(fp, "D%d", i);
	fprintf(fp, "%s~\n\r", exit->keyword);
	fprintf(fp, "%s~\n\r", exit->description);
	fprintf(fp, "%d %d %d\n\r", exit->exit_info, exit->key, exit->u1.vnum);
}

/*
 * Write the char.
 */
void fwrite_char( CHAR_DATA *ch, FILE *fp )
{
    AFFECT_DATA *paf;
    TRAIT_DATA *trait;
    STOCKS *stock;
    int sn = 0, pos = 0;
    int i = 0;
	time_t current_time = time(NULL); // Get the current epoch time

	if(IS_SET(ch->affected_by2, AFF2_EARTHMELD))
	{
	    REMOVE_BIT(ch->affected_by2, AFF2_EARTHMELD);
	}

	WriteToFile(fp, false, "#", IS_NPC(ch) ? "MOB" : "PLAYER");

	WriteNumber(fp, "Ver", ch->version);
	{
		char *tmp = strip_trailing_whitespace(ch->name);
		WriteToFile(fp, true, "Name", tmp);
		free(tmp);
	}
	{
		char *tmp = strip_trailing_whitespace(ch->surname);
		WriteToFile(fp, true, "SName", tmp);
		free(tmp);
	}
	{
		char *tmp = strip_trailing_whitespace(ch->alt_name);
		WriteToFile(fp, true, "AName", tmp);
		free(tmp);
	}

	WriteLong(fp, "Id", ch->id);
	WriteLong(fp, "LogO", current_time); // Save the current logon time as epoch time
    // WriteLong(fp, "LastO", ch->laston); // This should be storing the epoch time directly
    WriteLong(fp, "LastV", ch->pcdata->last_vote);


	{
		char *tmp = strip_trailing_whitespace(ch->short_descr);
		WriteToFile(fp, true, "ShD", tmp);
		free(tmp);
	}
	{
		char *tmp = strip_trailing_whitespace(ch->long_descr);
		WriteToFile(fp, true, "LnD", tmp);
		free(tmp);
	}
	{
		char *tmp = strip_trailing_whitespace(ch->description);
		WriteToFile(fp, true, "Desc", tmp);
		free(tmp);
	}
	{
		char *tmp = strip_trailing_whitespace(ch->switch_desc);
		WriteToFile(fp, true, "SDesc", tmp);
		free(tmp);
	}

	WriteNumber(fp, "Race", ch->race);

    fprintf( fp, "AusBr %d %d\n", ch->auspice, ch->breed);

	WriteNumber(fp, "Gen", ch->gen);
	WriteNumber(fp, "Shape", ch->shape);
	WriteNumber(fp, "Warrants", ch->warrants);

	fprintf( fp, "Hunted %d\n",  ch->hunter_vis	);

	{
		char *tmp = strip_trailing_whitespace(clan_table[ch->clan].name);
		WriteToFile(fp, true, "Cln", tmp);
		free(tmp);
	}
	{
		char *tmp = strip_trailing_whitespace(ch->material);
		WriteToFile(fp, true, "Material", tmp);
		free(tmp);
	}
	/* WriteToFile(fp, true, "Sire", sire_table[ch->sire].name ); */

	fprintf( fp, "CPow ");
	for(i = 0; i < 3; i++)
    	fprintf( fp, "%d ", ch->clan_powers[i] );
	fprintf( fp, "\n" );

	WriteNumber(fp, "Sex", ch->sex);
	WriteNumber(fp, "Tru", ch->trust);
	WriteNumber(fp, "Sec", ch->pcdata->security);
	WriteNumber(fp, "Playd", ch->played + (int) (current_time - ch->logon)	);
	WriteLong(fp, "Not", ch->pcdata->last_note);
	WriteNumber(fp, "Scro", ch->lines);

	 WriteNumber(fp, "Room",
        (  ch->in_room == get_room_index( ROOM_VNUM_LIMBO )
         && ch->was_in_room != NULL )
             ? ch->was_in_room->vnum
             : ch->in_room == NULL ? 10000 : ch->in_room->vnum );
    WriteNumber(fp, "Listen",  ch->listening == NULL ? 0 : ch->listening->vnum );

	{
		char *tmp = strip_trailing_whitespace(ch->prompt);
		WriteToFile(fp, true, "Prompt", tmp);
		free(tmp);
	}
    fprintf( fp, "HMV  %d %d\n", ch->health, ch->agghealth );
    fprintf( fp, "Virt  %d %d %d\n", ch->virtues[0], ch->virtues[1], ch->virtues[2] );
    fprintf( fp, "RGW %d %d %d %d %d %d\n", ch->RBPG, ch->max_RBPG, ch->GHB, ch->max_GHB, ch->willpower, ch->max_willpower );

    fprintf( fp, "Dsc " );
    for(i = 0; i < MAX_DISC; i++)
	fprintf( fp, "%d ", ch->disc[i] );
    fprintf( fp, "\n" );

    fprintf( fp, "Pow " );
    for(i = 0; i < MAX_POWERS; i++)
    {
	char flagbuf[52]={'\0'};
	print_flags_buf(ch->powers[i], flagbuf, sizeof(flagbuf));
	fprintf( fp, "%s ", flagbuf);
    }
    fprintf( fp, "\n" );

    fprintf( fp, "Status " );
    for(i = MAX_BACKGROUND; i < MAX_BG; i++)
	fprintf( fp, "%d ", ch->backgrounds[i] );
    fprintf( fp, "\n" );

    fprintf( fp, "Infl " );
    for(i = 0; i < MAX_INFL; i++)
	fprintf( fp, "%d ", ch->influences[i] );
    fprintf( fp, "\n" );

    fprintf( fp, "Back " );
    for(i = 0; i < MAX_BACKGROUND; i++)
	fprintf( fp, "%d ", ch->backgrounds[i] );
    fprintf( fp, "\n" );

	{
		char *tmp = strip_trailing_whitespace(ch->married);
		WriteToFile(fp, true, "Marry", tmp);
		free(tmp);
	}
	{
		char *tmp = strip_trailing_whitespace(ch->ghouled_by);
		WriteToFile(fp, true, "Ghld", tmp);
		free(tmp);
	}

    WriteNumber( fp, "BgTime",	ch->bg_timer );
    WriteNumber( fp, "BgCount",ch->bg_count );

    WriteNumber( fp, "Herdt",	ch->herd_timer );

    WriteNumber( fp, "Age",	ch->char_age );

    fprintf( fp, "Oocxp %d %d\n",	ch->ooc_xp_time, ch->ooc_xp_count );

	{
		char *tmp = strip_trailing_whitespace(ch->pcdata->ignore_reject);
		WriteToFile(fp, true, "Reject", tmp);
		free(tmp);
	}
	{
		char *tmp = strip_trailing_whitespace(ch->pcdata->block_join);
		WriteToFile(fp, true, "Block", tmp);
		free(tmp);
	}

    if (ch->dollars > 0)
    {
    	WriteNumber( fp, "Dollars", ch->dollars );
    }
    else
    {
    	WriteNumber( fp, "Dollars", 0 );
    }

    if (ch->cents > 0)
    {
    	WriteNumber( fp, "Cents", ch->cents );
    }
    else
    {
    	WriteNumber( fp, "Cents", 0 );
    }

    if (ch->bank_account > 0)
    	WriteLong( fp, "Bank", ch->bank_account );
    else
    	WriteNumber( fp, "Bank", 0 );

    WriteNumber( fp, "Exp", ch->exp );
    WriteNumber( fp, "OocExp", ch->oocxp );

    if (ch->act != 0)
    {
    	char flagbuf[52]={'\0'};
    	print_flags_buf(ch->act, flagbuf, sizeof(flagbuf));
    	WriteToFile( fp, true, "Act", flagbuf );
    }
    if (ch->act2 != 0)
    {
    	char flagbuf[52]={'\0'};
    	print_flags_buf(ch->act2, flagbuf, sizeof(flagbuf));
    	WriteToFile( fp, true, "Act2", flagbuf );
    }
    if (ch->plr_flags != 0)
    {
    	char flagbuf[52]={'\0'};
    	print_flags_buf(ch->plr_flags, flagbuf, sizeof(flagbuf));
    	WriteToFile( fp, true, "Pflgs", flagbuf );
    }
    if (ch->affected_by != 0)
    {
    	char flagbuf[52]={'\0'};
    	print_flags_buf(ch->affected_by, flagbuf, sizeof(flagbuf));
    	WriteToFile( fp, true, "AfBy", flagbuf );
    }
    if (ch->affected_by2 != 0)
    {
    	char flagbuf[52]={'\0'};
    	print_flags_buf(ch->affected_by2, flagbuf, sizeof(flagbuf));
    	WriteToFile( fp, true, "AfBy2", flagbuf );
    }
    if (ch->parts != race_table[ch->race].parts)
    {
    	char flagbuf[52]={'\0'};
    	print_flags_buf(ch->parts, flagbuf, sizeof(flagbuf));
    	WriteToFile( fp, true, "Part", flagbuf );
    }
    if (ch->form != race_table[ch->race].form)
    {
    	char flagbuf[52]={'\0'};
    	print_flags_buf(ch->form, flagbuf, sizeof(flagbuf));
    	WriteToFile( fp, true, "Form", flagbuf );
    }

    {
    	char flagbuf[52]={'\0'};
    	print_flags_buf(ch->comm, flagbuf, sizeof(flagbuf));
    	WriteToFile( fp, true, "Comm", flagbuf );
    }

    if (ch->wiznet)
    {
    	char flagbuf[52]={'\0'};
    	print_flags_buf(ch->wiznet, flagbuf, sizeof(flagbuf));
    	WriteToFile( fp, true, "Wizn", flagbuf);
    }
    if (ch->invis_level)
    	WriteNumber( fp, "Invi", ch->invis_level	);
    if (ch->incog_level)
    	WriteNumber(fp,"Inco",ch->incog_level);

    WriteNumber( fp, "Pos", ch->position == P_FIGHT ? P_STAND : ch->position );

    if (ch->saving_throw != 0)
    	WriteNumber( fp, "Save",	ch->saving_throw);

    fprintf( fp, "Attr %d %d %d %d %d %d %d %d %d\n",
	ch->perm_stat[STAT_STR],
	ch->perm_stat[STAT_DEX],
	ch->perm_stat[STAT_STA],
	ch->perm_stat[STAT_CHA],
	ch->perm_stat[STAT_MAN],
	ch->perm_stat[STAT_APP],
	ch->perm_stat[STAT_PER],
	ch->perm_stat[STAT_INT],
	ch->perm_stat[STAT_WIT] );

    fprintf (fp, "AMod %d %d %d %d %d %d %d %d %d\n",
	ch->mod_stat[STAT_STR],
	ch->mod_stat[STAT_DEX],
	ch->mod_stat[STAT_STA],
	ch->mod_stat[STAT_CHA],
	ch->mod_stat[STAT_MAN],
	ch->mod_stat[STAT_APP],
	ch->mod_stat[STAT_PER],
	ch->mod_stat[STAT_INT],
	ch->mod_stat[STAT_WIT] );

	{
		char *tmp = strip_trailing_whitespace(ch->email_addr);
		WriteToFile(fp, true, "Email", tmp);
		free(tmp);
	}
    WriteLong (fp, "Econfnum", ch->email_lock );

    fprintf (fp, "Prof %s %ld %d %d\n",
	ch->profession != NULL ? ch->profession : "None", ch->employer, ch->pospts, ch->markup );

	{
		char *tmp = strip_trailing_whitespace(ch->nature);
		WriteToFile(fp, true, "Nature", tmp);
		free(tmp);
	}
	{
		char *tmp = strip_trailing_whitespace(ch->demeanor);
		WriteToFile(fp, true, "Demnor", tmp);
		free(tmp);
	}
	{
		char *tmp = strip_trailing_whitespace(ch->pack);
		WriteToFile(fp, true, "Pack", tmp);
		free(tmp);
	}

    fprintf( fp, "Totem %d %d\n", ch->totem_attitudes[0], ch->totem_attitudes[1] );

    for ( sn = 0; sn < MAX_ABIL; sn++ )
    {
    	if ( ability_table[sn].name != NULL && IS_ATTRIB_AVAILABLE(ch->race,sn))
    	{
    		fprintf( fp, "Ab %d '%s'\n", ch->ability[sn].value, ability_table[sn].name );
    	}
    }

    for(stock = ch->stocks; stock != NULL; stock = stock->next)
    {
    	if(stock->cost > 0)
    	{
    		fprintf( fp, "STOCK %s~ %d %ld\n", stock->ticker, stock->cost, stock->cost_basis );
    	}
    }

    for(trait = ch->traits; trait != NULL; trait = trait->next)
    {
    	fprintf( fp, "Trait %d %d %s~ %s~\n", trait->type, trait->value,
    		trait->qualifier != NULL ? trait->qualifier : "",
    		trait->detail != NULL ? trait->detail : "" );
    }

    if ( IS_NPC(ch) )
    {
    	WriteNumber( fp, "Vnum",	ch->pIndexData->vnum	);
    	if(IS_INTELLIGENT(ch))
    		fprintf( fp, "Aifile %s.ai\n", ch->name );
    }
    else
    {
		/* Account linkage — written first so it's easy to find on load */
		if ( !IS_NULLSTR(ch->pcdata->acct_login) )
		{
			char *tmp = strip_trailing_whitespace(ch->pcdata->acct_login);
			WriteToFile(fp, true, "AcLg", tmp);
			free(tmp);
			fprintf( fp, "AcId %ld\n", ch->pcdata->acct_id );
		}
		{
			char *tmp = strip_trailing_whitespace(ch->pcdata->pwd);
			WriteToFile(fp, true, "Pass", tmp);
			free(tmp);
		}
		{
			char *tmp = strip_trailing_whitespace(ch->pcdata->bamfin);
			WriteToFile(fp, true, "Bin", tmp);
			free(tmp);
		}
		{
			char *tmp = strip_trailing_whitespace(ch->pcdata->bamfout);
			WriteToFile(fp, true, "Bout", tmp);
			free(tmp);
		}
		{
			char *tmp = strip_trailing_whitespace(ch->pcdata->title);
			WriteToFile(fp, true, "Titl", tmp);
			free(tmp);
		}
		{
			char *tmp = strip_trailing_whitespace(ch->pcdata->rpok_string);
			WriteToFile(fp, true, "RPTitle", tmp);
			free(tmp);
		}
    	fprintf( fp, "Colours %s~ %s~\n",
    		ch->colours[0] != NULL ? ch->colours[0] : "",
    		ch->colours[1] != NULL ? ch->colours[1] : "" );
    	WriteNumber( fp, "Pnts", ch->pcdata->points );
    	if(ch->xpgift)
    		WriteNumber( fp, "Gift", ch->xpgift );
    	else
    		WriteNumber( fp, "Gift", 0 );
    	WriteNumber( fp, "TSex", ch->pcdata->true_sex );
    	fprintf( fp, "Condit  %d %d %d %d %d %d %d %d %d %d %d\n",
    			ch->condition[0],
    			ch->condition[1],
    			ch->condition[2],
    			ch->condition[3],
    			ch->condition[4],
    			ch->condition[5],
    			ch->condition[6],
    			ch->condition[7],
    			ch->condition[8],
    			ch->condition[9],
    			ch->condition[10] );

    	/* Player home. */
    	WriteNumber(fp, "Home", ch->home);

    	if(ch->home > 0)
    		fprintf(fp, "MyRms ");
    	for (pos = 0; pos < ch->home; pos++)
    	{
    		if(ch->rooms[pos] != NULL)
    		{
    			fprintf(fp, "%d ", ch->rooms[pos]->vnum);
    		}
    	}
    	if(ch->home > 0)
    		fprintf(fp, "\n");

    	/* write alias */
    	for (pos = 0; pos < MAX_ALIAS; pos++)
    	{
    		if (ch->pcdata->alias[pos] == NULL ||  ch->pcdata->alias_sub[pos] == NULL)
    			break;

    		fprintf(fp,"Alias %s %s~\n",ch->pcdata->alias[pos], ch->pcdata->alias_sub[pos]);
    	}

    	for ( sn = 0; sn < MAX_SKILL; sn++ )
    	{
    		if ( skill_table[sn].name != NULL && ch->learned[sn] > 0 )
    		{
    			fprintf( fp, "Sk %d '%s'\n", ch->learned[sn], skill_table[sn].name );
    		}
    	}
    }

    for ( paf = ch->affected; paf != NULL; paf = paf->next )
    {
    	if (paf->type < 0 || paf->type>= MAX_SKILL)
    		continue;

    	fprintf( fp, "Affc '%s' %3d %3d %3d %3d %3d %10d\n",
    			skill_table[paf->type].name,
    			paf->where,
    			paf->level,
    			paf->duration,
    			paf->modifier,
    			paf->location,
    			paf->bitvector
    	);
    }

	{
		char *tmp = strip_trailing_whitespace(ch->ignore);
		WriteToFile(fp, true, "Ign", tmp);
		free(tmp);
	}
	{
		int ki;
		for (ki = 0; ki < ch->n_known_rites; ki++)
			fprintf(fp, "KnRite  %d\n", ch->known_rite_ids[ki]);
	}
    fprintf( fp, "End\n\n" );
    return;
}

/* write a pet */
void fwrite_pet( CHAR_DATA *pet, FILE *fp)
{
	AFFECT_DATA *paf;
	int i = 0;
	time_t current_time = time(NULL);

	fprintf(fp,"#PET\n");

	WriteNumber(fp,"Vnum",pet->pIndexData->vnum);

	{
		char *tmp = strip_trailing_whitespace(pet->name);
		WriteToFile(fp, true, "Name", tmp);
		free(tmp);
	}
	WriteLong(fp,"LogO", current_time);
	{
		char *tmp = strip_trailing_whitespace(pet->short_descr);
		WriteToFile(fp, true, "ShD", tmp);
		free(tmp);
	}
	{
		char *tmp = strip_trailing_whitespace(pet->long_descr);
		WriteToFile(fp, true, "LnD", tmp);
		free(tmp);
	}
	{
		char *tmp = strip_trailing_whitespace(pet->description);
		WriteToFile(fp, true, "Desc", tmp);
		free(tmp);
	}
	{
		char *tmp = strip_trailing_whitespace(race_table[pet->race].name);
		WriteToFile(fp, true, "Race", tmp);
		free(tmp);
	}

	if (pet->clan)
	{
		char *tmp = strip_trailing_whitespace(clan_table[pet->clan].name);
		WriteToFile(fp, true, "Cln", tmp);
		free(tmp);
		fprintf( fp, "CPow ");
		for(i = 0; i < 3; i++)
			fprintf( fp, "%d ", pet->clan_powers[i] );
		fprintf( fp, "\n" );
	}
	WriteNumber(fp,"Sex", pet->sex);
	fprintf(fp, "HMV  %d %d\n",
			pet->health, pet->agghealth);
	if (pet->dollars > 0)
		WriteNumber(fp,"Dollars",pet->dollars);
	if (pet->cents > 0)
		WriteNumber(fp,"Cents",pet->cents);
	if (pet->exp > 0)
		WriteNumber(fp, "Exp", pet->exp);
	if (pet->oocxp > 0)
		WriteNumber( fp, "OocExp",	pet->oocxp	);
	if (pet->act != pet->pIndexData->act)
	{
		char flagbuf[52]={'\0'};
		print_flags_buf(pet->act, flagbuf, sizeof(flagbuf));
		WriteToFile(fp, true, "Act", flagbuf);
	}
	if (pet->act2 != 0)
	{
		char flagbuf[52]={'\0'};
		print_flags_buf(pet->act2, flagbuf, sizeof(flagbuf));
		WriteToFile(fp, true, "Act2", flagbuf);
	}
	if (pet->affected_by != pet->pIndexData->affected_by)
	{
		char flagbuf[52]={'\0'};
		print_flags_buf(pet->affected_by, flagbuf, sizeof(flagbuf));
		WriteToFile(fp, true, "AfBy", flagbuf);
	}
	if (pet->affected_by2 != pet->pIndexData->affected_by2)
	{
		char flagbuf[52]={'\0'};
		print_flags_buf(pet->affected_by2, flagbuf, sizeof(flagbuf));
		WriteToFile(fp, true, "AfBy2", flagbuf);
	}
	if (pet->comm != 0)
	{
		char flagbuf[52]={'\0'};
		print_flags_buf(pet->comm, flagbuf, sizeof(flagbuf));
		WriteToFile(fp, true, "Comm", flagbuf);
	}
	WriteNumber(fp, "Pos", pet->position == P_FIGHT ? P_STAND : pet->position);
	if (pet->saving_throw != 0)
		WriteNumber(fp, "Save", pet->saving_throw);
	fprintf(fp, "Attr %d %d %d %d %d %d %d %d %d\n",
			pet->perm_stat[STAT_STR], pet->perm_stat[STAT_DEX],
			pet->perm_stat[STAT_STA], pet->perm_stat[STAT_CHA],
			pet->perm_stat[STAT_MAN], pet->perm_stat[STAT_APP],
			pet->perm_stat[STAT_PER], pet->perm_stat[STAT_INT],
			pet->perm_stat[STAT_WIT]);
	fprintf(fp, "AMod %d %d %d %d %d %d %d %d %d\n",
			pet->mod_stat[STAT_STR], pet->mod_stat[STAT_DEX],
			pet->mod_stat[STAT_STA], pet->mod_stat[STAT_CHA],
			pet->mod_stat[STAT_MAN], pet->mod_stat[STAT_APP],
			pet->mod_stat[STAT_PER], pet->mod_stat[STAT_INT],
			pet->mod_stat[STAT_WIT]);

	for ( paf = pet->affected; paf != NULL; paf = paf->next )
	{
		if (paf->type < 0 || paf->type >= MAX_SKILL)
			continue;

		fprintf(fp, "Affc '%s' %3d %3d %3d %3d %3d %10d\n",
				skill_table[paf->type].name,
				paf->where, paf->level, paf->duration, paf->modifier,paf->location,
				paf->bitvector);
	}

	fprintf(fp,"End\n");
	return;
}

/*
 * Write an object and its contents.
 */
void fwrite_obj( CHAR_DATA *ch, OBJ_DATA *obj, FILE *fp, int iNest )
{
	EXTRA_DESCR_DATA *ed;
	AFFECT_DATA *paf;

	/*
	 * Slick recursion to write lists backwards,
	 *   so loading them will load in forwards order.
	 */
	if ( obj->next_content != NULL )
	{
		fwrite_obj( ch, obj->next_content, fp, iNest );
	}

	if  ( (obj->item_type == ITEM_KEY && !IS_SET(obj->extra2, OBJ_KEEP))
		||   (obj->item_type == ITEM_MAP && !obj->value[0]))
	{
		return;
	}

	if((obj->name == NULL || obj->name[0] == '\0')
		&& (obj->short_descr == NULL || obj->short_descr[0] == '\0')
		&& obj->description == NULL )
	{
		return;
	}

	fprintf( fp, "#O\n" );
	WriteNumber( fp, "Vnum", obj->pIndexData->vnum );
	if (obj->enchanted)
		fprintf( fp,"Enchanted\n");
	WriteNumber( fp, "Nest", iNest );

	/* these data are only used if they do not match the defaults */

	if ( obj->name != obj->pIndexData->name)
	{
		char *tmp = strip_trailing_whitespace(obj->name);
		WriteToFile( fp, true, "Name", tmp );
		free(tmp);
	}
	if ( obj->short_descr != obj->pIndexData->short_descr)
	{
		char *tmp = strip_trailing_whitespace(obj->short_descr);
		WriteToFile( fp, true, "ShD", tmp );
		free(tmp);
	}
	if ( obj->description != obj->pIndexData->description)
	{
		char *tmp = strip_trailing_whitespace(obj->description);
		WriteToFile( fp, true, "Desc", tmp );
		free(tmp);
	}
	if ( obj->full_desc != obj->pIndexData->full_desc)
	{
		char *tmp = strip_trailing_whitespace(obj->full_desc);
		fprintf( fp, "FDesc %s~\n",	tmp );
		free(tmp);
	}
	if ( obj->extra_flags != obj->pIndexData->extra_flags)
		WriteNumber( fp, "ExtF", obj->extra_flags );
	if ( obj->wear_flags != obj->pIndexData->wear_flags)
		WriteNumber( fp, "WeaF", obj->wear_flags );
	if ( obj->item_type != obj->pIndexData->item_type)
		WriteNumber( fp, "Ityp", obj->item_type );
	if ( obj->weight != obj->pIndexData->weight)
		WriteNumber( fp, "Wt", obj->weight );
	if ( obj->condition != obj->pIndexData->condition)
		WriteNumber( fp, "Cond", obj->condition	);
	if ( obj->uses != obj->pIndexData->uses)
		WriteNumber( fp, "Uses", obj->uses );
	if ( obj->quality != obj->pIndexData->quality)
		WriteNumber( fp, "Quality",	obj->quality );

	/* variable data */
	WriteNumber( fp, "Wear", obj->wear_loc );

	if(obj->wear_loc == WEAR_RESET)
		WriteLong( fp, "Reset", obj->reset_loc );
	if (obj->timer != 0)
		WriteNumber( fp, "Time", obj->timer );
	WriteNumber( fp, "Cost", obj->cost );
	if (obj->value[0] != obj->pIndexData->value[0] ||  obj->value[1] != obj->pIndexData->value[1]
		||  obj->value[2] != obj->pIndexData->value[2] ||  obj->value[3] != obj->pIndexData->value[3]
		||  obj->value[4] != obj->pIndexData->value[4] ||  obj->value[5] != obj->pIndexData->value[5])
		fprintf( fp, "Val  %d %d %d %d %d %d\n",
			obj->value[0], obj->value[1], obj->value[2], obj->value[3],
			obj->value[4], obj->value[5]	     );

	switch ( obj->item_type )
	{
		case ITEM_POTION:
		case ITEM_SCROLL:
		case ITEM_PILL:
		if ( obj->value[1] > 0 )
		{
			fprintf( fp, "Spell 1 '%s'\n", skill_table[obj->value[1]].name );
		}

		if ( obj->value[2] > 0 )
		{
			fprintf( fp, "Spell 2 '%s'\n", skill_table[obj->value[2]].name );
		}

		if ( obj->value[3] > 0 )
		{
			fprintf( fp, "Spell 3 '%s'\n", skill_table[obj->value[3]].name );
		}

		break;
	}

	for ( paf = obj->affected; paf != NULL; paf = paf->next )
	{
		if (paf->type < 0 || paf->type >= MAX_SKILL)
			continue;
		fprintf( fp, "Affc '%s' %3d %3d %3d %3d %3d %10d\n",
			skill_table[paf->type].name,
			paf->where,
			paf->level,
			paf->duration,
			paf->modifier,
			paf->location,
			paf->bitvector
			);
	}

	for ( ed = obj->extra_descr; ed != NULL; ed = ed->next )
	{
		fprintf( fp, "ExDe %s~ %s~\n",
			ed->keyword != NULL ? ed->keyword : "",
			ed->description != NULL ? ed->description : "" );
	}

	fprintf( fp, "End\n\n" );

	if ( obj->contains != NULL )
		fwrite_obj( ch, obj->contains, fp, iNest + 1 );

	return;
}



/*
 * Load a char and inventory into a new ch structure.
 */
bool load_char_obj( DESCRIPTOR_DATA *d, char *name, bool log_load, bool load_concept, bool load_backup )
{
	CHAR_DATA *ch;
	FILE *fp;
	int stat = 0;
	bool found = FALSE;
	char *directory;

	if(load_concept)
		directory = PLAYER_DIR;
	else if(load_backup)
		directory = PLAYER_BACKUP_DIR;
	else
		directory = PLAYER_DIR;

	ch = new_char();
	ch->pcdata = new_pcdata();

	d->character			= ch;
	ch->desc				= d;


	PURGE_DATA( ch->aifile );
	PURGE_DATA( ch->alt_description );
	PURGE_DATA( ch->alt_long_descr );
	PURGE_DATA( ch->alt_name );
	PURGE_DATA( ch->demeanor );
	PURGE_DATA( ch->description );
	PURGE_DATA( ch->email_addr );
	PURGE_DATA( ch->ghouled_by );
	PURGE_DATA( ch->ignore );
	PURGE_DATA( ch->long_descr );
	PURGE_DATA( ch->married );
	PURGE_DATA( ch->material );
	PURGE_DATA( ch->name );
	PURGE_DATA( ch->nature );
	PURGE_DATA( ch->oldname );
	PURGE_DATA( ch->pack );
	PURGE_DATA( ch->prefix );
	PURGE_DATA( ch->profession );
	PURGE_DATA( ch->prompt );
	PURGE_DATA( ch->short_descr );
	PURGE_DATA( ch->sire );
	PURGE_DATA( ch->surname );
	PURGE_DATA( ch->switch_desc );
	PURGE_DATA( ch->to_learn );

	PURGE_DATA( ch->pcdata->bamfin );
	PURGE_DATA( ch->pcdata->bamfout );
	PURGE_DATA( ch->pcdata->block_join );
	PURGE_DATA( ch->pcdata->ignore_reject );
	PURGE_DATA( ch->pcdata->pwd );
	PURGE_DATA( ch->pcdata->rpok_string );
	PURGE_DATA( ch->pcdata->title );

	ch->aifile 					= NULL;
	ch->alt_description			= NULL;
	ch->alt_long_descr			= NULL;
	ch->alt_name				= NULL;
	ch->demeanor				= str_dup( "None" );
	ch->description 			= str_dup("An impossibly descriptive individual");
	ch->email_addr				= NULL;
	ch->ghouled_by				= NULL;
	ch->ignore 					= NULL;
	ch->laston 					= time(NULL); // This sets laston to the current time in seconds since the epoch
	ch->long_descr				= NULL;
	ch->married					= NULL;
	ch->material				= str_dup("flesh");
	ch->name					= str_dup( name );
	ch->nature					= str_dup( "None" );
	ch->oldname					= NULL;
	ch->pack					= str_dup("None");
	ch->profession				= str_dup( "None" );
	ch->prompt 					= str_dup("<%h> ");
	ch->short_descr				= NULL;
	ch->sire					= str_dup( "None" );
	ch->surname					= NULL;
	ch->switch_desc				= NULL;
	ch->to_learn				= NULL;

	ch->pcdata->bamfin			= NULL;
	ch->pcdata->bamfout			= NULL;
	ch->pcdata->block_join		= NULL;
	ch->pcdata->ignore_reject	= NULL;
	ch->pcdata->pwd				= NULL;
	ch->pcdata->rpok_string		= str_dup( "Not Available" );
	ch->pcdata->title			= NULL;


	for (stat =0; stat < MAX_STATS; stat++)
		ch->perm_stat[stat]		= 1;
	for (stat =0; stat < MAX_POWERS; stat++)
		ch->powers[stat]		= 0;
	for (stat =MAX_BACKGROUND; stat < MAX_BG; stat++)
		ch->backgrounds[stat]		= 0;
	for (stat =0; stat < MAX_INFL; stat++)
		ch->influences[stat]		= 0;
	for (stat =0; stat < MAX_BACKGROUND; stat++)
		ch->backgrounds[stat]		= 0;

	ch->pcdata->confirm_delete		= FALSE;
	ch->pcdata->full_reset		= FALSE;
	ch->id					= get_pc_id();
	ch->race				= race_lookup("human");
	ch->plr_flags			= PLR_AUTOEXIT;
	ch->comm				= COMM_COMBINE|COMM_PROMPT;
	ch->email_lock			= 0;
	ch->bg_timer			= 6;
	ch->bg_count			= 0;
	ch->condition[COND_THIRST]	= 48;
	ch->condition[COND_FULL]	= 48;
	ch->condition[COND_HUNGER]	= 48;
	ch->condition[COND_SANITY]	= UMIN(number_fuzzy(90), 100);
	ch->pcdata->security		= 0; /*OLC*/
	ch->xpgift				= 0;
	ch->shape				= 0;
	ch->blood_timer			= 10;
	ch->power_timer			= 0;
	ch->jump_timer			= 0;
	ch->combat_flag			= 0;
	ch->pospts				= 0;
	ch->employer			= 0;
	ch->xp_job_acts			= 0;
	ch->trust				= 0;
	ch->auspice				= number_range(0, 4);
	ch->breed				= number_range(0, 2);
	ch->gen					= 13;
	ch->ooc_xp_time			= 0;
	ch->ooc_xp_count		= 0;
	ch->oocxp				= 0;
	ch->stock_ticker		= 0;
	ch->totem_attitudes[0]	= 3;
	ch->totem_attitudes[1]	= 3;
	ch->quest				= NULL;
	ch->home				= 0;
	for (stat =0; stat < MAX_OWNED_ROOMS; stat++)
		ch->rooms[stat]			= NULL;

	for (stat = 0; stat < MAX_JOB_SKILLS; stat++)
		ch->job_skill[stat] = 0;

	found = FALSE;
	closeReserve();

#if defined(__unix__)

	/* decompress if .gz file exists */
	char strsave[MAX_INPUT_LENGTH]={'\0'};
	char cmd[MSL]={'\0'};

	snprintf( strsave, sizeof(strsave), "%s%s%s", directory, capitalize(name),".gz");
	if ( ( fp = fopen( strsave, "r" ) ) != NULL )
	{
		fclose(fp);
		snprintf(cmd, sizeof(cmd), "gzip -dfq %s", strsave);
		int systemRet = system(cmd);
		if (systemRet == -1)
		{
			log_string(LOG_BUG, "Error in decompressing mud.  Custom Error 801.");
		}
	}
#endif

	char filepath[MIL]={'\0'};
	snprintf(filepath, sizeof(filepath), "%s%s", directory, capitalize(name));
	if ( ( fp = fopen( filepath, "r" ) ) != NULL )
	{
		int iNest;

		for ( iNest = 0; iNest < MAX_NEST; iNest++ )
			rgObjNest[iNest] = NULL;

		found = TRUE;
		for ( ; ; )
		{
			char letter;
			const char *word;

			letter = fread_letter( fp );
			if ( letter == '*' )
			{
				fread_to_eol( fp );
				continue;
			}

			if ( letter != '#' )
			{
				log_string(LOG_BUG, "Load_char_obj: # not found.");
				break;
			}

			word = fread_word( fp );
			if      ( !str_cmp( word, "PLAYER" ) ) fread_char (ch,fp,log_load);
			else if ( !str_cmp( word, "OBJECT" ) ) fread_obj  ( ch, fp );
			else if ( !str_cmp( word, "O"      ) ) fread_obj  ( ch, fp );
			else if ( !str_cmp( word, "PET"    ) ) fread_pet  ( ch, fp );
			else if ( !str_cmp( word, "END"    ) ) break;
			else
			{
				char logmsg[MSL]={'\0'};
				snprintf(logmsg, sizeof(logmsg), "Load_char_obj: bad section: %s", word);
				log_string(LOG_BUG, logmsg);
				break;
			}
		}
		fclose( fp );
	}

	openReserve();

	/* initialize race */
	if (found)
	{
		/*int i;*/

		if (ch->race == 0)
			ch->race = race_lookup("human");

		ch->size = pc_race_table[ch->race].size; /* @@@@@ */

		if(ch->shape == SHAPE_NONE || ch->shape == SHAPE_HUMAN)
			ch->dam_type = 17; /*punch */

		ch->affected_by = ch->affected_by|race_table[ch->race].aff;
		ch->imm_flags	= ch->imm_flags | race_table[ch->race].imm;
		ch->res_flags	= ch->res_flags | race_table[ch->race].res;
		ch->vuln_flags	= ch->vuln_flags | race_table[ch->race].vuln;
		ch->form	= ch->form | race_table[ch->race].form;
		ch->parts	= ch->parts | race_table[ch->race].parts;
		if(ch->health <= 0 || ch->agghealth <= 0 || (ch->health + ch->agghealth) <= 0) 
		{
			ch->health			= 7;
			ch->agghealth			= 7;
		}
	}

	return found;
}



/*
 * Read in a char.
 */

#if defined(KEY)
#undef KEY
#endif

#define KEY( literal, field, value ) if ( !str_cmp( word, literal ) )	\
				{					\
				    field  = value;			\
				    fMatch = TRUE;			\
				    break;				\
				}

/* provided to free strings */
#if defined(KEYS)
#undef KEYS
#endif

#define KEYS( literal, field, value )					\
				if ( !str_cmp( word, literal ) )	\
				{					\
					PURGE_DATA(field);			\
				    field  = value;			\
				    fMatch = TRUE;			\
				    break;				\
				}

void fread_char( CHAR_DATA *ch, FILE *fp, bool log_load )
{
	const char *word;
	int count = 0;
	time_t current_time = time(NULL);
	int lastlogoff = current_time;
	int percent = 0;
	int i = 0;
	bool fMatch;

	if(log_load)
	{
		char logmsg[MSL]={'\0'};
		snprintf(logmsg, sizeof(logmsg), "Loading %s.", ch->name);
		log_string(LOG_GAME, logmsg);
	}

	for ( ; ; )
	{
		word   = feof( fp ) ? "End" : fread_word( fp );
		fMatch = FALSE;

		switch ( UPPER(word[0]) )
		{
		case '*':
			fMatch = TRUE;
			fread_to_eol( fp );
			break;

		case '~':
			/* Orphaned tilde from old string formatting - skip it */
			fMatch = TRUE;
			break;

		case 'A':
			KEY( "Act",		ch->act,		fread_flag( fp ) );
			KEY( "Act2",	ch->act2,		fread_flag( fp ) );
			KEY( "AcId",	ch->pcdata->acct_id,	fread_number( fp ) );
			KEYS( "AcLg",	ch->pcdata->acct_login,	fread_string( fp ) );
			KEY( "AfBy",	ch->affected_by,	fread_flag( fp ) );
			KEY( "AfBy2",	ch->affected_by2,	fread_flag( fp ) );
			KEY( "Age",		ch->char_age,		fread_number( fp ) );
			KEY( "Aifile",	ch->aifile,		fread_word( fp ) );
			KEYS( "AName",	ch->alt_name,		fread_string( fp ) );

			if (!str_cmp(word,"Ab"))
			{
				int sn = 0;
				int value = 0;
				const char *temp;

				value = fread_number( fp );
				temp = fread_word( fp ) ;
				sn = abil_lookup(temp, ch);
				if ( sn < 0 )
				{
					fprintf(stderr,"%s",temp);
					log_string(LOG_BUG, "Fread_char: unknown ability. ");
				}
				else
					ch->ability[sn].value = value;
				fMatch = TRUE;
				break;
			}

			if (!str_cmp( word, "Alias"))
			{
				if (count >= MAX_ALIAS)
				{
					fread_to_eol(fp);
					fMatch = TRUE;
					break;
				}

				PURGE_DATA(ch->pcdata->alias[count]);
				PURGE_DATA(ch->pcdata->alias_sub[count]);
				ch->pcdata->alias[count]        = str_dup(fread_word(fp));
				ch->pcdata->alias_sub[count]    = fread_string(fp);
				count++;
				fMatch = TRUE;
				break;
			}

			if (!str_cmp(word, "AffD"))
			{
				AFFECT_DATA *paf;
				int sn = 0;

				paf = new_affect();

				sn = skill_lookup(fread_word(fp));
				if (sn < 0)
					log_string(LOG_BUG, "Fread_char: unknown skill.");
				else
					paf->type = sn;

				paf->level	= fread_number( fp );
				paf->duration	= fread_number( fp );
				paf->modifier	= fread_number( fp );
				paf->location	= fread_number( fp );
				paf->bitvector	= fread_number( fp );
				paf->next	= ch->affected;
				ch->affected	= paf;
				fMatch = TRUE;
				break;
			}

			if (!str_cmp(word, "Affc"))
			{
				AFFECT_DATA *paf;
				int sn = 0;

				paf = new_affect();

				sn = skill_lookup(fread_word(fp));
				if (sn < 0)
					log_string(LOG_BUG, "Fread_char: unknown skill.");
				else
					paf->type = sn;

				paf->where  = fread_number(fp);
				paf->level      = fread_number( fp );
				paf->duration   = fread_number( fp );
				paf->modifier   = fread_number( fp );
				paf->location   = fread_number( fp );
				paf->bitvector  = fread_number( fp );
				paf->next       = ch->affected;
				ch->affected    = paf;
				fMatch = TRUE;
				break;
			}

			if ( !str_cmp(word,"AMod"))
			{
				int stat = 0;
				for (stat = 0; stat < (MAX_STATS - 1); stat++)
					ch->mod_stat[stat] = fread_number(fp);
				fMatch = TRUE;
				break;
			}

			if ( !str_cmp(word,"Attr"))
			{
				int stat = 0;

				for (stat = 0; stat < (MAX_STATS - 1); stat++)
					ch->perm_stat[stat] = fread_number(fp);
				fMatch = TRUE;
				break;
			}

			if ( !str_cmp(word, "AusBr"))
			{
				ch->auspice	= fread_number(fp);
				ch->breed	= fread_number(fp);
				fMatch = TRUE;
				break;
			}
			break;

		case 'B':
			if (!str_cmp(word,"Back"))
			{
				for(i = 0; i < MAX_BACKGROUND; i++)
				{
					ch->backgrounds[i] = fread_number( fp );
				}
				fMatch = TRUE;
				break;
			}
			KEY( "Bank",        ch->bank_account,       fread_number( fp ) );
			KEY( "Block",	ch->pcdata->block_join,	fread_string( fp ) );
			KEY( "BgCount",	ch->bg_count,		fread_number( fp ) );
			KEY( "BgTime",	ch->bg_timer,		fread_number( fp ) );
			KEY( "Bin",		ch->pcdata->bamfin,	fread_string( fp ) );
			KEY( "Bout",	ch->pcdata->bamfout,	fread_string( fp ) );
			break;

		case 'C':
			KEY( "Cents",        ch->cents,      fread_number( fp ) );

			if ( !str_cmp( word, "Cln" ) ) {
				ch->clan = clan_lookup(fread_string( fp ));
				fMatch = TRUE;
				break;
			}

			if ( !str_cmp( word, "CPow" ) ) {
				ch->clan_powers[0] = fread_number( fp );
				ch->clan_powers[1] = fread_number( fp );
				ch->clan_powers[2] = fread_number( fp );
				fMatch = TRUE;
				break;
			}

			if (!str_cmp(word,"Cnd"))
			{
				ch->condition[0] = fread_number( fp );
				ch->condition[1] = fread_number( fp );
				ch->condition[2] = fread_number( fp );
				ch->condition[3] = fread_number( fp );
				fMatch = TRUE;
				break;
			}
			if ( !str_cmp(word,"Cond") )
			{
				ch->condition[0] = fread_number( fp );
				ch->condition[1] = fread_number( fp );
				ch->condition[2] = fread_number( fp );
				ch->condition[3] = fread_number( fp );
				ch->condition[4] = fread_number( fp );
				ch->condition[5] = fread_number( fp );
				ch->condition[6] = fread_number( fp );
				ch->condition[7] = fread_number( fp );
				ch->condition[8] = fread_number( fp );
				ch->condition[9] = fread_number( fp );
				fMatch = TRUE;
				break;
			}
			if ( !str_cmp(word,"Condit") )
			{
				ch->condition[0] = fread_number( fp );
				ch->condition[1] = fread_number( fp );
				ch->condition[2] = fread_number( fp );
				ch->condition[3] = fread_number( fp );
				ch->condition[4] = fread_number( fp );
				ch->condition[5] = fread_number( fp );
				ch->condition[6] = fread_number( fp );
				ch->condition[7] = fread_number( fp );
				ch->condition[8] = fread_number( fp );
				ch->condition[9] = fread_number( fp );
				ch->condition[10] = fread_number( fp );
				fMatch = TRUE;
				break;
			}
			KEY("Comm",		ch->comm,		fread_flag( fp ) );
			if (!str_cmp(word,"Colours"))
			{
				ch->colours[0] = fread_string( fp );
				ch->colours[1] = fread_string( fp );
				fMatch = TRUE;
				break;
			}

			break;

		case 'D':
			KEYS( "Demnor",	ch->demeanor,		fread_string( fp ) );
			KEY( "Description",	ch->description,	fread_string( fp ) );
			KEY( "Desc",	ch->description,	fread_string( fp ) );
			if (!str_cmp(word,"Disc"))
			{
				for(i = 0; i < 15; i++)
				{
					ch->disc[i] = fread_number( fp );
				}
				fMatch = TRUE;
				break;
			}
			if (!str_cmp(word,"Dsc"))
			{
				for(i = 0; i < MAX_DISC; i++)
				{
					ch->disc[i] = fread_number( fp );
				}
				fMatch = TRUE;
				break;
			}
			KEY( "Dollars",     ch->dollars,            fread_number( fp ) );
			break;

		case 'E':
			KEY( "Econfnum",		ch->email_lock,	fread_number( fp ) );
			if ( !str_cmp( word, "End" ) )
			{
				/* adjust hp mana move up  -- here for speed's sake */
				percent = (current_time - lastlogoff) * 25 / ( 2 * 60 * 60);

				percent = UMIN(percent,100);

				if (percent > 0 && !IS_AFFECTED(ch,AFF_POISON)
						&&  !IS_AFFECTED(ch,AFF_PLAGUE))
				{
					ch->health	+= (ch->health - ch->agghealth) * percent / 100;
				}
				return;
			}
			KEY( "Email",	ch->email_addr,		fread_string( fp ) );
			KEY( "Exp",		ch->exp,		fread_number( fp ) );

			break;

		case 'F':
			KEY( "Form",	ch->form,		fread_flag( fp ) );
			break;

		case 'G':
			KEY( "Gift",	ch->xpgift,		fread_number( fp ) );
			KEY( "Gen",		ch->gen,		fread_number( fp ) );
			KEYS( "Ghld",	ch->ghouled_by,		fread_string( fp ) );
			break;

		case 'H':
			KEY( "Herdt",	ch->herd_timer,		fread_number( fp ) );
			KEY( "Home",	ch->home,		fread_number( fp ) );
			KEY( "Hunted",	ch->hunter_vis,		fread_number( fp ) );
			if ( !str_cmp(word,"HMV"))
			{
				ch->health		= fread_number( fp );
				ch->agghealth	= fread_number( fp );
				fMatch = TRUE;
				break;
			}
			break;

		case 'I':
			KEY( "Id",		ch->id,			fread_number( fp ) );
			KEY( "Ign",		ch->ignore,		fread_string( fp ) );
			KEY( "InvisLevel",	ch->invis_level,	fread_number( fp ) );
			KEY( "Inco",	ch->incog_level,	fread_number( fp ) );
			if (!str_cmp(word,"Infl"))
			{
				for(i = 0; i < MAX_INFL; i++)
				{
					ch->influences[i] = fread_number( fp );
				}
				fMatch = TRUE;
				break;
			}
			KEY( "Invi",	ch->invis_level,	fread_number( fp ) );
			break;

		case 'K':
			if (!str_cmp(word, "KnRite"))
			{
				int rid = fread_number(fp);
				if (ch->n_known_rites < MAX_KNOWN_RITES)
					ch->known_rite_ids[ch->n_known_rites++] = rid;
				fMatch = TRUE;
			}
			break;

		case 'L':
			KEY( "LastV",	ch->pcdata->last_vote,	fread_number( fp ) );
			KEY( "LastO", ch->laston, fread_long(fp)); // Ensure fread_long reads it as time_t
			KEY( "LogO",	lastlogoff,		fread_number( fp ) );
			KEY( "LongDescr",	ch->long_descr,		fread_string( fp ) );
			KEY( "LnD",		ch->long_descr,		fread_string( fp ) );
			if ( !str_cmp( word, "Listen" ) )
			{
				ch->listening = get_room_index( fread_number( fp ) );
				fMatch = TRUE;
				break;
			}

			break;

		case 'M':
			KEYS( "Material", ch->material, fread_string(fp) );
			KEYS( "Marry", ch->married,		fread_string( fp ) );

			if ( !str_cmp( word, "MyRms" ) )
			{
				for(i = 0; i < ch->home; i++)
				{
					ch->rooms[i] = get_room_index( fread_number( fp ) );
					if(str_cmp(ch->rooms[i]->owner, ch->name))
					{
						PURGE_DATA(ch->rooms[i]->owner);
						ch->rooms[i]->owner = str_dup(ch->name);
					}
				}
				fMatch = TRUE;
				break;
			}

			break;

		case 'N':
			KEYS( "Name",	ch->name,		fread_string( fp ) );
			KEYS( "Nature",	ch->nature,		fread_string( fp ) );
			KEY( "Not",		ch->pcdata->last_note,	fread_number( fp ) );
			break;

		case 'O':
			if(!str_cmp( word, "Oocxp" ))
			{
				ch->ooc_xp_time  = fread_number( fp );
				ch->ooc_xp_count = fread_number( fp );
				fMatch = TRUE;
				break;
			}
			KEY( "OocExp",	ch->oocxp,		fread_number( fp ) );
			break;

		case 'P':
			KEY( "Pass",	ch->pcdata->pwd,	fread_string( fp ) );
			KEY( "Pack",	ch->pack,		fread_string( fp ) );
			KEY( "Part",	ch->parts,		fread_flag( fp ) );
			KEY( "Pflgs",	ch->plr_flags,		fread_flag( fp ) );
			KEY( "Playd",	ch->played,		fread_number( fp ) );
			KEY( "Pnts",	ch->pcdata->points,	fread_number( fp ) );
			KEY( "Pos",		ch->position,		fread_number( fp ) );

			if ( !str_cmp( word, "Pow" ) ) {
				int a = 0;
				for(a = 0; a < MAX_POWERS; a++)
				{
					ch->powers[a] = fread_flag( fp );
				}
				fMatch = TRUE;
				break;
			}

			if ( !str_cmp( word, "Prof" ) ) {
				PURGE_DATA(ch->profession);
				ch->profession = str_dup(fread_word( fp ));
				ch->employer = fread_number( fp );
				ch->pospts = fread_number( fp );
				ch->markup = fread_number( fp );
				fMatch = TRUE;
				break;
			}

			if ( !str_cmp( word, "Prompt" ) ) {
				PURGE_DATA(ch->prompt);
				ch->prompt = fread_string(fp);
				fMatch = TRUE;
				break;
			}

			break;

		case 'R':
			if ( !str_cmp( word, "Race" ) ) {
				ch->race = fread_number( fp );
				fMatch = TRUE;
				break;
			}

			KEY( "Reject",	ch->pcdata->ignore_reject,fread_string( fp ) );

			if ( !str_cmp( word, "Room" ) )
			{
				ch->in_room = get_room_index( fread_number( fp ) );
				if ( ch->in_room == NULL )
					ch->in_room = get_room_index( ROOM_VNUM_LIMBO );
				fMatch = TRUE;
				break;
			}

			if(!str_cmp(word,"RGW"))
			{
				ch->RBPG = fread_number( fp );
				ch->max_RBPG = fread_number( fp );
				ch->GHB = fread_number( fp );
				ch->max_GHB = fread_number( fp );
				ch->willpower = fread_number( fp );
				ch->max_willpower = fread_number( fp );
				fMatch = TRUE;
				break;
			}

			KEY( "RPTitle",	ch->pcdata->rpok_string,		fread_string( fp ) );

			break;

		case 'S':
			KEY( "SavingThrow",	ch->saving_throw,	fread_number( fp ) );
			KEY( "Save",	ch->saving_throw,	fread_number( fp ) );
			if (!str_cmp(word,"Saycol"))
			{
				ch->colours[0] = fread_string( fp );
				ch->colours[1] = NULL;
				fMatch = TRUE;
				break;
			}

			KEY( "Scro",	ch->lines,			fread_number( fp ) );
			KEY( "Sex",		ch->sex,			fread_number( fp ) );
			KEY( "ShD",		ch->short_descr,	fread_string( fp ) );
			KEY( "Sire",	ch->sire,			fread_string( fp ) );
			KEY( "SDesc",	ch->switch_desc,	fread_string( fp ) );
			KEY( "Sec",		ch->pcdata->security,	fread_number(fp)); /*OLC*/
			KEYS( "SName",	ch->surname,		fread_string( fp ) );

			KEY("Shape",		ch->shape,		fread_number( fp ) );

			if ( !str_cmp( word, "Skill" ) || !str_cmp(word,"Sk"))
			{
				int sn = 0;
				int value = 0;
				const char *temp;

				value = fread_number( fp );
				temp = fread_word( fp ) ;
				sn = skill_lookup(temp);
				/* sn    = skill_lookup( fread_word( fp ) ); */
				if ( sn < 0 )
				{
					fprintf(stderr,"%s",temp);
					log_string(LOG_BUG, "Fread_char: unknown skill. ");
				}
				else
					ch->learned[sn] = value;
				fMatch = TRUE;
				break;
			}

			if ( !str_cmp( word, "Status" ) ) 
			{
				int a = 0;
				for(a = MAX_BACKGROUND; a < MAX_BG; a++)
				{
					ch->backgrounds[a] = fread_number( fp );
				}
				fMatch = TRUE;
				break;
			}

			if (!str_cmp(word, "STOCK"))
			{
				STOCKS *st, *spot;

				if((spot = stock_lookup(fread_string(fp))) != NULL)
				{
					int c;

					st		= new_stock();
					st->ticker	= spot->ticker;
					st->cost	= fread_number( fp );

					do { c = getc(fp); } while(c == ' ' || c == '\t');
					if(c != EOF && (isdigit(c) || c == '-' || c == '+'))
					{
						ungetc(c, fp);
						st->cost_basis = fread_number( fp );
					}
					else
					{
						if(c != EOF) ungetc(c, fp);
						st->cost_basis = (long)st->cost * spot->cost;
					}

					st->next	= ch->stocks;
					ch->stocks	= st;
				}
				fMatch = TRUE;
				break;
			}

			break;

		case 'T':
			KEY( "TSex",	ch->pcdata->true_sex,   fread_number( fp ) );
			KEY( "Tru",		ch->trust,		fread_number( fp ) );


            KEY( "Titl", ch->pcdata->title,        fread_string( fp ) );
			if(!str_cmp(word, "Totem"))
			{
				for(i = 0;i < 2;i++)
				{
					ch->totem_attitudes[i] = fread_number( fp );
				}
				fMatch = TRUE;
				break;
			}
			if(!str_cmp(word, "Trait"))
			{
				TRAIT_DATA *t;

				t = new_trait();
				t->type = fread_number( fp );

				/*
				 * De-screwing code. Unnecessary in a released version.
				 * To overcome a bug caused by save and load being out
				 * of sync.
				 */
				if(t->type < 0) t->type *= -1;
				if(t->type > 3) t->type = 0;

				t->value = fread_number( fp );
				PURGE_DATA(t->qualifier);
				PURGE_DATA(t->detail);
				t->qualifier = fread_string(fp);
				t->detail = fread_string(fp);
				t->next = ch->traits;
				ch->traits = t;

				fMatch = TRUE;
				break;
			}

			break;

		case 'V':
			if ( !str_cmp( word, "Vnum" ) )
			{
				ch->pIndexData = get_mob_index( fread_number( fp ) );
				fMatch = TRUE;
				break;
			}
			if(!str_cmp(word, "Virt"))
			{
				for(i = 0;i < (MAX_VIRTUES - 1);i++)
				{
					ch->virtues[i] = fread_number( fp );
				}
				fMatch = TRUE;
				break;
			}
			KEY( "Ver",	ch->version,		fread_number( fp ) );
			break;

		case 'W':
			KEY( "Warrants",	ch->warrants,		fread_number( fp ) );
			KEY( "Wizn",	ch->wiznet,		fread_flag( fp ) );
			break;
		}

		if ( !fMatch )
		{
			char logmsg[MSL]={'\0'};
			long file_pos = ftell(fp);
			snprintf(logmsg, sizeof(logmsg),
				"fread_char: no match for key '%s' (player: %s, file offset: %ld bytes)",
				word, ch->name ? ch->name : "UNKNOWN", file_pos);
			log_string(LOG_BUG, logmsg);
			fread_to_eol( fp );
		}
	}
}

/* load a pet from the forgotten reaches */
void fread_pet( CHAR_DATA *ch, FILE *fp )
{
	CHAR_DATA *pet = NULL;
	const char *word;
	time_t current_time = time(NULL);
	int lastlogoff = current_time;
	int percent = 0;
	bool fMatch = FALSE;

	/* first entry had BETTER be the vnum or we barf */
	word = feof(fp) ? "END" : fread_word(fp);
	if (!str_cmp(word,"Vnum"))
	{
		int vnum = 0;

		vnum = fread_number(fp);
		if (get_mob_index(vnum) == NULL)
		{
			char logmsg[MSL]={'\0'};
			snprintf(logmsg, sizeof(logmsg), "Fread_pet: bad vnum %d.", vnum);
			log_string(LOG_BUG, logmsg);
		}
		else
			pet = create_mobile(get_mob_index(vnum));
	}
	else
	{
		log_string(LOG_BUG, "Fread_pet: no vnum in file.");
	}

	for ( ; ; )
	{
		word 	= feof(fp) ? "END" : fread_word(fp);
		fMatch = FALSE;

		switch (UPPER(word[0]))
		{
		case '*':
			fMatch = TRUE;
			fread_to_eol(fp);
			break;

		case '~':
			/* Orphaned tilde from old string formatting - skip it */
			fMatch = TRUE;
			break;

		case 'A':
			KEY( "Act",		pet->act,		fread_flag(fp));
			KEY( "Act2",	pet->act2,		fread_flag(fp));
			KEY( "AfBy",	pet->affected_by,	fread_flag(fp));
			KEY( "AfBy2",	pet->affected_by2,	fread_flag(fp));

			if (!str_cmp(word,"AffD"))
			{
				AFFECT_DATA *paf;
				int sn = 0;

				paf = new_affect();

				sn = skill_lookup(fread_word(fp));
				if (sn < 0)
					log_string(LOG_BUG, "Fread_char: unknown skill.");
				else
					paf->type = sn;

				paf->level	= fread_number(fp);
				paf->duration	= fread_number(fp);
				paf->modifier	= fread_number(fp);
				paf->location	= fread_number(fp);
				paf->bitvector	= fread_number(fp);
				paf->next	= pet->affected;
				pet->affected	= paf;
				fMatch		= TRUE;
				break;
			}

			if (!str_cmp(word,"Affc"))
			{
				AFFECT_DATA *paf;
				int sn = 0;

				paf = new_affect();

				sn = skill_lookup(fread_word(fp));
				if (sn < 0)
					log_string(LOG_BUG, "Fread_char: unknown skill.");
				else
					paf->type = sn;

				paf->where	= fread_number(fp);
				paf->level      = fread_number(fp);
				paf->duration   = fread_number(fp);
				paf->modifier   = fread_number(fp);
				paf->location   = fread_number(fp);
				paf->bitvector  = fread_number(fp);
				paf->next       = pet->affected;
				pet->affected   = paf;
				fMatch          = TRUE;
				break;
			}

			if (!str_cmp(word,"AMod"))
			{
				int stat = 0;

				for (stat = 0; stat < (MAX_STATS - 1); stat++)
					pet->mod_stat[stat] = fread_number(fp);
				fMatch = TRUE;
				break;
			}

			if (!str_cmp(word,"Attr"))
			{
				int stat = 0;

				for (stat = 0; stat < (MAX_STATS - 1); stat++)
					pet->perm_stat[stat] = fread_number(fp);
				fMatch = TRUE;
				break;
			}
			break;

		case 'C':
			KEY( "Cents",      pet->cents,            fread_number( fp ) );
			KEY( "Clan",       pet->clan,       clan_lookup(fread_string(fp)));

			if ( !str_cmp( word, "CPow" ) ) {
				pet->clan_powers[0] = fread_number( fp );
				pet->clan_powers[1] = fread_number( fp );
				pet->clan_powers[2] = fread_number( fp );
				fMatch = TRUE;
				break;
			}

			KEY( "Comm",	pet->comm,		fread_flag(fp));
			break;

		case 'D':
			KEYS( "Desc",	pet->description,	fread_string(fp));
			KEY( "Dollars",     pet->dollars,            fread_number( fp ) );
			break;

		case 'E':
			if (!str_cmp(word,"End"))
			{
				pet->leader = ch;
				pet->master = ch;
				ch->pet = pet;
				/* adjust hp mana move up  -- here for speed's sake */
				percent = (current_time - lastlogoff) * 25 / ( 2 * 60 * 60);

				if (percent > 0 && !IS_AFFECTED(ch,AFF_POISON)
						&&  !IS_AFFECTED(ch,AFF_PLAGUE))
				{
					percent = UMIN(percent,100);
					pet->health	+= (pet->health - pet->agghealth) * percent / 100;
				}
				return;
			}
			KEY( "Exp",	pet->exp,		fread_number(fp));
			break;

		case 'G':
			KEY( "Dollar",	pet->dollars,		fread_number(fp));
			break;

		case 'H':
			if (!str_cmp(word,"HMV"))
			{
				pet->health	= fread_number(fp);
				pet->agghealth	= fread_number(fp);
				fMatch = TRUE;
				break;
			}
			break;

		case 'L':
			KEYS( "LnD",	pet->long_descr,	fread_string(fp));
			KEY( "LogO",	lastlogoff,		fread_number(fp));
			break;

		case 'N':
			KEYS( "Name",	pet->name,		fread_string(fp));
			break;

		case 'P':
			KEY( "Pos",	pet->position,		fread_number(fp));
			break;

		case 'R':
			KEY( "Race",	pet->race, race_lookup(fread_string(fp)));
			break;

		case 'S' :
			KEY( "Save",	pet->saving_throw,	fread_number(fp));
			KEY( "Sex",		pet->sex,		fread_number(fp));
			KEYS( "ShD",	pet->short_descr,	fread_string(fp));
			KEY( "Cent",        pet->cents,             fread_number( fp ) );
			break;

		}

		if ( !fMatch )
		{
			char logmsg[MSL]={'\0'};
			long file_pos = ftell(fp);
			snprintf(logmsg, sizeof(logmsg),
				"Fread_pet: no match for key '%s' (owner: %s, file offset: %ld bytes)",
				word, ch->name ? ch->name : "UNKNOWN", file_pos);
			log_string(LOG_BUG, logmsg);
			fread_to_eol(fp);
		}
	}
}

void fread_obj( CHAR_DATA *ch, FILE *fp )
{
	OBJ_DATA *obj;
	const char *word;
	int iNest = 0;
	bool fMatch = FALSE;
	bool fNest = FALSE;
	bool fVnum = FALSE;
	bool first = TRUE;

	obj = NULL;

	word   = feof( fp ) ? "End" : fread_word( fp );
	if (!str_cmp(word,"Vnum" ))
	{
		int vnum = 0;
		first = FALSE;  /* fp will be in right place */

		vnum = fread_number( fp );
		if (  get_obj_index( vnum )  == NULL )
		{
			char logmsg[MSL]={'\0'};
			snprintf(logmsg, sizeof(logmsg), "Fread_obj: bad vnum %d.", vnum);
			log_string(LOG_BUG, logmsg);
		}
		else
		{
			obj = create_object(get_obj_index(vnum));
		}

	}

	if (obj == NULL)  /* either not found or old style */
	{
		obj = create_object(get_obj_index(OBJ_VNUM_DUMMY));
		PURGE_DATA( obj->name );
		PURGE_DATA( obj->short_descr );
		PURGE_DATA( obj->description );
		PURGE_DATA( obj->full_desc );
		obj->name               = NULL;
		obj->short_descr        = NULL;
		obj->description        = NULL;
		obj->full_desc        = NULL;
	}

	fNest		= FALSE;
	fVnum		= TRUE;
	iNest		= 0;

	for ( ; ; )
	{
		if (first)
			first = FALSE;
		else
			word   = feof( fp ) ? "End" : fread_word( fp );
		fMatch = FALSE;

		switch ( UPPER(word[0]) )
		{
			case '*':
			fMatch = TRUE;
			fread_to_eol( fp );
			break;

			case '~':
			/* Orphaned tilde from old string formatting - skip it */
			fMatch = TRUE;
			break;

			case 'A':
			if (!str_cmp(word,"AffD"))
			{
				AFFECT_DATA *paf;
				int sn = 0;

				paf = new_affect();

				sn = skill_lookup(fread_word(fp));
				if (sn < 0)
					log_string(LOG_BUG, "Fread_obj: unknown skill.");
				else
					paf->type = sn;

				paf->level	= fread_number( fp );
				paf->duration	= fread_number( fp );
				paf->modifier	= fread_number( fp );
				paf->location	= fread_number( fp );
				paf->bitvector	= fread_number( fp );
				paf->next	= obj->affected;
				obj->affected	= paf;
				fMatch		= TRUE;
				break;
			}
			if (!str_cmp(word,"Affc"))
			{
				AFFECT_DATA *paf;
				int sn = 0;

				paf = new_affect();

				sn = skill_lookup(fread_word(fp));
				if (sn < 0)
					log_string(LOG_BUG, "Fread_obj: unknown skill.");
				else
					paf->type = sn;

				paf->where	= fread_number( fp );
				paf->level      = fread_number( fp );
				paf->duration   = fread_number( fp );
				paf->modifier   = fread_number( fp );
				paf->location   = fread_number( fp );
				paf->bitvector  = fread_number( fp );
				paf->next       = obj->affected;
				obj->affected   = paf;
				fMatch          = TRUE;
				break;
			}
			break;

			case 'C':
			KEY( "Cond",	obj->condition,		fread_number( fp ) );
			KEY( "Cost",	obj->cost,		fread_number( fp ) );
			break;

			case 'D':
			KEY( "Description",	obj->description,	fread_string( fp ) );
			KEY( "Desc",	obj->description,	fread_string( fp ) );
			break;

			case 'E':

			if ( !str_cmp( word, "Enchanted"))
			{
				obj->enchanted = TRUE;
				fMatch 	= TRUE;
				break;
			}

			KEY( "ExtF",	obj->extra_flags,	fread_number( fp ) );

			if ( !str_cmp(word,"ExDe"))
			{
				EXTRA_DESCR_DATA *ed;

				ed = new_extra_descr();

				ed->keyword		= fread_string( fp );
				ed->description		= fread_string( fp );
				ed->next		= obj->extra_descr;
				obj->extra_descr	= ed;
				fMatch = TRUE;
			}

			if ( !str_cmp( word, "End" ) )
			{
				if(((obj->name == NULL || obj->name[0] == '\0')
					&& (obj->short_descr == NULL || obj->short_descr[0] == '\0')
					&& obj->description == NULL )
					|| !fNest || (fVnum && obj->pIndexData == NULL))
				{
					log_string(LOG_BUG, "Fread_obj: incomplete object.");
					extract_obj(obj);
					return;
				}
				else
				{
					if ( !fVnum )
					{
						extract_obj( obj );
						obj = create_object( get_obj_index( OBJ_VNUM_DUMMY ) );
					}
					if ( iNest == 0 || rgObjNest[iNest] == NULL )
						obj_to_char( obj, ch );
					else
						obj_to_obj( obj, rgObjNest[iNest-1] );

					return;
				}
			}
			break;

			case 'F':
			KEY( "FDesc",	obj->full_desc,		fread_string( fp ) );
			break;

			case 'I':
			KEY( "ItemType",	obj->item_type,		fread_number( fp ) );
			KEY( "Ityp",	obj->item_type,		fread_number( fp ) );
			break;

			case 'N':
			KEY( "Name",	obj->name,		fread_string( fp ) );

			if ( !str_cmp( word, "Nest" ) )
			{
				iNest = fread_number( fp );
				if ( iNest < 0 || iNest >= MAX_NEST )
				{
					char logmsg[MSL]={'\0'};
					snprintf(logmsg, sizeof(logmsg), "Fread_obj: bad nest %d.", iNest);
					log_string(LOG_BUG, logmsg);
				}
				else
				{
					rgObjNest[iNest] = obj;
					fNest = TRUE;
				}
				fMatch = TRUE;
			}
			break;

			case 'O':
			break;

			case 'Q':
			KEY( "Quality",	obj->quality,		fread_number( fp ) );
			break;

			case 'S':
			KEY( "ShD",		obj->short_descr,	fread_string( fp ) );

			if ( !str_cmp( word, "Spell" ) )
			{
				int iValue = 0;
				int sn = 0;

				iValue = fread_number( fp );
				sn     = skill_lookup( fread_word( fp ) );
				if ( iValue < 0 || iValue > 3 )
				{
					char logmsg[MSL]={'\0'};
					snprintf(logmsg, sizeof(logmsg), "Fread_obj: bad iValue %d.", iValue);
					log_string(LOG_BUG, logmsg);
				}
				else if ( sn < 0 )
				{
					log_string(LOG_BUG, "Fread_obj: unknown skill.");
				}
				else
				{
					obj->value[iValue] = sn;
				}
				fMatch = TRUE;
				break;
			}

			break;

			case 'T':
			KEY( "Timer",	obj->timer,		fread_number( fp ) );
			KEY( "Time",	obj->timer,		fread_number( fp ) );
			break;

			case 'U':
			KEY( "Uses",	obj->uses,		fread_number( fp ) );
			break;

			case 'V':
			if ( !str_cmp( word, "Values" ) || !str_cmp(word,"Vals"))
			{
				obj->value[0]	= fread_number( fp );
				obj->value[1]	= fread_number( fp );
				obj->value[2]	= fread_number( fp );
				obj->value[3]	= fread_number( fp );
				if (obj->item_type == ITEM_WEAPON && obj->value[0] == 0)
					obj->value[0] = obj->pIndexData->value[0];
				fMatch		= TRUE;
				break;
			}

			if ( !str_cmp( word, "Val" ) )
			{
				obj->value[0] 	= fread_number( fp );
				obj->value[1]	= fread_number( fp );
				obj->value[2] 	= fread_number( fp );
				obj->value[3]	= fread_number( fp );
				obj->value[4]	= fread_number( fp );
				obj->value[5]	= fread_number( fp );
				fMatch = TRUE;
				break;
			}

			if ( !str_cmp( word, "Vnum" ) )
			{
				int vnum = 0;

				vnum = fread_number( fp );
				if ( ( obj->pIndexData = get_obj_index( vnum ) ) == NULL )
				{
					char logmsg[MSL]={'\0'};
					snprintf(logmsg, sizeof(logmsg), "Fread_obj: bad vnum %d.", vnum);
					log_string(LOG_BUG, logmsg);
				}
				else
					fVnum = TRUE;
				fMatch = TRUE;
				break;
			}
			break;

			case 'W':
			KEY( "WearFlags",	obj->wear_flags,	fread_number( fp ) );
			KEY( "WeaF",	obj->wear_flags,	fread_number( fp ) );
			KEY( "WearLoc",	obj->wear_loc,		fread_number( fp ) );
			KEY( "Wear",	obj->wear_loc,		fread_number( fp ) );
			KEY( "Weight",	obj->weight,		fread_number( fp ) );
			KEY( "Wt",		obj->weight,		fread_number( fp ) );
			break;

		}

		if ( !fMatch )
		{
			char logmsg[MSL]={'\0'};
			long file_pos = ftell(fp);
			snprintf(logmsg, sizeof(logmsg),
				"Fread_obj: no match for key '%s' (owner: %s, obj: %s, file offset: %ld bytes)",
				word,
				ch->name ? ch->name : "UNKNOWN",
				obj && obj->short_descr ? obj->short_descr : "UNKNOWN",
				file_pos);
			log_string(LOG_BUG, logmsg);
			fread_to_eol( fp );
		}
	}
	if(obj->condition == 0) 
		obj->condition = 25;
	if(obj->timer == 0) 
		obj->timer = -1;

}

