// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.clock/cpp/wire.h>
#include <getopt.h>
#include <lib/fdio/directory.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <fbl/unique_fd.h>

using FrequencyInfo = fuchsia_hardware_clock::wire::FrequencyInfo;

typedef enum action {
  UNKNOWN,
  MEASURE,
  ENABLE,
  DISABLE,
} action_t;

int usage(const char* cmd) {
  fprintf(stderr,
          "\nInteract with clocks on the SOC:\n"
          "   %s measure                    Measures all clock values\n"
          "   %s measure -idx <idx>         Measure CLK idx\n"
          "   %s enable -idx <idx>          Enable clock idx\n"
          "   %s disable -idx <idx>         Disable clock idx\n"
          "   %s help                       Print this message\n",
          cmd, cmd, cmd, cmd, cmd);
  return -1;
}

// Returns "true" if the argument matches the prefix.
// In this case, moves the argument past the prefix.
bool prefix_match(const char** arg, const char* prefix) {
  if (!strncmp(*arg, prefix, strlen(prefix))) {
    *arg += strlen(prefix);
    return true;
  }
  return false;
}

// Gets the value of a particular field passed through
// command line.
const char* getValue(int argc, char** argv, const char* field) {
  int i = 1;
  while (i < argc - 1 && strcmp(argv[i], field) != 0) {
    ++i;
  }
  if (i >= argc - 1) {
    printf("NULL\n");
    return NULL;
  } else {
    return argv[i + 1];
  }
}

char* guess_dev(void) {
  char path[26];  // strlen("/dev/class/clock-impl/###") + 1
  DIR* d = opendir("/dev/class/clock-impl");
  if (!d) {
    return NULL;
  }

  struct dirent* de;
  while ((de = readdir(d)) != NULL) {
    if (strlen(de->d_name) != 3) {
      continue;
    }

    if (isdigit(de->d_name[0]) && isdigit(de->d_name[1]) && isdigit(de->d_name[2])) {
      sprintf(path, "/dev/class/clock-impl/%.3s", de->d_name);
      closedir(d);
      return strdup(path);
    }
  }

  closedir(d);
  return NULL;
}

ssize_t measure_clk_util(fidl::WireSyncClient<fuchsia_hardware_clock::Device>& client,
                         uint32_t idx) {
  auto measure_result = client->Measure(idx);

  if (!measure_result.ok()) {
    fprintf(stderr, "Failed to measure clock: %s\n", zx_status_get_string(measure_result.status()));
    return -1;
  }

  auto result = client->Measure(idx);

  if (!result.ok()) {
    fprintf(stderr, "ERROR: failed to measure clock: %d\n", result.status());
    return -1;
  }

  std::string name(std::begin(result->info.name), std::end(result->info.name));

  printf("[%4d][%4ld MHz] %s\n", idx, result->info.frequency, name.c_str());
  return 0;
}

ssize_t measure_clk(const char* path, uint32_t idx, bool clk) {
  fbl::unique_fd device(open(path, O_RDWR));

  if (!device.is_valid()) {
    fprintf(stderr, "Failed to open clock device: %s\n", strerror(errno));
    return -1;
  }

  fidl::ClientEnd<fuchsia_hardware_clock::Device> client_end;

  zx_status_t st =
      fdio_get_service_handle(device.release(), client_end.channel().reset_and_get_address());
  if (st != ZX_OK) {
    fprintf(stderr, "Failed to get service handle: %s\n", zx_status_get_string(st));
    return -1;
  }

  fidl::WireSyncClient<fuchsia_hardware_clock::Device> client(std::move(client_end));

  auto clk_count_result = client->GetCount();
  if (!clk_count_result.ok()) {
    fprintf(stderr, "Failed to get count: %s\n", zx_status_get_string(clk_count_result.status()));
    return -1;
  }

  if (clk) {
    if (idx > clk_count_result->count) {
      fprintf(stderr, "ERROR: Invalid clock index.\n");
      return -1;
    }
    return measure_clk_util(client, idx);
  } else {
    for (uint32_t i = 0; i < clk_count_result->count; i++) {
      ssize_t rc = measure_clk_util(client, i);
      if (rc < 0) {
        return rc;
      }
    }
  }

  return 0;
}

ssize_t toggle_clk(const char* path, uint32_t idx, action_t subcmd) {
  if (subcmd != ENABLE && subcmd != DISABLE) {
    return -1;
  }

  fbl::unique_fd device(open(path, O_RDWR));

  if (!device.is_valid()) {
    fprintf(stderr, "Failed to open clock device: %s\n", strerror(errno));
    return -1;
  }

  fidl::ClientEnd<fuchsia_hardware_clock::Device> client_end;

  zx_status_t st =
      fdio_get_service_handle(device.release(), client_end.channel().reset_and_get_address());
  if (st != ZX_OK) {
    fprintf(stderr, "Failed to get service handle: %s\n", zx_status_get_string(st));
    return -1;
  }

  fidl::WireSyncClient<fuchsia_hardware_clock::Device> client(std::move(client_end));

  if (subcmd == ENABLE) {
    auto en_result = client->Enable(idx);
    if (!en_result.ok()) {
      fprintf(stderr, "ERROR: Failed to enable clock: %s\n",
              zx_status_get_string(en_result.status()));
      return -1;
    }
  } else if (subcmd == DISABLE) {
    auto dis_result = client->Disable(idx);
    if (!dis_result.ok()) {
      fprintf(stderr, "ERROR: Failed to disable clock: %s\n",
              zx_status_get_string(dis_result.status()));
      return -1;
    }
  } else {
    fprintf(stderr, "ERROR: Unknown command\n");
    return -1;
  }

  return 0;
}

int main(int argc, char** argv) {
  const char* cmd = basename(argv[0]);
  char* path = NULL;
  const char* index = NULL;
  action_t subcmd = UNKNOWN;
  bool clk = false;
  uint32_t idx = 0;

  // If no arguments passed, bail out after dumping
  // usage information.
  if (argc == 1) {
    return usage(cmd);
  }

  // Parse all args.
  while (argc > 1) {
    const char* arg = argv[1];
    if (prefix_match(&arg, "measure")) {
      subcmd = MEASURE;
    } else if (prefix_match(&arg, "enable")) {
      subcmd = ENABLE;
    } else if (prefix_match(&arg, "disable")) {
      subcmd = DISABLE;
    }

    if (prefix_match(&arg, "-idx")) {
      index = getValue(argc, argv, "-idx");
      clk = true;
      if (index) {
        idx = atoi(index);
      } else {
        fprintf(stderr, "Enter Valid CLK IDX.\n");
      }
    }
    if (prefix_match(&arg, "help")) {
      return usage(cmd);
    }
    argc--;
    argv++;
  }

  // Get the device path.
  path = guess_dev();
  if (!path) {
    fprintf(stderr, "No CLK device found.\n");
    return usage(cmd);
  }

  ssize_t err;
  switch (subcmd) {
    case MEASURE:
      err = measure_clk(path, idx, clk);
      if (err) {
        printf("Measure CLK failed.\n");
      }
      break;
    case ENABLE:
      __FALLTHROUGH;
    case DISABLE:
      if (!clk) {
        printf("-idx argument is required.\n");
        return -1;
      }

      err = toggle_clk(path, idx, subcmd);
      if (err) {
        printf("Clock Toggle failed\n");
      }
      break;
    default:
      return usage(cmd);
  }

  return 0;
}
