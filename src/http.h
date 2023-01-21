#include <arpa/inet.h> // HTTP stuff
#include <bsd/string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h> // close()

#include "log.h"
#include "modbus.h"

enum {
  BACKLOG = 10, // passed to listen()
};

#define PROMETHEUS_CONTENT_TYPE "text/plain; version=0.0.4; charset=utf-8"

void set_response(const uint8_t *ids, char *response) {
  char metrics[PROMETHEUS_RESPONSE_SIZE];
  int code = 0;

  if ((code = query(metrics, ids))) { // NOLINT(bugprone-assignment-in-if-condition)
    fprintf(LOG_ERROR, "Modbus query failed (code %d)\n", code);

    strlcpy(metrics, "503 Service Temporarily Unavailable\n", PROMETHEUS_RESPONSE_SIZE);

    strlcpy(response, "HTTP/1.1 503 Service Unavailable\r\n", PROMETHEUS_RESPONSE_SIZE);
  } else {
    strlcpy(response, "HTTP/1.1 200 OK\r\n", PROMETHEUS_RESPONSE_SIZE);
  }

  strlcat(response, "Server: growatt-modbus\r\n", PROMETHEUS_RESPONSE_SIZE);
  char content_length[PROMETHEUS_RESPONSE_SIZE];
  sprintf(content_length, "Content-Length: %zu\r\n", strlen(metrics));
  strlcat(response, content_length, PROMETHEUS_RESPONSE_SIZE);
  strlcat(response, "Content-Type: ", PROMETHEUS_RESPONSE_SIZE);
  strlcat(response, PROMETHEUS_CONTENT_TYPE, PROMETHEUS_RESPONSE_SIZE);
  strlcat(response, "\r\n\r\n", PROMETHEUS_RESPONSE_SIZE);
  strlcat(response, metrics, PROMETHEUS_RESPONSE_SIZE);
}

int http(const uint16_t port, const uint8_t *ids) {
  int server_socket = socket(AF_INET6,    // IPv6
                             SOCK_STREAM, // TCP
                             0            // protocol 0
  );

  struct sockaddr_in6 address;
  address.sin6_family = AF_INET6;
  address.sin6_addr = in6addr_any;
  address.sin6_port = htons(port);

  if (bind(server_socket, (struct sockaddr *)&address, sizeof(address))) {
    fprintf(LOG_ERROR, "bind failed\n");
    return EXIT_FAILURE;
  }

  int listening = listen(server_socket, BACKLOG);
  if (listening < 0) {
    fprintf(LOG_ERROR, "The server is not listening\n");
    return EXIT_FAILURE;
  }

  fprintf(LOG_INFO, "HTTP server listening on [::]:%" PRIu16 "...\n", port);

  char response[PROMETHEUS_RESPONSE_SIZE];
  struct timespec before, after; // NOLINT(readability-isolate-declaration)
  while (1) {
    fprintf(LOG_DEBUG, "HTTP server waiting for request...\n");
    const int client_socket = accept(server_socket, NULL, NULL); // NOLINT(android-cloexec-accept)
    clock_gettime(CLOCK_REALTIME, &before);
    fprintf(LOG_DEBUG, "HTTP server received request...\n");
    set_response(ids, response);
    // printf("response=\"\n%s\n\"\n", response);

    const size_t expected_size = strlen(response);
    const size_t actual_size = write(client_socket, response, expected_size);
    if (actual_size != expected_size) {
      fprintf(LOG_ERROR, "Wrote %zu bytes instead of %zu\n", actual_size, expected_size);
      return EXIT_FAILURE;
    }

    if (close(client_socket)) {
      fprintf(LOG_ERROR, "Error %d closing socket\n", errno);
      return EXIT_FAILURE;
    }

    clock_gettime(CLOCK_REALTIME, &after);

    const double elapsed =
        after.tv_sec - before.tv_sec + (double)(after.tv_nsec - before.tv_nsec) / 1e9; // NOLINT
    fprintf(LOG_INFO, "HTTP server sent response (%zu bytes) in %.1fs\n\n", strlen(response),
            elapsed);
  }

  return EXIT_FAILURE;
}
