// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_HW_POLICY_H_
#define SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_HW_POLICY_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/eventpair.h>

#include <optional>

class CodecFactoryHwPolicy {
 public:
  class Owner {
   public:
    virtual async_dispatcher_t* dispatcher() = 0;
  };

  explicit CodecFactoryHwPolicy(Owner* owner);
  virtual ~CodecFactoryHwPolicy();

  virtual bool AdmitHwDecoder(const fuchsia::mediacodec::CreateDecoder_Params& params) = 0;
  virtual bool AdmitHwEncoder(const fuchsia::mediacodec::CreateEncoder_Params& params) = 0;
  virtual zx::eventpair TrackHwDecoder(const fuchsia::mediacodec::CreateDecoder_Params& params) = 0;
  virtual zx::eventpair TrackHwEncoder(const fuchsia::mediacodec::CreateEncoder_Params& params) = 0;

 protected:
  async_dispatcher_t* dispatcher();

 private:
  Owner* owner_ = nullptr;
};

#endif  // SRC_MEDIA_CODEC_FACTORY_CODEC_FACTORY_HW_POLICY_H_
