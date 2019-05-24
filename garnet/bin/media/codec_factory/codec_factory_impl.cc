// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_factory_impl.h"

#include "lib/svc/cpp/services.h"

namespace {

// Other types of SW isolates can exist, but at the moment we only have one,
// which uses ffmpeg for SW decode (or potentially encode).
//
// For HW-based codecs, we discover their "LocalCodecFactory" by watching for
// their device and sending the server end of a (local) CodecFactory to the
// driver.
const std::string kIsolateUrlFfmpeg =
    "fuchsia-pkg://fuchsia.com/codec_runner_sw_ffmpeg#meta/"
    "codec_runner_sw_ffmpeg.cmx";

// TODO(turnage): Devise a better routing system between SW-based codec
// factories.  Using this should be fine for now since this is the first/only
// type that we use ffmpeg for and we don't currently have any other SW-based
// codecs.
const std::string kFfmpegMimeType = "video/h264";

}  // namespace

namespace codec_factory {

// TODO(dustingreen): Currently we assume, potentially incorrectly, that clients
// of CodecFactory won't spam CodecFactory channel creation.  Rather than trying
// to mitigate that problem locally in this class, it seems better to intergrate
// with a more general-purpose request spam mitigation mechanism.
void CodecFactoryImpl::CreateSelfOwned(
    CodecFactoryApp* app, component::StartupContext* startup_context,
    zx::channel request) {
  // I considered just doing "new CodecFactoryImpl(...)" here and declaring that
  // it always inherently owns itself (and implementing it that way), but that
  // seems less flexible for testing purposes and also not necessarily as safe
  // if we were to add any error cases before the Binding has taken over
  // ownership.
  //
  // As usual, can't use std::make_unique<> here since making it a friend would
  // break the point of making the constructor private.
  std::unique_ptr<CodecFactoryImpl> self(
      new CodecFactoryImpl(app, startup_context, std::move(request)));
  auto* self_ptr = self.get();
  self_ptr->OwnSelf(std::move(self));
  assert(!self);
}

CodecFactoryImpl::CodecFactoryImpl(CodecFactoryApp* app,
                                   component::StartupContext* startup_context,
                                   zx::channel channel)
    : app_(app),
      startup_context_(startup_context),
      channel_temp_(std::move(channel)) {}

// TODO(dustingreen): Seems simpler to avoid channel_temp_ and OwnSelf() and
// just have CreateSelfOwned() directly create the binding.
void CodecFactoryImpl::OwnSelf(std::unique_ptr<CodecFactoryImpl> self) {
  binding_ = std::make_unique<BindingType>(
      std::move(self), std::move(channel_temp_), app_->loop()->dispatcher());
  binding_->set_error_handler([this](zx_status_t status) {
    // this will also ~this
    binding_.reset();
  });
}

void CodecFactoryImpl::CreateDecoder(
    fuchsia::mediacodec::CreateDecoder_Params params,
    fidl::InterfaceRequest<fuchsia::media::StreamProcessor> decoder) {
  if (!params.has_input_details()) {
    FXL_LOG(WARNING) << "missing input_details";
    return;
  }

  if (!params.input_details().has_mime_type()) {
    FXL_LOG(WARNING) << "input details missing mime type";
    // Without mime_type we cannot search for a decoder.
    return;
  }

  // We don't have any need to bind the codec_request locally to this process.
  // Instead, we find where to delegate the request to.

  // First, try to find a hw-accelerated codec to satisfy the request.
  const fuchsia::mediacodec::CodecFactoryPtr* factory =
      app_->FindHwDecoder([&params](const fuchsia::mediacodec::CodecDescription&
                                        hw_codec_description) -> bool {
        // TODO(dustingreen): pay attention to the bool constraints of the
        // params vs. the hw_codec_description bools.  For the moment we just
        // match the codec_type, mime_type.
        constexpr fuchsia::mediacodec::CodecType codec_type =
            fuchsia::mediacodec::CodecType::DECODER;
        return (codec_type == hw_codec_description.codec_type) &&
               (params.input_details().mime_type() ==
                hw_codec_description.mime_type);
      });
  if (factory) {
    // prefer HW-accelerated
    (*factory)->CreateDecoder(std::move(params), std::move(decoder));
    return;
  }

  if (params.has_require_hw() && params.require_hw()) {
    FXL_LOG(WARNING) << "require_hw, but no matching HW decoder factory found ("
                     << params.input_details().mime_type() << "); closing";
    // TODO(dustingreen): Send epitaph when possible.
    // ~decoder
    return;
  }

  fuchsia::sys::ComponentControllerPtr component_controller;
  component::Services services;
  fuchsia::sys::LaunchInfo launch_info{};
  std::string url;
  if (params.input_details().mime_type() == kFfmpegMimeType) {
    url = kIsolateUrlFfmpeg;
  } else {
    // ~decoder
    return;
  }
  launch_info.url = url;
  launch_info.directory_request = services.NewRequest();
  startup_context_->launcher()->CreateComponent(
      std::move(launch_info), component_controller.NewRequest());
  component_controller.set_error_handler([url](zx_status_t status) {
    FXL_LOG(ERROR) << "app_controller_ error connecting to CodecFactoryImpl of "
                   << url;
  });
  fuchsia::mediacodec::CodecFactoryPtr factory_delegate;
  services.ConnectToService(
      factory_delegate.NewRequest().TakeChannel(),
      // TODO(dustingreen): Might be helpful (for debugging maybe) to change
      // this name to distinguish these delegate CodecFactory(s) from the main
      // CodecFactory service.
      CodecFactory::Name_);

  // Forward the request to the factory_delegate_ as-is.  This avoids conversion
  // to command-line parameters and back, and avoids creating a separate
  // interface definition for the delegated call.  The downside is potential
  // confusion re. why we have several implementations of CodecFactory, but we
  // can comment why.  The presently-running implementation is the main
  // implementation that clients use directly.
  factory_delegate->CreateDecoder(std::move(params), std::move(decoder));

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
  //
  // TODO(dustingreen): Double-check the above description with someone who is
  // likely to be more sure that this is plausible and reasonable for now.
  component_controller->Detach();

  // TODO(dustingreen): Determine if ~factory_delegate occurring immediately at
  // the end of this method is completely ok - that the CreateAudioDecoder()
  // message will be sent and delivered strictly in-order with respect to the
  // ~factory_delegate channel closure.  Seems like it plausibly _should_ be
  // fine, but make sure.
}

void CodecFactoryImpl::CreateEncoder(
    fuchsia::mediacodec::CreateEncoder_Params encoder_params,
    fidl::InterfaceRequest<fuchsia::media::StreamProcessor> encoder_request) {
  // We have no encoders to provide.
  // ~encoder_request
}

}  // namespace codec_factory
