#include <stdio.h>
#include <stdlib.h>

#include "http.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port < 1 || port > 65535) {
        fprintf(stderr, "Invalid port number: %d\n", port);
        return 1;
    }

    return http(port);
}

