// SPDX-License-Identifier: AGPL-3.0-or-later

#include <stdio.h>
#include <stdlib.h>

#include "http.h"

#define MAX_PORT_NUMBER 65535
#define MAX_DEVICES 8

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <device_id[,device2_id]> <port>\n", argv[0]);
    fprintf(stderr, "Example: %s 20 1234\n", argv[0]);
    fprintf(stderr, "Example: %s 20,30 1234\n", argv[0]);
    return 1;
  }

  int device_ids[MAX_DEVICES];
  int current_device_id = 0;
  char *device_id = NULL;
  char *rest = argv[1];
  while ((device_id = strtok_r(rest, ",", &rest))) {
    device_ids[current_device_id++] = atoi(device_id);
  }
  device_ids[current_device_id] = -1;

  const int port = atoi(argv[2]);
  if (port < 1 || port > MAX_PORT_NUMBER) {
    fprintf(stderr, "Invalid port number: %d\n", port);
    return 1;
  }

  return http(port, device_ids);
}
