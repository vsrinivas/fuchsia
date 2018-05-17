// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

namespace btlib {
namespace sm {
namespace util {

PairingMethod SelectPairingMethod(bool sec_conn, bool local_oob, bool peer_oob,
                                  bool mitm_required, IOCapability local_ioc,
                                  IOCapability peer_ioc) {
  if ((sec_conn && (local_oob || peer_oob)) ||
      (!sec_conn && local_oob && peer_oob)) {
    return PairingMethod::kOutOfBand;
  }

  // If neither device requires MITM protection or if the peer has not I/O
  // capable, we select Just Works.
  if (!mitm_required || peer_ioc == IOCapability::kNoInputNoOutput) {
    return PairingMethod::kJustWorks;
  }

  // Select the pairing method by comparing I/O capabilities. The switch
  // statement will return if an authenticated entry method is selected.
  // Otherwise, we'll break out and default to Just Works below.
  switch (local_ioc) {
    case IOCapability::kNoInputNoOutput:
      break;

    case IOCapability::kDisplayOnly:
      switch (peer_ioc) {
        case IOCapability::kKeyboardOnly:
        case IOCapability::kKeyboardDisplay:
          return PairingMethod::kPasskeyEntry;
        default:
          break;
      }
      break;

    case IOCapability::kDisplayYesNo:
      switch (peer_ioc) {
        case IOCapability::kDisplayYesNo:
          return sec_conn ? PairingMethod::kNumericComparison
                          : PairingMethod::kJustWorks;
        case IOCapability::kKeyboardDisplay:
          return sec_conn ? PairingMethod::kNumericComparison
                          : PairingMethod::kPasskeyEntry;
        case IOCapability::kKeyboardOnly:
          return PairingMethod::kPasskeyEntry;
        default:
          break;
      }
      break;

    case IOCapability::kKeyboardOnly:
      return PairingMethod::kPasskeyEntry;

    case IOCapability::kKeyboardDisplay:
      if (peer_ioc == IOCapability::kKeyboardOnly ||
          peer_ioc == IOCapability::kDisplayOnly) {
        return PairingMethod::kPasskeyEntry;
      }

      return sec_conn ? PairingMethod::kNumericComparison
                      : PairingMethod::kPasskeyEntry;
  }

  return PairingMethod::kJustWorks;
}

}  // namespace util
}  // namespace sm
}  // namespace btlib
