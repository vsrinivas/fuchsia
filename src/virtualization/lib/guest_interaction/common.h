// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_COMMON_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_COMMON_H_

#include "src/virtualization/lib/guest_interaction/proto/guest_interaction.grpc.pb.h"

#define GUEST_INTERACTION_PORT 9999
#define CHUNK_SIZE 1024

class CallData {
 public:
  virtual ~CallData() = default;
  virtual void Proceed(bool ok) = 0;
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_COMMON_H_
