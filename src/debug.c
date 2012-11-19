/*
    debug.c: log (or not) messages
    Copyright (C) 2003-2005   Ludovic Rousseau

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public License
	along with this library; if not, write to the Free Software Foundation,
	Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

/*
 * $Id: debug.c 2974 2008-05-28 18:32:52Z rousseau $
 */


#include "config.h"
#include "debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DEBUG_BUF_SIZE ((256+20)*3+10)

static char DebugBuffer[DEBUG_BUF_SIZE];

#define LOG_TO_STDERR

void log_msg(const int priority, const char *fmt, ...)
{
	va_list argptr;

	va_start(argptr, fmt);
	vsnprintf(DebugBuffer, DEBUG_BUF_SIZE, fmt, argptr);
	va_end(argptr);

#ifdef LOG_TO_STDERR
	fprintf(stderr, "%s\n", DebugBuffer);
#endif
} /* log_msg */

void log_xxd(const int priority, const char *msg, const unsigned char *buffer,
	const int len)
{
	int i;
	char *c, *debug_buf_end;

	debug_buf_end = DebugBuffer + DEBUG_BUF_SIZE - 5;

	strncpy(DebugBuffer, msg, sizeof(DebugBuffer)-1);
	c = DebugBuffer + strlen(DebugBuffer);

	for (i = 0; (i < len) && (c < debug_buf_end); ++i)
	{
		sprintf(c, "%02X ", (unsigned char)buffer[i]);
		c += strlen(c);
	}

#ifdef LOG_TO_STDERR
	if (c >= debug_buf_end)
		fprintf(stderr, "Debug buffer too short\n");

	fprintf(stderr, "%s\n", DebugBuffer);
#endif
} /* log_xxd */

