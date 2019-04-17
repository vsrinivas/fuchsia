// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/symbol_server.h"

#include <cstdio>
#include <filesystem>
#include <map>

#include "rapidjson/document.h"
#include "src/developer/debug/zxdb/client/curl.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {
namespace {

constexpr char kClientId[] =
    "446450136466-2hr92jrq8e6i4tnsa56b52vacp7t3936"
    ".apps.googleusercontent.com";
constexpr char kClientSecret[] = "uBfbay2KCy9t4QveJ-dOqHtp";

constexpr char kAuthServer[] = "https://accounts.google.com/o/oauth2/v2/auth";
constexpr char kScope[] =
    "https://www.googleapis.com/auth/devstorage.read_only";
constexpr char kTokenServer[] = "https://www.googleapis.com/oauth2/v4/token";

constexpr size_t kMaxRetries = 5;

class CloudStorageSymbolServer : public SymbolServer {
 public:
  // Construct a new cloud storage symbol server. Expects a url of the format
  // gs://<bucket name>
  CloudStorageSymbolServer(Session* session, const std::string& url)
      : SymbolServer(session, url) {
    state_ = SymbolServer::State::kAuth;

    // Strip off the protocol identifier, yielding only the bucket name.
    bucket_ = url.substr(5);

    if (bucket_.back() != '/') {
      bucket_ += "/";
    }
  }

  std::string AuthInfo() const override;
  void Authenticate(const std::string& data,
                    std::function<void(const Err&)> cb) override;
  void Fetch(const std::string& build_id,
             std::function<void(const Err&, const std::string&)> cb) override;

 private:
  void IncrementRetries() {
    if (++retries_ == kMaxRetries) {
      state_ = SymbolServer::State::kUnreachable;
    }
  }

  void ClearRetries() {
    retries_ = 0;
    error_log_.clear();
  }

  size_t retries_ = 0;

  std::string bucket_;
  std::string access_token_;
  std::string refresh_token_;
};

std::string CloudStorageSymbolServer::AuthInfo() const {
  static std::string result;
  static const std::string kEmpty;

  if (state_ != SymbolServer::State::kAuth) {
    return kEmpty;
  }

  if (!result.empty()) {
    return result;
  }

  Curl curl;

  result = kAuthServer;
  result += "?client_id=";
  result += curl.Escape(kClientId);
  result += "&redirect_uri=urn:ietf:wg:oauth:2.0:oob";
  result += "&response_type=code";
  result += "&scope=";
  result += curl.Escape(kScope);

  return result;
}

void CloudStorageSymbolServer::Authenticate(
    const std::string& data, std::function<void(const Err&)> cb) {
  if (state_ != SymbolServer::State::kAuth) {
    cb(Err("Authentication not required."));
    return;
  }

  state_ = SymbolServer::State::kBusy;

  Curl curl;

  curl.SetURL(kTokenServer);

  std::map<std::string, std::string> post_data;
  post_data["code"] = data;
  post_data["client_id"] = kClientId;
  post_data["client_secret"] = kClientSecret;
  post_data["redirect_uri"] = "urn:ietf:wg:oauth:2.0:oob";
  post_data["grant_type"] = "authorization_code";

  curl.set_post_data(post_data);

  rapidjson::Document document;
  curl.set_data_callback([&document](const std::string& data) {
    document.Parse(data);
    return data.size();
  });

  // TODO: Make async once curlcpp has curl_multi support and we've wired it in
  // to the event loop.
  if (auto result = curl.Perform()) {
    std::string error = "Could not contact authentication server: ";
    error += result.ToString();

    error_log_.push_back(error);
    state_ = SymbolServer::State::kAuth;
    cb(Err(error));
    return;
  }

  if (!document.HasMember("access_token") ||
      !document.HasMember("refresh_token")) {
    error_log_.push_back("Authentication failed");
    state_ = SymbolServer::State::kAuth;
    cb(Err("Authentication failed"));
    return;
  }

  access_token_ = document["access_token"].GetString();
  refresh_token_ = document["refresh_token"].GetString();

  error_log_.clear();
  state_ = SymbolServer::State::kReady;
  cb(Err());
}

void CloudStorageSymbolServer::Fetch(
    const std::string& build_id,
    std::function<void(const Err&, const std::string&)> cb) {
  if (state_ != SymbolServer::State::kReady) {
    cb(Err("Server not ready."), "");
    return;
  }

  std::string url = "https://storage.googleapis.com/";
  url += bucket_ + build_id + ".debug";

  Curl curl;

  curl.SetURL(url);
  curl.headers().push_back(std::string("Authorization: Bearer ") +
                           access_token_);

  auto cache_path = session()->system().settings().GetString(
      ClientSettings::System::kSymbolCache);
  std::string path;

  if (cache_path.empty()) {
    // We don't have a folder specified where downloaded symbols go. We'll just
    // drop it in tmp and at least you'll be able to use them for this session.
    path = std::tmpnam(nullptr);
  } else {
    std::error_code ec;
    auto path_obj = std::filesystem::path(cache_path) / ".build-id";

    if (!std::filesystem::is_directory(path_obj, ec)) {
      // Something's wrong with the build ID folder we were provided. We'll
      // just drop it in tmp.
      path = std::tmpnam(nullptr);
    } else {
      // Download to a temporary file, so if we get cancelled (or we get sent
      // a 404 page instead of the real symbols) we don't pollute the build ID
      // folder.
      std::string name = build_id + ".debug.part";
      path = path_obj / name;
    }
  }

  FXL_DCHECK(!path.empty());

  FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) {
    cb(Err("Error opening temporary file."), "");
    return;
  }

  curl.set_data_callback([file](const std::string& data) {
    return std::fwrite(data.data(), 1, data.size(), file);
  });

  // TODO: Make Async. See comment in Authenticate.
  if (auto result = curl.Perform()) {
    std::string error = "Could not contact server: ";
    error += result.ToString();

    error_log_.push_back(error);
    IncrementRetries();
    state_ = SymbolServer::State::kAuth;
    cb(Err(error), "");
    return;
  }

  std::fclose(file);

  Err err;
  std::string final_path;
  auto code = curl.ResponseCode();
  if (code != 200) {
    if (code != 404 && code != 410) {
      err = Err("Error downloading symbols: " + std::to_string(code));
      error_log_.push_back(err.msg());
      IncrementRetries();
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
  } else if (!cache_path.empty()) {
    auto target_path =
        std::filesystem::path(cache_path) / ".build-id" / build_id.substr(0, 2);
    auto target_name = build_id.substr(2) + ".debug";

    std::error_code ec;
    std::filesystem::create_directory(target_path, ec);
    if (std::filesystem::is_directory(target_path, ec)) {
      std::filesystem::rename(path, target_path / target_name, ec);
      final_path = target_path / target_name;
    } else {
      final_path = path;
      err = Err("Could not move file in to cache.");
    }
  }

  cb(err, final_path);
  return;
}

}  // namespace

std::unique_ptr<SymbolServer> SymbolServer::FromURL(Session* session,
                                                    const std::string& url) {
  if (!StringBeginsWith(url, "gs://")) {
    // We only support cloud storage buckets today.
    return nullptr;
  }

  return std::make_unique<CloudStorageSymbolServer>(session, url);
}

}  // namespace zxdb
