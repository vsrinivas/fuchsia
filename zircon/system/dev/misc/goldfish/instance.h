// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_INSTANCE_H_
#define ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_INSTANCE_H_

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/goldfish/pipe.h>
#include <lib/async-loop/cpp/loop.h>
#include <threads.h>
#include <zircon/types.h>

#include <map>
#include <memory>

namespace goldfish {

class Pipe;
class Instance;
using InstanceType = ddk::Device<Instance, ddk::Messageable, ddk::Closable>;

// This class implements a pipe instance device. By opening the pipe device,
// an instance of this class will be created to service a new channel
// to the virtual device.
class Instance : public InstanceType {
 public:
  explicit Instance(zx_device_t* parent);
  ~Instance();

  zx_status_t Bind();

  // FIDL interface
  zx_status_t FidlOpenPipe(zx_handle_t pipe_request_handle);

  // Device protocol implementation.
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  zx_status_t DdkClose(uint32_t flags);
  void DdkRelease();

 private:
  int ClientThread();

  thrd_t client_thread_{};
  async::Loop client_loop_;
  using PipeMap = std::map<Pipe*, std::unique_ptr<Pipe>>;
  PipeMap pipes_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Instance);
};

}  // namespace goldfish

#endif  // ZIRCON_SYSTEM_DEV_MISC_GOLDFISH_INSTANCE_H_
