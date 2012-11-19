/*
    commands.c: Commands sent to the card
    Copyright (C) 2003-2004   Ludovic Rousseau

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
 * $Id: commands.c 3104 2008-07-17 16:29:00 ausenok $
 */

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pcsclite.h>
#include <ifdhandler.h>
#include <reader.h>
#include <stdio.h>

#include "misc.h"
#include "commands.h"
#include "openct/proto-t1.h"
#include "ccid.h"
#include "defs.h"
#include "ccid_ifdhandler.h"
#include "config.h"
#include "debug.h"
#include "ccid_usb.h"
#include "apdu.h"

/* All the pinpad readers I used are more or less bogus
 * I use code to change the user command and make the firmware happy */
#define BOGUS_PINPAD_FIRMWARE

/* The firmware of SCM readers reports dwMaxCCIDMessageLength = 263
 * instead of 270 so this prevents from sending a full length APDU
 * of 260 bytes since the driver check this value */
#define BOGUS_SCM_FIRMWARE_FOR_dwMaxCCIDMessageLength

#define max( a, b )   ( ( ( a ) > ( b ) ) ? ( a ) : ( b ) )
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define IFD_ERROR_INSUFFICIENT_BUFFER 700

/* internal functions */
static RESPONSECODE CmdXfrBlockAPDU_extended(unsigned int reader_index,
	unsigned int tx_length, unsigned char tx_buffer[], unsigned int *rx_length,
	unsigned char rx_buffer[]);

static RESPONSECODE CmdXfrBlockTPDU_T0(unsigned int reader_index,
	unsigned int tx_length, unsigned char tx_buffer[], unsigned int *rx_length,
	unsigned char rx_buffer[]);

static RESPONSECODE CmdXfrBlockCHAR_T0(unsigned int reader_index, unsigned int
	tx_length, unsigned char tx_buffer[], unsigned int *rx_length, unsigned
	char rx_buffer[]);

static RESPONSECODE CmdXfrBlockTPDU_T1(unsigned int reader_index,
	unsigned int tx_length, unsigned char tx_buffer[], unsigned int *rx_length,
	unsigned char rx_buffer[]);

static void i2dw(int value, unsigned char *buffer);

const char *ct_hexdump(const void *data, size_t len);

static int rutoken_send(unsigned int reader_index, unsigned int dad,
		const unsigned char *buffer, size_t len);
static int rutoken_recv(unsigned int reader_index, unsigned int dad,
		unsigned char *buffer, size_t len, long timeout);
static int rutoken_getstatus(unsigned int reader_index, unsigned char *status);
static int rutoken_recv_sw(unsigned int reader_index, int dad, unsigned char *sw);
static int rutoken_send_tpducomand(unsigned int reader_index, int dad, const void *sbuf, 
		size_t slen, void *rbuf, size_t rlen, int iscase4);
static int rutoken_transparent( unsigned int reader_index, int dad,
		const void *sbuf, size_t slen,
		void *rbuf, size_t rlen);
static void swap_pair(unsigned char *buf, size_t len);
static void swap_four(unsigned char *buf, size_t len);
static int read_tag(unsigned char *buf, size_t buf_len,
		unsigned char tag_in, unsigned char *out, size_t out_len);
static int convert_doinfo_to_rtprot(void *data, size_t data_len);
static int convert_fcp_to_rtprot(void *data, size_t data_len);
static int convert_rtprot_to_doinfo(void *data, size_t data_len);
static int convert_rtprot_to_fcp(void *data, size_t data_len);

/*****************************************************************************
 *
 *					CmdPowerOn
 *
 ****************************************************************************/
RESPONSECODE CmdPowerOn(unsigned int reader_index, unsigned int * nlength,
	unsigned char buffer[], int voltage)
{
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	int r;
	unsigned char pcbuffer[SIZE_GET_SLOT_STATUS];

	/* first power off to reset the ICC state machine */
	r = CmdPowerOff(reader_index);
	if (r != IFD_SUCCESS)
		return r;

	/* wait for ready */
	r = CmdGetSlotStatus(reader_index, pcbuffer);
	if (r != IFD_SUCCESS)
		return r;

	/* Power On */
	r = ControlUSB(reader_index, 0xC1, 0x62, 0, buffer, 19);

	/* we got an error? */
	if (r < 0)
	{
		DEBUG_INFO2("ICC Power On failed: %s", strerror(errno));
		return IFD_COMMUNICATION_ERROR;
	}

	*nlength = 19;

	return IFD_SUCCESS;
} /* CmdPowerOn */


/*****************************************************************************
 *
 *					SecurePINVerify
 *
 ****************************************************************************/
RESPONSECODE SecurePINVerify(unsigned int reader_index,
	unsigned char TxBuffer[], unsigned int TxLength,
	unsigned char RxBuffer[], unsigned int *RxLength)
{
	unsigned char cmd[11+14+CMD_BUF_SIZE];
	unsigned int a, b;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	int old_read_timeout;
	RESPONSECODE ret;

	cmd[0] = 0x69;	/* Secure */
	cmd[5] = ccid_descriptor->bCurrentSlotIndex;	/* slot number */
	cmd[6] = (*ccid_descriptor->pbSeq)++;
	cmd[7] = 0;		/* bBWI */
	cmd[8] = 0;		/* wLevelParameter */
	cmd[9] = 0;
	cmd[10] = 0;	/* bPINOperation: PIN Verification */

	/* 19 is the size of the PCSCv2 PIN verify structure
	 * The equivalent CCID structure is only 14-bytes long */
	if (TxLength > 19+CMD_BUF_SIZE) /* command too large? */
	{
		DEBUG_INFO3("Command too long: %d > %d", TxLength, 19+CMD_BUF_SIZE);
		return IFD_NOT_SUPPORTED;
	}

	if (TxLength < 19+4 /* 4 = APDU size */)	/* command too short? */
	{
		DEBUG_INFO3("Command too short: %d < %d", TxLength, 19+4);
		return IFD_NOT_SUPPORTED;
	}

	if (dw2i(TxBuffer, 15) + 19 != TxLength) /* ulDataLength field coherency */
	{
		DEBUG_INFO3("Wrong lengths: %d %d", dw2i(TxBuffer, 15) + 19, TxLength);
		return IFD_NOT_SUPPORTED;
	}

	/* make sure bEntryValidationCondition is valid
	 * The Cherry XX44 reader crashes with a wrong value */
	if ((0x00 == TxBuffer[7]) || (TxBuffer[7] > 0x07))
	{
		DEBUG_INFO2("Correct bEntryValidationCondition (was 0x%02X)",
			TxBuffer[7]);
		TxBuffer[7] = 0x02;
	}

#ifdef BOGUS_PINPAD_FIRMWARE
	/* bug circumvention for the GemPC Pinpad */
	if (GEMPCPINPAD == ccid_descriptor->readerID)
	{
		/* the firmware reject the cases: 00h No string and FFh default
		 * CCID message. The only value supported is 01h (display 1 message) */
		if (0x01 != TxBuffer[8])
		{
			DEBUG_INFO2("Correct bNumberMessage for GemPC Pinpad (was %d)",
				TxBuffer[8]);
			TxBuffer[8] = 0x01;
		}

		/* The reader does not support, and actively reject, "max size reached"
		 * and "timeout occured" validation conditions */
		if (0x02 != TxBuffer[7])
		{
			DEBUG_INFO2("Correct bEntryValidationCondition for GemPC Pinpad (was %d)",
				TxBuffer[7]);
			TxBuffer[7] = 0x02;	/* validation key pressed */
		}

	}
#endif

	/* T=1 Protocol Management for a TPDU reader */
	if ((SCARD_PROTOCOL_T1 == ccid_descriptor->cardProtocol)
		&& (CCID_CLASS_TPDU == (ccid_descriptor->dwFeatures & CCID_CLASS_EXCHANGE_MASK)))
	{
		ct_buf_t sbuf;
		unsigned char sdata[T1_BUFFER_SIZE];

		/* Initialize send buffer with the APDU */
		ct_buf_set(&sbuf,
			(void *)(TxBuffer + offsetof(PIN_VERIFY_STRUCTURE, abData)),
			TxLength - offsetof(PIN_VERIFY_STRUCTURE, abData));

		/* Create T=1 block */
		ret = t1_build(&((get_ccid_slot(reader_index))->t1),
			sdata, 0, T1_I_BLOCK, &sbuf, NULL);

		/* Increment the sequence numbers  */
		get_ccid_slot(reader_index)->t1.ns ^= 1;
		get_ccid_slot(reader_index)->t1.nr ^= 1;

		/* Copy the generated T=1 block prologue into the teoprologue
		 * of the CCID command */
		memcpy(TxBuffer + offsetof(PIN_VERIFY_STRUCTURE, bTeoPrologue),
			sdata, 3);
	}

	/* Build a CCID block from a PC/SC V2.1.2 Part 10 block */
	for (a = 11, b = 0; b < TxLength; b++)
	{
		if (1 == b) /* bTimeOut2 field */
			/* Ignore the second timeout as there's nothing we can do with
			 * it currently */
			continue;

		if ((b >= 15) && (b <= 18)) /* ulDataLength field (4 bytes) */
			/* the ulDataLength field is not present in the CCID frame
			 * so do not copy */
			continue;

		/* copy the CCID block 'verbatim' */
		cmd[a] = TxBuffer[b];
		a++;
	}

	/* SPR532 and Case 1 APDU */
	if ((SPR532 == ccid_descriptor->readerID) && (TxBuffer[15] == 4))
	{
		RESPONSECODE return_value;
		unsigned char cmd_tmp[] = { 0x80, 0x02, 0x00 };
		unsigned char res_tmp[1];
		unsigned int res_length = sizeof(res_tmp);

		/* the SPR532 will append the PIN code without any padding */
		return_value = CmdEscape(reader_index, cmd_tmp, sizeof(cmd_tmp),
			res_tmp, &res_length);
		if (return_value != IFD_SUCCESS)
			return return_value;
	}

	i2dw(a - 10, cmd + 1);  /* CCID message length */

	old_read_timeout = ccid_descriptor -> readTimeout;
	ccid_descriptor -> readTimeout = max(30, TxBuffer[0]+10);	/* at least 30 seconds */

	if (WritePort(reader_index, a, cmd) != STATUS_SUCCESS)
		return IFD_COMMUNICATION_ERROR;

	ret = CCID_Receive(reader_index, RxLength, RxBuffer, NULL);

	/* T=1 Protocol Management for a TPDU reader */
	if ((IFD_SUCCESS == ret)
		&& (SCARD_PROTOCOL_T1 == ccid_descriptor->cardProtocol)
		&& (CCID_CLASS_TPDU == (ccid_descriptor->dwFeatures & CCID_CLASS_EXCHANGE_MASK)))
	{
		/* timeout and cancel cases are faked by CCID_Receive() */
		if (2 == *RxLength)
		{
			/* Decrement the sequence numbers since no TPDU was sent */
			get_ccid_slot(reader_index)->t1.ns ^= 1;
			get_ccid_slot(reader_index)->t1.nr ^= 1;
		}
		else
		{
			/* get only the T=1 data */
			/* FIXME: manage T=1 error blocks */
			memmove(RxBuffer, RxBuffer+3, *RxLength -4);
			*RxLength -= 4;	/* remove NAD, PCB, LEN and CRC */
		}
	}

	ccid_descriptor -> readTimeout = old_read_timeout;
	return ret;
} /* SecurePINVerify */


/*****************************************************************************
 *
 *					SecurePINModify
 *
 ****************************************************************************/
RESPONSECODE SecurePINModify(unsigned int reader_index,
	unsigned char TxBuffer[], unsigned int TxLength,
	unsigned char RxBuffer[], unsigned int *RxLength)
{
	unsigned char cmd[11+19+CMD_BUF_SIZE];
	unsigned int a, b;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	int old_read_timeout;
	RESPONSECODE ret;
#ifdef BOGUS_PINPAD_FIRMWARE
	int bNumberMessages = 0; /* for GemPC Pinpad */
#endif

	cmd[0] = 0x69;	/* Secure */
	cmd[5] = ccid_descriptor->bCurrentSlotIndex;	/* slot number */
	cmd[6] = (*ccid_descriptor->pbSeq)++;
	cmd[7] = 0;		/* bBWI */
	cmd[8] = 0;		/* wLevelParameter */
	cmd[9] = 0;
	cmd[10] = 1;	/* bPINOperation: PIN Modification */

	/* 24 is the size of the PCSC PIN modify structure
	 * The equivalent CCID structure is only 18 or 19-bytes long */
	if (TxLength > 24+CMD_BUF_SIZE) /* command too large? */
	{
		DEBUG_INFO3("Command too long: %d > %d", TxLength, 24+CMD_BUF_SIZE);
		return IFD_NOT_SUPPORTED;
	}

	if (TxLength < 24+4 /* 4 = APDU size */) /* command too short? */
	{
		DEBUG_INFO3("Command too short: %d < %d", TxLength, 24+4);
		return IFD_NOT_SUPPORTED;
	}

	if (dw2i(TxBuffer, 20) + 24 != TxLength) /* ulDataLength field coherency */
	{
		DEBUG_INFO3("Wrong lengths: %d %d", dw2i(TxBuffer, 20) + 24, TxLength);
		return IFD_NOT_SUPPORTED;
	}

	/* Make sure in the beginning if bNumberMessage is valid or not */
	if (TxBuffer[11] > 3)
	{
		DEBUG_INFO2("Wrong bNumberMessage: %d", TxBuffer[11]);
		return IFD_NOT_SUPPORTED;
	}

	/* Make sure bEntryValidationCondition is valid
	 * The Cherry XX44 reader crashes with a wrong value */
	if ((0x00 == TxBuffer[10]) || (TxBuffer[10] > 0x07))
	{
		DEBUG_INFO2("Correct bEntryValidationCondition (was 0x%02X)",
			TxBuffer[10]);
		TxBuffer[10] = 0x02;
	}

#ifdef BOGUS_PINPAD_FIRMWARE
	/* some firmwares are buggy so we try to "correct" the frame */
	/*
	 * SPR 532 and Cherry ST 2000C has no display but requires _all_
	 * bMsgIndex fields with bNumberMessage set to 0.
	 */
	if ((SPR532 == ccid_descriptor->readerID)
		|| (CHERRYST2000 == ccid_descriptor->readerID))
	{
		TxBuffer[11] = 0x03; /* set bNumberMessages to 3 so that
								all bMsgIndex123 are filled */
		TxBuffer[14] = TxBuffer[15] = TxBuffer[16] = 0;	/* bMsgIndex123 */
	}

	/* the bug is a bit different than for the Cherry ST 2000C
	 * with bNumberMessages < 3 the command seems to be accepted
	 * and the card sends 6B 80 */
	if (CHERRYXX44 == ccid_descriptor->readerID)
	{
		TxBuffer[11] = 0x03; /* set bNumberMessages to 3 so that
								all bMsgIndex123 are filled */
	}

	/* bug circumvention for the GemPC Pinpad */
	if (GEMPCPINPAD == ccid_descriptor->readerID)
	{
		/* The reader does not support, and actively reject, "max size reached"
		 * and "timeout occured" validation conditions */
		if (0x02 != TxBuffer[10])
		{
			DEBUG_INFO2("Correct bEntryValidationCondition for GemPC Pinpad (was %d)",
				TxBuffer[10]);
			TxBuffer[10] = 0x02;	/* validation key pressed */
		}

		/* the reader does not support any other value than 3 for the number
		 * of messages */
		bNumberMessages = TxBuffer[11];
		if (0x03 != TxBuffer[11])
		{
			DEBUG_INFO2("Correct bNumberMessages for GemPC Pinpad (was %d)",
				TxBuffer[11]);
			TxBuffer[11] = 0x03; /* 3 messages */
		}
	}
#endif

	/* T=1 Protocol Management for a TPDU reader */
	if ((SCARD_PROTOCOL_T1 == ccid_descriptor->cardProtocol)
		&& (CCID_CLASS_TPDU == (ccid_descriptor->dwFeatures & CCID_CLASS_EXCHANGE_MASK)))
	{
		ct_buf_t sbuf;
		unsigned char sdata[T1_BUFFER_SIZE];

		/* Initialize send buffer with the APDU */
		ct_buf_set(&sbuf,
			(void *)(TxBuffer + offsetof(PIN_MODIFY_STRUCTURE, abData)),
			TxLength - offsetof(PIN_MODIFY_STRUCTURE, abData));

		/* Create T=1 block */
		ret = t1_build(&((get_ccid_slot(reader_index))->t1),
			sdata, 0, T1_I_BLOCK, &sbuf, NULL);

		/* Increment the sequence numbers  */
		get_ccid_slot(reader_index)->t1.ns ^= 1;
		get_ccid_slot(reader_index)->t1.nr ^= 1;

		/* Copy the generated T=1 block prologue into the teoprologue
		 * of the CCID command */
		memcpy(TxBuffer + offsetof(PIN_MODIFY_STRUCTURE, bTeoPrologue),
			sdata, 3);
	}

	/* Build a CCID block from a PC/SC V2.1.2 Part 10 block */

	/* Do adjustments as needed - CCID spec is not exact with some
	 * details in the format of the structure, per-reader adaptions
	 * might be needed.
	 */
	for (a = 11, b = 0; b < TxLength; b++)
	{
		if (1 == b) /* bTimeOut2 */
			/* Ignore the second timeout as there's nothing we can do with it
			 * currently */
			continue;

		if (15 == b) /* bMsgIndex2 */
		{
			/* in CCID the bMsgIndex2 is present only if bNumberMessage != 0 */
			if (0 == TxBuffer[11])
				continue;
		}

		if (16 == b) /* bMsgIndex3 */
		{
			/* in CCID the bMsgIndex3 is present only if bNumberMessage == 3 */
			if (TxBuffer[11] < 3)
				continue;
		}

		if ((b >= 20) && (b <= 23)) /* ulDataLength field (4 bytes) */
			/* the ulDataLength field is not present in the CCID frame
			 * so do not copy */
			continue;

		/* copy to the CCID block 'verbatim' */
		cmd[a] = TxBuffer[b];
		a++;
 	}

#ifdef BOGUS_PINPAD_FIRMWARE
	if ((SPR532 == ccid_descriptor->readerID)
		|| (CHERRYST2000 == ccid_descriptor->readerID))
	{
		cmd[21] = 0x00; /* set bNumberMessages to 0 */
	}

	if (GEMPCPINPAD == ccid_descriptor->readerID)
		cmd[21] = bNumberMessages;	/* restore the real value */
#endif

	/* We know the size of the CCID message now */
	i2dw(a - 10, cmd + 1);	/* command length (includes bPINOperation) */

	old_read_timeout = ccid_descriptor -> readTimeout;
	ccid_descriptor -> readTimeout = max(30, TxBuffer[0]+10);	/* at least 30 seconds */

	if (WritePort(reader_index, a, cmd) != STATUS_SUCCESS)
 		return IFD_COMMUNICATION_ERROR;

 	ret = CCID_Receive(reader_index, RxLength, RxBuffer, NULL);

	/* T=1 Protocol Management for a TPDU reader */
	if ((IFD_SUCCESS == ret)
		&& (SCARD_PROTOCOL_T1 == ccid_descriptor->cardProtocol)
		&& (CCID_CLASS_TPDU == (ccid_descriptor->dwFeatures & CCID_CLASS_EXCHANGE_MASK)))
	{
		/* timeout and cancel cases are faked by CCID_Receive() */
		if (2 == *RxLength)
		{
			/* Decrement the sequence numbers since no TPDU was sent */
			get_ccid_slot(reader_index)->t1.ns ^= 1;
			get_ccid_slot(reader_index)->t1.nr ^= 1;
		}
		else
		{
			/* get only the T=1 data */
			/* FIXME: manage T=1 error blocks */
			memmove(RxBuffer, RxBuffer+3, *RxLength -4);
			*RxLength -= 4;	/* remove NAD, PCB, LEN and CRC */
		}
	}

	ccid_descriptor -> readTimeout = old_read_timeout;
	return ret;
} /* SecurePINModify */


/*****************************************************************************
 *
 *					Escape
 *
 ****************************************************************************/
RESPONSECODE CmdEscape(unsigned int reader_index,
	const unsigned char TxBuffer[], unsigned int TxLength,
	unsigned char RxBuffer[], unsigned int *RxLength)
{
	unsigned char *cmd_in, *cmd_out;
	status_t res;
	unsigned int length_in, length_out;
	RESPONSECODE return_value = IFD_SUCCESS;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);

again:
	/* allocate buffers */
	length_in = 10 + TxLength;
	if (NULL == (cmd_in = malloc(length_in)))
		return IFD_COMMUNICATION_ERROR;

	length_out = 10 + *RxLength;
	if (NULL == (cmd_out = malloc(length_out)))
	{
		free(cmd_in);
		return IFD_COMMUNICATION_ERROR;
	}

	cmd_in[0] = 0x6B; /* PC_to_RDR_Escape */
	i2dw(length_in - 10, cmd_in+1);	/* dwLength */
	cmd_in[5] = ccid_descriptor->bCurrentSlotIndex;	/* slot number */
	cmd_in[6] = (*ccid_descriptor->pbSeq)++;
	cmd_in[7] = cmd_in[8] = cmd_in[9] = 0; /* RFU */

	/* copy the command */
	memcpy(&cmd_in[10], TxBuffer, TxLength);

	res = WritePort(reader_index, length_in, cmd_in);
	free(cmd_in);
	if (res != STATUS_SUCCESS)
	{
		free(cmd_out);
		return IFD_COMMUNICATION_ERROR;
	}

	res = ReadPort(reader_index, &length_out, cmd_out);

	/* replay the command if NAK
	 * This (generally) happens only for the first command sent to the reader
	 * with the serial protocol so it is not really needed for all the other
	 * ReadPort() calls */
	if (STATUS_COMM_NAK == res)
	{
		free(cmd_out);
		goto again;
	}

	if (res != STATUS_SUCCESS)
	{
		free(cmd_out);
		return IFD_COMMUNICATION_ERROR;
	}

	if (length_out < STATUS_OFFSET+1)
	{
		DEBUG_CRITICAL2("Not enough data received: %d bytes", length_out);
		return IFD_COMMUNICATION_ERROR;
	}

	if (cmd_out[STATUS_OFFSET] & CCID_COMMAND_FAILED)
	{
		ccid_error(cmd_out[ERROR_OFFSET], __FILE__, __LINE__, __FUNCTION__);    /* bError */
		return_value = IFD_COMMUNICATION_ERROR;
	}

	/* copy the response */
	length_out = dw2i(cmd_out, 1);
	if (length_out > *RxLength)
		length_out = *RxLength;
	*RxLength = length_out;
	memcpy(RxBuffer, &cmd_out[10], length_out);

	free(cmd_out);

	return return_value;
} /* Escape */


/*****************************************************************************
 *
 *					CmdPowerOff
 *
 ****************************************************************************/
RESPONSECODE CmdPowerOff(unsigned int reader_index)
{
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	int r;

	/* PowerOff */
	r = ControlUSB(reader_index, 0x41, 0x63, 0, NULL, 0);

	/* we got an error? */
	if (r < 0)
	{
		DEBUG_INFO2("ICC Power Off failed: %s", strerror(errno));
		return IFD_COMMUNICATION_ERROR;
	}

	return IFD_SUCCESS;
} /* CmdPowerOff */


/*****************************************************************************
 *
 *					CmdGetSlotStatus
 *
 ****************************************************************************/
RESPONSECODE CmdGetSlotStatus(unsigned int reader_index, unsigned char buffer[])
{
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	int r;
	unsigned char status;

	/* SlotStatus */
	r = ControlUSB(reader_index, 0xC1, 0xA0, 0, &status, sizeof(status)); 

	/* we got an error? */
	if (r < 0)
	{
		DEBUG_INFO2("ICC Slot Status failed: %s", strerror(errno));
		if (ENODEV == errno)
			return IFD_NO_SUCH_DEVICE;
		return IFD_COMMUNICATION_ERROR;
	}

	/* busy */
	if ((status & 0xF0) == 0x40)
	{
		int i;
		unsigned char prev_status;

		DEBUG_INFO2("Busy: 0x%02X", status);

		for (i = 0; i < 200; i++)
		{
			do {
				usleep(10000);  /* 10 ms */
				prev_status = status;

				/* SlotStatus */
				r = ControlUSB(reader_index, 0xC1, 0xA0, 0, &status, sizeof(status));

				/* we got an error? */
				if (r < 0)
				{
					DEBUG_INFO2("ICC Slot Status failed: %s", strerror(errno));
					if (ENODEV == errno)
						return IFD_NO_SUCH_DEVICE;
					return IFD_COMMUNICATION_ERROR;
				}

				if ((status & 0xF0) != 0x40)
					goto end;
			} while ((((prev_status & 0x0F) + 1) & 0x0F) == (status & 0x0F));
		}

		return IFD_COMMUNICATION_ERROR;
	}

end:
	/* simulate a CCID bStatus */
	/* present and active by default */
	buffer[7] = CCID_ICC_PRESENT_ACTIVE;

	/* mute */
	if (0x80 == status)

		buffer[7] = CCID_ICC_ABSENT;

	/* store the status for CmdXfrBlockCHAR_T0() */
	buffer[0] = status;

	return IFD_SUCCESS;
} /* CmdGetSlotStatus */


/*****************************************************************************
 *
 *					CmdXfrBlock
 *
 ****************************************************************************/
RESPONSECODE CmdXfrBlock(unsigned int reader_index, unsigned int tx_length,
	unsigned char tx_buffer[], unsigned int *rx_length,
	unsigned char rx_buffer[], int protocol)
{
	RESPONSECODE return_value = IFD_SUCCESS;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);

	/* APDU or TPDU? */
	switch (ccid_descriptor->dwFeatures & CCID_CLASS_EXCHANGE_MASK)
	{
		/* case CCID_CLASS_TPDU:
			if (protocol == T_0)
				return_value = CmdXfrBlockTPDU_T0(reader_index,
					tx_length, tx_buffer, rx_length, rx_buffer);
			else
				if (protocol == T_1)
					return_value = CmdXfrBlockTPDU_T1(reader_index, tx_length,
						tx_buffer, rx_length, rx_buffer);
				else
					return_value = IFD_PROTOCOL_NOT_SUPPORTED;
			break;

		case CCID_CLASS_SHORT_APDU:
			return_value = CmdXfrBlockTPDU_T0(reader_index,
				tx_length, tx_buffer, rx_length, rx_buffer);
			break;

		case CCID_CLASS_EXTENDED_APDU:
			return_value = CmdXfrBlockAPDU_extended(reader_index,
				tx_length, tx_buffer, rx_length, rx_buffer);
			break; */

		case CCID_CLASS_CHARACTER:
			if (protocol == T_0)
				if((*rx_length = rutoken_transparent(reader_index, 0, tx_buffer, tx_length, rx_buffer,
					 *rx_length)) > 0)
					return_value = IFD_SUCCESS;
			else
			{
				/* if (protocol == T_1)
					return_value = CmdXfrBlockTPDU_T1(reader_index, tx_length,
						tx_buffer, rx_length, rx_buffer);
 				else */
				*rx_length = 0;
				return_value = IFD_PROTOCOL_NOT_SUPPORTED;
			}
			break;

		default:
			return_value = IFD_COMMUNICATION_ERROR;
	}

	return return_value;
} /* CmdXfrBlock */


/*****************************************************************************
 *
 *					CCID_Transmit
 *
 ****************************************************************************/
RESPONSECODE CCID_Transmit(unsigned int reader_index, unsigned int tx_length,
	const unsigned char* tx_buffer /* [] */, unsigned short rx_length, unsigned char bBWI)
{
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	unsigned char status;
	int r;

	/* Xfr Block */
	r = ControlUSB(reader_index, 0x41, 0x65, 0, (unsigned char*)tx_buffer, tx_length);

	/* we got an error? */
	if (r < 0)
	{
		DEBUG_INFO2("ICC Xfr Block failed: %s", strerror(errno));
		return IFD_COMMUNICATION_ERROR;
	}

	if (CmdGetSlotStatus(reader_index, &status) != IFD_SUCCESS)
	{
		DEBUG_INFO("error get status");
		return IFD_COMMUNICATION_ERROR;
	}

	return IFD_SUCCESS;
} /* CCID_Transmit */


/*****************************************************************************
 *
 *					CCID_Receive
 *
 ****************************************************************************/
RESPONSECODE CCID_Receive(unsigned int reader_index, unsigned int *rx_length,
	unsigned char rx_buffer[], unsigned char *chain_parameter)
{
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	int r;
	unsigned char status;

	/* Data Block */
	r = ControlUSB(reader_index, 0xC1, 0x6F, 0, rx_buffer, *rx_length);

	/* we got an error? */
	if (r < 0)
	{
		DEBUG_INFO2("ICC Data Block failed: %s", strerror(errno));
		return IFD_COMMUNICATION_ERROR;
	}

	if (CmdGetSlotStatus(reader_index, &status) != IFD_SUCCESS)
	{
		DEBUG_INFO("error get status");
		return IFD_COMMUNICATION_ERROR;
	}

	return IFD_SUCCESS;
} /* CCID_Receive */


/*****************************************************************************
 *
 *					CmdXfrBlockAPDU_extended
 *
 ****************************************************************************/
static RESPONSECODE CmdXfrBlockAPDU_extended(unsigned int reader_index,
	unsigned int tx_length, unsigned char tx_buffer[], unsigned int *rx_length,
	unsigned char rx_buffer[])
{
	RESPONSECODE return_value;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	unsigned char chain_parameter;
	unsigned int local_tx_length, sent_length;
	unsigned int local_rx_length, received_length;
	int buffer_overflow = 0;

	if (ICCD_B == ccid_descriptor->bInterfaceProtocol)
	{
		/* length is on 16-bits only
		 * if a size > 0x1000 is used then usb_control_msg() fails with
		 * "Invalid argument" */
		if (*rx_length > 0x1000)
			*rx_length = 0x1000;
	}

	DEBUG_COMM2("T=0 (extended): %d bytes", tx_length);

	/* send the APDU */
	sent_length = 0;

	/* we suppose one command is enough */
	chain_parameter = 0x00;

	local_tx_length = tx_length - sent_length;
	if (local_tx_length > CMD_BUF_SIZE)
	{
		local_tx_length = CMD_BUF_SIZE;
		/* the command APDU begins with this command, and continue in the next
		 * PC_to_RDR_XfrBlock */
		chain_parameter = 0x01;
	}
	if (local_tx_length > ccid_descriptor->dwMaxCCIDMessageLength-10)
	{
		local_tx_length = ccid_descriptor->dwMaxCCIDMessageLength-10;
		chain_parameter = 0x01;
	}

send_next_block:
	return_value = CCID_Transmit(reader_index, local_tx_length, tx_buffer,
		chain_parameter, 0);
	if (return_value != IFD_SUCCESS)
		return return_value;

	sent_length += local_tx_length;
	tx_buffer += local_tx_length;

	/* we just sent the last block (0x02) or only one block was needded (0x00) */
	if ((0x02 == chain_parameter) || (0x00 == chain_parameter))
		goto receive_block;

	/* read a nul block */
	return_value = CCID_Receive(reader_index, &local_rx_length, NULL, NULL);
	if (return_value != IFD_SUCCESS)
		return return_value;

	/* size of the next block */
	if (tx_length - sent_length > local_tx_length)
	{
		/* the abData field continues a command APDU and
		 * another block is to follow */
		chain_parameter = 0x03;
	}
	else
	{
		/* this abData field continues a command APDU and ends
		 * the APDU command */
		chain_parameter = 0x02;

		/* last (smaller) block */
		local_tx_length = tx_length - sent_length;
	}

	goto send_next_block;

receive_block:
	/* receive the APDU */
	received_length = 0;

receive_next_block:
	local_rx_length = *rx_length - received_length;
	return_value = CCID_Receive(reader_index, &local_rx_length, rx_buffer,
		&chain_parameter);
	if (IFD_ERROR_INSUFFICIENT_BUFFER == return_value)
	{
		buffer_overflow = 1;

		/* we continue to read all the response APDU */
		return_value = IFD_SUCCESS;
	}

	if (return_value != IFD_SUCCESS)
		return return_value;

	/* advance in the reiceiving buffer */
	rx_buffer += local_rx_length;
	received_length += local_rx_length;

	switch (chain_parameter)
	{
		/* the response APDU begins and ends in this command */
		case 0x00:
		/* this abData field continues the response APDU and ends the response
		 * APDU */
		case 0x02:
			break;

		/* the response APDU begins with this command and is to continue */
		case 0x01:
		/* this abData field continues the response APDU and another block is
		 * to follow */
		case 0x03:
		/* empty abData field, continuation of the command APDU is expected in
		 * next PC_to_RDR_XfrBlock command */
		case 0x10:
			/* send a nul block */
			return_value = CCID_Transmit(reader_index, 0, NULL, 0, 0);
			if (return_value != IFD_SUCCESS)
				return return_value;

			goto receive_next_block;
	}

	*rx_length = received_length;

	/* generate an overflow detected by pcscd */
	if (buffer_overflow)
		(*rx_length)++;

	return IFD_SUCCESS;
} /* CmdXfrBlockAPDU_extended */


/*****************************************************************************
 *
 *					CmdXfrBlockTPDU_T0
 *
 ****************************************************************************/
static RESPONSECODE CmdXfrBlockTPDU_T0(unsigned int reader_index,
	unsigned int tx_length, unsigned char tx_buffer[], unsigned int *rx_length,
	unsigned char rx_buffer[])
{
	RESPONSECODE return_value = IFD_SUCCESS;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);

	DEBUG_COMM2("T=0: %d bytes", tx_length);

	/* command length too big for CCID reader? */
	if (tx_length > ccid_descriptor->dwMaxCCIDMessageLength-10)
	{
#ifdef BOGUS_SCM_FIRMWARE_FOR_dwMaxCCIDMessageLength
		if (263 == ccid_descriptor->dwMaxCCIDMessageLength)
		{
			DEBUG_INFO3("Command too long (%d bytes) for max: %d bytes."
				" SCM reader with bogus firmware?",
				tx_length, ccid_descriptor->dwMaxCCIDMessageLength-10);
		}
		else
#endif
		{
			DEBUG_CRITICAL3("Command too long (%d bytes) for max: %d bytes",
				tx_length, ccid_descriptor->dwMaxCCIDMessageLength-10);
			return IFD_COMMUNICATION_ERROR;
		}
	}

	/* command length too big for CCID driver? */
	if (tx_length > CMD_BUF_SIZE)
	{
		DEBUG_CRITICAL3("Command too long (%d bytes) for max: %d bytes",
				tx_length, CMD_BUF_SIZE);
		return IFD_COMMUNICATION_ERROR;
	}

	return_value = CCID_Transmit(reader_index, tx_length, tx_buffer, 0, 0);
	if (return_value != IFD_SUCCESS)
		return return_value;

	return CCID_Receive(reader_index, rx_length, rx_buffer, NULL);
} /* CmdXfrBlockTPDU_T0 */


/*****************************************************************************
 *
 *					T0CmdParsing
 *
 ****************************************************************************/
static RESPONSECODE T0CmdParsing(unsigned char *cmd, unsigned int cmd_len,
	unsigned int *exp_len)
{
	*exp_len = 0;

	/* Ref: 7816-4 Annex A */
	switch (cmd_len)
	{
		case 4:	/* Case 1 */
			*exp_len = 2; /* SW1 and SW2 only */
			break;

		case 5: /* Case 2 */
			if (cmd[4] != 0)
				*exp_len = cmd[4] + 2;
			else
				*exp_len = 256 + 2;
			break;

		default: /* Case 3 */
			if (cmd_len > 5 && cmd_len == (unsigned int)(cmd[4] + 5))
				*exp_len = 2; /* SW1 and SW2 only */
			else
				return IFD_COMMUNICATION_ERROR;	/* situation not supported */
			break;
	}

	return IFD_SUCCESS;
} /* T0CmdParsing */


/*****************************************************************************
 *
 *					T0ProcACK
 *
 ****************************************************************************/
static RESPONSECODE T0ProcACK(unsigned int reader_index,
	unsigned char **snd_buf, unsigned int *snd_len,
	unsigned char **rcv_buf, unsigned int *rcv_len,
	unsigned char **in_buf, unsigned int *in_len,
	unsigned int proc_len, int is_rcv)
{
	RESPONSECODE return_value;
	unsigned int remain_len;
	unsigned char tmp_buf[512];
	unsigned int ret_len;

	DEBUG_COMM2("Enter, is_rcv = %d", is_rcv);

	if (is_rcv == 1)
	{	/* Receiving mode */
		if (*in_len > 0)
		{	/* There are still available data in our buffer */
			if (*in_len >= proc_len)
			{
				/* We only need to get the data from our buffer */
				memcpy(*rcv_buf, *in_buf, proc_len);
				*rcv_buf += proc_len;
				*in_buf += proc_len;
				*rcv_len += proc_len;
				*in_len -= proc_len;

				return IFD_SUCCESS;
			}
			else
			{
				/* Move all data in the input buffer to the reply buffer */
				remain_len = proc_len - *in_len;
				memcpy(*rcv_buf, *in_buf, *in_len);
				*rcv_buf += *in_len;
				*in_buf += *in_len;
				*rcv_len += *in_len;
				*in_len = 0;
			}
		}
		else
			/* There is no data in our tmp_buf,
			 * we have to read all data we needed */
			remain_len = proc_len;

		/* Read the expected data from the smartcard */
		if (*in_len != 0)
		{
			DEBUG_CRITICAL("*in_len != 0");
			return IFD_COMMUNICATION_ERROR;
		}

		memset(tmp_buf, 0, sizeof(tmp_buf));

#ifdef O2MICRO_OZ776_PATCH
		if((0 != remain_len) && (0 == (remain_len + 10) % 64))
        {
			/* special hack to avoid a command of size modulo 64
			 * we send two commands instead */
            ret_len = 1;
            return_value = CCID_Transmit(reader_index, 0, *snd_buf, ret_len, 0);
            if (return_value != IFD_SUCCESS)
                return return_value;
            return_value = CCID_Receive(reader_index, &ret_len, tmp_buf, NULL);
            if (return_value != IFD_SUCCESS)
                return return_value;

            ret_len = remain_len - 1;
            return_value = CCID_Transmit(reader_index, 0, *snd_buf, ret_len, 0);
            if (return_value != IFD_SUCCESS)
                return return_value;
            return_value = CCID_Receive(reader_index, &ret_len, &tmp_buf[1],
				NULL);
            if (return_value != IFD_SUCCESS)
                return return_value;

            ret_len += 1;
        }
        else
#endif
		{
			ret_len = remain_len;
			return_value = CCID_Transmit(reader_index, 0, *snd_buf, ret_len, 0);
			if (return_value != IFD_SUCCESS)
				return return_value;

			return_value = CCID_Receive(reader_index, &ret_len, tmp_buf, NULL);
			if (return_value != IFD_SUCCESS)
				return return_value;
		}
		memcpy(*rcv_buf, tmp_buf, remain_len);
		*rcv_buf += remain_len, *rcv_len += remain_len;

		/* If ret_len != remain_len, our logic is erroneous */
		if (ret_len != remain_len)
		{
			DEBUG_CRITICAL("ret_len != remain_len");
			return IFD_COMMUNICATION_ERROR;
		}
	}
	else
	{	/* Sending mode */

		return_value = CCID_Transmit(reader_index, proc_len, *snd_buf, 1, 0);
		if (return_value != IFD_SUCCESS)
			return return_value;

		*snd_len -= proc_len;
		*snd_buf += proc_len;
	}

	DEBUG_COMM("Exit");

	return IFD_SUCCESS;
} /* T0ProcACK */


/*****************************************************************************
 *
 *					T0ProcSW1
 *
 ****************************************************************************/
static RESPONSECODE T0ProcSW1(unsigned int reader_index,
	unsigned char *rcv_buf, unsigned int *rcv_len,
	unsigned char *in_buf, unsigned int in_len)
{
	RESPONSECODE return_value = IFD_SUCCESS;
	UCHAR tmp_buf[512];
	unsigned char *rcv_buf_tmp = rcv_buf;
	const unsigned int rcv_len_tmp = *rcv_len;
	unsigned char sw1, sw2;

	/* store the SW1 */
	sw1 = *rcv_buf = *in_buf;
	rcv_buf++;
	in_buf++;
	in_len--;
	(*rcv_len)++;

	/* store the SW2 */
	if (0 == in_len)
	{
		return_value = CCID_Transmit(reader_index, 0, rcv_buf, 1, 0);
		if (return_value != IFD_SUCCESS)
			return return_value;

		in_len = 1;

		return_value = CCID_Receive(reader_index, &in_len, tmp_buf, NULL);
		if (return_value != IFD_SUCCESS)
			return return_value;

		in_buf = tmp_buf;
	}
	sw2 = *rcv_buf = *in_buf;
	rcv_buf++;
	in_buf++;
	in_len--;
	(*rcv_len)++;

	if (return_value != IFD_SUCCESS)
	{
		rcv_buf_tmp[0] = rcv_buf_tmp[1] = 0;
		*rcv_len = rcv_len_tmp;
	}

	DEBUG_COMM3("Exit: SW=%02X %02X", sw1, sw2);

	return return_value;
} /* T0ProcSW1 */


/*****************************************************************************
 *
 *					CmdXfrBlockCHAR_T0
 *
 ****************************************************************************/
static RESPONSECODE CmdXfrBlockCHAR_T0(unsigned int reader_index,
	unsigned int snd_len, unsigned char snd_buf[], unsigned int *rcv_len,
	unsigned char rcv_buf[])
{
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	unsigned char cmd[5];
	unsigned char pcbuffer[SIZE_GET_SLOT_STATUS];
	unsigned char sw_buf[2];
	unsigned int sw_len = 2;
	RESPONSECODE return_value;

	DEBUG_COMM2("T=0: %d bytes", snd_len);

	/* Command to send to the smart card (must be 5 bytes) */
	memset(cmd, 0, sizeof(cmd));
	if (snd_len == 4)
	{
		/* Cla + Ins + P1 + P2 + 0 */
		memcpy(cmd, snd_buf, 4);
		snd_buf += 4;
		snd_len -= 4;

		*rcv_len = 0;
	}
	else
	{
		memcpy(cmd, snd_buf, 5);
		snd_buf += 5;
		snd_len -= 5;

		/* Cla + Ins + P1 + P2 + Le */
		if (snd_len == 0)
		{
			/* zero is 256 bytes */
			if (cmd[4] == 0)
				*rcv_len = 0x100;
			else
				*rcv_len = cmd[4];

			/* Rutoken bug! */
			if (cmd[1] == 0xA4) /* Ins == 0xA4 */
			{
				cmd[4] = 0x20;
				*rcv_len = 0x20;
			}
		}

		/* Cla + Ins + P1 + P2 + Lc + Data */
		if ((snd_len > 0) && (snd_len == cmd[4]))
			*rcv_len = 0;

		/* Cla + Ins + P1 + P2 + Lc + Data + Le */
		if ((snd_len > 0) && (snd_len == cmd[4] + 1))
		{
			snd_len--;

			/* zero is 256 bytes */
			if (snd_buf[snd_len] == 0)
				*rcv_len = 0x100;
			else
				*rcv_len = snd_buf[snd_len];			
		}
	}

	/* wait for ready */
	return_value = CmdGetSlotStatus(reader_index, pcbuffer);
	if (return_value != IFD_SUCCESS)
		return return_value;

	/* at most 5 bytes */
	return_value = CCID_Transmit(reader_index, 5, cmd, 0, 0);
	if (return_value != IFD_SUCCESS)
		return return_value;

	/* wait for ready */
	return_value = CmdGetSlotStatus(reader_index, pcbuffer);
	if (return_value != IFD_SUCCESS)
		return return_value;

	if (snd_len == 0)
	{
		if (*rcv_len == 0)
		{
			/* Cla + Ins + P1 + P2 + 0 */

			/* read SW1-SW2 */
			if (0x20 == pcbuffer[0])
			{
				return_value = CCID_Receive(reader_index, &sw_len, sw_buf, NULL);
				if (return_value != IFD_SUCCESS)
					return return_value;
			}
			else
			{
				unsigned int nlength = sizeof(pcbuffer);
				int PowerOnVoltage = VOLTAGE_5V;

				if (CmdPowerOn(reader_index, &nlength, pcbuffer, PowerOnVoltage) != IFD_SUCCESS)
				{
					DEBUG_CRITICAL("PowerUp failed");
					return IFD_ERROR_POWER_ACTION;
				}

				return IFD_COMMUNICATION_ERROR;
			}
		}
		else
		{
			/* Cla + Ins + P1 + P2 + Le */

			/* receive answer */
			if (0x10 == pcbuffer[0])
			{
				return_value = CCID_Receive(reader_index, rcv_len, rcv_buf, NULL);
				if (return_value != IFD_SUCCESS)
					return return_value;

				/* wait for ready */
				return_value = CmdGetSlotStatus(reader_index, pcbuffer);
				if (return_value != IFD_SUCCESS)
					return return_value;
			}

			/* read SW1-SW2 */
			if (0x20 == pcbuffer[0])
			{
				return_value = CCID_Receive(reader_index, &sw_len, sw_buf, NULL);
				if (return_value != IFD_SUCCESS)
					return return_value;
			}
			else
			{
				unsigned int nlength = sizeof(pcbuffer);
				int PowerOnVoltage = VOLTAGE_5V;

				if (CmdPowerOn(reader_index, &nlength, pcbuffer, PowerOnVoltage) != IFD_SUCCESS) 
				{
					DEBUG_CRITICAL("PowerUp failed");
					return IFD_ERROR_POWER_ACTION;
				}

				return IFD_COMMUNICATION_ERROR;
			}
		}
	}
	else
	{
		/* continue sending the APDU */
		if (0x10 == pcbuffer[0])
		{
			return_value = CCID_Transmit(reader_index, snd_len, snd_buf, 0, 0);
			if (return_value != IFD_SUCCESS)
				return return_value;
	
			/* wait for ready */
			return_value = CmdGetSlotStatus(reader_index, pcbuffer);
			if (return_value != IFD_SUCCESS)
				return return_value;
		}

		/* read SW1-SW2 */
		if (0x20 == pcbuffer[0])
		{
			return_value = CCID_Receive(reader_index, &sw_len, sw_buf, NULL);
			if (return_value != IFD_SUCCESS)
				return return_value;
		}
		else
		{
			unsigned int nlength = sizeof(pcbuffer);
			int PowerOnVoltage = VOLTAGE_5V;

			if (CmdPowerOn(reader_index, &nlength, pcbuffer, PowerOnVoltage) != IFD_SUCCESS)
			{
				DEBUG_CRITICAL("PowerUp failed");
				return IFD_ERROR_POWER_ACTION;
			}

			return IFD_COMMUNICATION_ERROR;
		}

		if ((sw_buf[0] == 0x90) && (sw_buf[1] == 0x00) && (*rcv_len > 0))
		{
			memset(cmd, 0, sizeof(cmd));
			cmd[1] = 0xC0;
			cmd[4] = *rcv_len;

			return CmdXfrBlockCHAR_T0(reader_index, 5, cmd, rcv_len, rcv_buf);
		}
	}

	/* add SW to respond */
	memcpy((char *)rcv_buf + *rcv_len, sw_buf, sw_len);
	*rcv_len += 2;

	return return_value;
} /* CmdXfrBlockCHAR_T0 */


/*****************************************************************************
 *
 *					CmdXfrBlockTPDU_T1
 *
 ****************************************************************************/
static RESPONSECODE CmdXfrBlockTPDU_T1(unsigned int reader_index,
	unsigned int tx_length, unsigned char tx_buffer[], unsigned int *rx_length,
	unsigned char rx_buffer[])
{
	RESPONSECODE return_value = IFD_SUCCESS;
	int ret;

	DEBUG_COMM3("T=1: %d and %d bytes", tx_length, *rx_length);

	ret = t1_transceive(&((get_ccid_slot(reader_index)) -> t1), 0,
		tx_buffer, tx_length, rx_buffer, *rx_length);

	if (ret < 0)
		return_value = IFD_COMMUNICATION_ERROR;
	else
		*rx_length = ret;

	return return_value;
} /* CmdXfrBlockTPDU_T1 */


/*****************************************************************************
 *
 *					SetParameters
 *
 ****************************************************************************/
RESPONSECODE SetParameters(unsigned int reader_index, char protocol,
	unsigned int length, unsigned char buffer[])
{
	unsigned char cmd[10+CMD_BUF_SIZE];	/* CCID + APDU buffer */
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);

	DEBUG_COMM2("length: %d bytes", length);

	cmd[0] = 0x61; /* SetParameters */
	i2dw(length, cmd+1);	/* APDU length */
	cmd[5] = ccid_descriptor->bCurrentSlotIndex;	/* slot number */
	cmd[6] = (*ccid_descriptor->pbSeq)++;
	cmd[7] = protocol;	/* bProtocolNum */
	cmd[8] = cmd[9] = 0; /* RFU */

	/* check that the command is not too large */
	if (length > CMD_BUF_SIZE)
		return IFD_NOT_SUPPORTED;

	memcpy(cmd+10, buffer, length);

	if (WritePort(reader_index, 10+length, cmd) != STATUS_SUCCESS)
		return IFD_COMMUNICATION_ERROR;

	length = sizeof(cmd);
	if (ReadPort(reader_index, &length, cmd) != STATUS_SUCCESS)
		return IFD_COMMUNICATION_ERROR;

	if (length < STATUS_OFFSET+1)
	{
		DEBUG_CRITICAL2("Not enough data received: %d bytes", length);
		return IFD_COMMUNICATION_ERROR;
	}

	if (cmd[STATUS_OFFSET] & CCID_COMMAND_FAILED)
	{
		ccid_error(cmd[ERROR_OFFSET], __FILE__, __LINE__, __FUNCTION__);    /* bError */
		if (0x00 == cmd[ERROR_OFFSET])	/* command not supported */
			return IFD_NOT_SUPPORTED;
		else
			if ((cmd[ERROR_OFFSET] >= 1) && (cmd[ERROR_OFFSET] <= 127))
				/* a parameter is not changeable */
				return IFD_SUCCESS;
			else
				return IFD_COMMUNICATION_ERROR;
	}

	return IFD_SUCCESS;
} /* SetParameters */


/*****************************************************************************
 *
 *					isCharLevel
 *
 ****************************************************************************/
int isCharLevel(int reader_index)
{
	return CCID_CLASS_CHARACTER == (get_ccid_descriptor(reader_index)->dwFeatures & CCID_CLASS_EXCHANGE_MASK);
} /* isCharLevel */


/*****************************************************************************
 *
 *					i2dw
 *
 ****************************************************************************/
static void i2dw(int value, unsigned char buffer[])
{
	buffer[0] = value & 0xFF;
	buffer[1] = (value >> 8) & 0xFF;
	buffer[2] = (value >> 16) & 0xFF;
	buffer[3] = (value >> 24) & 0xFF;
} /* i2dw */

const char *ct_hexdump(const void *data, size_t len)
{
	static char string[1024];
	unsigned char *d = (unsigned char *)data;
	unsigned int i, left;

	string[0] = '\0';
	left = sizeof(string);
	for (i = 0; len--; i += 3) {
		if (i >= sizeof(string) - 4)
			break;
		snprintf(string + i, 4, " %02x", *d++);
	}
	return string;
}

#define MAX_BUF_T0_LEN  256
#define T0_HDR_LEN      5

#define USB_ICC_POWER_ON	0x62
#define USB_ICC_POWER_OFF	0x63
#define USB_ICC_XFR_BLOCK	0x65
#define USB_ICC_DATA_BLOCK	0x6F
#define USB_ICC_GET_STATUS	0xA0

#define ICC_STATUS_IDLE			0x00
#define ICC_STATUS_READY_DATA	0x10
#define ICC_STATUS_READY_SW		0x20
#define ICC_STATUS_BUSY_COMMON	0x40
#define ICC_STATUS_MUTE			0x80

#define OUR_ATR_LEN	19


static int rutoken_send(unsigned int reader_index, unsigned int dad,
		const unsigned char *buffer, size_t len)
{
	if(IFD_SUCCESS == CCID_Transmit(reader_index, len,	buffer, /* (unsigned short) */0, 0))
	{
		
		return (int)len;
	
	}
	else
		return -1;
}

static int rutoken_recv(unsigned int reader_index, unsigned int dad,
		unsigned char *buffer, size_t len, long timeout)
{
	if(IFD_SUCCESS == CCID_Receive(reader_index, &len, buffer, 0))
		return len;
	else
		return -1;

}

static int rutoken_getstatus(unsigned int reader_index, unsigned char *status)
{
	unsigned char buffer[8];
	if(IFD_SUCCESS == CmdGetSlotStatus( reader_index, buffer))
	{	
		*status = buffer[0];
		return *status;
	
	}
	else
		return -1;
}

static int rutoken_recv_sw(unsigned int reader_index, int dad, unsigned char *sw)
{
	unsigned char status;
	if(rutoken_getstatus(reader_index, &status) == ICC_STATUS_MUTE)
	{  //If device not responsive
		DEBUG_INFO("status = ICC_STATUS_MUTE");
		
		return -1;
	}
	if(status == ICC_STATUS_READY_SW)
	{
		DEBUG_INFO("status = ICC_STATUS_READY_SW;");
		if(rutoken_recv(reader_index, 0, sw, 2, 10000) < 0)
			return -5;
		
		DEBUG_INFO3("Get SW %x %x", sw[0], sw[1]);
		return 2;
	}
	return -1;
}

// return how mach byte send
// sbuf - APDU bufer
// slen
static int rutoken_send_tpducomand(unsigned int reader_index, int dad, const void *sbuf, 
		size_t slen, void *rbuf, size_t rlen, int iscase4)
{
	int rrecv = 0;
	unsigned char status;
	unsigned char sw[2];
	ifd_iso_apdu_t iso;
	DEBUG_INFO3("send tpdu command %s, len: %d", ct_hexdump(sbuf, slen), slen);
	
	if ( ifd_iso_apdu_parse(sbuf, slen, &iso) < 0)
		return -1;
	
	unsigned char hdr[T0_HDR_LEN]={iso.cla, iso.ins, iso.p1, iso.p2, 0};
	switch(iso.cse){
		case	IFD_APDU_CASE_1:
			// {cla, ins, p1, p2, 0};
			DEBUG_INFO("case 1");
			break;
		case    IFD_APDU_CASE_2S:
			// {cla, ins, p1, p2, le};
			// Rutoken Bug!!!
			DEBUG_INFO("case 2");
			/* select file */
			if (iso.cla == 0 && iso.ins == 0xa4)
				iso.le = 0x20;
			/* get_do_info */
			else if (iso.cla == 0x80 && iso.ins == 0x30)
				iso.le = 0xff;
			hdr[4] = iso.le;
			break;
		case    IFD_APDU_CASE_3S:
			// {cla, ins, p1, p2, lc};
			DEBUG_INFO("case 3");
			hdr[4] = iso.lc;
			break;
		default:
			break;
	}
	//send TPDU header
	
	if (rutoken_send(reader_index, 0, hdr, T0_HDR_LEN) < 0)
		return -1;
	
	// send TPDU data or get answere and sw
	switch(iso.cse){
		case	IFD_APDU_CASE_1:
			// get sw
			if (rutoken_recv_sw(reader_index, 0, sw) < 0)
				return -2;
			break;
		case    IFD_APDU_CASE_2S:
		{
			// get answere
			DEBUG_INFO("get Data");
			DEBUG_INFO2("get Data %d", iso.le);
			if(rutoken_getstatus(reader_index, &status) == ICC_STATUS_READY_DATA)
			{
				rrecv = rutoken_recv(reader_index, 0, rbuf, iso.le, 10000);
				if (rrecv < 0)
					return -2;
				DEBUG_INFO2("get TPDU Anser %s", 
						ct_hexdump(rbuf, iso.le));
			}
			if (rutoken_recv_sw(reader_index, 0, sw) < 0)
				return -2;
			if ( sw[0] == 0x67) {
				// Le definitely not accepted
				break;
			}
			if ( (sw[0] == 0x6c) ) {
				unsigned char sbuftmp[slen];
				memcpy(sbuftmp, sbuf, slen);
				sbuftmp[4] = sw[1];
				return rutoken_send_tpducomand(reader_index, dad, sbuftmp, 
						slen, rbuf,  rlen, 0);
			}
		};
			break;
		case    IFD_APDU_CASE_3S:
			// send data
			DEBUG_INFO2("send Data %d", iso.lc);
			
			if(rutoken_getstatus(reader_index, &status) == ICC_STATUS_READY_DATA)
			{
				DEBUG_INFO2("send TPDU Data %s", 
						ct_hexdump(iso.data, iso.lc));
				if (rutoken_send(reader_index, 0, iso.data, iso.lc) < 0)
					return -4;
			} else return -3;
			// get sw
			if (rutoken_recv_sw(reader_index, 0, sw) < 0)
				return -2;

			// NOT STANDART TPDU!!! BEGIN
			if ( sw[0]== 0x61){
				unsigned char lx = sw[1];
				hdr[0] = 0x00;  //  iso.cla; (ruTokens specific)
				hdr[1] = 0xc0; // ins get response
				hdr[2] = 0; // p1
				hdr[3] = 0; // p2
				hdr[4] = lx ; //lx (case 2)
				if(iscase4)
					return rutoken_send_tpducomand(reader_index, dad, hdr, 
							T0_HDR_LEN, rbuf, rlen, 0);
				else {
					int recvtmp = rutoken_send_tpducomand(reader_index,dad,
							hdr, T0_HDR_LEN, rbuf, rlen, 0);
					rrecv = 0;
					memcpy(sw, (unsigned char*)rbuf+recvtmp-2, 2);
					break;
				}
			}

			if ( (sw[0] == 0x90) && (sw[1] == 0x00))
			{
				hdr[0] = 0x00; //iso.cla;
				hdr[1] = 0xc0; // ins get response
				hdr[2] = 0; // p1
				hdr[3] = 0; // p2
				hdr[4] = iso.le; // le (case 2)
				if(iscase4)
					return rutoken_send_tpducomand(reader_index, dad, hdr, 
							T0_HDR_LEN, rbuf, rlen, 0);
			}
			// NOT STANDART TPDU!!! END

			break;
		default:
			break;
	}
	// Add SW to respond
	DEBUG_INFO("add SW");
	DEBUG_INFO2("add SW, rrecv=%d",rrecv);
	memcpy(((char *)rbuf)+rrecv, sw, 2);
	rrecv+=2;
	DEBUG_INFO2("recv %d bytes", rrecv);
	return rrecv;
}

static int rutoken_transparent( unsigned int reader_index, int dad,
		const void *sbuf, size_t slen,
		void *rbuf, size_t rlen)
{
	unsigned char sw[2], *send_buf_trn = NULL;
	const void *send_buf = sbuf;
	int len, rrecv = -1, iscase4 = 0;
	ifd_iso_apdu_t iso;

	DEBUG_INFO3("buffer %s rlen = %d", ct_hexdump(sbuf, slen), rlen);
	if ( ifd_iso_apdu_parse(sbuf, slen, &iso) < 0)
		return -1;
	DEBUG_INFO2("iso.le = %d", iso.le);

	if (iso.cla == 0 && slen > 5) {
		send_buf_trn = malloc(slen);
		if (!send_buf_trn) {
			DEBUG_INFO2("out of memory (slen = %u)", slen);
			return -1;
		}
		memcpy(send_buf_trn, sbuf, slen);
		/* select file, delete file */
		if (iso.ins == 0xa4 || iso.ins == 0xe4)
			swap_pair(send_buf_trn + 5, slen - 5);
		/* create file */
		else if (iso.ins == 0xe0) {
			len = convert_fcp_to_rtprot(send_buf_trn + 5, slen - 5);
			DEBUG_INFO2("convert_fcp_to_rtprot = %i", len);
			if (len > 0) {
				slen = len + 5;
				send_buf_trn[4] = len; /* replace le */
			}
		}
		/* create_do, key_gen */
		else if (iso.ins == 0xda && iso.p1 == 1
				&& (iso.p2 == 0x65 || iso.p2 == 0x62)) {
			len = convert_doinfo_to_rtprot(send_buf_trn + 5, slen - 5);
			DEBUG_INFO2("convert_doinfo_to_rtprot = %i", len);
			if (len > 0) {
				slen = len + 5;
				send_buf_trn[4] = len; /* replace le */
			}
		}
		DEBUG_INFO2("le = %u", send_buf_trn[4]);
		send_buf = send_buf_trn;
	}
	switch(iso.cse){
		case	IFD_APDU_CASE_2S:
		case	IFD_APDU_CASE_3S:
			if (iso.cla == 0 && iso.ins == 0xa4)
				iscase4 = 1; /* FIXME: */
		case	IFD_APDU_CASE_1:
			rrecv = rutoken_send_tpducomand(reader_index, dad, send_buf, slen,
					rbuf, rlen, iscase4);
			break;
		case	IFD_APDU_CASE_4S:
			// make send case 4 command
			rrecv = rutoken_send_tpducomand(reader_index, dad, send_buf, slen-1,
					rbuf, rlen, 1);
			break;
		default:
			break;
	}
	if (send_buf_trn)
		free(send_buf_trn);

	if (rrecv > 0 && (size_t)rrecv >= sizeof(sw)) {
		memcpy(sw, (unsigned char*)rbuf + rrecv - sizeof(sw), sizeof(sw));
		if (sw[0] != 0x90 || sw[1] != 0)
			/* do nothing */;
		/* select file */
		else if (iso.cla == 0 && iso.ins == 0xa4
				&& rrecv == sizeof(sw) + 32 /* size rtprot */) {
			len = convert_rtprot_to_fcp(rbuf, rlen);
			DEBUG_INFO2("convert_rtprot_to_fcp = %i", len);
			if (len > 0) {
				rrecv = -1;
				if (rlen >= len + sizeof(sw)) {
					memcpy((unsigned char*)rbuf+len, sw, sizeof(sw));
					rrecv = len + sizeof(sw);
				}
			}
		}
		/* get_do_info */
		else if (iso.cla == 0x80 && iso.ins == 0x30
				&& (size_t)rrecv >= sizeof(sw) + 32 /* size rtprot */) {
			len = convert_rtprot_to_doinfo(rbuf, rlen);
			DEBUG_INFO2("convert_rtprot_to_doinfo = %i", len);
			if (len > 0) {
				rrecv = -1;
				if (rlen >= len + sizeof(sw)) {
					memcpy((unsigned char*)rbuf+len, sw, sizeof(sw));
					rrecv = len + sizeof(sw);
				}
			}
		}
		else if (iso.cla == 0 && iso.ins == 0xca && iso.p1 == 1) {
			/* get_serial, get_free_mem */
			if (iso.p2 == 0x81 || iso.p2 == 0x8a)
				swap_four(rbuf, rrecv - sizeof(sw));
			/* get_current_ef */
			else if (iso.p2 == 0x11)
				swap_pair(rbuf, rrecv - sizeof(sw));
		}
	}
	return rrecv;
}

static void swap_pair(unsigned char *buf, size_t len)
{
	size_t i;
	unsigned char tmp;

	for (i = 0; i + 1 < len; i += 2) {
		tmp = buf[i];
		buf[i] = buf[i + 1];
		buf[i + 1] = tmp;
	}
}

static void swap_four(unsigned char *buf, size_t len)
{
	size_t i;
	unsigned char tmp;

	for (i = 0; i + 3 < len; i += 4) {
		tmp = buf[i];
		buf[i] = buf[i + 3];
		buf[i + 3] = tmp;

		swap_pair(&buf[i + 1], 2);
	}
}

static int read_tag(unsigned char *buf, size_t buf_len,
		unsigned char tag_in, unsigned char *out, size_t out_len)
{
	unsigned char tag;
	size_t taglen, i = 0;

	while (i + 2 <= buf_len) {
		tag = buf[i];
		taglen = buf[i + 1];
		i += 2;
		if (taglen + i > buf_len)
			return -1;
		if (tag == tag_in) {
			if (taglen != out_len)
				return -1;
			memcpy(out, buf + i, out_len);
			return 0;
		}
		i += taglen;
	}
	return -1;
}

static int convert_doinfo_to_rtprot(void *data, size_t data_len)
{
	unsigned char dohdr[32] = { 0 };
	unsigned char secattr[40], data_a5[0xff];
	unsigned char *p = data;
	size_t i, data_a5_len;

	if (read_tag(p, data_len, 0x80, &dohdr[0], 2) == 0) {
		swap_pair(&dohdr[0], 2);
		DEBUG_INFO3("tag 0x80 (file size) = %02x %02x", dohdr[0], dohdr[1]);
	}
	data_a5_len = dohdr[1] & 0xff;
	if (read_tag(p, data_len, 0xA5, data_a5, data_a5_len) == 0)
		DEBUG_INFO2("tag 0xA5 = %s", ct_hexdump(data_a5, data_a5_len));
	else
		data_a5_len = 0;
	if (data_len < sizeof(dohdr) + data_a5_len) {
		DEBUG_INFO2("data_len = %u", data_len);
		return -1;
	}
	if (read_tag(p, data_len, 0x83, &dohdr[2], 2) == 0)
		DEBUG_INFO3("tag 0x83 (Type,ID) = %02x %02x", dohdr[2], dohdr[3]);
	if (read_tag(p, data_len, 0x85, &dohdr[4], 3) == 0)
		/* ifd_debug(6, "tag 0x85 (Opt,Flags,MaxTry) = %02x %02x %02x",
				dohdr[4], dohdr[5], dohdr[6]) */;
	if (read_tag(p, data_len, 0x86, secattr, sizeof(secattr)) == 0) {
		i = 17;
		memcpy(dohdr + i, secattr, 8);
		for (i += 8, p = &secattr[8]; i < sizeof(dohdr); ++i, p += 4)
			dohdr[i] = *p;
		DEBUG_INFO2("tag 0x86 = %s", ct_hexdump(&dohdr[17], 15));
	}
	memcpy(data, dohdr, sizeof(dohdr));
	memcpy((unsigned char*)data + sizeof(dohdr), data_a5, data_a5_len);
	return sizeof(dohdr) + data_a5_len;
}

static int convert_fcp_to_rtprot(void *data, size_t data_len)
{
	unsigned char rtprot[32] = { 0 };
	unsigned char secattr[40];
	unsigned char *p = data;
	size_t i;

	if (data_len < sizeof(rtprot)) {
		DEBUG_INFO2("data_len = %u", data_len);
		return -1;
	}
	/* 0x62 - FCP */
	if (p[0] != 0x62  ||  (size_t)p[1] + 2 > data_len) {
		DEBUG_INFO3("Tag = %02x  len = %u", p[0], p[1]);
		return -1;
	}
	p += 2;
	data_len -= 2;
	/* file type */
	if (read_tag(p, data_len, 0x82, &rtprot[4], 2) != 0)
		return -1;
	DEBUG_INFO3("tag 0x82 (file type) = %02x %02x", rtprot[4], rtprot[5]);
	/* file id */
	if (read_tag(p, data_len, 0x83, &rtprot[6], 2) != 0)
		return -1;
	swap_pair(&rtprot[6], 2);
	DEBUG_INFO3("tag 0x83 (file id) = %02x %02x", rtprot[6], rtprot[7]);
	/* file size */
	if (read_tag(p, data_len, 0x81, &rtprot[0], 2) == 0) {
		swap_pair(&rtprot[0], 2);
		DEBUG_INFO3("tag 0x81 (complete file size) = %02x %02x",
				rtprot[0], rtprot[1]);
	}
	if (read_tag(p, data_len, 0x80, &rtprot[2], 2) == 0) {
		swap_pair(&rtprot[2], 2);
		DEBUG_INFO3("tag 0x80 (file size) = %02x %02x", rtprot[2], rtprot[3]);
	}
	if (read_tag(p, data_len, 0x86, secattr, sizeof(secattr)) == 0) {
		i = 17;
		memcpy(rtprot + i, secattr, 8);
		for (i += 8, p = &secattr[8]; i < sizeof(rtprot); ++i, p += 4)
			rtprot[i] = *p;
		DEBUG_INFO2("tag 0x86 = %s", ct_hexdump(&rtprot[17], 15));
	}
	memcpy(data, rtprot, sizeof(rtprot));
	return sizeof(rtprot);
}

static int convert_rtprot_to_doinfo(void *data, size_t data_len)
{
	unsigned char doinfo[0xff] = { 0 };
	unsigned char *pdata = data;
	size_t i, doinfo_len = 0;

	if (data_len < 32) {
		DEBUG_INFO2("data_len = %u", data_len);
		return -1;
	}
	if (pdata[0] != 0 && pdata[0] < sizeof(doinfo) - 4 - 4 - 5 - 42 - 2) {
		/* Tag 0x80 */
		doinfo[doinfo_len++] = 0x80;
		doinfo[doinfo_len++] = 2;
		memcpy(doinfo + doinfo_len, pdata, 2);
		swap_pair(doinfo + doinfo_len, 2);
		doinfo_len += 2;
	}
	/* Tag 0x83 */
	doinfo[doinfo_len++] = 0x83;
	doinfo[doinfo_len++] = 2;
	doinfo[doinfo_len++] = pdata[2];
	doinfo[doinfo_len++] = pdata[3];

	/* Tag 0x85 */
	doinfo[doinfo_len++] = 0x85;
	doinfo[doinfo_len++] = 3;
	doinfo[doinfo_len++] = pdata[4];
	doinfo[doinfo_len++] = pdata[5];
	doinfo[doinfo_len++] = pdata[6];

	/* Tag 0x86 */
	doinfo[doinfo_len++] = 0x86;
	doinfo[doinfo_len++] = 40;
	memcpy(doinfo + doinfo_len, pdata + 17, 8);
	doinfo_len += 8;
	for (i = 0; i < 7 && doinfo_len + 3 < sizeof(doinfo); ++i, doinfo_len += 4)
		doinfo[doinfo_len] = pdata[17 + 8 + i];
	doinfo_len += 4; /* for reserved */
	if (pdata[0] != 0 && pdata[0] + doinfo_len + 2 < sizeof(doinfo)) {
		/* Tag 0xA5 */
		if (data_len - 32 < pdata[0]) {
			DEBUG_INFO2("for tag 0xA5 incorrect data_len = %u", data_len);
			return -1;
		}
		doinfo[doinfo_len++] = 0xA5;
		doinfo[doinfo_len++] = pdata[0];
		memcpy(doinfo + doinfo_len, pdata + 32, pdata[0]);
		doinfo_len += pdata[0];
	}
	DEBUG_INFO2("doinfo = %s", ct_hexdump(doinfo, doinfo_len));
	memcpy(data, doinfo, doinfo_len);
	return doinfo_len;
}

static int convert_rtprot_to_fcp(void *data, size_t data_len)
{
	unsigned char fcp[63] = {
		0x62, sizeof(fcp) - 2,
		0x81, 2, 0, 0,
		0x80, 2, 0, 0,
		0x82, 2, 0, 0,
		0x83, 2, 0, 0,
		0x8A, 1, 0,
		0x86, 40
	};
	unsigned char *p = data;
	size_t i;

	if (data_len < sizeof(fcp)) {
		DEBUG_INFO2("data_len = %u", data_len);
		return -1;
	}
	/* Tag 0x81 */
	memcpy(fcp + 4, p, 2);
	swap_pair(fcp + 4, 2);
	/* Tag 0x80 */
	memcpy(fcp + 8, p + 2, 2);
	swap_pair(fcp + 8, 2);
	/* Tag 0x82 */
	memcpy(fcp + 12, p + 4, 2);
	/* Tag 0x83 */
	memcpy(fcp + 16, p + 6, 2);
	swap_pair(fcp + 16, 2);
	/* Tag 0x8A */
	fcp[20] = p[8];

	/* Tag 0x86 */
	memcpy(fcp + 23, p + 17, 8);
	for (i = 0; i < 7 && sizeof(fcp) > 23 + 8 + i * 4; ++i)
		fcp[23 + 8 + i * 4] = p[17 + 8 + i];
	DEBUG_INFO2("fcp = %s", ct_hexdump(fcp, sizeof(fcp)));
	memcpy(data, fcp, sizeof(fcp));
	return sizeof(fcp);
}

