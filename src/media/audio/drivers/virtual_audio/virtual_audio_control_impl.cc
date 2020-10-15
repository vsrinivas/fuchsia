// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/drivers/virtual_audio/virtual_audio_control_impl.h"

#include <lib/async/cpp/task.h>

#include <ddk/debug.h>

#include "src/media/audio/drivers/virtual_audio/virtual_audio_device_impl.h"
#include "src/media/audio/drivers/virtual_audio/virtual_audio_stream.h"

namespace virtual_audio {

// static
//
// Unbind any published children (which will remove them), shut down any async loop, ensure nothing
// is in flight, then remove ourselves from the dev tree.
//
// Unbind proceeds "down" from parent to child, while Release proceeds "up" (called for parent once
// all children have been released).
void VirtualAudioControlImpl::DdkUnbind(void* ctx) {
  ZX_DEBUG_ASSERT(ctx != nullptr);

  auto self = static_cast<VirtualAudioControlImpl*>(ctx);

  // Close any remaining control (or stream) bindings, freeing those drivers.
  self->ReleaseBindings();

  // Now remove the control device itself (this later calls our DdkRelease).
  device_unbind_reply(self->dev_node());
}

// static
//
// Always called after DdkUnbind, which should guarantee that lists are emptied. Any last cleanup or
// logical consistency checks would be done here. By the time this is called, all child devices have
// already been released.
void VirtualAudioControlImpl::DdkRelease(void* ctx) {
  ZX_DEBUG_ASSERT(ctx != nullptr);

  // DevMgr has returned ownership of whatever we provided as driver ctx (our
  // VirtualAudioControlImpl). When this functions returns, this unique_ptr will go out of scope,
  // triggering ~VirtualAudioControlImpl.
  std::unique_ptr<VirtualAudioControlImpl> control_ptr(static_cast<VirtualAudioControlImpl*>(ctx));

  // By now, all our lists should be empty.
  ZX_DEBUG_ASSERT(control_ptr->bindings_.size() == 0);
  ZX_DEBUG_ASSERT(control_ptr->input_bindings_.size() == 0);
  ZX_DEBUG_ASSERT(control_ptr->output_bindings_.size() == 0);
}

// static
//
zx_status_t VirtualAudioControlImpl::DdkMessage(void* ctx, fidl_incoming_msg_t* msg,
                                                fidl_txn_t* txn) {
  ZX_DEBUG_ASSERT(ctx != nullptr);

  return fuchsia_virtualaudio_Forwarder_dispatch(ctx, txn, msg, &fidl_ops_);
}

// static
//
fuchsia_virtualaudio_Forwarder_ops_t VirtualAudioControlImpl::fidl_ops_ = {
    .SendControl =
        [](void* ctx, zx_handle_t control_request) {
          ZX_DEBUG_ASSERT(ctx != nullptr);
          return static_cast<VirtualAudioControlImpl*>(ctx)->SendControl(
              zx::channel(control_request));
        },
    .SendInput =
        [](void* ctx, zx_handle_t input_request) {
          ZX_DEBUG_ASSERT(ctx != nullptr);
          return static_cast<VirtualAudioControlImpl*>(ctx)->SendInput(zx::channel(input_request));
        },
    .SendOutput =
        [](void* ctx, zx_handle_t output_request) {
          ZX_DEBUG_ASSERT(ctx != nullptr);
          return static_cast<VirtualAudioControlImpl*>(ctx)->SendOutput(
              zx::channel(output_request));
        },
};

// A client connected to fuchsia.virtualaudio.Control hosted by the virtual audio service, which is
// forwarding the server-side binding to us.
zx_status_t VirtualAudioControlImpl::SendControl(zx::channel control_request_channel) {
  if (!control_request_channel.is_valid()) {
    zxlogf(ERROR, "%s: channel from request handle is invalid", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  // VirtualAudioControlImpl is a singleton so just save the binding in a list. Using the default
  // dispatcher means that we will be running on the same that drives all of our peer devices in the
  // /dev/test device host. We should ensure there are no long VirtualAudioControl operations.
  bindings_.AddBinding(this, fidl::InterfaceRequest<fuchsia::virtualaudio::Control>(
                                 std::move(control_request_channel)));
  return ZX_OK;
}

// A client connected to fuchsia.virtualaudio.Input hosted by the virtual audio service, which is
// forwarding the server-side binding to us.
zx_status_t VirtualAudioControlImpl::SendInput(zx::channel input_request_channel) {
  if (!input_request_channel.is_valid()) {
    zxlogf(ERROR, "%s: channel from request handle is invalid", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  // Create an VirtualAudioDeviceImpl for this binding; save it in our list. Using the default
  // dispatcher means that we will be running on the same that drives all of our peer devices in the
  // /dev/test device host. We should be mindful of this if doing long VirtualAudioInput operations.
  input_bindings_.AddBinding(
      VirtualAudioDeviceImpl::Create(this, true),
      fidl::InterfaceRequest<fuchsia::virtualaudio::Input>(std::move(input_request_channel)));

  auto* binding = input_bindings_.bindings().back().get();
  binding->impl()->SetBinding(binding);

  return ZX_OK;
}

zx_status_t VirtualAudioControlImpl::SendOutput(zx::channel output_request_channel) {
  if (!output_request_channel.is_valid()) {
    zxlogf(ERROR, "%s: channel from request handle is invalid", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  // Create a VirtualAudioDeviceImpl for this binding; save it in our list. Using the default
  // dispatcher means that we will be running on the same that drives all of our peer devices in the
  // /dev/test device host. We should be mindful of this if doing long VirtualAudioOutput
  // operations.
  output_bindings_.AddBinding(
      VirtualAudioDeviceImpl::Create(this, false),
      fidl::InterfaceRequest<fuchsia::virtualaudio::Output>(std::move(output_request_channel)));

  auto* binding = output_bindings_.bindings().back().get();
  binding->impl()->SetBinding(binding);

  return ZX_OK;
}

// Reset any remaining bindings of Controls, Inputs and Outputs. This is called during Unbind, at
// which time child drivers should be gone (and input_bindings_ and output_bindings_ empty).
void VirtualAudioControlImpl::ReleaseBindings() {
  bindings_.CloseAll();
  input_bindings_.CloseAll();
  output_bindings_.CloseAll();
}

// Allow subsequent new stream creation -- but do not automatically reactivate
// any streams that may have been deactivated (removed) by the previous Disable.
// Upon construction, the default state of this object is Enabled. The (empty)
// callback is used to synchronize with other in-flight asynchronous operations.
void VirtualAudioControlImpl::Enable(EnableCallback enable_callback) {
  enabled_ = true;

  enable_callback();
}

// Deactivate active streams and prevent subsequent new stream creation. Audio devices vanish from
// the dev tree (VirtualAudioStream objects are freed), but Input and Output channels remain open
// and can be reconfigured. Once Enable is called; they can be re-added without losing configuration
// state. The (empty) callback is used to synchronize with other in-flight asynchronous operations.
void VirtualAudioControlImpl::Disable(DisableCallback disable_callback) {
  if (enabled_) {
    for (auto& binding : input_bindings_.bindings()) {
      binding->impl()->RemoveStream();
    }

    for (auto& binding : output_bindings_.bindings()) {
      binding->impl()->RemoveStream();
    }

    enabled_ = false;
  }

  disable_callback();
}

// Return the number of active input and output streams. The callback is used to synchronize with
// other in-flight asynchronous operations.
void VirtualAudioControlImpl::GetNumDevices(GetNumDevicesCallback get_num_devices_callback) {
  uint32_t num_inputs = 0, num_outputs = 0;

  for (auto& binding : input_bindings_.bindings()) {
    if (binding->impl()->IsActive()) {
      ++num_inputs;
    }
  }

  for (auto& binding : output_bindings_.bindings()) {
    if (binding->impl()->IsActive()) {
      ++num_outputs;
    }
  }

  get_num_devices_callback(num_inputs, num_outputs);
}

}  // namespace virtual_audio
