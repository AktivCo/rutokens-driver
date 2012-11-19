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
#include "ccid.h"
#include "defs.h"
#include "ccid_ifdhandler.h"
#include "config.h"
#include "debug.h"
#include "ccid_usb.h"
#include "apdu.h"
#include "convert_apdu.h"

#define ICC_STATUS_IDLE			0x00
#define ICC_STATUS_READY_DATA	0x10
#define ICC_STATUS_READY_SW		0x20
#define ICC_STATUS_BUSY_COMMON	0x40
#define ICC_STATUS_MUTE			0x80

#define T0_HDR_LEN      5

#define USB_ICC_POWER_ON	0x62
#define USB_ICC_POWER_OFF	0x63
#define USB_ICC_XFR_BLOCK	0x65
#define USB_ICC_DATA_BLOCK	0x6F
#define USB_ICC_GET_STATUS	0xA0

#define max( a, b )   ( ( ( a ) > ( b ) ) ? ( a ) : ( b ) )
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define IFD_ERROR_INSUFFICIENT_BUFFER 700

/* internal functions */

RESPONSECODE CmdGetSlotStatus(unsigned int reader_index,
	unsigned char* status);

RESPONSECODE CCID_Transmit(unsigned int reader_index, unsigned int tx_length,
	const unsigned char tx_buffer[]);

RESPONSECODE CCID_Receive(unsigned int reader_index, unsigned int *rx_length,
	unsigned char rx_buffer[]);

RESPONSECODE CCID_Receive_SW(unsigned int reader_index, unsigned char sw[]);

static RESPONSECODE CmdXfrBlockCHAR_T0(unsigned int reader_index, unsigned int
	tx_length, unsigned char tx_buffer[], unsigned int *rx_length, unsigned
	char rx_buffer[]);

static int rutoken_send_tpducomand(unsigned int reader_index, int dad, const void *sbuf, 
		size_t slen, void *rbuf, size_t rlen, int iscase4);
static int rutoken_transparent( unsigned int reader_index, int dad,
		const void *sbuf, size_t slen,
		void *rbuf, size_t rlen);


/*****************************************************************************
 *
 *					isCharLevel
 *
 ****************************************************************************/
int isCharLevel(int reader_index) /* RT to remove */
{
	return CCID_CLASS_CHARACTER == (get_ccid_descriptor(reader_index)->dwFeatures & CCID_CLASS_EXCHANGE_MASK);
} /* isCharLevel */


/*****************************************************************************
 *
 *					CmdPowerOn
 *
 ****************************************************************************/
RESPONSECODE CmdPowerOn(unsigned int reader_index, unsigned int * nlength,
	unsigned char buffer[])
{
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	int r;
	unsigned char status;

	/* first power off to reset the ICC state machine */
	r = CmdPowerOff(reader_index);
	if (r != IFD_SUCCESS)
		return r;

	/* wait for ready */
	r = CmdGetSlotStatus(reader_index, &status);
	if (r != IFD_SUCCESS)
		return r;

	r = ControlUSB(reader_index, 0xC1, USB_ICC_POWER_ON, 0, buffer, RUTOKEN_ATR_LEN);
	/* we got an error? */
	if (r < 0)
	{
		DEBUG_INFO2("ICC Power On failed: %s", strerror(errno));
		return IFD_COMMUNICATION_ERROR;
	}

	*nlength = RUTOKEN_ATR_LEN;

	return IFD_SUCCESS;
} /* CmdPowerOn */


/*****************************************************************************
 *
 *					CmdPowerOff
 *
 ****************************************************************************/
RESPONSECODE CmdPowerOff(unsigned int reader_index)
{
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	int r;

	r = ControlUSB(reader_index, 0x41, USB_ICC_POWER_OFF, 0, NULL, 0);
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
RESPONSECODE CmdGetSlotStatus(unsigned int reader_index, unsigned char* status)
{
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	int r;

	r = ControlUSB(reader_index, 0xC1, USB_ICC_GET_STATUS, 0, status, sizeof(*status)); 
	/* we got an error? */
	if (r < 0)
	{
		DEBUG_INFO2("ICC Slot Status failed: %s", strerror(errno));
		if (ENODEV == errno)
			return IFD_NO_SUCH_DEVICE;
		return IFD_COMMUNICATION_ERROR;
	}

	if ((*status & 0xF0) == ICC_STATUS_BUSY_COMMON)
	{
		int i;
		unsigned char prev_status;
		DEBUG_INFO2("Busy: 0x%02X", *status);
		for (i = 0; i < 200; i++)
		{
			do 
			{
				usleep(10000);  /* 10 ms */
				prev_status = *status;

				r = ControlUSB(reader_index, 0xC1, USB_ICC_GET_STATUS, 0, status, sizeof(*status));
				/* we got an error? */
				if (r < 0)
				{
					DEBUG_INFO2("ICC Slot Status failed: %s", strerror(errno));
					if (ENODEV == errno)
						return IFD_NO_SUCH_DEVICE;
					return IFD_COMMUNICATION_ERROR;
				}

				if ((*status & 0xF0) != ICC_STATUS_BUSY_COMMON)
					return IFD_SUCCESS;
			} while ((((prev_status & 0x0F) + 1) & 0x0F) == (*status & 0x0F));
		}
		return IFD_COMMUNICATION_ERROR;
	}
	return IFD_SUCCESS;
} /* CmdGetSlotStatus */

/*****************************************************************************
 *
 *					CmdIccPresence
 *
 ****************************************************************************/
RESPONSECODE CmdIccPresence(unsigned int reader_index,
	unsigned char* presence)
{
	int r;
	unsigned char status;

	r = CmdGetSlotStatus(reader_index, &status);
	/* we got an error? */
	if(r != IFD_SUCCESS)
		return r;

	/* present and active by default */
	*presence = CCID_ICC_PRESENT_ACTIVE;

	if (ICC_STATUS_MUTE == status)
		*presence = CCID_ICC_ABSENT;

	return IFD_SUCCESS;
}/* CmdIccPresence */


/*****************************************************************************
 *
 *					CmdXfrBlock
 *
 ****************************************************************************/
RESPONSECODE CmdXfrBlock(unsigned int reader_index, unsigned int tx_length,
	unsigned char tx_buffer[], unsigned int *rx_length,
	unsigned char rx_buffer[], int protocol) /* RT remove protocol: we use T0 only */
{
	RESPONSECODE return_value = IFD_SUCCESS;
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);

	if (protocol == T_0)
		if((*rx_length = rutoken_transparent(reader_index, 0, tx_buffer, tx_length, rx_buffer,
				*rx_length)) > 0)
			return_value = IFD_SUCCESS;
	else
	{
		*rx_length = 0;
		return_value = IFD_PROTOCOL_NOT_SUPPORTED;
	}

	return return_value;
} /* CmdXfrBlock */


/*****************************************************************************
 *
 *					CCID_Transmit
 *
 ****************************************************************************/
RESPONSECODE CCID_Transmit(unsigned int reader_index, unsigned int tx_length,
	const unsigned char tx_buffer[])
{
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	unsigned char status;
	int r;

	/* Xfr Block */
	r = ControlUSB(reader_index, 0x41, USB_ICC_XFR_BLOCK, 0, (unsigned char*)tx_buffer, tx_length);
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
	unsigned char rx_buffer[])
{
	_ccid_descriptor *ccid_descriptor = get_ccid_descriptor(reader_index);
	int r;
	unsigned char status;

	/* Data Block */
	r = ControlUSB(reader_index, 0xC1, USB_ICC_DATA_BLOCK, 0, rx_buffer, *rx_length);
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
 *					CCID_Receive_SW
 *
 ****************************************************************************/
RESPONSECODE CCID_Receive_SW(unsigned int reader_index, unsigned char sw[])
{
	unsigned char status = 0;
	int sw_len = 2;
	int r = IFD_COMMUNICATION_ERROR;

	r = CmdGetSlotStatus(reader_index, &status);
	if (r != IFD_SUCCESS)
		return r;

	if(status == ICC_STATUS_MUTE)
	{  //If device not responsive
		DEBUG_INFO("status = ICC_STATUS_MUTE");
		return IFD_COMMUNICATION_ERROR;
	}
	if(status == ICC_STATUS_READY_SW)
	{
		DEBUG_INFO("status = ICC_STATUS_READY_SW;");

		if(CCID_Receive(reader_index, &sw_len, sw) != IFD_SUCCESS)
			return IFD_COMMUNICATION_ERROR;
		
		DEBUG_INFO3("Get SW %x %x", sw[0], sw[1]);
		return IFD_SUCCESS;
	}
	return IFD_COMMUNICATION_ERROR;
}/* CCID_Receive_SW */


// return how mach byte send
// sbuf - APDU bufer
// slen
static int rutoken_send_tpducomand(unsigned int reader_index, int dad, const void *sbuf, 
		size_t slen, void *rbuf, size_t rlen, int iscase4)
{
	int rrecv = 0;
	int r = 0;
	unsigned char status;
	unsigned char sw[2];
	ifd_iso_apdu_t iso;
	DEBUG_INFO3("send tpdu command %s, len: %d", array_hexdump(sbuf, slen), slen);
	
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
	
	if (CCID_Transmit(reader_index, T0_HDR_LEN, hdr) != IFD_SUCCESS)
		return -1;
	
	// send TPDU data or get answere and sw
	switch(iso.cse){
		case	IFD_APDU_CASE_1:
			// get sw
			if (CCID_Receive_SW(reader_index, sw) != IFD_SUCCESS)
				return -2;
			break;
		case    IFD_APDU_CASE_2S:
		{
			// get answere
			DEBUG_INFO("get Data");
			DEBUG_INFO2("get Data %d", iso.le);
			if((CmdGetSlotStatus(reader_index, &status) == IFD_SUCCESS) && (status == ICC_STATUS_READY_DATA))
			{
				rrecv = iso.le;
				r = CCID_Receive(reader_index, &rrecv, rbuf);
				if (r != IFD_SUCCESS)
					return -2;
				DEBUG_INFO2("get TPDU Anser %s", 
						array_hexdump(rbuf, iso.le));
			}
			if (CCID_Receive_SW(reader_index, sw) != IFD_SUCCESS)
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
			
			if((CmdGetSlotStatus(reader_index, &status) == IFD_SUCCESS ) && (status == ICC_STATUS_READY_DATA))
			{
				DEBUG_INFO2("send TPDU Data %s", 
						array_hexdump(iso.data, iso.lc));
				if (CCID_Transmit(reader_index, iso.lc, iso.data) != IFD_SUCCESS)
					return -4;
			} else return -3;
			// get sw
			if (CCID_Receive_SW(reader_index, sw) != IFD_SUCCESS)
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

	DEBUG_INFO3("buffer %s rlen = %d", array_hexdump(sbuf, slen), rlen);
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
