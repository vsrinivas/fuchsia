// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/strings/string_printf.h>

#include "garnet/bin/guest/integration_tests/guest_test.h"

static constexpr size_t kNumRetries = 40;
static constexpr zx::duration kStepSleep = zx::msec(500);
static constexpr char kTestUtilsUrl[] =
    "fuchsia-pkg://fuchsia.com/guest_integration_tests_utils";

zx_status_t GuestWaitForShellReady(EnclosedGuest& enclosed_guest) {
  for (size_t i = 0; i != kNumRetries; ++i) {
    std::string response;
    zx_status_t status = enclosed_guest.Execute("echo guest ready", &response);
    if (status != ZX_OK) {
      continue;
    }
    auto ready = response.find("guest ready");
    if (ready == std::string::npos) {
      zx::nanosleep(zx::deadline_after(kStepSleep));
      continue;
    }
    return ZX_OK;
  }
  return ZX_ERR_TIMED_OUT;
}

zx_status_t GuestWaitForAppmgrReady(EnclosedGuest& enclosed_guest) {
  for (size_t i = 0; i != kNumRetries; ++i) {
    std::string ps;
    zx_status_t status = enclosed_guest.Execute("ps", &ps);
    if (status != ZX_OK) {
      return status;
    }
    auto appmgr = ps.find("appmgr");
    if (appmgr == std::string::npos) {
      zx::nanosleep(zx::deadline_after(kStepSleep));
      continue;
    }
    return ZX_OK;
  }
  return ZX_ERR_TIMED_OUT;
}

zx_status_t GuestRun(EnclosedGuest& enclosed_guest, const std::string& cmx,
                     const std::string& args, std::string* result) {
  std::string message =
      fxl::StringPrintf("/pkgfs/packages/run/0/bin/run %s#%s %s", kTestUtilsUrl,
                        cmx.c_str(), args.c_str());
  // Even after checking for pkgfs to start up, the guest might not be ready to
  // accept run commands. We loop here to give it some time and reduce test
  // flakiness.
  for (size_t i = 0; i != kNumRetries; ++i) {
    std::string output;
    zx_status_t status = enclosed_guest.Execute(message, &output);
    if (status != ZX_OK) {
      return status;
    }
    auto not_found = output.find("run: not found");
    if (not_found != std::string::npos) {
      zx::nanosleep(zx::deadline_after(kStepSleep));
      continue;
    } else if (result != nullptr) {
      *result = output;
    }
    return ZX_OK;
  }
  return ZX_ERR_TIMED_OUT;
}
