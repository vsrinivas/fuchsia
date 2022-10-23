// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_CONSOLE_CONSOLE_H_
#define SRC_BRINGUP_BIN_CONSOLE_CONSOLE_H_

#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <fidl/fuchsia.logger/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/zx/eventpair.h>

#include <thread>
#include <vector>

#include "fifo.h"

class Console : public fidl::WireServer<fuchsia_logger::LogListenerSafe>,
                public fidl::WireServer<fuchsia_hardware_pty::Device> {
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

  Console(async_dispatcher_t* dispatcher, zx::eventpair event1, zx::eventpair event2,
          RxSource rx_source, TxSink tx_sink, std::vector<std::string> denied_log_tags);
  ~Console() override;

  // Used to implement fuchsia.log.LogListenerSafe/{Log,LogMany}. Exposed for testing.
  zx_status_t Log(fuchsia_logger::wire::LogMessage log);

 private:
  // Thread for getting tty input via zx_debug_read
  void DebugReaderThread();

  // Functions to handle fuchsia.log.LogListenerSafe
  void Log(LogRequestView request, LogCompleter::Sync& completer) override;
  void LogMany(LogManyRequestView request, LogManyCompleter::Sync& completer) override;
  void Done(DoneCompleter::Sync& completer) override;

  // Functions to handle fuchsia.hardware.pty.Device
  void Clone2(Clone2RequestView request, Clone2Completer::Sync& completer) final;
  void Close(CloseCompleter::Sync& completer) final;
  void Query(QueryCompleter::Sync& completer) final;
  void Read(ReadRequestView request, ReadCompleter::Sync& completer) final;
  void Write(WriteRequestView request, WriteCompleter::Sync& completer) final;
  void Clone(CloneRequestView request, CloneCompleter::Sync& completer) final;
  void Describe2(Describe2Completer::Sync& completer) final;

  void OpenClient(OpenClientRequestView request, OpenClientCompleter::Sync& completer) final;
  void ClrSetFeature(ClrSetFeatureRequestView request,
                     ClrSetFeatureCompleter::Sync& completer) final;
  void GetWindowSize(GetWindowSizeCompleter::Sync& completer) final;
  void MakeActive(MakeActiveRequestView request, MakeActiveCompleter::Sync& completer) final;
  void ReadEvents(ReadEventsCompleter::Sync& completer) final;
  void SetWindowSize(SetWindowSizeRequestView request,
                     SetWindowSizeCompleter::Sync& completer) final;

  async_dispatcher_t* dispatcher_;
  Fifo rx_fifo_;
  zx::eventpair rx_event_;
  RxSource rx_source_;
  TxSink tx_sink_;
  std::vector<std::string> denied_log_tags_;
  std::thread rx_thread_;
};

#endif  // SRC_BRINGUP_BIN_CONSOLE_CONSOLE_H_
