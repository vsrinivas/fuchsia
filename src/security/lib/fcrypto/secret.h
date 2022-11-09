// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_LIB_FCRYPTO_SECRET_H_
#define SRC_SECURITY_LIB_FCRYPTO_SECRET_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/macros.h>

#include "src/security/lib/fcrypto/bytes.h"

// |crypto::Secret| is a small helper class that simply wraps a buffer.  It saves on some
// boilerplate when allocating a buffer.  More importantly, when going out of scope, the destructor
// guarantees that the buffer will be zeroed in a way that will not be optimized away.  Any buffer
// that holds cryptographically sensitive random data should be a |Secret| and get its data via a
// call to |Secret::Randomize|.
namespace crypto {

class __EXPORT Secret final {
 public:
  Secret();
  ~Secret();

  Secret(Secret&&) noexcept;
  Secret& operator=(Secret&&) noexcept;

  // Accessors
  const uint8_t* get() const { return buf_.get(); }
  size_t len() const { return len_; }

  // Allocates |len| bytes for a secret and returns a pointer to the buffer via |out|.  This
  // method should be used when populating a secret from another source, and |out| should be
  // allowed to go out scope as quickly as possible.
  zx_status_t Allocate(size_t len, uint8_t** out);

  // Initializes this object with |size| pseudo-random bytes.
  zx_status_t Generate(size_t size);

  // Erases and frees the underlying buffer.
  void Clear();

 private:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Secret);

  // The underlying buffer.  The destructor is guaranteed to zero this buffer if allocated.
  std::unique_ptr<uint8_t[]> buf_;
  // Length in bytes of memory currently allocated to the underlying buffer.
  size_t len_;
};

}  // namespace crypto

#endif  // SRC_SECURITY_LIB_FCRYPTO_SECRET_H_
