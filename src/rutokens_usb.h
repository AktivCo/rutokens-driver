/*
    rutokens_usb.h:  USB access routines using the libusb library
    Copyright (C) 2012 Aktiv Co
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


#ifndef __RUTOKENS_USB_H__
#define __RUTOKENS_USB_H__

status_t OpenUSB(unsigned int reader_index, int channel);

status_t OpenUSBByName(unsigned int reader_index, /*@null@*/ char *device);

status_t CloseUSB(unsigned int reader_index);

#if defined (__USB_H__) || defined (_SYS_USB_LIBUSB_USB_H) || defined ( __FreeBSD__ )
/*@null@*/ struct usb_interface *get_usb_interface(struct usb_device *dev);
#endif

int ControlUSB(int reader_index, int requesttype, int request, int value,
	unsigned char *bytes, unsigned int size);

#endif
