// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_CONTROL_IMPL_H_
#define SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_CONTROL_IMPL_H_

#include <fidl/fuchsia.virtualaudio/cpp/wire.h>
#include <lib/ddk/device.h>

#include <memory>
#include <unordered_set>

namespace virtual_audio {

class VirtualAudioDeviceImpl;

class VirtualAudioControlImpl : public fidl::WireServer<fuchsia_virtualaudio::Control> {
 public:
  static zx_status_t DdkBind(void* ctx, zx_device_t* parent_bus);
  static void DdkRelease(void* ctx);
  static void DdkUnbind(void* ctx);
  static zx_status_t DdkMessage(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn);

 private:
  VirtualAudioControlImpl() = default;

  // Implements virtualaudio.Control.
  void AddInput(AddInputRequestView request, AddInputCompleter::Sync& completer) override;
  void AddOutput(AddOutputRequestView request, AddOutputCompleter::Sync& completer) override;
  void GetNumDevices(GetNumDevicesCompleter::Sync& completer) override;
  void RemoveAll(RemoveAllCompleter::Sync& completer) override;

  zx_device_t* dev_node_ = nullptr;
  async_dispatcher_t* dispatcher_ = nullptr;

  std::unordered_set<std::shared_ptr<VirtualAudioDeviceImpl>> devices_;
};

}  // namespace virtual_audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_CONTROL_IMPL_H_
