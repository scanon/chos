%define module chos
%define version 0.02
%define release 1

%define initdir /etc/rc.d/init.d

Summary: chos utilities
Name: %{module}
Version: %{version}
Release: %{release}
Vendor: PDSF
License: GPL
Packager: Shane Canon <canon@nersc.gov>
Group: System Environment/Base
Source0: %{module}-%{version}.tgz
Source1: %{module}-%{version}-kernel-2.4.20-28.7smp.tgz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root/
Requires: dkms >= 1.00
Requires: bash

%description
Chos is a utility that provides the ability to support multiple
Linux releases on a single system.

This package contains the chos module wrapped for
the DKMS framework.


%prep
%setup

%build
cd utils
make
cd ../
cd pam_chos
make

%install
if [ "$RPM_BUILD_ROOT" != "/" ]; then
	rm -rf $RPM_BUILD_ROOT
fi
mkdir -p $RPM_BUILD_ROOT/usr/bin/
mkdir -p $RPM_BUILD_ROOT/usr/src/%{module}-%{version}/
mkdir -p $RPM_BUILD_ROOT/usr/src/%{module}-%{version}/patches
mkdir -p $RPM_BUILD_ROOT/usr/src/%{module}-%{version}/redhat_driver_disk
mkdir -p $RPM_BUILD_ROOT/lib/security
mkdir -p $RPM_BUILD_ROOT/etc
mkdir -p ${RPM_BUILD_ROOT}%{_mandir}/man1
cp -rf kernel/* $RPM_BUILD_ROOT/usr/src/%{module}-%{version}

install -m 755 utils/chos $RPM_BUILD_ROOT/usr/bin/
install -m 755 utils/mklocal $RPM_BUILD_ROOT/usr/bin/
install -m 644 %{SOURCE1} $RPM_BUILD_ROOT/usr/src/%{module}-%{version}
install -m 755 pam_chos/pam_chos.so $RPM_BUILD_ROOT/lib/security/
install -m 644 conf/chos $RPM_BUILD_ROOT/etc/
install -m 644 conf/chos.conf $RPM_BUILD_ROOT/etc/
install -m644 docs/chos.1 ${RPM_BUILD_ROOT}%{_mandir}/man1/chos.1


mkdir -p $RPM_BUILD_ROOT%{initdir}
install -m755  utils/chos.init $RPM_BUILD_ROOT%{initdir}/chos

mkdir $RPM_BUILD_ROOT/chos
./mkchos $RPM_BUILD_ROOT/chos



%clean
if [ "$RPM_BUILD_ROOT" != "/" ]; then
	rm -rf $RPM_BUILD_ROOT
fi

%files
%defattr(-,root,root)
/usr/src/%{module}-%{version}/
%doc  README LICENSE
/chos
%config /etc/chos
%config /etc/chos.conf
/lib/security/pam_chos.so
/usr/bin/mklocal
%{_mandir}/man1/chos.1*
%config %{initdir}/*
%defattr(4755,root,root)
/usr/bin/chos


%pre

%post
dkms add -m %{module} -v %{version} --rpm_safe_upgrade

# Load tarballs as necessary
loaded_tarballs=""
for kernel_name in 2.4.20-28.7smp; do
        if [ `uname -r | grep -c "$kernel_name"` -gt 0 ] && [ `uname -m | grep -c "i*86"` -gt 0 ]; then
                echo -e ""
                echo -e "Loading/Installing pre-built modules for $kernel_name."
                dkms ldtarball --archive=/usr/src/%{module}-%{version}/%{module}-%{version}-kernel-${kernel_name}.tgz >/dev/null
                dkms install -m %{module} -v %{version} -k ${kernel_name} >/dev/null 2>&1
                dkms install -m %{module} -v %{version} -k ${kernel_name}smp >/dev/null 2>&1
		loaded_tarballs="true"
        fi
done

# If we haven't loaded a tarball, then try building it for the current kernel
if [ -z "$loaded_tarballs" ]; then
	if [ `uname -r | grep -c "BOOT"` -eq 0 ] && [ -e /lib/modules/`uname -r`/build/include ]; then
		dkms build -m %{module} -v %{version}
		dkms install -m %{module} -v %{version}
	elif [ `uname -r | grep -c "BOOT"` -gt 0 ]; then
		echo -e ""
		echo -e "Module build for the currently running kernel was skipped since you"
		echo -e "are running a BOOT variant of the kernel."
	else 
		echo -e ""
		echo -e "Module build for the currently running kernel was skipped since the"
		echo -e "kernel source for this kernel does not seem to be installed."
	fi
fi
/usr/bin/mklocal
/sbin/chkconfig chos on
/etc/init.d/chos start
exit 0

%preun
echo -e
echo -e "Uninstall of chos module (version %{version}) beginning:"
dkms remove -m %{module} -v %{version} --all --rpm_safe_upgrade
exit 0

%changelog
* Thu Mar 11 2004 Shane Canon <canon@nersc.gov>
- Created version 3
- Added init script
- Fixed permission problem on /chos directory


