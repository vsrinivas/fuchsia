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
#include <vector>

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

  if (cmdline.cpuid != kNoCpuIdChosen) {
    // The command line parser should have already ensured that this value is
    // non-negative.
    ZX_DEBUG_ASSERT(cmdline.cpuid >= 0);

    // The command line parser should have made certain we did not select both
    // --cpuid and --verbose.
    ZX_DEBUG_ASSERT(cmdline.verbose == false);

    if (static_cast<uint64_t>(cmdline.cpuid) >= desc->max_cpus) {
      fprintf(stderr, "CPU ID %d is out of range.  Descriptor reports max_cpus as %lu\n",
              cmdline.cpuid, desc->max_cpus);
      return 1;
    }
    printf("Dumping counters for CPU ID %d.\n", cmdline.cpuid);
  }

  if (cmdline.period != 0) {
    printf("Dumping counters every %d seconds.  Press any key to stop.\n", cmdline.period);
  }

  uint64_t cpu_range_start = (cmdline.cpuid != kNoCpuIdChosen) ? cmdline.cpuid : 0;
  uint64_t cpu_range_end = (cmdline.cpuid != kNoCpuIdChosen) ? cmdline.cpuid + 1 : desc->max_cpus;

  size_t match_count = 0;
  size_t max_name_length = 0;
  for (size_t i = 0; i < desc->num_counters(); ++i) {
    const auto& entry = desc->descriptor_table[i];
    if (matches(entry.name)) {
      max_name_length = std::max(max_name_length, strlen(entry.name));
      match_count++;
    }
  }
  std::vector<int64_t> previous_values(cmdline.verbose ? 0 : match_count);

  // Set last sample time to zero so that the system and period averages match
  // on the first iteration.
  zx_time_t last_sample_time = 0;

  // The printf modifier * expects an int for the field size. A negative value
  // specifies left justification.
  const auto name_field_width = -static_cast<int>(max_name_length);

  while (true) {
    if (cmdline.period != 0) {
      deadline += ZX_SEC(cmdline.period);
      printf("[%zu]\n", times);
    }

    if (!cmdline.terse && !cmdline.verbose && !cmdline.list) {
      printf("%*s     %-10s     %-9s   %-10s\n", name_field_width, "Counter", "Value", "Sys Avg",
             "Period Avg");
    }

    const zx_time_t sample_time = zx_clock_get_monotonic();
    size_t match_index = 0;
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
              fprintf(stderr, " ??? unknown type %" PRIu64 " ???\n",
                      static_cast<uint64_t>(entry.type));
          }
        } else {
          if (!cmdline.terse) {
            printf("%*s =%s", name_field_width, entry.name,
                   !cmdline.verbose                     ? " "
                   : entry.type == counters::Type::kMin ? " min("
                   : entry.type == counters::Type::kMax ? " max("
                                                        : " ");
          }
          int64_t value = 0;
          for (uint64_t cpu = cpu_range_start; cpu < cpu_range_end; ++cpu) {
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
            const bool overflow = mul_overflow(value, 1000000000LL, &ev_per_nsec);
            const int64_t system_rate = ev_per_nsec / sample_time;
            const int64_t delta_value = value - previous_values[match_index];
            const int64_t delta_sample_time = sample_time - last_sample_time;
            const int64_t period_rate = delta_value * 1000000000LL / delta_sample_time;

            previous_values[match_index] = value;

            if (!overflow) {
              printf("%12" PRId64 " [%8" PRId64 "/sec %8" PRId64 "/sec]\n", value, system_rate,
                     period_rate);
            } else {
              printf("%12" PRId64 " [%8s     %8" PRId64 "/sec]\n", value, "overflow", period_rate);
            }
          }
        }

        match_index++;
      }  // if (matches(entry.name))
    }    // for (size_t i = 0; i < desc->num_counters(); ++i)
    last_sample_time = sample_time;

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
