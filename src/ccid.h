/*
    ccid.h: CCID structures
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
 * $Id: ccid.h 2974 2008-05-28 18:32:52Z rousseau $
 */

/*
 * Maximum number of readers supported simultaneously
 *
 * The maximum number of readers is also limited in pcsc-lite (16 by default)
 * see the definition of PCSCLITE_MAX_READERS_CONTEXTS in src/PCSC/pcsclite.h
 */
#define DRIVER_MAX_READERS 16

typedef struct
{
	/*
	 * CCID Sequence number
	 */
	unsigned char *pbSeq;
	unsigned char real_bSeq;

	/*
	 * VendorID << 16 + ProductID
	 */
	int readerID;

	/*
	 * Maximum message length
	 */
	unsigned int dwMaxDevMessageLength;

	/*
	 * Maximum IFSD
	 */
	int dwMaxIFSD;

	/*
	 * PIN support of the reader (directly from Class Descriptor)
	 */
	char bPINSupport;

	/*
	 * Number of available slots
	 */
	char bMaxSlotIndex;

	/*
	 * Read communication port timeout
	 * value is seconds
	 * this value can evolve dynamically if card request it (time processing).
	 */
	unsigned int readTimeout;

	/*
	 * bNumEndpoints
	 */
	int bNumEndpoints;

} _device_descriptor;

/* Features from bPINSupport */
#define DEV_CLASS_PIN_VERIFY		0x01
#define DEV_CLASS_PIN_MODIFY		0x02

/* See CCID specs ch. 4.2.1 */
#define DEV_ICC_PRESENT_ACTIVE		0x00	/* 00 0000 00 */
#define DEV_ICC_PRESENT_INACTIVE	0x01	/* 00 0000 01 */
#define DEV_ICC_ABSENT			0x02	/* 00 0000 10 */
#define DEV_ICC_STATUS_MASK		0x03	/* 00 0000 11 */

_device_descriptor *get_device_descriptor(unsigned int reader_index);


