// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODEC_FACTORY_CODEC_FACTORY_IMPL_H_
#define GARNET_BIN_MEDIA_CODEC_FACTORY_CODEC_FACTORY_IMPL_H_

#include <fuchsia/mediacodec/cpp/fidl.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fsl/tasks/message_loop.h"

namespace codec_factory {

// There's an instance of CodecFactoryImpl per interface instance, to allow the
// implementation of this class to be stateful, since that makes it possible to
// evolve the interface in future, by permitting multiple steps to set up
// requirements, followed by another call to actually get a codec that meets
// those requirements.
class CodecFactoryImpl : public fuchsia::mediacodec::CodecFactory {
 public:
  static void CreateSelfOwned(fuchsia::sys::StartupContext* startup_context,
                              zx::channel request);

  // See .fidl file comments.
  void CreateAudioDecoder_Begin_Params(
      fuchsia::mediacodec::CreateAudioDecoder_Params params) override;
  void CreateAudioDecoder_Go(
      ::fidl::InterfaceRequest<fuchsia::mediacodec::Codec> audio_decoder)
      override;

 private:
  CodecFactoryImpl(fuchsia::sys::StartupContext* startup_context,
                   zx::channel channel);
  void OwnSelf(std::unique_ptr<CodecFactoryImpl> self);

  // We don't have a lock_ in here - we rely on FIDL message dispatch being
  // one-at-a-time.

  // This class doesn't own these pointers - the creator of CodecFactoryImpl
  // must ensure these outlast this instance of CodecFactoryImpl.
  fuchsia::sys::StartupContext* startup_context_;
  // This is only holding the underlying channel between construction and
  // OwnSelf(), at which point the channel moves into the binding.
  zx::channel channel_temp_;

  // The CodecFactoryImpl is essentially self-owned via this member.  If we need
  // to self-destruct we can reset this binding_ unique_ptr which will delete
  // the binding which will delete the CodecFactoryImpl owned by binding_.
  // Similarly if the channel closes, the binding will drop the unique_ptr to
  // the CodecFactory which will delete the factory and binding.
  //
  // TODO(dustingreen): Cover both cases mentioned above.  May have to punt a
  // stage of deletion to the async::Loop perhaps if this doesn't work.
  typedef fidl::Binding<fuchsia::mediacodec::CodecFactory,
                        std::unique_ptr<CodecFactoryImpl>>
      BindingType;
  std::unique_ptr<BindingType> binding_;

  // This exists to enforce that each request's message burst is separate from
  // other requests' message bursts.
  enum CurrentRequest {
    kRequest_None,
    kRequest_CreateAudioDecoder,
  };
  CurrentRequest current_request_;

  // If we add a newer separate parameter set like "Params2", we'll need to also
  // verify that the client is staying within a single parameter set for a
  // request, not mixing parameter sets.

  std::unique_ptr<fuchsia::mediacodec::CreateAudioDecoder_Params>
      params_CreateAudioDecoder_;
};

}  // namespace codec_factory

#endif  // GARNET_BIN_MEDIA_CODEC_FACTORY_CODEC_FACTORY_IMPL_H_
