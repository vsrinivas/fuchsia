// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAPPED_BATCH_H
#define MAPPED_BATCH_H

#include "gpu_mapping.h"
#include "magma_util/mapped_batch.h"
#include "platform_buffer.h"

class MsdVslContext;

using MappedBatch = magma::MappedBatch<MsdVslContext, MsdVslBuffer>;

#endif  // MAPPED_BATCH_H
