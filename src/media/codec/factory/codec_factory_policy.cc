// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_factory_policy.h"

#include "codec_factory_app.h"
// For now, all HW-specific policy for various HW is directly included in the codec factory binary
// regardless of which HW a build is for.
#include "codec_factory_hw_policy_astro.h"

CodecFactoryPolicy::CodecFactoryPolicy(CodecFactoryApp* app) : app_(app) {
  ZX_DEBUG_ASSERT(hw_policies_.empty());
  std::string board_name = app_->board_name();
  FX_LOGS(INFO) << "board_name: " << board_name.c_str();
  if (board_name == "astro") {
    FX_LOGS(INFO) << "baord name is astro";
    hw_policies_.emplace_back(
        std::unique_ptr<CodecFactoryHwPolicy>(new CodecFactoryHwPolicyAstro(this)));
  }
}

bool CodecFactoryPolicy::AdmitHwDecoder(const fuchsia::mediacodec::CreateDecoder_Params& params,
                                        std::vector<zx::eventpair>* lifetime_codec_eventpairs) {
  ZX_DEBUG_ASSERT(lifetime_codec_eventpairs);
  for (auto& hw_policy : hw_policies_) {
    if (!hw_policy->AdmitHwDecoder(params)) {
      return false;
    }
  }
  for (auto& hw_policy : hw_policies_) {
    zx::eventpair lifetime_codec_end = hw_policy->TrackHwDecoder(params);
    if (lifetime_codec_end) {
      lifetime_codec_eventpairs->emplace_back(std::move(lifetime_codec_end));
    }
  }
  return true;
}

bool CodecFactoryPolicy::AdmitHwEncoder(const fuchsia::mediacodec::CreateEncoder_Params& params,
                                        std::vector<zx::eventpair>* lifetime_codec_eventpairs) {
  ZX_DEBUG_ASSERT(lifetime_codec_eventpairs);
  for (auto& hw_policy : hw_policies_) {
    if (!hw_policy->AdmitHwEncoder(params)) {
      return false;
    }
  }
  for (auto& hw_policy : hw_policies_) {
    zx::eventpair lifetime_codec_end = hw_policy->TrackHwEncoder(params);
    if (lifetime_codec_end) {
      lifetime_codec_eventpairs->emplace_back(std::move(lifetime_codec_end));
    }
  }
  return true;
}

async_dispatcher_t* CodecFactoryPolicy::dispatcher() { return app_->dispatcher(); }
