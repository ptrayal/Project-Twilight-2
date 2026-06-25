/***************************************************************************
 * articles.c — Newspaper Article System                                   *
 *                                                                         *
 * Standalone article storage with stable IDs, OLC editor, and formatted   *
 * newspaper display. Replaces the old NOTE_ARTICLE-based system.          *
 *                                                                         *
 * Copyright (C) 2012 - 2026                                               *
 **************************************************************************/

#include <sys/types.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "twilight.h"
#include "tables.h"
#include "olc.h"
#include "recycle.h"

ARTICLE_DATA *article_list = NULL;
int           top_article_id = 1;

/* External XML helpers from research.c */
extern char *xml_escape( const char *str );
extern char *xml_unescape( const char *str );
extern char *read_xml_tag_content( FILE *fp, const char *tag );

/* External helpers — declared in olc.h (included above) and act_info.c */
extern void string_append( CHAR_DATA *ch, char **pString );
void send_wrapped_card_text(const char *text, CHAR_DATA *ch, int width);

/* Forward declarations */
void do_artedit( CHAR_DATA *ch, char *argument );
void artedit_interp( CHAR_DATA *ch, char *argument );
void save_articles( void );
void load_articles( void );
ARTICLE_DATA *find_article_by_id( int id );

/*
 * Find an article by its stable ID.
 */
ARTICLE_DATA *find_article_by_id( int id )
{
    ARTICLE_DATA *art;

    for(art = article_list; art != NULL; art = art->next)
    {
        if(art->id == id)
            return art;
    }

    return NULL;
}

/*
 * show_article_detail — Display all fields of an article.
 * Used by the OLC editor's show command.
 */
static void show_article_detail( CHAR_DATA *ch, ARTICLE_DATA *art )
{
    char datebuf[64];

    strftime(datebuf, sizeof(datebuf), "%b %d, %Y %H:%M", localtime(&art->date_stamp));

    send_to_char( "\tB--------------------------< ARTICLE OLC >--------------------------\tn\n\r", ch );
    send_to_char( Format( "ID:          \tW%d\tn\n\r", art->id ), ch );
    send_to_char( Format( "Headline:    \tW%s\tn\n\r", art->headline ? art->headline : "(none)" ), ch );
    send_to_char( Format( "Byline:      \tW%s\tn\n\r", art->byline ? art->byline : "(none)" ), ch );
    send_to_char( Format( "Category:    \tW%s\tn\n\r", art->category ? art->category : "(none)" ), ch );
    send_to_char( Format( "Date:        \tW%s\tn\n\r", datebuf ), ch );
    send_to_char( Format( "Suppression: \tW%d\tn\n\r", art->suppression ), ch );
    send_to_char( Format( "Approved:    \tW%s\tn\n\r",
        art->approved == 1 ? "Yes" : art->approved == -1 ? "Rejected" : "Pending" ), ch );

    if(art->submitted_by && art->submitted_by[0] != '\0')
        send_to_char( Format( "Submitted By:\tW%s\tn\n\r", art->submitted_by ), ch );

    if(art->body && art->body[0] != '\0')
    {
        send_to_char( "\n\r\tBBody:\tn\n\r", ch );
        send_to_char( art->body, ch );
        send_to_char( "\n\r", ch );
    }

    send_to_char( "\tOType 'done' to exit the editor.\tn\n\r", ch );
}

/*
 * artedit_interp — OLC interpreter for the article editor.
 * Called when a player is in ED_ARTICLE mode.
 */
void artedit_interp( CHAR_DATA *ch, char *argument )
{
    ARTICLE_DATA *art;
    char command[MAX_INPUT_LENGTH];
    char arg[MAX_INPUT_LENGTH];

    art = (ARTICLE_DATA *)ch->desc->pEdit;

    if(!art)
    {
        send_to_char( "Editor error: no article loaded.\n\r", ch );
        edit_done( ch );
        return;
    }

    smash_tilde( argument );
    strncpy( arg, argument, sizeof(arg) - 1 );
    arg[sizeof(arg) - 1] = '\0';
    argument = one_argument( argument, command );

    if( !str_cmp( command, "done" ) )
    {
        save_articles();
        edit_done( ch );
        return;
    }

    if( command[0] == '\0' || !str_cmp( command, "show" ) )
    {
        show_article_detail( ch, art );
        return;
    }

    if( !str_cmp( command, "headline" ) )
    {
        if( argument[0] == '\0' ) { send_to_char( "Syntax: headline <text>\n\r", ch ); return; }
        PURGE_DATA( art->headline );
        art->headline = str_dup( argument );
        send_to_char( Format( "Headline set to: %s\n\r", argument ), ch );
        return;
    }

    if( !str_cmp( command, "byline" ) )
    {
        if( argument[0] == '\0' ) { send_to_char( "Syntax: byline <author name>\n\r", ch ); return; }
        PURGE_DATA( art->byline );
        art->byline = str_dup( argument );
        send_to_char( Format( "Byline set to: %s\n\r", argument ), ch );
        return;
    }

    if( !str_cmp( command, "category" ) )
    {
        if( argument[0] == '\0' ) { send_to_char( "Syntax: category <section name>\n\r", ch ); return; }
        PURGE_DATA( art->category );
        art->category = str_dup( argument );
        send_to_char( Format( "Category set to: %s\n\r", argument ), ch );
        return;
    }

    if( !str_cmp( command, "body" ) )
    {
        string_append( ch, &art->body );
        return;
    }

    if( !str_cmp( command, "approve" ) )
    {
        art->approved = 1;
        send_to_char( "Article approved.\n\r", ch );
        return;
    }

    if( !str_cmp( command, "reject" ) )
    {
        art->approved = -1;
        send_to_char( "Article rejected.\n\r", ch );
        return;
    }

    if( !str_cmp( command, "suppression" ) )
    {
        int val;
        if( argument[0] == '\0' || !is_number( argument ) )
        { send_to_char( "Syntax: suppression <0-10>\n\r", ch ); return; }
        val = atoi( argument );
        if( val < 0 ) val = 0;
        if( val > 10 ) val = 10;
        art->suppression = val;
        send_to_char( Format( "Suppression set to: %d\n\r", val ), ch );
        return;
    }

    if( !str_cmp( command, "delete" ) )
    {
        ARTICLE_DATA *prev = NULL, *cur;

        for(cur = article_list; cur; prev = cur, cur = cur->next)
        {
            if(cur == art)
            {
                if(prev) prev->next = cur->next;
                else article_list = cur->next;
                break;
            }
        }

        send_to_char( "Article deleted.\n\r", ch );
        save_articles();
        ch->desc->pEdit = NULL;
        edit_done( ch );
        free_article( art );
        return;
    }

    interpret( ch, arg );
}

/*
 * do_artedit — Entry point for the article OLC editor.
 *
 * Usage:
 *   artedit list [pending]  — list articles (outside editor)
 *   artedit create          — create new article and enter editor
 *   artedit <id>            — enter editor for existing article
 */
void do_artedit( CHAR_DATA *ch, char *argument )
{
    char arg1[MAX_INPUT_LENGTH];
    ARTICLE_DATA *art;

    if(IS_NPC(ch))
    {
        send_to_char( "NPCs cannot use the article editor.\n\r", ch );
        return;
    }

    argument = one_argument( argument, arg1 );

    if(arg1[0] == '\0')
    {
        send_to_char( "\tB=== Article Editor ===\tn\n\r", ch );
        send_to_char( "  artedit list [pending]  - List all articles\n\r", ch );
        send_to_char( "  artedit create          - Create new article and enter editor\n\r", ch );
        send_to_char( "  artedit \tW<id>\tn            - Edit existing article\n\r", ch );
        return;
    }

    if(!str_cmp( arg1, "list" ))
    {
        bool pending_only = !str_cmp( argument, "pending" );

        if(!article_list)
        {
            send_to_char( "No articles exist.\n\r", ch );
            return;
        }

        send_to_char( Format("\tB%5s \tY|\tn \tB%-40s \tY|\tn \tB%-15s \tY|\tn \tBStatus\tn\n\r", "ID", "Headline", "Category"), ch );
        send_to_char( "\tY------+------------------------------------------+-----------------+-----------\tn\n\r", ch );

        for(art = article_list; art; art = art->next)
        {
            const char *status;

            if(pending_only && art->approved != 0)
                continue;

            if(art->approved == 1)
                status = art->suppression > 0 ? "\tYSuppressed\tn" : "\tGPublished\tn";
            else if(art->approved == -1)
                status = "\tRRejected\tn";
            else
                status = "\tOPending\tn";

            send_to_char( Format( "%5d \tY|\tn %-40s \tY|\tn %-15s \tY|\tn %s\n\r",
                art->id,
                art->headline ? art->headline : "(no headline)",
                art->category ? art->category : "(none)",
                status ), ch );
        }
        return;
    }

    if(!str_cmp( arg1, "create" ))
    {
        art = new_article();
        art->id = top_article_id++;
        art->headline = str_dup( "New Article" );
        art->byline = str_dup( "Staff Reporter" );
        art->category = str_dup( "Local" );
        art->body = str_dup( "" );
        art->date_stamp = time(NULL);
        art->approved = 1;
        art->next = article_list;
        article_list = art;

        ch->desc->pEdit = (void *)art;
        ch->desc->editor = ED_ARTICLE;

        send_to_char( Format( "Created article #%d.\n\r", art->id ), ch );
        show_article_detail( ch, art );
        return;
    }

    if(!is_number( arg1 ))
    {
        send_to_char( "Usage: artedit <id> or artedit create\n\r", ch );
        return;
    }

    art = find_article_by_id( atoi( arg1 ) );
    if(!art)
    {
        send_to_char( "Article not found. Use 'artedit list' to see articles.\n\r", ch );
        return;
    }

    ch->desc->pEdit = (void *)art;
    ch->desc->editor = ED_ARTICLE;
    show_article_detail( ch, art );
}

/*
 * save_articles — Write all articles to XML file.
 */
void save_articles( void )
{
    FILE *fp;
    ARTICLE_DATA *art;

    fp = fopen( ARTICLE_FILE, "w" );
    if(!fp)
    {
        log_string( LOG_ERR, "save_articles: Cannot open articles.xml for writing." );
        return;
    }

    fprintf( fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" );
    fprintf( fp, "<articles next_id=\"%d\">\n", top_article_id );

    for(art = article_list; art; art = art->next)
    {
        fprintf( fp, "  <article>\n" );
        fprintf( fp, "    <id>%d</id>\n", art->id );
        fprintf( fp, "    <headline>%s</headline>\n", xml_escape(art->headline ? art->headline : "") );
        fprintf( fp, "    <byline>%s</byline>\n", xml_escape(art->byline ? art->byline : "") );
        fprintf( fp, "    <category>%s</category>\n", xml_escape(art->category ? art->category : "") );
        fprintf( fp, "    <body>%s</body>\n", xml_escape(art->body ? art->body : "") );
        fprintf( fp, "    <date_stamp>%ld</date_stamp>\n", (long)art->date_stamp );
        fprintf( fp, "    <suppression>%d</suppression>\n", art->suppression );
        fprintf( fp, "    <approved>%d</approved>\n", art->approved );
        fprintf( fp, "    <submitted_by>%s</submitted_by>\n", xml_escape(art->submitted_by ? art->submitted_by : "") );
        fprintf( fp, "  </article>\n" );
    }

    fprintf( fp, "</articles>\n" );
    fclose( fp );

    log_string( LOG_GAME, "save_articles: Articles saved successfully." );
}

/*
 * load_articles — Load articles from XML file.
 * If no file exists, attempt to migrate NOTE_ARTICLE entries from the note system.
 */
void load_articles( void )
{
    FILE *fp;
    ARTICLE_DATA *art = NULL;
    char line[MSL * 4];
    char *content;

    fp = fopen( ARTICLE_FILE, "r" );
    if(!fp)
    {
        log_string( LOG_CONNECT, "load_articles: No articles.xml found. Starting with empty article list." );
        return;
    }

    while(fgets( line, sizeof(line), fp ))
    {
        if(strstr( line, "next_id=" ))
        {
            char *p = strstr( line, "next_id=\"" );
            if(p)
            {
                p += 9;
                top_article_id = atoi( p );
            }
        }

        if(strstr( line, "<article>" ))
        {
            art = new_article();
            continue;
        }

        if(strstr( line, "</article>" ))
        {
            if(art)
            {
                art->next = article_list;
                article_list = art;
            }
            art = NULL;
            continue;
        }

        if(!art)
            continue;

        if((content = strstr( line, "<id>" )) != NULL)
        {
            fseek( fp, -(long)strlen(line), SEEK_CUR );
            content = read_xml_tag_content( fp, "id" );
            if(content) art->id = atoi( content );
        }
        else if((content = strstr( line, "<headline>" )) != NULL)
        {
            fseek( fp, -(long)strlen(line), SEEK_CUR );
            content = read_xml_tag_content( fp, "headline" );
            if(content) art->headline = str_dup( xml_unescape(content) );
        }
        else if((content = strstr( line, "<byline>" )) != NULL)
        {
            fseek( fp, -(long)strlen(line), SEEK_CUR );
            content = read_xml_tag_content( fp, "byline" );
            if(content) art->byline = str_dup( xml_unescape(content) );
        }
        else if((content = strstr( line, "<category>" )) != NULL)
        {
            fseek( fp, -(long)strlen(line), SEEK_CUR );
            content = read_xml_tag_content( fp, "category" );
            if(content) art->category = str_dup( xml_unescape(content) );
        }
        else if((content = strstr( line, "<body>" )) != NULL)
        {
            fseek( fp, -(long)strlen(line), SEEK_CUR );
            content = read_xml_tag_content( fp, "body" );
            if(content)
            {
                char *unesc = xml_unescape(content);
                if(unesc && unesc[0] != '\0')
                    art->body = str_dup( unesc );
            }
        }
        else if((content = strstr( line, "<date_stamp>" )) != NULL)
        {
            fseek( fp, -(long)strlen(line), SEEK_CUR );
            content = read_xml_tag_content( fp, "date_stamp" );
            if(content) art->date_stamp = (time_t)atol( content );
        }
        else if((content = strstr( line, "<suppression>" )) != NULL)
        {
            fseek( fp, -(long)strlen(line), SEEK_CUR );
            content = read_xml_tag_content( fp, "suppression" );
            if(content) art->suppression = atoi( content );
        }
        else if((content = strstr( line, "<approved>" )) != NULL)
        {
            fseek( fp, -(long)strlen(line), SEEK_CUR );
            content = read_xml_tag_content( fp, "approved" );
            if(content) art->approved = atoi( content );
        }
        else if((content = strstr( line, "<submitted_by>" )) != NULL)
        {
            fseek( fp, -(long)strlen(line), SEEK_CUR );
            content = read_xml_tag_content( fp, "submitted_by" );
            if(content && content[0] != '\0') art->submitted_by = str_dup( xml_unescape(content) );
        }
    }

    fclose( fp );
    log_string( LOG_CONNECT, Format("load_articles: Articles loaded (next_id=%d).", top_article_id) );
}

/*
 * show_newspaper_front_page — Formatted newspaper display for players.
 * Builds a newspaper-style front page with articles grouped by category.
 * Called from show_item_card for ITEM_NEWSPAPER and from do_read.
 */
void show_newspaper_front_page( CHAR_DATA *ch, OBJ_DATA *obj )
{
    EXTRA_DESCR_DATA *ed;
    char datebuf[64];
    time_t now = time(NULL);
    int article_num = 1;
    char *categories[MAX_ARTICLES];
    int cat_count = 0;
    int i;
    bool found;

    strftime(datebuf, sizeof(datebuf), "%B %d, %Y", localtime(&now));

    send_to_char("\tY ___________________________________________________________________________\tn\n\r", ch);

    {
        int vlen, pad;
        vlen = (int)strlen(obj->short_descr); pad = 73 - vlen; if(pad < 0) pad = 0;
        send_to_char(Format("\tY|\tn \tB%s\tn%*s \tY|\tn\n\r", obj->short_descr, pad, ""), ch);
    }

    send_to_char("\tY|---------------------------------------------------------------------------|\tn\n\r", ch);

    {
        char vis[128];
        int vlen, pad;
        snprintf(vis, sizeof(vis), "%s", datebuf);
        vlen = (int)strlen(vis); pad = 73 - vlen; if(pad < 0) pad = 0;
        send_to_char(Format("\tY|\tn \tW%s\tn%*s \tY|\tn\n\r", datebuf, pad, ""), ch);
    }

    send_to_char("\tY|---------------------------------------------------------------------------|\tn\n\r", ch);

    /* Collect unique categories from articles in this newspaper */
    for(ed = obj->extra_descr; ed != NULL; ed = ed->next)
    {
        if(strncmp(ed->keyword, "art_cat_", 8) == 0)
        {
            found = FALSE;
            for(i = 0; i < cat_count; i++)
            {
                if(!str_cmp(categories[i], ed->description))
                {
                    found = TRUE;
                    break;
                }
            }
            if(!found && cat_count < MAX_ARTICLES)
            {
                categories[cat_count++] = ed->description;
            }
        }
    }

    /* Display articles grouped by category */
    for(i = 0; i < cat_count; i++)
    {
        char vis[128];
        int vlen, pad;

        snprintf(vis, sizeof(vis), "%s", categories[i]);
        vlen = (int)strlen(vis); pad = 73 - vlen; if(pad < 0) pad = 0;
        send_to_char(Format("\tY|\tn \tB%s\tn%*s \tY|\tn\n\r", categories[i], pad, ""), ch);

        for(ed = obj->extra_descr; ed != NULL; ed = ed->next)
        {
            if(strncmp(ed->keyword, "art_head_", 9) == 0)
            {
                char cat_key[MIL];
                EXTRA_DESCR_DATA *cat_ed;

                snprintf(cat_key, sizeof(cat_key), "art_cat_%s", ed->keyword + 9);

                for(cat_ed = obj->extra_descr; cat_ed; cat_ed = cat_ed->next)
                {
                    if(!str_cmp(cat_ed->keyword, cat_key) && !str_cmp(cat_ed->description, categories[i]))
                    {
                        snprintf(vis, sizeof(vis), "  [%d] %s", article_num, ed->description);
                        vlen = (int)strlen(vis); pad = 73 - vlen; if(pad < 0) pad = 0;
                        send_to_char(Format("\tY|\tn   \tW[%d]\tn %s%*s \tY|\tn\n\r",
                            article_num, ed->description, pad, ""), ch);
                        article_num++;
                        break;
                    }
                }
            }
        }

        if(i < cat_count - 1)
            send_to_char("\tY|---------------------------------------------------------------------------|\tn\n\r", ch);
    }

    if(cat_count == 0)
    {
        send_to_char(Format("\tY|\tn %-73s \tY|\tn\n\r", "No articles in this edition."), ch);
    }

    send_to_char("\tY|___________________________________________________________________________|\tn\n\r", ch);
    send_to_char("  Type '\tWread <number>\tn' to read an article.\n\r", ch);
}

/*
 * show_article_to_player — Display a single article formatted for reading.
 * article_num is the 1-based index in the newspaper.
 */
void show_article_to_player( CHAR_DATA *ch, OBJ_DATA *obj, int article_num )
{
    EXTRA_DESCR_DATA *ed;
    int cur = 1;
    char *headline = NULL, *body = NULL, *byline = NULL, *category = NULL;

    for(ed = obj->extra_descr; ed != NULL; ed = ed->next)
    {
        if(strncmp(ed->keyword, "art_head_", 9) == 0)
        {
            if(cur == article_num)
            {
                char key[MIL];

                headline = ed->description;
                snprintf(key, sizeof(key), "art_body_%s", ed->keyword + 9);
                for(ed = obj->extra_descr; ed; ed = ed->next)
                    if(!str_cmp(ed->keyword, key)) { body = ed->description; break; }

                snprintf(key, sizeof(key), "art_byline_%s", ed->keyword + 9);
                for(ed = obj->extra_descr; ed; ed = ed->next)
                    if(!str_cmp(ed->keyword, key)) { byline = ed->description; break; }

                snprintf(key, sizeof(key), "art_cat_%s", ed->keyword + 9);
                for(ed = obj->extra_descr; ed; ed = ed->next)
                    if(!str_cmp(ed->keyword, key)) { category = ed->description; break; }

                break;
            }
            cur++;
        }
    }

    if(!headline)
    {
        send_to_char("That article number doesn't exist in this newspaper.\n\r", ch);
        return;
    }

    send_to_char("\tY ___________________________________________________________________________\tn\n\r", ch);

    {
        int vlen, pad;
        vlen = (int)strlen(obj->short_descr); pad = 73 - vlen; if(pad < 0) pad = 0;
        send_to_char(Format("\tY|\tn \tB%s\tn%*s \tY|\tn\n\r", obj->short_descr, pad, ""), ch);
    }

    send_to_char("\tY|---------------------------------------------------------------------------|\tn\n\r", ch);

    {
        int vlen, pad;
        vlen = (int)strlen(headline); pad = 73 - vlen; if(pad < 0) pad = 0;
        send_to_char(Format("\tY|\tn \tW%s\tn%*s \tY|\tn\n\r", headline, pad, ""), ch);
    }

    {
        char info[256], vis[256];
        int vlen, pad;

        snprintf(vis, sizeof(vis), "By: %s     Section: %s",
            byline ? byline : "Unknown", category ? category : "General");
        vlen = (int)strlen(vis); pad = 73 - vlen; if(pad < 0) pad = 0;
        snprintf(info, sizeof(info), "\tWBy:\tn %s     \tWSection:\tn %s",
            byline ? byline : "Unknown", category ? category : "General");
        send_to_char(Format("\tY|\tn %s%*s \tY|\tn\n\r", info, pad, ""), ch);
    }

    send_to_char("\tY|---------------------------------------------------------------------------|\tn\n\r", ch);

    if(body && body[0] != '\0')
    {
        send_wrapped_card_text(body, ch, 73);
    }
    else
    {
        send_to_char(Format("\tY|\tn %-73s \tY|\tn\n\r", "(No article body)"), ch);
    }

    send_to_char("\tY|___________________________________________________________________________|\tn\n\r", ch);
}
