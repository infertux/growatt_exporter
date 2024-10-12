#include <inttypes.h>
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // sleep()

#include "growatt.h"
#include "log.h"
#include "modbus.h"

#define TOPIC_STATE "homeassistant/sensor/growatt/state"

enum {
  MQTT_KEEPALIVE = 60U,
  RESPONSE_SIZE = 8192U,
  PUBLISH_PERIOD = 15U, // seconds
  MQTT_CONFIG_SIZE = 128U,
  MQTT_METRIC_ID_SIZE = 128U,
  MQTT_METRIC_PAYLOAD_SIZE = 2048U,
};

typedef struct __attribute__((aligned(32))) {
  const char *host;
  int port;
  const char *username;
  const char *password;
} mqtt_config;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static struct mosquitto *client = NULL;

static void stop_mqtt_thread(void) {
  mosquitto_disconnect(client);
  mosquitto_destroy(client);

  mosquitto_lib_cleanup();
}

void connection_callback(struct mosquitto *_mosq, void *_obj, int code) { // NOLINT(misc-unused-parameters)
  if (code) {
    LOG(LOG_ERROR, "Cannot connect to broker: %s (%d)\n", mosquitto_connack_string(code), code);
    stop_mqtt_thread();
    thrd_exit(code);
  }
}

int start_mqtt_thread(void *config_ptr) {
  if (atexit(stop_mqtt_thread)) {
    PERROR("Could not register cleanup routine");
    return EXIT_FAILURE;
  }

  mqtt_config *config = (mqtt_config *)config_ptr;

  mosquitto_lib_init();

  client = mosquitto_new("growatt-exporter", true, NULL);
  if (!client) {
    PERROR("Cannot create mosquitto client instance");
    return EXIT_FAILURE;
  }

  mosquitto_connect_callback_set(client, connection_callback);

  assert(strlen(config->username) > 0);
  assert(strlen(config->password) > 0);
  if (mosquitto_username_pw_set(client, config->username, config->password) != MOSQ_ERR_SUCCESS) {
    LOG(LOG_ERROR, "Cannot set username/password");
    return EXIT_FAILURE;
  }

  assert(strlen(config->host) > 0);
  if (mosquitto_connect(client, config->host, config->port, MQTT_KEEPALIVE) != MOSQ_ERR_SUCCESS) {
    PERROR("MQTT client could not connect to %s:%" PRIu16, config->host, config->port);
    return EXIT_FAILURE;
  }

  if (mosquitto_loop_start(client) != MOSQ_ERR_SUCCESS) { // without this statement, the callback is not called upon connection
    PERROR("Unable to start loop");
    return EXIT_FAILURE;
  }

  LOG(LOG_INFO, "Connected to the MQTT broker");

  char payload[MQTT_METRIC_PAYLOAD_SIZE];
  char unique_id[MQTT_METRIC_ID_SIZE];
  char topic[MQTT_METRIC_ID_SIZE + sizeof("homeassistant/sensor/%s/config")];

  for (size_t index = 0; index < COUNT(input_registers); index++) {
    const REGISTER reg = input_registers[index];

    sprintf(unique_id, "growatt_%s", reg.metric_name);

    // don't include empty device_class otherwise https://www.home-assistant.io/integrations/mqtt will throw errors in the logs
    if (strlen(reg.device_class) > 0) {
      sprintf(payload,
              "{\"device_class\":\"%s\",\"state_class\":\"%s\",\"state_topic\":\"%s\",\"unit_of_measurement\":\"%s\","
              "\"value_template\":\"{{value_json.%s}}\",\"name\":\"%s\",\"unique_id\":\"%s\","
              "\"device\":{\"identifiers\":[\"1\"],\"name\":\"Growatt\",\"manufacturer\":\"Growatt\"}}",
              reg.device_class, reg.state_class, TOPIC_STATE, reg.unit, reg.metric_name, reg.human_name, unique_id);
    } else {
      sprintf(payload,
              "{\"state_class\":\"%s\",\"state_topic\":\"%s\",\"unit_of_measurement\":\"%s\","
              "\"value_template\":\"{{value_json.%s}}\",\"name\":\"%s\",\"unique_id\":\"%s\","
              "\"device\":{\"identifiers\":[\"1\"],\"name\":\"Growatt\",\"manufacturer\":\"Growatt\"}}",
              reg.state_class, TOPIC_STATE, reg.unit, reg.metric_name, reg.human_name, unique_id);
    }

    sprintf(topic, "homeassistant/sensor/%s/config", unique_id);
    mosquitto_publish(client, NULL, topic, (int)strlen(payload), payload, 0, true);
  }

  char metrics[RESPONSE_SIZE] = {0};
  char buffer[RESPONSE_SIZE] = {0};

  while (1) {
    strlcpy(metrics, "{", RESPONSE_SIZE);

    mtx_lock(&device_metrics.mutex);
    for (size_t i = 0; i < device_metrics.size; i++) {
      METRIC metric = device_metrics.metrics[i];

      snprintf(buffer, sizeof(buffer), "\"%s\":%lf,", metric.name, metric.value);
      strlcat(metrics, buffer, RESPONSE_SIZE);
    }
    mtx_unlock(&device_metrics.mutex);

    metrics[strlen(metrics) - 1] = '}'; // replace last ','

    if (strlen(metrics) > 1) { // don't publish empty metrics
      LOG(LOG_INFO, "Publishing status (%zu bytes) to broker...", strlen(metrics));
      mosquitto_publish(client, NULL, TOPIC_STATE, (int)strlen(metrics), metrics, 0 /* QoS */, false /* retain */);
    }

    LOG(LOG_DEBUG, "Waiting %u seconds...", PUBLISH_PERIOD);
    for (size_t i = 0; i < PUBLISH_PERIOD; i++) {
      sleep(1); // NOLINT(concurrency-mt-unsafe)
      if (!keep_running) {
        return EXIT_SUCCESS;
      }
    }
  }

  return EXIT_SUCCESS;
}
