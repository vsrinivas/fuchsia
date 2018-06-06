// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_factory_impl.h"

#include "lib/svc/cpp/services.h"

namespace {

// TODO(dustingreen): Other types of isolates can exist.  For some codecs we may
// not use an isolate and instead delegate to the client end of a CodecFactory
// instance that we got via other means (not by starting an isolate, but via a
// factory registration initiated by a driver process, or by discovering a
// device, or similar).
const std::string kIsolateUrlOmx = "codec_runner_sw_omx";

}  // namespace

namespace codec_factory {

// TODO(dustingreen): Currently we assume, potentially incorrectly, that clients
// of CodecFactory won't spam CodecFactory channel creation.  Rather than trying
// to mitigate that problem locally in this class, it seems better to intergrate
// with a more general-purpose request spam mitigation mechanism.
void CodecFactoryImpl::CreateSelfOwned(
    fuchsia::sys::StartupContext* startup_context, zx::channel request) {
  // I considered just doing "new CodecFactoryImpl(...)" here and declaring that
  // it always inherently owns itself (and implementing it that way), but that
  // seems less flexible for testing purposes and also not necessarily as safe
  // if we were to add any error cases before the Binding has taken over
  // ownership.
  //
  // As usual, can't use std::make_unique<> here since making it a friend would
  // break the point of making the constructor private.
  std::unique_ptr<CodecFactoryImpl> self(
      new CodecFactoryImpl(startup_context, std::move(request)));
  self->OwnSelf(std::move(self));
  assert(!self);
}

CodecFactoryImpl::CodecFactoryImpl(
    fuchsia::sys::StartupContext* startup_context, zx::channel channel)
    : startup_context_(startup_context),
      channel_temp_(std::move(channel)),
      current_request_(kRequest_None) {}

// TODO(dustingreen): Seems simpler to avoid channel_temp_ and OwnSelf() and
// just have CreateSelfOwned() directly create the binding.
void CodecFactoryImpl::OwnSelf(std::unique_ptr<CodecFactoryImpl> self) {
  binding_ =
      std::make_unique<BindingType>(std::move(self), std::move(channel_temp_));
}

void CodecFactoryImpl::CreateAudioDecoder_Begin_Params(
    fuchsia::mediacodec::CreateAudioDecoder_Params params1) {
  if (params_CreateAudioDecoder_ || current_request_ != kRequest_None) {
    // TODO(dustingreen): Send Epitaph when possible.  Probably separate the
    // state checks to be able to give better string in the epitaph.  Same goes
    // for other instances of binding_.reset();
    binding_.reset();
    return;
  }
  // This is a copy of some parts of the struct and a move of other parts...
  params_CreateAudioDecoder_ =
      std::make_unique<fuchsia::mediacodec::CreateAudioDecoder_Params>(
          std::move(params1));
  current_request_ = kRequest_CreateAudioDecoder;
}

void CodecFactoryImpl::CreateAudioDecoder_Go(
    ::fidl::InterfaceRequest<fuchsia::mediacodec::Codec> audio_decoder) {
  if (!params_CreateAudioDecoder_ ||
      current_request_ != kRequest_CreateAudioDecoder) {
    binding_.reset();
    return;
  }
  // Set current_request_ to kRequest_None early since we're on a single thread
  // anyway and this is just for enforcing request burst rules.
  current_request_ = kRequest_None;
  std::unique_ptr<fuchsia::mediacodec::CreateAudioDecoder_Params> params =
      std::move(params_CreateAudioDecoder_);

  // We don't have any need to bind the codec_request locally to this process.
  // Instead, we find where to delegate the request to.

  // For now, we always forward to a kIsolateUrlOmx app instance that we create
  // here.
  fuchsia::sys::ComponentControllerPtr component_controller;
  fuchsia::sys::Services services;
  fuchsia::sys::LaunchInfo launch_info{};
  std::string url = kIsolateUrlOmx;
  launch_info.url = url;
  launch_info.directory_request = services.NewRequest();
  startup_context_->launcher()->CreateComponent(
      std::move(launch_info), component_controller.NewRequest());
  component_controller.set_error_handler([url] {
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
  //
  // TODO(dustingreen): The CodecFactory will need to know (or detect) what
  // version interface can be handled by a given codec, and potentially convert
  // the incoming request down to an older version, or if not possible, refuse
  // to pick that codec.  In this context, older version can mean not calling a
  // method to set an optional non-critical hint parameter if it's not
  // implemented by the codec, or not selecting that codec if a critical
  // parameter method is not implemented by that codec and the client did use
  // that critical parameter method.  A critical parameter method can be a
  // _Begin_ method that's newer than the codec and has some critical fields in
  // it, or can be an optional but critical-if-used method that the codec server
  // doesn't implement.  Or we could just rip this message burst stuff out and
  // assume versions will always match for now, or add a better way to FIDL
  // itself.  If we want to handle versions, the factory will typically want to
  // already know what interface version each codec is at, to avoid false
  // starts.  Most likely a codec will only be expected to implement the latest
  // version, not old deprecated versions, and the CodecFactory will be
  // responsible for converting to the codec's implemented version.  The only
  // case that doesn't handle is a codec that's too new for the OS, but a codec
  // could be built targetting an older OS version and be usable from that older
  // version forward (unless a client demands a new critical thing that the old
  // version can't do), or could choose to implement an old version _and_ the
  // newest version at the time it is written/updated.
  //
  // Send the message burst of this request.
  factory_delegate->CreateAudioDecoder_Begin_Params(std::move(*params));
  factory_delegate->CreateAudioDecoder_Go(std::move(audio_decoder));

  // We don't want to be forced to keep app_controller around.  When using an
  // isolate, we trust that the ApplicationController will kill the app if we
  // crash before this point, as this process crashing will kill the server
  // side of the app_controller.  If we crash after this point, we trust that
  // the isolate will receive the CreateAudioDecoder() message sent just above,
  // and will either exit on failure to create the Codec server-side, or will
  // exit later when the client side of the Codec channel closes, or will exit
  // later when the Codec fails asynchronously in whatever way.  Essentially the
  // Codec channel owns the isolate at this point, and we trust the isolate to
  // exit when the Codec channel closes.
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

}  // namespace codec_factory
