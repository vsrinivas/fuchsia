// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "teec_context.h"

#include <zircon/types.h>

#include "tee-client-api/tee-client-types.h"

TeecContext::TeecContext() {
  // nothing to do here
}

TeecContext::~TeecContext() {
  if (context_.imp.tee_channel != ZX_HANDLE_INVALID) {
    zx::channel to_delete(context_.imp.tee_channel);
    context_.imp.tee_channel = ZX_HANDLE_INVALID;
  }
}

TEEC_Context& TeecContext::context() { return context_; }

void TeecContext::SetClientChannel(zx::channel client_channel) {
  context_.imp.tee_channel = client_channel.release();
}
