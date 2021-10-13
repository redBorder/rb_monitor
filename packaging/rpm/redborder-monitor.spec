Name:    redborder-monitor
Version: %{__version}
Release: %{__release}%{?dist}

License: GNU AGPLv3
URL: https://github.com/redBorder/rb_monitor
Source0: %{name}-%{version}.tar.gz

BuildRequires: gcc git librd-devel net-snmp-devel json-c-devel libmatheval-devel
#BuildRequires: librdkafka-devel

Summary: Get data events via SNMP or scripting and send results in json over kafka.
Group:   Development/Libraries/C and C++
Requires: librd0 libmatheval librdkafka1 net-snmp
Requires(pre): shadow-utils

%description
%{summary}

%prep
%setup -qn %{name}-%{version}

%build
git clone --branch v0.9.2 https://github.com/edenhill/librdkafka.git /tmp/librdkafka-v0.9.2
cd /tmp/librdkafka-v0.9.2
#make uninstall
./configure --prefix=/usr --sbindir=/usr/bin --exec-prefix=/usr && make
make install
cd -
ldconfig

export PKG_CONFIG_PATH=/usr/lib/pkgconfig
ls /usr/lib/pkgconfig

./configure --prefix=/usr
make

%install
export PKG_CONFIG_PATH=/usr/lib/pkgconfig

DESTDIR=%{buildroot} make install
mkdir -p %{buildroot}/usr/share/redborder-monitor
mkdir -p %{buildroot}/etc/redborder-monitor
install -D -m 644 redborder-monitor.service %{buildroot}/usr/lib/systemd/system/redborder-monitor.service
install -D -m 644 packaging/rpm/config.json %{buildroot}/usr/share/redborder-monitor

%clean
rm -rf %{buildroot}

%pre
getent group redborder-monitor >/dev/null || groupadd -r redborder-monitor
getent passwd redborder-monitor >/dev/null || \
    useradd -r -g redborder-monitor -d / -s /sbin/nologin \
    -c "User of redborder_monitor service" redborder-monitor
exit 0

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(755,root,root)
/usr/bin/rb_monitor
%defattr(644,root,root)
/usr/share/redborder-monitor/config.json
/usr/lib/systemd/system/redborder-monitor.service

%changelog
* Wed May 11 2016 Juan J. Prieto <jjprieto@redborder.com> - 1.0.0-1
- first spec version
