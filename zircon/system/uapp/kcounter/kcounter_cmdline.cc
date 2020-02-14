// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "kcounter_cmdline.h"

#include <getopt.h>
#include <stdlib.h>
#include <string.h>

constexpr char kShortOptions[] = "hltvw::";
constexpr struct option kLongOptions[] = {
    {"help", no_argument, nullptr, 'h'},        {"list", no_argument, nullptr, 'l'},
    {"terse", no_argument, nullptr, 't'},       {"verbose", no_argument, nullptr, 'v'},
    {"watch", optional_argument, nullptr, 'w'}, {nullptr, 0, nullptr, 0}};

constexpr int default_period = 3;

void kcounter_usage(const char* myname, FILE* into) {
  fprintf(into,
          "\
Usage: %s [-hltvw] [--help] [--list] [--terse] [--verbose] [--watch[=period]] [PREFIX...]\n\
Prints one counter per line.\n\
With --help or -h, display this help and exit.\n\
With --list or -l, show names and types rather than values.\n\
With --terse or -t, show only values and no names.\n\
With --verbose or -v, show space-separated lists of per-CPU values.\n\
With --watch or -w, keep showing the values every [period] seconds, default is %d seconds.\n\
Otherwise values are aggregated summaries across all CPUs.\n\
If PREFIX arguments are given, only matching names are shown.\n\
Results are always sorted by name.\n\
",
          myname, default_period);
}

bool kcounter_parse_cmdline(int argc, const char* const argv[], FILE* err,
                            KcounterCmdline* cmdline) {
  memset(cmdline, 0, sizeof(*cmdline));

  optind = 0;
  int opt;
  while ((opt = getopt_long(argc, const_cast<char* const*>(argv), kShortOptions, kLongOptions,
                            nullptr)) != -1) {
    switch (opt) {
      case 'h':
        cmdline->help = true;
        break;
      case 'l':
        cmdline->list = true;
        break;
      case 't':
        cmdline->terse = true;
        break;
      case 'v':
        cmdline->verbose = true;
        break;
      case 'w':
        // default to every 3 seconds.
        cmdline->period = optarg ? atoi(optarg) : default_period;
        if (cmdline->period < 1) {
          fprintf(err, "watch period must be greater than 1\n");
          return false;
        }
        break;
      default:
        return false;
    }
  }

  if (cmdline->list + cmdline->terse + cmdline->verbose > 1) {
    fprintf(err, "%s: --list, --terse, and --verbose are mutually exclusive\n", argv[0]);
    kcounter_usage(argv[0], err);
    return false;
  }

  if (cmdline->list && cmdline->period > 0) {
    fprintf(err, "%s: --list and --watch are mutually exclusive\n", argv[0]);
    kcounter_usage(argv[0], err);
    return false;
  }

  cmdline->unparsed_args_start = optind;
  return true;
}
