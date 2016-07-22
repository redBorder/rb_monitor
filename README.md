[![Build Status](https://travis-ci.org/redBorder/rb_monitor.svg?branch=redborder)](https://travis-ci.org/redBorder/rb_monitor)
[![Coverage Status](https://coveralls.io/repos/github/redBorder/rb_monitor/badge.svg?branch=redborder)](https://coveralls.io/github/redBorder/rb_monitor?branch=redborder)

# rb_monitor

## Introduction

Monitor agent, that sends periodically any kind of stat via kafka or http.

You can monitor your system using periodical SNMP (or pure raw system commands) and send them via kafka (or HTTP POST) to a central event ingestion system.

## Code Samples

### Usage of rb_monitor
To run rb_monitor, you have to prepare the config file to monitor the parameters you want, and run it using `rb_monitor -c <command-file>`

Config file are divided in two sections:

1. `conf` one is more generic, and allow to tune different parameters in kafka or the own rb_monitor.
1. `sensors` section, that define an array of sensors you want monitor
  1. Sensor properties, like IP, SNMP community, timeouts, etc
  1. Sensor monitors, actual stuff that we want to monitor and send

### Simple SNMP monitoring
The easiest way to start with rb_monitor is to monitor simple SNMP parameters, like load average, CPU and memory, and send them via kafka. This example configuration file does so:
```json
{
  "conf": {
    "debug": 2, /* See syslog error levels */
    "stdout": 1,
    "timeout": 1,
    "sleep_main_thread": 10,
    "sleep_worker_thread": 10,
    "kafka_broker": "192.168.101.201", /* Or your own kafka broker */
    "kafka_topic": "rb_monitor", /* Or the topic you desire */
  },
  "sensors": [
    {
      "sensor_id":1,
      "timeout":2000, /* Time this sensor has to answer */
      "sensor_name": "my-sensor", /* Name of the sensor you are monitoring*/
      "sensor_ip": "192.168.101.201", /* Sensor IP to send SNMP requests */
      "snmp_version": "2c",
      "community" : "redBorder", /* SNMP community */
      "monitors": [
        /* OID extracted from http://www.debianadmin.com/linux-snmp-oids-for-cpumemory-and-disk-statistics.html */

        {"name": "load_5", "oid": "UCD-SNMP-MIB::laLoad.2", "unit": "%"},
        {"name": "load_15", "oid": "UCD-SNMP-MIB::laLoad.3", "unit": "%"},
        {"name": "cpu_idle", "oid":"UCD-SNMP-MIB::ssCpuIdle.0", "unit":"%"},

        {"name": "memory_total", "nonzero":1, "oid": "UCD-SNMP-MIB::memTotalReal.0"},
        {"name": "memory_free",  "nonzero":1, "oid": "UCD-SNMP-MIB::memAvailReal.0"},

        {"name": "swap_total", "oid": "UCD-SNMP-MIB::memTotalSwap.0", "send":0},
        {"name": "swap_free",  "oid": "UCD-SNMP-MIB::memAvailSwap.0", "send":0 }
      ]
    }
  ]
}
```

This way, rb_monitor will send SNMP requests to obtain this information. If you read the kafka topic, you will see:
```json
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"load_5", "value":"0.100000", "type":"snmp", "unit":"%"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"load_15", "value":"0.100050", "type":"snmp", "unit":"%"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"cpu_idle", "value":"0.100000", "type":"snmp", "unit":"%"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"memory_total", "value":"256.000000", "type":"snmp"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"memory_free", "value":"120.000000", "type":"snmp"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"swap_total", "value":"0.000000", "type":"snmp"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"swap_free", "value":"0.000000", "type":"snmp"}
```

### Operation on monitors
The previous example is OK, but we can do better: What if I want the used CPU, or to know fast the % of the memory I have occupied? We can do operations on monitors (note: from now on, I will only put the monitors array, since the conf section is irrelevant):

```json
"monitors": [
  {"name": "load_5", "oid": "UCD-SNMP-MIB::laLoad.2", "unit": "%"},
  {"name": "load_15", "oid": "UCD-SNMP-MIB::laLoad.3", "unit": "%"},
  {"name": "cpu_idle", "oid":"UCD-SNMP-MIB::ssCpuIdle.0", "unit":"%", "send":0},
  {"name": "cpu", "op": "100-cpu_idle", "unit": "%"},


  {"name": "memory_total", "nonzero":1, "oid": "UCD-SNMP-MIB::memTotalReal.0", "send":0},
  {"name": "memory_free",  "nonzero":1, "oid": "UCD-SNMP-MIB::memAvailReal.0", "send":0},
  {"name": "memory", "op": "100*(memory_total-memory_free)/memory_total", "unit": "%", "kafka": 1 },
]
```

This way, rb_monitor will send SNMP requests to obtain this information. If you read the kafka topic, you will see:
```json
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"load_5", "value":"0.100000", "type":"snmp", "unit":"%"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"load_15", "value":"0.100050", "type":"snmp", "unit":"%"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"cpu", "value":"5.100000", "type":"op", "unit":"%"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"memory", "value":"20.000000", "type":"snmp"}
```

Here we got, we can do operations over previous monitor values, so we can get complex result from simpler values.

### System requests
You can't monitor everything using SNMP. We could add here telnet, HTTP REST interfaces, and a lot of complex stuffs. But, for now, we have the possibility of run a console command from rb_monitor, and to get result. For example, if you want to get the latency to reach some destination, you can add this monitor:
```json
"monitors"[
  {"name": "latency"  , "system": "nice -n 19 fping -q -s managerPro2 2>&1| grep 'avg round trip time'|awk '{print $1}'", "unit": "ms"}
]
```
And it will send to kafka this message:
```json
{"timestamp":1469183485,"sensor_name":"my-sensor","monitor":"latency","value":"0.390000","type":"system","unit":"ms"}
```
Notes:

1. Command are executed in the host running rb_monitor, so you can't execute remote commands this way. However, you can use ssh or telnet inside the system parameter
1. The shell used to run the command is the user's one, so take care if you use bash commands in dash shell, and stuffs like that.

### Vectors monitors
If you need to monitor same property on many instances (for example, received bytes of an interface), you can use vectors. You can return many values using a split token and then mix all them. For example, using `echo` instead of a proper program:

```json
"monitors"[
  {"name": "packets_received"  , "system": "echo '1;2;3'", "unit": "pkts", "split":";","instance_prefix": "interface-", "name_split_suffix":"_per_interface"}
]
```
And it will send to kafka this messages:
```json
{"timestamp":1469184314,"sensor_name":"my-sensor","monitor":"packets_received_per_interface","instance":"interface-0","value":1,"type":"system","unit":"pkts"}
{"timestamp":1469184314,"sensor_name":"my-sensor","monitor":"packets_received_per_interface","instance":"interface-1","value":2,"type":"system","unit":"pkts"}
{"timestamp":1469184314,"sensor_name":"my-sensor","monitor":"packets_received_per_interface","instance":"interface-2","value":3,"type":"system","unit":"pkts"}
```

If you want to calculate the sum off all packets (or the mean of another monitor), you can add `"split_op":"sum"` (or `"split_op":"mean"`) to the monitor and it will also send this last message:
```json
{"timestamp":1469184314,"sensor_name":"my-sensor","monitor":"packets_received","value":6,"type":"system","unit":"pkts"}
```

### Operations of vectors
If you have two vector monitors, you can operate on them as same as you do with scalar monitors.

Please note that If you do this kind of operation, it will apply for each vector element, but not to split operation result. But you can still do an split operation over the result (`sum` or `mean`) if you need that.

Blanks are handled this way: If one of the vector has a blank element, it is assumed as 0, for operation result and for split operation result.

### Timestamp provided on vectors
Sometimes you don't want to send the same value twice if it is a cached value. If you can get the timestamp the system obtained the value, and you can send it in executed command (or SNMP answer), `rb_monitor` can detect it.

You can specify that you want to provide timestamp with `"timestamp_given":1` in the monitor. For example, if we set this monitors:
```json
"monitors":[
  {"name": "packets_received", "system": "get_pkts.sh", "name_split_suffix":"_per_interface", "split":";","split_op":"sum", "instance_prefix":"instance-", "timestamp_given":1},
]
```

If `get_pkts.sh` returns `10:20;30:40` the first time we call it (i.e., in timestamp `10` the first interface had `20` packets, and in timestamp `30` the first interface had `40` packets. So `rb_monitor` will send to kafka:

```json
{"timestamp":10, "sensor_name":"my-sensor","monitor":"packets_received_per_interface","instance":"interface-0","value":20,"type":"system","unit":"pkts"}
{"timestamp":30, "sensor_name":"my-sensor","monitor":"packets_received_per_interface","instance":"interface-1","value":40,"type":"system","unit":"pkts"}
{"timestamp":50, "sensor_name":"my-sensor","monitor":"packets_received","value":60,"type":"system","unit":"pkts"}
```

Note that split op timestamp is the one in that split op is done.

After it, let's suppose that `get_pkts.sh` return `10:20;60:100`. `rb_monitor` wil notice that interface 1 has not changed, so it will only send the second one, and the split op result:

```json
{"timestamp":60, "sensor_name":"my-sensor","monitor":"packets_received_per_interface","instance":"interface-1","value":100,"type":"system","unit":"pkts"}
{"timestamp":70, "sensor_name":"my-sensor","monitor":"packets_received","value":60,"type":"system","unit":"pkts"}
```

### Monitors groups
If you need to separate monitors of the same sensor in different groups, you can use `group_id` monitor parameter. This way, you can use the same monitors names and do operations between them without mix variables.

For example, you can monitor the received bytes of different interfaces (rb_monitor `instances`) on different vlans (rb_monitor `groups`, assuming that all interfaces are untagged) this way:

```json
"monitors"[
  {"name": "packets_received"  , "system": "get_rcv_pkts.sh 1'", "unit": "pkts", "split":";", "group_id":1,"group_name":"VLAN-1", "send":0},
  {"name": "packets_drop"  , "system": "get_drop_pkts.sh 1", "unit": "pkts", "split":",", "group_id":1,"group_name":"VLAN-1", "send":0},
  {"name": "packets_drop_%"  , "op": "100*(packets_drop)/(packets_received+packets_drop)", "unit": "%", "split":";","split-op":"mean","instance_prefix": "interface-", "name_split_suffix":"_per_interface", "group_id":1,"group_name":"VLAN-1"},

  {"name": "packets_received"  , "system": "get_rcv_pkts.sh 2", "unit": "pkts", "split":";", "group_id":2,"group_name":"VLAN-2", "send":0},
  {"name": "packets_drop"  , "system": "get_drop_pkts.sh 2", "unit": "pkts", "split":",", "group_id":2,"group_name":"VLAN-2", "send":0},
  {"name": "packets_drop_%"  , "op": "100*(packets_drop)/(packets_received+packets_drop)", "unit": "%", "split":";","instance_prefix": "interface-", "name_split_suffix":"_per_interface", "group_id":2,"group_name":"VLAN-2"},]
```

Assuming `get_rcv_pkts.sh` and `get_drop_pkts.sh` gets all vlan `$1` interface packets joined by `;`, and that they returns in execution this values:

| Script | Vlan  | Return |
| --- | :---: | --- |
| get_rcv_pkts.sh | 1 | 10;20;30 |
| get_rcv_pkts.sh | 1 | 1;1;1 |
| get_rcv_pkts.sh | 2 | 40;50;60 |
| get_rcv_pkts.sh | 2 | 2;2;2 |

You will get the next output:

```json
{"timestamp":1469188000,"sensor_name":"my-sensor","monitor":"packets_drop_%_per_instance","instance":"interface-0","value":"10.000000","type":"system","unit":"%","group_name":"VLAN-1","group_id":1}
{"timestamp":1469188000,"sensor_name":"my-sensor","monitor":"packets_drop_%_per_instance","instance":"interface-1","value":"5.000000","type":"system","unit":"%","group_name":"VLAN-1","group_id":1}
{"timestamp":1469188000,"sensor_name":"my-sensor","monitor":"packets_drop_%_per_instance","instance":"interface-2","value":"2.500000","type":"system","unit":"%","group_name":"VLAN-1","group_id":1}
{"timestamp":1469188000,"sensor_name":"my-sensor","monitor":"packets_drop_%","value":"5.833333","type":"system","unit":"%","group_name":"VLAN-1","group_id":1}

{"timestamp":1469188000,"sensor_name":"my-sensor","monitor":"packets_drop_%_per_instance","instance":"interface-0","value":"5.000000","type":"system","unit":"%","group_name":"VLAN-1","group_id":1}
{"timestamp":1469188000,"sensor_name":"my-sensor","monitor":"packets_drop_%_per_instance","instance":"interface-1","value":"4.000000","type":"system","unit":"%","group_name":"VLAN-1","group_id":1}
{"timestamp":1469188000,"sensor_name":"my-sensor","monitor":"packets_drop_%_per_instance","instance":"interface-2","value":"3.33333","type":"system","unit":"%","group_name":"VLAN-1","group_id":1}
{"timestamp":1469188000,"sensor_name":"my-sensor","monitor":"packets_drop_%","value":"4.111111","type":"system","unit":"%","group_name":"VLAN-1","group_id":1}
```

### Sending custom data in messages
You can send attach any information you want in sent monitors if you use `enrichment` keyword, and adding an object. If you add it to a sensor, all monitors will be enrichment with that information; if you add it to a monitor, only that monitor will be enriched with the new JSON object.

For example, you can do:

```json
{
  "conf": {
    "debug": 2, /* See syslog error levels */
    "stdout": 1,
    "timeout": 1,
    "sleep_main_thread": 10,
    "sleep_worker_thread": 10,
    "kafka_broker": "192.168.101.201", /* Or your own kafka broker */
    "kafka_topic": "rb_monitor", /* Or the topic you desire */
    "enrichment":{ "my custom key":"my custom value" }
  },
  "sensors": [
    {
      "sensor_id":1,
      "timeout":2000, /* Time this sensor has to answer */
      "sensor_name": "my-sensor", /* Name of the sensor you are monitoring*/
      "sensor_ip": "192.168.101.201", /* Sensor IP to send SNMP requests */
      "snmp_version": "2c",
      "community" : "redBorder", /* SNMP community */
      "monitors": [
        /* OID extracted from http://www.debianadmin.com/linux-snmp-oids-for-cpumemory-and-disk-statistics.html */

        {"name": "load_5", "oid": "UCD-SNMP-MIB::laLoad.2", "unit": "%"},
        {"name": "load_15", "oid": "UCD-SNMP-MIB::laLoad.3", "unit": "%"},
        {"name": "cpu_idle", "oid":"UCD-SNMP-MIB::ssCpuIdle.0", "unit":"%", "enrichment": {"my-favourite-monitor":true}}
      ]
    }
  ]
}
```

And the kafka output will be:
```json
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"load_5", "value":"0.100000", "type":"snmp", "unit":"%","my custom key":"my custom value"  }
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"load_15", "value":"0.100050", "type":"snmp", "unit":"%","my custom key":"my custom value" }
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"cpu_idle", "value":"0.100000", "type":"snmp", "unit":"%","my custom key":"my custom value", "my-favourite-monitor":true}
```

### HTTP output
If you want to send the JSON directly via HTP POST, you can use this sensor properties:
```json
"sensors":[
  {
  ...
    "http_endpoint": "http://localhost:8080/monitor",
  ...
  }
]
```

Note that you need to configure with `--enable-http`

## Installation

Just use the well known `./configure && make && make install`. You can see
configure options with `configure --help`. The most important are:

* `--enable-zookeeper`, that allows to get monitors requests using zookeeper
* `--enable-rbhttp`, to send monitors via HTTP POST instead of kafka.

## TODO
- [ ] Vector <op> scalar operation (see #14 )
- [ ] SNMP tables / array (see #15 )
