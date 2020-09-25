// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sim-sig-loss-model.h"

namespace wlan::simulation {

// The <cstdlib> overloads confuse the compiler for <cstdint> types.
template <typename T>
constexpr T abs(T t) {
  return t < 0 ? -t : t;
}

double Location::distanceFrom(Location* b) {
  return std::sqrt(std::pow(abs(b->x - x), 2) + std::pow(abs(b->y - y), 2));
}

LogSignalLossModel::LogSignalLossModel()
    : ref_distance_(kDefaultRefDistance),
      ref_loss_(kDefaultRefLoss),
      loss_exponent_(kDefaultLossExponent) {}

// Calculates signal strength in dbm between two locations.
double LogSignalLossModel::CalcSignalStrength(Location* staTx, Location* staRx) {
  double distance = staTx->distanceFrom(staRx);
  if (distance <= ref_distance_) {
    return -ref_loss_;
  }

  double pathLossDb = 10 * loss_exponent_ * log10(distance / ref_distance_);

  return -ref_loss_ - pathLossDb;
}

}  // namespace wlan::simulation
