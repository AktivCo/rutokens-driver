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


%prep


%install
pwd_dir=`pwd`
mkdir -p "${RPM_BUILD_ROOT}/opt/aktivco/ifd-rutokens/%{target_arch}"
cp -r %{SOURCE0} "${RPM_BUILD_ROOT}/opt/aktivco/ifd-rutokens/%{target_arch}/"
mkdir -p "${RPM_BUILD_ROOT}/%{_libdir}/pcsc/drivers/ifd-rutokens.bundle/Contents/Linux"
mkdir -p "${RPM_BUILD_ROOT}/%{_etcdir}/udev/rules.d/"
cp %{SOURCE1} "${RPM_BUILD_ROOT}/opt/aktivco/ifd-rutokens/"
RULES_NAME=`basename "%{SOURCE1}"`


cd "${RPM_BUILD_ROOT}/%{_etcdir}/udev/rules.d/"
mv "${RPM_BUILD_ROOT}/opt/aktivco/ifd-rutokens/"${RULES_NAME} %rules_file
cd "${pwd_dir}"
cd "${RPM_BUILD_ROOT}/%{_libdir}/pcsc/drivers/ifd-rutokens.bundle/Contents/Linux"
ln -sf /opt/aktivco/ifd-rutokens/%{target_arch}/ifd-rutokens.bundle/Contents/Linux/librutokens.so.%{version} librutokens.so
cd "${pwd_dir}"
cd "${RPM_BUILD_ROOT}/%{_libdir}/pcsc/drivers/ifd-rutokens.bundle/Contents"
mv "${RPM_BUILD_ROOT}/opt/aktivco/ifd-rutokens/%{target_arch}/ifd-rutokens.bundle/Contents/Info.plist" Info.plist
cd "${pwd_dir}"


%clean

rm -rf $RPM_BUILD_ROOT


%post
if [ $1 -eq 1 ]; then
	%{_initrddir}/pcscd try-restart &>/dev/null || :
fi

%postun
%{_initrddir}/pcscd try-restart &>/dev/null || :

%files
%defattr(0644,root,root, 0755)
/opt/aktivco/ifd-rutokens
%{_libdir}/pcsc/drivers/ifd-rutokens.bundle/Contents/Linux/librutokens.so
%attr(0755, root, -) /opt/aktivco/ifd-rutokens/%{target_arch}/ifd-rutokens.bundle/Contents/Linux/librutokens.so.%{version}
%{_libdir}/pcsc/drivers/ifd-rutokens.bundle/Contents/Info.plist
%{_etcdir}/udev/rules.d/95-rutokens.rules

