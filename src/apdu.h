/*
    apdu.g: APDU handling
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

#ifndef OPENCT_APDU_H
#define OPENCT_APDU_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ifd_iso_apdu {
	unsigned char		cse, cla, ins, p1, p2;
	unsigned int		lc, le;
	unsigned int		sw;
	void *			data;
	size_t			len;

	/* xxx go away */
	unsigned char *		rcv_buf;
	unsigned int		rcv_len;
} ifd_iso_apdu_t;

enum {
	IFD_APDU_CASE_1  = 0x00,
	IFD_APDU_CASE_2S = 0x01,
	IFD_APDU_CASE_3S = 0x02,
	IFD_APDU_CASE_4S = 0x03,
	IFD_APDU_CASE_2E = 0x10,
	IFD_APDU_CASE_3E = 0x20,
	IFD_APDU_CASE_4E = 0x30,

	IFD_APDU_BAD = -1
};

#define IFD_APDU_CASE_LC(c)	((c) & 0x02)
#define IFD_APDU_CASE_LE(c)	((c) & 0x01)

extern int	ifd_iso_apdu_parse(const void *, size_t, ifd_iso_apdu_t *);
extern int	ifd_apdu_case(const void *, size_t);

#ifdef __cplusplus
}
#endif

#endif /* OPENCT_APDU_H */
