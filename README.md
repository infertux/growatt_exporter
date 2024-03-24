# growatt_exporter

growatt_exporter is a standalone program written in C that can query Growatt solar inverters (tested on SPF5000ES so far) and output metrics compatible with Prometheus.

This allows to monitor PV production, battery status, etc. on a nice Grafana interface.

## Build

```bash
apt install clang libbsd-dev libconfig-dev libmodbus-dev libmosquitto-dev mosquitto-clients
make
```

## Kudos

The "Growatt OffGrid SPF5000 Modbus RS485 RTU Protocol" PDF document has been a very valuable resource. A copy of it is included in this Git repository. Thank you to the original author for their work.

## Other brands

Would like to monitor Epever/Epsolar Tracer solar charge controllers instead? Here is a sister repository for that: https://github.com/infertux/epever_exporter

## Other approaches

- Blog post: https://www.splitbrain.org/blog/2023-11/03-growatt_and_home_assistant
- Reading Modbus registers via Home Assistant: https://github.com/home-assistant/core/issues/94149
- https://github.com/rspring/Esphome-Growatt

## License

AGPLv3+
