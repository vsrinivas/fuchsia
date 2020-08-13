// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/boot/c/fidl.h>
#include <getopt.h>
#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <lib/zx/resource.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <fbl/vector.h>

#include "stress_test.h"

namespace {

zx_status_t get_root_resource(zx::resource* root_resource) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = fdio_service_connect("/svc/fuchsia.boot.RootResource", remote.release());
  if (status != ZX_OK) {
    fprintf(stderr, "ERROR: Cannot open fuchsia.boot.RootResource: %s (%d)\n",
            zx_status_get_string(status), status);
    return ZX_ERR_NOT_FOUND;
  }

  zx_handle_t h;
  zx_status_t fidl_status = fuchsia_boot_RootResourceGet(local.get(), &h);

  if (fidl_status != ZX_OK) {
    fprintf(stderr, "ERROR: Cannot obtain root resource: %s (%d)\n",
            zx_status_get_string(fidl_status), fidl_status);
    return fidl_status;
  }

  root_resource->reset(h);

  return ZX_OK;
}

zx_status_t get_kmem_stats(zx::resource& root_resource, zx_info_kmem_stats_t* kmem_stats) {
  if (!root_resource) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t err = zx_object_get_info(root_resource.get(), ZX_INFO_KMEM_STATS, kmem_stats,
                                       sizeof(*kmem_stats), nullptr, nullptr);
  if (err != ZX_OK) {
    fprintf(stderr, "ZX_INFO_KMEM_STATS returns %d (%s)\n", err, zx_status_get_string(err));
    return err;
  }

  return ZX_OK;
}

void print_help(char** argv, FILE* f) {
  fprintf(f, "Usage: %s [options]\n", argv[0]);
  fprintf(f, "options:\n");
  fprintf(f, "\t-h:                   This help\n");
  fprintf(f, "\t-t [time in seconds]: stop all tests after the time has elapsed\n");
  fprintf(f, "\t-v:                   verbose, status output\n");
}

}  // namespace

int main(int argc, char** argv) {
  zx_status_t status;

  bool verbose = false;
  zx::duration run_duration = zx::duration::infinite();

  int c;
  while ((c = getopt(argc, argv, "ht:v")) > 0) {
    switch (c) {
      case 'h':
        print_help(argv, stdout);
        return 0;
      case 't': {
        long t = atol(optarg);
        if (t <= 0) {
          fprintf(stderr, "bad time argument\n");
          print_help(argv, stderr);
          return 1;
        }
        run_duration = zx::sec(t);
        break;
      }
      case 'v':
        verbose = true;
        break;
      default:
        fprintf(stderr, "Unknown option\n");
        print_help(argv, stderr);
        return 1;
    }
  }

  zx::resource root_resource;
  get_root_resource(&root_resource);

  // read some system stats for each test to use
  zx_info_kmem_stats_t kmem_stats;
  status = get_kmem_stats(root_resource, &kmem_stats);
  if (status != ZX_OK) {
    fprintf(stderr, "error reading kmem stats\n");
    return 1;
  }

  if (run_duration != zx::duration::infinite()) {
    printf("Running stress tests for %" PRIu64 " seconds\n", run_duration.to_secs());
  } else {
    printf("Running stress tests continually\n");
  }

  // initialize all the tests
  for (auto& test : StressTest::tests()) {
    printf("Initializing %s test\n", test->name());
    status = test->Init(verbose, kmem_stats, root_resource.borrow());
    if (status != ZX_OK) {
      fprintf(stderr, "error initializing test\n");
      return 1;
    }
  }

  // start all of them
  for (auto& test : StressTest::tests()) {
    printf("Starting %s test\n", test->name());
    status = test->Start();
    if (status != ZX_OK) {
      fprintf(stderr, "error initializing test\n");
      return 1;
    }
  }

  // set stdin to non blocking
  fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

  zx::time start_time = zx::clock::get_monotonic();
  bool stop = false;
  for (;;) {
    // look for ctrl-c for terminals that do not support it
    char c;
    while (read(STDIN_FILENO, &c, 1) > 0) {
      if (c == 0x3) {
        stop = true;
        break;
      }
    }
    if (stop) {
      break;
    }

    // wait for a second to try again
    zx::nanosleep(zx::deadline_after(zx::sec(1)));

    if (run_duration != zx::duration::infinite()) {
      zx::time now = zx::clock::get_monotonic();
      if (now - start_time >= run_duration) {
        break;
      }
    }
  }

  // shut them down
  for (auto& test : StressTest::tests()) {
    printf("Stopping %s test\n", test->name());
    status = test->Stop();
    if (status != ZX_OK) {
      fprintf(stderr, "error stopping test\n");
      return 1;
    }
  }

  return 0;
}
