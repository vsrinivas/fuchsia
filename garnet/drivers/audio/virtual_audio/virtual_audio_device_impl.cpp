// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/audio/virtual_audio/virtual_audio_device_impl.h"

#include "garnet/drivers/audio/virtual_audio/virtual_audio_control_impl.h"
#include "garnet/drivers/audio/virtual_audio/virtual_audio_stream.h"

namespace virtual_audio {

VirtualAudioDeviceImpl::VirtualAudioDeviceImpl(VirtualAudioControlImpl* owner)
    : owner_(owner) {
  ZX_DEBUG_ASSERT(owner_);
}

// If we have not already destroyed our child stream, do so now.
VirtualAudioDeviceImpl::~VirtualAudioDeviceImpl() { RemoveStream(); }

// Allows a child stream to signal to its parent that it has gone away.
void VirtualAudioDeviceImpl::ClearStream() { stream_ = nullptr; }

// Removes this device's child stream by calling its Unbind method. This may
// already have occurred, so first check it for null.
//
// TODO(mpuryear): This is not quite the right way to safely unwind in all
// cases: it makes some threading assumptions that cannot necessarily be
// enforced. But until ZX-3461 is addressed, the current VAD code appears to
// be safe (all Unbind callers are on the devhost primary thread).
void VirtualAudioDeviceImpl::RemoveStream() {
  if (stream_ != nullptr) {
    // This bool tells the stream that its Unbind is originating from us (the
    // parent), so that it doesn't call us back.
    stream_->shutdown_by_parent_ = true;
    stream_->DdkUnbind();
  }
}

//
// Device implementation
//
// Create a virtual audio device using the currently-specified configuration.
void VirtualAudioDeviceImpl::Add() {
  if (!owner_->enabled()) {
    zxlogf(TRACE, "%s: Disabled, cannot add stream\n", __PRETTY_FUNCTION__);
    return;
  }

  if (stream_ != nullptr) {
    zxlogf(TRACE, "%s: %p already has stream %p\n", __PRETTY_FUNCTION__, this,
           stream_.get());
    return;
  }

  if (!CreateStream(owner_->dev_node())) {
    zxlogf(ERROR, "CreateStream failed\n");
    return;
  }
}

// Remove the associated virtual audio device.
void VirtualAudioDeviceImpl::Remove() {
  if (!owner_->enabled()) {
    zxlogf(TRACE, "%s: Disabled, no streams for removal\n",
           __PRETTY_FUNCTION__);
    ZX_DEBUG_ASSERT(stream_ == nullptr);
    return;
  }

  if (stream_ == nullptr) {
    zxlogf(TRACE, "%s: %p has no stream to remove\n", __PRETTY_FUNCTION__,
           this);
    return;
  }

  // If stream_ exists, null our copy and call SimpleAudioStream::DdkUnbind
  // (which eventually calls ShutdownHook and re-nulls). This is necessary
  // because stream terminations can come either from "device" (direct DdkUnbind
  // call), or from "parent" (Control::Disable, Device::Remove, ~DeviceImpl).
  RemoveStream();
}

// Change the plug state on-the-fly for this active virtual audio device.
void VirtualAudioDeviceImpl::ChangePlugState(zx_time_t plug_change_time,
                                             bool plugged) {
  if (!owner_->enabled()) {
    zxlogf(TRACE, "%s: Disabled, cannot change plug state\n",
           __PRETTY_FUNCTION__);
    return;
  }

  // Update static config, and tell (if present) stream to dynamically change.
  plug_time_ = plug_change_time;
  plugged_ = plugged;

  if (stream_ == nullptr) {
    zxlogf(TRACE, "%s: %p has no stream; cannot change dynamic plug state\n",
           __PRETTY_FUNCTION__, this);
    return;
  }

  stream_->ChangePlugState(plugged);
}

}  // namespace virtual_audio
