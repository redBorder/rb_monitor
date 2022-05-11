Name:    redborder-monitor
Version: %{__version}
Release: %{__release}%{?dist}

License: GNU AGPLv3
URL: https://github.com/redBorder/rb_monitor
Source0: %{name}-%{version}.tar.gz

BuildRequires: gcc git librd-devel net-snmp-devel json-c-devel librdkafka-devel libmatheval-devel libpcap-devel librb-http0 libcurl-devel >= 7.48.0

Summary: Get data events via SNMP or scripting and send results in json over kafka.
Group:   Development/Libraries/C and C++
Requires: librd0 libmatheval libpcap librdkafka1 net-snmp librb-http0
Requires(pre): shadow-utils

%description
%{summary}

%prep
%setup -qn %{name}-%{version}

%build
git clone --branch v0.9.2 https://github.com/edenhill/librdkafka.git /tmp/librdkafka-v0.9.2
pushd /tmp/librdkafka-v0.9.2
./configure --prefix=/usr --sbindir=/usr/bin --exec-prefix=/usr && make
make install
popd
ldconfig

git clone --branch 1.2.0 https://github.com/redBorder/librb-http.git /tmp/librd-http
pushd /tmp/librd-http
./configure --prefix=/usr --sbindir=/usr/bin --exec-prefix=/usr && make
make install
popd
ldconfig

export PKG_CONFIG_PATH=/usr/lib/pkgconfig
./configure --prefix=/usr --sbindir=/usr/bin --exec-prefix=/usr --enable-rbhttp
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
getent group rb-monitor >/dev/null || groupadd -r rb-monitor
getent passwd rb-monitor >/dev/null || \
    useradd -r -g rb-monitor -d / -s /sbin/nologin \
    -c "User of rb_monitor service" rb-monitor
exit 0

%post
/sbin/ldconfig
systemctl daemon-reload

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


