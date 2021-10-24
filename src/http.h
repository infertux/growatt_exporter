#include <stdio.h>
//#include <stdlib.h>
#include <string.h> // strcat()
//#include <errno.h>
#include <arpa/inet.h> // HTTP stuff
#include <time.h>
#include <unistd.h> // close()

#include "modbus.h"

#define BUFFER_SIZE 4096 // must hold both the HTTP headers and body
#define BACKLOG 10       // passed to listen()

void set_response(const int *ids, char *response) {
  char metrics[BUFFER_SIZE];

  if (query(ids, metrics)) {
    fprintf(stderr, "Modbus query failed\n");
    strcpy(response, "HTTP/1.1 503 Service Unavailable\r\n");
    strcat(response, "Server: epever-modbus\r\n");
  } else {
    strcpy(response, "HTTP/1.1 200 OK\r\n");
    strcat(response, "Server: epever-modbus\r\n");
    strcat(response,
           "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n");
    strcat(response, "\r\n");
    strcat(response, metrics);
  }

  strcat(response, "\0");
}

int http(const int port, const int *ids) {
  int server_socket = socket(AF_INET,     // IPv4
                             SOCK_STREAM, // TCP
                             0            // protocol 0
  );

  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = inet_addr("127.0.0.1");

  bind(server_socket, (struct sockaddr *)&address, sizeof(address));

  int listening = listen(server_socket, BACKLOG);
  if (listening < 0) {
    fprintf(stderr, "The server is not listening.\n");
    return 1;
  }

  printf("HTTP server listening on 127.0.0.1:%d...\n", port);

  int client_socket;
  char response[BUFFER_SIZE];
  struct timespec before, after;
  double elapsed;
  while (1) {
    client_socket = accept(server_socket, NULL, NULL);
    clock_gettime(CLOCK_REALTIME, &before);
    printf("HTTP server received request...\n");
    set_response(ids, response);
    // printf("response=\"%s\"\n", response);
    send(client_socket, response, strlen(response), 0);
    close(client_socket);
    clock_gettime(CLOCK_REALTIME, &after);

    elapsed = after.tv_sec - before.tv_sec +
              (double)(after.tv_nsec - before.tv_nsec) / 1e9;
    printf("HTTP server sent response (%ld bytes) in %.1fs\n", strlen(response),
           elapsed);
  }

  return 1;
}
