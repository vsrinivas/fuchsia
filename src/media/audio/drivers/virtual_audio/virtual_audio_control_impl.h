// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_CONTROL_IMPL_H_
#define SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_CONTROL_IMPL_H_

#include <fuchsia/virtualaudio/c/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <memory>

#include <ddk/device.h>

namespace virtual_audio {

class VirtualAudioDeviceImpl;

class VirtualAudioControlImpl : public fuchsia::virtualaudio::Control {
 public:
  VirtualAudioControlImpl(async_dispatcher_t* dispatcher) : dev_host_dispatcher_(dispatcher) {
    ZX_ASSERT(dev_host_dispatcher_ != nullptr);
  }

  // TODO(mpuryear): Move the three static methods and table over to DDKTL.
  //
  // Always called after DdkUnbind.
  static void DdkRelease(void* ctx);
  // Always called after our child drivers are unbound and released.
  static void DdkUnbind(void* ctx);
  // Delivers C-binding-FIDL Forwarder calls to the driver.
  static zx_status_t DdkMessage(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn);

  //
  // virtualaudio.Forwarder interface
  //
  zx_status_t SendControl(zx::channel control_request_channel);
  zx_status_t SendInput(zx::channel input_request_channel);
  zx_status_t SendOutput(zx::channel output_request_channelf);

  //
  // virtualaudio.Control interface
  //
  void Enable(EnableCallback callback) override;
  void Disable(DisableCallback callback) override;
  void GetNumDevices(GetNumDevicesCallback callback) override;

  void ReleaseBindings();
  bool enabled() const { return enabled_; }
  zx_device_t* dev_node() const { return dev_node_; }
  async_dispatcher_t* dispatcher() const { return dev_host_dispatcher_; }

 private:
  friend class VirtualAudioBus;
  friend class VirtualAudioDeviceImpl;

  static fuchsia_virtualaudio_Forwarder_ops_t fidl_ops_;

  async_dispatcher_t* dev_host_dispatcher_ = nullptr;

  zx_device_t* dev_node_ = nullptr;
  bool enabled_ = true;

  fidl::BindingSet<fuchsia::virtualaudio::Control> bindings_;
  fidl::BindingSet<fuchsia::virtualaudio::Input, std::unique_ptr<VirtualAudioDeviceImpl>>
      input_bindings_;
  fidl::BindingSet<fuchsia::virtualaudio::Output, std::unique_ptr<VirtualAudioDeviceImpl>>
      output_bindings_;
};

}  // namespace virtual_audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_VIRTUAL_AUDIO_VIRTUAL_AUDIO_CONTROL_IMPL_H_
