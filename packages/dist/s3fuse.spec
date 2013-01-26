%define name s3fuse
%define version 0.13
%define release 1

%define debug_package %{nil}

Summary: FUSE Driver for AWS S3 and Google Storage
Name: %{name}
Version: %{version}
Release: %{release}
Source: %{name}-%{version}.tar.gz
Group: Applications/System
BuildRoot: %{_builddir}/%{name}-root
License: Apache-2.0

%description
Provides a FUSE filesystem driver for Amazon AWS S3 and Google Storage buckets.

%prep
%setup -q -n %{name}-%{version}

%build
autoreconf --force --install
./configure --prefix=%_prefix
make

%install
rm -rf $RPM_BUILD_ROOT
make DESTDIR=$RPM_BUILD_ROOT install

%clean
rm -rf $RPM_BUILD_ROOT

%post

%preun

%files
%defattr(-,root,root)
%config %_sysconfdir/s3fuse.conf
%doc ChangeLog
%doc COPYING
%doc INSTALL
%doc README
%_mandir/man*/s3fuse*
%_bindir/s3fuse
%_bindir/s3fuse_gen_key
%_bindir/s3fuse_gs_get_token
%_bindir/s3fuse_sha256_sum
