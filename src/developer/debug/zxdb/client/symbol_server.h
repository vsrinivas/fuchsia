// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYMBOL_SERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYMBOL_SERVER_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/client/client_object.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/debug_symbol_file_type.h"

namespace zxdb {

class SymbolServer : public ClientObject {
 public:
  // Callback used to receive the results of trying to fetch symbols. The string given is the path
  // where the symbols were downloaded. If the string is empty the symbols were unavailable. The
  // error is only set in the event of a connection error. If the symbols are simply unavailable the
  // error will not be set.
  using FetchCallback = fit::callback<void(const Err&, const std::string&)>;
  using CheckFetchCallback = fit::callback<void(const Err&, fit::callback<void(FetchCallback)>)>;

  enum class State {
    kInitializing,  // The server just gets created. It will become kBusy or kAuth shortly.
    kAuth,          // The authentication is missing or invalid.
    kBusy,          // The server is doing authentication.
    kReady,         // The authentication is done and the server is ready to use.
    kUnreachable,   // Too many failed downloads and the server is unusable.
  };

  enum class AuthType {
    kOAuth,
  };

  static std::unique_ptr<SymbolServer> FromURL(Session* session, const std::string& url);

  const std::string& name() const { return name_; }

  const std::vector<std::string>& error_log() const { return error_log_; }

  State state() const { return state_; }
  void set_state_change_callback(fit::callback<void(SymbolServer*, State)> cb) {
    state_change_callback_ = std::move(cb);
  }

  AuthType auth_type() const { return AuthType::kOAuth; }

  virtual std::string AuthInfo() const = 0;
  virtual void Authenticate(const std::string& data, fit::callback<void(const Err&)> cb) = 0;
  virtual void Fetch(const std::string& build_id, DebugSymbolFileType file_type,
                     FetchCallback cb) = 0;

  // Query to see whether the server has symbols for the given build ID, but don't actually download
  // them. Callback receives a function which it can call to continue and actually download the
  // symbols. That function has the same signature as the Fetch method. If the callback == nullptr
  // the symbol was not found. The error supplied is only set if there was a problem with the
  // connection, not if the symbols were simply unavailable.
  virtual void CheckFetch(const std::string& build_id, DebugSymbolFileType file_type,
                          CheckFetchCallback cb) = 0;

 protected:
  explicit SymbolServer(Session* session, const std::string& name)
      : ClientObject(session), name_(name) {}
  void ChangeState(State state);
  void IncrementRetries();

  std::vector<std::string> error_log_;
  size_t retries_ = 0;

  // Incremented each time the state becomes ready.
  size_t ready_count_ = 0;

 private:
  State state_ = State::kInitializing;

  // URL as originally used to construct the class. This is mostly to be used to identify the server
  // in the UI. The actual URL may be processed to handle custom protocol identifiers etc.
  std::string name_;

  fit::callback<void(SymbolServer*, State)> state_change_callback_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SYMBOL_SERVER_H_
