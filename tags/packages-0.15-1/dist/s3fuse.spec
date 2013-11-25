%define debug_package %{nil}

Summary: FUSE Driver for AWS S3 and Google Storage
Name: %{name}
Version: %{version}
Release: %{release}
Source: %{pkg_name}.tar.gz
Group: Applications/System
BuildRoot: %{_builddir}/%{name}-root
License: Apache-2.0

%description
Provides a FUSE filesystem driver for Amazon AWS S3 and Google Storage buckets.

%prep
%setup -q -n %{pkg_name}

%build
./configure --prefix=%_prefix --sysconfdir=%_sysconfdir
make

%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install
echo sysconfdir %_sysconfdir
echo prefix %_prefix
echo bindir %_bindir
echo mandir %_mandir

%clean
rm -rf $RPM_BUILD_ROOT

%post

%preun

%files
%defattr(-,root,root)
%config %_sysconfdir/%{name}.conf
%doc ChangeLog
%doc COPYING
%doc INSTALL
%doc README
%_mandir/man*/%{name}*
%_bindir/%{name}*
