// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_WEAVE_FACTORY_DATA_MANAGER_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_WEAVE_FACTORY_DATA_MANAGER_H_

#include <fuchsia/weave/cpp/fidl_test_base.h>

#include <optional>

#include <gtest/gtest.h>

namespace weave::adaptation::testing {

// Fake implementation of the fuchsia.weave.FactoryDataManager interface.
class FakeWeaveFactoryDataManager : public fuchsia::weave::testing::FactoryDataManager_TestBase {
 public:
  // Default values for the fuchsia.weave.FactoryDataManager fields.
  static constexpr char kPairingCode[] = "PAIRCODE123";
  static constexpr char kWeaveCertificate[] = "CERTIFICATE";

  // Replaces all unimplemented functions with a fatal error.
  void NotImplemented_(const std::string& name) override { FAIL() << name; }

  // Constructs a FakeWeaveFactoryDataManager using the provided configuration values.
  FakeWeaveFactoryDataManager(std::optional<std::string> pairing_code,
                              std::optional<std::string> weave_certificate)
      : pairing_code_(pairing_code), weave_certificate_(weave_certificate) {}

  // Constructs a FakeWeaveFactoryDataManager using the default configuration values.
  FakeWeaveFactoryDataManager() : FakeWeaveFactoryDataManager(kPairingCode, kWeaveCertificate) {}

  // Returns the pairing code or an error code.
  void GetPairingCode(GetPairingCodeCallback callback) override {
    fuchsia::weave::FactoryDataManager_GetPairingCode_Result result;
    fuchsia::weave::FactoryDataManager_GetPairingCode_Response response;

    if (pairing_code_) {
      std::copy(pairing_code_.value().begin(), pairing_code_.value().end(),
                std::back_inserter(response.pairing_code));
      result.set_response(response);
    } else {
      result.set_err(fuchsia::weave::ErrorCode::FILE_NOT_FOUND);
    }

    callback(std::move(result));
  }

  // Returns the certificate or an error code.
  void GetWeaveCertificate(GetWeaveCertificateCallback callback) override {
    fuchsia::weave::FactoryDataManager_GetWeaveCertificate_Result result;
    fuchsia::weave::FactoryDataManager_GetWeaveCertificate_Response response;

    if (weave_certificate_) {
      zx_status_t status =
          zx::vmo::create(weave_certificate_.value().size(), 0, &response.certificate.vmo);
      ASSERT_EQ(status, ZX_OK);
      status = response.certificate.vmo.write(weave_certificate_.value().data(), 0,
                                              weave_certificate_.value().size());
      ASSERT_EQ(status, ZX_OK);
      response.certificate.size = weave_certificate_.value().size();
      result.set_response(std::move(response));
    } else {
      result.set_err(fuchsia::weave::ErrorCode::CRYPTO_ERROR);
    }

    callback(std::move(result));
  }

  // Returns an interface request handler to attach to a service directory.
  fidl::InterfaceRequestHandler<fuchsia::weave::FactoryDataManager> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::weave::FactoryDataManager> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  // Closes the binding, simulating the service going away.
  void Close(zx_status_t epitaph_value = ZX_OK) { binding_.Close(epitaph_value); }

  // Update the pairing code.
  FakeWeaveFactoryDataManager& set_pairing_code(std::optional<std::string> pairing_code) {
    pairing_code_ = pairing_code;
    return *this;
  }

  // Update the weave certificate.
  FakeWeaveFactoryDataManager& set_weave_certificate(std::optional<std::string> weave_certificate) {
    weave_certificate_ = weave_certificate;
    return *this;
  }

 private:
  fidl::Binding<fuchsia::weave::FactoryDataManager> binding_{this};
  std::optional<std::string> pairing_code_;
  std::optional<std::string> weave_certificate_;
};

}  // namespace weave::adaptation::testing

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_FAKE_WEAVE_FACTORY_DATA_MANAGER_H_
