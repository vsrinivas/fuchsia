// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_factory_impl.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <zircon/syscalls.h>

#include <algorithm>

#include "lib/zx/eventpair.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace {

// Isolates for SW encode/decode
//
// For HW-based codecs, we discover their "LocalCodecFactory" by watching for
// their device and sending the server end of a (local) CodecFactory to the
// driver.
const std::string kIsolateUrlFfmpeg =
    "fuchsia-pkg://fuchsia.com/codec_runner_sw_ffmpeg#meta/"
    "codec_runner_sw_ffmpeg.cmx";
const std::string kIsolateUrlSbc =
    "fuchsia-pkg://fuchsia.com/codec_runner_sw_sbc#meta/"
    "codec_runner_sw_sbc.cmx";
const std::string kIsolateUrlAac =
    "fuchsia-pkg://fuchsia.com/codec_runner_sw_aac#meta/"
    "codec_runner_sw_aac.cmx";

const std::string kIsolateRelativeUrlSbc = "#meta/codec_runner_sw_sbc.cm";
const std::string kIsolateRelativeUrlAac = "#meta/codec_runner_sw_aac.cm";
const std::string kIsolateRelativeUrlFfmpeg = "#meta/codec_runner_sw_ffmpeg.cm";

struct EncoderSupportSpec {
  std::string isolate_url;
  std::string isolate_url_v2;
  std::vector<std::string> mime_types;
  std::function<bool(const fuchsia::media::EncoderSettings&)> supports_settings;
  bool supports_mime_type(const std::string& mime_type) const {
    return std::find(mime_types.begin(), mime_types.end(), mime_type) != mime_types.end();
  }
  bool supports(const std::string& mime_type,
                const fuchsia::media::EncoderSettings& settings) const {
    return supports_mime_type(mime_type) && supports_settings(settings);
  }
};

const EncoderSupportSpec kSbcEncoderSupportSpec = {
    .isolate_url = kIsolateUrlSbc,
    .isolate_url_v2 = kIsolateRelativeUrlSbc,
    .mime_types = {"audio/pcm"},
    .supports_settings =
        [](const fuchsia::media::EncoderSettings& settings) { return settings.is_sbc(); },
};

const EncoderSupportSpec kAacEncoderSupportSpec = {
    .isolate_url = kIsolateUrlAac,
    .isolate_url_v2 = kIsolateRelativeUrlAac,
    .mime_types = {"audio/pcm"},
    .supports_settings =
        [](const fuchsia::media::EncoderSettings& settings) { return settings.is_aac(); },
};  // namespace

const EncoderSupportSpec supported_encoders[] = {kSbcEncoderSupportSpec, kAacEncoderSupportSpec};

struct DecoderSupportSpec {
  std::string isolate_url;
  std::string isolate_url_v2;
  std::vector<std::string> mime_types;
  bool supports(const std::string& mime_type) const {
    return std::find(mime_types.begin(), mime_types.end(), mime_type) != mime_types.end();
  }
};

const DecoderSupportSpec kFfmpegSupportSpec = {
    .isolate_url = kIsolateUrlFfmpeg,
    .isolate_url_v2 = kIsolateRelativeUrlFfmpeg,
    .mime_types = {"video/h264"},
};

const DecoderSupportSpec kSbcDecoderSuportSpec = {
    .isolate_url = kIsolateUrlSbc,
    .isolate_url_v2 = kIsolateRelativeUrlSbc,
    .mime_types = {"audio/sbc"},
};

const DecoderSupportSpec supported_decoders[] = {kFfmpegSupportSpec, kSbcDecoderSuportSpec};

std::optional<std::string> FindEncoder(const std::string& mime_type,
                                       const fuchsia::media::EncoderSettings& settings,
                                       bool is_v2) {
  auto encoder = std::find_if(std::begin(supported_encoders), std::end(supported_encoders),
                              [&mime_type, &settings](const EncoderSupportSpec& encoder) {
                                return encoder.supports(mime_type, settings);
                              });

  if (encoder == std::end(supported_encoders)) {
    return std::nullopt;
  }

  return {is_v2 ? encoder->isolate_url_v2 : encoder->isolate_url};
}

std::optional<std::string> FindDecoder(const std::string& mime_type, bool is_v2) {
  auto decoder = std::find_if(
      std::begin(supported_decoders), std::end(supported_decoders),
      [&mime_type](const DecoderSupportSpec& decoder) { return decoder.supports(mime_type); });

  if (decoder == std::end(supported_decoders)) {
    return std::nullopt;
  }

  return {is_v2 ? decoder->isolate_url_v2 : decoder->isolate_url};
}

void ForwardToIsolate(std::string component_url, bool is_v2,
                      sys::ComponentContext* component_context,
                      fit::function<void(fuchsia::mediacodec::CodecFactoryPtr)> connect_func) {
  if (is_v2) {
    fuchsia::sys2::ChildDecl isolate;
    uint64_t rand_num;
    zx_cprng_draw(&rand_num, sizeof rand_num);
    std::string msg = fxl::StringPrintf("isolate-%" PRIu64 "", rand_num);
    isolate.set_name(msg);
    isolate.set_url(component_url);
    isolate.set_startup(fuchsia::sys2::StartupMode::LAZY);
    isolate.set_on_terminate(fuchsia::sys2::OnTerminate::NONE);

    fuchsia::sys2::CollectionRef collection{.name = "sw-codecs"};
    fuchsia::sys2::RealmPtr realm_svc;

    fuchsia::sys2::CreateChildArgs child_args;
    child_args.set_numbered_handles(std::vector<fuchsia::process::HandleInfo>());

    component_context->svc()->Connect(realm_svc.NewRequest());
    realm_svc.set_error_handler([](zx_status_t err) {
      FX_LOGS(WARNING) << "FIDL error using fuchsia.sys2.Realm protocol: " << err;
    });

    realm_svc->CreateChild(
        collection, std::move(isolate), std::move(child_args),
        [realm_svc = std::move(realm_svc), connect_func = std::move(connect_func),
         msg = std::move(msg)](fuchsia::sys2::Realm_CreateChild_Result res) mutable {
          if (res.is_err()) {
            FX_LOGS(WARNING) << "Isolate creation request failed for " << msg;
            return;
          }
          fidl::InterfaceHandle<fuchsia::io::Directory> exposed_dir;

          fuchsia::sys2::ChildRef child = fuchsia::sys2::ChildRef{
              .name = msg,
              .collection = "sw-codecs",
          };
          realm_svc->OpenExposedDir(
              child, exposed_dir.NewRequest(),
              [realm_svc = std::move(realm_svc), exposed_dir = std::move(exposed_dir),
               connect_func = std::move(connect_func)](
                  fuchsia::sys2::Realm_OpenExposedDir_Result res) mutable {
                if (res.is_err()) {
                  FX_LOGS(WARNING) << "OpenExposedDir on isolate failed";
                  return;
                }

                fuchsia::mediacodec::CodecFactoryPtr factory_delegate;
                auto delegate_req = factory_delegate.NewRequest();
                sys::ServiceDirectory child_services(std::move(exposed_dir));
                zx_status_t connect_res = child_services.Connect(
                    std::move(delegate_req),
                    // TODO(dustingreen): Might be helpful (for debugging maybe)
                    // to change this name to distinguish these delegate
                    // CodecFactory(s) from the main CodecFactory service.
                    fuchsia::mediacodec::CodecFactory::Name_);
                if (connect_res == ZX_OK) {
                  connect_func(std::move(factory_delegate));
                } else {
                  FX_LOGS(WARNING)
                      << "Connection to isolate services failed with error code: " << connect_res;
                }
              });
        });
  } else {
    fuchsia::sys::ComponentControllerPtr component_controller;
    fidl::InterfaceHandle<fuchsia::io::Directory> directory;
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = component_url;
    launch_info.directory_request = directory.NewRequest().TakeChannel();
    fuchsia::sys::LauncherPtr launcher;
    component_context->svc()->Connect(launcher.NewRequest());
    launcher->CreateComponent(std::move(launch_info), component_controller.NewRequest());
    sys::ServiceDirectory services(std::move(directory));
    component_controller.set_error_handler([component_url](zx_status_t status) {
      FX_LOGS(ERROR) << "app_controller_ error connecting to CodecFactoryImpl of " << component_url;
    });
    fuchsia::mediacodec::CodecFactoryPtr factory_delegate;
    services.Connect(factory_delegate.NewRequest(),
                     // TODO(dustingreen): Might be helpful (for debugging maybe) to change
                     // this name to distinguish these delegate CodecFactory(s) from the main
                     // CodecFactory service.
                     fuchsia::mediacodec::CodecFactory::Name_);

    // Forward the request to the factory_delegate_ as-is.  This avoids conversion
    // to command-line parameters and back, and avoids creating a separate
    // interface definition for the delegated call.  The downside is potential
    // confusion re. why we have several implementations of CodecFactory, but we
    // can comment why.  The presently-running implementation is the main
    // implementation that clients use directly.

    // Dropping factory_delegate in here is ok; messages will be received in order
    // by the peer before they see the PEER_CLOSED event.
    connect_func(std::move(factory_delegate));

    // We don't want to be forced to keep component_controller around.  When using
    // an isolate, we trust that the ComponentController will kill the app if we
    // crash before this point, as this process crashing will kill the server side
    // of the component_controller.  If we crash after this point, we trust that
    // the isolate will receive the CreateDecoder() message sent just above, and
    // will either exit on failure to create the Codec server-side, or will exit
    // later when the client side of the Codec channel closes, or will exit later
    // when the Codec fails asynchronously in whatever way. Essentially the Codec
    // channel owns the isolate at this point, and we trust the isolate to exit
    // when the Codec channel closes.
    component_controller->Detach();
  }
}

}  // namespace

// TODO(dustingreen): Currently we assume, potentially incorrectly, that clients
// of CodecFactory won't spam CodecFactory channel creation.  Rather than trying
// to mitigate that problem locally in this class, it seems better to intergrate
// with a more general-purpose request spam mitigation mechanism.
void CodecFactoryImpl::CreateSelfOwned(
    CodecFactoryApp* app, sys::ComponentContext* component_context,
    fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> request, bool is_v2) {
  // I considered just doing "new CodecFactoryImpl(...)" here and declaring that
  // it always inherently owns itself (and implementing it that way), but that
  // seems less flexible for testing purposes and also not necessarily as safe
  // if we were to add any error cases before the Binding has taken over
  // ownership.
  //
  // As usual, can't use std::make_unique<> here since making it a friend would
  // break the point of making the constructor private.
  std::shared_ptr<CodecFactoryImpl> self(
      new CodecFactoryImpl(app, component_context, std::move(request), is_v2));
  auto* self_ptr = self.get();
  self_ptr->OwnSelf(std::move(self));
  assert(!self);
}

void CodecFactoryImpl::OwnSelf(std::shared_ptr<CodecFactoryImpl> self) { self_ = std::move(self); }

CodecFactoryImpl::CodecFactoryImpl(
    CodecFactoryApp* app, sys::ComponentContext* component_context,
    fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> request, bool is_v2)
    : app_(app),
      component_context_(component_context),
      binding_(this, std::move(request), app_->dispatcher()),
      is_v2_(is_v2) {
  binding_.set_error_handler([this](zx_status_t status) { self_.reset(); });

  // The app already has all hardware codecs loaded by the time we get to talk
  // to it, so we don't need to wait for it now.
  binding_.events().OnCodecList(app_->MakeCodecList());
}

void CodecFactoryImpl::CreateDecoder(
    fuchsia::mediacodec::CreateDecoder_Params params,
    fidl::InterfaceRequest<fuchsia::media::StreamProcessor> decoder) {
  if (!params.has_input_details()) {
    FX_LOGS(WARNING) << "missing input_details";
    return;
  }

  if (!params.input_details().has_mime_type()) {
    FX_LOGS(WARNING) << "input details missing mime type";
    // Without mime_type we cannot search for a decoder.
    return;
  }

  // We don't have any need to bind the codec_request locally to this process.
  // Instead, we find where to delegate the request to.

  if (!params.has_require_sw() || !params.require_sw()) {
    // First, try to find a hw-accelerated codec to satisfy the request.
    auto mime_type = params.input_details().mime_type();
    const fuchsia::mediacodec::CodecFactoryPtr* factory = app_->FindHwCodec(
        [mime_type = std::move(mime_type)](
            const fuchsia::mediacodec::CodecDescription& hw_codec_description) -> bool {
          // TODO(dustingreen): pay attention to the bool constraints of the
          // params vs. the hw_codec_description bools.  For the moment we just
          // match the codec_type, mime_type.
          constexpr fuchsia::mediacodec::CodecType codec_type =
              fuchsia::mediacodec::CodecType::DECODER;
          return (codec_type == hw_codec_description.codec_type) &&
                 (mime_type == hw_codec_description.mime_type);
        });
    if (factory && (!params.has_require_hw() || !params.require_hw()) && !AdmitHwDecoder(params)) {
      factory = nullptr;
    }
    if (factory) {
      // prefer HW-accelerated
      FX_LOGS(INFO) << "CreateDecoder() found HW decoder for: "
                    << params.input_details().mime_type();
      AttachLifetimeTrackingEventpairDownstream(factory);
      (*factory)->CreateDecoder(std::move(params), std::move(decoder));
      return;
    }
  }

  // This is outside the above if on purpose, in case the client specifies both require_hw and
  // require_sw, in which case we should fail.
  if (params.has_require_hw() && params.require_hw()) {
    FX_LOGS(WARNING) << "require_hw, but no matching HW decoder factory found ("
                     << params.input_details().mime_type() << "); closing";
    // TODO(dustingreen): Send epitaph when possible.
    return;
  }

  auto maybe_decoder_isolate_url = FindDecoder(params.input_details().mime_type(), is_v2_);

  if (!maybe_decoder_isolate_url) {
    FX_LOGS(WARNING) << "No decoder supports " << params.input_details().mime_type();
    return;
  }

  FX_LOGS(INFO) << "CreateDecoder() found SW decoder for: " << params.input_details().mime_type();

  ForwardToIsolate(*maybe_decoder_isolate_url, is_v2_, component_context_,
                   [self = self_, params = std::move(params), decoder = std::move(decoder)](
                       fuchsia::mediacodec::CodecFactoryPtr factory_delegate) mutable {
                     // Forward the request to the factory_delegate_ as-is. This
                     // avoids conversion to command-line parameters and back,
                     // and avoids creating a separate interface definition for
                     // the delegated call.  The downside is potential confusion
                     // re. why we have several implementations of CodecFactory,
                     // but we can comment why.  The presently-running
                     // implementation is the main implementation that clients
                     // use directly.
                     self->AttachLifetimeTrackingEventpairDownstream(&factory_delegate);
                     factory_delegate->CreateDecoder(std::move(params), std::move(decoder));
                     ZX_DEBUG_ASSERT(self->lifetime_tracking_.empty());
                   });
}

void CodecFactoryImpl::CreateEncoder(
    fuchsia::mediacodec::CreateEncoder_Params encoder_params,
    ::fidl::InterfaceRequest<fuchsia::media::StreamProcessor> encoder_request) {
  if (!encoder_params.has_input_details()) {
    FX_LOGS(WARNING) << "missing input_details";
    return;
  }

  if (!encoder_params.input_details().has_mime_type()) {
    FX_LOGS(WARNING) << "missing mime_type";
    return;
  }

  if (!encoder_params.input_details().has_encoder_settings()) {
    FX_LOGS(WARNING) << "missing encoder_settings";
    return;
  }

  // We don't have any need to bind the codec_request locally to this process.
  // Instead, we find where to delegate the request to.

  // First, try to find a hw-accelerated codec to satisfy the request.
  const fuchsia::mediacodec::CodecFactoryPtr* factory = app_->FindHwCodec(
      [&encoder_params](const fuchsia::mediacodec::CodecDescription& hw_codec_description) -> bool {
        ;
        return (fuchsia::mediacodec::CodecType::ENCODER == hw_codec_description.codec_type) &&
               (encoder_params.input_details().mime_type() == hw_codec_description.mime_type);
      });

  if (factory && !AdmitHwEncoder(encoder_params)) {
    factory = nullptr;
  }

  if (factory) {
    // prefer HW-accelerated
    AttachLifetimeTrackingEventpairDownstream(factory);
    (*factory)->CreateEncoder(std::move(encoder_params), std::move(encoder_request));
    return;
  }

  if (encoder_params.has_require_hw() && encoder_params.require_hw()) {
    FX_LOGS(WARNING) << "require_hw, but no matching HW encoder factory found ("
                     << encoder_params.input_details().mime_type() << "); closing";
    // ~encoder
    return;
  }

  auto maybe_encoder_isolate_url =
      FindEncoder(encoder_params.input_details().mime_type(),
                  encoder_params.input_details().encoder_settings(), is_v2_);

  if (!maybe_encoder_isolate_url) {
    FX_LOGS(WARNING) << "No encoder supports " << encoder_params.input_details().mime_type()
                     << " input with these settings.";
    return;
  }

  ForwardToIsolate(*maybe_encoder_isolate_url, is_v2_, component_context_,
                   [self = self_, encoder_params = std::move(encoder_params),
                    encoder_request = std::move(encoder_request)](
                       fuchsia::mediacodec::CodecFactoryPtr factory_delegate) mutable {
                     self->AttachLifetimeTrackingEventpairDownstream(&factory_delegate);
                     factory_delegate->CreateEncoder(std::move(encoder_params),
                                                     std::move(encoder_request));
                   });
}

void CodecFactoryImpl::AttachLifetimeTracking(zx::eventpair codec_end) {
  ZX_DEBUG_ASSERT(lifetime_tracking_.size() <=
                  fuchsia::mediacodec::CODEC_FACTORY_LIFETIME_TRACKING_EVENTPAIR_PER_CREATE_MAX);
  if (lifetime_tracking_.size() >=
      fuchsia::mediacodec::CODEC_FACTORY_LIFETIME_TRACKING_EVENTPAIR_PER_CREATE_MAX) {
    binding_.Close(ZX_ERR_BAD_STATE);
    // This call will delete this.
    self_.reset();
    return;
  }
  lifetime_tracking_.emplace_back(std::move(codec_end));
}

void CodecFactoryImpl::AttachLifetimeTrackingEventpairDownstream(
    const fuchsia::mediacodec::CodecFactoryPtr* factory) {
  while (!lifetime_tracking_.empty()) {
    zx::eventpair lifetime_tracking_eventpair = std::move(lifetime_tracking_.back());
    lifetime_tracking_.pop_back();
    (*factory)->AttachLifetimeTracking(std::move(lifetime_tracking_eventpair));
  }
}

bool CodecFactoryImpl::AdmitHwDecoder(const fuchsia::mediacodec::CreateDecoder_Params& params) {
  std::vector<zx::eventpair> lifetime_eventpairs;
  if (app_->policy().AdmitHwDecoder(params, &lifetime_eventpairs)) {
    for (auto& eventpair : lifetime_eventpairs) {
      lifetime_tracking_.emplace_back(std::move(eventpair));
    }
    return true;
  } else {
    return false;
  }
}

bool CodecFactoryImpl::AdmitHwEncoder(const fuchsia::mediacodec::CreateEncoder_Params& params) {
  std::vector<zx::eventpair> lifetime_eventpairs;
  if (app_->policy().AdmitHwEncoder(params, &lifetime_eventpairs)) {
    for (auto& eventpair : lifetime_eventpairs) {
      lifetime_tracking_.emplace_back(std::move(eventpair));
    }
    return true;
  } else {
    return false;
  }
}
