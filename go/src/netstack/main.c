// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>

#include "apps/netstack/dispatcher.h"
#include "apps/netstack/multiplexer.h"
#include "apps/netstack/net_init.h"
#include "apps/netstack/request_queue.h"
#include "apps/netstack/trace.h"

int main(int argc, char** argv) {
  int trace_facil = TRACE_FACIL_ALL;
  int trace_level = TRACE_LEVEL_ERROR;

  int opt;
  while ((opt = getopt(argc, argv, "f:l:")) != -1) {
    switch (opt) {
      case 'f':
        trace_facil = atoi(optarg);
        printf("trace_facil set to %d\n", trace_facil);
        break;
      case 'l':
        trace_level = atoi(optarg);
        printf("trace_level set to %d\n", trace_level);
        break;
      default:
        fprintf(stderr, "usage: %s [-f trace_facil] [-l trace_level]\n",
                argv[0]);
        return -1;
    }
  }

  trace_init();
  set_trace_level(trace_facil, trace_level);

  if (net_init() < 0) {
    return -1;
  }
  if (shared_queue_create() < 0) {
    return -1;
  }

  thrd_t multiplexer_thread;
  if (thrd_create(&multiplexer_thread, multiplexer, NULL) < 0) {
    error("thrd_create failed\n");
    return -1;
  }

  dispatcher();

  thrd_join(multiplexer_thread, NULL);
  return 0;
}
