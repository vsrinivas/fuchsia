// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/symbol_server.h"

#include <cstdio>
#include <filesystem>
#include <map>

#include "rapidjson/document.h"
#include "src/developer/debug/shared/message_loop.h"
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
    // Strip off the protocol identifier, yielding only the bucket name.
    bucket_ = url.substr(5);

    if (bucket_.back() != '/') {
      bucket_ += "/";
    }

    ChangeState(SymbolServer::State::kAuth);
  }

  std::string AuthInfo() const override;
  void Authenticate(const std::string& data,
                    std::function<void(const Err&)> cb) override;
  void Fetch(const std::string& build_id,
             SymbolServer::FetchCallback cb) override;
  void CheckFetch(
      const std::string& build_id,
      std::function<void(const Err&,
                         std::function<void(SymbolServer::FetchCallback)>)>
          cb) override;

 private:
  std::shared_ptr<Curl> PrepareCurl(const std::string& build_id);
  void FetchWithCurl(const std::string& build_id, std::shared_ptr<Curl> curl,
                     SymbolServer::FetchCallback cb);

  void IncrementRetries() {
    if (++retries_ == kMaxRetries) {
      ChangeState(SymbolServer::State::kUnreachable);
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

  if (state() != SymbolServer::State::kAuth) {
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
  if (state() != SymbolServer::State::kAuth) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb]() { cb(Err("Authentication not required.")); });
    return;
  }

  ChangeState(SymbolServer::State::kBusy);

  auto curl = Curl::MakeShared();

  curl->SetURL(kTokenServer);

  std::map<std::string, std::string> post_data;
  post_data["code"] = data;
  post_data["client_id"] = kClientId;
  post_data["client_secret"] = kClientSecret;
  post_data["redirect_uri"] = "urn:ietf:wg:oauth:2.0:oob";
  post_data["grant_type"] = "authorization_code";

  curl->set_post_data(post_data);

  auto document = std::make_shared<rapidjson::Document>();
  curl->set_data_callback([document](const std::string& data) {
    document->Parse(data);
    return data.size();
  });

  curl->Perform([this, cb, document](Curl*, Curl::Error result) {
    if (result) {
      std::string error = "Could not contact authentication server: ";
      error += result.ToString();

      error_log_.push_back(error);
      ChangeState(SymbolServer::State::kAuth);
      cb(Err(error));
      return;
    }

    if (document->HasParseError() || !document->IsObject() ||
        !document->HasMember("access_token") ||
        !document->HasMember("refresh_token")) {
      error_log_.push_back("Authentication failed");
      ChangeState(SymbolServer::State::kAuth);
      cb(Err("Authentication failed"));
      return;
    }

    access_token_ = (*document)["access_token"].GetString();
    refresh_token_ = (*document)["refresh_token"].GetString();

    error_log_.clear();
    ChangeState(SymbolServer::State::kReady);
    cb(Err());
  });
}

std::shared_ptr<Curl> CloudStorageSymbolServer::PrepareCurl(
    const std::string& build_id) {
  if (state() != SymbolServer::State::kReady) {
    return nullptr;
  }

  std::string url = "https://storage.googleapis.com/";
  url += bucket_ + build_id + ".debug";

  auto curl = Curl::MakeShared();
  FXL_DCHECK(curl);

  curl->SetURL(url);
  curl->headers().push_back(std::string("Authorization: Bearer ") +
                            access_token_);

  return curl;
}

void CloudStorageSymbolServer::CheckFetch(
    const std::string& build_id,
    std::function<void(const Err&,
                       std::function<void(SymbolServer::FetchCallback)>)>
        cb) {
  auto curl = PrepareCurl(build_id);

  if (!curl) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb]() { cb(Err("Server not ready."), nullptr); });
    return;
  }

  curl->get_body() = false;

  curl->Perform([this, build_id, curl, cb](Curl*, Curl::Error result) {
    Err err;
    auto code = curl->ResponseCode();

    if (result) {
      err = Err("Could not contact server: " + result.ToString());
    } else if (code == 200) {
      curl->get_body() = true;
      cb(Err(), [this, build_id, curl](SymbolServer::FetchCallback fcb) {
        FetchWithCurl(build_id, curl, fcb);
      });
      return;
    } else if (code != 404 && code != 410) {
      err = Err("Unexpected response: " + std::to_string(code));
    }

    if (err.has_error()) {
      error_log_.push_back(err.msg());
      IncrementRetries();
    }

    cb(err, nullptr);
  });
}

void CloudStorageSymbolServer::Fetch(const std::string& build_id,
                                     SymbolServer::FetchCallback cb) {
  auto curl = PrepareCurl(build_id);

  if (!curl) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb]() { cb(Err("Server not ready."), ""); });
    return;
  }

  FetchWithCurl(build_id, curl, cb);
}

void CloudStorageSymbolServer::FetchWithCurl(const std::string& build_id,
                                             std::shared_ptr<Curl> curl,
                                             FetchCallback cb) {
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
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb]() { cb(Err("Error opening temporary file."), ""); });
    return;
  }

  auto cleanup = [file, path, cache_path, build_id](
                     bool valid, Err* result) -> std::string {
    fclose(file);

    std::error_code ec;
    if (!valid) {
      std::filesystem::remove(path, ec);
      return "";
    }

    if (cache_path.empty()) {
      *result = Err("No symbol cache specified.");
      return path;
    }

    auto target_path =
        std::filesystem::path(cache_path) / ".build-id" / build_id.substr(0, 2);
    auto target_name = build_id.substr(2) + ".debug";

    std::filesystem::create_directory(target_path, ec);
    if (std::filesystem::is_directory(target_path, ec)) {
      std::filesystem::rename(path, target_path / target_name, ec);
      return target_path / target_name;
    } else {
      *result = Err("Could not move file in to cache.");
      return path;
    }
  };

  curl->set_data_callback([file](const std::string& data) {
    return std::fwrite(data.data(), 1, data.size(), file);
  });

  curl->Perform([this, cleanup, cb](Curl* curl, Curl::Error result) {
    auto code = curl->ResponseCode();

    Err err;
    bool valid = false;

    if (result) {
      err = Err("Could not contact server: " + result.ToString());
    } else if (code == 200) {
      valid = true;
    } else if (code != 404 && code != 410) {
      err = Err("Error downloading symbols: " + std::to_string(code));
    }

    if (err.has_error()) {
      error_log_.push_back(err.msg());
      IncrementRetries();
    }

    std::string final_path = cleanup(valid, &err);
    cb(err, final_path);
  });
}

}  // namespace

void SymbolServer::ChangeState(SymbolServer::State state) {
  state_ = state;

  if (state_change_callback_)
    state_change_callback_(state_);
}

std::unique_ptr<SymbolServer> SymbolServer::FromURL(Session* session,
                                                    const std::string& url) {
  if (!StringBeginsWith(url, "gs://")) {
    // We only support cloud storage buckets today.
    return nullptr;
  }

  return std::make_unique<CloudStorageSymbolServer>(session, url);
}

}  // namespace zxdb
