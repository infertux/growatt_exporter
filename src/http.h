#include <arpa/inet.h> // HTTP stuff
#include <bsd/string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h> // close()

#include "epever.h"
#include "modbus.h"

#define BACKLOG 10 // passed to listen()
#define PROMETHEUS_CONTENT_TYPE "text/plain; version=0.0.4; charset=utf-8"

void set_response(const uint8_t *ids, char *response) {
  char metrics[PROMETHEUS_RESPONSE_SIZE];
  int code = 0;

  if ((code = query(metrics, ids))) {
    fprintf(LOG_INFO, "Modbus query failed (code %d)\n", code);
    strlcpy(response, "HTTP/1.1 503 Service Unavailable\r\n",
            PROMETHEUS_RESPONSE_SIZE);
    strlcat(response, "Server: epever-modbus\r\n", PROMETHEUS_RESPONSE_SIZE);
  } else {
    strlcpy(response, "HTTP/1.1 200 OK\r\n", PROMETHEUS_RESPONSE_SIZE);
    strlcat(response, "Server: epever-modbus\r\n", PROMETHEUS_RESPONSE_SIZE);
    strlcat(response, "Content-Type: ", PROMETHEUS_RESPONSE_SIZE);
    strlcat(response, PROMETHEUS_CONTENT_TYPE, PROMETHEUS_RESPONSE_SIZE);
    strlcat(response, "\r\n\r\n", PROMETHEUS_RESPONSE_SIZE);
    strlcat(response, metrics, PROMETHEUS_RESPONSE_SIZE);
  }
}

int http(const uint16_t port, const uint8_t *ids) {
  int server_socket = socket(AF_INET,     // IPv4
                             SOCK_STREAM, // TCP
                             0            // protocol 0
  );

  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = inet_addr("127.0.0.1");

  if (bind(server_socket, (struct sockaddr *)&address, sizeof(address))) {
    fprintf(LOG_ERROR, "bind failed\n");
    return EXIT_FAILURE;
  }

  int listening = listen(server_socket, BACKLOG);
  if (listening < 0) {
    fprintf(LOG_ERROR, "The server is not listening\n");
    return EXIT_FAILURE;
  }

  fprintf(LOG_INFO, "HTTP server listening on 127.0.0.1:%" PRIu16 "...\n",
          port);

  char response[PROMETHEUS_RESPONSE_SIZE];
  struct timespec before, after; // NOLINT(readability-isolate-declaration)
  while (1) {
    const int client_socket =
        accept(server_socket, NULL, NULL); // NOLINT(android-cloexec-accept)
    clock_gettime(CLOCK_REALTIME, &before);
    fprintf(LOG_DEBUG, "HTTP server received request...\n");
    set_response(ids, response);
    // printf("response=\"\n%s\n\"\n", response);
    send(client_socket, response, strlen(response), 0);
    close(client_socket);
    clock_gettime(CLOCK_REALTIME, &after);

    const double elapsed = after.tv_sec - before.tv_sec +
                           (double)(after.tv_nsec - before.tv_nsec) / 1e9;
    fprintf(LOG_DEBUG, "HTTP server sent response (%zu bytes) in %.1fs\n\n",
            strlen(response), elapsed);
  }

  return EXIT_FAILURE;
}
