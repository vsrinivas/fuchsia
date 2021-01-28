// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_POLICY_H_
#define SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_POLICY_H_

#include <memory>

#include "codec_factory_hw_policy.h"

class CodecFactoryApp;

class CodecFactoryPolicy : public CodecFactoryHwPolicy::Owner {
 public:
  explicit CodecFactoryPolicy(CodecFactoryApp* app);
  bool AdmitHwDecoder(const fuchsia::mediacodec::CreateDecoder_Params& params,
                      std::vector<zx::eventpair>* lifetime_codec_eventpairs);
  bool AdmitHwEncoder(const fuchsia::mediacodec::CreateEncoder_Params& params,
                      std::vector<zx::eventpair>* lifetime_codec_eventpairs);

 private:
  // CodecFactoryHwPolicy::Owner
  async_dispatcher_t* dispatcher() override;

  CodecFactoryApp* app_ = nullptr;

  // All must return true for a HW decoder/encoder to be created.
  std::vector<std::unique_ptr<CodecFactoryHwPolicy>> hw_policies_;
};

#endif  // SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_POLICY_H_
