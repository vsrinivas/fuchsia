// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_IMPL_H_
#define SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_IMPL_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

#include "codec_factory_app.h"

namespace codec_factory {

// There's an instance of CodecFactoryImpl per interface instance, to allow the
// implementation of this class to be stateful.
class CodecFactoryImpl : public fuchsia::mediacodec::CodecFactory {
 public:
  static void CreateSelfOwned(CodecFactoryApp* app, sys::ComponentContext* component_context,
                              fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> request);

  // See .fidl file comments.
  void CreateDecoder(fuchsia::mediacodec::CreateDecoder_Params params,
                     fidl::InterfaceRequest<fuchsia::media::StreamProcessor> decoder) override;

  virtual void CreateEncoder(
      fuchsia::mediacodec::CreateEncoder_Params encoder_params,
      fidl::InterfaceRequest<fuchsia::media::StreamProcessor> encoder_request) override;

 private:
  CodecFactoryImpl(CodecFactoryApp* app, sys::ComponentContext* component_context,
                   fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> request);
  void OwnSelf(std::unique_ptr<CodecFactoryImpl> self);

  // We don't have a lock_ in here - we rely on FIDL message dispatch being
  // one-at-a-time.

  // This class doesn't own these pointers - the creator of CodecFactoryImpl
  // must ensure these outlast this instance of CodecFactoryImpl.
  CodecFactoryApp* app_ = nullptr;
  sys::ComponentContext* component_context_ = nullptr;
  // This is only holding the underlying channel between construction and
  // OwnSelf(), at which point the channel moves into the binding.
  fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> request_temp_;

  // The CodecFactoryImpl is essentially self-owned via this member.  If we need
  // to self-destruct we can reset this binding_ unique_ptr which will delete
  // the binding which will delete the CodecFactoryImpl owned by binding_.
  // Similarly if the channel closes, the binding will drop the unique_ptr to
  // the CodecFactory which will delete the factory and binding.
  //
  // TODO(dustingreen): Cover both cases mentioned above.  May have to punt a
  // stage of deletion to the async::Loop perhaps if this doesn't work.
  using BindingType =
      fidl::Binding<fuchsia::mediacodec::CodecFactory, std::unique_ptr<CodecFactoryImpl>>;
  std::unique_ptr<BindingType> binding_;
};

}  // namespace codec_factory

#endif  // SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_IMPL_H_
