#ifndef GROWATT_LOG_H
#define GROWATT_LOG_H

#include <string.h>

#define LOG_VERBOSE 0 // compile with 1 to enable debug messages

enum {
  LOG_TRACE,
  LOG_DEBUG,
  LOG_INFO,
  LOG_ERROR,
};

#define PERROR_HELPER(fmt, ...) fprintf(stderr, "\x1b[37;41m" fmt ": %s%s\n\x1b[0m", __VA_ARGS__, strerror(errno))
#define PERROR(...) PERROR_HELPER(__VA_ARGS__, "")

const char *log_prefix(const char filename[static 1]) {
  if (!strcmp(filename, "src/growatt.c")) {
    return "32m[GRWT] ";
  } else if (!strcmp(filename, "src/mqtt.h")) {
    return "33m[MQTT] ";
  } else if (!strcmp(filename, "src/modbus.h")) {
    return "34m[MDBS] ";
  } else if (!strcmp(filename, "src/prometheus.h")) {
    return "35m[PRMT] ";
  }

  return "31m[????] ";
}

#define LOG(level, string, ...)                                                                                        \
  if (LOG_VERBOSE || level >= LOG_INFO)                                                                                \
  fprintf((level == LOG_ERROR ? stderr : stdout), "\x1b[%s" string "\x1b[0m\n",                                        \
          log_prefix(__FILE__) __VA_OPT__(, ) __VA_ARGS__)

#endif /* GROWATT_LOG_H */
