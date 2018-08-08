// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include <zircon/syscalls.h>

enum {
  DUMP_KERNEL_NAME = 1U << 1,
  DUMP_NODENAME = 1U << 2,
  DUMP_KERNEL_RELEASE = 1U << 3,
  DUMP_KERNEL_VERSION = 1U << 4,
  DUMP_MACHINE = 1U << 5,
  DUMP_OPERATING_SYSTEM = 1U << 6
};

static void usage(const char *exe_name) {
  fprintf(stderr, "Usage: %s <options>...\n", exe_name);
  fprintf(stderr, "Print system information\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -a (--all)                Equivalent to -mnrsv\n");
  fprintf(stderr, "  -s (--kernel-name)        Print the kernel name\n");
  fprintf(stderr, "  -n (--nodename)           Print the network hostname\n");
  fprintf(stderr, "  -r (--kernel-release)     Print the kernel release\n");
  fprintf(stderr, "  -v (--kernel-version)     Print the kernel version\n");
  fprintf(stderr, "  -m (--machine)            Print the machine type\n");
  fprintf(stderr, "  -o (--operating-system)   Print the operating system\n");
  fprintf(stderr, "  --help                    Print this message and exit\n");
}

/* Prints a single value out to stdout, without a trailing newline. */
static void print_string(const char *text) {
  static bool first_text = true;

  if (!first_text) {
    putchar(' ');
  } else {
    first_text = false;
  }

  fputs(text, stdout);
}

int main(int argc, char *const argv[]) {
  int selected_options = (argc == 1) ? DUMP_KERNEL_NAME : 0;

  static struct option long_options[] = {
      {"all", no_argument, 0, 'a'},
      {"kernel-name", no_argument, 0, 's'},
      {"nodename", no_argument, 0, 'n'},
      {"kernel-release", no_argument, 0, 'r'},
      {"kernel-version", no_argument, 0, 'v'},
      {"machine", no_argument, 0, 'm'},
      {"operating-system", no_argument, 0, 'o'},
      {"help", no_argument, 0, 'h'},
  };

  bool opts_done = false;

  while (!opts_done) {
    switch (getopt_long(argc, argv, "asnrvmpioh", long_options, NULL)) {
      case 'a':
        /* -a is equivalent to -mnrsv */
        selected_options |= DUMP_MACHINE | DUMP_NODENAME | DUMP_KERNEL_RELEASE |
                            DUMP_KERNEL_NAME | DUMP_KERNEL_VERSION;
        break;
      case 's':
        selected_options |= DUMP_KERNEL_NAME;
        break;
      case 'n':
        selected_options |= DUMP_NODENAME;
        break;
      case 'r':
        selected_options |= DUMP_KERNEL_RELEASE;
        break;
      case 'v':
        selected_options |= DUMP_KERNEL_VERSION;
        break;
      case 'm':
        selected_options |= DUMP_MACHINE;
        break;
      case 'o':
        selected_options |= DUMP_OPERATING_SYSTEM;
        break;
      case -1:
        opts_done = true;
        break;
      case '?':
        fprintf(stderr,
                "Unrecognized option '%c'. Use --help for list of options\n",
                optopt);
        return 1;
      case 'h':
        usage(argv[0]);
        return 0;
    }
  }

  if (selected_options == 0) {
    return 0;
  }

  /* Kernel name */
  if (selected_options & DUMP_KERNEL_NAME) {
    print_string("Zircon");
  }

  /* Network name */
  if (selected_options & DUMP_NODENAME) {
    char hostname[HOST_NAME_MAX + 1];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
      hostname[sizeof(hostname) - 1] = '\0';
      print_string(hostname);
    } else {
      print_string("unknown");
    }
  }

  /* Kernel release */
  if (selected_options & DUMP_KERNEL_RELEASE) {
    print_string("prerelease");
  }

  /* Kernel version */
  if (selected_options & DUMP_KERNEL_VERSION) {
    char kernel_version[256];
    if (zx_system_get_version(kernel_version, sizeof(kernel_version)) ==
        ZX_OK) {
      print_string(kernel_version);
    } else {
      print_string("unknown");
    }
  }

  /* Machine type */
  if (selected_options & DUMP_MACHINE) {
#if defined(__x86_64__)
    print_string("x86_64");
#elif defined(__aarch64__)
    print_string("aarch64");
#else
    print_string("unknown");
#endif
  }

  /* Operating system */
  if (selected_options & DUMP_OPERATING_SYSTEM) {
    print_string("Fuchsia");
  }

  putchar('\n');
  return 0;
}
