/*
    rutokens_usb.c: USB access routines using the libusb library
    Copyright (C) 2012 Aktiv Co
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


#define __RUTOKENS_USB__

#include <stdio.h>
#include <string.h>
#include <errno.h>
# ifdef S_SPLINT_S
# include <sys/types.h>
# endif
#include <usb.h>
#include <ifdhandler.h>

#include "misc.h"
#include "infopath.h"
#include "rutokens.h"
#include "config.h"
#include "debug.h"
#include "defs.h"
#include "utils.h"
#include "parser.h"


typedef struct
{
	usb_dev_handle *handle;
	char *dirname;
	char *filename;
	int interface;

	/*
	 * Endpoints
	 */
	int bulk_in;
	int bulk_out;
	int interrupt;

	/* Number of slots using the same device */
	int real_nb_opened_slots;
	int *nb_opened_slots;

	/*
	 * Device infos common to USB
	 */
	_device_descriptor rtdesc;

} _usbDevice;

/* The _usbDevice structure must be defined before including rutokens_usb.h */
#include "rutokens_usb.h"

/* ne need to initialize to 0 since it is static */
static _usbDevice usbDevice[DRIVER_MAX_READERS];

#define PCSCLITE_MANUKEY_NAME                   "ifdVendorID"
#define PCSCLITE_PRODKEY_NAME                   "ifdProductID"
#define PCSCLITE_NAMEKEY_NAME                   "ifdFriendlyName"


/*****************************************************************************
 *
 *					OpenUSB
 *
 ****************************************************************************/
status_t OpenUSB(unsigned int reader_index, /*@unused@*/ int Channel)
{
	return OpenUSBByName(reader_index, NULL);
} /* OpenUSB */


/*****************************************************************************
 *
 *					OpenUSBByName
 *
 ****************************************************************************/
status_t OpenUSBByName(unsigned int reader_index, /*@null@*/ char *device)
{
	static struct usb_bus *busses = NULL;
	int alias = 0;
	struct usb_bus *bus;
	struct usb_dev_handle *dev_handle;
	char keyValue[TOKEN_MAX_VALUE_SIZE];
	unsigned int vendorID, productID;
	char infofile[FILENAME_MAX];
	unsigned int device_vendor, device_product;
	char *dirname = NULL, *filename = NULL;
	static int previous_reader_index = -1;
#ifdef __APPLE__
	int count_libusb = 25;
#endif

	DEBUG_COMM3("Reader index: %X, Device: %s", reader_index, device);

#ifndef __APPLE__
	/* device name specified */
	if (device)
	{
		/* format: usb:%04x/%04x, vendor, product */
		if (strncmp("usb:", device, 4) != 0)
		{
			DEBUG_CRITICAL2("device name does not start with \"usb:\": %s", device);
			return STATUS_UNSUCCESSFUL;
		}

		{
			char* vendor=0;
			char* deviceproduct=0;
			char* deviceproductend=0;
			vendor = strchr(device, ':');
			deviceproduct = strchr(device, '/');

			if(vendor && deviceproduct)
			{
				vendor++;
				*deviceproduct = 0;
				deviceproduct++;
				deviceproductend = strchr(deviceproduct, ':');
			}

			if(deviceproductend)
				*deviceproductend = 0;
			else
			{
				DEBUG_CRITICAL2("device name can't be parsed: %s", device);
				return STATUS_UNSUCCESSFUL;
			}

			if(strlen(vendor) < 4 || strlen(deviceproduct) < 4)
			{
				DEBUG_CRITICAL2("device name can't be parsed: %s", device);
				return STATUS_UNSUCCESSFUL;
			}

			device_vendor = strtoul(vendor,NULL,16);
			device_product = strtoul(deviceproduct,NULL,16);

			if (!device_vendor || !device_product)
			{
				DEBUG_CRITICAL2("device name can't be parsed: %s", device);
				return STATUS_UNSUCCESSFUL;
			}
			device = deviceproductend+1;
		}

		/* format usb:%04x/%04x:libusb:%s
		 * with %s set to %s:%s, dirname, filename */
		if ((dirname = strstr(device, "libusb:")) != NULL)
		{
			/* dirname points to the first char after libusb: */
			dirname += strlen("libusb:");

			/* search the : (separation) char */
			filename = strchr(dirname, ':');

			if (filename)
			{
				/* end the dirname string */
				*filename = '\0';

				/* filename points to the first char after : */
				filename++;
			}
			else
			{
				/* parse failed */
				dirname = NULL;

				DEBUG_CRITICAL2("can't parse using libusb scheme: %s", device);
			}
		}
		/* format usb:%04x/%04x:libudev:%d:%s
		 * with %d set to
		 * 01 (or whatever the interface number is)
		 * and %s set to
		 * /dev/bus/usb/008/004
		 */
		else if ((dirname = strstr(device, "libudev:")) != NULL)
		{
			/* dirname points to the libudev:/dev/bus/usb/%d/%d */

			/* filename points to the last / */
			filename = strrchr(dirname, '/');

			if (filename)
			{
				/* end the dirname string */
				*filename = '\0';

				/* filename points to the first char after / */
				filename++;
			}

			/* dirname points to the last but only / */
			dirname = strrchr(dirname, '/');

			if(dirname)
			{
				/* dirname points to the first char after / */
				dirname++;
			}
			else
			{
				filename = NULL;
				DEBUG_CRITICAL2("can't parse using libudev scheme: %s", device);
			}
		}
		else
		{
			DEBUG_CRITICAL2("can't parse device naming sheme: %s", device);
		}
	}
#endif

	if (busses == NULL)
		usb_init();

#ifdef __APPLE__
again_libusb:
#endif

	usb_find_busses();
	usb_find_devices();

	busses = usb_get_busses();

	if (busses == NULL)
	{
		DEBUG_CRITICAL("No USB busses found");
		return STATUS_UNSUCCESSFUL;
	}

	/* is the reader_index already used? */
	if (usbDevice[reader_index].handle != NULL)
	{
		DEBUG_CRITICAL2("USB driver with index %X already in use",
			reader_index);
		return STATUS_UNSUCCESSFUL;
	}

	/* Info.plist full path filename */
	infoFileName(infofile);

	/* general driver info */
	if (!LTPBundleFindValueWithKey(infofile, "ifdManufacturerString", keyValue, 0))
	{
		DEBUG_INFO2("Manufacturer: %s", keyValue);
	}
	else
	{
		DEBUG_INFO2("LTPBundleFindValueWithKey error. Can't find %s", infofile);
		return STATUS_UNSUCCESSFUL;
	}
	if (!LTPBundleFindValueWithKey(infofile, "ifdProductString", keyValue, 0))
	{
		DEBUG_INFO2("ProductString: %s", keyValue);
	}
	else
		return STATUS_UNSUCCESSFUL;
	if (!LTPBundleFindValueWithKey(infofile, "Copyright", keyValue, 0))
	{
		DEBUG_INFO2("Copyright: %s", keyValue);
	}
	else
		return STATUS_UNSUCCESSFUL;
	vendorID = strlen(keyValue);
	alias = 0x1C;
	for (; vendorID--;)
		alias ^= keyValue[vendorID];

	/* for any supported reader */
	while (LTPBundleFindValueWithKey(infofile, PCSCLITE_MANUKEY_NAME, keyValue, alias) == 0)
	{
		vendorID = strtoul(keyValue, NULL, 0);

		if (LTPBundleFindValueWithKey(infofile, PCSCLITE_PRODKEY_NAME, keyValue, alias))
			goto end;
		productID = strtoul(keyValue, NULL, 0);

		if (LTPBundleFindValueWithKey(infofile, PCSCLITE_NAMEKEY_NAME, keyValue, alias))
			goto end;

		/* go to next supported reader for next round */
		alias++;

#ifndef __APPLE__
		/* the device was specified but is not the one we are trying to find */
		if (device
			&& (vendorID != device_vendor || productID != device_product))
			continue;
#else
		/* Leopard puts the friendlyname in the device argument */
		if (device && strcmp(device, keyValue))
			continue;
#endif

		/* on any USB buses */
		for (bus = busses; bus; bus = bus->next)
		{
			struct usb_device *dev;

			/* any device on this bus */
			for (dev = bus->devices; dev; dev = dev->next)
			{
				/* device defined by name? */
				if (dirname && (strcmp(dirname, bus->dirname) || strcmp(filename, dev->filename)))
					continue;

				if (dev->descriptor.idVendor == vendorID && dev->descriptor.idProduct == productID)
				{
					int r, already_used;
					struct usb_interface *usb_interface = NULL;
					int interface;

					/* is it already opened? */
					already_used = FALSE;

					DEBUG_COMM3("Checking device: %s/%s", bus->dirname, dev->filename);
					for (r=0; r<DRIVER_MAX_READERS; r++)
					{
						if (usbDevice[r].handle)
						{
							DEBUG_COMM3("Comparing with device: %s/%s", usbDevice[r].dirname, usbDevice[r].filename);
							/* same busname, same filename */
							if (strcmp(usbDevice[r].dirname, bus->dirname) == 0 && strcmp(usbDevice[r].filename, dev->filename) == 0)
								already_used = TRUE;
						}
					}

					/* this reader is already managed by us */
					if (already_used)
					{
						DEBUG_INFO3("USB device %s/%s already in use. Checking next one.", bus->dirname, dev->filename);
						continue;
					}

					DEBUG_COMM3("Trying to open USB bus/device: %s/%s",	 bus->dirname, dev->filename);

					dev_handle = usb_open(dev);
					if (dev_handle == NULL)
					{
						DEBUG_CRITICAL4("Can't usb_open(%s/%s): %s", bus->dirname, dev->filename, strerror(errno));
						continue;
					}

					/* now we found a free reader and we try to use it */
					if (dev->config == NULL)
					{
						usb_close(dev_handle);
						DEBUG_CRITICAL3("No dev->config found for %s/%s", bus->dirname, dev->filename);
						continue;
					}

					usb_interface = get_usb_interface(dev);
					if (usb_interface == NULL)
					{
						usb_close(dev_handle);
						DEBUG_CRITICAL3("Can't find a device interface on %s/%s",	bus->dirname, dev->filename);
						continue;
					}

					if (usb_interface->altsetting->extralen != 54)
						DEBUG_INFO4("Extra field for %s/%s has a wrong length: %d", bus->dirname, dev->filename, usb_interface->altsetting->extralen);

					interface = usb_interface->altsetting->bInterfaceNumber;
					if (usb_claim_interface(dev_handle, interface) < 0)
					{
						usb_close(dev_handle);
						DEBUG_CRITICAL4("Can't claim interface %s/%s: %s",	bus->dirname, dev->filename, strerror(errno));
						continue;
					}

					DEBUG_INFO4("Found Vendor/Product: %04X/%04X (%s)",	dev->descriptor.idVendor, dev->descriptor.idProduct, keyValue);
					DEBUG_INFO3("Using USB bus/device: %s/%s", bus->dirname, dev->filename);

					/* No Endpoints; control only*/

					/* store device information */
					usbDevice[reader_index].handle = dev_handle;
					usbDevice[reader_index].dirname = strdup(bus->dirname);
					usbDevice[reader_index].filename = strdup(dev->filename);
					usbDevice[reader_index].interface = interface;
					usbDevice[reader_index].real_nb_opened_slots = 1;
					usbDevice[reader_index].nb_opened_slots = &usbDevice[reader_index].real_nb_opened_slots;

					/* Device common informations */
					usbDevice[reader_index].rtdesc.real_bSeq = 0;
					usbDevice[reader_index].rtdesc.pbSeq = &usbDevice[reader_index].rtdesc.real_bSeq;
					usbDevice[reader_index].rtdesc.readerID = (dev->descriptor.idVendor << 16) + dev->descriptor.idProduct;

					usbDevice[reader_index].rtdesc.dwMaxDevMessageLength = 261;
					usbDevice[reader_index].rtdesc.dwMaxIFSD = 254;
					usbDevice[reader_index].rtdesc.bMaxSlotIndex = 0;

					usbDevice[reader_index].rtdesc.readTimeout = DEFAULT_COM_READ_TIMEOUT;
					usbDevice[reader_index].rtdesc.bNumEndpoints = usb_interface->altsetting->bNumEndpoints;
				}
			}
		}
	}
end:
	if (usbDevice[reader_index].handle == NULL) {
#ifdef __APPLE__
		// There is a race condition with libusb-1.0. The latter doesn't have time to process
		// token connection by the usb_find_devices() call. To handle this situation there is
		// 10 attempts with a delay for 100 ms.
		if (count_libusb > 0)
		{
			count_libusb--;
			DEBUG_INFO2("Wait after libusb: %d", count_libusb);
			usleep(100000);

			goto again_libusb;
		}
#endif
		return STATUS_UNSUCCESSFUL;
	}

	/* memorise the current reader_index so we can detect
	 * a new OpenUSBByName on a multi slot reader */
	previous_reader_index = reader_index;

	return STATUS_SUCCESS;
} /* OpenUSBByName */


/*****************************************************************************
 *
 *					CloseUSB
 *
 ****************************************************************************/
status_t CloseUSB(unsigned int reader_index)
{
	/* device not opened */
	if (usbDevice[reader_index].handle == NULL)
		return STATUS_UNSUCCESSFUL;

	DEBUG_COMM3("Closing USB device: %s/%s",
		usbDevice[reader_index].dirname,
		usbDevice[reader_index].filename);

	/* one slot closed */
	(*usbDevice[reader_index].nb_opened_slots)--;

	/* release the allocated ressources for the last slot only */
	if (0 == *usbDevice[reader_index].nb_opened_slots)
	{
		DEBUG_COMM("Last slot closed. Release resources");

		usb_release_interface(usbDevice[reader_index].handle,
			usbDevice[reader_index].interface);
		usb_close(usbDevice[reader_index].handle);
		usb_find_devices();

		free(usbDevice[reader_index].dirname);
		free(usbDevice[reader_index].filename);
	}

	/* mark the resource unused */
	usbDevice[reader_index].handle = NULL;
	usbDevice[reader_index].dirname = NULL;
	usbDevice[reader_index].filename = NULL;
	usbDevice[reader_index].interface = 0;

	return STATUS_SUCCESS;
} /* CloseUSB */


/*****************************************************************************
 *
 *					get_device_descriptor
 *
 ****************************************************************************/
_device_descriptor *get_device_descriptor(unsigned int reader_index)
{
	return &usbDevice[reader_index].rtdesc;
} /* get_device_descriptor */


/*****************************************************************************
 *
 *					get_usb_interface
 *
 ****************************************************************************/
/*@null@*/ EXTERNAL struct usb_interface * get_usb_interface(struct usb_device *dev)
{
	struct usb_interface *usb_interface = NULL;
	int i;

	/* if multiple interfaces use the first one with CCID class type */
	for (i=0; dev->config && i<dev->config->bNumInterfaces; i++)
	{
		if (dev->config->interface[i].altsetting->bInterfaceClass == 0xff)
		{
			usb_interface = &dev->config->interface[i];
			break;
		}
	}

	return usb_interface;
} /* get_usb_interface */


/*****************************************************************************
 *
 *                                      ControlUSB
 *
 ****************************************************************************/
int ControlUSB(int reader_index, int requesttype, int request, int value,
	unsigned char *bytes, unsigned int size)
{
	int ret;

	DEBUG_COMM2("request: 0x%02X", request);

	if (0 == (requesttype & 0x80))
		DEBUG_XXD("send: ", bytes, size);

	ret = usb_control_msg(usbDevice[reader_index].handle, requesttype,
		request, value, usbDevice[reader_index].interface, (char *)bytes, size,
		usbDevice[reader_index].rtdesc.readTimeout * 1000);

	if (requesttype & 0x80)
		 DEBUG_XXD("receive: ", bytes, ret);

	return ret;
} /* ControlUSB */
