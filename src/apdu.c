/*
    apdu.c: APDU handling
    Copyright (C) 2012 Aktiv Co
    Copyright (C) 2003, Olaf Kirch <okir@suse.de>

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


#include <stdlib.h>
#include <string.h>
#include "apdu.h"
/*
 * Check the APDU type and length
 */

static int __ifd_apdu_check(const void *sbuf, size_t len, ifd_iso_apdu_t * iso)
{
	unsigned char *data = (unsigned char *)sbuf;
	unsigned int b;

	memset(iso, 0, sizeof(*iso));
	if (len < 5) {
		iso->cse = IFD_APDU_CASE_1;
		return 0;
	}

	b = data[4];
	len -= 5;

	/* APDU + Le */
	if (len == 0) {
		iso->cse = IFD_APDU_CASE_2S;
		iso->le = b ? b : 256;
		return 0;
	}

	data += 5;
	if (b == 0)
		b = 256;

	iso->lc = b;
	iso->len = len;
	iso->data = data;

	/* APDU + Lc + data */
	if (len == b) {
		iso->cse = IFD_APDU_CASE_3S;
		return 0;
	}

	/* APDU + Lc + data + Le */
	if (len == b + 1) {
		iso->cse = IFD_APDU_CASE_4S;
		iso->le = data[b] ? data[b] : 256;
		iso->len--;
		return 0;
	}

	return -1;
}

int ifd_apdu_case(const void *buf, size_t len)
{
	ifd_iso_apdu_t iso;

	if (__ifd_apdu_check(buf, len, &iso) < 0)
		return -1;
	return iso.cse;
}

/*
 * Convert internal APDU type to an ISO-7816-4 APDU
 */
int ifd_iso_apdu_parse(const void *data, size_t len, ifd_iso_apdu_t * iso)
{
	unsigned char *p;

	if (len < 4)
		return -1;

	if (__ifd_apdu_check(data, len, iso) < 0)
		return -1;

	p = (unsigned char *)data;
	iso->cla = *p++;
	iso->ins = *p++;
	iso->p1 = *p++;
	iso->p2 = *p++;

	return 0;
}
