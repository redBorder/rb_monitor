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
cp rb-monitor.init %{buildroot}/etc/init/rb-monitor.conf
mkdir -p %{buildroot}/etc/rb-monitor
cp config.json %{buildroot}/etc/rb-monitor

%clean
rm -rf %{buildroot}

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(755,root,root)
/opt/rb/bin/rb_monitor
%defattr(644,root,root)
/opt/rb/etc/rb-monitor/config.json
/etc/init/rb-monitor.conf

%changelog
* Wed May 11 2016 Juan J. Prieto <jjprieto@redborder.com> - 1.0.0-1
- first spec version


