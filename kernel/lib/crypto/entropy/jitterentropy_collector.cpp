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

// TODO(MG-1024): Test jitterentropy in different environments (especially on
// different platforms/architectures, and in multi-threaded mode). Ensure
// entropy estimate is safe enough.

// Testing shows that, with the default parameters below (bs=64, bc=512,
// ml=32, ll=1, raw=true), each byte of data contributes approximately
// 0.58 bits of min-entropy on the rpi3 and 0.5 bits on qemu-arm64. A safety
// factor of 0.9 gives us 0.50 * 0.9 * 1000 == 450 bits of entropy per 1000
// bytes of random data.
JitterentropyCollector::JitterentropyCollector(uint8_t* mem, size_t len)
    : Collector("jitterentropy", /* entropy_per_1000_bytes */ 450) {
    // TODO(MG-1022): optimize default jitterentropy parameters, then update
    // values here and in docs/kernel_cmdline.md.
    uint32_t bs = cmdline_get_uint32("kernel.jitterentropy.bs", 64);
    uint32_t bc = cmdline_get_uint32("kernel.jitterentropy.bc", 512);
    mem_loops_ = cmdline_get_uint32("kernel.jitterentropy.ml", 32);
    lfsr_loops_ = cmdline_get_uint32("kernel.jitterentropy.ll", 1);
    use_raw_samples_ = cmdline_get_bool("kernel.jitterentropy.raw", true);

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
