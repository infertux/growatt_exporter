// SPDX-License-Identifier: AGPL-3.0-or-later

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "epever.h"
#include "http.h"

#define MAX_PORT_NUMBER USHRT_MAX
#define MAX_DEVICES 8
#define RADIX_DECIMAL 10

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(LOG_ERROR, "Usage: %s <device_id[,device2_id]> <port>\n", argv[0]);
    fprintf(LOG_ERROR, "Example: %s 20 1234\n", argv[0]);
    fprintf(LOG_ERROR, "Example: %s 20,30 1234\n", argv[0]);
    return EXIT_FAILURE;
  }

  uint8_t device_ids[MAX_DEVICES] = {0};
  uint8_t current_device_id = 0;
  char *device_id = NULL;
  char *rest = argv[1];
  while ((device_id = strtok_r(rest, ",", &rest))) {
    device_ids[current_device_id++] = strtol(device_id, NULL, RADIX_DECIMAL);
  }

  const uint16_t port = strtol(argv[2], NULL, RADIX_DECIMAL);
  if (port < 1 || port > MAX_PORT_NUMBER) {
    fprintf(LOG_ERROR, "Invalid port number: %d\n", port);
    return EXIT_FAILURE;
  }

  return http(port, device_ids);
}
