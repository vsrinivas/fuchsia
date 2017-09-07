// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/name.h>
#include <stddef.h>
#include <stdint.h>

namespace crypto {

namespace entropy {

class Collector {
public:
    virtual ~Collector();

    // Returns a null-terminated name, in the buffer of size |len| at |buf|.
    void get_name(char* name, size_t len) const { name_.get(len, name); }

    // Fills |len| bytes of memory at |buf| with random data from this entropy
    // collector's entropy source. The bytes that are returned may not be
    // perfectly random, i.e. they may be statistically dependent or biased. Use
    // the BytesNeeded() method to determine how may random bytes are needed to
    // collect a certain amount of entropy.
    virtual size_t DrawEntropy(uint8_t* buf, size_t len) = 0;

    // Returns the number of bytes of random data that should be drawn via
    // DrawEntropy() to get approximately |bits| bits of entropy. Note: |bits|
    // must be no larger than 2^20 (= 1048576).
    size_t BytesNeeded(size_t bits) const;

protected:
    // Initialize this entropy collector. |name| is used for debugging and
    // testing, and it may be truncated if it is too long.
    // |entropy_per_1000_bytes| is the (approximate) amount of min-entropy in
    // each 1000 bytes of data returned by the entropy source. The amount of
    // entropy in a byte from the entropy source is generally not an integer.
    // Quoting the entropy per 1000 bytes supports non-integer values, without
    // requiring floating-point or fixed-point arithmetic. It is an error if
    // |entropy_per_1000_bytes| is 0 or is greater than 8000.
    //
    // TODO(andrewkrieger): document entropy source quality tests, and reference
    // that document here, to explain how to find a good value for
    // entropy_per_1000_bytes.
    Collector(const char* name, size_t entropy_per_1000_bytes);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Collector);

    fbl::Name<MX_MAX_NAME_LEN> name_;

    size_t entropy_per_1000_bytes_;
};

} // namespace entropy

} // namespace crypto
