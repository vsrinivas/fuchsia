// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <thread>

#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/strings/string_number_conversions.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/types.h>

#include "test_helper.h"
#include "util.h"

std::atomic_int g_num_threads_running = 0;

static void WaitPeerClosed(const zx::channel& channel) {
  // Wait for the test to close the channel.
  zx_signals_t pending;
  zx_status_t status =
      channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), &pending);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Test helper: wait peer closed failed: "
                   << debugger_utils::ZxErrorString(status);
    exit(EXIT_FAILURE);
  }
}

static void WriteUint64Packet(const zx::channel& channel, uint64_t value) {
  uint64_t packet = value;
  FXL_CHECK(channel.write(0, &packet, sizeof(packet), nullptr, 0) == ZX_OK);
}

static void StartNThreadsThreadFunc(zx_handle_t eventpair, int num_threads) {
  zx_status_t status;

  // When all threads are running notify the main loop.
  if (g_num_threads_running.fetch_add(1) == num_threads - 1) {
    FXL_LOG(INFO) << "All threads started";
    status = zx_object_signal_peer(eventpair, 0, ZX_USER_SIGNAL_0);
    FXL_CHECK(status == ZX_OK);
  }

  // The main thread will close its side of |eventpair| when it's done.
  zx_signals_t pending;
  status = zx_object_wait_one(eventpair, ZX_EVENTPAIR_PEER_CLOSED,
                              ZX_TIME_INFINITE, &pending);
  FXL_CHECK(status == ZX_OK);
}

static int StartNThreads(zx::channel channel, int num_threads) {
  std::vector<std::thread> threads;

  // When our side of the event pair is closed the threads will exit.
  zx::eventpair our_event, their_event;
  FXL_CHECK(zx::eventpair::create(0, &our_event, &their_event) == ZX_OK);

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back(std::thread{
        StartNThreadsThreadFunc, their_event.get(), num_threads});
  }

  // Wait for all threads to start.
  zx_signals_t pending;
  zx_status_t status =
      our_event.wait_one(ZX_USER_SIGNAL_0, zx::time::infinite(), &pending);
  FXL_CHECK(status == ZX_OK);

  // Notify test all threads are running.
  WriteUint64Packet(channel, debugger_utils::kUint64MagicPacketValue);

  WaitPeerClosed(channel);

  // Terminate the threads;
  our_event.reset();
  for (auto& thread : threads) {
    thread.join();
  }

  return EXIT_SUCCESS;
}

static int PerformWaitPeerClosed(zx::channel channel) {
  zx_handle_t thread = zx_thread_self();
  zx_status_t status = channel.write(0, nullptr, 0, &thread, 1);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Test helper: channel write failed: "
                   << debugger_utils::ZxErrorString(status);
    return EXIT_FAILURE;
  }

  WaitPeerClosed(channel);

  return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl)) {
    return EXIT_FAILURE;
  }

  const std::vector<std::string>& args = cl.positional_args();

  if (args.empty()) {
    FXL_LOG(ERROR) << "Missing command";
    return EXIT_FAILURE;
  }

  const std::string& cmd = args[0];
  FXL_LOG(INFO) << argv[0] << ": Command " << cmd;

  if (cmd == "hello") {
    FXL_LOG(INFO) << "Hello.";
    return EXIT_SUCCESS;
  }

  zx::channel channel{zx_take_startup_handle(PA_HND(PA_USER0, 0))};
  // If no channel was passed we're running standalone.
  if (!channel.is_valid()) {
    FXL_LOG(WARNING) << "Test helper: channel not received";
  }

  if (cmd == "wait-peer-closed") {
    return PerformWaitPeerClosed(std::move(channel));
  } else if (cmd == "start-n-threads") {
    if (args.size() < 2) {
      FXL_LOG(ERROR) << "Missing iteration count";
      return EXIT_FAILURE;
    }
    int num_threads = 0;
    if (!fxl::StringToNumberWithError(args[1], &num_threads) ||
        num_threads < 1) {
      FXL_LOG(ERROR) << "Error parsing number of threads";
      return EXIT_FAILURE;
    }
    return StartNThreads(std::move(channel), num_threads);
  }

  FXL_LOG(ERROR) << "Unknown helper command";
  return EXIT_FAILURE;
}
