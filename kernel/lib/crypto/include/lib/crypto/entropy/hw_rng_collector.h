// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/entropy/collector.h>
#include <magenta/types.h>

namespace crypto {

namespace entropy {

class HwRngCollector : public Collector {
public:
    static mx_status_t GetInstance(Collector** ptr);

    HwRngCollector();

    size_t DrawEntropy(uint8_t* buf, size_t len) override;
};

} // namespace entropy

} // namespace crypto
