// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "option.h"

#include <cstring>

#include "util.h"

#define OPTION_DEFAULT(option)         \
  case OPTION_##option:                \
    value = OPTION_##option##_DEFAULT; \
    break

static void initialize_options(struct options* o) {
  for (int i = 0; i < OPTION_MAX; ++i) {
    const char* value = NULL;
    switch (option(i)) {
      OPTION_DEFAULT(ROOT);
      OPTION_DEFAULT(FILENAME);
      OPTION_DEFAULT(SHUTDOWN);
      OPTION_DEFAULT(REBOOT);
      case OPTION_MAX:
        __builtin_unreachable();
    }
    o->value[i] = value;
  }
}

#define OPTION_STRING(option)                          \
  case OPTION_##option:                                \
    string = OPTION_##option##_STRING;                 \
    string_len = sizeof(OPTION_##option##_STRING) - 1; \
    break

static void apply_option(struct options* o, const char* arg) {
  size_t len = strlen(arg);
  for (int i = 0; i < OPTION_MAX; ++i) {
    const char* string = NULL;
    size_t string_len = 0;
    switch (option(i)) {
      OPTION_STRING(ROOT);
      OPTION_STRING(FILENAME);
      OPTION_STRING(SHUTDOWN);
      OPTION_STRING(REBOOT);
      case OPTION_MAX:
        __builtin_unreachable();
    }
    if (len > string_len && arg[string_len] == '=' && !strncmp(arg, string, string_len)) {
      o->value[i] = &arg[string_len + 1];
    }
  }
}

uint32_t parse_options(zx_handle_t log, const char* cmdline, size_t cmdline_size,
                       struct options* o) {
  initialize_options(o);

  if (cmdline_size == 0 || cmdline[cmdline_size - 1] != '\0') {
    fail(log, "kernel command line of %zu bytes is unterminated", cmdline_size);
  }

  // Count the strings while parsing them.
  uint32_t n = 0;
  for (const char* p = cmdline; p < &cmdline[cmdline_size - 1]; p = strchr(p, '\0') + 1) {
    printl(log, "option \"%s\"", p);
    apply_option(o, p);
    ++n;
  }

  return n;
}
