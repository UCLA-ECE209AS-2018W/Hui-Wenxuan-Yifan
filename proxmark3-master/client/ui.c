//-----------------------------------------------------------------------------
// Copyright (C) 2009 Michael Gernoth <michael at gernoth.net>
// Copyright (C) 2010 iZsh <izsh at fail0verflow.com>
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// UI utilities
//-----------------------------------------------------------------------------

#include <stdbool.h>
#ifndef EXTERNAL_PRINTANDLOG
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <readline/readline.h>
#include <pthread.h>
#endif

#include "ui.h"

double CursorScaleFactor = 1;
int PlotGridX=0, PlotGridY=0, PlotGridXdefault= 64, PlotGridYdefault= 64, CursorCPos= 0, CursorDPos= 0;
int offline;
int flushAfterWrite = 0;  //buzzy
int GridOffset = 0;
bool GridLocked = false;
bool showDemod = true;

static char *logfilename = "proxmark3.log";

#ifndef EXTERNAL_PRINTANDLOG
// Declared in proxmark3.c
extern pthread_mutex_t print_lock;

void PrintAndLog(char *fmt, ...)
{
	char *saved_line;
	int saved_point;
	va_list argptr, argptr2;
	static FILE *logfile = NULL;
	static int logging=1;

	// lock this section to avoid interlacing prints from different threads
	pthread_mutex_lock(&print_lock);
  
	if (logging && !logfile) {
		logfile=fopen(logfilename, "a");
		if (!logfile) {
			fprintf(stderr, "Can't open logfile, logging disabled!\n");
			logging=0;
		}
	}

#ifdef RL_STATE_READCMD
	// We are using GNU readline.
	int need_hack = (rl_readline_state & RL_STATE_READCMD) > 0;

	if (need_hack) {
		saved_point = rl_point;
		saved_line = rl_copy_text(0, rl_end);
		rl_save_prompt();
		rl_replace_line("", 0);
		rl_redisplay();
	}
#else
	// We are using libedit (OSX), which doesn't support this flag.
	int need_hack = 0;
#endif
	
	va_start(argptr, fmt);
	va_copy(argptr2, argptr);
	vprintf(fmt, argptr);
	printf("          "); // cleaning prompt
	va_end(argptr);
	printf("\n");

	if (need_hack) {
		rl_restore_prompt();
		rl_replace_line(saved_line, 0);
		rl_point = saved_point;
		rl_redisplay();
		free(saved_line);
	}
	
	if (logging && logfile) {
		vfprintf(logfile, fmt, argptr2);
		fprintf(logfile,"\n");
		fflush(logfile);
	}
	va_end(argptr2);

	if (flushAfterWrite == 1)  //buzzy
	{
		fflush(NULL);
	}
	//release lock
	pthread_mutex_unlock(&print_lock);  
}
#endif

void SetLogFilename(char *fn)
{
  logfilename = fn;
}
