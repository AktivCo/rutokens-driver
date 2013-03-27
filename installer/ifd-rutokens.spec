Name:           ifd-rutokens
Url:            http://www.rutoken.ru/
Vendor:         Aktiv Co.
Packager:       Aktiv Co. <hotline@rutoken.ru>
Version:        %{version}
Release:        1
Group:          System Environment/Libraries
Summary:        Aktiv Co Rutoken S driver
License:        GPL
Requires:       libusb
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
Source0:        ifd-rutokens.bundle
Source1:        95-rutokens.rules

%description
Allows users to access Rutoken S through pcsc-lite.


%ifarch x86_64
%define target_arch x86_64
%else
%define target_arch x86
%endif

%define rules_file 95-rutokens.rules

%define _etcdir /etc

%define _binary_filedigest_algorithm 1
%define _source_filedigest_algorithm 1
%define _binary_payload w9.gzdio

%define usbdropdir %{_libdir}/pcsc/drivers/


%prep


%install
pwd_dir=`pwd`

mkdir -p "${RPM_BUILD_ROOT}/%{usbdropdir}"
cp -r %{SOURCE0} "${RPM_BUILD_ROOT}/%{usbdropdir}/"
mkdir -p "${RPM_BUILD_ROOT}/%{_etcdir}/udev/rules.d/"

cd "${RPM_BUILD_ROOT}/%{_etcdir}/udev/rules.d/"
cp %{SOURCE1}  %rules_file


%clean

rm -rf $RPM_BUILD_ROOT


%post

%{_initrddir}/pcscd try-restart &>/dev/null || :
/bin/sh -c "udevadm control --reload-rules" &>/dev/null || :
/bin/sh -c "/sbin/udevcontrol reload-rules" &>/dev/null || :

local_usbdropdir=`pkg-config libpcsclite --variable=usbdropdir 2>/dev/null`
%ifarch x86_64
if [ -d "/usr/lib/pcsc/drivers/" -a -z "${local_usbdropdir}" ] ; then
	ln -sf "%{usbdropdir}/%{name}.bundle" "/usr/lib/pcsc/drivers/%{name}.bundle" &>/dev/null
fi
%endif

if [ -n "${local_usbdropdir}" -a "${local_usbdropdir}"!="%{usbdropdir}" ] ; then
	ln -sf "%{usbdropdir}/%{name}.bundle" "${local_usbdropdir}/%{name}.bundle" &>/dev/null
fi


%postun

%{_initrddir}/pcscd try-restart &>/dev/null || :
/bin/sh -c "udevadm control --reload-rules" &>/dev/null || :
/bin/sh -c "/sbin/udevcontrol reload-rules" &>/dev/null || :

local_usbdropdir=`pkg-config libpcsclite --variable=usbdropdir 2>/dev/null`
if [ -n "${local_usbdropdir}" -a -h "${local_usbdropdir}/%{name}.bundle" ] ; then
	rm -rf "${local_usbdropdir}/%{name}.bundle" &>/dev/null
fi

if [ -h /usr/lib/pcsc/drivers/%{name}.bundle ] ; then
	rm -rf "/usr/lib/pcsc/drivers/%{name}.bundle" &>/dev/null
fi

%files
%defattr(0644,root,root, 0755)
%{usbdropdir}/ifd-rutokens.bundle/Contents/Linux/librutokens.so
%attr(0755, root, -) %{usbdropdir}/ifd-rutokens.bundle/Contents/Linux/librutokens.so.%{version}
%{usbdropdir}/ifd-rutokens.bundle/Contents/Info.plist
%{_etcdir}/udev/rules.d/95-rutokens.rules

