// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_SOCKET_SOCKET_DRAINER_H_
#define LIB_FSL_SOCKET_SOCKET_DRAINER_H_

#include <async/default.h>
#include <async/wait.h>
#include <zx/socket.h>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/macros.h"

namespace fsl {

class FXL_EXPORT SocketDrainer {
 public:
  class Client {
   public:
    virtual void OnDataAvailable(const void* data, size_t num_bytes) = 0;
    virtual void OnDataComplete() = 0;

   protected:
    virtual ~Client();
  };

  explicit SocketDrainer(Client* client,
                         async_t* async = async_get_default());
  ~SocketDrainer();

  void Start(zx::socket source);

 private:
  async_wait_result_t OnHandleReady(async_t* async, zx_status_t status,
                                    const zx_packet_signal_t* signal);

  Client* client_;
  async_t* async_;
  zx::socket source_;
  async::Wait wait_;
  bool* destruction_sentinel_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketDrainer);
};

}  // namespace fsl

#endif  // LIB_FSL_SOCKET_SOCKET_DRAINER_H_
