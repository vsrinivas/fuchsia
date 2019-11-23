// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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
