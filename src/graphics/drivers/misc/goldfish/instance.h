// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_INSTANCE_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_INSTANCE_H_

#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/device.h>
#include <threads.h>
#include <zircon/types.h>

#include <map>
#include <memory>

#include <ddktl/device.h>

namespace goldfish {

class Pipe;
class Instance;
using InstanceType =
    ddk::Device<Instance, ddk::Messageable<fuchsia_hardware_goldfish::PipeDevice>::Mixin,
                ddk::Closable>;

// This class implements a pipe instance device. By opening the pipe device,
// an instance of this class will be created to service a new channel
// to the virtual device.
class Instance : public InstanceType {
 public:
  explicit Instance(zx_device_t* parent);
  ~Instance();

  zx_status_t Bind();

  // |fidl::WireServer<fuchsia_hardware_goldfish::PipeDevice>|
  void OpenPipe(OpenPipeRequestView request, OpenPipeCompleter::Sync& completer) override;

  // Device protocol implementation.
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

#endif  // SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_INSTANCE_H_
