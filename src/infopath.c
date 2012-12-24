/*
    infopath.c: Functions to determine Info.plist path
    Copyright (C) 2012 Aktiv Co.

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

/* We use dladdr to determine module path*/
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "config.h"
#include "debug.h"

/*****************************************************************************
 *
 *					library_path
 *
 ****************************************************************************/
int library_path(char path[])
{
	size_t dli_fname_len;
	Dl_info dl_info;

	if(path == 0)
		return -1;

	if(dladdr((int *)library_path, &dl_info) == 0)
		return -1;

	dli_fname_len = strlen(dl_info.dli_fname);

	if(dli_fname_len > FILENAME_MAX)
		return -1;
	else
		strcpy(path, dl_info.dli_fname);

	return 0;
} /* library_path */

/*****************************************************************************
 *
 *					infoFileName
 *
 ****************************************************************************/
void infoFileName(char infofile[])
{
	char libraryPath[FILENAME_MAX];

	/* Library full path filename */
	if(library_path(libraryPath) != 0)
	{
		DEBUG_INFO2("Can't find library path, use default path to Info.plist", LogLevel);

		/* Info.plist full path filename */
		snprintf(infofile, FILENAME_MAX, "%s/%s/Contents/Info.plist",
			PCSCLITE_HP_DROPDIR, BUNDLE);
	}
	else
	{
		/* libraryPath ends up with librutokens.so */
		/* Remove filename and cd .. */
		int i = 0;
		for(; i < 2; ++i)
		{
			char* fName = 0;
			fName = strrchr(libraryPath, '/');
			if (fName != 0)
				*fName = 0;
		}

		/* Info.plist full path filename */
		snprintf(infofile, FILENAME_MAX, "%s/Info.plist", libraryPath);
	}
} /* infoFileName */
