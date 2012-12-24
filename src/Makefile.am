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
	commands.c \
	commands.h \
	convert_apdu.c \
	convert_apdu.h \
	debug.h \
	defs.h \
	ifdhandler.c \
	infopath.h \
	infopath.c \
	rutokens.h \
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

EXTRA_DIST = Info.plist.src \
	convert_version.pl 95-rutokens.rules

install: $(LIBS_TO_INSTALL)

if DISABLE_UDEV_RULES
INSTALL_UDEV_RULE_FILE=@echo -e "To support RutokenS copy src/95-rutokens.rules to /etc/udev/rules.d/"
else
INSTALL_UDEV_RULE_FILE=@cp -f ./95-rutokens.rules /etc/udev/rules.d/ ; echo -e "RutonenS udev rules file is copied to /etc/udev/rules.d/"
endif

Info.plist: Info.plist.src
	cat $(srcdir)/Info.plist.src | sed s/BUNDLE/$(bundle)/\
	| sed s/VERSION/$(PACKAGE_VERSION)/ | sed s/TARGET/$(RUTOKENS_LIB)/ > Info.plist
	
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

