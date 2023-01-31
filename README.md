# growatt_exporter

growatt_exporter is a standalone program written in C that can query Growatt solar inverters (tested on SPF5000ES so far) and output metrics compatible with Prometheus.

This allows to monitor PV production, battery status, etc. on a nice Grafana interface.

## Build

```bash
apt install clang libbsd-dev libmodbus-dev
make growatt
```

## Kudos

The "Growatt OffGrid SPF5000 Modbus RS485 RTU Protocol" PDF document has been a very valuable resource. A copy of it is included in this Git repository. Thank you to the original author for their work.

## License

AGPLv3+
