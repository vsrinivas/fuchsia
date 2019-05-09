// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "src/developer/debug/zxdb/client/curl.h"
#include "src/developer/debug/zxdb/client/symbol_server.h"

namespace zxdb {

class CloudStorageSymbolServer : public SymbolServer {
 public:
  static std::unique_ptr<CloudStorageSymbolServer> Impl(Session* session,
                                                        const std::string& url);

  // Construct a new cloud storage symbol server. Expects a url of the format
  // gs://<bucket name>
  CloudStorageSymbolServer(Session* session, const std::string& url);

  // Implementation of SymbolServer
  std::string AuthInfo() const override;
  void Authenticate(const std::string& data,
                    std::function<void(const Err&)> cb) override;

 protected:
  virtual void DoAuthenticate(const std::map<std::string, std::string>& data,
                              std::function<void(const Err&)> cb) = 0;

  // Initialize the class. We want the constructor to do this, but the test
  // mock might need to be manipulated first, so we break this out into a
  // separate function.
  void DoInit() {
    ChangeState(SymbolServer::State::kAuth);
    LoadCachedAuth();
  }

  // General dispatch from the result of a Curl transaction. Handles the error
  // cases and then returns true if no error occurred.
  bool HandleRequestResult(Curl::Error result, long response_code,
                           size_t previous_ready_count, Err* out_err);

  // Use the refresh token to get a new access token.
  void AuthRefresh();

  // Load our saved refresh token from disk and reauthenticate.
  void LoadCachedAuth();

  std::string bucket_;
  std::string access_token_;
  std::string refresh_token_;
};

class MockCloudStorageSymbolServer : public CloudStorageSymbolServer {
 public:
  MockCloudStorageSymbolServer(Session* session, const std::string& url)
      : CloudStorageSymbolServer(session, url) {}

  // Finishes constructing the object. This is manual for the mock class so we
  // can get our instrumentation in place before we do the heaveir parts of the
  // initialization.
  void InitForTest() { DoInit(); }

  // The big IO methods are proxied to callbacks for the mock so tests can just
  // intercept them.
  std::function<void(const std::string&, DebugSymbolFileType,
                     SymbolServer::FetchCallback)>
      on_fetch = {};
  std::function<void(const std::string&, DebugSymbolFileType,
                     SymbolServer::CheckFetchCallback)>
      on_check_fetch = {};
  std::function<void(const std::map<std::string, std::string>&,
                     std::function<void(const Err&)>)>
      on_do_authenticate = {};

  // Implementation of Symbol server.
  void Fetch(const std::string& build_id, DebugSymbolFileType file_type,
             SymbolServer::FetchCallback cb) override {
    on_fetch(build_id, file_type, cb);
  }
  void CheckFetch(const std::string& build_id, DebugSymbolFileType file_type,
                  SymbolServer::CheckFetchCallback cb) override {
    on_check_fetch(build_id, file_type, cb);
  }

 private:
  void DoAuthenticate(const std::map<std::string, std::string>& data,
                      std::function<void(const Err&)> cb) override {
    on_do_authenticate(data, cb);
  }
};

}  // namespace zxdb
