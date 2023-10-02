%if 0%{?rhel} <= 6
Name:    rb_monitor
%endif
%if 0%{?rhel} > 6
Name:    redborder-monitor
%endif
Version: %{__version}
Release: %{__release}%{?dist}

License: GNU AGPLv3
URL: https://github.com/redBorder/rb_monitor
Source0: %{name}-%{version}.tar.gz

BuildRequires: gcc librd-devel net-snmp-devel json-c-devel librdkafka-devel libmatheval-devel

Summary: Get data events via SNMP or scripting and send results in json over kafka.
Group:   Development/Libraries/C and C++
Requires: librd0 libmatheval librdkafka net-snmp
Requires(pre): shadow-utils

%description
%{summary}

%prep
%setup -qn %{name}-%{version}

%build
./configure --prefix=/usr
make

%install
DESTDIR=%{buildroot} make install
mkdir -p %{buildroot}/usr/share/%{name}
mkdir -p %{buildroot}/etc/%{name}
install -D -m 644 %{name}.service %{buildroot}/usr/lib/systemd/system/%{name}.service
install -D -m 644 packaging/rpm/config.json %{buildroot}/usr/share/%{name}

%clean
rm -rf %{buildroot}

%pre
getent group %{name} >/dev/null || groupadd -r %{name}
getent passwd %{name} >/dev/null || \
    useradd -r -g %{name} -d / -s /sbin/nologin \
    -c "User of %{name} service" %{name}
exit 0

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(755,root,root)
/usr/bin/rb_monitor
%defattr(644,root,root)
/usr/share/%{name}/config.json
/usr/lib/systemd/system/%{name}.service

%changelog
* Wed May 11 2016 Juan J. Prieto <jjprieto@redborder.com> - 1.0.0-1
- first spec version
