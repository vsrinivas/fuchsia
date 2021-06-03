// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_DEVICE_TYPE_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_DEVICE_TYPE_H_

namespace amlogic_decoder {

enum class DeviceType {
  // These should be ordered from oldest to newest.
  kGXM = 1,   // S912
  kG12A = 2,  // S905D2
  kG12B = 3,  // T931
  kSM1 = 4,   // S905D3
};

}  // namespace amlogic_decoder

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_DEVICE_TYPE_H_
