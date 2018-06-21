// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODEC_FACTORY_CODEC_FACTORY_IMPL_H_
#define GARNET_BIN_MEDIA_CODEC_FACTORY_CODEC_FACTORY_IMPL_H_

#include "codec_factory_app.h"

#include <fuchsia/mediacodec/cpp/fidl.h>

#include <lib/app/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fsl/tasks/message_loop.h>

namespace codec_factory {

// There's an instance of CodecFactoryImpl per interface instance, to allow the
// implementation of this class to be stateful.
class CodecFactoryImpl : public fuchsia::mediacodec::CodecFactory {
 public:
  static void CreateSelfOwned(CodecFactoryApp* app,
                              fuchsia::sys::StartupContext* startup_context,
                              zx::channel request);

  // See .fidl file comments.
  void CreateDecoder(
      fuchsia::mediacodec::CreateDecoder_Params params,
      ::fidl::InterfaceRequest<fuchsia::mediacodec::Codec> decoder) override;

 private:
  CodecFactoryImpl(CodecFactoryApp* app,
                   fuchsia::sys::StartupContext* startup_context,
                   zx::channel channel);
  void OwnSelf(std::unique_ptr<CodecFactoryImpl> self);

  // We don't have a lock_ in here - we rely on FIDL message dispatch being
  // one-at-a-time.

  // This class doesn't own these pointers - the creator of CodecFactoryImpl
  // must ensure these outlast this instance of CodecFactoryImpl.
  CodecFactoryApp* app_ = nullptr;
  fuchsia::sys::StartupContext* startup_context_ = nullptr;
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
};

}  // namespace codec_factory

#endif  // GARNET_BIN_MEDIA_CODEC_FACTORY_CODEC_FACTORY_IMPL_H_
