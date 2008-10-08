%define module_api 0.4
%define module_dir %{_libdir}/qore-module-api-%{module_api}

%if 0%{?sles_version}

%if 0%{?sles_version} == 10
%define dist .sle10
%endif

%if 0%{?sles_version} == 9
%define dist .sle9
%endif

%else
%if 0%{?suse_version}

%if 0%{?suse_version} == 1100
%define dist .opensuse11
%endif

%if 0%{?suse_version} == 1030
%define dist .opensuse10.3
%endif

%if 0%{?suse_version} == 1020
%define dist .opensuse10.2
%endif

%if 0%{?suse_version} == 1010
%define dist .suse10.1
%endif

%if 0%{?suse_version} == 1000
%define dist .suse10
%endif

%if 0%{?suse_version} == 930
%define dist .suse9.3
%endif

%endif
%endif

Summary: PostgreSQL DBI module for Qore
Name: qore-mysql-module
Version: 1.0
Release: 1%{dist}
License: GPL
Group: Development/Languages
URL: http://www.qoretechnologies.com/qore
Source: http://prdownloads.sourceforge.net/qore/%{name}-%{version}.tar.gz
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
Requires: /usr/bin/env
Requires: qore-module-api-0.4
BuildRequires: gcc-c++
BuildRequires: qore-devel
%if 0%{?suse_version}
Requires: libmysqlclient_r15
BuildRequires: libmysqlclient-devel
%else
Requires: mysql-libs
BuildRequires: mysql-devel
%endif
BuildRequires: qore

%description
MySQL DBI driver module for the Qore Programming Language. The MySQL driver is
character set aware and supports multithreading, transaction management, and
stored procedure execution.


%if 0%{?suse_version}
%debug_package
%endif

%prep
%setup -q
%ifarch x86_64 ppc64 x390x
c64=--enable-64bit
%endif
./configure RPM_OPT_FLAGS="$RPM_OPT_FLAGS" --prefix=/usr --disable-debug $c64

%build
%{__make}

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/%{module_dir}
mkdir -p $RPM_BUILD_ROOT/usr/share/doc/qore-mysql-module
make install DESTDIR=$RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%{module_dir}/mysql.qmod
%doc COPYING README ChangeLog AUTHORS test/db-test.q docs/mysql-module-doc.html

%changelog
* Tue Sep 2 2008 David Nichols <david_nichols@users.sourceforge.net>
- initial spec file for separate mysql release
