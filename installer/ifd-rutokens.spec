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


%prep


%install
pwd_dir=`pwd`
mkdir -p "${RPM_BUILD_ROOT}/%{_libdir}/pcsc/drivers/"
cp -r %{SOURCE0} "${RPM_BUILD_ROOT}/%{_libdir}/pcsc/drivers/"
mkdir -p "${RPM_BUILD_ROOT}/%{_etcdir}/udev/rules.d/"


cd "${RPM_BUILD_ROOT}/%{_etcdir}/udev/rules.d/"
cp %{SOURCE1}  %rules_file


%clean

rm -rf $RPM_BUILD_ROOT


%post
if [ $1 -eq 1 ]; then
	%{_initrddir}/pcscd try-restart &>/dev/null || :
	/bin/sh -c "udevadm control --reload-rules" &>/dev/null || :
	/bin/sh -c "/sbin/udevcontrol reload-rules" &>/dev/null || :
fi

%postun
%{_initrddir}/pcscd try-restart &>/dev/null || :
/bin/sh -c "udevadm control --reload-rules" &>/dev/null || :
/bin/sh -c "/sbin/udevcontrol reload-rules" &>/dev/null || :

%files
%defattr(0644,root,root, 0755)
%{_libdir}/pcsc/drivers/ifd-rutokens.bundle/Contents/Linux/librutokens.so
%attr(0755, root, -) %{_libdir}/pcsc/drivers/ifd-rutokens.bundle/Contents/Linux/librutokens.so.%{version}
%{_libdir}/pcsc/drivers/ifd-rutokens.bundle/Contents/Info.plist
%{_etcdir}/udev/rules.d/95-rutokens.rules

