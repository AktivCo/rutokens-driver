/*
    defines.h

    This file is part of the Unix driver for Towitoko smartcard readers
    Copyright (C) 2000 Carlos Prados <cprados@yahoo.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public License
	along with this library; if not, write to the Free Software Foundation,
	Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef DEFINES_H
#define DEFINES_H

/*
 * Get configuration information
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*
 * Boolean constants
 */

#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

/*
 * Type definitions
 */

#include <wintypes.h>

#ifndef __cplusplus
typedef int                bool;
#endif

/*
 * Utility macros
 */

/* Invert order of bits in a byte: b7->b0, b0->b7 */
#ifndef INVERT_BYTE
#define INVERT_BYTE(a)		((((a) << 7) & 0x80) | \
				(((a) << 5) & 0x40) | \
				(((a) << 3) & 0x20) | \
				(((a) << 1) & 0x10) | \
				(((a) >> 1) & 0x08) | \
				(((a) >> 3) & 0x04) | \
				(((a) >> 5) & 0x02) | \
				(((a) >> 7) & 0x01))
#endif

#endif /* DEFINES_H */

