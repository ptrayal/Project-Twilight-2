/**************************************************************************
 * Copyright (c) 2018 - 2024 by Rayal.                                    *
 **************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include "twilight.h"

#define MAX_BUFFER_SIZE 128

CHANGE_DATA *change_list = NULL;
CHANGE_DATA *change_last = NULL;
CHANGE_DATA *change_free = NULL;
int changecount = 0;

void load_changes();
void save_changes();
char *strip_ansi(char *str, size_t buf_size);
char *str_strip(char *str);
char *current_date();
void do_addchange(CHAR_DATA *ch, const char *argument);
void do_nchange(CHAR_DATA *ch, char *argument);
bool remove_change(int i);

/*
 * load_changes simply loads the list of changes,
 * should only be called at boot time. Add this
 * call to db.c's boot function.
 */
char *strip_ansi(char *str, size_t buf_size)
{
    static char buf[MSL];  // Assuming MSL is a constant defined elsewhere
    char *ptr = buf;
    size_t remaining_space = buf_size;

    while (*str != '\0' && remaining_space > 1)  // Leave space for null terminator
    {
        if (*str != '^')
        {
            *ptr++ = *str++;
            remaining_space--;
        }
        else
        {
            // Skip '^' character
            str++;

            // Skip the rest of the sequence
            bool in_escape_sequence = false;
            while (*str != '\0')
            {
                if (in_escape_sequence)
                {
                    if (*str >= 'A' && *str <= 'Z')
                        in_escape_sequence = false;
                }
                else if (*str == '[')
                {
                    in_escape_sequence = true;
                }
                str++;
            }
        }
    }
    *ptr = '\0';
    return buf;
}

char *str_strip(char *str)
{
    char* end;

    // Trim leading whitespaces
    while (isspace((unsigned char)*str))
        str++;

    // Trim trailing whitespaces
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;

    // Null-terminate the trimmed string
    *(end + 1) = '\0';

    return str;
}

// Define the function to free allocated memory
void free_change(struct change_data *change)
{
    // Add code to free individual members of 'change'
    free(change->imm);
    free(change->date);
    free(change->text);
    free(change);
}


// Define the function to parse and load changes
void load_changes()
{
    FILE *fp;
    char line[MAX_BUFFER_SIZE];
    char *name;
    char *value;
    int tagLevel = 0;
    struct change_data *change = NULL;

    FILE *debug_file = fopen("debug_log.txt", "w");
    if (!debug_file)
    {
        printf("Failed to open debug_log.txt for writing.\n");
        return;
    }

    if ((fp = fopen(CHANGE_FILE, "r")) == NULL)
    {
        fprintf(debug_file, "Non-fatal error: changes.xml not found!\n");
        fclose(debug_file);
        return;
    }

    while (fgets(line, MAX_BUFFER_SIZE, fp) != NULL)
    {
        char *trimmedLine = str_strip(line);

        fprintf(debug_file, "Read line: %s\n", trimmedLine);
        // log_string(LOG_GAME, Format("DEBUG: Read line: %s\n", trimmedLine));

        if (strcmp(trimmedLine, "<change>") == 0)
        {
            printf("DEBUG: Found <change> tag\n"); // Debug print
            // log_string(LOG_GAME, "DEBUG: Found <change> tag\n");

            change = (struct change_data *)malloc(sizeof(struct change_data));
            if (change)
            {
                change->next = NULL;
                change->prev = NULL;
                change->imm = NULL;
                change->date = NULL;
                change->text = NULL;
            }
            tagLevel = 1;
        }
        else if (strcmp(trimmedLine, "</change>") == 0)
        {
            printf("DEBUG: Found </change> tag\n"); // Debug print
            // log_string(LOG_GAME, "DEBUG: Found </change> tag\n");

            if (change)
            {
                if (change_list) change_list->prev = change;
                change->next = change_list;
                change_list = change;

                if (!change_last) change_last = change;

                changecount++;

                change = NULL;
            }
            tagLevel = 0;
        }
        else if (tagLevel == 1)
        {
            name = strtok(trimmedLine, "<>");
            value = strtok(NULL, "<>");

            if (name && value)
            {
                fprintf(debug_file, "Found tag: %s, value: %s\n", name, value);
                // log_string(LOG_GAME, Format("DEBUG: Found tag: %s, value: %s\n", name, value));

                if (strcmp(name, "imm") == 0)
                    change->imm = str_dup(value);
                else if (strcmp(name, "date") == 0)
                    change->date = str_dup(value);
                else if (strcmp(name, "text") == 0)
                    change->text = str_dup(value);
            }
        }
    }

    fclose(fp);
    fclose(debug_file); // Close the debug log file

    // Free memory allocated for 'change'
    if (change)
    {
        free_change(change);
    }
}


void write_change(FILE *fp, const CHANGE_DATA *change)
{
    fprintf(fp, "  <change>\n");
    fprintf(fp, "    <imm>%s</imm>\n", change->imm);
    fprintf(fp, "    <date>%s</date>\n", change->date);
    fprintf(fp, "    <text>%s</text>\n", change->text);
    fprintf(fp, "  </change>\n");
}

void save_changes()
{
    FILE *fp;
    CHANGE_DATA *change;

    if ((fp = fopen(CHANGE_FILE, "w")) == NULL)
    {
        log_string(LOG_ERR, "Error: Unable to open changes.xml for writing");
        return;
    }

    fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(fp, "<changes>\n");

    log_string(LOG_GAME, "DEBUG: change_list = %p, change_last = %p\n", (void *)change_list, (void *)change_last);

    for (change = change_last; change; change = change->prev)
    {
        if (change->imm && change->date && change->text)
        {
            if (fprintf(fp, "  <change>\n") < 0 ||
                fprintf(fp, "    <imm>%s</imm>\n", change->imm) < 0 ||
                fprintf(fp, "    <date>%s</date>\n", change->date) < 0 ||
                fprintf(fp, "    <text>%s</text>\n", change->text) < 0 ||
                fprintf(fp, "  </change>\n") < 0)
            {
                log_string(LOG_ERR, "Error: Failed to write change to file");
                fclose(fp);
                return;
            }
        }
    }

    fprintf(fp, "</changes>\n");

    if (fflush(fp) != 0)
    {
        log_string(LOG_ERR, "Error: Failed to flush file buffer");
    }

    if (fclose(fp) != 0)
    {
        log_string(LOG_ERR, "Error: Failed to close changes.xml");
    }
}

char *current_date()
{
    char *buf = (char *)malloc(MAX_BUFFER_SIZE);
    if (buf == NULL)
    {
        log_string(LOG_ERR, "Memory allocation failed");
        return NULL;
    }

    time_t nowtime = time(NULL);
    struct tm t;
    if (localtime_r(&nowtime, &t) == NULL)
    {
        log_string(LOG_ERR, "Failed to get local time");
        free(buf);
        return NULL;
    }

    if (strftime(buf, MAX_BUFFER_SIZE, "%d-%b-%Y", &t) == 0)
    {
        log_string(LOG_ERR, "strftime failed");
        free(buf);
        return NULL;
    }

    return buf;
}


/*
 * This is the immortal function, which allows any
 * immortal with access to the function to add new
 * changes to the changes list. The immortals name
 * will also be added to the list, as well as the
 * date the change was added.
 */
void do_addchange(CHAR_DATA *ch, const char *argument)
{
    if (!ch)
    {
        return;  // Check if 'ch' is NULL
    }

    CheckChNPC(ch);

    // Check if the argument is provided and has sufficient length
    if (IS_NULLSTR(argument) || strlen(argument) < 5)
    {
        send_to_char("What did you change?\n\r", ch);
        return;
    }

    // Allocate memory for the change structure
    CHANGE_DATA *change = (CHANGE_DATA *)malloc(sizeof(CHANGE_DATA));
    if (!change)
    {
        send_to_char("Memory allocation error.\n\r", ch);
        return;
    }

    // Initialize change data
    change->imm = str_dup(ch->name);
    change->text = str_dup(argument);
    change->date = current_date();
    change->next = NULL;
    change->prev = NULL;

    // Add the change to the linked list
    if (change_last)
    {
        change_last->next = change;
        change->prev = change_last;
    }
    else
    {
        change_list = change;
    }
    change_last = change;

    changecount++;
    send_to_char("Change added.\n\r", ch);
    save_changes();
}



int num_changes(void)
{
    CHANGE_DATA *change;
    const char *currentDate;
    int today = 0;

    currentDate = current_date();

    if (currentDate == NULL)
    {
        // Handle the error, e.g., print an error message or return an error code.
        // For now, let's just return -1 to indicate an error.
        return -1;
    }

    for (change = change_list; change; change = change->next)
    {
        if (!str_cmp(currentDate, change->date))
        {
            today++;
        }
    }

    return today;
}


char *cline_indent(char *text, int wBegin, int wMax)
{
    static char buf[MSL];
    char *ptr = text;
    char *ptr2 = buf;
    int count = 0;
    int wEnd = 0;
    bool stop = false;

    buf[0] = '\0';

    while (!stop)
    {
        int len = strlen(ptr);

        if (count == 0)
        {
            if (*ptr == '\0')
            {
                wEnd = wMax - wBegin;
            }
            else if (len < (wMax - wBegin))
            {
                wEnd = wMax - wBegin;
            }
            else
            {
                int x = 0;
                while (*(ptr + (wMax - wBegin - x)) != ' ')
                {
                    x++;
                }
                wEnd = wMax - wBegin - (x - 1);
                if (wEnd < 1)
                {
                    wEnd = wMax - wBegin;
                }
            }
        }

        if (count == 0 && *ptr == ' ')
        {
            ptr++;
        }
        else if (++count != wEnd)
        {
            if ((*ptr2++ = *ptr++) == '\0')
            {
                stop = true;
            }
        }
        else if (*ptr == '\0')
        {
            stop = true;
            *ptr2 = '\0';
        }
        else
        {
            count = 0;
            *ptr2++ = '\n';
            *ptr2++ = '\r';
            for (int k = 0; k < wBegin; k++)
            {
                *ptr2++ = ' ';
            }
        }
    }

    return buf;
}

char *format_text(char *text)
{
    static char buf[MSL];
    int buf_length = 0;
    int line_length = 0;
    char *ptr = text;
    bool new_line = true;

    buf[0] = '\0';

    while (*ptr != '\0')
    {
        if (*ptr == '\n')
        {
            buf[buf_length++] = *ptr++;
            line_length = 0;
            new_line = true;
        }
        else if (new_line)
        {
            while (isspace((unsigned char)*ptr))
                ptr++;
            new_line = false;
        }

        buf[buf_length++] = *ptr++;
        line_length++;

        if (line_length >= 70 || *ptr == '\0' || *ptr == '\n')
        {
            if (line_length >= 70)
            {
                buf[buf_length++] = '\n';
            }
            buf[buf_length++] = '\r';
            line_length = 0;
            new_line = true;
        }
    }

    buf[buf_length] = '\0';
    return buf;
}



void do_nchange(CHAR_DATA *ch, char *argument)
{
    int count = 0, page = 0, pcount = 0;
    int today = 0;
    CHANGE_DATA *change;
    char buf[MAX_BUFFER_SIZE];

    char *test = current_date();

    // Calculate the total number of pages
    pcount = (changecount + 9) / 10;

    // Parse the requested page number
    if (isdigit(argument[0]))
    {
        page = atoi(argument);

        if (page < 1)
            page = 1;
        else if (page > pcount)
            page = pcount;

        page--; // Adjust to zero-based index
    }
    else
    {
        page = 0;
    }

    // Calculate the range of changes to display
    int start_change = page * 10;
    int end_change = start_change + 9;

    send_to_char("CHANGES\n", ch);

    // Loop through the changes and display them
    for (change = change_list; change && count <= end_change; change = change->next, count++)
    {
        // Allocate memory and copy strings
        char *changeDate = strdup(change->date);
        char *changeText = strdup(change->text);
        char *changeImm = strdup(change->imm);

        if (!changeDate || !changeText || !changeImm)
        {
            // Handle memory allocation failure
            // Clean up allocated memory and exit
            free(changeDate);
            free(changeText);
            free(changeImm);
            // Additional error handling may be required
            return;
        }

        if (!strcmp(test, changeDate))
            today++;

        if (count >= start_change)
        {
            snprintf(buf, sizeof(buf), "%-2d) [%s] %s [%s]\n\r",
                     count + 1, changeDate, cline_indent(changeText, 3, 79), changeImm);
            send_to_char(buf, ch);
        }

        // Free allocated memory
        free(changeDate);
        free(changeText);
        free(changeImm);
    }

    send_to_char("+=============================================================================+\n\r", ch);

    if (today > 0)
    {
        printf_to_char(ch, "There is a total of %d change%s in the database of which %d %s added today.\n\r",
                       changecount, changecount == 1 ? "" : "s", today,
                       today == 1 ? "was" : "were");
    }
    else
    {
        printf_to_char(ch, "There is a total of %d change%s in the database.\n\r",
                       changecount, changecount == 1 ? "" : "s");
    }

    if (pcount > 1)
    {
        printf_to_char(ch, "To see pages of changes use: changes <1-%d>\n\r", pcount);
    }

    send_to_char("+=============================================================================+\n\r", ch);
}

// Removes a given change, and adds the change to a free_list for recycling
void do_delchange(CHAR_DATA *ch, char *argument)
{
    char arg[MIL] = {'\0'};
    bool found = false;
    int i = 0;

    CheckChNPC(ch);

    one_argument(argument, arg);

    if ((i = atoi(arg)) < 1)
    {
        send_to_char("Which number change did you want to remove?\n\r", ch);
        return;
    }

    found = remove_change(i);

    if (!found)
    {
        send_to_char("No such change.\n\r", ch);
    }
    else
    {
        send_to_char("Change removed.\n\r", ch);
    }

    changecount--;
    save_changes();

    return;
}

/*
 * This function handles the actual removing of the change
 */
// Function to remove a change
bool remove_change(int i)
{
    CHANGE_DATA *change;
    bool found = false; // Use lowercase 'false' and 'true' for boolean values
    int count = 0;

    // Traverse the change list to find the change at index i
    for (change = change_list; change != NULL; change = change->next)
    {
        count++;

        if (count == i)
        {
            found = true;

            // Update pointers to remove the change from the list
            if (change->prev)
            {
                change->prev->next = change->next;
            }
            else
            {
                change_list = change->next;
            }
            if (change->next)
            {
                change->next->prev = change->prev;
            }

            // Free memory for the change's data
            free(change->imm);
            free(change->date);
            free(change->text);

            // Add the change to the free list for recycling
            change->next = change_free;
            change->prev = NULL;
            if (change_free)
            {
                change_free->prev = change;
            }
            change_free = change;

            changecount--;
            save_changes();

            break; // Found and removed the change, exit the loop
        }
    }

    return found;
}
