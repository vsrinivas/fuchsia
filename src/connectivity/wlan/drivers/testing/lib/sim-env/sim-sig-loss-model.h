// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_SIG_LOSS_MODEL_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_SIG_LOSS_MODEL_H_

#include <zircon/types.h>

#include <cmath>

namespace wlan::simulation {

struct Location {
  // Absolute 2D coordinates in meters
  int32_t x;
  int32_t y;
  Location(uint32_t x, uint32_t y) : x(x), y(y) {}

  // Calculates distance between two locations in meters
  double distanceFrom(Location* b);
};

class SignalLossModel {
 public:
  SignalLossModel() = default;
  virtual ~SignalLossModel() = default;

  // calculates signal strength in dBm
  virtual double CalcSignalStrength(Location* staTx, Location* staRx) = 0;
};

// Common proprogation loss model to estimate indoor attenuation over
// short distances, derived from Friis Free Space Proprogation Model.
// Does not take into account factors such as wave length
// (common to longer distance models) or multi-path fading.
class LogSignalLossModel : public SignalLossModel {
 public:
  LogSignalLossModel();
  ~LogSignalLossModel() override {}

  double CalcSignalStrength(Location* staTx, Location* staRx) override;

 private:
  // Loss exponent is environment dependent. A loss exponent of 3 is the default in the ns-3
  // simulator used for suburban environments. See
  // https://www.comsys.rwth-aachen.de/fileadmin/papers/2012/2012-stoffers-ns3-propagation-models.pdf.
  // Lower loss exponents model ideal conditions (no absorption, inteference, etc).
  // Loss exponent value of 2 is perfectally ideal conditions.
  const double kDefaultLossExponent = 3.0;
  // Calibration based potentially on emperical data. At x ref distance, y ref loss in dBm.
  // Default values again based on ns-3 defaults.
  const double kDefaultRefDistance = 1.0;
  const double kDefaultRefLoss = 46.67;

  double ref_distance_;
  double ref_loss_;
  double loss_exponent_;
};

}  // namespace wlan::simulation

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_ENV_SIM_SIG_LOSS_MODEL_H_
