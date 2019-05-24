// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream> 
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/strings/string_number_conversions.h>
#include <zircon/status.h>

#include "garnet/bin/memory_monitor/capture.h"
#include "garnet/bin/memory_monitor/summary.h"
#include "garnet/bin/memory_monitor/printer.h"

using namespace memory;

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  CaptureState capture_state;
  auto s = Capture::GetCaptureState(capture_state);
  Printer printer(std::cout);

  if (s != ZX_OK) {
    std::cerr << "Error getting capture state: "
              << zx_status_get_string(s);
    return EXIT_FAILURE;
  }
  if (command_line.HasOption("summarize")) {
    Capture capture;
    s = Capture::GetCapture(capture, capture_state, VMO);
    if (s != ZX_OK) {
      std::cerr << "Error getting capture: " << zx_status_get_string(s);
      return EXIT_FAILURE;
    }
    Summary summary(capture);
    printer.PrintSummary(summary, VMO, SORTED);
    return EXIT_SUCCESS;
  }

  if (command_line.HasOption("print")) {
    Capture capture;
    auto s = Capture::GetCapture(capture, capture_state, VMO);
    if (s != ZX_OK) {
      std::cerr << "Error getting capture: " << zx_status_get_string(s);
      return EXIT_FAILURE;
    }
    printer.PrintCapture(capture, VMO, SORTED);
    return EXIT_SUCCESS;
  }

  if (command_line.HasOption("output")) {
    zx_koid_t pid = 0;
    if (command_line.HasOption("pid")) {
      std::string pid_value;
      command_line.GetOptionValue("pid", &pid_value);
      if (!fxl::StringToNumberWithError(pid_value, &pid)) {
        std::cerr << "Invalid value for --pid: " << pid_value;
        return EXIT_FAILURE;
      }
    }
    if (!command_line.HasOption("repeat")) {
      Capture capture;
      auto s = Capture::GetCapture(capture, capture_state, VMO);
      if (s != ZX_OK) {
        std::cerr << "Error getting capture: " << zx_status_get_string(s);
        return EXIT_FAILURE;
      }
      Summary summary(capture);
      printer.OutputSummary(summary, UNSORTED, pid);
      return EXIT_SUCCESS;
    }
    zx::time start = zx::clock::get_monotonic();
    uint64_t repeat = 0;
    std::string repeat_value;
    command_line.GetOptionValue("repeat", &repeat_value);
    if (!fxl::StringToNumberWithError(repeat_value, &repeat)) {
      std::cerr << "Invalid value for --repeat: " << repeat_value;
      return EXIT_FAILURE;
    }
    zx::duration repeat_secs = zx::sec(repeat);
    int64_t i = 1;
    do {
      Capture capture;
      auto s = Capture::GetCapture(capture, capture_state, VMO);
      if (s != ZX_OK) {
        std::cerr << "Error getting capture: " << zx_status_get_string(s);
        return EXIT_FAILURE;
      }
      Summary summary(capture);
      printer.OutputSummary(summary, UNSORTED, pid);
      zx::time next = start + (repeat_secs * i);
      if (next <= zx::clock::get_monotonic()) {
        next = zx::clock::get_monotonic() + repeat_secs;
      }
      zx::nanosleep(next);
      i++;
    } while (true);
    return EXIT_SUCCESS;
  }

  return EXIT_SUCCESS;
}
