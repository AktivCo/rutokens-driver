/*
    convert_apdu.h: APDU conversion for Rutoken S taken from OpenCT.
    Copyright (C) 2012 Aktiv Co
    Copyright (C) 2007, Pavel Mironchik <rutoken@rutoken.ru>
    Copyright (C) 2007, Eugene Hermann <e_herman@rutoken.ru>

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


#ifndef CONVERT_APDU_H
#define CONVERT_APDU_H

#ifdef __cplusplus
extern "C" {
#endif

void swap_pair(unsigned char *buf, size_t len);
void swap_four(unsigned char *buf, size_t len);
int convert_doinfo_to_rtprot(void *data, size_t data_len);
int convert_fcp_to_rtprot(void *data, size_t data_len);
int convert_rtprot_to_doinfo(void *data, size_t data_len);
int convert_rtprot_to_fcp(void *data, size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* CONVERT_APDU_H */
