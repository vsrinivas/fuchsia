// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fvm/host/fvm_reservation.h"

#include <inttypes.h>
#include <stdio.h>

bool FvmReservation::Approved() const {
  if (data_.request && (*data_.request > data_.reserved)) {
    return false;
  }

  if (nodes_.request && (*nodes_.request > nodes_.reserved)) {
    return false;
  }

  if (total_bytes_.request &&
      ((*total_bytes_.request != 0) && (*total_bytes_.request < total_bytes_.reserved))) {
    return false;
  }
  return true;
}

void FvmReservation::Dump(FILE* stream) const {
  fprintf(stream,
          "Requested: inodes: %" PRIu64 " data: %" PRIu64 " total bytes: %" PRIu64
          "\n"
          "Reserved:  inodes: %" PRIu64 " data: %" PRIu64 " total bytes: %" PRIu64 "\n",
          nodes_.request.value_or(0), data_.request.value_or(0), total_bytes_.request.value_or(0),
          nodes_.reserved, data_.reserved, total_bytes_.reserved);
}
