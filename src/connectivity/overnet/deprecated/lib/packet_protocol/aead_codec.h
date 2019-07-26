// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <openssl/aead.h>

#include <array>

#include "src/connectivity/overnet/deprecated/lib/packet_protocol/packet_protocol.h"
#include "src/connectivity/overnet/deprecated/lib/protocol/serialization_helpers.h"

namespace overnet {

class AEADCodec final : public PacketProtocol::Codec {
 public:
  static const auto inline kMaxKeyLength = EVP_AEAD_MAX_KEY_LENGTH;
  static const auto inline kMaxADLength = 32;

  AEADCodec(const EVP_AEAD* aead, const uint8_t* key, size_t key_len, const uint8_t* ad,
            size_t ad_len)
      : Codec(Border::Suffix(EVP_AEAD_max_overhead(aead))),
        aead_(aead),
        key_(Copy<uint8_t, kMaxKeyLength>(key, key_len)),
        ad_(Copy<uint8_t, kMaxADLength>(ad, ad_len)),
        key_len_(key_len),
        ad_len_(ad_len),
        nonce_len_(EVP_AEAD_nonce_length(aead_)) {}

  AEADCodec(const AEADCodec&) = delete;
  AEADCodec& operator=(const AEADCodec&) = delete;

  StatusOr<Slice> Encode(uint64_t seq_idx, Slice data) const;
  StatusOr<Slice> Decode(uint64_t seq_idx, Slice data) const;

 private:
  static Status PopError();

  template <class T, size_t kLength>
  static std::array<T, kLength> Copy(const T* data, size_t length) {
    if (data == nullptr) {
      assert(length == 0);
      return {};
    }
    assert(length <= kLength);
    std::array<T, kLength> out;
    std::copy(data, data + length, out.begin());
    return out;
  }

  std::array<uint8_t, EVP_AEAD_MAX_NONCE_LENGTH> Noncify(uint64_t x) const {
    std::array<uint8_t, EVP_AEAD_MAX_NONCE_LENGTH> out;
    std::fill(out.begin(), out.begin() + nonce_len_ - sizeof(uint64_t), 0);
    WriteLE64(x, out.begin() + nonce_len_ - sizeof(uint64_t));
    return out;
  }

  const EVP_AEAD* const aead_;
  const std::array<uint8_t, kMaxKeyLength> key_;
  const std::array<uint8_t, kMaxADLength> ad_;
  const uint8_t key_len_;
  const uint8_t ad_len_;
  const uint8_t nonce_len_;
};

}  // namespace overnet
