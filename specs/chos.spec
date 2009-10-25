%define module chos
%define version 0.07
%define release 3

#
# DKMS is a utility for managing modules outside of the kernel.
# It can be obtained from http://linux.dell.com/dkms/dkms.html
# Set the value to 1 if you plan to use dkms.
%define usedkms 1
# Optionally, you can distribute a pre-built binary tar file with the
# rpm.  The systems wouldn't need the kernel source then.
%define withdkmstarfile 0
%define dkmstarfilever 2.4.20-31psmp

#
# Set the value to 1 if you wish to build a kernel module.
# Note that this will build a module for the kernel running on the build
# host.
#
%define buildmod 0
#%define initdir /etc/rc.d/init.d

Summary: chos utilities
Name: %{module}
Version: %{version}
Release: %{release}
Vendor: PDSF
License: GPL
Packager: Shane Canon <canon@nersc.gov>
Group: System Environment/Base
Source0: %{module}-%{version}.tgz
%if %{withdkmstarfile}
Source1: %{module}-%{version}-kernel%{dkmstarfilever}.dkms.tar.gz
%endif
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root/
%if %{usedkms}
Requires: dkms >= 1.00
%endif
%if %{buildmod}
BuildRequires: kernel-source
%endif
Requires: bash

%description
CHOS is a framework that allows a system to support multiple Linux distributions
concurrently on a single system.  For example, two users can simulataneously
use two different Linux distributions at the same time on the same system.

%if %{usedkms}
This package contains the chos kernel module wrapped for
the DKMS framework.
%endif

%prep
%setup

%build
%configure \
        --enable-securedir=/%{_lib}/security \
%if %{buildmod}
        --enable-kernel=yes \
%endif
        --enable-fakeroot=$RPM_BUILD_ROOT
make

%install
if [ "$RPM_BUILD_ROOT" != "/" ]; then
	rm -rf $RPM_BUILD_ROOT
fi
%makeinstall

%if %{usedkms}
cd kernel
make installdkms
cd ..
#mkdir -p $RPM_BUILD_ROOT/usr/src/%{module}-%{version}/
#mkdir -p $RPM_BUILD_ROOT/usr/src/%{module}-%{version}/patches
#mkdir -p $RPM_BUILD_ROOT/usr/src/%{module}-%{version}/redhat_driver_disk
#cp -rf kernel/* $RPM_BUILD_ROOT/usr/src/%{module}-%{version}
%if %{withdkmstarfile}
install -m 644 %{SOURCE1} $RPM_BUILD_ROOT/usr/src/%{module}-%{version}
%endif
%endif

mkdir -p $RPM_BUILD_ROOT/%{_initrddir}
install -m755  utils/chos.init $RPM_BUILD_ROOT/%{_initrddir}/chos

mkdir $RPM_BUILD_ROOT/chos
./utils/mkchos $RPM_BUILD_ROOT/chos



%clean
if [ "$RPM_BUILD_ROOT" != "/" ]; then
	rm -rf $RPM_BUILD_ROOT
fi

%files
%defattr(-,root,root)

%if %{usedkms}
/usr/src/%{module}-%{version}/
%endif
%if %{buildmod}
/lib/modules
%endif

%doc  README LICENSE INSTALL NOTICE
%doc test/check_chos
%doc utils/job_starter.c
%doc utils/pam_job_starter.c
/chos
%config /etc/chos
%config /etc/chos.conf
/%{_lib}/security/pam_*.so
/usr/bin/mklocal
%{_mandir}/man1/chos.1*
%config /%{_initrddir}/*
%defattr(755,root,root)
/usr/bin/chos


%pre

%post

%if %{usedkms}
dkms add -m %{module} -v %{version} --rpm_safe_upgrade

# Load tarballs as necessary
loaded_tarballs=""

%if %{withdkmstarfile}
for kernel_name in %{dkmstarfilever}; do
        if [ `uname -r | grep -c "$kernel_name"` -gt 0 ] && [ `uname -m | grep -c "i*86"` -gt 0 ]; then
                echo -e ""
                echo -e "Loading/Installing pre-built modules for $kernel_name."
                dkms ldtarball --archive=/usr/src/%{module}-%{version}/%{module}-%{version}-kernel${kernel_name}.dkms.tar.gz >/dev/null
                dkms install -m %{module} -v %{version} -k ${kernel_name} >/dev/null 2>&1
                dkms install -m %{module} -v %{version} -k ${kernel_name}smp >/dev/null 2>&1
		loaded_tarballs="true"
        fi
done
%endif

# If we haven't loaded a tarball, then try building it for the current kernel
if [ -z "$loaded_tarballs" ]; then
	if [ `uname -r | grep -c "BOOT"` -eq 0 ] && [ -e /boot/config-`uname -r` ]; then
		dkms build -m %{module} -v %{version} --config /boot/config-`uname -r`
		dkms install -m %{module} -v %{version}
	elif [ `uname -r | grep -c "BOOT"` -eq 0 ] && [ -e /lib/modules/`uname -r`/build/include ]; then
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
%endif

/sbin/chkconfig chos on
/etc/init.d/chos start

echo ""
echo "If you run automounters, be sure to replicate all entries and prepend the paths with /chos."
echo "This is needed to insure that CHOS environments can access these file systems."
echo "After modifying the auto.master file, do an /etc/init.d/autofs reload to refresh"
echo "the daemons."
echo ""
echo "If you use AFS, it is recommended to mount afs under /chos/afs and add a symlink for "
echo "/afs -> /chos/afs.  This will allow CHOS environments to access AFS. This will require a"
echo "reboot to take effect."
echo ""
echo "Finally, add"
echo "  session    optional     pam_chos.so"
echo "to the pam configuration for sshd, so that users automatically get there CHOS OS."
exit 0

%preun
%if %{usedkms}
echo -e
echo -e "Uninstall of chos module (version %{version}) beginning:"
dkms remove -m %{module} -v %{version} --all --rpm_safe_upgrade
exit 0
%endif %{usedkms}

%changelog
* Tue May 24 2005 Shane Canon <canon@nersc.gov>
- Added pam_job_starter
- Modified pam_chos to check CHOS env. variable

* Thu Jun 10 2004 Shane Canon <canon@nersc.gov>
- autoconf support
- adjustments to dkms
- tweaks to spec file
- add job_starter example

* Fri Jun  4 2004 Shane Canon <canon@nersc.gov>
- Made dkms optional
- Added support to build kernel module in spec file
- General cleanup of spec file 

* Thu May 27 2004 Shane Canon <canon@nersc.gov>
- Preparing for initial release
- Added license
- General cleanup

* Thu Mar 11 2004 Shane Canon <canon@nersc.gov>
- Created version 3
- Added init script
- Fixed permission problem on /chos directory

