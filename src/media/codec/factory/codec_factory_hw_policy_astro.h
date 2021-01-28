// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_HW_POLICY_ASTRO_H_
#define SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_HW_POLICY_ASTRO_H_

#include <lib/async/cpp/wait.h>

#include <unordered_map>

#include "codec_factory_policy.h"

// TODO(fxbug.dev/68491): This platform/board/etc-specific allocation/creation
// policy code belongs in a platform/board/etc-specific binary.

class CodecFactoryHwPolicyAstro : public CodecFactoryHwPolicy {
 public:
  explicit CodecFactoryHwPolicyAstro(Owner* owner);
  bool AdmitHwDecoder(const fuchsia::mediacodec::CreateDecoder_Params& params) override;
  bool AdmitHwEncoder(const fuchsia::mediacodec::CreateEncoder_Params& params) override;
  zx::eventpair TrackHwDecoder(const fuchsia::mediacodec::CreateDecoder_Params& params) override;
  zx::eventpair TrackHwEncoder(const fuchsia::mediacodec::CreateEncoder_Params& params) override;

 private:
  // Limit number of decoders that are using buffers allocated from contiguous_memory_size, to avoid
  // setting contiguous_memory_size larger than necessary.
  uint32_t contiguous_memory_size_decoder_count_ = 0;
  // We keep these in an unordered_map so that deletion of CodecFactoryHwPolicyAstro will cancel and
  // delete all waits.  Aside from that case, each wait lasts until just after
  // lifetime_codec_eventpair is deleted (all handles to lifetime_codec_eventpair closed).
  std::unordered_map<async::WaitOnce*, std::unique_ptr<async::WaitOnce>> all_waits_;
};

#endif  // SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_HW_POLICY_ASTRO_H_
