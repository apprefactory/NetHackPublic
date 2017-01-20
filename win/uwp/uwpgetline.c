/* NetHack 3.6	getline.c	$NHDT-Date: 1432512813 2015/05/25 00:13:33 $  $NHDT-Branch: master $:$NHDT-Revision: 1.28 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

#ifdef UWP_GRAPHICS

#if !defined(MAC)
#define NEWAUTOCOMP
#endif

#include "winuwp.h"
#include "func_tab.h"

char uwp_morc = 0; /* tell the outside world what char you chose */
STATIC_DCL boolean FDECL(uwp_ext_cmd_getlin_hook, (char *));

typedef boolean FDECL((*getlin_hook_proc), (char *));

STATIC_DCL void FDECL(hooked_uwp_getlin,
                      (const char *, char *, getlin_hook_proc));

extern int NDECL(extcmd_via_menu); /* cmd.c */

//TODO: we should not need these
extern char erase_char, kill_char; /* from appropriate tty.c file */

/*
 * Read a line closed with '\n' into the array char bufp[BUFSZ].
 * (The '\n' is not stored. The string is closed with a '\0'.)
 * Reading can be interrupted by an escape ('\033') - now the
 * resulting string is "\033".
 */
void
uwp_getlin(query, bufp)
const char *query;
register char *bufp;
{
    hooked_uwp_getlin(query, bufp, (getlin_hook_proc) 0);
}

STATIC_OVL void
hooked_uwp_getlin(query, bufp, hook)
const char *query;
register char *bufp;
getlin_hook_proc hook;
{
    register char *obufp = bufp;
    register int c;
    struct UwpWinDesc *cw = uwp_wins[WIN_MESSAGE];
    boolean doprev = 0;

    if (uwpDisplay->toplin == 1 && !(cw->flags & UWP_WIN_STOP))
        uwp_more();
    cw->flags &= ~UWP_WIN_STOP;
    uwpDisplay->toplin = 3; /* special prompt state */
    uwpDisplay->inread++;
    pline("%s ", query);
    *obufp = 0;
    for (;;) {
        (void) fflush(stdout);
        Strcat(strcat(strcpy(toplines, query), " "), obufp);
        c = pgetchar();
        if (c == '\033' || c == EOF) {
            if (c == '\033' && obufp[0] != '\0') {
                obufp[0] = '\0';
                bufp = obufp;
                uwp_clear_nhwindow(WIN_MESSAGE);
                cw->maxcol = cw->maxrow;
                uwp_addtopl(query);
                uwp_addtopl(" ");
                uwp_addtopl(obufp);
            } else {
                obufp[0] = '\033';
                obufp[1] = '\0';
                break;
            }
        }
        if (uwpDisplay->intr) {
            uwpDisplay->intr--;
            *bufp = 0;
        }
        if (c == '\020') { /* ctrl-P */
            if (iflags.prevmsg_window != 's') {
                int sav = uwpDisplay->inread;
                uwpDisplay->inread = 0;
                (void) uwp_doprev_message();
                uwpDisplay->inread = sav;
                uwp_clear_nhwindow(WIN_MESSAGE);
                cw->maxcol = cw->maxrow;
                uwp_addtopl(query);
                uwp_addtopl(" ");
                *bufp = 0;
                uwp_addtopl(obufp);
            } else {
                if (!doprev)
                    (void) uwp_doprev_message(); /* need two initially */
                (void) uwp_doprev_message();
                doprev = 1;
                continue;
            }
        } else if (doprev && iflags.prevmsg_window == 's') {
            uwp_clear_nhwindow(WIN_MESSAGE);
            cw->maxcol = cw->maxrow;
            doprev = 0;
            uwp_addtopl(query);
            uwp_addtopl(" ");
            *bufp = 0;
            uwp_addtopl(obufp);
        }
        if (c == erase_char || c == '\b') {
            if (bufp != obufp) {
#ifdef NEWAUTOCOMP
                char *i;

#endif /* NEWAUTOCOMP */
                bufp--;
#ifndef NEWAUTOCOMP
                uwp_putsyms("\b \b"); /* putsym converts \b */
#else                             /* NEWAUTOCOMP */
                uwp_putsyms("\b");
                for (i = bufp; *i; ++i)
                    uwp_putsyms(" ");
                for (; i > bufp; --i)
                    uwp_putsyms("\b");
                *bufp = 0;
#endif                            /* NEWAUTOCOMP */
            } else
                uwp_nhbell();
#if defined(apollo)
        } else if (c == '\n' || c == '\r') {
#else
        } else if (c == '\n') {
#endif
#ifndef NEWAUTOCOMP
            *bufp = 0;
#endif /* not NEWAUTOCOMP */
            break;
        } else if (' ' <= (unsigned char) c && c != '\177'
                   && (bufp - obufp < BUFSZ - 1 && bufp - obufp < COLNO)) {
/* avoid isprint() - some people don't have it
   ' ' is not always a printing char */
#ifdef NEWAUTOCOMP
            char *i = eos(bufp);

#endif /* NEWAUTOCOMP */
            *bufp = c;
            bufp[1] = 0;
            uwp_putsyms(bufp);
            bufp++;
            if (hook && (*hook)(obufp)) {
                uwp_putsyms(bufp);
#ifndef NEWAUTOCOMP
                bufp = eos(bufp);
#else  /* NEWAUTOCOMP */
                /* pointer and cursor left where they were */
                for (i = bufp; *i; ++i)
                    uwp_putsyms("\b");
            } else if (i > bufp) {
                char *s = i;

                /* erase rest of prior guess */
                for (; i > bufp; --i)
                    uwp_putsyms(" ");
                for (; s > bufp; --s)
                    uwp_putsyms("\b");
#endif /* NEWAUTOCOMP */
            }
        } else if (c == kill_char || c == '\177') { /* Robert Viduya */
/* this test last - @ might be the kill_char */
#ifndef NEWAUTOCOMP
            while (bufp != obufp) {
                bufp--;
                uwp_putsyms("\b \b");
            }
#else  /* NEWAUTOCOMP */
            for (; *bufp; ++bufp)
                uwp_putsyms(" ");
            for (; bufp != obufp; --bufp)
                uwp_putsyms("\b \b");
            *bufp = 0;
#endif /* NEWAUTOCOMP */
        } else
            uwp_nhbell();
    }
    uwpDisplay->toplin = 2; /* nonempty, no --More-- required */
    uwpDisplay->inread--;
    clear_nhwindow(WIN_MESSAGE); /* clean up after ourselves */
}

void
uwp_xwaitforspace(s)
register const char *s; /* chars allowed besides return */
{
    register int c, x = uwpDisplay ? (int) uwpDisplay->dismiss_more : '\n';

    uwp_morc = 0;
    while (
#ifdef HANGUPHANDLING
        !program_state.done_hup &&
#endif
        (c = uwp_nhgetch()) != EOF) {
        if (c == '\n')
            break;

        if (iflags.cbreak) {
            if (c == '\033') {
                if (uwpDisplay)
                    uwpDisplay->dismiss_more = 1;
                uwp_morc = '\033';
                break;
            }
            if ((s && index(s, c)) || c == x) {
                uwp_morc = (char) c;
                break;
            }
            uwp_nhbell();
        }
    }
}

/*
 * Implement extended command completion by using this hook into
 * uwp_getlin.  Check the characters already typed, if they uniquely
 * identify an extended command, expand the string to the whole
 * command.
 *
 * Return TRUE if we've extended the string at base.  Otherwise return FALSE.
 * Assumptions:
 *
 *	+ we don't change the characters that are already in base
 *	+ base has enough room to hold our string
 */
STATIC_OVL boolean
uwp_ext_cmd_getlin_hook(base)
char *base;
{
    int oindex, com_index;

    com_index = -1;
    for (oindex = 0; extcmdlist[oindex].ef_txt != (char *) 0; oindex++) {
        if ((extcmdlist[oindex].flags & AUTOCOMPLETE)
            && !(!wizard && (extcmdlist[oindex].flags & WIZMODECMD))
            && !strncmpi(base, extcmdlist[oindex].ef_txt, strlen(base))) {
            if (com_index == -1) /* no matches yet */
                com_index = oindex;
            else /* more than 1 match */
                return FALSE;
        }
    }
    if (com_index >= 0) {
        Strcpy(base, extcmdlist[com_index].ef_txt);
        return TRUE;
    }

    return FALSE; /* didn't match anything */
}

/*
 * Read in an extended command, doing command line completion.  We
 * stop when we have found enough characters to make a unique command.
 */
int
uwp_get_ext_cmd()
{
    int i;
    char buf[BUFSZ];

    if (iflags.extmenu)
        return extcmd_via_menu();
    /* maybe a runtime option? */
    /* hooked_tty_getlin("#", buf, flags.cmd_comp ? ext_cmd_getlin_hook :
     * (getlin_hook_proc) 0); */
    hooked_uwp_getlin("#", buf, in_doagain ? (getlin_hook_proc) 0
                                           : uwp_ext_cmd_getlin_hook);
    (void) mungspaces(buf);
    if (buf[0] == 0 || buf[0] == '\033')
        return -1;

    for (i = 0; extcmdlist[i].ef_txt != (char *) 0; i++)
        if (!strcmpi(buf, extcmdlist[i].ef_txt))
            break;

    if (!in_doagain) {
        int j;
        for (j = 0; buf[j]; j++)
            savech(buf[j]);
        savech('\n');
    }

    if (extcmdlist[i].ef_txt == (char *) 0) {
        pline("%s: unknown extended command.", buf);
        i = -1;
    }

    return i;
}

#endif /* UWP_GRAPHICS */