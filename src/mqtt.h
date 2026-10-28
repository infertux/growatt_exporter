#include <inttypes.h>
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // sleep()

#include "growatt.h"
#include "log.h"
#include "modbus.h"

#define TOPIC_PREFIX "homeassistant/sensor/growatt"

enum {
  MQTT_KEEPALIVE = 60U,
  MQTT_RECONNECT_DELAY = 5U, // seconds
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
  int id;
} mqtt_config;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static struct mosquitto *client = NULL;

static void stop_mqtt_thread(void) {
  if (!client) {
    return;
  }

  mosquitto_disconnect(client);
  mosquitto_destroy(client);
  client = NULL;

  mosquitto_lib_cleanup();
}

static void publish_discovery_configs(struct mosquitto *mosq, mqtt_config const *config) {
  char payload[MQTT_METRIC_PAYLOAD_SIZE];
  char unique_id[MQTT_METRIC_ID_SIZE];
  char topic[MQTT_METRIC_ID_SIZE + sizeof("homeassistant/sensor/%s/config")];

  for (size_t index = 0; index < COUNT(input_registers); index++) {
    const REGISTER reg = input_registers[index];

    sprintf(unique_id, "growatt_%" PRIu8 "_%s", config->id, reg.metric_name);

    // don't include empty device_class otherwise https://www.home-assistant.io/integrations/mqtt will throw errors in the logs
    if (strlen(reg.device_class) > 0) {
      sprintf(payload,
              "{\"device_class\":\"%s\",\"state_class\":\"%s\",\"state_topic\":\"%s_%" PRIu8 "/state\",\"unit_of_measurement\":\"%s\","
              "\"value_template\":\"{{value_json.%s}}\",\"name\":\"%s\",\"unique_id\":\"%s\","
              "\"device\":{\"identifiers\":[\"%" PRIu8 "\"],\"name\":\"Growatt %" PRIu8 "\",\"manufacturer\":\"Growatt\"}}",
              reg.device_class, reg.state_class, TOPIC_PREFIX, config->id, reg.unit, reg.metric_name, reg.human_name, unique_id, config->id,
              config->id);
    } else {
      sprintf(payload,
              "{\"state_class\":\"%s\",\"state_topic\":\"%s_%" PRIu8 "/state\",\"unit_of_measurement\":\"%s\","
              "\"value_template\":\"{{value_json.%s}}\",\"name\":\"%s\",\"unique_id\":\"%s\","
              "\"device\":{\"identifiers\":[\"%" PRIu8 "\"],\"name\":\"Growatt %" PRIu8 "\",\"manufacturer\":\"Growatt\"}}",
              reg.state_class, TOPIC_PREFIX, config->id, reg.unit, reg.metric_name, reg.human_name, unique_id, config->id, config->id);
    }

    sprintf(topic, "homeassistant/sensor/%s/config", unique_id);
    mosquitto_publish(mosq, NULL, topic, (int)strlen(payload), payload, 0, true);
  }
}

// on failure, the broker closes the connection and network_loop() will retry, so we must neither exit nor destroy the client here
void connect_callback(struct mosquitto *mosq, void *obj, int code) {
  if (code) {
    LOG(LOG_ERROR, "Cannot connect to broker: %s (%d)", mosquitto_connack_string(code), code);
    return;
  }

  LOG(LOG_INFO, "Connected to the MQTT broker");

  // (re)publish discovery configs in case the broker lost its retained messages
  publish_discovery_configs(mosq, (mqtt_config const *)obj);
}

void disconnect_callback(struct mosquitto *_mosq, void *_obj, int code) { // NOLINT(misc-unused-parameters)
  LOG(LOG_ERROR, "Disconnected from broker: %s (%d)", mosquitto_strerror(code), code);
}

// We drive the network loop ourselves rather than using mosquitto_loop_start() because the latter never retries if the initial connection
// fails and gives up for good on some errors (e.g. DNS resolution failure), leaving us permanently disconnected.
static void network_loop(void) {
  static unsigned int retry_in = MQTT_RECONNECT_DELAY;

  if (mosquitto_loop(client, 1000 /* ms */, 1) == MOSQ_ERR_SUCCESS) {
    return;
  }

  sleep(1); // NOLINT(concurrency-mt-unsafe) mosquitto_loop() returns immediately when disconnected

  if (--retry_in > 0) {
    return;
  }
  retry_in = MQTT_RECONNECT_DELAY;

  LOG(LOG_INFO, "Reconnecting to the MQTT broker...");
  int rc = mosquitto_reconnect_async(client);
  if (rc != MOSQ_ERR_SUCCESS) {
    LOG(LOG_ERROR, "Cannot reconnect to broker: %s (%d)", mosquitto_strerror(rc), rc);
  }
}

int start_mqtt_thread(void *config_ptr) {
  if (atexit(stop_mqtt_thread)) {
    PERROR("Could not register cleanup routine");
    return EXIT_FAILURE;
  }

  mqtt_config *config = (mqtt_config *)config_ptr;

  mosquitto_lib_init();

  client = mosquitto_new(NULL, true, config);
  if (!client) {
    PERROR("Cannot create mosquitto client instance");
    return EXIT_FAILURE;
  }

  mosquitto_connect_callback_set(client, connect_callback);
  mosquitto_disconnect_callback_set(client, disconnect_callback);

  assert(strlen(config->username) > 0);
  assert(strlen(config->password) > 0);
  if (mosquitto_username_pw_set(client, config->username, config->password) != MOSQ_ERR_SUCCESS) {
    LOG(LOG_ERROR, "Cannot set username/password");
    return EXIT_FAILURE;
  }

  assert(strlen(config->host) > 0);
  int rc = mosquitto_connect_async(client, config->host, config->port, MQTT_KEEPALIVE);
  if (rc == MOSQ_ERR_INVAL) {
    LOG(LOG_ERROR, "Invalid MQTT broker settings %s:%d", config->host, config->port);
    return EXIT_FAILURE;
  }
  if (rc != MOSQ_ERR_SUCCESS) {
    // not fatal: the broker may simply not be up yet, network_loop() will keep retrying
    LOG(LOG_ERROR, "Cannot connect to %s:%d: %s (%d)", config->host, config->port, mosquitto_strerror(rc), rc);
  }

  char topic[MQTT_METRIC_ID_SIZE + sizeof("homeassistant/sensor/%s/config")];
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
      sprintf(topic, "%s_%" PRIu8 "/state", TOPIC_PREFIX, config->id);

      rc = mosquitto_publish(client, NULL, topic, (int)strlen(metrics), metrics, 0 /* QoS */, false /* retain */);
      if (rc != MOSQ_ERR_SUCCESS) {
        LOG(LOG_ERROR, "Publish failed: %s (%d)", mosquitto_strerror(rc), rc);
      } else {
        LOG(LOG_INFO, "Published status (%zu bytes) to %s", strlen(metrics), topic);
      }
    }

    LOG(LOG_DEBUG, "Waiting %u seconds...", PUBLISH_PERIOD);
    for (size_t i = 0; i < PUBLISH_PERIOD; i++) {
      network_loop(); // takes about one second
      if (!keep_running) {
        stop_mqtt_thread();
        return EXIT_SUCCESS;
      }
    }
  }

  return EXIT_SUCCESS;
}
