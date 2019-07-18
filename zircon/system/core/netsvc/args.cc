// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "args.h"

#include <stdlib.h>

#include <cstring>

int parse_common_args(int argc, char** argv, const char** error, const char** interface) {
  while (argc > 1) {
    if (!strncmp(argv[1], "--interface", 11)) {
      if (argc < 3) {
        *error = "netsvc: missing argument to --interface";
        return -1;
      }
      *interface = argv[2];
      argv++;
      argc--;
    }
    argv++;
    argc--;
  }
  return 0;
}

int parse_netsvc_args(int argc, char** argv, const char** error, bool* netboot, bool* advertise,
                      const char** interface) {
  int err = parse_common_args(argc, argv, error, interface);
  if (err) {
    return err;
  }
  while (argc > 1) {
    if (!strncmp(argv[1], "--netboot", 9)) {
      *netboot = true;
    } else if (!strncmp(argv[1], "--advertise", 11)) {
      *advertise = true;
    }
    argv++;
    argc--;
  }
  return 0;
}

int parse_device_name_provider_args(int argc, char** argv, const char** error,
                                    const char** interface, const char** nodename,
                                    const char** ethdir) {
  int err = parse_common_args(argc, argv, error, interface);
  if (err) {
    return err;
  }

  while (argc > 1) {
    if (!strncmp(argv[1], "--nodename", 10)) {
      if (argc < 3) {
        *error = "netsvc: missing argument to --nodename";
        return -1;
      }
      *nodename = argv[2];
      argv++;
      argc--;
    }
    if (!strncmp(argv[1], "--ethdir", 12)) {
      if (argc < 3) {
        *error = "netsvc: missing argument to --ethdir";
        return -1;
      }
      *ethdir = argv[2];
      argv++;
      argc--;
    }
    argv++;
    argc--;
  }
  return 0;
}
