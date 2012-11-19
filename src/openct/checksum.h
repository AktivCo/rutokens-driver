/*
    proto-t1.h: header file for proto-t1.c
    Copyright (C) 2004   Ludovic Rousseau

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

/* $Id: checksum.h 2974 2008-05-28 18:32:52Z rousseau $ */

#ifndef __CHECKSUM_H__
#define __CHECKSUM_H__

#include "config.h"
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <unistd.h>

extern unsigned int	csum_lrc_compute(const uint8_t *, size_t, unsigned char *);
extern unsigned int	csum_crc_compute(const uint8_t *, size_t, unsigned char *);

#endif

