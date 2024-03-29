%define mod_ver 2.1
%define module_api %(qore --latest-module-api 2>/dev/null)
%define module_dir %{_libdir}/qore-modules

%if 0%{?sles_version}

%define dist .sles%{?sles_version}

%else
%if 0%{?suse_version}

# get *suse release major version
%define os_maj %(echo %suse_version|rev|cut -b3-|rev)
# get *suse release minor version without trailing zeros
%define os_min %(echo %suse_version|rev|cut -b-2|rev|sed s/0*$//)

%if %suse_version > 1010
%define dist .opensuse%{os_maj}_%{os_min}
%else
%define dist .suse%{os_maj}_%{os_min}
%endif

%endif
%endif

# see if we can determine the distribution type
%if 0%{!?dist:1}
%define rh_dist %(if [ -f /etc/redhat-release ];then cat /etc/redhat-release|sed "s/[^0-9.]*//"|cut -f1 -d.;fi)
%if 0%{?rh_dist}
%define dist .rhel%{rh_dist}
%else
%define dist .unknown
%endif
%endif

Summary: MySQL DBI module for Qore
Name: qore-mysql-module
Version: %{mod_ver}
Release: 1%{dist}
License: GPL
Group: Development/Languages
URL: http://www.qoretechnologies.com/qore
Source: http://prdownloads.sourceforge.net/qore/%{name}-%{version}.tar.bz2
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
Requires: /usr/bin/env
Requires: qore-module(abi)%{?_isa} = %{module_api}
BuildRequires: gcc-c++
BuildRequires: qore-devel >= 0.9
%if 0%{?sles_version}
BuildRequires: mysql-devel
%else
%if 0%{?suse_version}
BuildRequires: libmysqlclient-devel
%else
BuildRequires: mysql-devel
%endif
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
./configure RPM_OPT_FLAGS="$RPM_OPT_FLAGS" --prefix=/usr --disable-debug

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
%{module_dir}
%doc COPYING.GPL COPYING.LGPL README RELEASE-NOTES ChangeLog AUTHORS


%package doc
Summary: MySQL DBI module for Qore
Group: Development/Languages

%description doc
MySQL module for the Qore Programming Language.

This RPM provides API documentation, test and example programs

%files doc
%defattr(-,root,root,-)
%doc docs/mysql/html test/db-test.q test/sql-stmt.q

%changelog
* Tue Jan 25 2022 David Nichols <david@qore.org> 2.1
- updated version to 2.1

* Tue May 1 2018 David Nichols <david@qore.org> 2.0.2.1
- updated version to 2.0.2.1

* Sun Dec 1 2013 David Nichols <david@qore.org> 2.0.2
- updated version to 2.0.2

* Sun Nov 18 2012 David Nichols <david@qore.org> 2.0.1
- updated version to 2.0.1

* Wed Sep 26 2012 David Nichols <david@qore.org> 2.0
- updated version to 2.0

* Fri Apr 16 2010 Petr Vanek <petr.vanek@qoretechnologies.com> 1.0.8
- updated version to 1.0.8 and various typos fixed

* Thu Jun 25 2009 David Nichols <david_nichols@users.sourceforge.net>
- updated version to 1.0.6

* Thu Apr 30 2009 David Nichols <david_nichols@users.sourceforge.net>
- updated version to 1.0.5

* Tue Mar 3 2009 David Nichols <david_nichols@users.sourceforge.net>
- updated version to 1.0.4

* Mon Mar 2 2009 David Nichols <david_nichols@users.sourceforge.net>
- updated version to 1.0.3

* Sat Jan 3 2009 David Nichols <david_nichols@users.sourceforge.net>
- updated version to 1.0.2

* Tue Sep 2 2008 David Nichols <david_nichols@users.sourceforge.net>
- initial spec file for separate mysql release
