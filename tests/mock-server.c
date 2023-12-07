#include "../src/growatt.h"
#include "../src/modbus.h"
#include <assert.h>
#include <errno.h>
#include <modbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

enum { PORT = 1502, MS = 1000U };

int main(void) {
  // rand() initialization, should only be called once
  srand(time(NULL)); // NOLINT(cert-msc32-c,cert-msc51-cpp)

  // assert there is nothing relevant after the clock register
  assert(holding_registers[COUNT(holding_registers) - 1].address < REGISTER_CLOCK_ADDRESS + REGISTER_CLOCK_SIZE);

  modbus_mapping_t *mapping =
      modbus_mapping_new_start_address(0, 0, 0, 0, 0, REGISTER_CLOCK_ADDRESS + REGISTER_CLOCK_SIZE + 1, 0,
                                       input_registers[COUNT(input_registers) - 1].address + 1);

  // NOLINTBEGIN(readability-magic-numbers)
  mapping->tab_registers[34] = 80;                         // settings_max_charging_amps
  mapping->tab_registers[35] = 580;                        // settings_bulk_charging_volts (x10)
  mapping->tab_registers[36] = 544;                        // settings_float_charging_volts (x10)
  mapping->tab_registers[37] = 480;                        // settings_switch_to_utility_volts (x10)
  mapping->tab_registers[REGISTER_CLOCK_ADDRESS] = 0x07E8; // set year to 2023

  mapping->tab_input_registers[3] = 0;         // pv1_watts (double size register)
  mapping->tab_input_registers[4] = 100 * 10;  // pv1_watts (double size register)
  mapping->tab_input_registers[17] = 50 * 100; // battery_volts
  // NOLINTEND(readability-magic-numbers)

  if (mapping == NULL) {
    fprintf(stderr, "Failed to allocate the mapping: %s\n", modbus_strerror(errno));
    return EXIT_FAILURE;
  }

  modbus_t *ctx = modbus_new_tcp("127.0.0.1", PORT);
  // modbus_set_debug(ctx, TRUE);

  int socket = modbus_tcp_listen(ctx, 1);
  modbus_tcp_accept(ctx, &socket);

  uint8_t req[MODBUS_RTU_MAX_ADU_LENGTH]; // request buffer
  int len = -1;                           // length of the request/response
  size_t index = 0;
  while (++index) {
    if ((len = modbus_receive(ctx, req)) == -1) {
      perror("modbus_receive");
      break;
    }

    // simulate random delay
    const size_t delay = rand() % MODBUS_RESPONSE_TIMEOUT; // NOLINT
    usleep(delay);

    // simulate timeout after a while to ensure the program is exiting
    if (index > 95) { // NOLINT(readability-magic-numbers)
      printf("Mock server timeout mode activated, sleeping %dms...\n", MODBUS_RESPONSE_TIMEOUT / MS);
      usleep(MODBUS_RESPONSE_TIMEOUT); // NOLINT(concurrency-mt-unsafe)
    }

    if (modbus_reply(ctx, req, len, mapping) == -1) {
      perror("modbus_reply");
      break;
    }

    if (index > 100) { // NOLINT(readability-magic-numbers)
      printf("Mock server exiting...\n");
      break;
    }
  }

  printf("Mock server terminated: %s\n", modbus_strerror(errno));

  if (socket != -1) {
    close(socket);
  }
  modbus_mapping_free(mapping);
  modbus_close(ctx);
  modbus_free(ctx);

  return EXIT_SUCCESS;
}
