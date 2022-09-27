// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/sys/component/cpp/service_client.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <utility>

#include <fbl/unique_fd.h>

#include "src/storage/fshost/constants.h"

namespace fio = fuchsia_io;

int usage(void) {
  fprintf(stderr, "usage: df [ <option>* ] [paths]\n");
  fprintf(stderr, "df displays the mounted filesystems for a list of paths\n");
  fprintf(stderr, " -i : List inode information instead of block usage\n");
  fprintf(stderr, " -h : Show sizes in human readable format (e.g., 1K 2M 3G)\n");
  fprintf(stderr, " --help : Show this help message\n");
  return -1;
}

typedef struct {
  bool node_usage;
  bool human_readable;
} df_options_t;

const char* root = "/";

int parse_args(int argc, const char** argv, df_options_t* options, const char*** dirs,
               size_t* count) {
  while (argc > 1) {
    if (!strcmp(argv[1], "-i")) {
      options->node_usage = true;
    } else if (!strcmp(argv[1], "-h")) {
      options->human_readable = true;
    } else if (!strcmp(argv[1], "--help")) {
      return usage();
    } else {
      break;
    }
    argc--;
    argv++;
  }
  if (argc >= 2) {
    *dirs = &argv[1];
    *count = argc - 1;
  } else {
    *dirs = &root;
    *count = 1;
  }
  return 0;
}

// Format for the header
const char* hfmt = "%-10s %10s %10s %10s %3s%%  %-10s  %-10s\n";
// Format for the human-readable header
const char* hrfmt = "%-10s %5s %5s %5s %5s%%  %-10s  %-10s\n";
// Format for the individual filesystems queried
const char* ffmt = "%-10s %10zu %10zu %10zu %3zu%%  %-10s  %-10s\n";

#define KB (1lu << 10)
#define MB (1lu << 20)
#define GB (1lu << 30)
#define TB (1lu << 40)
#define PB (1lu << 50)
#define EB (1lu << 60)

// Conditionally print the size if it falls within the range of the magnitude.
// [1.0XX, 999XX]
bool print_magnitude(int padding, size_t size, size_t magnitude, const char* mag_string) {
  if (size < 10 * magnitude) {
    printf("%*zu.%zu%s ", padding - 4, size / magnitude, size / (magnitude / 10) % 10, mag_string);
    return true;
  } else if (size < magnitude << 10) {
    printf("%*zu%s ", padding - 2, size / magnitude, mag_string);
    return true;
  }
  return false;
}

void print_human_readable(int padding, size_t size) {
  if (size < KB) {
    printf("%*s ", padding, "0");
  } else if (print_magnitude(padding, size, KB, "KB")) {
  } else if (print_magnitude(padding, size, MB, "MB")) {
  } else if (print_magnitude(padding, size, GB, "GB")) {
  } else if (print_magnitude(padding, size, TB, "TB")) {
  } else if (print_magnitude(padding, size, PB, "PB")) {
  } else {
    printf("%*zu ", padding, size);
  }
}

void print_fs_type(const char* name, const df_options_t* options,
                   const fuchsia_io::wire::FilesystemInfo* info, const char* device_path) {
  if (options->node_usage) {
    size_t nodes_total = info ? info->total_nodes : 0;
    size_t nodes_used = info ? info->used_nodes : 0;
    size_t nodes_available = nodes_total - nodes_used;
    size_t use_percentage = nodes_total ? nodes_used * 100 / nodes_total : 0;
    printf(ffmt, info != nullptr ? reinterpret_cast<const char*>(info->name.data()) : "?",
           nodes_total, nodes_used, nodes_available, use_percentage, name, device_path);
  } else {
    // Block Usage
    if (options->human_readable) {
      size_t bytes_total = info ? info->total_bytes : 0;
      size_t bytes_used = info ? info->used_bytes : 0;
      size_t bytes_available = bytes_total - bytes_used;
      size_t use_percentage = bytes_total ? bytes_used * 100 / bytes_total : 0;
      printf("%-10s ", info != nullptr ? reinterpret_cast<const char*>(info->name.data()) : "?");
      print_human_readable(5, bytes_total);
      print_human_readable(5, bytes_used);
      print_human_readable(5, bytes_available);
      printf("%5zu%%  ", use_percentage);
      printf("%-10s  ", name);
      printf("%-10s\n", device_path);
    } else {
      size_t blocks_total = info ? info->total_bytes >> 10 : 0;
      size_t blocks_used = info ? info->used_bytes >> 10 : 0;
      size_t blocks_available = blocks_total - blocks_used;
      size_t use_percentage = blocks_total ? blocks_used * 100 / blocks_total : 0;
      printf(ffmt, info != nullptr ? reinterpret_cast<const char*>(info->name.data()) : "?",
             blocks_total, blocks_used, blocks_available, use_percentage, name, device_path);
    }
  }
}

int main(int argc, const char** argv) {
  const char** dirs;
  size_t dircount;
  df_options_t options;
  memset(&options, 0, sizeof(df_options_t));
  int r;
  if ((r = parse_args(argc, argv, &options, &dirs, &dircount))) {
    return r;
  }

  if (options.node_usage) {
    printf(hfmt, "Filesystem", "Inodes", "IUsed", "IFree", "IUse", "Path", "Device");
  } else {
    if (options.human_readable) {
      printf(hrfmt, "Filesystem", "Size", "Used", "Avail", "Use", "Path", "Device");
    } else {
      printf(hfmt, "Filesystem", "1K-Blocks", "Used", "Available", "Use", "Path", "Device");
    }
  }

  std::string fshost_path(fshost::kHubAdminServicePath);
  auto fshost_or = component::Connect<fuchsia_fshost::Admin>(fshost_path.c_str());
  if (fshost_or.is_error()) {
    fprintf(stderr, "Error connecting to fshost (@ %s): %s\n", fshost_path.c_str(),
            fshost_or.status_string());
    // Continue...
  }

  for (size_t i = 0; i < dircount; i++) {
    fbl::unique_fd fd;
    fd.reset(open(dirs[i], O_RDONLY));
    if (!fd) {
      fprintf(stderr, "df: Could not open target: %s\n", dirs[i]);
      continue;
    }

    fuchsia_io::wire::FilesystemInfo info;
    fdio_cpp::FdioCaller caller(std::move(fd));
    auto result =
        fidl::WireCall(fidl::UnownedClientEnd<fuchsia_io::Directory>(caller.borrow_channel()))
            ->QueryFilesystem();
    if (!result.ok() || result.value().s != ZX_OK) {
      print_fs_type(dirs[i], &options, nullptr, "Unknown; cannot query filesystem");
      continue;
    }
    info = *result.value().info;
    info.name[fuchsia_io::wire::kMaxFsNameBuffer - 1] = '\0';

    std::string device_path;
    if (fshost_or.is_ok()) {
      auto result = fidl::WireCall(*fshost_or)->GetDevicePath(info.fs_id);
      if (!result.ok()) {
        fprintf(stderr, "Error getting device path, fidl error: %s\n",
                result.FormatDescription().c_str());
        return EXIT_FAILURE;
      }
      if (result->is_error())
        device_path = zx_status_get_string(result->error_value());
      else
        device_path = std::string(result->value()->path.get());
    }

    print_fs_type(dirs[i], &options, &info, device_path.c_str());
  }

  return 0;
}
