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

static RESPONSECODE CmdXfrBlockCHAR_T0(unsigned int reader_index, unsigned int
	tx_length, unsigned char tx_buffer[], unsigned int *rx_length, unsigned
	char rx_buffer[]);

const char *ct_hexdump(const void *data, size_t len);

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
	int sw_len = 2;
	if(rutoken_getstatus(reader_index, &status) == ICC_STATUS_MUTE)
	{  //If device not responsive
		DEBUG_INFO("status = ICC_STATUS_MUTE");
		
		return -1;
	}
	if(status == ICC_STATUS_READY_SW)
	{
		DEBUG_INFO("status = ICC_STATUS_READY_SW;");

		if(CCID_Receive(reader_index, &sw_len, sw) != IFD_SUCCESS)
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
	int r = 0;
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
	
	if (CCID_Transmit(reader_index, T0_HDR_LEN, hdr) != IFD_SUCCESS)
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
				rrecv = iso.le;
				r = CCID_Receive(reader_index, &rrecv, rbuf);
				if (r != IFD_SUCCESS)
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
				if (CCID_Transmit(reader_index, iso.lc, iso.data) != IFD_SUCCESS)
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

