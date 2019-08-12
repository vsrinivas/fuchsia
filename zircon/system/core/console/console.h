// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/hardware/pty/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>

#include <thread>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "fifo.h"

class Console : public fbl::RefCounted<Console> {
 public:
  // Function to be invoked in order to receive new data.  It should return
  // a byte at a time and to block until a byte is available.  If it returns an
  // error, the RX loop will terminate.
  using RxSource = fit::function<zx_status_t(uint8_t* byte)>;

  // Function to be invoked in order to transmit data.  If it returns an error,
  // it is assumed that no data from this request was transmitted.
  using TxSink = fit::function<zx_status_t(const uint8_t* buffer, size_t length)>;

  // Do not use, instead use Create()
  Console(async_dispatcher_t* dispatcher, zx::eventpair event1, zx::eventpair event2,
          RxSource rx_source, TxSink tx_sink);
  ~Console();

  static zx_status_t Create(async_dispatcher_t* dispatcher, RxSource rx_source, TxSink tx_sink,
                            fbl::RefPtr<Console>* console);

  // Used to implement fuchsia.io.File/{Read,Write}
  zx_status_t Read(void* data, size_t len, size_t offset, size_t* out_actual);
  zx_status_t Write(const void* data, size_t len, size_t offset, size_t* out_actual);

  async_dispatcher_t* dispatcher() { return dispatcher_; }

  // Return the NodeInfo for a connection to this console
  zx_status_t GetNodeInfo(::llcpp::fuchsia::io::NodeInfo* info) const;

 private:
  // Maximum amount of data that will be written to tx_sink_() per call.
  static inline constexpr size_t kMaxWriteSize = 256;

  // Thread for getting tty input via zx_debug_read
  void DebugReaderThread();

  async_dispatcher_t* dispatcher_;
  Fifo rx_fifo_;
  zx::eventpair rx_event_;
  RxSource rx_source_;
  TxSink tx_sink_;
  std::thread rx_thread_;
};
