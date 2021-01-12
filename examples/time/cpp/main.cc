// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START common_imports]
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <stdio.h>
// [END common_imports]
// [START utc_imports]
#include <zircon/utc.h>
// [END utc_imports]

// [START monotonic]
void monotonic_examples() {
  // Read monotonic time.
  zx::time monotonic_time = zx::clock::get_monotonic();
  printf("The monotonic time is %ld ns.\n", monotonic_time.get());
}
// [END monotonic]

// [START utc]
void utc_examples() {
  // This is a borrowed handle. Do not close it, and do not replace it using
  // zx_utc_reference_swap while using it.
  zx_handle_t utc_clock_handle = zx_utc_reference_get();
  zx::unowned_clock utc_clock(utc_clock_handle);

  // Wait for the UTC clock to start.
  zx_status_t status = utc_clock->wait_one(ZX_CLOCK_STARTED, zx::time::infinite(), NULL);
  if (status == ZX_OK) {
    printf("UTC clock is started.\n");
  } else {
    printf("Waiting for the UTC clock to start failed (status = %d).\n", status);
  }

  // Read the UTC clock.
  zx_time_t utc_time;
  status = utc_clock->read(&utc_time);
  if (status == ZX_OK) {
    printf("The UTC time is %ld ns since the epoch\n", utc_time);
  } else {
    printf("Reading the UTC clock failed (status = %d).\n", status);
  }

  // Read clock details.
  zx_clock_details_v1_t details;
  status = utc_clock->get_details(&details);
  if (status == ZX_OK) {
    printf("The UTC clock's backstop time is %ld ns since the epoch.\n", details.backstop_time);
  } else {
    printf("Reading the UTC clock details failed (status = %d).\n", status);
  }
}
// [END utc]

int main(int argc, const char** argv) {
  monotonic_examples();
  utc_examples();
}
