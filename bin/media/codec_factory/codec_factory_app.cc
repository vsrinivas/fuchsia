// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_factory_app.h"

#include "codec_factory_impl.h"

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/svc/cpp/services.h>
#include <trace-provider/provider.h>
#include <zircon/device/media-codec.h>

#include <fcntl.h>

namespace codec_factory {

namespace {}  // namespace

CodecFactoryApp::CodecFactoryApp(
    std::unique_ptr<component::StartupContext> startup_context,
    async::Loop* loop)
    : startup_context_(std::move(startup_context)), loop_(loop) {
  // TODO(dustingreen): Determine if this is useful and if we're holding it
  // right.
  trace::TraceProvider trace_provider(loop->dispatcher());

  DiscoverMediaCodecDrivers();

  startup_context_->outgoing_services()->AddServiceForName(
      [this](zx::channel request) {
        // The CodecFactoryImpl is self-owned and will self-delete when the
        // channel closes or an error occurs.
        CodecFactoryImpl::CreateSelfOwned(this, startup_context_.get(),
                                          std::move(request));
      },
      fuchsia::mediacodec::CodecFactory::Name_);
}

void CodecFactoryApp::DiscoverMediaCodecDrivers() {
  // TODO(dustingreen): Enumerate/watch the /dev/class/media-codec dir for
  // devices, including across devhost failure/replacement. For the moment we
  // just open device 000, and don't try to re-open it should it fail.  The
  // DeviceWatcher class can help with this.
  constexpr char kDeviceName[] = "/dev/class/media-codec/000";

  int fd = ::open(kDeviceName, O_RDONLY);
  if (fd < 0) {
    printf("Failed to open \"%s\" (res %d errno %d)\n", kDeviceName, fd, errno);
    return;
  }

  zx::channel client_factory_channel;
  ssize_t res =
      ::fdio_ioctl(fd, MEDIA_CODEC_IOCTL_GET_CODEC_FACTORY_CHANNEL, nullptr, 0,
                   &client_factory_channel, sizeof(client_factory_channel));
  ::close(fd);

  if (res != sizeof(client_factory_channel)) {
    printf("Failed to obtain channel (res %zd)\n", res);
    // ignore/skip the driver that failed the ioctl
    return;
  }

  std::shared_ptr<fuchsia::mediacodec::CodecFactoryPtr> codec_factory =
      std::make_shared<fuchsia::mediacodec::CodecFactoryPtr>();
  codec_factory->set_error_handler([this, factory = codec_factory.get()] {
    hw_codecs_.remove_if(
        [factory](const std::unique_ptr<CodecListEntry>& entry) {
          return factory == entry->factory.get();
        });
  });

  fidl::VectorPtr<fuchsia::mediacodec::CodecDescription> driver_codec_list;
  FXL_DCHECK(!driver_codec_list);
  codec_factory->events().OnCodecList =
      [&driver_codec_list](
          fidl::VectorPtr<fuchsia::mediacodec::CodecDescription> codec_list) {
        driver_codec_list = std::move(codec_list);
      };
  codec_factory->Bind(std::move(client_factory_channel), loop_->dispatcher());
  // We _rely_ on the driver to either fail the channel or send OnCodecList().
  // We don't set a timeout here because under different conditions this could
  // take different duration.
  zx_status_t run_result;
  do {
    run_result = loop_->Run(zx::time::infinite(), true);
  } while (run_result == ZX_OK && !driver_codec_list);
  if (run_result != ZX_OK) {
    // ignore/skip the driver that failed the channel already
    // The ~codec_factory takes care of un-binding.
    return;
  }
  // We're no longer interested in OnCodecList events from the driver's
  // CodecFactory, should the driver send any more.  Sending more is not legal,
  // but disconnect this event just in case, since we don't want the old lambda
  // that touches local_codec_list.
  codec_factory->events().OnCodecList =
      [](fidl::VectorPtr<fuchsia::mediacodec::CodecDescription> codec_list) {};

  FXL_DCHECK(driver_codec_list);
  for (auto& codec_description : driver_codec_list.get()) {
    FXL_LOG(INFO) << "CodecFactory::DiscoverMediaCodecDrivers() registering:"
                  << " codec_type: " << codec_description.codec_type
                  << " mime_type: " << codec_description.mime_type;
    hw_codecs_.emplace_back(std::make_unique<CodecListEntry>(CodecListEntry{
        .description = std::move(codec_description),
        .factory = codec_factory,
    }));
  }

  printf("CodecFactory::DiscoverMediaCodecDrivers() success.\n");
}

const fuchsia::mediacodec::CodecFactoryPtr* CodecFactoryApp::FindHwDecoder(
    fit::function<bool(const fuchsia::mediacodec::CodecDescription&)>
        is_match) {
  auto iter = std::find_if(
      hw_codecs_.begin(), hw_codecs_.end(),
      [&is_match](const std::unique_ptr<CodecListEntry>& entry) -> bool {
        return is_match(entry->description);
      });
  if (iter == hw_codecs_.end()) {
    return nullptr;
  }
  return (*iter)->factory.get();
}

}  // namespace codec_factory
