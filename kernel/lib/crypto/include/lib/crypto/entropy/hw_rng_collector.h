// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/crypto/entropy/collector.h>
#include <magenta/types.h>
#include <fbl/mutex.h>

namespace crypto {

namespace entropy {

// An implementation of crypto::entropy::Collector that uses hw_rng_draw_entropy
// as its entropy source. Currently, this is only supported on x86.
class HwRngCollector : public Collector {
public:
    // Gets the current HwRngCollector instance. Returns MX_ERR_NOT_SUPPORTED if
    // hw_rng_draw_entropy is not supported.
    //
    // This function is thread-safe, and the DrawEntropy() method of the global
    // HwRngCollector instance is also thread-safe.
    static mx_status_t GetInstance(Collector** ptr);

    // Inherited from crypto::entropy::Collector; see comments there.
    //
    // Note that this method internally uses a mutex to prevent multiple
    // accesses. It is safe to call this method from multiple threads, but it
    // may block.
    //
    // TODO(andrewkrieger): Determine what level of thread safety is needed for
    // RNG reseeding, and support it more uniformly (e.g. have a thread safety
    // contract for Collector::DrawEntropy, obeyed by all implementations).
    size_t DrawEntropy(uint8_t* buf, size_t len) override;

private:
    HwRngCollector();

    DISALLOW_COPY_ASSIGN_AND_MOVE(HwRngCollector);

    fbl::Mutex lock_;
};

} // namespace entropy

} // namespace crypto
