/*
    ccid_serial.h:  Serial access routines
    Copyright (C) 2003   Ludovic Rousseau

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
 * $Id: ccid_serial.h 2974 2008-05-28 18:32:52Z rousseau $
 */

#ifndef __CCID_SERAL_H__
#define __CCID_SERAL_H__

status_t OpenSerial(unsigned int reader_index, int channel);

status_t OpenSerialByName(unsigned int reader_index, char *dev_name);

status_t WriteSerial(unsigned int reader_index, unsigned int length,
	unsigned char *Buffer);

status_t ReadSerial(unsigned int reader_index, unsigned int *length,
	unsigned char *Buffer);

status_t CloseSerial(unsigned int reader_index);

#endif
