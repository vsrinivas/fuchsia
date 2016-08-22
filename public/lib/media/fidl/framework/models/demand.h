// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_MODELS_DEMAND_H_
#define SERVICES_MEDIA_FRAMEWORK_MODELS_DEMAND_H_

namespace mojo {
namespace media {

// Expresses packet demand for signalling upstream in a graph.
enum class Demand {
  // Ordered such that (kNegative < kNeutral < kPositive).

  // No packet can currently be accepted.
  kNegative = -1,

  // A packet can be accepted but is not required to meet timing constraints.
  kNeutral = 0,

  // A packet is required to meet timing constraints.
  kPositive = 1
};

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_MODELS_DEMAND_H_
