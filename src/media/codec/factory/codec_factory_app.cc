// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_factory_app.h"

#include <fuchsia/hardware/mediacodec/cpp/fidl.h>
#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/svc/cpp/services.h>
#include <zircon/status.h>

#include <trace-provider/provider.h>

#include "codec_factory_impl.h"
#include "src/lib/fsl/io/device_watcher.h"

namespace codec_factory {

namespace {

constexpr char kDeviceClass[] = "/dev/class/media-codec";

}  // namespace

CodecFactoryApp::CodecFactoryApp(async::Loop* loop) : loop_(loop) {
  // TODO(dustingreen): Determine if this is useful and if we're holding it
  // right.
  trace::TraceProviderWithFdio trace_provider(loop_->dispatcher());

  // We pump |loop| in here, so it's important that
  // sys::ComponentContext::Create() happen after
  // DiscoverMediaCodecDrivers(), else the pumping of the loop will drop the
  // incoming request for CodecFactory before AddPublicService() below has had
  // a chance to register for it.
  DiscoverMediaCodecDriversAndListenForMoreAsync();

  // We _rely_ on the driver to either fail the channel or send OnCodecList().
  // We don't set a timeout here because under different conditions this could
  // take different duration.
  zx_status_t run_result;
  do {
    run_result = loop_->Run(zx::time::infinite(), true);
  } while (run_result == ZX_OK && !existing_devices_discovered_);
  if (run_result != ZX_OK) {
    // ignore/skip the driver that failed the channel already
    // The ~codec_factory takes care of un-binding.
    FX_LOGS(ERROR) << "loop failed: " << zx_status_get_string(run_result);
    return;
  }

  // We delay doing this until we're completely ready to add services, because
  // Create() binds to |loop| implicitly, so we don't want any
  // pumping of |loop| up to this point to drop service connection requests.
  //
  // It's fine that AddPublicService() happens after Create()
  // only because |loop| doesn't have a separate thread, and the current thread
  // won't pump |loop| until after AddPublicService() is also done.
  startup_context_ = sys::ComponentContext::Create();
  startup_context_->outgoing()->AddPublicService<fuchsia::mediacodec::CodecFactory>(
      [this](fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> request) {
        // The CodecFactoryImpl is self-owned and will self-delete when the
        // channel closes or an error occurs.
        CodecFactoryImpl::CreateSelfOwned(this, startup_context_.get(), std::move(request));
      });
}

const fuchsia::mediacodec::CodecFactoryPtr* CodecFactoryApp::FindHwDecoder(
    fit::function<bool(const fuchsia::mediacodec::CodecDescription&)> is_match) {
  auto iter = std::find_if(hw_codecs_.begin(), hw_codecs_.end(),
                           [&is_match](const std::unique_ptr<CodecListEntry>& entry) -> bool {
                             return is_match(entry->description);
                           });
  if (iter == hw_codecs_.end()) {
    return nullptr;
  }
  return (*iter)->factory.get();
}

void CodecFactoryApp::DiscoverMediaCodecDriversAndListenForMoreAsync() {
  // We use fsl::DeviceWatcher::CreateWithIdleCallback() instead of
  // fsl::DeviceWatcher::Create() because the CodecFactory service is started on
  // demand, and we don't want to start serving CodecFactory until we've
  // discovered and processed all existing media-codec devices.  That way, the
  // first time a client requests a HW-backed codec, we robustly consider all
  // codecs provided by pre-existing devices.  The request for a HW-backed Codec
  // will have a much higher probability of succeeding vs. if we just discovered
  // pre-existing devices async.  This doesn't prevent the possiblity that the
  // device might not exist at the moment the CodecFactory is started, but as
  // long as the device does exist by then, this will ensure the device's codecs
  // are considered, including for the first client request.
  device_watcher_ = fsl::DeviceWatcher::CreateWithIdleCallback(
      kDeviceClass,
      [this](int dir_fd, std::string filename) {
        std::string device_path = std::string(kDeviceClass) + "/" + filename;
        zx::channel device_channel, device_remote;
        zx_status_t status = zx::channel::create(0, &device_channel, &device_remote);
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "Failed to create channel - status: " << status;
          return;
        }
        zx::channel client_factory_channel, client_factory_remote;
        status = zx::channel::create(0, &client_factory_channel, &client_factory_remote);
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "Failed to create channel - status: " << status;
          return;
        }

        status = fdio_service_connect(device_path.c_str(), device_remote.release());
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "Failed to connect to device by filename -"
                         << " status: " << status << " device_path: " << device_path;
          return;
        }

        fuchsia::hardware::mediacodec::DevicePtr device_interface;
        status = device_interface.Bind(std::move(device_channel));
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "Failed to bind to interface -"
                         << " status: " << status << " device_path: " << device_path;
          return;
        }
        device_interface->GetCodecFactory(std::move(client_factory_remote));

        // From here on in the current lambda, we're doing stuff that can't fail
        // here locally (at least, not without exiting the whole process).  The
        // error handler will handle channel error async.

        auto discovery_entry = std::make_unique<DeviceDiscoveryEntry>();
        discovery_entry->device_path = device_path;

        discovery_entry->codec_factory = std::make_shared<fuchsia::mediacodec::CodecFactoryPtr>();
        discovery_entry->codec_factory->set_error_handler(
            [this, device_path,
             factory = discovery_entry->codec_factory.get()](zx_status_t status) {
              // Any given factory won't be in both lists, but will be in one or
              // the other by the time this error handler runs.
              device_discovery_queue_.remove_if(
                  [factory](const std::unique_ptr<DeviceDiscoveryEntry>& entry) {
                    return factory == entry->codec_factory.get();
                  });
              // Perhaps the removed discovery item was the first item in the
              // list; maybe now the new first item in the list can be
              // processed.
              PostDiscoveryQueueProcessing();

              hw_codecs_.remove_if([factory](const std::unique_ptr<CodecListEntry>& entry) {
                return factory == entry->factory.get();
              });
            });

        discovery_entry->codec_factory->events().OnCodecList =
            [this, discovery_entry = discovery_entry.get()](
                std::vector<fuchsia::mediacodec::CodecDescription> codec_list) {
              discovery_entry->driver_codec_list = fidl::VectorPtr(codec_list);
              // In case discovery_entry is the first item which is now ready to
              // process, process the discovery queue.
              PostDiscoveryQueueProcessing();

              // We're no longer interested in OnCodecList events from the
              // driver's CodecFactory, should the driver send any more. Sending
              // more is not legal, but disconnect this event just in case,
              // since we don't want the old lambda that touches
              // driver_codec_list (this lambda).
              discovery_entry->codec_factory->events().OnCodecList = nullptr;
            };

        discovery_entry->codec_factory->Bind(std::move(client_factory_channel),
                                             loop_->dispatcher());

        device_discovery_queue_.emplace_back(std::move(discovery_entry));
      },
      [this] {
        // The idle_callback indicates that all pre-existing devices have been
        // seen, and by the time this item reaches the front of the discovery
        // queue, all pre-existing devices have all been processed.
        device_discovery_queue_.emplace_back(std::make_unique<DeviceDiscoveryEntry>());
        PostDiscoveryQueueProcessing();
      });
}

void CodecFactoryApp::PostDiscoveryQueueProcessing() {
  async::PostTask(loop_->dispatcher(),
                  fit::bind_member(this, &CodecFactoryApp::ProcessDiscoveryQueue));
}

void CodecFactoryApp::ProcessDiscoveryQueue() {
  // Both startup and steady-state use this processing loop.
  //
  // In startup, we care about ordering of the discovery queue because we want
  // to allow serving of CodecFactory as soon as all pre-existing devices are
  // done processing.  We care that pre-existing devices are before
  // newly-discovered devices in the queue.  As far as startup is concerned,
  // there are other ways we could track this without using a queue, but a queue
  // works, and using the queue allows startup to share code with steady-state.
  //
  // In steady-state, we care (a little) about ordering of the discovery queue
  // because we want (to a limited degree, for now) to prefer a
  // more-recently-discovered device over a less-recently-discovered device (for
  // now at least), so to make that robust, we preserve the device discovery
  // order through the codec discovery sequence, to account for the possibility
  // that an previously discovered device may have only just recently sent
  // OnCodecList before failing; without the device_discovery_queue_ that
  // previously-discovered device's OnCodecList could re-order vs. the
  // replacement device's OnCodecList.
  //
  // The device_discovery_queue_ marginally increases the odds of a client
  // request picking up a replacement devhost instead of an old devhost that
  // failed quickly and which we haven't yet noticed is gone.  This devhost
  // replacement case is the main motivation for caring about the device
  // discovery order in the first place (at least for now), since it should be
  // robustly the case that discovery of the old devhost happens before
  // discovery of the replacement devhost.
  //
  // The ordering of the hw_codec_ list is the main way in which
  // more-recently-discovered codecs are prefered over less-recently-discovered
  // codecs.  The device_discovery_queue_ just makes the hw_codec_ ordering
  // exactly correspond to the device discovery order (reversed) even when
  // devices are discovered near each other in time.
  //
  // None of this changes the fact that a replacement devhost's arrival can race
  // with a client's request, so if a devhost fails and is replaced, it's quite
  // possible the client will see the Codec interface just fail.  Even if this
  // were mitigated, it wouldn't change the fact that a devhost failure later
  // would result in Codec interface failure at that time, so failures near the
  // start aren't really much different than async failures later. It can make
  // sense for a client to retry a low number of times (if the client wants to
  // work despite a devhost not always fully working), even if the Codec failure
  // happens quite early.
  while (!device_discovery_queue_.empty()) {
    std::unique_ptr<DeviceDiscoveryEntry>& front = device_discovery_queue_.front();
    if (!front->codec_factory) {
      // All pre-existing devices have been processed.
      //
      // Now the CodecFactory can begin serving (shortly).
      existing_devices_discovered_ = true;
      // The marker has done its job, so remove the marker.
      device_discovery_queue_.pop_front();
      return;
    }

    if (!front->driver_codec_list) {
      // The first item is not yet ready.  The current method will get re-posted
      // when the first item is potentially ready.
      return;
    }
    FX_DCHECK(front->driver_codec_list.has_value());

    for (auto& codec_description : front->driver_codec_list.value()) {
      FX_LOGS(INFO) << "registering - codec_type: "
                    << fidl::ToUnderlying(codec_description.codec_type)
                    << " mime_type: " << codec_description.mime_type
                    << " device_path: " << front->device_path;
      hw_codecs_.emplace_front(std::make_unique<CodecListEntry>(CodecListEntry{
          .description = std::move(codec_description),
          // shared_ptr<>
          .factory = front->codec_factory,
      }));
    }

    device_discovery_queue_.pop_front();
  }
}

}  // namespace codec_factory
