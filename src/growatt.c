// SPDX-License-Identifier: AGPL-3.0-or-later

#include <assert.h>
#include <libconfig.h>
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

typedef struct __attribute__((aligned(64))) {
  const char *device_or_uri;
  prometheus_config prometheus_config;
  mqtt_config mqtt_config;
} config;

static int usage(char const program[static 1]) {
  fprintf(stderr, "Usage: %s <config_file>\n", program);
  fprintf(stderr, "Example: %s /etc/growatt-exporter.conf\n", program);
  return EXIT_FAILURE;
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

int parse_config(config *config, config_t *parser, char const *filename) {
  if (!config_read_file(parser, filename)) {
    LOG(LOG_ERROR, "%s:%d - %s\n", config_error_file(parser), config_error_line(parser), config_error_text(parser));
    return EXIT_FAILURE;
  }

  if (CONFIG_TRUE != config_lookup_string(parser, "device_or_uri", &config->device_or_uri)) {
    LOG(LOG_ERROR, "No 'device_or_uri' setting in configuration file");
    return EXIT_FAILURE;
  }

  if (CONFIG_TRUE != config_lookup_int(parser, "prometheus.port", &config->prometheus_config.port)) {
    config->prometheus_config.port = 0;
  }

  if (CONFIG_TRUE != config_lookup_int(parser, "mqtt.port", &config->mqtt_config.port)) {
    config->mqtt_config.port = 0;
  }

  config_lookup_string(parser, "mqtt.host", &config->mqtt_config.host);
  config_lookup_string(parser, "mqtt.username", &config->mqtt_config.username);
  config_lookup_string(parser, "mqtt.password", &config->mqtt_config.password);

  return EXIT_SUCCESS;
}

int main(int argc, char *argv[argc + 1]) {
  static_assert(__STDC_VERSION__ >= STDC_VERSION_MIN, "C17+ required");

  // disable log buffering
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  // signal(SIGINT, sig_handler);

  if (argc < 2) {
    return usage(argv[0]);
  }

  config config;
  config_t parser_config;
  config_init(&parser_config);
  if (parse_config(&config, &parser_config, argv[1])) {
    config_destroy(&parser_config);
    return EXIT_FAILURE;
  }

  thrd_t prometheus_thread = 0;
  thrd_t mqtt_thread = 0;
  thrd_t modbus_thread = 0;

  if (config.prometheus_config.port) {
    int status = thrd_create(&prometheus_thread, (thrd_start_t)start_prometheus_thread, &config.prometheus_config);
    if (status != thrd_success) {
      PERROR("thrd_create() failed");
      config_destroy(&parser_config);
      return EXIT_FAILURE;
    }
  }

  if (config.mqtt_config.port) {
    int status = thrd_create(&mqtt_thread, (thrd_start_t)start_mqtt_thread, &config.mqtt_config);
    if (status != thrd_success) {
      PERROR("thrd_create() failed");
      config_destroy(&parser_config);
      return EXIT_FAILURE;
    }
  }

  if (!prometheus_thread && !mqtt_thread) {
    LOG(LOG_ERROR, "You must configure at least Prometheus or MQTT (or both)");
    config_destroy(&parser_config);
    return EXIT_FAILURE;
  }

  int status = thrd_create(&modbus_thread, (thrd_start_t)start_modbus_thread, (void *)config.device_or_uri);
  if (status != thrd_success) {
    PERROR("thrd_create() failed");
    config_destroy(&parser_config);
    return EXIT_FAILURE;
  }

  // FIXME: catch MQTT thread termination somehow
  int value = join_thread(&modbus_thread, "MDBS");

  if (prometheus_thread) {
    value += join_thread(&prometheus_thread, "PRMT");
  }
  if (mqtt_thread) {
    value += join_thread(&mqtt_thread, "MQTT");
  }

  config_destroy(&parser_config);

  LOG(LOG_INFO, "Bye");
  exit(value); // will terminate any remaining threads
}
