// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_CONSOLE_CONSOLE_H_
#define SRC_BRINGUP_BIN_CONSOLE_CONSOLE_H_

#include <fuchsia/hardware/pty/llcpp/fidl.h>
#include <fuchsia/logger/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/zx/eventpair.h>

#include <thread>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "fifo.h"

class Console : public llcpp::fuchsia::logger::LogListenerSafe::Interface,
                public fbl::RefCounted<Console> {
 public:
  // Maximum amount of data that will be written to tx_sink_() per call.
  static constexpr size_t kMaxWriteSize = 256;

  // Function to be invoked in order to receive new data.  It should return
  // a byte at a time and to block until a byte is available.  If it returns an
  // error, the RX loop will terminate.
  using RxSource = fit::function<zx_status_t(uint8_t* byte)>;

  // Function to be invoked in order to transmit data.  If it returns an error,
  // it is assumed that no data from this request was transmitted.
  using TxSink = fit::function<zx_status_t(const uint8_t* buffer, size_t length)>;

  // Do not use, instead use Create()
  Console(zx::eventpair event1, zx::eventpair event2, RxSource rx_source, TxSink tx_sink,
          std::vector<std::string> denied_log_tags);
  ~Console();

  static zx_status_t Create(RxSource rx_source, TxSink tx_sink,
                            std::vector<std::string> denied_log_tags,
                            fbl::RefPtr<Console>* console);

  // Used to implement fuchsia.io.File/{Read,Write}
  zx_status_t Read(void* data, size_t len, size_t* out_actual);
  zx_status_t Write(const void* data, size_t len, size_t* out_actual);

  // Used to implement fuchsia.log.LogListenerSafe/{Log,LogMany}
  zx_status_t Log(llcpp::fuchsia::logger::LogMessage log);

  // Return the event for a connection to this console
  zx_status_t GetEvent(zx::eventpair* event) const;

 private:
  // Thread for getting tty input via zx_debug_read
  void DebugReaderThread();

  // Functions to handle fuchsia.log.LogListenerSafe
  void Log(llcpp::fuchsia::logger::LogMessage log, LogCompleter::Sync completer) override;
  void LogMany(fidl::VectorView<llcpp::fuchsia::logger::LogMessage> logs,
               LogManyCompleter::Sync completer) override;
  void Done(DoneCompleter::Sync completer) override;

  Fifo rx_fifo_;
  zx::eventpair rx_event_;
  RxSource rx_source_;
  TxSink tx_sink_;
  std::vector<std::string> denied_log_tags_;
  std::thread rx_thread_;
};

#endif  // SRC_BRINGUP_BIN_CONSOLE_CONSOLE_H_
