Name:    rb_monitor
Version: %{__version}
Release: %{__release}%{?dist}

License: GNU AGPLv3
URL: https://github.com/redBorder/rb_monitor
Source0: %{name}-%{version}.tar.gz

BuildRequires: gcc librd-devel net-snmp-devel json-c-devel librdkafka-devel libmatheval-devel libpcap-devel

Summary: Get data events via SNMP or scripting and send results in json over kafka.
Group:   Development/Libraries/C and C++
Requires: librd
%description
%{summary}

%prep
%setup -qn %{name}-%{version}

%build
./configure --prefix=/usr
make

%install
DESTDIR=%{buildroot} make install
mkdir -p %{buildroot}/etc/init
mkdir -p %{buildroot}/usr/share/rb_monitor
mkdir -p %{buildroot}/etc/rb-monitor
install -D -m 644 rb-monitor.service %{buildroot}/usr/lib/systemd/system/rb-monitor.service
install -D -m 644 config.json %{buildroot}/usr/share/rb_monitor

%clean
rm -rf %{buildroot}

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(755,root,root)
/usr/bin/rb_monitor
%defattr(644,root,root)
/usr/share/rb_monitor/config.json
/usr/lib/systemd/system/rb-monitor.service

%changelog
* Wed May 11 2016 Juan J. Prieto <jjprieto@redborder.com> - 1.0.0-1
- first spec version


