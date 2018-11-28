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

VirtualAudioDeviceImpl::~VirtualAudioDeviceImpl() {
  if (stream_ != nullptr) {
    // TODO(mpuryear): This is not quite the right way to safely unwind in all
    // cases: it makes some threading assumptions that cannot necessarily be
    // enforced. But until ZX-3461 is addressed, the current VAD code appears to
    // be safe (all Unbind callers are on the devhost primary thread).
    stream_->DdkUnbind();
  }
}

// Called from our stream's ShutdownHook, as a result of DdkUnbind being called.
void VirtualAudioDeviceImpl::RemoveStream() {
  if (stream_ != nullptr) {
    stream_.reset();
  }
}

//
// Device implementation
//
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

  // This will result in our stream_ being reset. SimpleAudioStream::DdkUnbind
  // leads to SimpleAudioStream::Shutdown/VirtualAudioStream::ShutdownHook,
  // which calls its parent (that's us!) VirtualAudioDeviceImpl::RemoveStream.
  stream_->DdkUnbind();
}

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
