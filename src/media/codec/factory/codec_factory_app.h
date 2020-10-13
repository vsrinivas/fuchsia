// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_APP_H_
#define SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_APP_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <list>
#include <memory>

#include "src/lib/fsl/io/device_watcher.h"

namespace codec_factory {

// CodecFactoryApp is singleton per-process.
class CodecFactoryApp {
 public:
  CodecFactoryApp(async_dispatcher_t* dispatcher);

  // The caller must only call this on the FIDL thread, and the returned * is
  // only valid for use until the caller returns from the caller's work on the
  // FIDL thread.  The caller must not stash the returned * beyond the caller's
  // return from the caller's work on the FIDL thread, as the next item of work
  // on the FIDL thread could ~CodecFactoryPtr or similar.
  //
  // This method can return nullptr if a HW decoder isn't found...
  const fuchsia::mediacodec::CodecFactoryPtr* FindHwCodec(
      fit::function<bool(const fuchsia::mediacodec::CodecDescription&)> is_match);

  std::vector<fuchsia::mediacodec::CodecDescription> MakeCodecList() const;

  async_dispatcher_t* dispatcher() { return dispatcher_; }

 private:
  struct CodecListEntry {
    fuchsia::mediacodec::CodecDescription description;

    // When a HW-accelerated CodecFactory supports more than one sort of codec,
    // the CodecFactory will have multiple entries that share the CodecFactory
    // via the shared_ptr<> here.  The relevant entries co-own the
    // CodecFactoryPtr, and a shared_ptr<> ref is only transiently held by any
    // other code (not posted; not sent across threads).
    std::shared_ptr<fuchsia::mediacodec::CodecFactoryPtr> factory;
  };

  void DiscoverMediaCodecDriversAndListenForMoreAsync();

  void PublishService();
  void PostDiscoveryQueueProcessing();
  void ProcessDiscoveryQueue();

  std::unique_ptr<sys::ComponentContext> startup_context_;

  async_dispatcher_t* dispatcher_;

  // We don't keep a fidl::BindingSet<> here, as we want each CodecFactory
  // instance to delete itself if an error occurs on its channel.
  //
  // The App layer is just here to create CodecFactory instances, each
  // independently bound to its own channel using a std::unique_ptr ImplPtr so
  // that if the channel closes, the CodecFactory instance will go away.  And
  // if the CodecFactory instance wants to self-destruct, it can delete the
  // binding, which will close the channel and delete the CodecFactory.
  // This is true despite comments in the binding code that constantly say how
  // ImplPtr isn't taking ownership; as long as we use std::unique_ptr as
  // ImplPtr it actually will take ownership.
  //
  // We create a new instance of CodecFactory for each interface request,
  // because CodecFactory's implementation isn't stateless, by design, for
  // more plausible interface evolution over time.

  // This maps from mime type to hw-based (driver-based) codec factory.  For
  // now, the first driver discovered that supports decoding a given mime type
  // will be chosen to decode that mime type, with an optional fallback to SW if
  // no driver supports the requested mime type.
  //
  // We rely on each create request being self-contained in the CodecFactory
  // interface.
  //
  // For now, items are added at the end of this list as codecs are discovered,
  // removed as channel failure is detected, and when looking for a HW codec the
  // first matching item in the list is selected, if any.
  //
  // This is only read or written from the main FIDL thread.
  //
  // As new devices are discovered, their codecs go at the start of the list and
  // will be used in favor of previously-discovered devices.  If an old device
  // devhost exits, its entry will be eventually removed from this list thanks
  // to that device's local CodecFactory channel closing.
  //
  // This list is ordered by reverse discovery order.
  std::list<std::unique_ptr<CodecListEntry>> hw_codecs_;

  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;

  // This queue is to ensure we process discovered devices in the order
  // discovered, so that devices discovered later take priority over devices
  // discovered earlier.  We can be concurrently waiting for more than one
  // device's codec list, but we won't add a device's codec descriptions to
  // hw_codecs_ until temporally after all previously-discovered devices.
  //
  // This list is ordered by discovery order.
  struct DeviceDiscoveryEntry {
    // !driver_codec_list until OnCodecList() has been seen from the
    // codec_factory.
    fidl::VectorPtr<fuchsia::mediacodec::CodecDescription> driver_codec_list;

    // We don't really need a shared_ptr<> until hw_codecs_ (to allow it to be
    // just a flat list).  However, using a shared_ptr<> here seems more
    // readable than using unique_ptr<> here, especially given that
    // fuchsia::mediacodec::CodecFactoryPtr is very similar to a unique_ptr<>
    // itself.
    std::shared_ptr<fuchsia::mediacodec::CodecFactoryPtr> codec_factory;

    // Purely as FYI for log output.
    std::string device_path;
  };
  std::list<std::unique_ptr<DeviceDiscoveryEntry>> device_discovery_queue_;
  bool existing_devices_discovered_ = false;

  sys::OutgoingDirectory outgoing_codec_aux_service_directory_parent_;
  vfs::PseudoDir* outgoing_codec_aux_service_directory_ = nullptr;

  FXL_DISALLOW_COPY_AND_ASSIGN(CodecFactoryApp);
};

}  // namespace codec_factory

#endif  // SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_APP_H_
