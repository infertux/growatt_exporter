#include <arpa/inet.h> // HTTP stuff
#include <bsd/string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h> // close()

#include "log.h"
#include "modbus.h"

enum {
  BACKLOG = 10,              // passed to listen()
  MINIMUM_REQUEST_SIZE = 16, // bytes
  REQUEST_BUFFER_SIZE = 1024,
};

#define PROMETHEUS_CONTENT_TYPE "text/plain; version=0.0.4; charset=utf-8"
#define REQUEST_PROMETHEUS "GET /metrics"

int set_response(char *response) {
  char metrics[RESPONSE_BUFFER_SIZE] = {0};
  char buffer[RESPONSE_BUFFER_SIZE] = {0};
  int code = EXIT_SUCCESS;

  mtx_lock(&device_metrics.mutex);
  for (size_t i = 0; i < device_metrics.size; i++) {
    METRIC metric = device_metrics.metrics[i];
    // LOG(LOG_DEBUG, "%s = %lf\n", metric.name, metric.value);

    snprintf(buffer, sizeof(buffer), "# TYPE growatt_%s gauge\ngrowatt_%s %lf\n", metric.name, metric.name,
             metric.value);
    strlcat(metrics, buffer, RESPONSE_BUFFER_SIZE);
  }
  mtx_unlock(&device_metrics.mutex);

  if (device_metrics.read_metric_succeeded_total == 0) {
    code = EXIT_FAILURE;
    LOG(LOG_ERROR, "No metrics");
    strlcpy(metrics, "503 Service Temporarily Unavailable\n", RESPONSE_BUFFER_SIZE);
    strlcpy(response, "HTTP/1.1 503 Service Unavailable\r\n", RESPONSE_BUFFER_SIZE);
  } else {
    strlcpy(response, "HTTP/1.1 200 OK\r\n", RESPONSE_BUFFER_SIZE);
  }

  strlcat(response, "Server: growatt-exporter\r\n", RESPONSE_BUFFER_SIZE);
  char content_length[RESPONSE_BUFFER_SIZE];
  sprintf(content_length, "Content-Length: %zu\r\n", strlen(metrics));
  strlcat(response, content_length, RESPONSE_BUFFER_SIZE);
  strlcat(response, "Content-Type: ", RESPONSE_BUFFER_SIZE);
  strlcat(response, PROMETHEUS_CONTENT_TYPE, RESPONSE_BUFFER_SIZE);
  strlcat(response, "\r\n\r\n", RESPONSE_BUFFER_SIZE);
  strlcat(response, metrics, RESPONSE_BUFFER_SIZE);

  return code;
}

int handle_client(const int client_fd) {
  struct timespec before, after; // NOLINT(readability-isolate-declaration)
  clock_gettime(CLOCK_REALTIME, &before);
  LOG(LOG_DEBUG, "HTTP server received request...");

  int code = EXIT_SUCCESS;
  char request[REQUEST_BUFFER_SIZE * sizeof(char)];
  char response[RESPONSE_BUFFER_SIZE] = {'\0'};

  ssize_t const bytes_received = recv(client_fd, request, REQUEST_BUFFER_SIZE, 0);
  if (bytes_received < MINIMUM_REQUEST_SIZE) {
    PERROR("Request too short (only %zu bytes)\n", bytes_received);
    strlcpy(response, "HTTP/1.1 400 Bad Request\r\n", RESPONSE_BUFFER_SIZE);
    return EXIT_FAILURE;
  }

  if (!strncmp(request, REQUEST_PROMETHEUS, strlen(REQUEST_PROMETHEUS))) {
    code = set_response(response);
  } else {
    strlcpy(response, "HTTP/1.1 400 Bad Request\r\n", RESPONSE_BUFFER_SIZE);
  }

  size_t const expected_size = strlen(response);
  size_t const actual_size = write(client_fd, response, expected_size);
  if (actual_size != expected_size) {
    PERROR("Wrote %zu bytes instead of %zu", actual_size, expected_size);
    return EXIT_FAILURE;
  }

  if (close(client_fd)) {
    PERROR("Error %d closing socket", errno);
    return EXIT_FAILURE;
  }

  clock_gettime(CLOCK_REALTIME, &after);
  double const elapsed = after.tv_sec - before.tv_sec + (double)(after.tv_nsec - before.tv_nsec) / 1e9; // NOLINT
  LOG(LOG_INFO, "HTTP server sent response (%zu bytes) in %.0fms", strlen(response), elapsed * 1e3);

  return code;
}

static int server_socket;

static void stop_prometheus_thread(void) {
  LOG(LOG_DEBUG, "Terminating Prometheus thread");

  if (shutdown(server_socket, SHUT_RD)) {
    PERROR("shutdown failed");
  }
  if (close(server_socket)) {
    PERROR("close failed");
  }

  LOG(LOG_TRACE, "Terminated Prometheus thread");
  // thrd_exit(EXIT_SUCCESS);
}

static void sig_handler(int signal) {
  LOG(LOG_INFO, "Got signal %d, exiting...", signal);
  keep_running = 0;
  stop_prometheus_thread();
}

int start_prometheus_thread(void *port_value) {
  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  uint16_t const port = (unsigned long)port_value;
  server_socket = socket(AF_INET6,    // IPv6
                         SOCK_STREAM, // TCP
                         0            // protocol 0
  );

  const struct sockaddr_in6 address = {.sin6_family = AF_INET6, .sin6_port = htons(port), .sin6_addr = in6addr_any};

  // prevent "bind failed: Address already in use" when restarting the program too quickly
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int))) {
    PERROR("setsockopt(SO_REUSEADDR) failed");
  }

  if (bind(server_socket, (struct sockaddr *)&address, sizeof(address))) {
    PERROR("bind failed");
    return EXIT_FAILURE;
  }

  if (listen(server_socket, BACKLOG) < 0) {
    PERROR("The server is not listening");
    return EXIT_FAILURE;
  }

  LOG(LOG_INFO, "HTTP server listening on [::]:%" PRIu16 "...", port);
  while (keep_running) {
    LOG(LOG_DEBUG, "HTTP server waiting for request...");
    const int client_fd = accept(server_socket, NULL, NULL); // NOLINT(android-cloexec-accept)
    if (client_fd < 0) {
      if (keep_running) {
        PERROR("HTTP server could not accept request");
        return EXIT_FAILURE;
      } else {
        return EXIT_SUCCESS;
      }
    }

    handle_client(client_fd);
  }

  return EXIT_SUCCESS;
}
