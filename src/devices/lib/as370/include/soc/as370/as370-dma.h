// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_DMA_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_DMA_H_

#include <limits.h>

enum DmaId {
  kDmaIdMa0,
  kDmaIdMa1,
  kDmaIdMa2,
  kDmaIdMa3,

  kDmaIdMic1W0,
  kDmaIdMic1W1,
  kDmaIdMic1PdmW0,
  kDmaIdSec,

  kDmaIdBcm,
  kDmaIdMic1PdmW1,
  kDmaIdPdmW0,
  kDmaIdPdmW1,

  kDmaIdMic2W0,
  kDmaIdReserved,
  kDmaIdSpdifR,
  kDmaIdSpdifW,

  kDmaIdMax,
};

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_DMA_H_
