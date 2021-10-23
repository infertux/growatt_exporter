#include <stdio.h>
//#include <stdlib.h>
#include <string.h> // strcat()
//#include <errno.h>
#include <unistd.h> // close()
#include <arpa/inet.h> // HTTP stuff

#include "modbus.h"

#define PORT 1234
#define BUFFER_SIZE 4096 // must hold both the HTTP headers and body
#define BACKLOG 10  // passed to listen()

void set_response(char *response)
{
    char metrics[BUFFER_SIZE];

    if (query(metrics)) {
        fprintf(stderr, "Modbus query failed\n");
        strcpy(response, "HTTP/1.1 503 Service Unavailable\r\n");
        strcat(response, "Server: epever-modbus\r\n");
    } else {
        strcpy(response, "HTTP/1.1 200 OK\r\n");
        strcat(response, "Server: epever-modbus\r\n");
        strcat(response, "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n");
        strcat(response, "\r\n");
        strcat(response, metrics);
    }
}

int http(void)
{
    int server_socket = socket(
        AF_INET,     // IPv4
        SOCK_STREAM, // TCP
        0            // protocol 0
    );

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = inet_addr("127.0.0.1");

    bind(server_socket, (struct sockaddr *)&address, sizeof(address));

    int listening = listen(server_socket, BACKLOG);
    if (listening < 0) {
        fprintf(stderr, "The server is not listening.\n");
        return 1;
    }

    int client_socket;
    char response[BUFFER_SIZE];
    while(1) {
        printf("HTTP server listening on 127.0.0.1:%d...\n", PORT);
        client_socket = accept(server_socket, NULL, NULL);

        set_response(response);
        //printf("response=\"%s\"\n", response);

        send(client_socket, response, sizeof(response), 0);
        close(client_socket);
    }

    return 1;
}
