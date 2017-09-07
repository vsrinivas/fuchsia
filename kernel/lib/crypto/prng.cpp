// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/prng.h>

#include <assert.h>
#include <string.h>

#include <err.h>
#include <lib/crypto/cryptolib.h>
#include <magenta/compiler.h>
#include <magenta/types.h>
#include <fbl/auto_lock.h>
#include <openssl/chacha.h>
#include <pow2.h>

namespace crypto {

PRNG::PRNG(const void* data, size_t size)
    : PRNG(data, size, NonThreadSafeTag()) {
    BecomeThreadSafe();
}

PRNG::PRNG(const void* data, size_t size, NonThreadSafeTag tag)
    : is_thread_safe_(false), lock_(), total_entropy_added_(0) {
    memset(key_, 0, sizeof(key_));
    memset(nonce_.u8, 0, sizeof(nonce_.u8));
    AddEntropy(data, size);
}

void PRNG::AddEntropy(const void* data, size_t size) {
    DEBUG_ASSERT(data || size == 0);
    if (likely(is_thread_safe_)) {
        uint64_t total;
        {
            fbl::AutoLock guard(&lock_);
            AddEntropyInternal(data, size);
            total = total_entropy_added_;
        }
        if (total >= kMinEntropy) {
            event_signal(&ready_, true);
        }
    } else {
        AddEntropyInternal(data, size);
    }
}

static_assert(PRNG::kMaxEntropy <= INT_MAX, "bad entropy limit");
static_assert(sizeof(uint32_t) * 2 <= clSHA256_DIGEST_SIZE, "digest too small");
void PRNG::AddEntropyInternal(const void* data, size_t size) {
    ASSERT(size < kMaxEntropy);
    clSHA256_CTX ctx;
    clSHA256_init(&ctx);
    // We mix all of the entropy with the previous key to make the PRNG state
    // depend on both the entropy added and the sequence in which it was added.
    clHASH_update(&ctx, data, static_cast<int>(size));
    clHASH_update(&ctx, key_, sizeof(key_));
    static_assert(clSHA256_DIGEST_SIZE <= sizeof(key_), "key too small");
    memcpy(key_, clHASH_final(&ctx), clSHA256_DIGEST_SIZE);
    total_entropy_added_ += size;
}

void PRNG::Draw(void* out, size_t size) {
    DEBUG_ASSERT(out || size == 0);
    if (likely(is_thread_safe_)) {
        fbl::AutoLock guard(&lock_);
        if (unlikely(total_entropy_added_ < kMinEntropy)) {
            lock_.Release();
            mx_status_t status = event_wait(&ready_);
            ASSERT(status == MX_OK);
            lock_.Acquire();
        }
        DrawInternal(out, size);
    } else {
        DrawInternal(out, size);
    }
}

void PRNG::DrawInternal(void* out, size_t size) {
    ASSERT(size < kMaxDrawLen);
    uint8_t* buf = static_cast<uint8_t*>(out);
    // CRYPTO_chacha_20 XORs the cipher stream with the contents of the 'in'
    // buffer (which can't be null).  Zero it to get just the cipher stream.
    // TODO(aarongreen): try to get BoringSSL to take a patch which allows 'in'
    // to be null.
    memset(buf, 0, size);
    // We use a unique nonce for each request, meaning we can reset the
    // counter to 0 each time.  The counter is guaranteed not to overflow within
    // the call below because of the above assertion on the overall size.
    CRYPTO_chacha_20(buf, buf, size, key_, nonce_.u8, 0);
    // We use a different 12-byte nonce for each request by treating it as the
    // concatenation of an 8-byte and 4-byte counter.  Every time the 8-byte
    // counter overflows, we increment the 4-byte counter.  The 4-byte counter
    // must not overflow, meaning we can make a total of 2**96 requests.  Even
    // at 1000 requests per nanosecond, this would take 2.5 billion years.
    ++nonce_.u64;
    if (unlikely(nonce_.u64 == 0)) {
        ++nonce_.u32[2];
        ASSERT(nonce_.u32[2] != 0);
    }
}

uint64_t PRNG::RandInt(uint64_t exclusive_upper_bound) {
    ASSERT(exclusive_upper_bound != 0);

    const uint log2 = log2_ulong_ceil(exclusive_upper_bound);
    const size_t mask = (log2 != sizeof(uint64_t) * CHAR_BIT)
                            ? (uint64_t(1) << log2) - 1
                            : UINT64_MAX;
    DEBUG_ASSERT(exclusive_upper_bound - 1 <= mask);

    // This loop should terminate very fast, since the probability that the
    // drawn value is >= exclusive_upper_bound is less than 0.5.  This is the
    // classic discard out-of-range values approach.
    while (true) {
        uint64_t v;
        Draw(reinterpret_cast<uint8_t*>(&v),
             sizeof(uint64_t) / sizeof(uint8_t));
        v &= mask;
        if (v < exclusive_upper_bound) {
            return v;
        }
    }
}

// It is safe to call this function from PRNG's constructor provided
// |is_thread_safe_| and |total_entropy_added_| are initialized.
void PRNG::BecomeThreadSafe() {
    ASSERT(!is_thread_safe_);

    const bool enough_entropy = (total_entropy_added_ >= kMinEntropy);
    ready_ = EVENT_INITIAL_VALUE(ready_, enough_entropy, 0);

    is_thread_safe_ = true;
}

PRNG::~PRNG() {}

} // namespace crypto
