// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/entropy/jitterentropy_collector.h>

#include <kernel/cmdline.h>
#include <magenta/errors.h>
#include <fbl/atomic.h>

#ifndef JITTERENTROPY_MEM_SIZE
#define JITTERENTROPY_MEM_SIZE (64u * 1024u)
#endif

// defined in vm/bootalloc.cpp
extern "C" void* boot_alloc_mem(size_t len);

namespace crypto {

namespace entropy {

mx_status_t JitterentropyCollector::GetInstance(Collector** ptr) {
    static JitterentropyCollector* instance = nullptr;
    // Note: this would be fbl::atomic<bool>, except that fbl doesn't support
    // that specialization.
    static fbl::atomic<int> initialized = {0};

    // Release-acquire ordering guarantees that, once a thread has stored a
    // value in |initialized| with |memory_order_release|, any other thread that
    // later loads |initialized| with |memory_order_acquire| will see the result
    // that the first thread wrote to |instance|. In particular, any thread that
    // reads |initialized| as 1 will see |instance| as having been initialized.
    //
    // Note that this doesn't protect against concurrent access: if two threads
    // both enter GetInstance while |initialized| is still 0, they might both
    // try to run the initialization code. That's why the comment in
    // jitterentropy_collector.h requires that GetInstance() runs to completion
    // first before concurrent calls are allowed.
    if (!initialized.load(fbl::memory_order_acquire)) {
        if (jent_entropy_init() != 0) {
            // Initialization failed; keep instance == nullptr
            instance = nullptr;
        } else {
            // TODO(andrewkrieger): after optimizing jitterentropy parameters
            // (see MG-1022), replace JITTERENTROPY_MEM_SIZE by the optimal
            // size.
            static uint8_t mem[JITTERENTROPY_MEM_SIZE];
            static JitterentropyCollector collector(mem, sizeof(mem));
            instance = &collector;
        }
        initialized.store(1, fbl::memory_order_release);
    }

    if (instance) {
        *ptr = instance;
        return MX_OK;
    } else {
        *ptr = nullptr;
        return MX_ERR_NOT_SUPPORTED;
    }
}

JitterentropyCollector::JitterentropyCollector(uint8_t* mem, size_t len)
    : Collector("jitterentropy", /* entropy_per_1000_bytes */ 8000) {
    // TODO(MG-1022): optimize default jitterentropy parameters, then update
    // values here and in docs/kernel_cmdline.md.
    uint32_t bs = cmdline_get_uint32("kernel.jitterentropy.bs", 64);
    uint32_t bc = cmdline_get_uint32("kernel.jitterentropy.bc", 1024);
    mem_loops_ = cmdline_get_uint32("kernel.jitterentropy.ml", 128);
    lfsr_loops_ = cmdline_get_uint32("kernel.jitterentropy.ll", 16);
    use_raw_samples_ = cmdline_get_bool("kernel.jitterentropy.raw", false);

    jent_entropy_collector_init(&ec_, mem, len, bs, bc, mem_loops_,
                                /* stir */ true);
}

size_t JitterentropyCollector::DrawEntropy(uint8_t* buf, size_t len) {
    // TODO(MG-1024): Test jitterentropy in multi-CPU environment. Disable
    // interrupts, or otherwise ensure that jitterentropy still performs well in
    // multi-threaded systems.
    fbl::AutoLock guard(&lock_);

    if (use_raw_samples_) {
        for (size_t i = 0; i < len; i++) {
            buf[i] = static_cast<uint8_t>(jent_lfsr_var_stat(&ec_, lfsr_loops_,
                                                             mem_loops_));
        }
        return len;
    } else {
        ssize_t err =jent_read_entropy(&ec_, reinterpret_cast<char*>(buf),
                                       len);
        return (err < 0) ? 0 : static_cast<size_t>(err);
    }
}

} // namespace entropy

} // namespace crypto
