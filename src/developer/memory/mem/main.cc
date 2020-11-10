// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/trace-provider/fdio_connect.h>
#include <lib/trace-provider/provider.h>
#include <lib/trace/observer.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include <condition_variable>
#include <iostream>
#include <mutex>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/digest.h"
#include "src/developer/memory/metrics/printer.h"
#include "src/developer/memory/metrics/summary.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

using namespace memory;

int Mem(const fxl::CommandLine& command_line) {
  CaptureState capture_state;
  auto s = Capture::GetCaptureState(&capture_state);
  Printer printer(std::cout);

  if (s != ZX_OK) {
    std::cerr << "Error getting capture state: " << zx_status_get_string(s);
    return EXIT_FAILURE;
  }

  if (command_line.HasOption("print")) {
    Capture capture;
    auto s = Capture::GetCapture(&capture, capture_state, VMO);
    if (s != ZX_OK) {
      std::cerr << "Error getting capture: " << zx_status_get_string(s);
      return EXIT_FAILURE;
    }
    printer.PrintCapture(capture);
    return EXIT_SUCCESS;
  }

  if (command_line.HasOption("output")) {
    zx_koid_t pid = 0;
    std::string pid_value;
    if (command_line.GetOptionValue("pid", &pid_value)) {
      if (!fxl::StringToNumberWithError(pid_value, &pid)) {
        std::cerr << "Invalid value for --pid: " << pid_value;
        return EXIT_FAILURE;
      }
    }
    uint64_t repeat = 0;
    std::string repeat_value;
    if (command_line.GetOptionValue("repeat", &repeat_value)) {
      if (!fxl::StringToNumberWithError(repeat_value, &repeat)) {
        std::cerr << "Invalid value for --repeat: " << repeat_value;
        return EXIT_FAILURE;
      }
    }
    zx::duration repeat_secs = zx::sec(repeat);
    zx::time start = zx::clock::get_monotonic();
    Namer namer(Summary::kNameMatches);
    for (int64_t i = 1; true; i++) {
      Capture capture;
      auto s = Capture::GetCapture(&capture, capture_state, VMO);
      if (s != ZX_OK) {
        std::cerr << "Error getting capture: " << zx_status_get_string(s);
        return EXIT_FAILURE;
      }
      if (command_line.HasOption("digest")) {
        Digester digester;
        Digest d(capture, &digester);
        printer.OutputDigest(d);
      } else {
        printer.OutputSummary(Summary(capture, &namer), UNSORTED, pid);
      }

      if (repeat == 0) {
        break;
      }

      zx::time next = start + (repeat_secs * i);
      if (next <= zx::clock::get_monotonic()) {
        next = zx::clock::get_monotonic() + repeat_secs;
      }
      zx::nanosleep(next);
    }

    return EXIT_SUCCESS;
  }

  Capture capture;
  s = Capture::GetCapture(&capture, capture_state, VMO);
  if (s != ZX_OK) {
    std::cerr << "Error getting capture: " << zx_status_get_string(s);
    return EXIT_FAILURE;
  }
  if (command_line.HasOption("digest")) {
    Digester digester;
    Digest digest(capture, &digester);
    printer.PrintDigest(digest);
    if (command_line.HasOption("undigested")) {
      std::cout << capture.koid_to_vmo().size() << " VMOs, " << digest.undigested_vmos().size()
                << " Undigested\n";
      Namer namer(Summary::kNameMatches);
      Summary undigested_summary(capture, &namer, digest.undigested_vmos());
      std::cout << undigested_summary.process_summaries().size() << " Process summaries\n";
      printer.PrintSummary(undigested_summary, memory::VMO, SORTED);
    }
    return EXIT_SUCCESS;
  }
  printer.PrintSummary(Summary(capture, Summary::kNameMatches), VMO, SORTED);
  return EXIT_SUCCESS;
}

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  if (command_line.HasOption("help")) {
    std::cout << "Usage: mem [options]\n"
                 "  Prints system-wide task and memory\n\n"
                 "  Memory numbers are triplets P,S,T\n"
                 "  P: Private bytes\n"
                 "  S: Total bytes scaled by 1/# processes sharing each byte\n"
                 "  T: Total bytes\n"
                 "     S and T are inclusive of P\n\n"
                 " Options:\n"
                 " [default] Human readable representation of process and vmo groups\n"
                 " --trace   Enable tracing support\n"
                 " --print   Machine readable representation of proccess and vmos\n"
                 " --output  CSV of process memory\n"
                 "           --repeat=N Runs forever, outputing every N seconds\n"
                 "           --pid=N    Output vmo groups of process pid instead\n";
    return EXIT_SUCCESS;
  }

  if (command_line.HasOption("trace")) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    loop.StartThread("provider loop");
    trace::TraceProviderWithFdio trace_provider(loop.dispatcher(), "mem");
    trace::TraceObserver trace_observer;
    std::mutex m;
    std::condition_variable cv;
    bool got_start = false;
    trace_observer.Start(loop.dispatcher(), [&m, &cv, &got_start] {
      std::unique_lock<std::mutex> lock(m);
      got_start = true;
      cv.notify_all();
    });
    std::unique_lock<std::mutex> l(m);
    cv.wait(l, [&got_start] { return got_start; });
    return Mem(command_line);
  }
  return Mem(command_line);
}
