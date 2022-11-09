// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_LIB_FCRYPTO_BYTES_H_
#define SRC_SECURITY_LIB_FCRYPTO_BYTES_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/macros.h>

// |crypto::Bytes| is a small helper class that simply wraps a buffer.  It saves on some boilerplate
// when allocating a buffer.  More importantly, when going out of scope, the destructor guarantees
// that the buffer will be zeroed in a way that will not be optimized away.  Any buffer that holds
// cryptographically sensitive random data should be a |Bytes| and get its data via a call to
// |Bytes::Randomize|.
namespace crypto {

class __EXPORT Bytes final {
 public:
  Bytes();
  ~Bytes();

  Bytes(Bytes&&) noexcept;
  Bytes& operator=(Bytes&&) noexcept;

  // Accessors
  const uint8_t* get() const { return buf_.get(); }
  uint8_t* get() { return buf_.get(); }
  size_t len() const { return len_; }

  // Resizes the underlying buffer to |len| bytes and fills it with random data.
  zx_status_t Randomize() { return Randomize(len_); }
  zx_status_t Randomize(size_t len);

  // Resize the underlying buffer.  If the new length is shorter, the data is truncated.  If it is
  // longer, it is padded with the given |fill| value.
  zx_status_t Resize(size_t size, uint8_t fill = 0);

  // Copies |len| bytes from |src| to |dst_off| in the underlying buffer.  Resizes the buffer as
  // needed, padding with zeros.
  zx_status_t Copy(const void* src, size_t len, zx_off_t dst_off = 0);
  zx_status_t Copy(const Bytes& src, zx_off_t dst_off = 0) {
    return Copy(src.get(), src.len(), dst_off);
  }

  // Erases and frees the underlying buffer.
  void Clear();

  // Array access operators.  Assert that |off| is not out of bounds.
  const uint8_t& operator[](zx_off_t off) const;
  uint8_t& operator[](zx_off_t off);

  // Comparison operators.  These are guaranteed to be constant-time.
  bool operator==(const Bytes& other) const;
  bool operator!=(const Bytes& other) const { return !(*this == other); }

 private:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Bytes);

  // The underlying buffer.
  std::unique_ptr<uint8_t[]> buf_;
  // Length in bytes of memory currently allocated to the underlying buffer.
  size_t len_;
};

}  // namespace crypto

#endif  // SRC_SECURITY_LIB_FCRYPTO_BYTES_H_
