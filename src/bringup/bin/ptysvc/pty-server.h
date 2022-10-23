// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_PTYSVC_PTY_SERVER_H_
#define SRC_BRINGUP_BIN_PTYSVC_PTY_SERVER_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <lib/zx/eventpair.h>

#include "fifo.h"

// forward-decl
class PtyClient;

class PtyServer : public std::enable_shared_from_this<PtyServer>,
                  public fidl::WireServer<fuchsia_hardware_pty::Device> {
 public:
  class Args {
   public:
    static zx::result<Args> Create();

   private:
    friend class PtyServer;

    zx::eventpair local_, remote_;
  };

  PtyServer(Args args, async_dispatcher_t* dispatcher);
  ~PtyServer() override;

  async_dispatcher_t* dispatcher() { return dispatcher_; }

  zx_status_t Read(void* data, size_t count, size_t* out_actual);
  zx_status_t Write(const void* data, size_t count, size_t* out_actual);

  void AddConnection(fidl::ServerEnd<fuchsia_hardware_pty::Device> request);

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

  // Create a new PtyClient
  zx_status_t CreateClient(uint32_t id,
                           fidl::ServerEnd<fuchsia_hardware_pty::Device> client_request);

  // Remove a PtyClient
  void RemoveClient(uint32_t id);

  // Called when data is written by the active client
  zx_status_t Recv(const void* data, size_t len, size_t* actual, bool* is_full);

  // Called when data is being sent to the active client
  zx_status_t Send(const void* data, size_t len, size_t* actual);

  void AssertWritableSignal() {
    local_.signal_peer(0, static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kWritable));
  }

  // Read all outstanding events, and reset the pending signal.
  uint32_t DrainEvents();

  // Called to make the given client active.  Any existing active client is
  // deactivated.  Returns ZX_ERR_NOT_FOUND if the requested id does not exist.
  zx_status_t MakeActive(uint32_t id);

  fuchsia_hardware_pty::wire::WindowSize window_size() { return size_; }

  [[nodiscard]] bool is_active(const PtyClient& client) const {
    return active_.has_value() && &active_.value().get() == &client;
  }

  [[nodiscard]] bool is_control(const PtyClient& client) const {
    return control_.has_value() && &control_.value().get() == &client;
  }

 private:
  // Called to make the given client active.  Any existing active client is
  // deactivated.
  void MakeActive(PtyClient& client);

  // remote_ is signaled to indicate to server connections various status
  // conditions.
  zx::eventpair local_, remote_;

  async_dispatcher_t* dispatcher_;

  // Data waiting to be read by a connection to the server.
  Fifo rx_fifo_;

  // Pending out-of-band events
  uint32_t events_ = 0;

  // List of all clients
  std::unordered_map<uint32_t, PtyClient> clients_;

  // Active client
  std::optional<std::reference_wrapper<PtyClient>> active_;

  // The control client is the client that receives events
  std::optional<std::reference_wrapper<PtyClient>> control_;

  // The dimensions (in characters) of the window
  fuchsia_hardware_pty::wire::WindowSize size_ = {};

  std::unordered_map<zx_handle_t, fidl::ServerBindingRef<fuchsia_hardware_pty::Device>> bindings_;
};

#endif  // SRC_BRINGUP_BIN_PTYSVC_PTY_SERVER_H_
