// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_IMPL_H_
#define SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_IMPL_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

#include "codec_factory_app.h"
#include "codec_factory_policy.h"

// There's an instance of CodecFactoryImpl per interface instance, to allow the
// implementation of this class to be stateful.  In particular, the state set up
// by AttachLifetimeTracking() calls applies to the next
// create.
class CodecFactoryImpl : public fuchsia::mediacodec::CodecFactory {
 public:
  static void CreateSelfOwned(CodecFactoryApp* app, sys::ComponentContext* component_context,
                              fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> request,
                              bool is_v2);

  // See .fidl file comments.
  void CreateDecoder(fuchsia::mediacodec::CreateDecoder_Params params,
                     fidl::InterfaceRequest<fuchsia::media::StreamProcessor> decoder) override;

  void CreateEncoder(
      fuchsia::mediacodec::CreateEncoder_Params encoder_params,
      fidl::InterfaceRequest<fuchsia::media::StreamProcessor> encoder_request) override;

  void AttachLifetimeTracking(zx::eventpair codec_end) override;

 private:
  CodecFactoryImpl(CodecFactoryApp* app, sys::ComponentContext* component_context,
                   fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> request, bool is_v2);
  void OwnSelf(std::shared_ptr<CodecFactoryImpl> self);
  void AttachLifetimeTrackingEventpairDownstream(
      const fuchsia::mediacodec::CodecFactoryPtr* factory);

  bool AdmitHwDecoder(const fuchsia::mediacodec::CreateDecoder_Params& params);
  bool AdmitHwEncoder(const fuchsia::mediacodec::CreateEncoder_Params& params);

  // We don't have a lock_ in here - we rely on FIDL message dispatch being
  // one-at-a-time.

  // This class doesn't own these pointers - the creator of CodecFactoryImpl
  // must ensure these outlast this instance of CodecFactoryImpl.
  CodecFactoryApp* app_ = nullptr;
  sys::ComponentContext* component_context_ = nullptr;

  fidl::Binding<fuchsia::mediacodec::CodecFactory> binding_;

  // The CodecFactoryImpl is self-owned via this member. If we need to self-destruct we reset this
  // member. If the channel closes we will also reset this member. The only references handed out
  // is to async fidl callbacks that may need to run after the binding channel has closed, in order
  // to pass requests to child codecs.
  std::shared_ptr<CodecFactoryImpl> self_;

  std::vector<zx::eventpair> lifetime_tracking_;
  bool is_v2_;
};

#endif  // SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_IMPL_H_
