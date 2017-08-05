// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/entropy/hw_rng_collector.h>

#include <dev/hw_rng.h>

namespace crypto {

namespace entropy {

bool HwRngCollector::IsSupported() {
#if ARCH_X86_64
    return true;
#else
    return false;
#endif
}

Collector* HwRngCollector::GetInstance() {
    static HwRngCollector instance;
    return &instance;
}

HwRngCollector::HwRngCollector()
    : Collector("hw_rng", /* entropy_per_1000_bytes */ 8000) {
}

size_t HwRngCollector::DrawEntropy(uint8_t* buf, size_t len) {
    // TODO(andrewkrieger): Remove the dev/hw_rng API and move the relevant code
    // directly into this class.
    return hw_rng_get_entropy(buf, len, /* block */ true);
}

} // namespace entropy

} // namespace crypto
