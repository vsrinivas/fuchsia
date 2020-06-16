// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_PTYSVC_PTY_SERVER_H_
#define ZIRCON_SYSTEM_CORE_PTYSVC_PTY_SERVER_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <lib/zx/eventpair.h>

#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fs/vfs.h>

#include "fifo.h"

// forward-decl
class PtyClient;

// Window size dimensions, in characters.
struct WindowSize {
  uint32_t width = 0;
  uint32_t height = 0;
};

class PtyServer : public fbl::RefCounted<PtyServer> {
 public:
  // This ctor is only public for the use of fbl::MakeRefCounted.  Use Create()
  // instead.
  PtyServer(zx::eventpair local, zx::eventpair remote, fs::Vfs* vfs);

  ~PtyServer();

  // Create a new PtyServer.  This method is preferred to the ctor.
  static zx_status_t Create(fbl::RefPtr<PtyServer>* out, fs::Vfs* vfs);

  zx_status_t Read(void* data, size_t len, size_t* out_actual);
  zx_status_t Write(const void* data, size_t len, size_t* out_actual);
  zx_status_t GetEvent(zx::eventpair* event) { return remote_.duplicate(ZX_RIGHTS_BASIC, event); }

  // Create a new PtyClient
  zx_status_t CreateClient(uint32_t id, zx::channel client_request);
  // Remove a PtyClient
  void RemoveClient(PtyClient* client);

  // Called when data is written by the active client
  zx_status_t Recv(const void* data, size_t len, size_t* actual, bool* is_full);

  // Called when data is being sent to the active client
  zx_status_t Send(const void* data, size_t len, size_t* actual);

  // Called when all connections to the PtyServerVnode are closed, to inform pty
  // clients that the pty server is gone.
  void Shutdown();

  void AssertWritableSignal() {
    local_.signal_peer(0, ::llcpp::fuchsia::device::DEVICE_SIGNAL_WRITABLE);
  }

  // Read all outstanding events, and reset the pending signal.
  uint32_t DrainEvents();

  // Called to make the given client active.  Any existing active client is
  // deactivated.  Returns ZX_ERR_NOT_FOUND if the requested id does not exist.
  zx_status_t MakeActive(uint32_t id);

  WindowSize window_size() { return size_; }
  void set_window_size(WindowSize size) { size_ = size; }

  [[nodiscard]] bool is_active(const PtyClient* client) const { return active_.get() == client; }

  [[nodiscard]] bool is_control(const PtyClient* client) const { return control_.get() == client; }

 private:
  // Called to make the given client active.  Any existing active client is
  // deactivated.
  void MakeActive(fbl::RefPtr<PtyClient> client);

  // remote_ is signaled to indicate to server connections various status
  // conditions.
  zx::eventpair local_, remote_;

  fs::Vfs* vfs_;

  // Data waiting to be read by a connection to the server.
  Fifo rx_fifo_;

  // Pending out-of-band events
  uint32_t events_ = 0;

  // List of all clients
  fbl::DoublyLinkedList<fbl::RefPtr<PtyClient>> clients_;

  // Active client
  fbl::RefPtr<PtyClient> active_;

  // The control client is the client that receives events
  fbl::RefPtr<PtyClient> control_;

  // The dimensions (in characters) of the window
  WindowSize size_ = {};
};

#endif  // ZIRCON_SYSTEM_CORE_PTYSVC_PTY_SERVER_H_
