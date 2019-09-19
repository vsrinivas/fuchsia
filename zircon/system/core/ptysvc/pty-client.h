// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_PTYSVC_PTY_CLIENT_H_
#define ZIRCON_SYSTEM_CORE_PTYSVC_PTY_CLIENT_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/pty/llcpp/fidl.h>
#include <lib/zx/eventpair.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "fifo.h"
#include "pty-server.h"

class PtyClient : public fbl::RefCounted<PtyClient>,
                  public fbl::DoublyLinkedListable<fbl::RefPtr<PtyClient>> {
 public:
  // This ctor is only public for the use of fbl::MakeRefCounted.  Use Create()
  // instead.
  PtyClient(fbl::RefPtr<PtyServer> server, uint32_t id, zx::eventpair local, zx::eventpair remote);

  ~PtyClient();

  // Create a new PtyClient.  This method is preferred to the ctor.
  static zx_status_t Create(fbl::RefPtr<PtyServer> server, uint32_t id,
                            fbl::RefPtr<PtyClient>* out);

  zx_status_t Read(void* data, size_t len, size_t* out_actual);
  zx_status_t Write(const void* data, size_t len, size_t* out_actual);
  zx_status_t GetEvent(zx::eventpair* event) { return remote_.duplicate(ZX_RIGHTS_BASIC, event); }

  Fifo* rx_fifo() { return &rx_fifo_; }

  void AssertHangupSignal() {
    ClearSetFlags(0, kFlagPeerClosed);
    local_.signal_peer(::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE,
                       ::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP);
  }
  void AssertActiveHungup() {
    local_.signal_peer(0, ::llcpp::fuchsia::hardware::pty::SIGNAL_EVENT |
                              ::llcpp::fuchsia::device::DEVICE_SIGNAL_HANGUP);
  }

  void AssertReadableSignal() {
    local_.signal_peer(0, ::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE);
  }
  void AssertWritableSignal() {
    local_.signal_peer(0, ::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE);
  }
  void AssertEventSignal() {
    static_assert(::llcpp::fuchsia::hardware::pty::SIGNAL_EVENT ==
                  ::llcpp::fuchsia::device::DEVICE_SIGNAL_OOB);
    local_.signal_peer(0, ::llcpp::fuchsia::hardware::pty::SIGNAL_EVENT);
  }

  void DeAssertReadableSignal() {
    local_.signal_peer(::llcpp::fuchsia::device::DEVICE_SIGNAL_READABLE, 0);
  }
  void DeAssertWritableSignal() {
    local_.signal_peer(::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE, 0);
  }
  void DeAssertEventSignal() {
    local_.signal_peer(::llcpp::fuchsia::hardware::pty::SIGNAL_EVENT, 0);
  }

  void ClearSetFlags(uint32_t clr, uint32_t set) { flags_ = (flags_ & ~clr) | set; }
  uint32_t flags() const { return flags_; }
  [[nodiscard]] bool in_raw_mode() const { return flags_ & kFlagRawMode; }
  [[nodiscard]] bool is_peer_closed() const { return flags_ & kFlagPeerClosed; }

  uint32_t id() const { return id_; }

  // Ensure READABLE/WRITABLE signals are properly asserted based on active
  // status and rx_fifo status.
  void AdjustSignals();

  [[nodiscard]] bool is_active() { return server_->is_active(this); }
  [[nodiscard]] bool is_control() { return server_->is_control(this); }

  const fbl::RefPtr<PtyServer>& server() const { return server_; }

  // Teardown this client
  void Shutdown();

 private:
  zx_status_t WriteChunk(const void* buf, size_t count, size_t* actual);

  const fbl::RefPtr<PtyServer> server_;
  const uint32_t id_;

  // remote_ is signaled to indicate to client connections various status
  // conditions.
  zx::eventpair local_, remote_;

  static constexpr uint32_t kFlagRawMode = 0x00000001u;
  static_assert(kFlagRawMode == ::llcpp::fuchsia::hardware::pty::FEATURE_RAW);
  static constexpr uint32_t kFlagPeerClosed = 0x00000002u;

  uint32_t flags_ = 0;
  Fifo rx_fifo_;
};

#endif  // ZIRCON_SYSTEM_CORE_PTYSVC_PTY_CLIENT_H_
