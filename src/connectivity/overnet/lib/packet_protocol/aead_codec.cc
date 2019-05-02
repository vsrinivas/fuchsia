// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/packet_protocol/aead_codec.h"
#include <openssl/err.h>

namespace overnet {

StatusOr<Slice> AEADCodec::Encode(uint64_t seq_idx, Slice data) const {
  EVP_AEAD_CTX ctx;
  if (EVP_AEAD_CTX_init(&ctx, aead_, key_.data(), key_len_,
                        EVP_AEAD_DEFAULT_TAG_LENGTH, nullptr) != 1) {
    return PopError();
  }
  size_t new_length;
  size_t orig_length = data.length();
  bool ssl_error = false;
  data = data.WithBorders(
      Border::Suffix(border.suffix),
      [this, seq_idx, orig_length, &ctx, &new_length, &ssl_error](uint8_t* p) {
        auto nonce = Noncify(seq_idx);
        ssl_error =
            EVP_AEAD_CTX_seal(&ctx, p, &new_length, orig_length + border.suffix,
                              nonce.data(), nonce_len_, p, orig_length,
                              ad_.data(), ad_len_) != 1;
      });
  EVP_AEAD_CTX_cleanup(&ctx);
  if (ssl_error) {
    return PopError();
  }
  return data.ToOffset(new_length);
}

StatusOr<Slice> AEADCodec::Decode(uint64_t seq_idx, Slice data) const {
  EVP_AEAD_CTX ctx;
  if (EVP_AEAD_CTX_init(&ctx, aead_, key_.data(), key_len_,
                        EVP_AEAD_DEFAULT_TAG_LENGTH, nullptr) != 1) {
    return PopError();
  }
  size_t new_length;
  size_t orig_length = data.length();
  bool ssl_error = false;
  data = data.MutateUnique([this, seq_idx, orig_length, &ctx, &new_length,
                            &ssl_error](uint8_t* p) {
    auto nonce = Noncify(seq_idx);
    ssl_error =
        EVP_AEAD_CTX_open(&ctx, p, &new_length, orig_length, nonce.data(),
                          nonce_len_, p, orig_length, ad_.data(), ad_len_) != 1;
  });
  EVP_AEAD_CTX_cleanup(&ctx);
  if (ssl_error) {
    return PopError();
  }
  return data.ToOffset(new_length);
}

Status AEADCodec::PopError() {
  const char* file;
  int line;
  uint32_t err = ERR_get_error_line(&file, &line);
  std::ostringstream msg;
  msg << "SSL_error:" << err << " from " << file << ":" << line << ": "
      << ERR_reason_error_string(err);
  return Status(StatusCode::UNKNOWN, msg.str());
}

}  // namespace overnet
