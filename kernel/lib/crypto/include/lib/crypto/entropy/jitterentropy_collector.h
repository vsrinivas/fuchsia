// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/crypto/entropy/collector.h>
#include <lib/jitterentropy/jitterentropy.h>
#include <magenta/types.h>
#include <fbl/mutex.h>

namespace crypto {

namespace entropy {

// An implementation of crypto::entropy::Collector that uses jitterentropy as
// its entropy source. Ultimately, the entropy is derived from variations in
// CPU timing, when various code blocks are exercised.
//
// TODO(andrewkrieger): Document jitterentropy better for Magenta, then link to
// that documentation here.
class JitterentropyCollector : public Collector {
public:
    // Gets the global JitterentropyCollector instance. Returns
    // MX_ERR_NOT_SUPPORTED if jitterentropy is not supported (usually because
    // the system clock is not available or not suitable).
    //
    // This function must be called once in a single-threaded context to
    // initialize the JitterentropyCollector instance. After one successful call
    // (typically during boot), it's safe to call this function from multiple
    // threads, and to access the JitterentropyCollector instance from multiple
    // threads. The JitterentropyCollector::DrawEntropy method is internally
    // guarded by a mutex, so it's safe to call from multiple threads but it may
    // block.
    static mx_status_t GetInstance(Collector** ptr);

    // Inherited from Collector; see comments there.
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
    JitterentropyCollector(uint8_t* mem, size_t len);

    DISALLOW_COPY_ASSIGN_AND_MOVE(JitterentropyCollector);

    struct rand_data ec_;
    fbl::Mutex lock_;
    uint32_t mem_loops_, lfsr_loops_;
    bool use_raw_samples_;
};

} // namespace entropy

} // namespace crypto
