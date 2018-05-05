// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/hci/hci_constants.h"

#include <cstdint>

namespace btlib {
namespace hci {

// Remote devices and local controllers have a feature set defined by the
// Link Manager Protocol.
// LMP features are organized into "pages", each containing a bit-mask of
// supported controller features. See Core Spec v5.0, Vol 2, Part C, Secton 3.3
// "Feature Mask Definition".
// Three of these pages (the standard page plus two "extended feature" pages)
// are defined by the spec.

// See LMPFeature in hci_constants.h for the list of feature bits.
class LMPFeatureSet {
 public:
  // Creates a feature set with no pages set.
  LMPFeatureSet() : valid_pages_{false} {}

  // The maximum number of pages that we support, including the standard page.
  constexpr static size_t kMaxPages = 3;

  // Returns true if |bit| is set in the LMP Features.
  // |page| is the page that this bit resides on.
  // Page 0 is the standard features.
  inline bool HasBit(size_t page, LMPFeature bit) const {
    return HasPage(page) && (features_[page] & static_cast<uint64_t>(bit));
  }

  // Sets |page| features to |features|
  inline void SetPage(size_t page, uint64_t features) {
    FXL_DCHECK(page < kMaxPages);
    features_[page] = features;
    valid_pages_[page] = true;
  }

  // Returns true if the feature page |page| has been set.
  inline bool HasPage(size_t page) const {
    return (page < kMaxPages) && valid_pages_[page];
  }

 private:
  uint64_t features_[kMaxPages];
  bool valid_pages_[kMaxPages];
};

}  // namespace hci
}  // namespace btlib
