/* Source File: bgt.c */

/*
 * Copyright 2011 David F. May
 * 
 * This software is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; version
 * 2 of the License only.  See the file Copying at the top of
 * the distribution for more information.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * You can contact me at dmay at cnm dot edu.
 * 
 * Note: The sqlite3.{h,c} source files have been put under the public domain by thier
 * author.  The number.{h,c} are source files from bc-1.0.6 which have been slightly
 * modified.  Taken as a whole, the program is under the GNU General Public License.
 */

/*
 * Version information
 *
 * $Id: bgt.c,v 0.10.1.4 2012/07/08 00:46:17 dfmay Exp dfmay $
 *
 * $Log: bgt.c,v $
 * Revision 0.10.1.4  2012/07/08 00:46:17  dfmay
 * This version provides a validation for the data provided by --date.
 *
 * Revision 0.10.1.3  2012/07/08 00:11:24  dfmay
 * This version contains the --date command.
 *
 * Revision 0.10.1.2  2012/07/07 23:41:59  dfmay
 * This version implements a quiet option and only updates the date on categories that are changed.
 *
 * Revision 0.10.1.1  2012/06/01 21:56:41  dfmay
 * This version contains the --tot command-line switch.
 *
 * Revision 0.10  2011/11/29 19:31:02  dfmay
 * Fixed some bugs.
 *
 * Revision 0.9  2011/10/27 22:36:18  dfmay
 * Added the --nclr option and the --qif option.  Also, changed do_post() so that the dtime for the category
 * gets updated each time that something is added to it.  That way, the user will be able to tell when the
 * category was last updated.
 *
 * Revision 0.8  2011/03/28 21:06:25  dfmay
 * I changed do_csv so that it captures everyting in the tran table in the CSV file.  Also,
 * numerical values do not have quotes around them.
 *
 * Revision 0.7  2011/03/28 20:45:30  dfmay
 * I created the --csv switch to generate CSV output of the transactions in the tran table.
 *
 * Revision 0.6  2011/03/17 00:52:58  dfmay
 * This version has a documentation string and some cosmetic changes.
 *
 * Revision 0.5  2011/03/15 20:53:02  dfmay
 * I implemented --arch.
 *
 * Revision 0.4  2011/03/15 16:28:19  dfmay
 * I enhanced --qry and --exp and added --inc, --net and --scr.
 *
 * Revision 0.3  2011/03/04 23:04:59  dfmay
 * I simplified the code, added the --exp command and fixed the --recalc command so that it wasn't dog slow.
 *
 * Revision 0.2  2011/02/28 23:38:26  dfmay
 * I insured that all data changing commands put data in the act table as necessary.
 *
 * Revision 0.1  2011/02/28 22:55:51  dfmay
 * This is the first functioning version of bgt.  The following are supported:
 *
 * --ls
 * --add
 * --edit
 * --rm
 * --qry
 * --recalc
 *
 * I am going to work with it from here, shake out the bugs and then I will add to it as the need arises.
 *
 *
 */

/*
 * ================================================================================
 * This program will allow the user to create a budget and manage it from the
 * command-line.  This will not replace the programs used to manage check books
 * or to do taxes.  It is designed to fill a very specific nitch of people who
 * have simple needs for a budget.  I anticipate using it in conjunction with
 * kmymoney to do my finances.
 * 
 * The following are the design goals.
 *
 * - It would allow the user to enter categories and transactions on the command-line.
 * NOTE: I decided to nix the overflow category
 * - All categories would be in whole dollar amounts.  The change would go into an
 *   overflow category, if the user entered amounts with change.
 * NOTE: I decided to nix the overflow category
 * - The program will use the bc binary coded engine for number processing.
 * - It would journalize every transaction and allow the user to recalculate at any time.
 * - It would allow the user to edit transactions or remove transactions from being
 *   calculated (although they wouldn't be removed from the journal).
 * - Transactions would be allowed to be entered without posting them, but then when
 *   the user uses the --ls option, they get posted to the categories, reflecting
 *   final amounts.
 * - I would allow the users to archive transactions into an archive table.
 * - I would allow split transactions, where the user entered a final amount, and the
 *   program allowed the user to enter partial amounts up to the full amount.
 * ================================================================================
 */

/*
 * ================================================================================
 * Header Files
 * ================================================================================
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
#include <string.h>
#else
#error "Must have string.h to compile bgt."
#endif
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#error "Must have getopt.h to compile bgt."
#endif
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef HAVE_READLINE_H
#include <readline/readline.h>
#else
# error "Must have readline to compile bgt."
#endif
#include "sqlite3.h"
#include "number.h"

#ifndef HAVE_STRNCPY
#error "You must have strncpy to compile bgt."
#endif

#ifndef HAVE_SNPRINTF
#error "You must have snprintf to compile bgt."
#endif

/*
 * ================================================================================
 * Constants
 * ================================================================================
 */
#define SM_ARY 25
#define MD_ARY 50
#define FIELD_ARB 255
#define DESCR_ARB 1023
#define SIZE_ARB 4095
#define SIZE_TSTMP 20
#define SIZE_AMT 25
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// For the QIF parser
#define QIF_SIZE_DATE 15
#define QIF_SIZE_DESC 255
#define QIF_SIZE_CAT 63
#define QIF_SIZE_TRAN 31
#define QIF_SIZE_PAYEE 63
#define QIF_SIZE_RQDA 63
#define QIF_SIZE_CHECK 15

#ifndef NO
#define NO 0
#endif
#ifndef YES
#define YES 1
#endif

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/*
 * ================================================================================
 * Data Objects
 * ================================================================================
 */
/*
 * ================================================================================
 * Possible iterations
 * *--bgt 'BUDGET_FILE_NAME'
 * *--catt 'CATNAME'
 * *--cat 'CATNUM'
 * *--ls
 * *--add --cat 'CATNUM' --to 'TO_WHOM' --date 'YYYY-MM-DD HH:MM:SS' \
 *        --amt 'HOW_MUCH' --cmt 'COMMENT'
 * --cedit --cat 'CATNUM' --catt 'NEW_CATEGORY_NAME'
 * *--edit --tran 'TRAN_NUM' [--to 'TO_WHOM'] [--amt 'HOW_MUCH] [--cmt 'COMMENT] \
 *        [--cat 'CATNUM']
 * *--rm --tran 'TRAN_NUM'
 * *--qry 'QUERY_STRING'
 *   QUERY_STRING is one of the following:
 *   dt:DATE_SEG - query for anything with a date that matches the date segment.
 *   cat:CAT_SEG - query for anything with a category that matches the cat segment.
 *   cmt:CMT_SEG - query for anything with a comment that matches the cmt segment.
 *   amt:AMT - query for anyything with an amount like AMT.
 *   mr - generate output of everything in machine readable form (colon delimited).
 *   anything else - query everything.
 * *--recalc
 * * --arch
 * --pr
 * ================================================================================
 */

typedef struct _splitObject {
  int cat;
  char amt[SIZE_AMT+1];
} splitObject;

typedef struct _cat_ls {
  int cat;
  char dtime[SIZE_TSTMP+1];
  char amt[SIZE_AMT+1];
  char name[FIELD_ARB+1];
} cat_ls;


/*
 * ================================================================================
 * optObject
 *
 * This item stores the data from options.  The program allocates an optObject
 * device, and that gets used throughout the life of the program and then released
 * before the program exits.
 * ================================================================================
 */
typedef struct _optObject {
  char home_dir[PATH_MAX];
  char db_name[PATH_MAX];
  char inputline[FIELD_ARB+1];
  sqlite3 *db;
  int initialized;
  cat_ls catList[MD_ARY];
  /* options variables here */
  char is_bgt;
  char bgt[PATH_MAX];
  int is_cat;
  int cat;
  int is_catt;
  char catt[FIELD_ARB+1];
  int is_tran;
  int tran;
  int is_ls;
  int is_add;
  int is_to;
  char to [FIELD_ARB+1];
  int is_amt;
  char amt[SIZE_AMT+1];
  int is_cmt;
  char cmt[FIELD_ARB+1];
  int is_cedit;
  int is_crm;
  char crm[FIELD_ARB+1];
  int is_edit;
  int is_rm;
  int is_split;
  splitObject soary[MD_ARY];
  int is_adj;
  int is_src_cat;
  int src_cat;
  int is_dst_cat;
  int dst_cat;
  int is_qry;
  char qry[DESCR_ARB+1];
  int is_recalc;
  int is_arch;
  int is_pr;
  int is_exp;
  int is_inc;
  int is_net;
  int is_beg;
  int beg;
  int is_end;
  int end;
  int is_scr;
  int is_csv;
  int is_date;
  char date[FIELD_ARB+1];
  int is_nclr;
  char nclr[SIZE_ARB+1];
  int is_tot;
  int is_quiet;
  char tot[FIELD_ARB+1];
  int is_qif;
  char qif[SIZE_ARB+1];
} optObject;

optObject *opt;

/*
 * c_item - used for queries with up to SM_ARY columns returned.
 */
typedef struct _c_item {
  struct _c_item *next;
  struct _c_item *prev;
  int num_items;
  char *col[SM_ARY];
  char *item[SM_ARY];
} c_item;

c_item *c_head, *c_tail, *c_current;

/*
 * nclr_dat - needed by proc_nclr_file() and do_nclr().
 */
typedef struct nclr_dat_ {
  struct nclr_dat_ *next;
  int line;
  int cat;
  char amt[SIZE_AMT+1];
} nclr_dat;

static nclr_dat *lastnd = 0, *ndlist = 0;

/*
 * Needed for qif parsing.
 */
typedef struct _split_item {
  char split_cat[QIF_SIZE_CAT+1];      /* split category */
  char amt[QIF_SIZE_TRAN+1];           /* amount of this split */
} splitItem;

typedef struct _qif_item {
  char date[QIF_SIZE_DATE+1];          /* date - YYYYMMDD format */
  char category[QIF_SIZE_CAT+1];       /* category */
  char payee[QIF_SIZE_PAYEE+1];        /* payee */
  char memo[QIF_SIZE_DESC+1];          /* memo, if there is one */
  char check[QIF_SIZE_CHECK+1];        /* check number, if this is a check */
  char ftran[QIF_SIZE_TRAN+1];         /* first amount of the entire transaction */
  char tran[QIF_SIZE_TRAN+1];          /* amount of the entire transaction */
  int isSplit;                         /* is this a split transaction? */
  splitItem *sia[QIF_SIZE_TRAN+1];     /* array of split items */
} qifItem;

  char *usg = 
"Usage: bgt [options]\n"
"\n"
"The bgt program allows a user to create and maintain a budget using only\n"
"the command-line.  The data for the budget is kept in a SQLite database file.\n"
"The following are command-line options supported by bgt.\n"
"\n"
"\n"
"--ls The --ls switch causes bgt to post any outstanding (unposted) transactions and generate\n"
"  a list of category balances.\n"
"\n"
"--add The --add switch adds a transaction.  There are four items necessary when you add a\n"
"  transaction.  The category (either specified using --catt or --cat), the amount (specified\n"
"  using --amt), the to-field (specified using --to) and a comment (specified using --cmt).\n"
"  If you specify --date 'DATE_STR' to add, the date you specify will be entered rather than\n"
"  now.\n"
"\n"
"--edit The --edit switch allows the user to edit a transaction (an entry) by transaction id\n"
"  (using the --tran switch).  Anything specified on the command-line, up to and including all\n"
"  five of --amt, --to, --cat (or --catt), --date, and/or --cmt, are changed on the transaction.\n"
"\n"
"--rm The --rm switch allows the user to remove a transaction.  A copy is made of the transaction\n"
"  in the archive table and it is removed from the tran table.  A recalc is forced after a transaction\n"
"  is removed.\n"
"\n"
"--recalc The --recalc switch forces a recalc of all the transactions in the tran table.  Unlike a posting during a\n"
"--ls, this will act is if nothing is posted and recalculate everything.  Anything that is not\n"
"  posted will get flagged as posted after this is complete.  Finally, a listing of the recalculated\n"
"  balances is produced.\n"
"\n"
"--qry 'QRY_STR'  The --qry switch allows the user to query for transactions by specifying matching\n"
"  criteria.  The QRY_STR will determine the criteria.  The following query types are supported:\n"
"\n"
"        dt:STR The dt: query takes all or part of a date and time stamp, allowing the user\n"
"        to query on parts of dates.\n"
"        \n"
"        st:STR The dt: query allows the user to query on the status of the transaction.\n"
"\n"
"        to:STR The to: query allows the user to query on the to field.\n"
"\n"
"        cat:STR The cat: query allows the user to query on the category field.\n"
"\n"
"        cmt:STR The cmt: query allows the user to query on the comment field.\n"
"\n"
"        amt:STR The amt: query allows the user to query on the amount field.\n"
"\n"
"        mr The mr query generates a machine-readable (colon-delimited) list of all transactions.\n"
"\n"
"        Anything else  Any other query string generates a list of transactions in a human readable form.\n"
"\n"
"--exp The --exp switch generates a report of all expenses by categories.  The --exp switch causes budget\n"
"  to look at all the transactions with negative amounts (except those that have a to field like 'adjust').\n"
"  It then adds these up and generates a report.  The user can use --beg to specify a beginning transaction\n"
"  (otherwise the first one is assumed) and/or --end to specify an ending transaction (otherwise the last one\n"
"  is assumed).\n"
"\n"
"--inc The --inc switch is like the --exp switch except that it reports on income rather than expenses.\n"
"\n"
"--net The --net switch is similar to --exp and --inc, except that it includes all the transactions less\n"
"  those that have a to field like 'adjust'.\n"
"\n"
"--scr The --scr switch generates the commands necessary to recreate the transactions in the bgt.db tran\n"
"  table.  This includes the value of the --bgt switch, if any.\n"
"\n"
"--arch The --arch switch archieves all the transactions in the tran table.  Then, it recreates enough\n"
"  transactions to reset the balances of the categories to what they were before the archiving occurred.  This\n"
"  allows the user to keep their transaction table cleaned out of old stuff.  It is not necessary to do an\n"
"  archive, but it is available should someone want to use it.\n"
"\n"
"--csv The --csv switch generates CSV output of the data in your budget tran table.\n"
"\n"
"--nclr <filename>  The --nclr switch allows you to add items that have not cleared to the output of bgt --ls.\n"
"  Basically, you need to create a text file in the following format:\n"
"# Comment - use a # or a // at the beginning of the line to specify a comment.\n"
"Category:amt\n"
"Category:amt\n"
"...\n"
"\n"
"  bgt will take what would have been the output, the posted value, and incorporate the changes listed in the no clear\n"
"  file.  This allows you to have the actual balances reflected by bgt be what has actually cleared the bank, but\n"
"  also allows you to get the actual amount that is available by subtracting out what has not cleared but is paid.\n"
"\n"
"--qif <filename>  The --qif switch will read a qif file and parse it.  The categories in the file must\n"
"  correspond to categories in the budget database, or an warning is issued, though parsing continues.\n"
"  Once parsing is completed, you should be able to take the --add statements thus generated and add them to your\n"
"  budget database.\n"
"\n"
"\n"
"--bgt 'BGT_PATH' The --bgt switch allows the user to specify a directory for the bgt.db file.  If one is not specified,\n"
"  ~/.bgt is used by default.\n"
"\n"
"--catt 'CATNAME' The --catt switch allows the user to specify a category by name.  Category names can be arbitrarily\n"
"  long (up to 256 characters in length) and can have spaces.\n"
"\n"
"--cat CATNUM The --cat switch allows the user to specify a category by number.  When categories are created,\n"
"  they are assigned a number.  You can use this number to refer to that category.\n"
"\n"
"--amt 'AMT' The --amt switch is used to specify an amount.  The amount can be any valid positive or\n"
"  negative number.  The scale used to track and add or subtract numbers is 2, so any decimal places\n"
"  beyond that are discarded.\n"
"\n"
"--to 'TO_WHOM' The --to switch allows the user to specify the recipient of a transaction.\n"
"\n"
"--cmt 'COMMENT' The --cmt switch allows the user to specify a comment for a transaction.\n"
"\n"
"--tran 'TRANNUM' The --tran switch allows the user to enter a transaction number, for those action switches\n"
"  that require a transaction number, like --edit or --rm.\n"
"\n"
"--beg 'BEG_TRAN'\n"
"--end 'END_TRAN' The --beg and --end switches allow the user to specify a range of transactions.  If you specify\n"
"  a valid --beg, then the operation being requested (like --exp, --inc, or --net) will go from that beginning to the last\n"
"  transaction.  If you specify --end, the operation will go from the beginning of the transactions to that one, inclusive.\n"
"  If you specify both, the operation will only include the beginning, the end, and all the ones in between.\n"
"\n"
"--help This generates a help screen.\n";

int is_query = 0;

/*
 * ================================================================================
 * Function Prototypes
 * ================================================================================
 */
inline int init_cb_data (void);
inline void del_cb_data (void);
static int cbitem (void *NotUsed, int argc, char **argv, char **azColName);
static void remove_chr (char *str, int chr);
static int do_initialize (void);
static int get_cat_num_from_name (char *name);
static int get_cat_name_from_num (int num, char *name);
static int get_next_cat_num (void);
static int get_next_tran_num (void);
static inline int verify_number (const char *num);
static int proc_nclr_file (void);
static qifItem *parseQIFItem (char *rqda[], const char *file, int lnctr);

static void usage (void);
static int do_add (void);
static int do_new_cat (void);
static int do_post (int recalc);
static int do_nclr (void);
static int do_ls (void);
static int do_recalc ();
static int do_qry(void);
static int do_edit (void);
static int do_rm (void);
static int do_exp (void);
static int do_inc (void);
static int do_net (void);
static int do_scr (void);
static int do_arch (void);
static int do_csv (void);
static int do_qif (void);

/*
 * ================================================================================
 * Functions
 * ================================================================================
 */

static void usage (void)
{
  printf ("%s\n", usg);
}

/*
 * fexists
 * 
 * Returns TRUE if a file exists; FALSE otherwise.
 */
inline int fexists (const char *path)
{
  struct stat buf;
  if (stat (path, &buf) == 0)
    return TRUE;
  else
    return FALSE;
}

/*
 * inputline
 *
 * Grabs a line from the user using readline.
 */
inline int inputline (char *prompt)
{
  char *p;
  p = readline (prompt);
  if (p == 0) {
    printf ("***Error in input_line(), line %d: readline() returned NULL\n", __LINE__);
    return -1;
  }
  strncpy (opt->inputline, p, FIELD_ARB);
  free (p);
  return 0;
}

/*
 * dupchar
 *
 * This function copies the data from d1 to d2, duplicating every instance of val.
 */
inline void dupchar (char *d1, char *d2, int val)
{
  char *cp = d1;
  char *cp2 = d2;
  while (*cp != '\0') {
    if (*cp == (char)val) {
      *cp2 = *cp;
      cp2++;
    }
    *cp2 = *cp;
    cp++; cp2++;
  }
  return;
}

/*
 * remove_chr
 *
 * This function removes a character given by chr, replacing it with a space, from a string given by str.
 */
static void remove_chr (char *str, int chr)
{
  char *cp;
  cp = strchr (str, chr);
  if (cp == 0)
    return;
  *cp = ' ';
  cp = strchr (cp, chr);
  while (cp) {
    *cp = ' ';
    cp = strchr (cp, chr);
  }
  return;
}

/*
 * init_cb_data
 *
 * This initializes the linked list that will contain all the results of queries.
 */
inline int init_cb_data (void)
{
  c_head = malloc (sizeof (c_item));
  if (c_head == 0) {
    printf ("\n***Error: Fatal memory error, line %d, allocating c_item head (%d bytes)\n", __LINE__, sizeof(c_item));
    return -1;
  }
  memset (c_head, 0, sizeof (c_item));
  c_tail = malloc (sizeof (c_item));
  if (c_tail == 0) {
    printf ("\n***Error: Fatal memory error, line %d, allocating c_1item tail (%d bytes)\n", __LINE__, sizeof(c_item));
    return -1;
  }
  memset (c_tail, 0, sizeof (c_item));
  c_head->next = c_tail;
  c_tail->prev = c_head;
  c_current = c_head;
  is_query = TRUE;
  return 0;
}

/*
 * del_cb_data
 *
 * This function removes the query linked list and cleans up all used allocated memory.
 */
inline void del_cb_data (void)
{
  int i;
  c_item *c;
  if (c_head == 0 || c_tail == 0) {
    return;
  }
  while (c_head->next != c_tail) {
    c = c_head->next;
    c->next->prev = c->prev;
    c->prev->next = c->next;
    for (i = 0; i < SM_ARY; i++) {
      if (c->col[i] != 0) {
        free (c->col[i]);
        c->col[i] = 0;
      }
      if (c->item[i] != 0) {
        free (c->item[i]);
        c->item[i] = 0;
      }
    }
    free (c);
    c_head->num_items--;
  }
  free (c_tail);
  c_tail = 0;
  free (c_head);
  c_head = 0;
  is_query = FALSE;
  return;
}

/*
 * cbitem
 * Call back function for queries.  This function populates a c_item linked list for a
 * query returning n columns.  The number of columns for each line is in c->num_items,
 * so the caller can verify it is right.
 */
static int cbitem (void *NotUsed, int argc, char **argv, char **azColName)
{
  int len, i;
  c_item *c;

  if (argc < 1 || argc > SM_ARY) {
    printf ("***Error in cbitem(), line %d: argc = %d is invalid.\n", __LINE__, argc);
    return -1;
  }
  c = malloc (sizeof (c_item));
  if (c == 0) {
    printf ("\n***Error, in cbitem(), line %d: Fatal memory error allocating %d bytes for a c_item\n",
        __LINE__, sizeof(c_item));
    return -1;
  }
  memset (c, 0, sizeof(c_item));
  if (! is_query)
    init_cb_data();
  if (azColName[0] == 0) {
    // ignore this call
    free (c);
    return 0;
  }
  for (i = 0; i < argc; i++) {
    len = strlen (azColName[i]);
    c->col[i] = malloc (len+1);
    if (c->col[i] == 0) {
      printf ("***Error in cbitem(), line %d: Fatal memory error allocating %d bytes for arg %d column name, azColName='%s'\n",
          __LINE__, len+1, i, azColName[i]);
      free(c);
      return -1;
    }
    strcpy (c->col[i], azColName[i]);
    if (argv[i] == 0)
      c->item[i] = 0;
    else {
      len = strlen (argv[i]);
      c->item[i] = malloc (len+1);
      if (c->item[i] == 0) {
        printf ("***Error in cbitem(), line %d: Fatal memory error allocating %d bytes for arg %d argument, azColName='%s'\n",
            __LINE__, len+1, i, azColName[i]);
        free(c);
        return -1;
      }
      memset (c->item[i], 0, len+1);
      strcpy (c->item[i], argv[i]);
    }
  }
  c->num_items = argc;
  /* now, link it */
  c_tail->prev->next = c;
  c->prev = c_tail->prev;
  c_tail->prev = c;
  c->next = c_tail;
  c_head->num_items++;
  return 0;
}

static char *SQLInitializeString =
"/* Created by the bgt program. */\n"
"/* Do not change this schema manually. */\n"
"/* Vile things will happen to your data if you do. */\n"
"BEGIN TRANSACTION;\n"
"/* cat table */\n"
"/* Contains information about categories, including balances. */\n"
"CREATE TABLE cat (\n"
"  num INTEGER PRIMARY KEY AUTOINCREMENT,\n"
"  dtime CHAR (20),\n"
"  name VARCHAR (256),\n"
"  amt VARCHAR (25),\n"
"  comment VARCHAR (256)\n"
");\n"
"-- INSERT INTO cat VALUES (1, datetime('now','localtime'), 'overflow', '0.00', 'The overflow category used to track cent amounts.');\n"
"CREATE INDEX c_dt ON cat(dtime);\n"
"CREATE INDEX c_cnm ON cat(name);\n"
"\n"
"/* tran table */\n"
"/* Contains information about transactions. */\n"
"CREATE TABLE tran (\n"
"  num INTEGER PRIMARY KEY AUTOINCREMENT,\n"
"  cat_num INTEGER,\n"
"  dtime CHAR (20),\n"
"  amt CHAR (25),\n"
"  status CHAR (4) CHECK (status IN ('ARCH','EDIT','FARC','NPST','PSTD','RMVD')),\n"
"  to_who VARCHAR (256),\n"
"  comment VARCHAR (256)\n"
");\n"
"\n"
"CREATE INDEX t_dt ON tran(dtime);\n"
"\n"
"/* arch table */\n"
"/* Contains archived transactions. */\n"
"CREATE TABLE arch (\n"
"  num INTEGER,\n"
"  cat_num INTEGER,\n"
"  dtime CHAR (20),\n"
"  amt CHAR (25),\n"
"  status CHAR (4) CHECK (status IN ('ARCH','EDIT','FARC','NPST','PSTD','RMVD')),\n"
"  to_who VARCHAR (256),\n"
"  comment VARCHAR (256)\n"
");\n"
"CREATE INDEX a_num ON arch(num);\n"
"CREATE INDEX a_dt ON arch(dtime);\n"
"\n"
"/* act table */\n"
"/* Contains activity information, like a journal. */\n"
"CREATE TABLE act (\n"
"  type CHAR(4) CHECK (type IN ('ARC','CAT','EDT','RMV','TRN')),\n"
"  cat_num INTEGER,\n"
"  tran_num INTEGER,\n"
"  dtime CHAR(20),\n"
"  amt CHAR(25),\n"
"  to_who VARCHAR (256),\n"
"  comment VARCHAR (256)\n"
");\n"
"-- INSERT INTO act VALUES ('CAT',1,NULL,datetime('now','localtime'),NULL,'overflow','Added cat 1, the overflow category.');\n"
"CREATE INDEX ct_dt ON act(dtime);\n"
"COMMIT;\n";

/*
 * do_initialize
 *
 * This function initializes the database.
 */
static int do_initialize (void)
{
  int ret;
  char *errmsg = 0;

  if (opt->initialized) {
    printf ("\n***Warning: do_initialized called twice\n");
    return 0;
  }
  /* Initialize the database with default startup values. */
  printf ("\nRunning do_initialize() for %s\n\n", opt->db_name);
  if (! fexists (opt->db_name)) {
    ret = sqlite3_open (opt->db_name, &opt->db);
    if (ret) {
      printf ("\n***Error in do_initialize(), line %d: Can't open %s: %s\n", __LINE__, opt->db_name, sqlite3_errmsg(opt->db));
      return -1;
    }
    ret = sqlite3_exec (opt->db, SQLInitializeString, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_initialize(), line %d: SQLite Error initializing bgt.db: %s\n", __LINE__, errmsg);
      sqlite3_free (errmsg);
      ret = -1;
      return ret;
    }
  }
  opt->initialized = TRUE;
  return 0;
}

/*
 * get_cat_num_from_name
 *
 * This function gets the category number when the caller provides the category name.
 */
static int get_cat_num_from_name (char *name)
{
  char *errmsg = 0;
  c_item *c;
  int ret;
  char tmp[SIZE_ARB+1];

  if (is_query) {
    printf ("\n***Error: in get_cat_num_from_name (), line %d: Called when is_query is TRUE\n", __LINE__);
    return -1;
  }
  snprintf (tmp, SIZE_ARB, "SELECT num FROM cat WHERE name = '%s';", name);
  ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in get_cat_num_from_name(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    del_cb_data();
    return -1;
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    del_cb_data();
    return -1;
  }
  if (c_head->num_items != 1) {
    printf ("\n***Error: in get_cat_num_from_name(), line %d, c_head->num_items = %d for query '%s'\n",  __LINE__, c_head->num_items, tmp);
    return -1;
  }
  c = c_head->next;
  ret = atoi (c->item[0]);
  del_cb_data();
  return ret;
}

/*
 * get_cat_num_from_name
 *
 * This function gets the category number when the caller provides the category name.
 */
static int get_cat_name_from_num (int num, char *name)
{
  char *errmsg = 0;
  int ret;
  c_item *c;
  char tmp[SIZE_ARB+1];

  if (is_query) {
    printf ("\n***Error: in get_cat_num_from_name (), line %d: Called when is_query is TRUE\n", __LINE__);
    return -1;
  }
  snprintf (tmp, SIZE_ARB, "SELECT name FROM cat WHERE num = '%d';", num);
  ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in get_cat_num_from_name(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    del_cb_data();
    return -1;
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    del_cb_data();
    return -1;
  }
  if (c_head->num_items != 1) {
    printf ("\n***Error: in get_cat_num_from_name(), line %d, c_head->num_items = %d for query '%s'\n",  __LINE__, c_head->num_items, tmp);
    return -1;
  }
  c = c_head->next;
  strcpy (name, c->item[0]);
  del_cb_data();
  return 0;
}

/*
 * Get_next_cat_num
 *
 * This function gets the next category number.  It grabs max(num) from the table, and adds one to it.
 */
static int get_next_cat_num (void)
{
  char *errmsg = 0;
  c_item *c;
  int ret;
  char tmp[SIZE_ARB+1];

  if (is_query) {
    printf ("\n***Error: in get_next_cat_num (), line %d: Called when is_query is TRUE\n", __LINE__);
    return -1;
  }
  snprintf (tmp, SIZE_ARB, "SELECT MAX(num) FROM cat;");
  ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in get_next_cat_num(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    del_cb_data();
    return -1;
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    del_cb_data();
    return 1;
  }
  if (c_head->num_items != 1) {
    printf ("\n***Error: in get_next_cat_num(), line %d, c_head->num_items = %d for query '%s'\n",  __LINE__, c_head->num_items, tmp);
    return -1;
  }
  c = c_head->next;
  if (c->item[0] == 0)
    ret = 0;
  else
    ret = atoi (c->item[0]);
  ret++;
  del_cb_data();
  return ret;
}

/*
 * get_next_tran_num
 *
 * This function gets the next transaction number.  It grabs max(tran_num) from the table and adds one to it.
 */
static int get_next_tran_num (void)
{
  char *errmsg = 0;
  c_item *c;
  int ret;
  char tmp[SIZE_ARB+1];

  if (is_query) {
    printf ("\n***Error: in get_next_tran_num (), line %d: Called when is_query is TRUE\n", __LINE__);
    return -1;
  }
  snprintf (tmp, SIZE_ARB, "SELECT MAX(num) FROM tran;");
  ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in get_next_tran_num(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    del_cb_data();
    return -1;
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    del_cb_data();
    return 1;
  }
  if (c_head->num_items != 1) {
    printf ("\n***Error: in get_next_tran_num(), line %d, c_head->num_items = %d for query '%s'\n",  __LINE__, c_head->num_items, tmp);
    return -1;
  }
  c = c_head->next;
  if (c->item[0] == 0)
    ret = 0;
  else
    ret = atoi (c->item[0]);
  ret++;
  del_cb_data();
  return ret;
}

/*
 * verify_number
 *
 * This function verifies a number.
 */
static inline int verify_number (const char *num)
{
  int i = 0;
  int saw_period = FALSE;
  /* first char must be -, + or a digit */
  if (num[0] != '-' && num[0] != '+' && ! isdigit (num[0])) {
    printf ("\n***Error in verify_number(), line %d: invalid number '%s'\n", __LINE__, num);
    return FALSE;
  }
  while (num[i] != '\0') {
    /* must be digits and 1 or 0 period from now on */
    i++;
    if (num[i] == '.') {
      if (saw_period == TRUE) {
        printf ("\n***Error in verify_number(), line %d: number has two '.' in it: '%s'\n", __LINE__, num);
        return FALSE;
      }
      saw_period = TRUE;
      continue;
    }
    if (isdigit (num[i]))
      continue;
    if (num[i] == '\0')
      continue;
    /* shouldn't get here */
    printf ("\n***Error in verify_number(), line %d: number has invalid char %c: %s\n", __LINE__, num[i], num);
    return FALSE;
  }
  return TRUE;
}

static qifItem *parseQIFItem (char *rqda[], const char *file, int lnctr)
{
  qifItem *qi;
  int i, j, mon, day, year;
  char *cp;
  char buf[10];
  int splitNum = 0;

  if (rqda[0][0] == '\0')
    return 0;
  qi = malloc (sizeof (qifItem));
  if (qi == 0) {
    printf ("\n\n***Error: fatal memory error allocating a qifItem\n");
    return 0;
  }
  memset (qi, 0, sizeof (qifItem));
  /*check_pointer (qi);*/
  for (i = 0; i < QIF_SIZE_RQDA && rqda[i][0] != '\0'; i++) {
    switch (rqda[i][0]) {
      case 'D':
        /* parse the date */
        // Stupid qif date formats.  <sigh>
        // Date can be DD.MM.YYYY or M[M]/D[D]/[YY]YY or M[M]/D[D]'[ |Y]Y
        // DD.MM.YYYY - I needed this because I couldn't get kmymoney to generate a better date format.
        // Your mileage may vary.
        cp = strchr (&rqda[i][1], '.');
        if (cp != 0) {
          cp = &rqda[i][1];
          for (j = 0; *cp != '.' && j < 9; j++) {
            buf[j] = *cp;
            cp++;
          }
          buf[j] = '\0';
          day = atoi (buf);
            if (day < 1 || day > 31) {
            goto BrokenDate;
          }
          if (*cp != '.')
            goto BrokenDate;
          cp++;
          for (j = 0; *cp != '.' && j < 9; j++) {
            buf[j] = *cp;
            cp++;
          }
          buf[j] = '\0';
          mon = atoi (buf);
          if (mon < 1 || mon > 12)
            goto BrokenDate;
            if (*cp != '.')
            goto BrokenDate;
          cp++;
          for (j = 0; *cp != '\0' && j < 9; j++) {
            buf[j] = *cp;
            cp++;
          }
        buf[j] = '\0';
          year = atoi (buf);
          if (year < 1900 || year > 2025)
            goto BrokenDate;
          sprintf (qi->date, "%04d%02d%02d", year, mon, day);
          goto GoodDate;
        }
        cp = strchr (rqda[i], '\'');
        if (cp == 0) {
          /* old date format M[M]/D[D]/[YY]YY*/
          cp = &rqda[i][1];
          for (j = 0; *cp != '/' && j < 9; j++) {
            buf[j] = *cp;
            cp++;
          }
          buf[j] = '\0';
          mon = atoi (buf);
          if (mon < 1 || mon > 12) {
            goto BrokenDate;
          }
          if (*cp != '/')
            goto BrokenDate;
          cp++;
          for (j = 0; *cp != '/' && j < 9; j++) {
            buf[j] = *cp;
            cp++;
          }
          buf[j] = '\0';
          day = atoi (buf);
          if (day < 1 || day > 31)
            goto BrokenDate;
          if (*cp != '/')
            goto BrokenDate;
          cp++;
          for (j = 0; *cp != '\0' && j < 9; j++) {
            buf[j] = *cp;
            cp++;
          }
          buf[j] = '\0';
          year = atoi (buf);
          if (year >= 50 && year < 100)
            year += 1900;
          else if (year > 1 && year < 50)
            year += 2000;
          if (year < 1900 || year > 2025)
            goto BrokenDate;
          sprintf (qi->date, "%04d%02d%02d", year, mon, day);
          goto GoodDate;
        }
        else {
          /* newer date format M[M]/D[D]'[ |Y]Y */
          buf[0] = '2';
          buf[1] = '0';
          if (*(cp+1) == ' ')
            buf[2] = '0';
          else
            buf[2] = *(cp+1);
          buf[3] = *(cp+2);
          buf[4] = '\0';
          year = atoi(buf);
          if (year < 1900 || year > 2025)
            goto BrokenDate;
          cp = &rqda[i][1];
          for (j = 0; *cp != '/' && j < 9; j++) {
            buf[j] = *cp;
            cp++;
          }
          buf[j] = '\0';
          mon = atoi (buf);
          if (mon < 1 || mon > 12) {
            goto BrokenDate;
          }
          if (*cp != '/')
            goto BrokenDate;
          cp++;
          for (j = 0; *cp != '/' && j < 9; j++) {
            buf[j] = *cp;
            cp++;
          }
          buf[j] = '\0';
          day = atoi (buf);
          if (day < 1 || day > 31)
            goto BrokenDate;
          sprintf (qi->date, "%04d%02d%02d", year, mon, day);
          goto GoodDate;
        }


BrokenDate:
        printf ("***Warning: parse error on date \"%s\" ", &rqda[i][1]);
        printf (": ignoring qif item\n");
        free (qi);
        return 0;

GoodDate:
        break;
      case 'U':;

        /* amt of first transaction - ignore it */
        snprintf (qi->ftran, QIF_SIZE_TRAN, "%s", &rqda[i][1]);
        break;
      case 'T':
        /* get the amount */
        // snprintf (qi->tran, QIF_SIZE_TRAN, "%s", &rqda[i][1]);
        cp = &rqda[i][1];
        for (j = 0; j < QIF_SIZE_TRAN && *cp != '\0'; j++, cp++) {
          if (*cp == ',') {
            cp++;
          }
          qi->tran[j] = *cp;
        }
        qi->tran[j] = '\0';
        break;
      case 'C':
        /* cleared status - ignore it */
        break;
      case 'P':
        /* get the payee */
        snprintf (qi->payee, QIF_SIZE_PAYEE, "%s", &rqda[i][1]);
        break;
      case 'M':
        /* get the memo */
        snprintf (qi->memo, QIF_SIZE_DESC, "%s", &rqda[i][1]);
        break;
      case 'L':
        /* get the category */
        snprintf (qi->category, QIF_SIZE_CAT, "%s", &rqda[i][1]);
        break;
      case 'N':
        /* get the check number */
        snprintf (qi->check, QIF_SIZE_CHECK, "%s", &rqda[i][1]);
        break;
      case 'E':
        /* comment in split - ignore it */
        break;
      case 'R':
        /* ignore it */
        break;
      case 'I':
        /* ignore it */
        break;
      case 'B':
        /* ignore it */
        break;
      case 'S':
        /* process split rqda with S and $ prefixes */
        qi->isSplit = TRUE;
        qi->sia[splitNum] = malloc (sizeof (splitItem));
        memset (qi->sia[splitNum], 0, sizeof (splitItem)); 
        snprintf (qi->sia[splitNum]->split_cat, QIF_SIZE_CAT, "%s", &rqda[i][1]);
        i++;
        if (i >= QIF_SIZE_RQDA || rqda[i][0] == '\0') {
          free (qi->sia[splitNum]);
          qi->sia[splitNum] = 0;
        }
        else {
          if (rqda[i][0] == 'E')
            i++;
          snprintf (qi->sia[splitNum]->amt, QIF_SIZE_TRAN, "%s", &rqda[i][1]);
        }
        splitNum++;
        break;
      default:
        /* unrecognized - just ignore it */
        printf ("\n***Warning: qif file \"%s\", had an "
            "unrecognized Bank item near line %d: %s\n",
            file, lnctr, rqda[i]);
        break;
    }
  }
  /* OK, we've got the rqda in the form of a qifParser; process it */
  return qi;
}

/*
 * do_add
 *
 * This function adds a transaction.
 */
static int do_add (void)
{
  char *errmsg = 0;
  int ret;
  int cat;
  int tran_num;
  char tmp[SIZE_ARB+1];
  char catt[FIELD_ARB+1];

  if (! opt->is_amt) {
    printf ("\n***Error in do_add(), line %d: no amount entered on the command-line\n", __LINE__);
    return -1;
  }
  if (! opt->is_cat && ! opt->catt) {
    printf ("\n***Error in do_add(), line %d: no category entered on the command-line\n", __LINE__);
    return -1;
  }
  if (! opt->is_to) {
    printf ("\n***Error in do_add(), line %d: no To: field entered in transaction\n", __LINE__);
    return -1;
  }
  if (! opt->is_cmt) {
    printf ("\n***Error in do_add(), line %d: no comment entered in transaction\n", __LINE__);
    return -1;
  }

  if (opt->catt) {
    cat = get_cat_num_from_name (opt->catt);
    strcpy (catt, opt->catt);
  }
  else {
    cat = opt->cat;
    ret = get_cat_name_from_num (cat, catt);
    if (ret == -1)
      cat = -1;
  }
  if (cat == -1) {
    printf ("\n***Error in do_add(), line %d: Invalid category name or number\n", __LINE__);
    return -1;
  }

  ret = verify_number (opt->amt);
  if (!ret)
    return ret;

  tran_num = get_next_tran_num ();
  if (tran_num == -1)
    return -1;

  if (opt->is_date) {
    opt->date[SIZE_TSTMP] = '\0';
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO tran VALUES (%d,%d,'%s','%s','NPST','%s','%s');\nCOMMIT;\n",
        tran_num, cat, opt->date, opt->amt, opt->to, opt->cmt);
  }
  else {
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO tran VALUES (%d,%d,datetime('now','localtime'),'%s','NPST','%s','%s');\nCOMMIT;\n",
        tran_num, cat, opt->amt, opt->to, opt->cmt);
  }
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_add(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    ret = -1;
    return ret;
  }
  if (opt->is_date) {
    opt->date[SIZE_TSTMP] = '\0';
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO act VALUES ('TRN',%d,%d,'%s','%s','%s','%s');\nCOMMIT;\n",
        cat, tran_num, opt->date, opt->amt, opt->to, opt->cmt);
  }
  else {
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO act VALUES ('TRN',%d,%d,datetime('now','localtime'),'%s','%s','%s');\nCOMMIT;\n",
        cat, tran_num, opt->amt, opt->to, opt->cmt);
  }
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_add(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    ret = -1;
    return ret;
  }
  printf ("Journalized %s for the %d (%s) category.\n", opt->amt, cat, catt);

  return 0;
}

/*
 * do_new_cat
 *
 * This function adds a category to a budget.
 */
static int do_new_cat (void)
{
  int ret;
  int new_cat;
  char *errmsg = 0;
  char tmp[SIZE_ARB+1];
  char tmp1[SIZE_ARB+1];

  if (! opt->is_catt) {
    printf ("\n***Error in do_new_cat (), line %d: Called without --catt having been entered.\n", __LINE__);
    return -1;
  }
  ret = get_cat_num_from_name (opt->catt);
  if (ret != -1) {
    printf ("\n***Error in do_new_cat(), line %d: Trying to create a category with a name that already exists: '%s'\n", __LINE__, opt->catt);
    return -1;
  }
  new_cat = get_next_cat_num ();
  snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO cat VALUES (%d, datetime('now','localtime'), '%s', '0.00', '%s');\nCOMMIT;\n", new_cat, opt->catt,
      (opt->is_cmt ? opt->cmt : "Create a new Category."));
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_new_cat(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    ret = -1;
    return ret;
  }
  snprintf (tmp1, SIZE_ARB, "Add cat %d, %s", new_cat, opt->catt);
  snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO act VALUES ('CAT',%d,NULL,datetime('now','localtime'),NULL,'%s','%s');\nCOMMIT;\n", 
      new_cat, opt->catt, tmp1);
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_new_cat(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    ret = -1;
    return ret;
  }
  snprintf (tmp1, SIZE_ARB, "Created cat %d named '%s'\n", new_cat, opt->catt);
  printf ("%s\n", tmp1);
  return 0;
}

/*
 * do_cedit
 *
 * This function allows the user to edit a category.
 */
#if 0
static int do_cedit (void)
{
  int ret;
  int new_cat;
  char *errmsg = 0;
  char tmp[SIZE_ARB+1];
  char tmp1[SIZE_ARB+1];

  if (! opt->is_cat) {
    printf ("\n***Error in do_cedit (), line %d: Called without --cat having been entered.\n", __LINE__);
    return -1;
  }
  if (opt->is_catt) {
    ret = get_cat_num_from_name (opt->catt);
    if (ret != -1) {
      printf ("\n***Error in do_cedit(), line %d: Trying to update a category with a name that already exists: '%s'\n", __LINE__, opt->catt);
      return -1;
    }
  }
  // FIXME
  // Continue here
  // FIXME
  snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO cat VALUES (%d, datetime('now','localtime'), '%s', '0.00', '%s');\nCOMMIT;\n", new_cat, opt->catt,
      (opt->is_cmt ? opt->cmt : "Create a new Category."));
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_cedit(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    ret = -1;
    return ret;
  }
  snprintf (tmp1, SIZE_ARB, "Add cat %d, %s", new_cat, opt->catt);
  snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO act VALUES ('CAT',%d,NULL,datetime('now','localtime'),NULL,'%s','%s');\nCOMMIT;\n", 
      new_cat, opt->catt, tmp1);
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_cedit(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    ret = -1;
    return ret;
  }
  snprintf (tmp1, SIZE_ARB, "Updated cat %d named '%s'\n", new_cat, opt->catt);
  printf ("%s\n", tmp1);
  return 0;
}
#endif

typedef struct _tran_dat {
  int num;
  int cat_num;
  char dtime[SIZE_TSTMP+1];
  char amt[SIZE_AMT+1];
} tran_dat;

/*
 * do_post
 *
 * This function posts transactions.  If recalc is 0, only the transactions that haven't been posted before get changed.  If recalc is 1, do_post
 * recalculates everything from the beginning of the trans table.
 */
static int do_post (int recalc)
{
  char *errmsg = 0;
  c_item *c;
  int ret;
  int i;
  int num_cats;
  int num_trans;
  char tmp[SIZE_ARB+1];
  char *cp;
  tran_dat *td;
  int cat_ary[MD_ARY];

  /* first, grab everything from the cat table that we need and populate opt->catList */
  snprintf (tmp, SIZE_ARB, "SELECT num,dtime,name,amt FROM cat ORDER BY num;");
  ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_post(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    del_cb_data();
    return -1;
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    printf ("\n***Error in do_post(), line %d: The SQL query '%s' generated no data and it should have.\n", __LINE__, tmp);
    printf ("This probably means that the default bgt database has not been initialized properly.\n");
    printf ("If there are no categories in the default budget, add some and try again.\n");
    printf ("Worst case, remove the budget directory (rm -rf ~/.bgt) and try again.\n");
    printf ("See the man page for more information.\n");
    del_cb_data();
    return -1;
  }
  num_cats = c_head->num_items;
  for (c = c_head->next; c != c_tail; c = c->next) {
    int cat = atoi (c->item[0]);
    if (cat > MD_ARY) {
      printf ("\n***Error in do_post(), line %d: You have more categories in %s than are allowed (max %d, you have %d)\n", __LINE__, opt->db_name, MD_ARY, cat);
      del_cb_data();
      return -1;
    }
    opt->catList[cat].cat = cat;
    strncpy (opt->catList[cat].dtime, c->item[1], SIZE_TSTMP);
    strncpy (opt->catList[cat].name, c->item[2], FIELD_ARB);
    if (recalc == 0)
      /* posting - do not reset category amounts back to 0 */
      strncpy (opt->catList[cat].amt, c->item[3], SIZE_AMT);
    else
      /* recalculating - reset category amounts back to 0 */
      strcpy (opt->catList[cat].amt, "0.00");
  }
  del_cb_data();
  /* now, let's grab the transactions that we need to process */
  if (recalc == 0)
    /* not doing a recalc - just grab what hasn't been posted yet. */
    snprintf (tmp, SIZE_ARB, "SELECT num,cat_num,dtime,amt FROM tran WHERE status = 'NPST';");
  else
    /* doing a recalc - grab everything */
    snprintf (tmp, SIZE_ARB, "SELECT num,cat_num,dtime,amt FROM tran;");
  ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_post(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    del_cb_data();
    return -1;
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    if (! opt->is_quiet)
      printf ("\nNothing to post.\n");
    del_cb_data();
    return 0;
  }
  num_trans = c_head->num_items;
  td = malloc (sizeof (tran_dat));
  if (0 == td) {
    printf ("\n***Error in do_post(), line %d: Fatal memory error allocating %d bytes.", __LINE__, sizeof (tran_dat));
    del_cb_data();
    return -1;
  }
  for (i = 0; i < MD_ARY; i++)
    cat_ary[i] = 0;
  for (c = c_head->next; c != c_tail; c = c->next) {
    memset (td, 0, sizeof(tran_dat));
    td->num = atoi (c->item[0]);
    td->cat_num = atoi (c->item[1]);
    strncpy (td->dtime, c->item[2], SIZE_TSTMP);
    strncpy (td->amt, c->item[3], SIZE_AMT);
    cp = bcnum_add (opt->catList[td->cat_num].amt, td->amt, 2);
    strncpy (opt->catList[td->cat_num].amt, cp, SIZE_AMT);
    /* flag the record in the transaction table as posted */
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nUPDATE tran SET status = 'PSTD' WHERE num = %d AND status != 'PSTD';\nCOMMIT;\n", td->num);
    ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_post(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      ret = -1;
      return ret;
    }
    cat_ary[td->cat_num] = 1;
  }
  /* now, update the categories with the data that was entered */
  for (i = 0; i < MD_ARY; i++) {
    if (opt->catList[i].cat == 0)
      continue;
    if (cat_ary[i] == 0)
      continue;
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nUPDATE cat SET amt = '%s',dtime=datetime('now','localtime') WHERE num = %d;\nCOMMIT;\n", opt->catList[i].amt, opt->catList[i].cat);
    ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_post(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      ret = -1;
      return ret;
    }
  }
  if (! opt->is_quiet)
    printf ("Posted %d transactions to %d categories\n", num_trans, num_cats);
  return 0;
}

/*
 * proc_nclr_file
 *
 * This function opens an nclr file, parses it, and alters the totals according to what was in it.
 */
static int proc_nclr_file (void)
{
  nclr_dat *nd;
  FILE *fp;
  int lnctr = 0;
  char *cp, *cp1;
  int cat;
  char line[SIZE_ARB+1];

  fp = fopen (opt->nclr, "rb");
  if (fp == 0) {
    printf ("\n***Warning in proc_nclr_file(), line %d: could not open %s to read.\n", __LINE__, opt->nclr);
    printf ("\tNot processing nclr data.\n");
    return -1;
  }
  while (fgets (line, SIZE_ARB, fp) != 0) {
    lnctr ++;
    if (line[0] == '\n')
      continue;
    if (line[0] == '#')
      continue;
    if (line[0] == '/' && line[1] == '/')
      continue;
    nd = malloc (sizeof(nclr_dat));
    if (nd == 0) {
      printf ("\n***Warning in proc_nclr_file(), line %d: Allocating %d bytes of memory.\n", __LINE__, sizeof(nclr_dat));
      printf ("\tNot processing nclr data.\n");
      fclose (fp);
      return -1;
    }
    memset (nd, 0, sizeof(nclr_dat));
    cp = strchr (line, '\n');
    if (0 != cp)
      *cp = '\0';
    cp1 = strchr (line, ':');
    if (cp1 == 0) {
      printf ("\n***Warning in proc_nclr_file(), line %d: Syntax error (no ':'), line %d, '%s'.\n", __LINE__, lnctr, line);
      printf ("\tNot processing nclr data.\n");
      free (nd);
      do {
        nd = ndlist;
        ndlist = ndlist->next;
        if (nd != 0)
          free (nd);
      } while (ndlist != NULL);
      ndlist = 0;
      fclose (fp);
      return -1;
    }
    *cp1 = '\0';
    cp = cp1+1;
    if (verify_number (cp) == FALSE) {
      printf ("\n***Warning in proc_nclr_file(), line %d: Invalid number in %s, line %d, '%s'\n", __LINE__, opt->nclr, lnctr, cp);
      printf ("\tNot processing nclr data.\n");
      do {
        nd = ndlist;
        ndlist = ndlist->next;
        if (nd != 0)
          free (nd);
      } while (ndlist != NULL);
      ndlist = 0;
      fclose (fp);
      return -1;
    }
    cat = get_cat_num_from_name (line);
    if (cat == -1) {
      printf ("\n***Warning in proc_nclr_file(), line %d: Invalid category name in %s, line %d, '%s'\n", __LINE__, opt->nclr, lnctr, cp);
      printf ("\tNot processing nclr data.\n");
      do {
        nd = ndlist;
        ndlist = ndlist->next;
        if (nd != 0)
          free (nd);
      } while (ndlist != NULL);
      ndlist = 0;
      fclose (fp);
      return -1;
    }
    nd->line = lnctr;
    nd->cat = cat;
    nd->next = 0;
    strncpy (nd->amt, cp, SIZE_AMT);
    if (ndlist == 0) {
      ndlist = nd;
      lastnd = nd;
    }
    else {
      lastnd->next = nd;
      lastnd = nd;
    }
  }
  fclose (fp);
  return 0;
}

/*
 * do_nclr
 *
 * This function is the high-level function that sets up for an nclr call.
 */
static int do_nclr (void)
{
  nclr_dat *nd;
  int ret;
  char *cp;

  ret = proc_nclr_file ();
  if (ret)
    /* ignore the error and return */
    return 0;

  for (nd = ndlist; nd != 0; nd = nd->next) {
    cp = bcnum_add (opt->catList[nd->cat].amt, nd->amt, 2);
    strncpy (opt->catList[nd->cat].amt, cp, SIZE_AMT);
  }

  /* Clean up the nclr file data */
  do {
    nd = ndlist;
    ndlist = ndlist->next;
    if (nd != 0)
      free (nd);
  } while (ndlist != NULL);
  ndlist = 0;

  return 0;
}

static int do_ls (void)
{
  int ret;
  int i;
  char *cp;
  char tot[SIZE_AMT+1];

  ret = do_post(0);
  if (ret)
    return ret;

  if (opt->is_nclr) {
    ret = do_nclr();
    if (ret)
      return ret;
  }
  if (opt->is_tot && ! opt->is_catt) {
    printf ("\n***Error in do_ls(), line %d: Need --catt CAT with --ls --tot\n", __LINE__);
    return (-1);
  }

  if (! opt->is_tot) {
    strcpy (tot, "0.00");
    printf ("==========================================================================================\n");
    printf ("CATEGORY    DATE/TIME             NAME                                              AMOUNT\n");
    printf ("------------------------------------------------------------------------------------------\n");
  }
  for (i = 0; i < MD_ARY; i++) {
    if (opt->catList[i].cat == 0)
      continue;
    if (! opt->is_tot) {
      printf ("%-12d%-22s%-40s%16s\n", opt->catList[i].cat, opt->catList[i].dtime, opt->catList[i].name, opt->catList[i].amt);
      cp = bcnum_add (tot, opt->catList[i].amt, 2);
      strncpy (tot, cp, SIZE_AMT);
    }
    else {
      if (! strcmp (opt->catList[i].name, opt->catt)) {
        printf ("%s\n", opt->catList[i].amt);
        return 0;
      }
    }
  }
  printf ("==========================================================================================\n");
  printf ("                                                                   Total: %16s\n", tot);
  printf ("==========================================================================================\n");
  return 0;
}
/*
 * do_recalc
 *
 * This function forces a recalculation of all the transactions in the tran table.
 */
static int do_recalc ()
{
  int ret;
  int i;
  char *cp;
  char tot[SIZE_AMT+1];

  ret = do_post (1);
  if (ret)
    return ret;
  strcpy (tot, "0.00");
  printf ("==========================================================================================\n");
  printf ("CATEGORY    DATE/TIME             NAME                                              AMOUNT\n");
  for (i = 0; i < MD_ARY; i++) {
    if (opt->catList[i].cat == 0)
      continue;
    printf ("%-12d%-22s%-40s%16s\n", opt->catList[i].cat, opt->catList[i].dtime, opt->catList[i].name, opt->catList[i].amt);
    cp = bcnum_add (tot, opt->catList[i].amt, 2);
    strncpy (tot, cp, SIZE_AMT);
  }
  printf ("==========================================================================================\n");
  printf ("                                                                   Total: %16s\n", tot);
  printf ("==========================================================================================\n");
  return 0;
}

/*
 * do_qry
 *
 * This function parses out the query string and does the specified query.
 */
static int do_qry(void)
{
  int ret;
  char *errmsg = 0;
  c_item *c;
  int len;
  int num_trans;
  char tmp[SIZE_ARB+1];

  if (opt->qry[0] == 'd' && opt->qry[1] == 't' && opt->qry[2] == ':') {
    /* query on the date */
    len = strlen (opt->qry);
    if (len <= 3) {
      printf ("\n***Error in do_qry(), line %d: a query string of %s is invalid\n", __LINE__, opt->qry);
      return -1;
    }
    snprintf (tmp, SIZE_ARB,
        "SELECT t.num,c.name,t.dtime,t.amt,t.status,t.to_who,t.comment FROM tran t,cat c WHERE t.dtime like '%%%s%%' AND t.cat_num = c.num ORDER BY t.dtime;",
        &(opt->qry[3]));
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_qry(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
    if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
      printf ("\n***Warning in do_qry(), line %d: There aren't any items in the tran table matching query string %s\n", __LINE__, opt->qry);
      del_cb_data();
      return 0;
    }
    num_trans = c_head->num_items;
    for (c = c_head; c != c_tail; c = c->next) {
      if (c->item[0])
        printf ("%-10s%-15s |%-19s| %15s (%s) To:'%s'  Cmt:'%s'\n",c->item[0],c->item[1],c->item[2],c->item[3],c->item[4],c->item[5],c->item[6]);
    }
    printf ("Processed %d items\n", num_trans);
    del_cb_data ();
    return 0;
  }
  if (opt->qry[0] == 's' && opt->qry[1] == 't' && opt->qry[2] == ':') {
    /* query on the status */
    len = strlen (opt->qry);
    if (len <= 3) {
      printf ("\n***Error in do_qry(), line %d: a query string of %s is invalid\n", __LINE__, opt->qry);
      return -1;
    }
    snprintf (tmp, SIZE_ARB,
        "SELECT t.num,c.name,t.dtime,t.amt,t.status,t.to_who,t.comment FROM tran t,cat c WHERE t.status like '%%%s%%' AND t.cat_num = c.num ORDER BY t.dtime;",
        &(opt->qry[3]));
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_qry(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
    if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
      printf ("\n***Warning in do_qry(), line %d: There aren't any items in the tran table matching query string %s\n", __LINE__, opt->qry);
      del_cb_data();
      return 0;
    }
    num_trans = c_head->num_items;
    for (c = c_head; c != c_tail; c = c->next) {
      if (c->item[0])
        printf ("%-10s%-15s |%-19s| %15s (%s) To:'%s'  Cmt:'%s'\n",c->item[0],c->item[1],c->item[2],c->item[3],c->item[4],c->item[5],c->item[6]);
    }
    printf ("Processed %d items\n", num_trans);
    del_cb_data ();
    return 0;
  }
  if (opt->qry[0] == 't' && opt->qry[1] == 'o' && opt->qry[2] == ':') {
    /* query on the date */
    len = strlen (opt->qry);
    if (len <= 3) {
      printf ("\n***Error in do_qry(), line %d: a query string of %s is invalid\n", __LINE__, opt->qry);
      return -1;
    }
    snprintf (tmp, SIZE_ARB,
        "SELECT t.num,c.name,t.dtime,t.amt,t.status,t.to_who,t.comment FROM tran t,cat c WHERE t.to_who like '%%%s%%' AND t.cat_num = c.num ORDER BY t.dtime;",
        &(opt->qry[3]));
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_qry(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
    if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
      printf ("\n***Warning in do_qry(), line %d: There aren't any items in the tran table matching query string %s\n", __LINE__, opt->qry);
      del_cb_data();
      return 0;
    }
    num_trans = c_head->num_items;
    for (c = c_head; c != c_tail; c = c->next) {
      if (c->item[0])
        printf ("%-10s%-15s |%-19s| %15s (%s) To:'%s'  Cmt:'%s'\n",c->item[0],c->item[1],c->item[2],c->item[3],c->item[4],c->item[5],c->item[6]);
    }
    printf ("Processed %d items\n", num_trans);
    del_cb_data ();
    return 0;
  }
  if (opt->qry[0] == 'c' && opt->qry[1] == 'a' && opt->qry[2] == 't' && opt->qry[3] == ':') {
    /* query on the date */
    len = strlen (opt->qry);
    if (len <= 4) {
      printf ("\n***Error in do_qry(), line %d: a query string of %s is invalid\n", __LINE__, opt->qry);
      return -1;
    }
    snprintf (tmp, SIZE_ARB,
        "SELECT t.num,c.name,t.dtime,t.amt,t.status,t.to_who,t.comment FROM tran t,cat c WHERE c.name LIKE '%%%s%%' AND t.cat_num = c.num ORDER BY t.dtime;",
        &(opt->qry[4]));
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_qry(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
    if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
      printf ("\n***Warning in do_qry(), line %d: There aren't any items in the tran table matching query string %s\n", __LINE__, opt->qry);
      del_cb_data();
      return 0;
    }
    num_trans = c_head->num_items;
    for (c = c_head; c != c_tail; c = c->next) {
      if (c->item[0])
        printf ("%-10s%-15s |%-19s| %15s (%s) To:'%s'  Cmt:'%s'\n",c->item[0],c->item[1],c->item[2],c->item[3],c->item[4],c->item[5],c->item[6]);
    }
    printf ("Processed %d items\n", num_trans);
    del_cb_data ();
    return 0;
  }
  if (opt->qry[0] == 'c' && opt->qry[1] == 'm' && opt->qry[2] == 't' && opt->qry[3] == ':') {
    /* query on the date */
    len = strlen (opt->qry);
    if (len <= 4) {
      printf ("\n***Error in do_qry(), line %d: a query string of %s is invalid\n", __LINE__, opt->qry);
      return -1;
    }
    snprintf (tmp, SIZE_ARB,
        "SELECT t.num,c.name,t.dtime,t.amt,t.status,t.to_who,t.comment FROM tran t,cat c WHERE t.comment LIKE '%%%s%%' AND t.cat_num = c.num ORDER BY t.dtime;",
        &(opt->qry[4]));
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_qry(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
    if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
      printf ("\n***Warning in do_qry(), line %d: There aren't any items in the tran table matching query string %s\n", __LINE__, opt->qry);
      del_cb_data();
      return 0;
    }
    num_trans = c_head->num_items;
    for (c = c_head; c != c_tail; c = c->next) {
      if (c->item[0])
        printf ("%-10s%-15s |%-19s| %15s (%s) To:'%s'  Cmt:'%s'\n",c->item[0],c->item[1],c->item[2],c->item[3],c->item[4],c->item[5],c->item[6]);
    }
    printf ("Processed %d items\n", num_trans);
    del_cb_data ();
    return 0;
  }
  if (opt->qry[0] == 'a' && opt->qry[1] == 'm' && opt->qry[2] == 't' && opt->qry[3] == ':') {
    /* query on the date */
    len = strlen (opt->qry);
    if (len <= 4) {
      printf ("\n***Error in do_qry(), line %d: a query string of %s is invalid\n", __LINE__, opt->qry);
      return -1;
    }
    snprintf (tmp, SIZE_ARB,
        "SELECT t.num,c.name,t.dtime,t.amt,t.status,t.to_who,t.comment FROM tran t,cat c WHERE t.amt like '%%%s%%' ORDER BY t.dtime;",
        &(opt->qry[4]));
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_qry(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
    if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
      printf ("\n***Warning in do_qry(), line %d: There aren't any items in the tran table matching query string %s\n", __LINE__, opt->qry);
      del_cb_data();
      return 0;
    }
    num_trans = c_head->num_items;
    for (c = c_head; c != c_tail; c = c->next) {
      if (c->item[0])
        printf ("%-10s%-15s |%-19s| %15s (%s) To:'%s'  Cmt:'%s'\n",c->item[0],c->item[1],c->item[2],c->item[3],c->item[4],c->item[5],c->item[6]);
    }
    printf ("Processed %d items\n", num_trans);
    del_cb_data ();
    return 0;
  }
  if (opt->qry[0] == 'm' && opt->qry[1] == 'r') {
    /* Machine readable dump of everything */
    snprintf (tmp, SIZE_ARB,
        "SELECT t.num,c.name,t.dtime,t.amt,t.status,t.to_who,t.comment FROM tran t,cat c WHERE t.cat_num = c.num ORDER BY t.dtime;");
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_qry(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
    if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
      printf ("\n***Warning in do_qry(), line %d: There aren't any items in the tran table\n", __LINE__);
      del_cb_data();
      return 0;
    }
    num_trans = c_head->num_items;
    for (c = c_head; c != c_tail; c = c->next) {
      if (c->item[0])
        printf ("%s:%s:%s:%s:%s:%s:%s\n",c->item[0],c->item[1],c->item[2],c->item[3],c->item[4],c->item[5],c->item[6]);
    }
    printf ("Processed %d items\n", num_trans);
    del_cb_data ();
    return 0;
  }
  /* anything else => do all, in human readable form */
  snprintf (tmp, SIZE_ARB,
      "SELECT t.num,c.name,t.dtime,t.amt,t.status,t.to_who,t.comment FROM tran t,cat c WHERE t.cat_num = c.num ORDER BY t.dtime;");
  ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_qry(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    del_cb_data();
    return -1;
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    printf ("\n***Warning in do_qry(), line %d: There aren't any items in the tran table\n", __LINE__);
    del_cb_data();
    return 0;
  }
  num_trans = c_head->num_items;
  for (c = c_head; c != c_tail; c = c->next) {
    if (c->item[0])
      printf ("%-10s%-15s |%-19s| %15s (%s) To:'%s'  Cmt:'%s'\n",c->item[0],c->item[1],c->item[2],c->item[3],c->item[4],c->item[5],c->item[6]);
  }
  printf ("Processed %d items\n", num_trans);
  del_cb_data ();
  return 0;
}

/*
 * do_edit
 *
 * This function processes an edit command.
 */
static int do_edit (void)
{
  int ret;
  int new_tran;
  int val;
  char *errmsg = 0;
  char tmp[SIZE_ARB+1];

  if (! opt->is_tran) {
    printf ("\n***Error in do_edit(), line %d: Must specify a transaction number\n", __LINE__);
    return -1;
  }
  if (!opt->is_to && !opt->is_amt && !opt->is_cat && !opt->is_catt && !opt->is_cmt && !opt->is_date) {
    printf ("\n***Error in do_edit(), line %d: User must specify something to change for transaction %d\n", __LINE__, opt->tran);
    return -1;
  }
  new_tran = get_next_tran_num ();
  if (opt->tran > new_tran-1 || opt->tran < 0) {
    printf ("\n***Error in do_edit(), line %d: Invalid transaction number %d\n", __LINE__, opt->tran);
    return -1;
  }
  if (opt->is_amt) {
    ret = verify_number (opt->amt);
    if (!ret)
      return ret;
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nUPDATE tran SET amt = %s WHERE num = %d;\nCOMMIT;\n", opt->amt, opt->tran);
    ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_edit(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      ret = -1;
      return ret;
    }
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO act VALUES ('EDT',NULL,%d,datetime('now','localtime'),'%s',NULL,NULL);\nCOMMIT;\n",
        opt->tran,opt->amt);
    ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_add(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      ret = -1;
      return ret;
    }
  }
  if (opt->is_cat) {
    val = get_next_cat_num ();
    if (opt->cat < 0 || opt->cat > val-1) {
      printf ("\n***Error in do_edit(), line %d: Invalid category number %d\n", __LINE__, opt->cat);
      return -1;
    }
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nUPDATE tran SET cat_num = %d WHERE num = %d;\nCOMMIT;\n", opt->cat, opt->tran);
    ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_edit(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      ret = -1;
      return ret;
    }
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO act VALUES ('EDT',%d,%d,datetime('now','localtime'),NULL,NULL,NULL);\nCOMMIT;\n",
        opt->cat, opt->tran);
    ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_add(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      ret = -1;
      return ret;
    }
  }
  if (opt->is_to) {
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nUPDATE tran SET to_who = '%s' WHERE num = %d;\nCOMMIT;\n", opt->to, opt->tran);
    ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_edit(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      ret = -1;
      return ret;
    }
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO act VALUES ('EDT',NULL,%d,datetime('now','localtime'),NULL,'%s',NULL);\nCOMMIT;\n",
        opt->tran, opt->to);
    ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_add(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      ret = -1;
      return ret;
    }
  }
  if (opt->is_cmt) {
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nUPDATE tran SET comment = '%s' WHERE num = %d;\nCOMMIT;\n", opt->cmt, opt->tran);
    ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_edit(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      ret = -1;
      return ret;
    }
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO act VALUES ('EDT',NULL,%d,datetime('now','localtime'),NULL,NULL,'%s');\nCOMMIT;\n",
        opt->tran, opt->cmt);
    ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_add(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      ret = -1;
      return ret;
    }
  }
  if (opt->is_date) {
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nUPDATE tran SET dtime = '%s' WHERE num = %d;\nCOMMIT;\n", opt->date, opt->tran);
    ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_edit(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      ret = -1;
      return ret;
    }
    snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO act VALUES ('EDT',NULL,%d,datetime('now','localtime'),NULL,NULL,'%s');\nCOMMIT;\n",
        opt->tran, opt->date);
    ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_add(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      ret = -1;
      return ret;
    }
  }

  if (opt->is_amt || opt->is_cat)
    return (do_post (1));  // need to repost everything.
  return 0;
}

/*
 * do_rm
 *
 * This function processes an rm command.  It replicates a transaction into the journal table and then removes it from the tran table.
 */
static int do_rm (void)
{
  int ret;
  int new_tran;
  char *errmsg = 0;
  char tmp[SIZE_ARB+1];

  if (! opt->is_tran) {
    printf ("\n***Error in do_rm(), line %d: Must specify a transaction number\n", __LINE__);
    return -1;
  }
  new_tran = get_next_tran_num ();
  if (opt->tran >= new_tran || opt->tran < 0) {
    printf ("\n***Error: do_rm(), line %d: Invalid tran_id %d\n", __LINE__, opt->tran);
    return -1;
  }
  /* first, update the status to show removed. */
  snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nUPDATE tran SET status = 'RMVD' WHERE num = %d;\nCOMMIT;\n", opt->tran);
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_edit(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    ret = -1;
    return ret;
  }
  /* then, copy to the archive table */
  snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO arch SELECT * FROM tran WHERE num = %d;\nCOMMIT;\n", opt->tran);
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_edit(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    ret = -1;
    return ret;
  }
  /* finally, remove it */
  snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nDELETE FROM tran WHERE num = %d;\nCOMMIT;\n", opt->tran);
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_edit(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    ret = -1;
    return ret;
  }
  snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO act VALUES ('RMV',NULL,%d,datetime('now','localtime'),NULL,NULL,NULL);\nCOMMIT;\n",
      opt->tran);
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_edit(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    ret = -1;
    return ret;
  }
  return (do_post (1)); // need to repost everything
}

/*
 * do_exp
 *
 * This function processes the exp command.  This looks at only expenses in the tran table, not including ones with adjust in the to or cmt field.
 */
static int do_exp (void)
{
  char *errmsg = 0;
  c_item *c;
  int ret;
  int i;
  int num_cats;
  int num_trans;
  char tmp[SIZE_ARB+1];
  char *cp;
  tran_dat *td;
  char tot[SIZE_AMT+1];

  /* first, grab everything from the cat table that we need and populate opt->catList */
  snprintf (tmp, SIZE_ARB, "SELECT num,dtime,name,amt FROM cat ORDER BY num;");
  ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    del_cb_data();
    return -1;
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    printf ("\n***Error in do_exp(), line %d: The SQL query '%s' generated no data and it should have.", __LINE__, tmp);
    del_cb_data();
    return -1;
  }
  num_cats = c_head->num_items;
  for (c = c_head->next; c != c_tail; c = c->next) {
    int cat = atoi (c->item[0]);
    if (cat > MD_ARY) {
      printf ("\n***Error in do_exp(), line %d: You have more categories in %s than are allowed (max %d, you have %d)\n", __LINE__, opt->db_name, MD_ARY, cat);
      del_cb_data();
      return -1;
    }
    opt->catList[cat].cat = cat;
    strncpy (opt->catList[cat].dtime, c->item[1], SIZE_TSTMP);
    strncpy (opt->catList[cat].name, c->item[2], FIELD_ARB);
    /* set category amounts to 0 */
    strcpy (opt->catList[cat].amt, "0.00");
  }
  del_cb_data();
  /* now, let's grab the transactions that we need to process */
  if (opt->is_beg == TRUE && opt->is_end == TRUE) {
    /* process beg and end limits */
    snprintf (tmp, SIZE_ARB,
        "SELECT num,cat_num,dtime,amt FROM tran WHERE amt LIKE '%%-%%' AND num BETWEEN %d AND %d AND to_who NOT LIKE '%%adjust%%';",
        opt->beg, opt->end);
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  else if (opt->is_beg == TRUE) {
    /* only opt->beg */
    snprintf (tmp, SIZE_ARB,
        "SELECT num,cat_num,dtime,amt FROM tran WHERE amt LIKE '%%-%%' AND num >= %d AND to_who NOT LIKE '%%adjust%%';",
        opt->beg);
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  else if (opt->is_end == TRUE) {
    /* only opt->end */
    snprintf (tmp, SIZE_ARB,
        "SELECT num,cat_num,dtime,amt FROM tran WHERE amt LIKE '%%-%%' AND num <= %d AND to_who NOT LIKE '%%adjust%%';",
        opt->end);
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  else {
    /* otherwise, there are no limits - calculate everything */
    snprintf (tmp, SIZE_ARB, "SELECT num,cat_num,dtime,amt FROM tran WHERE amt LIKE '%%-%%' AND to_who NOT LIKE '%%adjust%%';");
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    printf ("\nNothing to consider.\n");
    del_cb_data();
    return 0;
  }
  num_trans = c_head->num_items;
  td = malloc (sizeof (tran_dat));
  if (0 == td) {
    printf ("\n***Error in do_exp(), line %d: Fatal memory error allocating %d bytes.", __LINE__, sizeof (tran_dat));
    del_cb_data();
    return -1;
  }
  for (c = c_head->next; c != c_tail; c = c->next) {
    memset (td, 0, sizeof(tran_dat));
    td->num = atoi (c->item[0]);
    td->cat_num = atoi (c->item[1]);
    strncpy (td->dtime, c->item[2], SIZE_TSTMP);
    strncpy (td->amt, c->item[3], SIZE_AMT);
    cp = bcnum_add (opt->catList[td->cat_num].amt, td->amt, 2);
    strncpy (opt->catList[td->cat_num].amt, cp, SIZE_AMT);
  }
  /* now, print the data */
  strcpy (tot, "0.00");
  printf ("==========================================================================================\n");
  printf ("CATEGORY    DATE/TIME             NAME                                              AMOUNT\n");
  for (i = 0; i < MD_ARY; i++) {
    if (opt->catList[i].cat == 0)
      continue;
    printf ("%-12d%-22s%-40s%16s\n", opt->catList[i].cat, opt->catList[i].dtime, opt->catList[i].name, opt->catList[i].amt);
    cp = bcnum_add (tot, opt->catList[i].amt, 2);
    strncpy (tot, cp, SIZE_AMT);
  }
  printf ("==========================================================================================\n");
  printf ("                                                                   Total: %16s\n", tot);
  printf ("==========================================================================================\n");

  printf ("Posted %d transactions to %d categories\n", num_trans, num_cats);
  return 0;
}

/*
 * do_inc
 *
 * This function processes the inc command.  This looks at only income in the tran table, not including ones with adjust in the to or cmt field.
 */
static int do_inc (void)
{
  char *errmsg = 0;
  c_item *c;
  int ret;
  int i;
  int num_cats;
  int num_trans;
  char tmp[SIZE_ARB+1];
  char *cp;
  tran_dat *td;
  char tot[SIZE_AMT+1];

  /* first, grab everything from the cat table that we need and populate opt->catList */
  snprintf (tmp, SIZE_ARB, "SELECT num,dtime,name,amt FROM cat ORDER BY num;");
  ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    del_cb_data();
    return -1;
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    printf ("\n***Error in do_exp(), line %d: The SQL query '%s' generated no data and it should have.", __LINE__, tmp);
    del_cb_data();
    return -1;
  }
  num_cats = c_head->num_items;
  for (c = c_head->next; c != c_tail; c = c->next) {
    int cat = atoi (c->item[0]);
    if (cat > MD_ARY) {
      printf ("\n***Error in do_exp(), line %d: You have more categories in %s than are allowed (max %d, you have %d)\n", __LINE__, opt->db_name, MD_ARY, cat);
      del_cb_data();
      return -1;
    }
    opt->catList[cat].cat = cat;
    strncpy (opt->catList[cat].dtime, c->item[1], SIZE_TSTMP);
    strncpy (opt->catList[cat].name, c->item[2], FIELD_ARB);
    /* set category amounts to 0 */
    strcpy (opt->catList[cat].amt, "0.00");
  }
  del_cb_data();
  if (opt->is_beg == TRUE && opt->is_end == TRUE) {
    /* process beg and end limits */
    snprintf (tmp, SIZE_ARB,
        "SELECT num,cat_num,dtime,amt FROM tran WHERE amt NOT LIKE '%%-%%' AND num BETWEEN %d AND %d AND to_who NOT LIKE '%%adjust%%';",
        opt->beg, opt->end);
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  else if (opt->is_beg == TRUE) {
    /* only opt->beg */
    snprintf (tmp, SIZE_ARB,
        "SELECT num,cat_num,dtime,amt FROM tran WHERE amt NOT LIKE '%%-%%' AND num >= %d AND to_who NOT LIKE '%%adjust%%';",
        opt->beg);
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  else if (opt->is_end == TRUE) {
    /* only opt->end */
    snprintf (tmp, SIZE_ARB,
        "SELECT num,cat_num,dtime,amt FROM tran WHERE amt NOT LIKE '%%-%%' AND num <= %d AND to_who NOT LIKE '%%adjust%%';",
        opt->end);
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  else {
    /* now, let's grab the transactions that we need to process */
    snprintf (tmp, SIZE_ARB, "SELECT num,cat_num,dtime,amt FROM tran WHERE amt NOT LIKE '%%-%%' AND to_who NOT LIKE '%%adjust%%';");
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    printf ("\nNothing to consider.\n");
    del_cb_data();
    return 0;
  }
  num_trans = c_head->num_items;
  td = malloc (sizeof (tran_dat));
  if (0 == td) {
    printf ("\n***Error in do_exp(), line %d: Fatal memory error allocating %d bytes.", __LINE__, sizeof (tran_dat));
    del_cb_data();
    return -1;
  }
  for (c = c_head->next; c != c_tail; c = c->next) {
    memset (td, 0, sizeof(tran_dat));
    td->num = atoi (c->item[0]);
    td->cat_num = atoi (c->item[1]);
    strncpy (td->dtime, c->item[2], SIZE_TSTMP);
    strncpy (td->amt, c->item[3], SIZE_AMT);
    cp = bcnum_add (opt->catList[td->cat_num].amt, td->amt, 2);
    strncpy (opt->catList[td->cat_num].amt, cp, SIZE_AMT);
  }
  /* now, print the data */
  strcpy (tot, "0.00");
  printf ("==========================================================================================\n");
  printf ("CATEGORY    DATE/TIME             NAME                                              AMOUNT\n");
  for (i = 0; i < MD_ARY; i++) {
    if (opt->catList[i].cat == 0)
      continue;
    printf ("%-12d%-22s%-40s%16s\n", opt->catList[i].cat, opt->catList[i].dtime, opt->catList[i].name, opt->catList[i].amt);
    cp = bcnum_add (tot, opt->catList[i].amt, 2);
    strncpy (tot, cp, SIZE_AMT);
  }
  printf ("==========================================================================================\n");
  printf ("                                                                   Total: %16s\n", tot);
  printf ("==========================================================================================\n");

  printf ("Posted %d transactions to %d categories\n", num_trans, num_cats);
  return 0;
}

/*
 * do_net
 *
 * This function processes the net command.  This adds up all transactions, excluding the ones with adjust in the to or cmt fields.
 */
static int do_net (void)
{
  char *errmsg = 0;
  c_item *c;
  int ret;
  int i;
  int num_cats;
  int num_trans;
  char tmp[SIZE_ARB+1];
  char *cp;
  tran_dat *td;
  char tot[SIZE_AMT+1];

  /* first, grab everything from the cat table that we need and populate opt->catList */
  snprintf (tmp, SIZE_ARB, "SELECT num,dtime,name,amt FROM cat ORDER BY num;");
  ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    del_cb_data();
    return -1;
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    printf ("\n***Error in do_exp(), line %d: The SQL query '%s' generated no data and it should have.", __LINE__, tmp);
    del_cb_data();
    return -1;
  }
  num_cats = c_head->num_items;
  for (c = c_head->next; c != c_tail; c = c->next) {
    int cat = atoi (c->item[0]);
    if (cat > MD_ARY) {
      printf ("\n***Error in do_exp(), line %d: You have more categories in %s than are allowed (max %d, you have %d)\n", __LINE__, opt->db_name, MD_ARY, cat);
      del_cb_data();
      return -1;
    }
    opt->catList[cat].cat = cat;
    strncpy (opt->catList[cat].dtime, c->item[1], SIZE_TSTMP);
    strncpy (opt->catList[cat].name, c->item[2], FIELD_ARB);
    /* set category amounts to 0 */
    strcpy (opt->catList[cat].amt, "0.00");
  }
  del_cb_data();
  if (opt->is_beg == TRUE && opt->is_end == TRUE) {
    /* process beg and end limits */
    snprintf (tmp, SIZE_ARB,
        "SELECT num,cat_num,dtime,amt FROM tran WHERE num BETWEEN %d AND %d AND to_who NOT LIKE '%%adjust%%';",
        opt->beg, opt->end);
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  else if (opt->is_beg == TRUE) {
    /* only opt->beg */
    snprintf (tmp, SIZE_ARB,
        "SELECT num,cat_num,dtime,amt FROM tran WHERE num >= %d AND to_who NOT LIKE '%%adjust%%';",
        opt->beg);
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  else if (opt->is_end == TRUE) {
    /* only opt->end */
    snprintf (tmp, SIZE_ARB,
        "SELECT num,cat_num,dtime,amt FROM tran WHERE num <= %d AND to_who NOT LIKE '%%adjust%%';",
        opt->end);
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  else {
    /* now, let's grab the transactions that we need to process */
    snprintf (tmp, SIZE_ARB, "SELECT num,cat_num,dtime,amt FROM tran WHERE to_who NOT LIKE '%%adjust%%';");
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    printf ("\nNothing to consider.\n");
    del_cb_data();
    return 0;
  }
  num_trans = c_head->num_items;
  td = malloc (sizeof (tran_dat));
  if (0 == td) {
    printf ("\n***Error in do_exp(), line %d: Fatal memory error allocating %d bytes.", __LINE__, sizeof (tran_dat));
    del_cb_data();
    return -1;
  }
  for (c = c_head->next; c != c_tail; c = c->next) {
    memset (td, 0, sizeof(tran_dat));
    td->num = atoi (c->item[0]);
    td->cat_num = atoi (c->item[1]);
    strncpy (td->dtime, c->item[2], SIZE_TSTMP);
    strncpy (td->amt, c->item[3], SIZE_AMT);
    cp = bcnum_add (opt->catList[td->cat_num].amt, td->amt, 2);
    strncpy (opt->catList[td->cat_num].amt, cp, SIZE_AMT);
  }
  /* now, print the data */
  strcpy (tot, "0.00");
  printf ("==========================================================================================\n");
  printf ("CATEGORY    DATE/TIME             NAME                                              AMOUNT\n");
  for (i = 0; i < MD_ARY; i++) {
    if (opt->catList[i].cat == 0)
      continue;
    printf ("%-12d%-22s%-40s%16s\n", opt->catList[i].cat, opt->catList[i].dtime, opt->catList[i].name, opt->catList[i].amt);
    cp = bcnum_add (tot, opt->catList[i].amt, 2);
    strncpy (tot, cp, SIZE_AMT);
  }
  printf ("==========================================================================================\n");
  printf ("                                                                   Total: %16s\n", tot);
  printf ("==========================================================================================\n");

  printf ("Posted %d transactions to %d categories\n", num_trans, num_cats);
  return 0;
}

/*
 * do_scr
 *
 * This function generates the commands that would be necessary to replicate the contents of the bgt tran table.
 */
static int do_scr (void)
{
  char *errmsg = 0;
  c_item *c;
  int ret;
  char tmp[SIZE_ARB+1];
  int is_bgt = 0;
  char bgt[SIZE_ARB+1];

  if (opt->is_bgt == TRUE) {
    snprintf (bgt, SIZE_ARB, "--bgt %s", opt->bgt);
    is_bgt = TRUE;
  }
  else
    bgt[0] = '\0';

  /* first, grab everything from the cat table that we need and populate opt->catList */
  snprintf (tmp, SIZE_ARB, "SELECT num,dtime,name,comment FROM cat ORDER BY num;");
  ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_scr(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    del_cb_data();
    return -1;
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    printf ("\n***Error in do_exp(), line %d: The SQL query '%s' generated no data and it should have.", __LINE__, tmp);
    del_cb_data();
    return -1;
  }
  for (c = c_head->next; c != c_tail; c = c->next) {
    int cat = atoi (c->item[0]);
    if (cat > MD_ARY) {
      printf ("\n***Error in do_scr(), line %d: You have more categories in %s than are allowed (max %d, you have %d)\n", __LINE__, opt->db_name, MD_ARY, cat);
      del_cb_data();
      return -1;
    }
    printf ("# cat '%d', dtime '%s'\n", cat, c->item[1]);
    if (is_bgt == TRUE)
      printf ("bgt %s --catt '%s' --cmt '%s'\n", bgt, c->item[2], c->item[3]);
    else
      printf ("bgt --catt '%s' --cmt '%s'\n", c->item[2], c->item[3]);
  }
  del_cb_data();
  snprintf (tmp, SIZE_ARB,
      "SELECT t.num,c.name,t.dtime,t.amt,t.status,t.to_who,t.comment FROM tran t,cat c WHERE t.cat_num = c.num ORDER BY t.dtime;");
  ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_scr(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    del_cb_data();
    return -1;
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    del_cb_data();
    return 0;
  }
  for (c = c_head; c != c_tail; c = c->next) {
    if (c->item[0]) {
      printf ("# tran '%s', dtime '%s', status '%s'\n", c->item[0], c->item[2], c->item[4]);
      if (is_bgt == TRUE)
        printf ("bgt %s --add --catt '%s' --date '%s' --amt '%s' --to '%s' --cmt '%s'\n", bgt, c->item[1], c->item[2], c->item[3], c->item[5], c->item[6]);
      else
        printf ("bgt --add --catt '%s' --date '%s' --amt '%s' --to '%s' --cmt '%s'\n", c->item[1], c->item[2], c->item[3], c->item[5], c->item[6]);
    }
  }
  del_cb_data();
  return 0;
}

/*
 * do_arch
 *
 * This function archives everything in the tran table, and then places the minimal transactions into tran to get the balances of the accounts where they
 * were before the archive was called.
 */
static int do_arch (void)
{
  int ret;
  int trnum;
  int i;
  char *errmsg = 0;
  char *cp;
  char tmp[SIZE_ARB+1];
  char tot[SIZE_AMT+1];

  ret = inputline ("Are you sure you want to archive everything? (Enter 'yes' to proceed) >> ");
  if (opt->inputline[0] != 'y' && opt->inputline[0] != 'Y' &&
      opt->inputline[1] != 'e' && opt->inputline[1] != 'Y' &&
      opt->inputline[2] != 's' && opt->inputline[2] != 'S') {
    printf ("Cancelled by user.\n");
    return 0;
  }
  /* OK to proceed - First, do a posting */
  ret = do_post(1);
  if (ret)
    return ret;
  /* Next, archive the transactions. */
  snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nUPDATE tran SET status = 'ARCH';\nCOMMIT;\n");
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_arch(), line %d: SQLite Error executing '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    return -1;
  }
  snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO arch SELECT * FROM tran;\nCOMMIT;\n");
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_arch(), line %d: SQLite Error executing '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    return -1;
  }
  snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nDELETE FROM tran;\nCOMMIT;\n");
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_arch(), line %d: SQLite Error executing '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    return -1;
  }
  /* Now, enter transactions to get balances where they should be. */
  strcpy (tot, "0.00");
  printf ("==========================================================================================\n");
  printf ("CATEGORY    DATE/TIME             NAME                                              AMOUNT\n");
  for (i = 0, trnum = 1; i < MD_ARY; i++) {
    if (opt->catList[i].cat == 0)
      continue;
    printf ("%-12d%-22s%-40s%16s\n", opt->catList[i].cat, opt->catList[i].dtime, opt->catList[i].name, opt->catList[i].amt);
    cp = bcnum_add (tot, opt->catList[i].amt, 2);
    strncpy (tot, cp, SIZE_AMT);
    snprintf (tmp, SIZE_ARB,\
        "BEGIN TRANSACTION;\nINSERT INTO tran VALUES (%d,%d,datetime('now','localtime'),'%s','PSTD','Initial','Initial Balance for account.');\nCOMMIT;\n",
        trnum++, opt->catList[i].cat, opt->catList[i].amt);
    ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_arch(), line %d: SQLite Error executing '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      return -1;
    }
  }
  printf ("==========================================================================================\n");
  printf ("                                                                   Total: %16s\n", tot);
  printf ("==========================================================================================\n");
  /* Finally, indicate the activity that occurred. */
  snprintf (tmp, SIZE_ARB, "BEGIN TRANSACTION;\nINSERT INTO act VALUES ('ARC',NULL,NULL,datetime('now','localtime'),NULL,NULL,'Archived everything.');\nCOMMIT;\n");
  ret = sqlite3_exec (opt->db, tmp, 0, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_arch(), line %d: SQLite Error executing '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    return -1;
  }

  return 0;
}

/*
 * do_csv
 *
 * Generate a CSV output of the data.
 */
static int do_csv (void)
{
  char *errmsg = 0;
  c_item *c;
  int ret;
  int i;
  int cat_max;
  int num_cats;
  int num_trans;
  char tmp[SIZE_ARB+1];

  /* first, grab everything from the cat table that we need and populate opt->catList */
  snprintf (tmp, SIZE_ARB, "SELECT num,dtime,name,amt FROM cat ORDER BY num;");
  ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
  if (ret != SQLITE_OK) {
    printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
    sqlite3_free (errmsg);
    del_cb_data();
    return -1;
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
    printf ("\n***Error in do_exp(), line %d: The SQL query '%s' generated no data and it should have.", __LINE__, tmp);
    del_cb_data();
    return -1;
  }
  num_cats = c_head->num_items;
  cat_max = 0;
  for (c = c_head->next; c != c_tail; c = c->next) {
    int cat = atoi (c->item[0]);
    if (cat > MD_ARY) {
      printf ("\n***Error in do_exp(), line %d: You have more categories in %s than are allowed (max %d, you have %d)\n", __LINE__, opt->db_name, MD_ARY, cat);
      del_cb_data();
      return -1;
    }
    if (cat > cat_max)
      cat_max = cat;
    opt->catList[cat].cat = cat;
    strncpy (opt->catList[cat].dtime, c->item[1], SIZE_TSTMP);
    strncpy (opt->catList[cat].name, c->item[2], FIELD_ARB);
    /* set category amounts to 0 */
    strcpy (opt->catList[cat].amt, "0.00");
  }
  del_cb_data();
  if (opt->is_beg == TRUE && opt->is_end == TRUE) {
    /* process beg and end limits */
    snprintf (tmp, SIZE_ARB,
        "SELECT num,cat_num,dtime,amt,status,to_who,comment FROM tran WHERE num BETWEEN %d AND %d;",
        opt->beg, opt->end);
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  else if (opt->is_beg == TRUE) {
    /* only opt->beg */
    snprintf (tmp, SIZE_ARB,
        "SELECT num,cat_num,dtime,amt,status,to_who,comment FROM tran WHERE num >= %d;",
        opt->beg);
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  else if (opt->is_end == TRUE) {
    /* only opt->end */
    snprintf (tmp, SIZE_ARB,
        "SELECT num,cat_num,dtime,amt,status,to_who,comment FROM tran WHERE num <= %d;",
        opt->end);
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  else {
    /* get them all*/
    snprintf (tmp, SIZE_ARB, "SELECT num,cat_num,dtime,amt,status,to_who,comment FROM tran;");
    ret = sqlite3_exec (opt->db, tmp, cbitem, 0, &errmsg); 
    if (ret != SQLITE_OK) {
      printf ("\n\n***Error in do_exp(), line %d: SQLite Error entering '%s': %s\n", __LINE__, tmp, errmsg);
      sqlite3_free (errmsg);
      del_cb_data();
      return -1;
    }
  }
  if (c_head == 0 || c_tail == 0 || c_head->next == c_tail || c_tail->prev == c_head) {
      printf ("\nNothing to consider.\n");
    del_cb_data();
    return 0;
  }
  num_trans = c_head->num_items;
  if (num_trans == 0) {
    printf ("No transactions to process\n");
    return 0;
  }
  printf ("\"Transaction\",\"Date/time\",\"To field\",\"Comment\",\"Amount\"");
  for (i = 0; i < MD_ARY; i++) {
    if (opt->catList[i].cat == 0)
      continue;
    printf (",\"%s\"", opt->catList[i].name);
  }
  printf ("\n");

  for (c = c_head->next; c != c_tail; c = c->next) {
    int c_num = atoi(c->item[1]);
    printf ("\"%s\",\"%s\",\"%s\",\"%s\",%s", c->item[0], c->item[2], c->item[5], c->item[6], c->item[3]);
    for (i = 0; i < MD_ARY; i++) {
      if (opt->catList[i].cat == 0)
        continue;
      if (c_num == opt->catList[i].cat)
        printf (",%s", c->item[3]);
      else
        printf (",");
      /* continue here */
    }
    printf ("\n");
  }
  return 0;
}

static int do_qif (void)
{
  int ret;
  FILE *fp;
  FILE *outfp;
  char *data, *cp;
  qifItem *qi;
  int i;
  int lnctr = 0;
  static char *rqda[QIF_SIZE_RQDA];

  if (! fexists (opt->qif)) {
    printf ("***Error in do_qif(), line %d: The qif file '%s' does not exist.\n", __LINE__, opt->qif);
    return -1;
  }
  // The outfp is there so you could specify a file to output to on the command-line, if you wanted to.
  outfp = stdout;
  for (i = 0; i < QIF_SIZE_RQDA; i++) {
    rqda[i] = malloc (QIF_SIZE_DESC+1);
    if (0 == rqda[i]) {
      printf ("***Error in do_qif(), line %d: fatal allocation error allocating %d bytes.\n", __LINE__, QIF_SIZE_DESC+1);
      return -1;
    }
    memset (rqda[i], 0, QIF_SIZE_DESC+1);
  }
  data = malloc (DESCR_ARB+1);
  if (0 == data) {
    printf ("***Error in do_qif(), line %d: fatal allocation error allocting %d bytes.\n", __LINE__, DESCR_ARB+1);
    for (i = 0; i < QIF_SIZE_RQDA; i++) {
      free (rqda[i]);
    }
    return -1;
  }
  memset (data, 0, DESCR_ARB+1);
  fp = fopen (opt->qif, "rb");
  if (0 == fp) {
    printf ("***Error in do_qif(), line %d: Couldn't open %s\n", __LINE__, opt->qif);
    for (i = 0; i < QIF_SIZE_RQDA; i++) {
      free (rqda[i]);
    }
    return -1;
  }
  while (TRUE) {
    cp = fgets (data, DESCR_ARB, fp);
    if (feof (fp))
      break;
    if (cp == 0)
      break;
    lnctr++;
    if (data[0] == '\0' || data[0] == '\n' || data[0] == '\r')
      continue;

    cp = strchr (data, '\n');
    if (0 != cp)
      *cp = '\0';
    cp = strchr (data, '\r');
    if (0 != cp)
      *cp = '\0';

    if (data[0] != '!') {
      printf ("***Error in do_qif(), line %d: file \"%s\", line %d: invalid qif file\n", __LINE__, opt->qif, lnctr);
      fclose (fp);
      for (i = 0; i < QIF_SIZE_RQDA; i++) {
        free (rqda[i]);
      }
      return -1;
    }
    /* Should be: !Type:XXXX */
    cp = strchr (data, ':');
    if (cp != 0 && strncmp (cp+1, "Bank", 4)) {
      /* skip through anything but Bank */
      while (TRUE) {
        lnctr++;
        cp = fgets (data, DESCR_ARB+1, fp);
        if (feof (fp))
          goto CloseHerOut;
        if (cp == 0)
          goto CloseHerOut;
        if (data[0] == '\0')
          break;
        if (data[0] == '!') {
          cp = strchr (data, ':');
          if (cp != 0)
            break;
          else
            continue;
        }
      }
      cp = strchr (data, ':');
    }
    else {
      /* OK, here's the meat of the processing */
      while (TRUE) {
        int isFinished;
        isFinished = FALSE;
        for (i = 0; i < QIF_SIZE_RQDA; i++) {
          lnctr++;
          cp = fgets (data, DESCR_ARB+1, fp);
          if (feof (fp))
            goto CloseHerOut;
          if (cp == 0)
            goto CloseHerOut;
          if (data[0] == '\0')
            break;
          cp = strchr (data, '\n');
          if (0 != cp)
            *cp = '\0';
          cp = strchr (data, '\r');
          if (0 != cp)
            *cp = '\0';
          if (data[0] == '\0')
            continue;

          if (data[0] != '^') {
            snprintf (rqda[i], QIF_SIZE_DESC, "%s", data);
            rqda[i+1][0] = '\0';
          }
          else {
            isFinished = TRUE;
            break;
          }
        }
        if (isFinished == FALSE) {
          printf ("***Error in do_qif(), line %d: file \"%s\", line %d: QIF_SIZE_RQDA (%d) is not big enough\n",
              __LINE__, opt->qif, lnctr, QIF_SIZE_RQDA);
          for (i = 0; i < QIF_SIZE_RQDA; i++) {
            free (rqda[i]);
          }
          return -1;
        }
        /*
         * OK, parse the data in rqda and return a qifItem.  Then, print out the 
         * data in the qifItem per the needs of the application.  We need to
         * free the memory that the qifItem is using.
         */
        qi = parseQIFItem (&rqda[0], opt->qif, lnctr);
        if (qi == 0) {
          fprintf (stdout, "***Warning in do_qif(), line %d: qif item invalid around line %d\n", __LINE__, lnctr);
          continue;
        }
        else {
          if (strcmp (qi->category, "[Checking]") == 0) {
            free (qi);
            continue;
          }
          /* split item */
          if (qi->isSplit) {
            /* print it out as separate line items */
            int i;
            for (i = 0; i < 30 && qi->sia[i] != 0; i++) {
              remove_chr (qi->memo, '\'');
              remove_chr (qi->memo, '&');
              remove_chr (qi->payee, '\'');
              remove_chr (qi->payee, '&');
              ret = get_cat_num_from_name (qi->sia[i]->split_cat);
              if (ret == -1) {
                printf ("***Warning in do_qif(), line %d: qif category '%s' invalid around line %d\n", __LINE__, qi->sia[i]->split_cat, lnctr);
                continue;
              }
              if (opt->is_bgt) {
                printf ("bgt --bgt %s --add --amt %s --catt '%s' --to '%s' --cmt '%s:%s:%s-%d'\n",
                    opt->bgt, qi->sia[i]->amt, qi->sia[i]->split_cat, qi->payee, qi->date, qi->check, qi->memo, i);
              }
              else {
                printf ("bgt --add --amt %s --catt '%s' --to '%s' --cmt '%s:%s:%s-%d'\n",
                    qi->sia[i]->amt, qi->sia[i]->split_cat, qi->payee, qi->date, qi->check, qi->memo, i);
              }
            }
          }
          /* non-split item */
          else {
            remove_chr (qi->memo, '\'');
            remove_chr (qi->memo, '&');
            remove_chr (qi->payee, '\'');
            remove_chr (qi->payee, '&');
            ret = get_cat_num_from_name (qi->category);
            if (ret == -1) {
              printf ("***Warning in do_qif(), line %d: qif category '%s' invalid around line %d\n", __LINE__, qi->category, lnctr);
              continue;
            }
            if (opt->is_bgt) {
              printf ("bgt --bgt %s --add --amt %s --catt '%s' --to '%s' --cmt '%s:%s:%s'\n",
                  opt->bgt, qi->tran, qi->category, qi->payee, qi->date, qi->check, qi->memo);
            }
            else {
              printf ("bgt --add --amt %s --catt '%s' --to '%s' --cmt '%s:%s:%s'\n",
                  qi->tran, qi->category, qi->payee, qi->date, qi->check, qi->memo);
            }
          }
        }
        free (qi);
      }
    }
    /* should be done... */
  }
CloseHerOut:
  fclose (fp);
  free (data);
  for (i = 0; i < QIF_SIZE_RQDA; i++) {
    free (rqda[i]);
  }
  return 0;
}


/******************************************
 * main function.
 * ****************************************/
int main (int argc, char *argv[])
{
  int ch;
  int ret = 0;
  int status;
  int option_index = 0;
  struct option long_options[] = {
    {"bgt",        1, 0, 'b'},
    {"cat",        1, 0, 'c'},
    {"catt",       1, 0, 'C'},
    {"tran",       1, 0, 't'},
    {"ls",         0, 0, 'l'},
    {"add",        0, 0, 'a'},
    {"to",         1, 0, 'T'},
    {"amt",        1, 0, 'A'},
    {"cmt",        1, 0, 'm'},
    {"cedit",      0, 0, 'd'},
    {"crm",        0, 0, 'G'},
    {"edit",       0, 0, 'e'},
    {"rm",         0, 0, 'r'},
    {"split",      0, 0, 's'},
    {"adj",        0, 0, 'j'},
    {"src_cat",    1, 0, 'R'},
    {"dst_cat",    1, 0, 'S'},
    {"qry",        1, 0, 'q'},
    {"recalc",     0, 0, 'L'},
    {"arch",       0, 0, 'H'},
    {"pr",         0, 0, 'p'},
    {"exp",        0, 0, 'x'},
    {"inc",        0, 0, 'N'},
    {"net",        0, 0, 'n'},
    {"beg",        1, 0, 'B'},
    {"end",        1, 0, 'E'},
    {"scr",        0, 0, 'o'},
    {"csv",        0, 0, 'v'},
    {"date",       1, 0, 'D'},
    {"nclr",       1, 0, 'P'},
    {"tot",        0, 0, 'O'},
    {"qif",        1, 0, 'Q'},
    {"help",       0, 0, 'h'},
    {0,0,0,0}
  };

  opt = malloc (sizeof (optObject));
  if (opt == 0) {
    printf ("\n\n***Error in main(), line %d: fatal memory error allocating an option object.\n", __LINE__);
    ret = -1;
    goto CleanupAndQuit;
  }
  memset (opt, 0, sizeof(optObject));
  opterr = 1; /* tell getopt() to hush */
  while ( (ch = getopt_long (argc, argv, "b:c:C:t:laT:A:m:dGersjR:S:q:LHpxNnB:E:ovDPOQ:h", long_options, &option_index)) != EOF) {
    switch (ch) {
      case 'b': /* --bgt */
        opt->is_bgt = TRUE;
        strncpy (opt->bgt, optarg, DESCR_ARB);
        break;
      case 'c': /* --cat */
        opt->is_cat = TRUE;
        opt->cat = atoi (optarg);
        break;
      case 'C': /* --catt */
        opt->is_catt = TRUE;
        strncpy (opt->catt, optarg, FIELD_ARB);
        break;
      case 't': /* --tran */
        opt->is_tran = TRUE;
        opt->tran = atoi (optarg);
        break;
      case 'l': /* --ls */
        opt->is_ls = TRUE;
        break;
      case 'a': /* --add */
        opt->is_add = TRUE;
        break;
      case 'T': /* --to */
        opt->is_to = TRUE;
        strncpy (opt->to, optarg, FIELD_ARB);
        break;
      case 'A': /* --amt */
        opt->is_amt = TRUE;
        strncpy (opt->amt, optarg, SIZE_AMT);
        break;
      case 'm': /* --cmt */
        opt->is_cmt = TRUE;
        strncpy (opt->cmt, optarg, FIELD_ARB);
        break;
      case 'd': /* --cedit */
        opt->is_cedit = TRUE;
        break;
      case 'G': /* --crm */
        opt->is_crm = TRUE;
        strncpy (opt->crm, optarg, FIELD_ARB);
        break;
      case 'e': /* --edit */
        opt->is_edit = TRUE;
        break;
      case 'r': /* --rm */
        opt->is_rm = TRUE;
        break;
      case 's': /* --split */
        opt->is_split = TRUE;
        break;
      case 'j': /* --adj */
        opt->is_adj = TRUE;
        break;
      case 'R': /* --src_cat */
        opt->is_src_cat = TRUE;
        opt->src_cat = atoi (optarg);
        break;
      case 'S': /* --dst_cat */
        opt->is_dst_cat = TRUE;
        opt->dst_cat = atoi (optarg);
        break;
      case 'q': /* --qry */
        opt->is_qry = TRUE;
        strncpy (opt->qry, optarg, DESCR_ARB);
        break;
      case 'L': /* --recalc */
        opt->is_recalc = TRUE;
        break;
      case 'H': /* --arch */
        opt->is_arch = TRUE;
        break;
      case 'p': /* --pr */
        opt->is_pr = TRUE;
        break;
      case 'x': /* --exp */
        opt->is_exp = TRUE;
        break;
      case 'N': /* --inc */
        opt->is_inc = TRUE;
        break;
      case 'n': /* --net */
        opt->is_net = TRUE;
        break;
      case 'B': /* --beg */
        opt->is_beg = TRUE;
        opt->beg = atoi (optarg);
        break;
      case 'E': /* --end */
        opt->is_end = TRUE;
        opt->end = atoi (optarg);
        break;
      case 'o': /* --scr */
        opt->is_scr = TRUE;
        break;
      case 'v': /* --csv */
        opt->is_csv = TRUE;
        break;
      case 'P': /* --nclr */
        opt->is_nclr = TRUE;
        strncpy (opt->nclr, optarg, SIZE_ARB);
        break;
      case 'D': /* --date */
        opt->is_date = TRUE;
        strncpy (opt->date, optarg, FIELD_ARB);
        {
          char *cp = optarg;
          int len = strlen (optarg);
          if (len > SIZE_TSTMP) {
            printf ("\n***Warning in main(), line %d: invalid date '%s' in command-line option: too long\n", __LINE__, optarg);
            opt->is_date = FALSE;
            opt->date[0] = '\0';
            break;
          }
          len = 0;
          while (cp[len] != '\0' && len < SIZE_TSTMP) {
            if (! isdigit (cp[len]) && cp[len] != '-' && cp[len] != ' ' && cp[len] != ':') {
              printf ("\n***Warning in main(), line %d: invalid date '%s' in command-line option: too long\n", __LINE__, optarg);
              opt->is_date = FALSE;
              opt->date[0] = '\0';
              break;
            }
            len++;
          }
        }
        break;
      case 'O': /* --tot */
        opt->is_tot = TRUE;
        opt->is_quiet = TRUE;
        break;
      case 'Q': /* --qif */
        opt->is_qif = TRUE;
        strncpy (opt->qif, optarg, SIZE_ARB);
        break;
      case 'h': /* --help */
        usage();
        ret = 0;
        goto CleanupAndQuit;
      default:
        if (ch == '?') {
          printf ("\n\n***Error in main(), line %d: incorrect command-line option\n", __LINE__);
          usage();
          ret = -1;
          goto CleanupAndQuit;
        }
        else {
          printf ("\n***Error in main(), line %d: unknown command-line error: ch = %c", __LINE__, ch);
          usage();
          ret = -1;
          goto CleanupAndQuit;
        }
    }
  }

  strncpy (opt->home_dir, getenv ("HOME"), PATH_MAX-1);
  if (strlen (opt->home_dir) == 0) {
    printf ("\n***Error in main(), line %d: Could not get the HOME environment variable\n", __LINE__);
    ret = -1;
    goto CleanupAndQuit;
  }
  if (opt->is_bgt == FALSE) {
    char *cp = getenv ("BGTHOME");
    if (cp != 0) {
      strncpy (opt->bgt, getenv("BGTHOME"), PATH_MAX-1);
      if (strlen (opt->bgt) == 0) {
        snprintf (opt->bgt, PATH_MAX-1, "%s/.bgt", opt->home_dir);
      }
    }
    else 
      snprintf (opt->bgt, PATH_MAX-1, "%s/.bgt", opt->home_dir);
  }
  snprintf (opt->db_name, PATH_MAX-1, "%s/%s", opt->bgt, "bgt.db");
  if (! fexists (opt->bgt)) {
    status = mkdir (opt->bgt, 0755);
    if (status != 0) {
      fprintf (stderr, "\n***Error in main(), line %d: Could not mkdir(%s, 0755)\n", __LINE__, opt->bgt);
      perror ("mkdir()");
      ret = -1;
      goto CleanupAndQuit;
    }
    ret = do_initialize();
    if (ret)
      goto CleanupAndQuit;
  }
  if (! fexists (opt->db_name)) {
    ret = do_initialize();
    if (ret)
      goto CleanupAndQuit;
  }
  else {
    ret = sqlite3_open (opt->db_name, &opt->db);
    if (ret) {
      printf ("\n***Error in main(), line %d: Can't open SQLite database file %s: %s\n", __LINE__, opt->db_name, sqlite3_errmsg(opt->db));
      goto CleanupAndQuit;
    }
  }

  if (opt->is_catt && ! opt->is_add && ! opt->is_ls && ! opt->is_tot && ! opt->is_edit) {
    ret = do_new_cat ();
    goto CleanupAndQuit;
  }

  if (opt->is_add) {
    ret = do_add ();
    goto CleanupAndQuit;
  }

  if (opt->is_ls) {
    ret = do_ls();
    goto CleanupAndQuit;
  }

  if (opt->is_recalc) {
    ret = do_recalc ();
    goto CleanupAndQuit;
  }

  if (opt->is_qry) {
    ret = do_qry ();
    goto CleanupAndQuit;
  }

  if (opt->is_edit) {
    ret = do_edit();
    goto CleanupAndQuit;
  }

  if (opt->is_rm) {
    ret = do_rm ();
    goto CleanupAndQuit;
  }

  if (opt->is_exp) {
    ret = do_exp();
    goto CleanupAndQuit;
  }

  if (opt->is_inc) {
    ret = do_inc();
    goto CleanupAndQuit;
  }

  if (opt->is_net) {
    ret = do_net();
    goto CleanupAndQuit;
  }

  if (opt->is_scr) {
    ret = do_scr();
    goto CleanupAndQuit;
  }

  if (opt->is_arch) {
    ret = do_arch();
    goto CleanupAndQuit;
  }

  if (opt->is_csv) {
    ret = do_csv();
    goto CleanupAndQuit;
  }

  if (opt->is_nclr) {
    if (!opt->is_ls) {
      printf ("\n***Error, main(), line %d: --nclr should be used with --ls.\n", __LINE__);
      ret = -1;
      goto CleanupAndQuit;
    }
  }

  if (opt->is_qif) {
    ret = do_qif();
    goto CleanupAndQuit;
  }

CleanupAndQuit:

  if (opt->db != 0)
    sqlite3_close(opt->db);
  free (opt);
  return ret;
}
