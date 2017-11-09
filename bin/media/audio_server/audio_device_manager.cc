// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_device_manager.h"

#include <fbl/algorithm.h>
#include <string>

#include "garnet/bin/media/audio_server/audio_link.h"
#include "garnet/bin/media/audio_server/audio_output.h"
#include "garnet/bin/media/audio_server/audio_plug_detector.h"
#include "garnet/bin/media/audio_server/audio_server_impl.h"
#include "garnet/bin/media/audio_server/platform/generic/throttle_output.h"

namespace media {
namespace audio {

AudioDeviceManager::AudioDeviceManager(AudioServerImpl* server)
    : server_(server) {}

AudioDeviceManager::~AudioDeviceManager() {
  Shutdown();
  FXL_DCHECK(devices_.is_empty());
}

MediaResult AudioDeviceManager::Init() {
  // Step #1: Instantiate and initialize the default throttle output.
  auto throttle_output = ThrottleOutput::Create(this);
  if (throttle_output == nullptr) {
    FXL_LOG(WARNING)
        << "AudioDeviceManager failed to create default throttle output!";
    return MediaResult::INSUFFICIENT_RESOURCES;
  }

  MediaResult res = throttle_output->Startup();
  if (res != MediaResult::OK) {
    FXL_LOG(WARNING)
        << "AudioDeviceManager failed to initalize the throttle output (res "
        << res << ")";
    throttle_output->Shutdown();
  }
  throttle_output_ = std::move(throttle_output);

  // Step #2: Being monitoring for plug/unplug events for pluggable audio
  // output devices.
  res = plug_detector_.Start(this);
  if (res != MediaResult::OK) {
    FXL_LOG(WARNING) << "AudioDeviceManager failed to start plug detector (res "
                     << res << ")";
    return res;
  }

  return MediaResult::OK;
}

void AudioDeviceManager::Shutdown() {
  // Step #1: Stop monitoring plug/unplug events.  We are shutting down and
  // no longer care about devices coming and going.
  plug_detector_.Stop();

  // Step #2: Shutdown all of the active renderers in the system.
  while (!renderers_.is_empty()) {
    auto renderer =
        fbl::RefPtr<AudioRendererImpl>::Downcast(renderers_.pop_front());
    renderer->Shutdown();
  }

  // Step #3: Shut down each currently active device in the system.
  while (!devices_.is_empty()) {
    auto device = fbl::RefPtr<AudioDevice>::Downcast(devices_.pop_front());
    device->Shutdown();
  }

  throttle_output_->Shutdown();
  throttle_output_ = nullptr;
}

MediaResult AudioDeviceManager::AddDevice(
    const fbl::RefPtr<AudioDevice>& device) {
  FXL_DCHECK(device != nullptr);
  FXL_DCHECK(!device->in_object_list());

  if (device->is_output()) {
    auto output = static_cast<AudioOutput*>(device.get());
    FXL_DCHECK(output != throttle_output_.get());
    static_cast<AudioOutput*>(device.get())->SetGain(master_gain());
  }
  devices_.push_back(device);

  MediaResult res = device->Startup();
  if (res != MediaResult::OK) {
    devices_.erase(*device);
    device->Shutdown();
  }

  if (device->plugged()) {
    OnDevicePlugged(device);
  }

  return res;
}

void AudioDeviceManager::RemoveDevice(const fbl::RefPtr<AudioDevice>& device) {
  FXL_DCHECK(device != nullptr);
  FXL_DCHECK(device->is_output() || (static_cast<AudioDevice*>(device.get()) !=
                                     throttle_output_.get()));

  device->PreventNewLinks();
  device->Unlink();

  if (device->in_object_list()) {
    if (device->UpdatePlugState(false, device->plug_time())) {
      OnDeviceUnplugged(device);
    }
    device->Shutdown();
    devices_.erase(*device);
  }
}

void AudioDeviceManager::HandlePlugStateChange(
    const fbl::RefPtr<AudioDevice>& device,
    bool plugged,
    zx_time_t plug_time) {
  FXL_DCHECK(device != nullptr);
  if (device->UpdatePlugState(plugged, plug_time)) {
    if (plugged) {
      OnDevicePlugged(device);
    } else {
      OnDeviceUnplugged(device);
    }
  }
}

void AudioDeviceManager::SetMasterGain(float db_gain) {
  master_gain_ = fbl::clamp(db_gain, AudioRenderer::kMutedGain, 0.0f);
  for (auto& device : devices_) {
    if (device.is_input()) {
      continue;
    }
    static_cast<AudioOutput*>(&device)->SetGain(master_gain_);
  }
}

void AudioDeviceManager::SelectOutputsForRenderer(AudioRendererImpl* renderer) {
  FXL_DCHECK(renderer);
  FXL_DCHECK(renderer->format_info_valid());

  // TODO(johngro): Add some way to assert that we are executing on the main
  // message loop thread.

  // Regardless of policy, all renderers should always be linked to the special
  // throttle output.
  LinkOutputToRenderer(throttle_output_.get(), renderer);

  switch (routing_policy_) {
    case RoutingPolicy::ALL_PLUGGED_OUTPUTS: {
      for (auto& obj : devices_) {
        FXL_DCHECK(obj.is_input() || obj.is_output());
        auto device = static_cast<AudioDevice*>(&obj);
        if (device->is_output() && device->plugged()) {
          LinkOutputToRenderer(static_cast<AudioOutput*>(device), renderer);
        }
      }
    } break;

    case RoutingPolicy::LAST_PLUGGED_OUTPUT: {
      fbl::RefPtr<AudioOutput> last_plugged = FindLastPluggedOutput();
      if (last_plugged != nullptr) {
        LinkOutputToRenderer(last_plugged.get(), renderer);
      }

    } break;
  }
}

void AudioDeviceManager::LinkOutputToRenderer(AudioOutput* output,
                                              AudioRendererImpl* renderer) {
  FXL_DCHECK(output);
  FXL_DCHECK(renderer);

  // Do not create any links if the renderer's output format has not been set.
  // Links will be created during SelectOutputsForRenderer when the renderer
  // finally has its format set via AudioRendererImpl::SetMediaType
  if (!renderer->format_info_valid())
    return;

  std::shared_ptr<AudioLink> link = AudioObject::LinkObjects(
      fbl::WrapRefPtr(renderer), fbl::WrapRefPtr(output));
  // TODO(johngro): get rid of the throttle output.  See MTWN-52
  if ((link != nullptr) && (output == throttle_output_.get())) {
    FXL_DCHECK(link->source_type() == AudioLink::SourceType::Packet);
    renderer->SetThrottleOutput(
        std::static_pointer_cast<AudioLinkPacketSource>(std::move(link)));
  }
}

void AudioDeviceManager::ScheduleMessageLoopTask(const fxl::Closure& task) {
  FXL_DCHECK(server_);
  server_->ScheduleMessageLoopTask(task);
}

fbl::RefPtr<AudioDevice> AudioDeviceManager::FindLastPlugged(
    AudioObject::Type type,
    bool allow_unplugged) {
  FXL_DCHECK((type == AudioObject::Type::Output) ||
             (type == AudioObject::Type::Input));
  AudioDevice* best = nullptr;

  // TODO(johngro) : Consider tracking last plugged time using a fbl::WAVLTree
  // so that this operation becomes O(1).  N is pretty low right now, so the
  // benefits do not currently outweigh the complexity of maintaining this
  // index.
  for (auto& obj : devices_) {
    auto device = static_cast<AudioDevice*>(&obj);
    if (device->type() != type) {
      continue;
    }

    if ((best == nullptr) || (!best->plugged() && device->plugged()) ||
        ((best->plugged() == device->plugged()) &&
         (best->plug_time() < device->plug_time()))) {
      best = device;
    }
  }

  FXL_DCHECK((best == nullptr) || (best->type() == type));
  if (!allow_unplugged && best && !best->plugged())
    return nullptr;

  return fbl::WrapRefPtr(best);
}

void AudioDeviceManager::OnDeviceUnplugged(
    const fbl::RefPtr<AudioDevice>& device) {
  FXL_DCHECK(device && !device->plugged());

  // This device was just unplugged.  Unlink it from everything it is currently
  // linked to.
  device->Unlink();

  // If this is an output, and we we are applying 'last plugged output' policy,
  // replace it with the new 'last plugged' output (if any)
  if (device->is_output() &&
      (routing_policy_ == RoutingPolicy::LAST_PLUGGED_OUTPUT)) {
    FXL_DCHECK(static_cast<AudioOutput*>(device.get()) !=
               throttle_output_.get());

    fbl::RefPtr<AudioOutput> replacement = FindLastPluggedOutput();
    if (replacement) {
      for (auto& obj : renderers_) {
        FXL_DCHECK(obj.is_renderer());
        auto renderer = static_cast<AudioRendererImpl*>(&obj);
        LinkOutputToRenderer(replacement.get(), renderer);
      }
    }
  }
}

void AudioDeviceManager::OnDevicePlugged(
    const fbl::RefPtr<AudioDevice>& device) {
  FXL_DCHECK(device && device->plugged());

  // If this is an input, we currently have nothing special to do.
  if (device->is_input()) {
    return;
  }

  auto output = static_cast<AudioOutput*>(device.get());

  switch (routing_policy_) {
    case RoutingPolicy::ALL_PLUGGED_OUTPUTS:
      // If we are following the 'all plugged outputs' routing policy, simply
      // add this newly plugged output to all of the active renderers.
      for (auto& obj : renderers_) {
        FXL_DCHECK(obj.is_renderer());
        auto renderer = static_cast<AudioRendererImpl*>(&obj);
        LinkOutputToRenderer(output, renderer);
      }
      break;

    case RoutingPolicy::LAST_PLUGGED_OUTPUT:
      // This output was just plugged in, deal with routing issues.  If we are
      // using the 'last plugged' policy, unlink all of the active renderers
      // from the outputs they are connected to, then link them to the newly
      // plugged output.
      //
      // Note; we need to make sure that this is actually the last plugged
      // output.  Because of the parallelized nature of plug detection and
      // stream discovery, it is possible that two outputs might be plugged in
      // at similar times, but we handle their plugged status out-of-order.
      if (FindLastPluggedOutput().get() != output)
        return;

      for (auto& unlink_tgt : devices_) {
        if (unlink_tgt.is_output() && (&unlink_tgt != device.get())) {
          unlink_tgt.UnlinkSources();
        }
      }

      for (auto& obj : renderers_) {
        FXL_DCHECK(obj.is_renderer());
        auto renderer = static_cast<AudioRendererImpl*>(&obj);
        LinkOutputToRenderer(output, renderer);
      }
      break;
  }
}

}  // namespace audio
}  // namespace media
