// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_CREDENTIALS_H_
#define SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_CREDENTIALS_H_

#include <string>

#include <src/lib/fxl/strings/string_view.h>
#include <openssl/pem.h>
#include <rapidjson/document.h>

namespace service_account {

// Credentials to access a Google cloud service.
//
// A Firebase service account with admin access to the project is automatically
// created for every Firebase project.
//
// In order to download the JSON credential file corresponding to this account,
// visit `Settings > Service accounts > Firebase admin SDK` in the Firebase
// Console and click on the 'Generate new private key' button. The content of
// this JSON file must be pass to the |Parse| method of this class.
class Credentials {
 public:
  Credentials(std::string project_id, std::string client_email,
              std::string client_id, bssl::UniquePtr<EVP_PKEY> private_key);

  const std::string& project_id() { return project_id_; }
  const std::string& client_email() { return client_email_; }
  const std::string& client_id() { return client_id_; }
  const bssl::UniquePtr<EVP_PKEY>& private_key() { return private_key_; }

  // Clone the current credentials.
  std::unique_ptr<Credentials> Clone();

  // Loads the service account credentials.
  //
  // This method must be called before this class is usable. |json| must be the
  // content of the service account configuration file that can be retrieved
  // from the firebase admin console (see the class-level comment).
  //
  // These methods will return an empty unique_ptr if the json content is
  // invalid.
  static std::unique_ptr<Credentials> Parse(fxl::StringView json);
  static std::unique_ptr<Credentials> Parse(const rapidjson::Value& json);

 private:
  std::string project_id_;
  std::string client_email_;
  std::string client_id_;
  bssl::UniquePtr<EVP_PKEY> private_key_;
};

}  // namespace service_account

#endif  // SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_CREDENTIALS_H_
