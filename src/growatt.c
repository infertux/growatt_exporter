// SPDX-License-Identifier: AGPL-3.0-or-later

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "log.h"
#include "mqtt.h"
#include "prometheus.h"

enum {
  RADIX_DECIMAL = 10,
  STDC_VERSION_MIN = 201710L,
};

static int usage(char const program[static 1]) {
  LOG(LOG_ERROR, "Usage: %s <device_or_uri> --prometheus <http_bind_port>", program);
  LOG(LOG_ERROR, "Example: %s /dev/ttyUSB0 --prometheus 1234", program);
  LOG(LOG_ERROR, "Example: %s 127.0.0.1:1502 --prometheus 1234", program);
  LOG(LOG_ERROR, "");
  LOG(LOG_ERROR, "Usage: %s <device_or_uri> --mqtt <broker_host> <broker_port> <broker_username> <broker_password>",
      program);
  LOG(LOG_ERROR, "Example: %s /dev/ttyUSB0 --mqtt localhost 1883 admin admin", program);
  LOG(LOG_ERROR, "\nBoth prometheus and mqtt can be combined:");
  LOG(LOG_ERROR, "Example: %s /dev/ttyUSB0 --prometheus 1234 --mqtt localhost 1883 admin admin", program);
  return EXIT_FAILURE;
}

static uint16_t parse_port(char const *string) {
  const uint16_t port = strtol(string, NULL, RADIX_DECIMAL);

  if (port < 1 || port > USHRT_MAX) {
    LOG(LOG_ERROR, "Invalid port number: %d", port);
    return 0;
  }

  return port;
}

static int join_thread(thrd_t const *thread, char const label[static 1]) {
  int value = 0;
  int code = thrd_join(*thread, &value);

  if (code != thrd_success || value != EXIT_SUCCESS) {
    LOG(LOG_ERROR, "Thread %s failed with value %d (code = %d)", label, value, code);

    keep_running = 0;

    if (!strcmp(label, "MDBS")) {
      stop_prometheus_thread();
      // kill(SIGTERM, 0);
    }
  } else {
    LOG(LOG_INFO, "Thread %s exited successfully", label);
  }

  return value;
}

/*static void sig_handler(int signal)
{
  LOG(LOG_INFO, "Got signal %d", signal);
  keep_running = 0;
}*/

int main(int argc, char *argv[argc + 1]) {
  static_assert(__STDC_VERSION__ >= STDC_VERSION_MIN, "C17+ required");

  // disable log buffering
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  // signal(SIGINT, sig_handler);

  if (argc < 2) {
    return usage(argv[0]);
  }

  thrd_t prometheus_thread = 0;
  thrd_t mqtt_thread = 0;
  thrd_t modbus_thread = 0;

  char const *device_or_uri = argv[1];

  uint8_t argi = 2;

  if (!strcmp("--prometheus", argv[argi])) {
    const unsigned long port = parse_port(argv[argi + 1]);
    if (!port) {
      return usage(argv[0]);
    }

    argi += 2;

    int status = thrd_create(&prometheus_thread, (thrd_start_t)start_prometheus_thread,
                             (void *)port); // NOLINT(performance-no-int-to-ptr)
    if (status != thrd_success) {
      PERROR("thrd_create() failed");
      return EXIT_FAILURE;
    }
  }

  if (!strcmp("--mqtt", argv[argi])) {
    if (argc < argi + 4) { // TODO: make user/pass optional
      return usage(argv[0]);
    }

    const uint16_t port = parse_port(argv[argi + 2]);
    if (!port) {
      return usage(argv[0]);
    }

    mqtt_config config;
    config.port = port;
    strlcpy(config.host, argv[argi + 1], MQTT_CONFIG_SIZE);
    strlcpy(config.username, argv[argi + 3], MQTT_CONFIG_SIZE);
    strlcpy(config.password, argv[argi + 4], MQTT_CONFIG_SIZE);

    int status = thrd_create(&mqtt_thread, (thrd_start_t)start_mqtt_thread, &config);
    if (status != thrd_success) {
      PERROR("thrd_create() failed");
      return EXIT_FAILURE;
    }
  }

  int status = thrd_create(&modbus_thread, (thrd_start_t)start_modbus_thread, (void *)device_or_uri);
  if (status != thrd_success) {
    PERROR("thrd_create() failed");
    return EXIT_FAILURE;
  }

  int value = join_thread(&modbus_thread, "MDBS");

  if (prometheus_thread) {
    value += join_thread(&prometheus_thread, "PRMT");
  }
  if (mqtt_thread) {
    value += join_thread(&mqtt_thread, "MQTT");
  }

  LOG(LOG_INFO, "Bye");
  exit(value); // will terminate any remaining threads
}
