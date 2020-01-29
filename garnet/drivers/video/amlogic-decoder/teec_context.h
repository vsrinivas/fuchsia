// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TEEC_CONTEXT_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TEEC_CONTEXT_H_

#include <lib/zx/channel.h>

#include <tee-client-api/tee-client-types.h>

class TeecContext {
 public:
  TeecContext();
  ~TeecContext();

  TEEC_Context& context();

  void SetClientChannel(zx::channel client_channel);

 private:
  TEEC_Context context_{};
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_TEEC_CONTEXT_H_
