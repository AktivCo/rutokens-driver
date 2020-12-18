/*
    debug.c: log (or not) messages
    Copyright (C) 2012 Aktiv Co
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


#include "config.h"
#include "debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef __APPLE__
#include <syslog.h>
#endif // __APPLE__

#define DEBUG_BUF_SIZE ((256+20)*3+10)

static char DebugBuffer[DEBUG_BUF_SIZE];

#ifdef __APPLE__
int translate_pcsc_to_syslog(int priority) {
	switch(priority)
	{
		case PCSC_LOG_CRITICAL:
			return LOG_CRIT;
		case PCSC_LOG_ERROR:
			return LOG_ERR;
		case PCSC_LOG_INFO:
			return LOG_INFO;
		case PCSC_LOG_DEBUG:
		default:
			return LOG_DEBUG;
	}
}
#endif // __APPLE__

void log_msg(const int priority, const char *fmt, ...)
{
	va_list argptr;

	va_start(argptr, fmt);
	vsnprintf(DebugBuffer, DEBUG_BUF_SIZE, fmt, argptr);

#ifdef __APPLE__
	syslog(translate_pcsc_to_syslog(priority), "%s\n", DebugBuffer);
#else // __APPLE__
	fprintf(stderr, "%s\n", DebugBuffer);
#endif // __APPLE__

	va_end(argptr);
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

#ifdef __APPLE__
	syslog(translate_pcsc_to_syslog(priority), "%s\n", DebugBuffer);
#else // __APPLE__
	if (c >= debug_buf_end)
		fprintf(stderr, "Debug buffer too short\n");

	fprintf(stderr, "%s\n", DebugBuffer);
#endif // __APPLE__
} /* log_xxd */
