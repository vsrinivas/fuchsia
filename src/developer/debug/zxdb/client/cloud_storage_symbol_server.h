// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_CLOUD_STORAGE_SYMBOL_SERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_CLOUD_STORAGE_SYMBOL_SERVER_H_

#include <map>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/client/curl.h"
#include "src/developer/debug/zxdb/client/symbol_server.h"

namespace zxdb {

class CloudStorageSymbolServer : public SymbolServer {
 public:
  static std::unique_ptr<CloudStorageSymbolServer> Impl(Session* session, const std::string& url);

  // Construct a new cloud storage symbol server. Expects a url of the format
  // gs://bucket/[namespace]
  CloudStorageSymbolServer(Session* session, const std::string& url);

  // Implementation of SymbolServer
  std::string AuthInfo() const override;
  void Authenticate(const std::string& data, fit::callback<void(const Err&)> cb) override;

 protected:
  virtual void DoAuthenticate(const std::map<std::string, std::string>& data,
                              fit::callback<void(const Err&)> cb) = 0;

  // Initialize the class. We want the constructor to do this, but the test mock might need to be
  // manipulated first, so we break this out into a separate function.
  void DoInit() { LoadCachedAuth(); }

  // General dispatch from the result of a Curl transaction. Handles the error cases and then
  // returns true if no error occurred.
  bool HandleRequestResult(Curl::Error result, long response_code, size_t previous_ready_count,
                           Err* out_err);

  // Use the refresh token to get a new access token.
  void AuthRefresh();

  // Load our saved refresh token from disk and reauthenticate.
  void LoadCachedAuth();

  std::string path_;
  std::string access_token_;
  std::string refresh_token_;
};

class MockCloudStorageSymbolServer : public CloudStorageSymbolServer {
 public:
  MockCloudStorageSymbolServer(Session* session, const std::string& url)
      : CloudStorageSymbolServer(session, url) {}

  // Finishes constructing the object. This is manual for the mock class so we can get our
  // instrumentation in place before we do the heavier parts of the initialization.
  void InitForTest() { DoInit(); }

  // The big IO methods are proxied to callbacks for the mock so tests can just intercept them.
  //
  // These are fit::function and not fit::callback because they can be called more than once.
  fit::function<void(const std::string&, DebugSymbolFileType, SymbolServer::FetchCallback)>
      on_fetch = {};
  fit::function<void(const std::string&, DebugSymbolFileType, SymbolServer::CheckFetchCallback)>
      on_check_fetch = {};
  fit::function<void(const std::map<std::string, std::string>&, fit::callback<void(const Err&)>)>
      on_do_authenticate = {};

  // Force the symbol server into the ready state.
  void ForceReady() { ChangeState(SymbolServer::State::kReady); }

  // Implementation of Symbol server.
  void Fetch(const std::string& build_id, DebugSymbolFileType file_type,
             SymbolServer::FetchCallback cb) override {
    on_fetch(build_id, file_type, std::move(cb));
  }
  void CheckFetch(const std::string& build_id, DebugSymbolFileType file_type,
                  SymbolServer::CheckFetchCallback cb) override {
    on_check_fetch(build_id, file_type, std::move(cb));
  }

 private:
  void DoAuthenticate(const std::map<std::string, std::string>& data,
                      fit::callback<void(const Err&)> cb) override {
    on_do_authenticate(data, std::move(cb));
  }
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_CLOUD_STORAGE_SYMBOL_SERVER_H_
