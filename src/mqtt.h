#include <inttypes.h>
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // sleep()

#include "log.h"
#include "modbus.h"

#define TOPIC_STATE "homeassistant/sensor/growatt/state"

enum {
  MQTT_KEEPALIVE = 60U,
  RESPONSE_SIZE = 8192U,
  PUBLISH_PERIOD = 15U, // seconds
  MQTT_CONFIG_SIZE = 128U,
};

typedef struct __attribute__((aligned(MQTT_CONFIG_SIZE * 4))) {
  char _padding[4]; // XXX: prevents the 'host' member from getting corrupted sometimes?!
  char host[MQTT_CONFIG_SIZE];
  uint16_t port;
  char username[MQTT_CONFIG_SIZE];
  char password[MQTT_CONFIG_SIZE];
} mqtt_config;

static struct mosquitto *mosq = NULL;

static void stop_mqtt_thread(void) {
  LOG(LOG_DEBUG, "Terminating MQTT thread");

  mosquitto_disconnect(mosq);
  mosquitto_destroy(mosq);

  mosquitto_lib_cleanup();

  LOG(LOG_TRACE, "Terminated MQTT thread");
}

void connection_callback(struct mosquitto *mosq, void *obj, int rc) {
  if (rc) {
    LOG(LOG_ERROR, "Cannot connect to broker: %s (%d)\n", mosquitto_connack_string(rc), rc);
    stop_mqtt_thread();
    exit(rc);
  }
}

int start_mqtt_thread(void *config_ptr) {
  if (atexit(stop_mqtt_thread)) {
    PERROR("Could not register cleanup routine");
    return EXIT_FAILURE;
  }

  mqtt_config *config = (mqtt_config *)config_ptr;

  mosquitto_lib_init();

  mosq = mosquitto_new("growatt-exporter", true, NULL);
  if (!mosq) {
    PERROR("Cannot create mosquitto socket");
    return EXIT_FAILURE;
  }

  mosquitto_connect_callback_set(mosq, connection_callback);

  assert(strlen(config->username) > 0);
  assert(strlen(config->password) > 0);
  if (mosquitto_username_pw_set(mosq, config->username, config->password) != MOSQ_ERR_SUCCESS) {
    LOG(LOG_ERROR, "Cannot set username/password");
    stop_mqtt_thread();
    return EXIT_FAILURE;
  }

  assert(strlen(config->host) > 0);
  if (mosquitto_connect(mosq, config->host, config->port, MQTT_KEEPALIVE) != MOSQ_ERR_SUCCESS) {
    PERROR("MQTT client could not connect to %s:%" PRIu16, config->host, config->port);
    stop_mqtt_thread();
    return EXIT_FAILURE;
  }

  if (mosquitto_loop_start(mosq) !=
      MOSQ_ERR_SUCCESS) { // without this statement, the callback is not called upon connection
    PERROR("Unable to start loop");
    stop_mqtt_thread();
    return EXIT_FAILURE;
  }

  LOG(LOG_INFO, "Connected to the MQTT broker");

  // TODO:
  char payload[2048];
  char metric_id[32];
  char unique_id[64];
  char topic[128];

  sprintf(metric_id, "battery_volts");
  sprintf(unique_id, "growatt_%s", metric_id);
  sprintf(payload,
          "{\"device_class\":\"voltage\",\"state_topic\":\"%s\",\"unit_of_measurement\":\"V\",\"value_template\":\"{{"
          "value_json.%s}}\",\"name\":\"Battery "
          "voltage\",\"unique_id\":\"%s\",\"device\":{\"identifiers\":[\"1\"],\"name\":\"Growatt\",\"manufacturer\":"
          "\"Growatt\"}}",
          TOPIC_STATE, metric_id, unique_id);

  sprintf(topic, "homeassistant/sensor/%s/config", unique_id);
  mosquitto_publish(mosq, NULL, topic, (int)strlen(payload), payload, 0, true);

  sprintf(metric_id, "pv1_watts");
  sprintf(unique_id, "growatt_%s", metric_id);
  sprintf(payload,
          "{\"device_class\":\"power\",\"state_topic\":\"%s\",\"unit_of_measurement\":\"W\",\"value_template\":\"{{"
          "value_json.%s}}\",\"name\":\"PV1 "
          "power\",\"unique_id\":\"%s\",\"device\":{\"identifiers\":[\"1\"],\"name\":\"Growatt\",\"manufacturer\":"
          "\"Growatt\"}}",
          TOPIC_STATE, metric_id, unique_id);

  sprintf(topic, "homeassistant/sensor/%s/config", unique_id);
  mosquitto_publish(mosq, NULL, topic, (int)strlen(payload), payload, 0, true);

  sprintf(metric_id, "pv1_volts");
  sprintf(unique_id, "growatt_%s", metric_id);
  sprintf(payload,
          "{\"device_class\":\"voltage\",\"state_topic\":\"%s\",\"unit_of_measurement\":\"V\",\"value_template\":\"{{"
          "value_json.%s}}\",\"name\":\"PV1 "
          "voltage\",\"unique_id\":\"%s\",\"device\":{\"identifiers\":[\"1\"],\"name\":\"Growatt\",\"manufacturer\":"
          "\"Growatt\"}}",
          TOPIC_STATE, metric_id, unique_id);

  sprintf(topic, "homeassistant/sensor/%s/config", unique_id);
  mosquitto_publish(mosq, NULL, topic, (int)strlen(payload), payload, 0, false);

  sprintf(metric_id, "energy_pv_today_kwh");
  sprintf(unique_id, "growatt_%s", metric_id); // should be on Energy Dashboard because of "device_class: energy"
  sprintf(payload,
          "{\"device_class\":\"energy\",\"state_topic\":\"%s\",\"unit_of_measurement\":\"kWh\",\"value_template\":\"{{"
          "value_json.%s}}\",\"name\":\"PV production "
          "today\",\"unique_id\":\"%s\",\"device\":{\"identifiers\":[\"1\"],\"name\":\"Growatt\",\"manufacturer\":"
          "\"Growatt\"}}",
          TOPIC_STATE, metric_id, unique_id);

  sprintf(metric_id, "energy_pv_total_kwh");
  sprintf(unique_id, "growatt_%s",
          metric_id); // should be on Energy Dashboard because of "device_class: energy" and state_class
  sprintf(payload,
          "{\"device_class\":\"energy\",\"state_class\":\"total_increasing\",\"state_topic\":\"%s\",\"unit_of_"
          "measurement\":\"kWh\",\"value_template\":\"{{"
          "value_json.%s}}\",\"name\":\"PV production "
          "total\",\"unique_id\":\"%s\",\"device\":{\"identifiers\":[\"1\"],\"name\":\"Growatt\",\"manufacturer\":"
          "\"Growatt\"}}",
          TOPIC_STATE, metric_id, unique_id);

  sprintf(topic, "homeassistant/sensor/%s/config", unique_id);
  mosquitto_publish(mosq, NULL, topic, (int)strlen(payload), payload, 0, false);

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
      mosquitto_publish(mosq, NULL, TOPIC_STATE, (int)strlen(metrics), metrics, 0 /* QoS */, false /* retain */);
    }

    LOG(LOG_DEBUG, "Waiting %u seconds...", PUBLISH_PERIOD);
    for (size_t i = 0; i < PUBLISH_PERIOD; i++) {
      sleep(1);
      if (!keep_running)
        thrd_exit(EXIT_SUCCESS);
    }
  }

  return EXIT_SUCCESS;
}
