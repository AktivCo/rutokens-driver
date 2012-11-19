/*
    commands.h: Commands sent to the card
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
 * $Id: commands.h 3074 2008-06-17 16:31:00 ausenok $
 */

#define SIZE_GET_SLOT_STATUS 10
#define STATUS_OFFSET 7
#define ERROR_OFFSET 8
#define CHAIN_PARAMETER_OFFSET 9
#define RUTOKEN_ATR_LEN	19

RESPONSECODE CmdPowerOn(unsigned int reader_index, unsigned int * nlength,
	unsigned char buffer[]);

RESPONSECODE CmdPowerOff(unsigned int reader_index);

RESPONSECODE CmdIccPresence(unsigned int reader_index,
	unsigned char* presence);

RESPONSECODE CmdXfrBlock(unsigned int reader_index, unsigned int tx_length,
	unsigned char tx_buffer[], unsigned int *rx_length,
	unsigned char rx_buffer[], int protoccol);

int isCharLevel(int reader_index);

