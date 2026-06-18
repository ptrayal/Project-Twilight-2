# Newspaper System Documentation

## Overview

The newspaper system allows staff to create in-game newspapers with articles that players can buy from newsstand NPCs and read. Players with Media influence can suppress, promote, and submit articles. The system uses a dedicated article storage format with stable IDs, independent from the note system.

---

## For Players

### Buying a Newspaper
Find a newsstand NPC (marked with the ACT2_NEWS_STAND flag) and type:
```
buy newspaper
```
The cost is set per newspaper by staff.

### Reading a Newspaper
While carrying a newspaper:
- **`read`** — Shows the front page with a table of contents organized by section, with numbered articles.
- **`read 1`** — Reads article #1 in full, with headline, byline, section, and body text.
- **`read 2`** — Reads article #2, etc.
- **`look newspaper`** — Also shows the front page via the item card.

### Using Media Influence
Players with Media influence can interact with the article system:

- **`influence articles`** — List all published articles with their IDs and suppression status. Requires a Manipulation + Expression roll.
- **`influence suppress <article_id>`** — Suppress an article so it stops appearing in new newspaper editions. Higher roll = stronger suppression. Costs 1 Media influence.
- **`influence promote <article_id>`** — Reduce an article's suppression level, making it reappear. Higher roll = more suppression removed. Costs 1 Media influence.
- **`influence submit`** — Submit a player-written article for staff review:
  - `influence submit headline <text>` — Set the article headline
  - `influence submit category <section>` — Set the section (e.g., "Local", "Crime")
  - `influence submit body` — Open the text editor to write the article body
  - `influence submit send` — Submit for editorial review (requires Manipulation + Expression roll)

Submitted articles appear as "Pending" in the staff article editor and must be approved before they can be placed in a newspaper.

---

## For Staff

### Managing Newspapers

Newspapers are managed with the `newspaper` command:

| Command | Description |
|---------|-------------|
| `newspaper list` | List all newspapers with status and price |
| `newspaper create <cost_cents> <name>` | Create a new newspaper |
| `newspaper delete <paper>` | Delete a newspaper (must be off stands) |
| `newspaper rename <paper> <new name>` | Rename a newspaper |
| `newspaper price <paper> <cost_cents>` | Set price in cents |
| `newspaper show <paper>` | Show newspaper layout with article slots |
| `newspaper place <paper> <position> <article_id>` | Place an article by ID into a position |
| `newspaper clear <paper>` | Remove all articles from the newspaper |
| `newspaper release <paper>` | Put the newspaper on stands (distributes to newsstand NPCs) |
| `newspaper stop <paper>` | Remove the newspaper from stands |
| `newspaper save` | Save all newspaper data to disk |

### Managing Articles

Articles are managed with the `artedit` OLC editor:

**Outside the editor:**
| Command | Description |
|---------|-------------|
| `artedit list` | List all articles with ID, headline, category, and status |
| `artedit list pending` | List only pending player submissions |
| `artedit create` | Create a new article and enter the editor |
| `artedit <id>` | Enter the editor for an existing article |

**Inside the editor (after entering with `artedit <id>` or `artedit create`):**
| Command | Description |
|---------|-------------|
| `show` or press return | Display all article fields |
| `headline <text>` | Set the article headline |
| `byline <text>` | Set the author/credit line |
| `category <text>` | Set the section (e.g., "Local News", "Crime", "Business") |
| `body` | Open the string editor for the article body |
| `approve` | Approve a pending player submission |
| `reject` | Reject a pending player submission |
| `suppression <0-10>` | Set suppression level directly (0 = visible) |
| `delete` | Delete the article entirely |
| `done` | Exit editor and auto-save |

### Publishing Workflow

1. Create articles with `artedit create` and fill in headline, byline, category, and body.
2. Create a newspaper with `newspaper create 250 "The Daily Herald"` (price in cents).
3. Place articles into the newspaper: `newspaper place 0 3 1` (newspaper 0, position 3, article ID 1).
4. Release the newspaper: `newspaper release 0`.
5. Newsstand NPCs automatically receive copies for sale.

### Handling Player Submissions

1. Check for pending articles: `artedit list pending`
2. Review a submission: `artedit <id>` then read the body
3. Approve or reject: `approve` or `reject`
4. If approved, place the article in a newspaper as normal

---

## Article Lifecycle

```
Created (artedit create)    Player submitted (influence submit)
        |                              |
        v                              v
    [Approved]                    [Pending]
        |                         /        \
        |                   [Approve]    [Reject]
        |                      |
        v                      v
  Available for placement in newspapers
        |
        v
  Placed in newspaper (newspaper place)
        |
        v
  Published (newspaper release)
        |
   +---------+---------+
   |                   |
[Suppress]         [Promote]
   |                   |
   v                   v
Suppressed        Unsuppressed
(hidden from      (visible in
 new editions)     new editions)
```

---

## Technical Reference

### Data Structures

**ARTICLE_DATA** (`include/twilight.h`):
- `id` — Stable unique ID, auto-incremented, never reused
- `headline` — Article title
- `byline` — Author/credit line
- `category` — Section name (e.g., "Local", "Crime")
- `body` — Article text
- `date_stamp` — Creation timestamp
- `suppression` — 0 = visible, >0 = suppressed
- `approved` — 0 = pending, 1 = approved, -1 = rejected
- `submitted_by` — Player name for player submissions, NULL for staff

**NEWSPAPER** (`include/twilight.h`):
- `name` — Newspaper name
- `on_stands` — Whether currently available for purchase
- `articles[MAX_ARTICLES]` — Array of article IDs (MAX_ARTICLES = 20)
- `cost` — Price in cents

### Files

| File | Purpose |
|------|---------|
| `src/articles.c` | Article system: load/save, OLC editor, display helpers |
| `src/subcmds_influence.c` | Media influence commands (articles, suppress, promote, submit) |
| `src/update.c` | `make_newspapers()` — builds newspaper objects from article data |
| `src/act_info.c` | `do_read()` — player reading interface |
| `src/act_obj.c` | `do_buy()` — newsstand purchase handling |
| `src/olc.c` | OLC editor integration |
| `data/articles.xml` | Article persistence (XML format) |
| `data/paper.txt` | Newspaper persistence (text format) |

### Constants

| Constant | Value | Location | Description |
|----------|-------|----------|-------------|
| `OBJ_VNUM_NEWSPAPER` | 23 | twilight.h | Template object vnum for newspapers |
| `MAX_ARTICLES` | 20 | twilight.h | Maximum articles per newspaper |
| `ARTICLE_FILE` | "../data/articles.xml" | twilight.h | Article data file path |
| `PAPER_FILE` | "../data/paper.txt" | twilight.h | Newspaper data file path |
| `ACT2_NEWS_STAND` | (N) | twilight.h | NPC flag for newsstand vendors |
