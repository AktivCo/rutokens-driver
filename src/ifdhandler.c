/*
    ifdhandler.c: IFDH API
    Copyright (C) 2003-2008   Ludovic Rousseau

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

/* $Id: ifdhandler.c 3025 2008-06-26 13:38:50Z rousseau $ */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "misc.h"
#include "config.h"
#include <pcsclite.h>
#include <ifdhandler.h>
#include <reader.h>

#include "ccid.h"
#include "defs.h"
#include "ccid_ifdhandler.h"
#include "debug.h"
#include "utils.h"
#include "commands.h"
#include "towitoko/atr.h"
#include "towitoko/pps.h"
#include "parser.h"

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

/* Array of structures to hold the ATR and other state value of each slot */
static CcidDesc CcidSlots[CCID_DRIVER_MAX_READERS];

/* global mutex */
#ifdef HAVE_PTHREAD
static pthread_mutex_t ifdh_context_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

int LogLevel = DEBUG_LEVEL_CRITICAL | DEBUG_LEVEL_INFO;
int DriverOptions = 0;
int PowerOnVoltage = VOLTAGE_5V;
static int DebugInitialized = FALSE;

/* local functions */
#if HAVE_DECL_TAG_IFD_POLLING_THREAD && !defined(TWIN_SERIAL) && defined(USE_USB_INTERRUPT)
static RESPONSECODE IFDHPolling(DWORD Lun);
static RESPONSECODE IFDHSleep(DWORD Lun);
#endif
static void init_driver(void);
static void extra_egt(ATR_t *atr, _ccid_descriptor *ccid_desc, DWORD Protocol);
static char find_baud_rate(unsigned int baudrate, unsigned int *list);
static unsigned int T0_card_timeout(double f, double d, int TC1, int TC2,
	int clock_frequency);
static unsigned int T1_card_timeout(double f, double d, int TC1, int BWI,
	int CWI, int clock_frequency);


EXTERNAL RESPONSECODE IFDHCreateChannelByName(DWORD Lun, LPSTR lpcDevice)
{
	RESPONSECODE return_value = IFD_SUCCESS;
	int reader_index;

	if (! DebugInitialized)
		init_driver();

	DEBUG_INFO3("lun: %X, device: %s", Lun, lpcDevice);

	if (-1 == (reader_index = GetNewReaderIndex(Lun)))
		return IFD_COMMUNICATION_ERROR;

	/* Reset ATR buffer */
	CcidSlots[reader_index].nATRLength = 0;
	*CcidSlots[reader_index].pcATRBuffer = '\0';

	/* Reset PowerFlags */
	CcidSlots[reader_index].bPowerFlags = POWERFLAGS_RAZ;

#ifdef HAVE_PTHREAD
	pthread_mutex_lock(&ifdh_context_mutex);
#endif

	if (OpenPortByName(reader_index, lpcDevice) != STATUS_SUCCESS)
	{
		DEBUG_CRITICAL("failed");
		return_value = IFD_COMMUNICATION_ERROR;

		/* release the allocated reader_index */
		ReleaseReaderIndex(reader_index);
	}
	else
	{
		/* Try to access the reader */
		/* This "warm up" sequence is sometimes needed when pcscd is
		 * restarted with the reader already connected. We get some
		 * "usb_bulk_read: Resource temporarily unavailable" on the first
		 * few tries. It is an empirical hack */
		if ((IFD_COMMUNICATION_ERROR == IFDHICCPresence(Lun))
			&& (IFD_COMMUNICATION_ERROR == IFDHICCPresence(Lun))
			&& (IFD_COMMUNICATION_ERROR == IFDHICCPresence(Lun)))
		{
			DEBUG_CRITICAL("failed");
			return_value = IFD_COMMUNICATION_ERROR;

			/* release the allocated reader_index */
			ReleaseReaderIndex(reader_index);
		}
	}

#ifdef HAVE_PTHREAD
	pthread_mutex_unlock(&ifdh_context_mutex);
#endif

	return return_value;
} /* IFDHCreateChannelByName */


EXTERNAL RESPONSECODE IFDHCreateChannel(DWORD Lun, DWORD Channel)
{
	/*
	 * Lun - Logical Unit Number, use this for multiple card slots or
	 * multiple readers. 0xXXXXYYYY - XXXX multiple readers, YYYY multiple
	 * slots. The resource manager will set these automatically.  By
	 * default the resource manager loads a new instance of the driver so
	 * if your reader does not have more than one smartcard slot then
	 * ignore the Lun in all the functions. Future versions of PC/SC might
	 * support loading multiple readers through one instance of the driver
	 * in which XXXX would be important to implement if you want this.
	 */

	/*
	 * Channel - Channel ID.  This is denoted by the following: 0x000001 -
	 * /dev/pcsc/1 0x000002 - /dev/pcsc/2 0x000003 - /dev/pcsc/3
	 *
	 * USB readers may choose to ignore this parameter and query the bus
	 * for the particular reader.
	 */

	/*
	 * This function is required to open a communications channel to the
	 * port listed by Channel.  For example, the first serial reader on
	 * COM1 would link to /dev/pcsc/1 which would be a sym link to
	 * /dev/ttyS0 on some machines This is used to help with intermachine
	 * independance.
	 *
	 * Once the channel is opened the reader must be in a state in which
	 * it is possible to query IFDHICCPresence() for card status.
	 *
	 * returns:
	 *
	 * IFD_SUCCESS IFD_COMMUNICATION_ERROR
	 */
	RESPONSECODE return_value = IFD_SUCCESS;
	int reader_index;

	if (! DebugInitialized)
		init_driver();

	DEBUG_INFO2("lun: %X", Lun);

	if (-1 == (reader_index = GetNewReaderIndex(Lun)))
		return IFD_COMMUNICATION_ERROR;

	/* Reset ATR buffer */
	CcidSlots[reader_index].nATRLength = 0;
	*CcidSlots[reader_index].pcATRBuffer = '\0';

	/* Reset PowerFlags */
	CcidSlots[reader_index].bPowerFlags = POWERFLAGS_RAZ;

#ifdef HAVE_PTHREAD
	pthread_mutex_lock(&ifdh_context_mutex);
#endif

	if (OpenPort(reader_index, Channel) != STATUS_SUCCESS)
	{
		DEBUG_CRITICAL("failed");
		return_value = IFD_COMMUNICATION_ERROR;

		/* release the allocated reader_index */
		ReleaseReaderIndex(reader_index);
	}

#ifdef HAVE_PTHREAD
	pthread_mutex_unlock(&ifdh_context_mutex);
#endif

	return return_value;
} /* IFDHCreateChannel */


EXTERNAL RESPONSECODE IFDHCloseChannel(DWORD Lun)
{
	/*
	 * This function should close the reader communication channel for the
	 * particular reader.  Prior to closing the communication channel the
	 * reader should make sure the card is powered down and the terminal
	 * is also powered down.
	 *
	 * returns:
	 *
	 * IFD_SUCCESS IFD_COMMUNICATION_ERROR
	 */
	int reader_index;

	DEBUG_INFO2("lun: %X", Lun);

	if (-1 == (reader_index = LunToReaderIndex(Lun)))
		return IFD_COMMUNICATION_ERROR;

	/* Restore the default timeout
	 * No need to wait too long if the reader disapeared */
	get_ccid_descriptor(reader_index)->readTimeout = DEFAULT_COM_READ_TIMEOUT;

	(void)CmdPowerOff(reader_index);
	/* No reader status check, if it failed, what can you do ? :) */

#ifdef HAVE_PTHREAD
	pthread_mutex_lock(&ifdh_context_mutex);
#endif

	(void)ClosePort(reader_index);
	ReleaseReaderIndex(reader_index);

#ifdef HAVE_PTHREAD
	pthread_mutex_unlock(&ifdh_context_mutex);
#endif

	return IFD_SUCCESS;
} /* IFDHCloseChannel */


#if HAVE_DECL_TAG_IFD_POLLING_THREAD  && defined(USE_USB_INTERRUPT)
static RESPONSECODE IFDHPolling(DWORD Lun)
{
	int reader_index;
	int ret;

	if (-1 == (reader_index = LunToReaderIndex(Lun)))
		return IFD_COMMUNICATION_ERROR;

	/* log only if DEBUG_LEVEL_PERIODIC is set */
	if (LogLevel & DEBUG_LEVEL_PERIODIC)
		DEBUG_INFO2("lun: %X", Lun);

	ret = InterruptRead(reader_index);
	if (ret > 0)
		return IFD_SUCCESS;
	if (0 == ret)
		return IFD_NO_SUCH_DEVICE;
	return IFD_COMMUNICATION_ERROR;
}

/* on an ICCD device the card is always inserted
 * so no card movement will ever happen: just do nothing */
static RESPONSECODE IFDHSleep(DWORD Lun)
{
	DEBUG_INFO2("lun: %X", Lun);
	sleep(60*60);	/* 1 hour */
	return IFD_SUCCESS;
}
#endif


EXTERNAL RESPONSECODE IFDHGetCapabilities(DWORD Lun, DWORD Tag,
	PDWORD Length, PUCHAR Value)
{
	/*
	 * This function should get the slot/card capabilities for a
	 * particular slot/card specified by Lun.  Again, if you have only 1
	 * card slot and don't mind loading a new driver for each reader then
	 * ignore Lun.
	 *
	 * Tag - the tag for the information requested example: TAG_IFD_ATR -
	 * return the Atr and it's size (required). these tags are defined in
	 * ifdhandler.h
	 *
	 * Length - the length of the returned data Value - the value of the
	 * data
	 *
	 * returns:
	 *
	 * IFD_SUCCESS IFD_ERROR_TAG
	 */
	int reader_index;

	DEBUG_INFO3("lun: %X, tag: 0x%X", Lun, Tag);

	if (-1 == (reader_index = LunToReaderIndex(Lun)))
		return IFD_COMMUNICATION_ERROR;

	switch (Tag)
	{
		case TAG_IFD_ATR:
		case SCARD_ATTR_ATR_STRING:
			/* If Length is not zero, powerICC has been performed.
			 * Otherwise, return NULL pointer
			 * Buffer size is stored in *Length */
			*Length = (*Length < CcidSlots[reader_index].nATRLength) ?
				*Length : CcidSlots[reader_index].nATRLength;

			if (*Length)
				memcpy(Value, CcidSlots[reader_index].pcATRBuffer, *Length);
			break;

#ifdef HAVE_PTHREAD
		case TAG_IFD_SIMULTANEOUS_ACCESS:
			if (*Length >= 1)
			{
				*Length = 1;
				*Value = CCID_DRIVER_MAX_READERS;
			}
			break;

		case TAG_IFD_THREAD_SAFE:
			if (*Length >= 1)
			{
				*Length = 1;
#ifdef __APPLE__
				*Value = 0; /* Apple pcscd is bogus (rdar://problem/5697388) */
#else
				*Value = 1; /* Can talk to multiple readers at the same time */
#endif
			}
			break;
#endif

		case TAG_IFD_SLOTS_NUMBER:
			if (*Length >= 1)
			{
				*Length = 1;
				*Value = 1 + get_ccid_descriptor(reader_index) -> bMaxSlotIndex;
				DEBUG_INFO2("Reader supports %d slot(s)", *Value);
			}
			break;

		case TAG_IFD_SLOT_THREAD_SAFE:
			if (*Length >= 1)
			{
				*Length = 1;
				*Value = 0; /* Can NOT talk to multiple slots at the same time */
			}
			break;

		case SCARD_ATTR_VENDOR_IFD_VERSION:
			/* Vendor-supplied interface device version (DWORD in the form
			 * 0xMMmmbbbb where MM = major version, mm = minor version, and
			 * bbbb = build number). */
			*Length = sizeof(DWORD);
			if (Value)
				*(DWORD *)Value = CCID_VERSION;
			break;

		case SCARD_ATTR_VENDOR_NAME:
#define VENDOR_NAME "Aktiv Co"
			*Length = sizeof(VENDOR_NAME);
			if (Value)
				memcpy(Value, VENDOR_NAME, sizeof(VENDOR_NAME));
			break;

		case SCARD_ATTR_MAXINPUT:
			*Length = sizeof(uint32_t);
			if (Value)
				*(uint32_t *)Value = get_ccid_descriptor(reader_index) -> dwMaxCCIDMessageLength - 10;
			break;

#if HAVE_DECL_TAG_IFD_POLLING_THREAD && defined(USE_USB_INTERRUPT)
		case TAG_IFD_POLLING_THREAD:
			{
				_ccid_descriptor *ccid_desc;

				/* default value: not supported */
				*Length = 0;

				ccid_desc = get_ccid_descriptor(reader_index);
				/* CCID and not ICCD */
				if ((0 == ccid_desc -> bInterfaceProtocol)
					/* 3 end points */
					&& (3 == ccid_desc -> bNumEndpoints))
				{
					*Length = sizeof(void *);
					if (Value)
						*(void **)Value = IFDHPolling;
				}
			}
			break;
#endif

		default:
			return IFD_ERROR_TAG;
	}

	return IFD_SUCCESS;
} /* IFDHGetCapabilities */


EXTERNAL RESPONSECODE IFDHSetCapabilities(DWORD Lun, DWORD Tag,
	/*@unused@*/ DWORD Length, /*@unused@*/ PUCHAR Value)
{
	/*
	 * This function should set the slot/card capabilities for a
	 * particular slot/card specified by Lun.  Again, if you have only 1
	 * card slot and don't mind loading a new driver for each reader then
	 * ignore Lun.
	 *
	 * Tag - the tag for the information needing set
	 *
	 * Length - the length of the returned data Value - the value of the
	 * data
	 *
	 * returns:
	 *
	 * IFD_SUCCESS IFD_ERROR_TAG IFD_ERROR_SET_FAILURE
	 * IFD_ERROR_VALUE_READ_ONLY
	 */

	/* By default, say it worked */

	DEBUG_INFO3("lun: %X, tag: 0x%X", Lun, Tag);

	/* if (CheckLun(Lun))
		return IFD_COMMUNICATION_ERROR; */

	return IFD_NOT_SUPPORTED;
} /* IFDHSetCapabilities */


EXTERNAL RESPONSECODE IFDHSetProtocolParameters(DWORD Lun, DWORD Protocol,
	UCHAR Flags, UCHAR PTS1, UCHAR PTS2, UCHAR PTS3)
{
	/*
	 * This function should set the PTS of a particular card/slot using
	 * the three PTS parameters sent
	 *
	 * Protocol - 0 .... 14 T=0 .... T=14
	 * Flags - Logical OR of possible values:
	 *  IFD_NEGOTIATE_PTS1
	 *  IFD_NEGOTIATE_PTS2
	 *  IFD_NEGOTIATE_PTS3
	 * to determine which PTS values to negotiate.
	 * PTS1,PTS2,PTS3 - PTS Values.
	 *
	 * returns:
	 *  IFD_SUCCESS
	 *  IFD_ERROR_PTS_FAILURE
	 *  IFD_COMMUNICATION_ERROR
	 *  IFD_PROTOCOL_NOT_SUPPORTED
	 */

	int reader_index;

	/* Set ccid desc params */
	CcidDesc *ccid_slot;
	_ccid_descriptor *ccid_desc;

	DEBUG_INFO3("lun: %X, protocol T=%d", Lun, Protocol-1);

	if (-1 == (reader_index = LunToReaderIndex(Lun)))
		return IFD_COMMUNICATION_ERROR;

	return IFD_SUCCESS;
} /* IFDHSetProtocolParameters */


EXTERNAL RESPONSECODE IFDHPowerICC(DWORD Lun, DWORD Action,
	PUCHAR Atr, PDWORD AtrLength)
{
	/*
	 * This function controls the power and reset signals of the smartcard
	 * reader at the particular reader/slot specified by Lun.
	 *
	 * Action - Action to be taken on the card.
	 *
	 * IFD_POWER_UP - Power and reset the card if not done so (store the
	 * ATR and return it and it's length).
	 *
	 * IFD_POWER_DOWN - Power down the card if not done already
	 * (Atr/AtrLength should be zero'd)
	 *
	 * IFD_RESET - Perform a quick reset on the card.  If the card is not
	 * powered power up the card.  (Store and return the Atr/Length)
	 *
	 * Atr - Answer to Reset of the card.  The driver is responsible for
	 * caching this value in case IFDHGetCapabilities is called requesting
	 * the ATR and it's length.  This should not exceed MAX_ATR_SIZE.
	 *
	 * AtrLength - Length of the Atr.  This should not exceed
	 * MAX_ATR_SIZE.
	 *
	 * Notes:
	 *
	 * Memory cards without an ATR should return IFD_SUCCESS on reset but
	 * the Atr should be zero'd and the length should be zero
	 *
	 * Reset errors should return zero for the AtrLength and return
	 * IFD_ERROR_POWER_ACTION.
	 *
	 * returns:
	 *
	 * IFD_SUCCESS IFD_ERROR_POWER_ACTION IFD_COMMUNICATION_ERROR
	 * IFD_NOT_SUPPORTED
	 */

	unsigned int nlength;
	RESPONSECODE return_value = IFD_SUCCESS;
	unsigned char pcbuffer[RESP_BUF_SIZE];
	int reader_index;
	const char *actions[] = { "PowerUp", "PowerDown", "Reset" };

	DEBUG_INFO3("lun: %X, action: %s", Lun, actions[Action-IFD_POWER_UP]);

	/* By default, assume it won't work :) */
	*AtrLength = 0;

	if (-1 == (reader_index = LunToReaderIndex(Lun)))
		return IFD_COMMUNICATION_ERROR;

	switch (Action)
	{
		case IFD_POWER_DOWN:
			/* Clear ATR buffer */
			CcidSlots[reader_index].nATRLength = 0;
			*CcidSlots[reader_index].pcATRBuffer = '\0';

			/* Memorise the request */
			CcidSlots[reader_index].bPowerFlags |= MASK_POWERFLAGS_PDWN;

			/* send the command */
			if (IFD_SUCCESS != CmdPowerOff(reader_index))
			{
				DEBUG_CRITICAL("PowerDown failed");
				return_value = IFD_ERROR_POWER_ACTION;
				goto end;
			}

			/* clear T=1 context */
			t1_release(&(get_ccid_slot(reader_index) -> t1));
			break;

		case IFD_POWER_UP:
		case IFD_RESET:
			nlength = sizeof(pcbuffer);
			if (CmdPowerOn(reader_index, &nlength, pcbuffer)
				!= IFD_SUCCESS)
			{
				/* used by GemCore SIM PRO: no card is present */
				get_ccid_descriptor(reader_index)->dwSlotStatus
					= IFD_ICC_NOT_PRESENT;;

				DEBUG_CRITICAL("PowerUp failed");
				return_value = IFD_ERROR_POWER_ACTION;
				goto end;
			}

			/* Power up successful, set state variable to memorise it */
			CcidSlots[reader_index].bPowerFlags |= MASK_POWERFLAGS_PUP;
			CcidSlots[reader_index].bPowerFlags &= ~MASK_POWERFLAGS_PDWN;

			/* Reset is returned, even if TCK is wrong */
			CcidSlots[reader_index].nATRLength = *AtrLength =
				(nlength < MAX_ATR_SIZE) ? nlength : MAX_ATR_SIZE;
			memcpy(Atr, pcbuffer, *AtrLength);
			memcpy(CcidSlots[reader_index].pcATRBuffer, pcbuffer, *AtrLength);

			/* initialise T=1 context */
			t1_init(&(get_ccid_slot(reader_index) -> t1), reader_index);
			break;

		default:
			DEBUG_CRITICAL("Action not supported");
			return_value = IFD_NOT_SUPPORTED;
	}
end:

	return return_value;
} /* IFDHPowerICC */


EXTERNAL RESPONSECODE IFDHTransmitToICC(DWORD Lun, SCARD_IO_HEADER SendPci,
	PUCHAR TxBuffer, DWORD TxLength,
	PUCHAR RxBuffer, PDWORD RxLength, /*@unused@*/ PSCARD_IO_HEADER RecvPci)
{
	/*
	 * This function performs an APDU exchange with the card/slot
	 * specified by Lun.  The driver is responsible for performing any
	 * protocol specific exchanges such as T=0/1 ... differences.  Calling
	 * this function will abstract all protocol differences.
	 *
	 * SendPci Protocol - 0, 1, .... 14 Length - Not used.
	 *
	 * TxBuffer - Transmit APDU example (0x00 0xA4 0x00 0x00 0x02 0x3F
	 * 0x00) TxLength - Length of this buffer. RxBuffer - Receive APDU
	 * example (0x61 0x14) RxLength - Length of the received APDU.  This
	 * function will be passed the size of the buffer of RxBuffer and this
	 * function is responsible for setting this to the length of the
	 * received APDU.  This should be ZERO on all errors.  The resource
	 * manager will take responsibility of zeroing out any temporary APDU
	 * buffers for security reasons.
	 *
	 * RecvPci Protocol - 0, 1, .... 14 Length - Not used.
	 *
	 * Notes: The driver is responsible for knowing what type of card it
	 * has.  If the current slot/card contains a memory card then this
	 * command should ignore the Protocol and use the MCT style commands
	 * for support for these style cards and transmit them appropriately.
	 * If your reader does not support memory cards or you don't want to
	 * then ignore this.
	 *
	 * RxLength should be set to zero on error.
	 *
	 * returns:
	 *
	 * IFD_SUCCESS IFD_COMMUNICATION_ERROR IFD_RESPONSE_TIMEOUT
	 * IFD_ICC_NOT_PRESENT IFD_PROTOCOL_NOT_SUPPORTED
	 */

	RESPONSECODE return_value;
	unsigned int rx_length;
	int reader_index;

	DEBUG_INFO2("lun: %X", Lun);

	if (-1 == (reader_index = LunToReaderIndex(Lun)))
		return IFD_COMMUNICATION_ERROR;

	rx_length = *RxLength;
	return_value = CmdXfrBlock(reader_index, TxLength, TxBuffer, &rx_length,
		RxBuffer, SendPci.Protocol);
	if (IFD_SUCCESS == return_value)
		*RxLength = rx_length;
	else
		*RxLength = 0;

	return return_value;
} /* IFDHTransmitToICC */


EXTERNAL RESPONSECODE IFDHControl(DWORD Lun, DWORD dwControlCode,
	PUCHAR TxBuffer, DWORD TxLength, PUCHAR RxBuffer, DWORD RxLength,
	PDWORD pdwBytesReturned)
{
	/*
	 * This function performs a data exchange with the reader (not the
	 * card) specified by Lun.  Here XXXX will only be used. It is
	 * responsible for abstracting functionality such as PIN pads,
	 * biometrics, LCD panels, etc.  You should follow the MCT, CTBCS
	 * specifications for a list of accepted commands to implement.
	 *
	 * TxBuffer - Transmit data TxLength - Length of this buffer. RxBuffer
	 * - Receive data RxLength - Length of the received data.  This
	 * function will be passed the length of the buffer RxBuffer and it
	 * must set this to the length of the received data.
	 *
	 * Notes: RxLength should be zero on error.
	 */
	RESPONSECODE return_value = IFD_COMMUNICATION_ERROR;
	int reader_index;

	DEBUG_INFO3("lun: %X, ControlCode: 0x%X", Lun, dwControlCode);
	DEBUG_INFO_XXD("Control TxBuffer: ", TxBuffer, TxLength);

	reader_index = LunToReaderIndex(Lun);
	if ((-1 == reader_index) || (NULL == pdwBytesReturned))
		return return_value;

	/* Set the return length to 0 to avoid problems */
	*pdwBytesReturned = 0;

	/* Implement the PC/SC v2.1.2 Part 10 IOCTL mechanism */

	/* Query for features */
	if (CM_IOCTL_GET_FEATURE_REQUEST == dwControlCode)
	{
		unsigned int iBytesReturned = 0;
		PCSC_TLV_STRUCTURE *pcsc_tlv = (PCSC_TLV_STRUCTURE *)RxBuffer;

		/* we need room for two records */
		if (RxLength < 2 * sizeof(PCSC_TLV_STRUCTURE))
			return IFD_COMMUNICATION_ERROR;

		/* We can only support direct verify and/or modify currently */
		if (get_ccid_descriptor(reader_index) -> bPINSupport
			& CCID_CLASS_PIN_VERIFY)
		{
			pcsc_tlv -> tag = FEATURE_VERIFY_PIN_DIRECT;
			pcsc_tlv -> length = 0x04; /* always 0x04 */
			pcsc_tlv -> value = htonl(IOCTL_FEATURE_VERIFY_PIN_DIRECT);

			pcsc_tlv++;
			iBytesReturned += sizeof(PCSC_TLV_STRUCTURE);
		}

		if (get_ccid_descriptor(reader_index) -> bPINSupport
			& CCID_CLASS_PIN_MODIFY)
		{
			pcsc_tlv -> tag = FEATURE_MODIFY_PIN_DIRECT;
			pcsc_tlv -> length = 0x04; /* always 0x04 */
			pcsc_tlv -> value = htonl(IOCTL_FEATURE_MODIFY_PIN_DIRECT);

			pcsc_tlv++;
			iBytesReturned += sizeof(PCSC_TLV_STRUCTURE);
		}
		*pdwBytesReturned = iBytesReturned;
		return_value = IFD_SUCCESS;
	}

	if (IFD_SUCCESS != return_value)
		*pdwBytesReturned = 0;

	DEBUG_INFO_XXD("Control RxBuffer: ", RxBuffer, *pdwBytesReturned);
	return return_value;
} /* IFDHControl */


EXTERNAL RESPONSECODE IFDHICCPresence(DWORD Lun)
{
	/*
	 * This function returns the status of the card inserted in the
	 * reader/slot specified by Lun.  It will return either:
	 *
	 * returns: IFD_ICC_PRESENT IFD_ICC_NOT_PRESENT
	 * IFD_COMMUNICATION_ERROR
	 */

	unsigned char presence;
	RESPONSECODE return_value = IFD_COMMUNICATION_ERROR;
	int oldLogLevel;
	int reader_index;
	_ccid_descriptor *ccid_descriptor;
	unsigned int oldReadTimeout;

	DEBUG_PERIODIC2("lun: %X", Lun);

	if (-1 == (reader_index = LunToReaderIndex(Lun)))
		return IFD_COMMUNICATION_ERROR;

	ccid_descriptor = get_ccid_descriptor(reader_index);

	/* save the current read timeout computed from card capabilities */
	oldReadTimeout = ccid_descriptor->readTimeout;

	/* use default timeout since the reader may not be present anymore */
	ccid_descriptor->readTimeout = DEFAULT_COM_READ_TIMEOUT;

	/* if DEBUG_LEVEL_PERIODIC is not set we remove DEBUG_LEVEL_COMM */
	oldLogLevel = LogLevel;
	if (! (LogLevel & DEBUG_LEVEL_PERIODIC))
		LogLevel &= ~DEBUG_LEVEL_COMM;

	return_value = CmdIccPresence(reader_index, &presence);

	/* set back the old timeout */
	ccid_descriptor->readTimeout = oldReadTimeout;

	/* set back the old LogLevel */
	LogLevel = oldLogLevel;

	if (return_value != IFD_SUCCESS)
		return return_value;

	return_value = IFD_COMMUNICATION_ERROR;
	switch (presence & CCID_ICC_STATUS_MASK)	/* bStatus */
	{
		case CCID_ICC_PRESENT_ACTIVE:
			return_value = IFD_ICC_PRESENT;
			/* use default slot */
			break;

		case CCID_ICC_ABSENT:
			/* Reset ATR buffer */
			CcidSlots[reader_index].nATRLength = 0;
			*CcidSlots[reader_index].pcATRBuffer = '\0';

			/* Reset PowerFlags */
			CcidSlots[reader_index].bPowerFlags = POWERFLAGS_RAZ;

			return_value = IFD_ICC_NOT_PRESENT;
			break;
	}

	DEBUG_PERIODIC2("Card %s",
		IFD_ICC_PRESENT == return_value ? "present" : "absent");

	return return_value;
} /* IFDHICCPresence */


CcidDesc *get_ccid_slot(unsigned int reader_index)
{
	return &CcidSlots[reader_index];
} /* get_ccid_slot */


void init_driver(void)
{
	char keyValue[TOKEN_MAX_VALUE_SIZE];
	char infofile[FILENAME_MAX];
	char *e;

	DEBUG_INFO("Driver version: " VERSION);

	/* Info.plist full patch filename */
	snprintf(infofile, sizeof(infofile), "%s/%s/Contents/Info.plist",
		PCSCLITE_HP_DROPDIR, BUNDLE);

	/* Log level */
	if (0 == LTPBundleFindValueWithKey(infofile, "ifdLogLevel", keyValue, 0))
	{
		/* convert from hex or dec or octal */
		LogLevel = strtoul(keyValue, NULL, 0);

		/* print the log level used */
		DEBUG_INFO2("LogLevel: 0x%.4X", LogLevel);
	}

	e = getenv("LIBCCID_ifdLogLevel");
	if (e)
	{
		/* convert from hex or dec or octal */
		LogLevel = strtoul(e, NULL, 0);

		/* print the log level used */
		DEBUG_INFO2("LogLevel from LIBCCID_ifdLogLevel: 0x%.4X", LogLevel);
	}
	
	/* Driver options */
	if (0 == LTPBundleFindValueWithKey(infofile, "ifdDriverOptions", keyValue, 0))
	{
		/* convert from hex or dec or octal */
		DriverOptions = strtoul(keyValue, NULL, 0);

		/* print the log level used */
		DEBUG_INFO2("DriverOptions: 0x%.4X", DriverOptions);
	}

	/* get the voltage parameter */
	switch ((DriverOptions >> 4) & 0x03)
	{
		case 0:
			PowerOnVoltage = VOLTAGE_5V;
			break;

		case 1:
			PowerOnVoltage = VOLTAGE_3V;
			break;

		case 2:
			PowerOnVoltage = VOLTAGE_1_8V;
			break;

		case 3:
			PowerOnVoltage = VOLTAGE_AUTO;
			break;
	}

	/* initialise the Lun to reader_index mapping */
	InitReaderIndex();

	DebugInitialized = TRUE;
} /* init_driver */


void extra_egt(ATR_t *atr, _ccid_descriptor *ccid_desc, DWORD Protocol)
{
	/* This function use an EGT value for cards who comply with followings
	 * criterias:
	 * - TA1 > 11
	 * - current EGT = 0x00 or 0xFF
	 * - T=0 or (T=1 and CWI >= 2)
	 *
	 * Without this larger EGT some non ISO 7816-3 smart cards may not
	 * communicate with the reader.
	 *
	 * This modification is harmless, the reader will just be less restrictive
	 */

	unsigned int card_baudrate;
	unsigned int default_baudrate;
	double f, d;
	int i;

	/* if TA1 not present */
	if (! atr->ib[0][ATR_INTERFACE_BYTE_TA].present)
		return;

	ATR_GetParameter(atr, ATR_PARAMETER_D, &d);
	ATR_GetParameter(atr, ATR_PARAMETER_F, &f);

	/* may happen with non ISO cards */
	if ((0 == f) || (0 == d))
		return;

	/* Baudrate = f x D/F */
	card_baudrate = (unsigned int) (1000 * ccid_desc->dwDefaultClock * d / f);

	default_baudrate = (unsigned int) (1000 * ccid_desc->dwDefaultClock
		* ATR_DEFAULT_D / ATR_DEFAULT_F);

	/* TA1 > 11? */
	if (card_baudrate <= default_baudrate)
		return;

	/* Current EGT = 0 or FF? */
	if (atr->ib[0][ATR_INTERFACE_BYTE_TC].present &&
		((0x00 == atr->ib[0][ATR_INTERFACE_BYTE_TC].value) ||
		(0xFF == atr->ib[0][ATR_INTERFACE_BYTE_TC].value)))
	{
		if (SCARD_PROTOCOL_T0 == Protocol)
		{
			/* Init TC1 */
			atr->ib[0][ATR_INTERFACE_BYTE_TC].present = TRUE;
			atr->ib[0][ATR_INTERFACE_BYTE_TC].value = 2;
			DEBUG_INFO("Extra EGT patch applied");
		}

		if (SCARD_PROTOCOL_T1 == Protocol)
		{
			/* TBi (i>2) present? BWI/CWI */
			for (i=2; i<ATR_MAX_PROTOCOLS; i++)
			{
				/* CWI >= 2 ? */
				if (atr->ib[i][ATR_INTERFACE_BYTE_TB].present &&
					((atr->ib[i][ATR_INTERFACE_BYTE_TB].value & 0x0F) >= 2))
				{
					/* Init TC1 */
					atr->ib[0][ATR_INTERFACE_BYTE_TC].present = TRUE;
					atr->ib[0][ATR_INTERFACE_BYTE_TC].value = 2;
					DEBUG_INFO("Extra EGT patch applied");

					/* only the first TBi (i>2) must be used */
					break;
				}
			}
		}
	}
} /* extra_egt */


static char find_baud_rate(unsigned int baudrate, unsigned int *list)
{
	int i;

	DEBUG_COMM2("Card baud rate: %d", baudrate);

	/* Does the reader support the announced smart card data speed? */
	for (i=0;; i++)
	{
		/* end of array marker */
		if (0 == list[i])
			break;

		DEBUG_COMM2("Reader can do: %d", list[i]);

		/* We must take into account that the card_baudrate integral value
		 * is an approximative result, computed from the d/f float result.
		 */
		if ((baudrate < list[i] + 2) && (baudrate > list[i] - 2))
			return TRUE;
	}

	return FALSE;
} /* find_baud_rate */


static unsigned int T0_card_timeout(double f, double d, int TC1, int TC2,
	int clock_frequency)
{
	unsigned int timeout = DEFAULT_COM_READ_TIMEOUT;
	double EGT, WWT;
	unsigned int t;

	/* Timeout applied on ISO_IN or ISO_OUT card exchange
	 * we choose the maximum computed value.
	 *
	 * ISO_IN timeout is the sum of:
	 * Terminal:					Smart card:
	 * 5 bytes header cmd  ->
	 *                    <-		Procedure byte
	 * 256 data bytes	   ->
	 * 					  <-		SW1-SW2
	 * = 261 EGT       + 3 WWT     + 3 WWT
	 *
	 * ISO_OUT Timeout is the sum of:
	 * Terminal:                    Smart card:
	 * 5 bytes header cmd  ->
	 * 					  <-        Procedure byte + 256 data bytes + SW1-SW2
	 * = 5 EGT          + 1 WWT     + 259 WWT
	 */

	/* clock_frequency is in kHz so the times are in milliseconds and not
	 * in seconds */

	/* may happen with non ISO cards */
	if ((0 == f) || (0 == d) || (0 == clock_frequency))
		return 60;	/* 60 seconds */

	/* EGT */
	/* see ch. 6.5.3 Extra Guard Time, page 12 of ISO 7816-3 */
	EGT = 12 * f / d / clock_frequency + (f / d) * TC1 / clock_frequency;

	/* card WWT */
	/* see ch. 8.2 Character level, page 15 of ISO 7816-3 */
	WWT = 960 * TC2 * f / clock_frequency;

	/* ISO in */
	t  = 261 * EGT + (3 + 3) * WWT;
	/* Convert from milliseonds to seconds rouned to the upper value
	 * use +1 instead of ceil() to round up to the nearest interger
	 * so we can avoid a dependency on the math library */
	t = t/1000 +1;
	if (timeout < t)
		timeout = t;

	/* ISO out */
	t = 5 * EGT + (1 + 259) * WWT;
	t = t/1000 +1;
	if (timeout < t)
		timeout = t;

	return timeout;
} /* T0_card_timeout  */


static unsigned int T1_card_timeout(double f, double d, int TC1,
	int BWI, int CWI, int clock_frequency)
{
	double EGT, BWT, CWT, etu;
	unsigned int timeout;

	/* Timeout applied on ISO in + ISO out card exchange
	 *
     * Timeout is the sum of:
	 * - ISO in delay between leading edge of the first character sent by the
	 *   interface device and the last one (NAD PCB LN APDU CKS) = 260 EGT,
	 * - delay between ISO in and ISO out = BWT,
	 * - ISO out delay between leading edge of the first character sent by the
	 *   card and the last one (NAD PCB LN DATAS CKS) = 260 CWT.
	 */

	/* clock_frequency is in kHz so the times are in milliseconds and not
	 * in seconds */

	/* may happen with non ISO cards */
	if ((0 == f) || (0 == d) || (0 == clock_frequency))
		return 60;	/* 60 seconds */

	/* see ch. 6.5.2 Transmission factors F and D, page 12 of ISO 7816-3 */
	etu = f / d / clock_frequency;

	/* EGT */
	/* see ch. 6.5.3 Extra Guard Time, page 12 of ISO 7816-3 */
	EGT = 12 * etu + (f / d) * TC1 / clock_frequency;

	/* card BWT */
	/* see ch. 9.5.3.2 Block Waiting Time, page 20 of ISO 7816-3 */
	BWT = 11 * etu + (1<<BWI) * 960 * 372 / clock_frequency;

	/* card CWT */
	/* see ch. 9.5.3.1 Caracter Waiting Time, page 20 of ISO 7816-3 */
	CWT = (11 + (1<<CWI)) * etu;

	timeout = 260*EGT + BWT + 260*CWT;

	/* Convert from milliseonds to seconds rounded to the upper value
	 * we use +1 instead of ceil() to round up to the nearest greater interger
	 * so we can avoid a dependency on the math library */
	timeout = timeout/1000 +1;

	return timeout;
} /* T1_card_timeout  */

