%define module intel-i915-dkms
%define version 0.6379.0.28.221103.25.5.14.21.150400.24.33
%define release 1
%define deppmtpkg intel-platform-vsec-dkms

Summary: I915 Backports Module DKMS Package
Name: %{module}
Version: %{version}
Release: %{release}
License: GPL
Group: System Environment/Base
BuildArch: x86_64
Vendor: Intel
Provides: %{module}
Packager: linux-graphics@intel.com
Requires: dkms gcc bash sed intel-platform-cse-dkms lsb-release %{deppmtpkg}
# There is no Source# line for dkms.conf since it has been placed
# into the source tarball of SOURCE0
Source0: %{module}-%{version}-src.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root/

%description
Out of tree i915 driver for SLES15SP3 kernel Version 5.3.18.
Installed as dkms module.

%prep
rm -rf %{module}-%{version}
mkdir %{module}-%{version}
cd %{module}-%{version}
tar xvzf $RPM_SOURCE_DIR/%{module}-%{version}-src.tar.gz

%install
if [ "%{buildroot}" != "/" ]; then
rm -rf %{buildroot}
fi
mkdir -p %{buildroot}/usr/src/%{module}-%{version}/
cp -rf %{module}-%{version}/* %{buildroot}/usr/src/%{module}-%{version}

%clean
if [ "%{buildroot}" != "/" ]; then
rm -rf %{buildroot}
fi

%files
%defattr (-, root, root)
/usr/src/%{module}-%{version}/

%pre

%post
/usr/sbin/dkms add -m %module -v %version --rpm_safe_upgrade
for i in /lib/modules/5.14*; do
H="$i/build";
K=$(echo $i | cut -d '/' -f 4);
if [ -d $H ]; then
dkms install --force -m %module -v %version -k $K;
else
echo "SKIP DKMS Installation: kernel Headers not available for variant $K";
fi
done
echo -e
echo -e "Creating initramfs for :$(uname -r)"
if [ -e /boot/vmlinuz-$(uname -r) ] && [ -e /sbin/depmod ]; then
/sbin/depmod -a "$(uname -r)"
/sbin/mkinitrd
else
echo -e "Unable to create initramfs for :$(uname -r)"
fi
exit 0

%preun
echo -e
echo -e "Uninstall of %{module} module (version %{version}) beginning:"
for i in /var/lib/dkms/%{module}/%{version}/5.14*; do
K=$(echo $i | cut -d '/' -f 7);
/usr/sbin/dkms remove -m %{module} -v %{version} -k $K --rpm_safe_upgrade
done
echo -e "Creating initramfs for :$(uname -r)"
if [ -e /boot/vmlinuz-$(uname -r) ] && [ -e /sbin/depmod ]; then
/sbin/depmod -a "$(uname -r)"
/sbin/mkinitrd
else
echo -e "Unable to create initramfs for :$(uname -r)"
fi
exit 0
