// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <lib/counter-vmo-abi.h>
#include <lib/fdio/io.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/status.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <utility>

#include <fbl/array.h>
#include <fbl/unique_fd.h>

#include "kcounter_cmdline.h"

namespace {

constexpr char kVmoFileDir[] = "/boot/kernel";

}  // anonymous namespace

int main(int argc, char** argv) {
  KcounterCmdline cmdline;
  if (!kcounter_parse_cmdline(argc, argv, stderr, &cmdline)) {
    return 1;
  }

  if (cmdline.help) {
    kcounter_usage(argv[0], stdout);
    return 0;
  }

  if (cmdline.period) {
    printf("watch mode every %d seconds\n", cmdline.period);
  }

  fbl::unique_fd dir_fd(open(kVmoFileDir, O_RDONLY | O_DIRECTORY));
  if (!dir_fd) {
    fprintf(stderr, "%s: %s\n", kVmoFileDir, strerror(errno));
    return 2;
  }

  fzl::OwnedVmoMapper desc_mapper;
  const counters::DescriptorVmo* desc;
  {
    fbl::unique_fd desc_fd(openat(dir_fd.get(), counters::DescriptorVmo::kVmoName, O_RDONLY));
    if (!desc_fd) {
      fprintf(stderr, "%s/%s: %s\n", kVmoFileDir, counters::DescriptorVmo::kVmoName,
              strerror(errno));
      return 2;
    }
    zx::vmo vmo;
    zx_status_t status = fdio_get_vmo_exact(desc_fd.get(), vmo.reset_and_get_address());
    if (status != ZX_OK) {
      fprintf(stderr, "fdio_get_vmo_exact: %s: %s\n", counters::DescriptorVmo::kVmoName,
              zx_status_get_string(status));
      return 2;
    }
    uint64_t size;
    status = vmo.get_size(&size);
    if (status != ZX_OK) {
      fprintf(stderr, "cannot get %s VMO size: %s\n", counters::DescriptorVmo::kVmoName,
              zx_status_get_string(status));
      return 2;
    }
    status = desc_mapper.Map(std::move(vmo), size, ZX_VM_PERM_READ);
    if (status != ZX_OK) {
      fprintf(stderr, "cannot map %s VMO: %s\n", counters::DescriptorVmo::kVmoName,
              zx_status_get_string(status));
      return 2;
    }
    desc = reinterpret_cast<counters::DescriptorVmo*>(desc_mapper.start());
    if (desc->magic != counters::DescriptorVmo::kMagic) {
      fprintf(stderr, "%s: magic number %" PRIu64 " != expected %" PRIu64 "\n",
              counters::DescriptorVmo::kVmoName, desc->magic, counters::DescriptorVmo::kMagic);
      return 2;
    }
    if (size < sizeof(*desc) + desc->descriptor_table_size) {
      fprintf(stderr, "%s size %#" PRIx64 " too small for %" PRIu64 " bytes of descriptor table\n",
              counters::DescriptorVmo::kVmoName, size, desc->descriptor_table_size);
      return 2;
    }
  }

  fzl::OwnedVmoMapper arena_mapper;
  const volatile int64_t* arena = nullptr;
  if (!cmdline.list) {
    fbl::unique_fd arena_fd(openat(dir_fd.get(), counters::kArenaVmoName, O_RDONLY));
    if (!arena_fd) {
      fprintf(stderr, "%s/%s: %s\n", kVmoFileDir, counters::kArenaVmoName, strerror(errno));
      return 2;
    }
    zx::vmo vmo;
    zx_status_t status = fdio_get_vmo_exact(arena_fd.get(), vmo.reset_and_get_address());
    if (status != ZX_OK) {
      fprintf(stderr, "fdio_get_vmo_exact: %s: %s\n", counters::kArenaVmoName,
              zx_status_get_string(status));
      return 2;
    }
    uint64_t size;
    status = vmo.get_size(&size);
    if (status != ZX_OK) {
      fprintf(stderr, "cannot get %s VMO size: %s\n", counters::kArenaVmoName,
              zx_status_get_string(status));
      return 2;
    }
    if (size < desc->max_cpus * desc->num_counters() * sizeof(int64_t)) {
      fprintf(stderr,
              "%s size %#" PRIx64 " too small for %" PRIu64 " CPUS * %" PRIu64 " counters\n",
              counters::kArenaVmoName, size, desc->max_cpus, desc->num_counters());
      return 2;
    }
    status = arena_mapper.Map(std::move(vmo), size, ZX_VM_PERM_READ);
    if (status != ZX_OK) {
      fprintf(stderr, "cannot map %s VMO: %s\n", counters::kArenaVmoName,
              zx_status_get_string(status));
      return 2;
    }
    arena = reinterpret_cast<int64_t*>(arena_mapper.start());
  }

  dir_fd.reset();

  fbl::Array<bool> matched(new bool[argc - cmdline.unparsed_args_start](),
                           argc - cmdline.unparsed_args_start);
  auto matches = [&](const char* name) -> bool {
    if (cmdline.unparsed_args_start == argc) {
      return true;
    }
    for (int i = cmdline.unparsed_args_start; i < argc; ++i) {
      if (!strncmp(name, argv[i], strlen(argv[i]))) {
        matched[i - cmdline.unparsed_args_start] = true;
        return true;
      }
    }
    return false;
  };

  size_t times = 1;
  zx_time_t deadline = zx_clock_get_monotonic();
  bool match_failed = false;

  if (cmdline.period != 0) {
    printf("Dumping counters every %d seconds.  Press any key to stop.\n", cmdline.period);
  }

  while (true) {
    if (cmdline.period != 0) {
      deadline += ZX_SEC(cmdline.period);
      printf("[%zu]\n", times);
    }

    for (size_t i = 0; i < desc->num_counters(); ++i) {
      const auto& entry = desc->descriptor_table[i];
      if (matches(entry.name)) {
        if (cmdline.list) {
          fputs(entry.name, stdout);
          switch (entry.type) {
            case counters::Type::kSum:
              puts(" sum");
              break;
            case counters::Type::kMin:
              puts(" min");
              break;
            case counters::Type::kMax:
              puts(" max");
              break;
            default:
              printf(" ??? unknown type %" PRIu64 " ???\n", static_cast<uint64_t>(entry.type));
          }
        } else {
          if (!cmdline.terse) {
            printf("%s =%s", entry.name,
                   !cmdline.verbose                     ? " "
                   : entry.type == counters::Type::kMin ? " min("
                   : entry.type == counters::Type::kMax ? " max("
                                                        : " ");
          }
          int64_t value = 0;
          for (uint64_t cpu = 0; cpu < desc->max_cpus; ++cpu) {
            const int64_t cpu_value = arena[(cpu * desc->num_counters()) + i];
            if (cmdline.verbose) {
              printf("%s%" PRId64,
                     cpu == 0                             ? ""
                     : entry.type == counters::Type::kSum ? " + "
                                                          : ", ",
                     cpu_value);
            }
            switch (entry.type) {
              case counters::Type::kSum:
              default:
                value += cpu_value;
                break;
              case counters::Type::kMin:
                if (cpu_value < value) {
                  value = cpu_value;
                }
                break;
              case counters::Type::kMax:
                if (cpu_value > value) {
                  value = cpu_value;
                }
                break;
            }
          }
          if (cmdline.verbose) {
            printf("%s = %" PRId64 "\n", entry.type == counters::Type::kSum ? "" : ")", value);
          } else {
            int64_t ev_per_nsec = 0;
            if (unlikely(mul_overflow(value, 1000000000LL, &ev_per_nsec))) {
              printf("%" PRId64 " [rate overflow]\n", value);
            } else {
              auto rate = ev_per_nsec / zx_clock_get_monotonic();
              if (rate != 0) {
                printf("%" PRId64 " [%" PRId64 "/sec]\n", value, rate);
              } else {
                printf("%" PRId64 "\n", value);
              }
            }
          }
        }
      }
    }

    // Check that each prefix was actually used.
    if (times == 1) {
      for (auto it = matched.begin(); it != matched.end(); ++it) {
        if (!*it) {
          fprintf(stderr, "%s: prefix not found\n",
                  argv[cmdline.unparsed_args_start + (it - matched.begin())]);
          match_failed = true;
        }
      }
    }

    if ((cmdline.period == 0) || match_failed) {
      break;
    }

    zx_time_t now = zx_clock_get_monotonic();
    zx_duration_t timeout = zx_time_sub_time(deadline, now);
    if (timeout > 0) {
      struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
      int msec_timeout = static_cast<int>(std::min<zx_duration_t>(
          (timeout + ZX_MSEC(1) - 1) / ZX_MSEC(1), std::numeric_limits<int>::max()));

      int poll_result = poll(&pfd, 1, msec_timeout);
      if (poll_result > 0) {
        printf("Shutting down\n");
        break;
      }
    } else {
      // We are falling behind.   Reset our deadline to catch up
      deadline = now;
    }

    ++times;
  }  // while

  return match_failed ? 1 : 0;
}
