// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_WEAVE_SIGNER_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_WEAVE_SIGNER_H_

#include <fuchsia/weave/cpp/fidl_test_base.h>

#include <optional>

#include <gtest/gtest.h>

namespace weave::adaptation::testing {

class FakeWeaveSigner final : public fuchsia::weave::testing::Signer_TestBase {
 public:
  // Default values for signer operation returns.
  static constexpr uint8_t kSignedHash[] = {0xFF};

  // Replaces all unimplemented functions with a fatal error.
  void NotImplemented_(const std::string& name) override { FAIL() << name; }

  // Construct a FakeWeaveSigner with provided configuration.
  FakeWeaveSigner(const uint8_t* signed_hash, size_t signed_hash_size)
      : signed_hash_(signed_hash, signed_hash + signed_hash_size) {}

  // Construct a FakeWeaveSigner with default configuration.
  FakeWeaveSigner() : FakeWeaveSigner(kSignedHash, sizeof(kSignedHash)) {}

  // Signs the provided hash.
  void SignHash(std::vector<uint8_t> hash, SignHashCallback callback) override {
    fuchsia::weave::Signer_SignHash_Result result;

    // If an error was set, immediately return it.
    if (sign_hash_error_) {
      result.set_err(sign_hash_error_.value());
    } else {
      fuchsia::weave::Signer_SignHash_Response response;
      std::copy(signed_hash_.begin(), signed_hash_.end(), std::back_inserter(response.signature));
      result.set_response(response);
    }
    callback(std::move(result));
  }

  // Returns an interface request handler to attach to a service directory.
  fidl::InterfaceRequestHandler<fuchsia::weave::Signer> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::weave::Signer> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  // Closes the binding, simulating the service going away.
  void Close(zx_status_t epitaph_value = ZX_OK) { binding_.Close(epitaph_value); }

  // Set the signed hash.
  FakeWeaveSigner& set_signed_hash(const uint8_t* signed_hash, size_t signed_hash_size) {
    signed_hash_ = std::vector<uint8_t>(signed_hash, signed_hash + signed_hash_size);
    return *this;
  }

  // Set the signed hash error.
  FakeWeaveSigner& set_sign_hash_error(std::optional<fuchsia::weave::ErrorCode> sign_hash_error) {
    sign_hash_error_ = sign_hash_error;
    return *this;
  }

 private:
  fidl::Binding<fuchsia::weave::Signer> binding_{this};
  std::vector<uint8_t> signed_hash_;
  std::optional<fuchsia::weave::ErrorCode> sign_hash_error_;
};

}  // namespace weave::adaptation::testing

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_WEAVE_SIGNER_H_
