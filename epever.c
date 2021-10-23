#include <stdio.h>
#include <stdlib.h>

#include "http.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <device_id[,device2_id]> <port>\n", argv[0]);
        fprintf(stderr, "Example: %s 20 1234\n", argv[0]);
        fprintf(stderr, "Example: %s 20,30 1234\n", argv[0]);
        return 1;
    }

    int ids[8], i = 0;
    char *param = argv[1];
    char *id = strtok(param, ","); // extract the first ID
    while (id) { // loop through the string to extract all other tokens
        ids[i++] = atoi(id);
        id = strtok(NULL, ",");
    }
    ids[i] = -1;

    const int port = atoi(argv[2]);
    if (port < 1 || port > 65535) {
        fprintf(stderr, "Invalid port number: %d\n", port);
        return 1;
    }

    return http(port, ids);
}

