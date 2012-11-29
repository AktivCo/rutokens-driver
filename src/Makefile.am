# $Id: Makefile.am 3024 2008-06-26 13:20:43Z rousseau $

CCID_BUNDLE = $(bundle)
CCID_LIB = librutokens.$(DYN_LIB_EXT)

CCID_VERSION=CCID_VERSION=`$(srcdir)/convert_version.pl $(PACKAGE_VERSION)`

lib_LTLIBRARIES =
LIBS_TO_INSTALL =
if WITH_LIBUSB
lib_LTLIBRARIES += librutokens.la
LIBS_TO_INSTALL += install_ccid
endif

COMMON = apdu.c \
	apdu.h \
	rutokens.h \
	commands.c \
	commands.h \
	convert_apdu.c \
	convert_apdu.h \
	debug.h \
	defs.h \
	ifdhandler.c \
	utils.c \
	utils.h 
USB = ccid_usb.c ccid_usb.h
SERIAL = ccid_serial.c ccid_serial.h
TOKEN_PARSER = tokenparser.l parser.h \
	strlcpy.c \
	misc.h \
	strlcpycat.h

MAINTAINERCLEANFILES = tokenparser.c

if WITHOUT_PCSC
PROVIDED_BY_PCSC = debug.c
endif

librutokens_la_SOURCES = $(COMMON) $(USB) $(TOKEN_PARSER) $(PROVIDED_BY_PCSC) $(T1)
librutokens_la_LIBADD = $(LEXLIB) $(COREFOUNDATION) $(IOKIT) \
	$(LIBUSB_LIBS) $(PTHREAD_LIBS)
librutokens_la_CFLAGS = $(PCSC_CFLAGS) $(LIBUSB_CFLAGS) $(PTHREAD_CFLAGS) \
	$(SYMBOL_VISIBILITY) -D$(CCID_VERSION)

EXTRA_DIST = Info.plist.src create_Info_plist.pl reader.conf.in \
	openct/LICENSE \
	convert_version.pl pcscd_ccid.rules

install: $(LIBS_TO_INSTALL)

if UDEV
ifdCapabilities=0x00000001
INSTALL_UDEV_RULE_FILE=@echo -e "\n\33[01;31m***************\n" ; echo "copy the src/pcscd_ccid.rules file in udev directory (/etc/udev/rules.d/)" ; echo -e "\n***************\n\33[0m"
else
ifdCapabilities=0x00000000
endif

Info.plist: Info.plist.src $(srcdir)/../readers/supported_readers.txt
	$(srcdir)/create_Info_plist.pl $(srcdir)/../readers/supported_readers.txt $(srcdir)/Info.plist.src $(ifdCapabilities) | sed s/TARGET/$(CCID_LIB)/ > Info.plist
	
DISTCLEANFILES = Info.plist

install_ccid: librutokens.la Info.plist
	$(mkinstalldirs) $(DESTDIR)$(usbdropdir)/$(CCID_BUNDLE)/Contents/$(BUNDLE_HOST)/
	cp Info.plist $(DESTDIR)$(usbdropdir)/$(CCID_BUNDLE)/Contents/
	cp .libs/$(CCID_LIB) $(DESTDIR)$(usbdropdir)/$(CCID_BUNDLE)/Contents/$(BUNDLE_HOST)/$(CCID_LIB).$(VERSION)
	ln -fs $(CCID_LIB).$(VERSION) $(DESTDIR)$(usbdropdir)/$(CCID_BUNDLE)/Contents/$(BUNDLE_HOST)/$(CCID_LIB)
	$(INSTALL_UDEV_RULE_FILE)

uninstall: uninstall_ccid

uninstall_ccid:
	rm -rf $(DESTDIR)$(usbdropdir)/$(CCID_BUNDLE)

