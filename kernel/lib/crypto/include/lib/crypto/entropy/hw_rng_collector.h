// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/entropy/collector.h>

namespace crypto {

namespace entropy {

class HwRngCollector : public Collector {
public:
    static bool IsSupported();
    static Collector* GetInstance();

    HwRngCollector();

    size_t DrawEntropy(uint8_t* buf, size_t len) override;
};

} // namespace entropy

} // namespace crypto
