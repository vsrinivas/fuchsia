// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/audio_device_manager.h"

#include <string>

#include <fbl/algorithm.h>

#include "garnet/bin/media/audio_server/audio_capturer_impl.h"
#include "garnet/bin/media/audio_server/audio_link.h"
#include "garnet/bin/media/audio_server/audio_output.h"
#include "garnet/bin/media/audio_server/audio_plug_detector.h"
#include "garnet/bin/media/audio_server/audio_server_impl.h"
#include "garnet/bin/media/audio_server/throttle_output.h"

namespace media {
namespace audio {

AudioDeviceManager::AudioDeviceManager(AudioServerImpl* server)
    : server_(server) {}

AudioDeviceManager::~AudioDeviceManager() {
  Shutdown();
  FXL_DCHECK(devices_.is_empty());
}

zx_status_t AudioDeviceManager::Init() {
  // Step #1: Instantiate and initialize the default throttle output.
  auto throttle_output = ThrottleOutput::Create(this);
  if (throttle_output == nullptr) {
    FXL_LOG(WARNING)
        << "AudioDeviceManager failed to create default throttle output!";
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t res = throttle_output->Startup();
  if (res != ZX_OK) {
    FXL_LOG(WARNING)
        << "AudioDeviceManager failed to initalize the throttle output (res "
        << res << ")";
    throttle_output->Shutdown();
  }
  throttle_output_ = std::move(throttle_output);

  // Step #2: Being monitoring for plug/unplug events for pluggable audio
  // output devices.
  res = plug_detector_.Start(this);
  if (res != ZX_OK) {
    FXL_LOG(WARNING) << "AudioDeviceManager failed to start plug detector (res "
                     << res << ")";
    return res;
  }

  return ZX_OK;
}

void AudioDeviceManager::Shutdown() {
  // Step #1: Stop monitoring plug/unplug events.  We are shutting down and
  // no longer care about devices coming and going.
  plug_detector_.Stop();

  // Step #2: Shutdown all of the active capturers in the system.
  while (!capturers_.is_empty()) {
    auto capturer = capturers_.pop_front();
    capturer->Shutdown();
  }

  // Step #3: Shutdown all of the active renderers in the system.
  while (!renderers_.is_empty()) {
    auto renderer = renderers_.pop_front();
    renderer->Shutdown();
  }

  // Step #4: Shut down each device which is currently waiting to become
  // initialized.
  while (!devices_pending_init_.is_empty()) {
    auto device = devices_pending_init_.pop_front();
    device->Shutdown();
  }

  // Step #5: Shut down each currently active device in the system.
  while (!devices_.is_empty()) {
    auto device = devices_.pop_front();
    device->Shutdown();
  }

  throttle_output_->Shutdown();
  throttle_output_ = nullptr;
}

void AudioDeviceManager::AddDeviceEnumeratorClient(zx::channel ch) {
  bindings_.AddBinding(
      this, fidl::InterfaceRequest<fuchsia::media::AudioDeviceEnumerator>(
                std::move(ch)));
}

zx_status_t AudioDeviceManager::AddDevice(
    const fbl::RefPtr<AudioDevice>& device) {
  FXL_DCHECK(device != nullptr);
  FXL_DCHECK(device != throttle_output_);
  FXL_DCHECK(!device->InContainer());

  zx_status_t res = device->Startup();
  if (res != ZX_OK) {
    device->Shutdown();
  }

  devices_pending_init_.insert(device);
  return res;
}

void AudioDeviceManager::ActivateDevice(
    const fbl::RefPtr<AudioDevice>& device) {
  FXL_DCHECK(device != nullptr);
  FXL_DCHECK(device != throttle_output_);

  // Have we already been removed from the pending list?  If so, the device is
  // already shutting down and there is
  // nothing to be done.
  if (!device->InContainer()) {
    return;
  }

  // Move the deivce over to the set of active devices.
  devices_.insert(devices_pending_init_.erase(*device));
  device->SetActivated();

  // TODO(johngro): load and apply persisted settings now

  // TODO(johngro): notify interested users of the new device.

  // Reconsider our current routing policy now that we have a new device present
  // in the system.
  if (device->plugged()) {
    zx_time_t plug_time = device->plug_time();
    device->UpdatePlugState(false, plug_time);
    OnDevicePlugged(device, plug_time);
  }
}

void AudioDeviceManager::RemoveDevice(const fbl::RefPtr<AudioDevice>& device) {
  FXL_DCHECK(device != nullptr);
  FXL_DCHECK(device->is_output() || (static_cast<AudioDevice*>(device.get()) !=
                                     throttle_output_.get()));

  device->PreventNewLinks();
  device->Unlink();

  if (device->activated()) {
    OnDeviceUnplugged(device, device->plug_time());
  }

  device->Shutdown();

  if (device->InContainer()) {
    auto& device_set = device->activated() ? devices_ : devices_pending_init_;
    device_set.erase(*device);
  }
}

void AudioDeviceManager::HandlePlugStateChange(
    const fbl::RefPtr<AudioDevice>& device, bool plugged, zx_time_t plug_time) {
  FXL_DCHECK(device != nullptr);
  if (plugged) {
    OnDevicePlugged(device, plug_time);
  } else {
    OnDeviceUnplugged(device, plug_time);
  }
}

void AudioDeviceManager::SetMasterGain(float db_gain) {
  master_gain_ = fbl::clamp(db_gain, fuchsia::media::kMutedGain, 0.0f);
  for (auto& device : devices_) {
    if (device.is_input()) {
      continue;
    }
    static_cast<AudioOutput*>(&device)->SetGain(master_gain_);
  }
}

void AudioDeviceManager::GetDevices(GetDevicesCallback cbk) {
  std::vector<::fuchsia::media::AudioDeviceInfo> ret;

  // TODO(johngro): Implement

  cbk(fidl::VectorPtr<::fuchsia::media::AudioDeviceInfo>(std::move(ret)));
}

void AudioDeviceManager::GetDeviceGain(uint64_t device_token,
                                       GetDeviceGainCallback cbk) {
  // TODO(johngro): Implement
  ::fuchsia::media::AudioGainInfo info;
  info.db_gain = 0.0;
  info.flags = 0;
  cbk(ZX_KOID_INVALID, std::move(info));
}

void AudioDeviceManager::SetDeviceGain(
    uint64_t device_token, ::fuchsia::media::AudioGainInfo gain_info,
    uint32_t set_flags) {
  // TODO(johngro): Implement
}

void AudioDeviceManager::GetDefaultInputDevice(
    GetDefaultInputDeviceCallback cbk) {
  // TODO(johngro): Implement
  cbk(ZX_KOID_INVALID);
}

void AudioDeviceManager::GetDefaultOutputDevice(
    GetDefaultOutputDeviceCallback cbk) {
  // TODO(johngro): Implement
  cbk(ZX_KOID_INVALID);
}

void AudioDeviceManager::SelectOutputsForRenderer(AudioRendererImpl* renderer) {
  FXL_DCHECK(renderer);
  FXL_DCHECK(renderer->format_info_valid());
  FXL_DCHECK(ValidateRoutingPolicy(routing_policy_));

  // TODO(johngro): Add some way to assert that we are executing on the main
  // message loop thread.

  // Regardless of policy, all renderers should always be linked to the
  // special throttle output.
  LinkOutputToRenderer(throttle_output_.get(), renderer);

  switch (routing_policy_) {
    case fuchsia::media::AudioOutputRoutingPolicy::kAllPluggedOutputs: {
      for (auto& obj : devices_) {
        FXL_DCHECK(obj.is_input() || obj.is_output());
        auto device = static_cast<AudioDevice*>(&obj);
        if (device->is_output() && device->plugged()) {
          LinkOutputToRenderer(static_cast<AudioOutput*>(device), renderer);
        }
      }
    } break;

    case fuchsia::media::AudioOutputRoutingPolicy::kLastPluggedOutput: {
      fbl::RefPtr<AudioOutput> last_plugged = FindLastPluggedOutput();
      if (last_plugged != nullptr) {
        LinkOutputToRenderer(last_plugged.get(), renderer);
      }

    } break;
  }

  // Figure out the initial minimum clock lead time requirement.
  renderer->RecomputeMinClockLeadTime();
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

void AudioDeviceManager::AddCapturer(fbl::RefPtr<AudioCapturerImpl> capturer) {
  FXL_DCHECK(capturer != nullptr);
  FXL_DCHECK(!capturer->InContainer());
  capturers_.push_back(capturer);

  fbl::RefPtr<AudioDevice> source;
  if (capturer->loopback()) {
    source = FindLastPluggedOutput(true);
  } else {
    source = FindLastPluggedInput(true);
  }

  if (source != nullptr) {
    FXL_DCHECK(source->driver() != nullptr);
    auto initial_format = source->driver()->GetSourceFormat();

    if (initial_format) {
      capturer->SetInitialFormat(std::move(*initial_format));
    }

    if (source->plugged()) {
      AudioObject::LinkObjects(source, capturer);
    }
  }
}

void AudioDeviceManager::RemoveCapturer(AudioCapturerImpl* capturer) {
  FXL_DCHECK(capturer != nullptr);
  FXL_DCHECK(capturer->InContainer());
  capturers_.erase(*capturer);
}

void AudioDeviceManager::ScheduleMainThreadTask(fit::closure task) {
  FXL_DCHECK(server_);
  server_->ScheduleMainThreadTask(std::move(task));
}

fbl::RefPtr<AudioDevice> AudioDeviceManager::FindLastPlugged(
    AudioObject::Type type, bool allow_unplugged) {
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

// Our policy governing the routing of audio outputs has changed. For the output
// considered "preferred" (because it was most-recently-added), nothing changes;
// all other outputs will toggle on or off, depending on the policy chosen.
void AudioDeviceManager::SetRoutingPolicy(
    fuchsia::media::AudioOutputRoutingPolicy routing_policy) {
  if (!ValidateRoutingPolicy(routing_policy)) {
    FXL_LOG(ERROR) << "Out-of-range RoutingPolicy(" << routing_policy << ")";
    // TODO(mpuryear) Once AudioServer has a way to know which connection made
    // this request, terminate that connection now rather than doing nothing.
    return;
  }

  if (routing_policy == routing_policy_) {
    return;
  }

  routing_policy_ = routing_policy;
  fbl::RefPtr<AudioOutput> last_plugged_output = FindLastPluggedOutput();

  // Iterate thru all of our audio devices -- only a subset are affected.
  for (auto& dev_obj : devices_) {
    // If device is an input, it is unaffected by this change in output-routing.
    if (dev_obj.is_input()) {
      continue;
    }

    // If (output) device is not plugged-in, it is unaffected by output-routing.
    auto output = static_cast<AudioOutput*>(&dev_obj);
    if (!output->plugged()) {
      continue;
    }

    // If device is most-recently plugged, it is unaffected by this change in
    // policy (either way, it will continue to be attached to all renderers).
    FXL_DCHECK(output != throttle_output_.get());
    if (output == last_plugged_output.get()) {
      continue;
    }

    // We've excluded inputs, unplugged outputs and the most-recently-plugged
    // output. For each remaining output (based on the new policy), we ...
    if (routing_policy ==
        fuchsia::media::AudioOutputRoutingPolicy::kLastPluggedOutput) {
      // ...disconnect it (i.e. link renderers to Last-Plugged only), or...
      dev_obj.UnlinkSources();
    } else {
      // ...attach it (i.e. link renderers to All Outputs).
      for (auto& obj : renderers_) {
        FXL_DCHECK(obj.is_renderer());
        auto renderer = static_cast<AudioRendererImpl*>(&obj);
        LinkOutputToRenderer(output, renderer);
      }
    }
  }

  // After changing routing, determine new minimum clock lead time requirements.
  for (auto& obj : renderers_) {
    FXL_DCHECK(obj.is_renderer());
    auto renderer = static_cast<AudioRendererImpl*>(&obj);
    renderer->RecomputeMinClockLeadTime();
  }
}

void AudioDeviceManager::OnDeviceUnplugged(
    const fbl::RefPtr<AudioDevice>& device, zx_time_t plug_time) {
  FXL_DCHECK(device);
  FXL_DCHECK(ValidateRoutingPolicy(routing_policy_));

  // Start by checking to see if this device was the last plugged device
  // (before we update the plug state).
  bool was_last_plugged = FindLastPlugged(device->type()) == device;

  // Update the plug state of the device.  If this was not an actual change in
  // the plug state of the device, then we are done.
  if (!device->UpdatePlugState(false, plug_time)) {
    return;
  }

  // This device was just unplugged.  Unlink it from everything it is
  // currently linked to.
  device->Unlink();

  // If the device which was unplugged was not the last plugged device in the
  // system, then there has been no change in who was the last plugged device,
  // and no updates to the routing state are needed.
  if (was_last_plugged) {
    if (device->is_output()) {
      // This was an output.  If we are applying 'last plugged output' policy,
      // go over our list of renderers and link them to the most recently
      // plugged output (if any).  Then go over our list of capturers and do
      // the same for each of the loopback capturers.  Note: the current hack
      // routing policy for inputs is always 'last plugged'
      FXL_DCHECK(static_cast<AudioOutput*>(device.get()) !=
                 throttle_output_.get());

      fbl::RefPtr<AudioOutput> replacement = FindLastPluggedOutput();
      if (replacement) {
        if (routing_policy_ ==
            fuchsia::media::AudioOutputRoutingPolicy::kLastPluggedOutput) {
          for (auto& renderer : renderers_) {
            LinkOutputToRenderer(replacement.get(), &renderer);
          }
        }

        LinkToCapturers(replacement);
      }
    } else {
      // This was an input.  Find the new most recently plugged in input (if
      // any), then go over our list of capturers and link all of the
      // non-loopback capturers to the new input.
      FXL_DCHECK(device->is_input());

      fbl::RefPtr<AudioInput> replacement = FindLastPluggedInput();
      if (replacement) {
        LinkToCapturers(replacement);
      }
    }
  }

  // If the device which was removed was an output, recompute our renderers'
  // minimum lead time requirements.
  if (device->is_output()) {
    for (auto& renderer : renderers_) {
      renderer.RecomputeMinClockLeadTime();
    }
  }
}

void AudioDeviceManager::OnDevicePlugged(const fbl::RefPtr<AudioDevice>& device,
                                         zx_time_t plug_time) {
  FXL_DCHECK(device);

  // Update the plug state of the device.  If this was not an actual change in
  // the plug state of the device, then we are done.
  if (!device->UpdatePlugState(true, plug_time)) {
    return;
  }

  if (device->is_output()) {
    // This new device is an output.  Go over our list of renderers and "do
    // the right thing" based on our current routing policy.  If we are using
    // last plugged policy, replace all of the renderers current output with
    // this new one (assuming that this new one is actually the most recently
    // plugged). If we are using the "all plugged" policy, then just add this
    // new output to all of the renderers.
    //
    // Then, apply last plugged policy to all of the capturers which are in
    // loopback mode.
    fbl::RefPtr<AudioOutput> last_plugged = FindLastPluggedOutput();
    auto output = static_cast<AudioOutput*>(device.get());

    FXL_DCHECK(ValidateRoutingPolicy(routing_policy_));

    bool lp_policy =
        (routing_policy_ ==
         fuchsia::media::AudioOutputRoutingPolicy::kLastPluggedOutput);
    bool is_lp = (output == last_plugged.get());

    if (is_lp && lp_policy) {
      for (auto& unlink_tgt : devices_) {
        if (unlink_tgt.is_output() && (&unlink_tgt != device.get())) {
          unlink_tgt.UnlinkSources();
        }
      }
    }
    if (is_lp || !lp_policy) {
      for (auto& renderer : renderers_) {
        LinkOutputToRenderer(output, &renderer);

        // If we are adding a new link (regardless of whether we may or may
        // not have removed old links based on the specific active policy)
        // because of an output becoming plugged in, we need to recompute the
        // minimum clock lead time requirement, and perhaps update users as to
        // what it is supposed to be.
        //
        // TODO(johngro) : In theory, this could be optimized.  We don't
        // *technically* need to go over the entire set of links and find the
        // largest minimum lead time requirement if we know (for example) that
        // we just added a link, but didn't remove any.  Right now, we are
        // sticking to the simple approach because we know that N (the total
        // number of outputs an input is linked to) is small, and maintaining
        // optimized/specialized logic for computing this value would start to
        // become a real pain as we start to get more complicated in our
        // approach to policy based routing.
        renderer.RecomputeMinClockLeadTime();
      }
    }

    // 'loopback' capturers should listen to this output now
    if (is_lp) {
      LinkToCapturers(device);
    }
  } else {
    FXL_DCHECK(device->is_input());

    fbl::RefPtr<AudioInput> last_plugged = FindLastPluggedInput();
    auto input = static_cast<AudioInput*>(device.get());

    // non-'loopback' capturers should listen to this input now
    if (input == last_plugged.get()) {
      LinkToCapturers(device);
    }
  }
}

// New device arrived and is the most-recently-plugged.
// *If device is an output, all 'loopback' capturers should
// listen to this output going forward (default output).
// *If device is an input, then all NON-'loopback' capturers
// should listen to this input going forward (default input).
void AudioDeviceManager::LinkToCapturers(
    const fbl::RefPtr<AudioDevice>& device) {
  bool link_to_loopbacks = device->is_output();

  for (auto& capturer : capturers_) {
    if (capturer.loopback() == link_to_loopbacks) {
      capturer.UnlinkSources();
      AudioObject::LinkObjects(device, fbl::WrapRefPtr(&capturer));
    }
  }
}

}  // namespace audio
}  // namespace media
