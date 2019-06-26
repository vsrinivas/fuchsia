// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/firebase_auth/testing/credentials.h"

#include <rapidjson/document.h>
#include <rapidjson/schema.h>

#include "garnet/public/lib/rapidjson_utils/rapidjson_validation.h"

namespace service_account {

namespace {
constexpr fxl::StringView kServiceAccountConfigurationSchema = R"({
  "type": "object",
  "additionalProperties": true,
  "properties": {
    "project_id": {
      "type": "string"
    },
    "private_key": {
      "type": "string"
    },
    "client_email": {
      "type": "string"
    },
    "client_id": {
      "type": "string"
    }
  },
  "required": ["project_id", "private_key", "client_email", "client_id"]
})";
}  // namespace

Credentials::Credentials(std::string project_id, std::string client_email, std::string client_id,
                         bssl::UniquePtr<EVP_PKEY> private_key)
    : project_id_(std::move(project_id)),
      client_email_(std::move(client_email)),
      client_id_(std::move(client_id)),
      private_key_(std::move(private_key)) {}

std::unique_ptr<Credentials> Credentials::Parse(fxl::StringView json) {
  rapidjson::Document document;
  document.Parse(json.data(), json.size());

  if (document.HasParseError() || !document.IsObject()) {
    FXL_LOG(ERROR) << "Json file is incorrect: " << json;
    return nullptr;
  }

  return Parse(document);
}

std::unique_ptr<Credentials> Credentials::Parse(const rapidjson::Value& json) {
  static auto service_account_schema =
      rapidjson_utils::InitSchema(kServiceAccountConfigurationSchema);
  FXL_DCHECK(service_account_schema);
  FXL_DCHECK(json.IsObject());
  if (!rapidjson_utils::ValidateSchema(json, *service_account_schema)) {
    return nullptr;
  }

  auto project_id = json["project_id"].GetString();
  auto client_email = json["client_email"].GetString();
  auto client_id = json["client_id"].GetString();

  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(json["private_key"].GetString(), json["private_key"].GetStringLength()));
  bssl::UniquePtr<EVP_PKEY> private_key(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));

  if (EVP_PKEY_id(private_key.get()) != EVP_PKEY_RSA) {
    FXL_LOG(ERROR) << "Provided key is not a RSA key.";
    return nullptr;
  }

  return std::make_unique<Credentials>(std::move(project_id), std::move(client_email),
                                       std::move(client_id), std::move(private_key));
}

std::unique_ptr<Credentials> Credentials::Clone() {
  if (!EVP_PKEY_up_ref(private_key_.get())) {
    FXL_LOG(ERROR) << "Unable to increment the private key ref count.";
    return nullptr;
  }
  bssl::UniquePtr<EVP_PKEY> private_key(private_key_.get());
  return std::make_unique<Credentials>(project_id_, client_email_, client_id_,
                                       std::move(private_key));
}

}  // namespace service_account
