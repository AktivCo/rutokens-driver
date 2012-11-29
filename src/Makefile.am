# $Id: Makefile.am 3024 2008-06-26 13:20:43Z rousseau $

RUTOKENS_BUNDLE = $(bundle)
RUTOKENS_LIB = librutokens.$(DYN_LIB_EXT)

RUTOKENS_VERSION=RUTOKENS_VERSION=`$(srcdir)/convert_version.pl $(PACKAGE_VERSION)`

lib_LTLIBRARIES =
LIBS_TO_INSTALL =
if WITH_LIBUSB
lib_LTLIBRARIES += librutokens.la
LIBS_TO_INSTALL += install_rutokens
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
USB = rutokens_usb.c rutokens_usb.h
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
	$(SYMBOL_VISIBILITY) -D$(RUTOKENS_VERSION)

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
	$(srcdir)/create_Info_plist.pl $(srcdir)/../readers/supported_readers.txt $(srcdir)/Info.plist.src $(ifdCapabilities) | sed s/TARGET/$(RUTOKENS_LIB)/ > Info.plist
	
DISTCLEANFILES = Info.plist

install_rutokens: librutokens.la Info.plist
	$(mkinstalldirs) $(DESTDIR)$(usbdropdir)/$(RUTOKENS_BUNDLE)/Contents/$(BUNDLE_HOST)/
	cp Info.plist $(DESTDIR)$(usbdropdir)/$(RUTOKENS_BUNDLE)/Contents/
	cp .libs/$(RUTOKENS_LIB) $(DESTDIR)$(usbdropdir)/$(RUTOKENS_BUNDLE)/Contents/$(BUNDLE_HOST)/$(RUTOKENS_LIB).$(VERSION)
	ln -fs $(RUTOKENS_LIB).$(VERSION) $(DESTDIR)$(usbdropdir)/$(RUTOKENS_BUNDLE)/Contents/$(BUNDLE_HOST)/$(RUTOKENS_LIB)
	$(INSTALL_UDEV_RULE_FILE)

uninstall: uninstall_rutokens

uninstall_rutokens:
	rm -rf $(DESTDIR)$(usbdropdir)/$(RUTOKENS_BUNDLE)

