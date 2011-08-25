%ifarch alpha ia64 ppc64 s390x sparc64 x86_64
%global flavor gcc64
%else
%global flavor gcc32
%endif


%if "%{?rhel}" == "5"
%global docdiroption "with-docdir"
%else
%global docdiroption "docdir"
%endif

%{!?perl_vendorlib: %global perl_vendorlib %(eval "`perl -V:installvendorlib`"; echo $installvendorlib)}

Name:		globus-gram-job-manager-pbs
%global _name %(tr - _ <<< %{name})
Version:	0.0
Release:	5%{?dist}
Summary:	Globus Toolkit - PBS Job Manager

Group:		Applications/Internet
License:	ASL 2.0
URL:		http://www.globus.org/
Source:		%{_name}-%{version}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Obsoletes:      globus-gram-job-manager-setup-pbs < 4.5

Requires:       globus-gram-job-manager-scripts >= 3.4
Requires:	globus-gass-cache-program >= 4
Requires:	globus-common-progs >= 2
Requires:       torque-client
Requires:	perl(:MODULE_COMPAT_%(eval "`perl -V:version`"; echo $version))
BuildRequires:	grid-packaging-tools
BuildRequires:	globus-core
BuildRequires:	globus-common-devel
BuildRequires:	globus-xio-devel
BuildRequires:	globus-scheduler-event-generator-devel
BuildRequires:	globus-gram-protocol-devel
BuildRequires:	doxygen
BuildRequires:	graphviz
%if "%{?rhel}" == "5"
BuildRequires:	graphviz-gd
%endif
BuildRequires:	ghostscript
%if %{?fedora}%{!?fedora:0} >= 9 || %{?rhel}%{!?rhel:0} >= 6
BuildRequires:	tex(latex)
%else
BuildRequires:	tetex-latex
%endif

%package doc
Summary:	Globus Toolkit - PBS Job Manager Documentation Files
Group:		Documentation
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:      noarch
%endif

Requires:	%{name} = %{version}-%{release}

%package setup-poll
Summary:        Globus Toolkit - PBS Job Manager Setup Files
Group:		Applications/Internet
%if %{?fedora}%{!?fedora:0} >= 10 || %{?rhel}%{!?rhel:0} >= 6
BuildArch:      noarch
%endif
Provides:       %{name}-setup
Provides:       globus-gram-job-manager-setup
Requires:	%{name} = %{version}-%{release}
requires(post): globus-gram-job-manager-scripts >= 3.4
requires(preun): globus-gram-job-manager-scripts >= 3.4
Conflicts:      %{name}-setup-seg

%package setup-seg
Summary:	Globus Toolkit - PBS Job Manager Setup Files
Group:		Applications/Internet
Provides:       %{name}-setup
Provides:       globus-gram-job-manager-setup
Requires:	%{name} = %{version}-%{release}
Requires:       globus-scheduler-event-generator-progs >= 3.1
Requires:       torque-server
requires(post): globus-gram-job-manager-scripts >= 3.4
requires(preun): globus-gram-job-manager-scripts >= 3.4
Conflicts:      %{name}-setup-poll

%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
PBS Job Manager 

%description doc
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name}-doc package contains:
PBS Job Manager Documentation Files

%description setup-poll
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
PBS Job Manager Setup using polling to monitor job state

%description setup-seg
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
PBS Job Manager Setup using SEG to monitor job state

%prep
%setup -q -n %{_name}-%{version}

%build
# Remove files that should be replaced during bootstrap
rm -f doxygen/Doxyfile*
rm -f doxygen/Makefile.am
rm -f pkgdata/Makefile.am
rm -f globus_automake*
rm -rf autom4te.cache

%{_datadir}/globus/globus-bootstrap.sh

export MPIEXEC=no
export MPIRUN=no
export QDEL=/usr/bin/qdel-torque
export QSTAT=/usr/bin/qstat-torque
export QSUB=/usr/bin/qsub-torque
%configure --with-flavor=%{flavor} --enable-doxygen \
           --%{docdiroption}=%{_docdir}/%{name}-%{version} \
           --with-globus-state-dir=%{_localstatedir}/lib/globus \
           --with-log-path=/var/log/torque/server_logs \
           --disable-static

make %{?_smp_mflags}

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
# Remove jobmanager-pbs from install dir so that it can be
# added/removed by post scripts
rm $RPM_BUILD_ROOT/etc/grid-services/jobmanager-pbs

GLOBUSPACKAGEDIR=$RPM_BUILD_ROOT%{_datadir}/globus/packages

# Remove libtool archives (.la files)
find $RPM_BUILD_ROOT%{_libdir} -name 'lib*.la' -exec rm -v '{}' \;
sed '/lib.*\.la$/d' -i $GLOBUSPACKAGEDIR/%{_name}/%{flavor}_dev.filelist


# Generate package filelists
# Main package: pbs.pm and globus-pbs.config
cat $GLOBUSPACKAGEDIR/%{_name}/%{flavor}_rtl.filelist \
    $GLOBUSPACKAGEDIR/%{_name}/noflavor_data.filelist \
  | sed s!^!%{_prefix}! \
  | sed s!^%{_prefix}/etc!/etc! \
  | grep -E 'pbs.pm|globus-pbs.conf|pkg_data_|.filelist' > package.filelist

# setup-poll package: /etc/grid-services/available/job-manager-pbs-poll
cat $GLOBUSPACKAGEDIR/%{_name}/noflavor_data.filelist \
  | sed s!^!%{_prefix}! \
  | sed s!^%{_prefix}/etc!/etc! \
  | grep jobmanager-pbs-poll > package-setup-poll.filelist

# setup-seg package: /etc/grid-services/available/job-manager-pbs-seg
# plus seg module
cat $GLOBUSPACKAGEDIR/%{_name}/%{flavor}_pgm.filelist \
    $GLOBUSPACKAGEDIR/%{_name}/%{flavor}_dev.filelist \
    $GLOBUSPACKAGEDIR/%{_name}/%{flavor}_rtl.filelist \
    $GLOBUSPACKAGEDIR/%{_name}/noflavor_data.filelist \
  | sed s!^!%{_prefix}! \
  | sed s!^%{_prefix}/etc!/etc! \
  | grep -Ev 'jobmanager-pbs-poll|globus-pbs.conf|pbs.pm|pkg_data_%{flavor}_rtl|pkg_data_noflavor_data|%{flavor}_rtl.filelist|noflavor_data.filelist' > package-setup-seg.filelist

cat $GLOBUSPACKAGEDIR/%{_name}/noflavor_doc.filelist \
  | sed 's!^!%doc %{_prefix}!' > package-doc.filelist

%clean
rm -rf $RPM_BUILD_ROOT

%post setup-poll
if [ $1 -ge 1 ]; then
    globus-gatekeeper-admin -e jobmanager-pbs-poll -n jobmanager-pbs > /dev/null 2>&1 || :
    if [ ! -f /etc/grid-services/jobmanager ]; then
        globus-gatekeeper-admin -e jobmanager-pbs-poll -n jobmanager
    fi
fi

%preun setup-poll
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-pbs-poll > /dev/null 2>&1 || :
fi

%postun setup-poll
if [ $1 -ge 1 ]; then
    globus-gatekeeper-admin -e jobmanager-pbs-poll -n jobmanager-pbs > /dev/null 2>&1 || :
elif [ $1 -eq 0 -a ! -f /etc/grid-services/jobmanager ]; then
    globus-gatekeeper-admin -E > /dev/null 2>&1 || :
fi

%post setup-seg
ldconfig
if [ $1 -ge 1 ]; then
    globus-gatekeeper-admin -e jobmanager-pbs-seg -n jobmanager-pbs > /dev/null 2>&1 || :
    globus-scheduler-event-generator-admin -e pbs > /dev/null 2>&1 || :
    service globus-scheduler-event-generator condrestart pbs
fi

%preun setup-seg
ldconfig
if [ $1 -eq 0 ]; then
    globus-gatekeeper-admin -d jobmanager-pbs-seg > /dev/null 2>&1 || :
    globus-scheduler-event-generator-admin -d pbs > /dev/null 2>&1 || :
    service globus-scheduler-event-generator stop pbs > /dev/null 2>&1 || :
fi

%postun setup-seg
if [ $1 -ge 1 ]; then
    globus-gatekeeper-admin -e jobmanager-pbs-seg > /dev/null 2>&1 || :
    globus-scheduler-event-generator-admin -e pbs > /dev/null 2>&1 || :
    service globus-scheduler-event-generator condrestart pbs > /dev/null 2>&1 || :
elif [ $1 -eq 0 -a ! -f /etc/grid-services/jobmanager ]; then
    globus-gatekeeper-admin -E > /dev/null 2>&1 || :
fi

%files -f package.filelist
%defattr(-,root,root,-)
%dir %{_datadir}/globus/packages/%{_name}
%dir %{_docdir}/%{name}-%{version}
%config(noreplace) %{_sysconfdir}/globus/globus-pbs.conf

%files setup-poll -f package-setup-poll.filelist
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-pbs-poll

%files setup-seg -f package-setup-seg.filelist
%defattr(-,root,root,-)
%config(noreplace) %{_sysconfdir}/grid-services/available/jobmanager-pbs-seg

%files doc -f package-doc.filelist
%defattr(-,root,root,-)
%dir %{_docdir}/%{name}-%{version}/html

%changelog
