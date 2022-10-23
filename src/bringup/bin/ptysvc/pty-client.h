// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_PTYSVC_PTY_CLIENT_H_
#define SRC_BRINGUP_BIN_PTYSVC_PTY_CLIENT_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <lib/zx/eventpair.h>

#include "fifo.h"
#include "pty-server.h"

class PtyClient : public fidl::WireServer<fuchsia_hardware_pty::Device> {
 public:
  PtyClient(std::shared_ptr<PtyServer>, uint32_t id, zx::eventpair local, zx::eventpair remote);

  zx_status_t Read(void* data, size_t count, size_t* out_actual);
  zx_status_t Write(const void* data, size_t count, size_t* out_actual);

  void AddConnection(fidl::ServerEnd<fuchsia_hardware_pty::Device> request);

  // fuchsia.hardware.pty.Device.
  void Clone2(Clone2RequestView request, Clone2Completer::Sync& completer) final;
  void Close(CloseCompleter::Sync& completer) final;
  void Query(QueryCompleter::Sync& completer) final;
  void Read(ReadRequestView request, ReadCompleter::Sync& completer) final;
  void Write(WriteRequestView request, WriteCompleter::Sync& completer) final;
  void Describe2(Describe2Completer::Sync& completer) final;

  void OpenClient(OpenClientRequestView request, OpenClientCompleter::Sync& completer) final;
  void ClrSetFeature(ClrSetFeatureRequestView request,
                     ClrSetFeatureCompleter::Sync& completer) final;
  void GetWindowSize(GetWindowSizeCompleter::Sync& completer) final;
  void MakeActive(MakeActiveRequestView request, MakeActiveCompleter::Sync& completer) final;
  void ReadEvents(ReadEventsCompleter::Sync& completer) final;
  void SetWindowSize(SetWindowSizeRequestView request,
                     SetWindowSizeCompleter::Sync& completer) final;

  Fifo* rx_fifo() { return &rx_fifo_; }

  void AssertHangupSignal() {
    ClearSetFlags(0, kFlagPeerClosed);
    local_.signal_peer(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kWritable),
                       static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kHangup));
  }
  void AssertActiveHungup() {
    local_.signal_peer(0, static_cast<zx_signals_t>(fuchsia_hardware_pty::wire::kSignalEvent |
                                                    fuchsia_device::wire::DeviceSignal::kHangup));
  }
  void AssertReadableSignal() {
    local_.signal_peer(0, static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable));
  }
  void AssertWritableSignal() {
    local_.signal_peer(0, static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kWritable));
  }
  void AssertEventSignal() {
    static_assert(fuchsia_hardware_pty::wire::kSignalEvent ==
                  fuchsia_device::wire::DeviceSignal::kOob);
    local_.signal_peer(0, static_cast<zx_signals_t>(fuchsia_hardware_pty::wire::kSignalEvent));
  }

  void DeAssertReadableSignal() {
    local_.signal_peer(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable), 0);
  }
  void DeAssertWritableSignal() {
    local_.signal_peer(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kWritable), 0);
  }
  void DeAssertEventSignal() {
    local_.signal_peer(static_cast<zx_signals_t>(fuchsia_hardware_pty::wire::kSignalEvent), 0);
  }

  void ClearSetFlags(uint32_t clr, uint32_t set) { flags_ = (flags_ & ~clr) | set; }
  uint32_t flags() const { return flags_; }
  [[nodiscard]] bool in_raw_mode() const { return flags_ & kFlagRawMode; }
  [[nodiscard]] bool is_peer_closed() const { return flags_ & kFlagPeerClosed; }

  // Ensure READABLE/WRITABLE signals are properly asserted based on active
  // status and rx_fifo status.
  void AdjustSignals();

  [[nodiscard]] bool is_active() { return server().is_active(*this); }
  [[nodiscard]] bool is_control() { return server().is_control(*this); }

  PtyServer& server() { return *server_; }

 private:
  zx_status_t WriteChunk(const void* buf, size_t count, size_t* actual);

  std::shared_ptr<PtyServer> server_;
  const uint32_t id_;

  // remote_ is signaled to indicate to client connections various status
  // conditions.
  zx::eventpair local_, remote_;

  static constexpr uint32_t kFlagRawMode = 0x00000001u;
  static_assert(kFlagRawMode == fuchsia_hardware_pty::wire::kFeatureRaw);
  static constexpr uint32_t kFlagPeerClosed = 0x00000002u;

  uint32_t flags_ = 0;
  Fifo rx_fifo_;

  std::unordered_map<zx_handle_t, fidl::ServerBindingRef<fuchsia_hardware_pty::Device>> bindings_;
};

#endif  // SRC_BRINGUP_BIN_PTYSVC_PTY_CLIENT_H_
