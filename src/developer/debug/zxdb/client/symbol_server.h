// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/developer/debug/zxdb/client/client_object.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

class SymbolServer : public ClientObject {
 public:
  // Callback used to receive the results of trying to fetch symbols. The
  // string given is the path where the symbols were downloaded. If the string
  // is empty the symbols were unavailable. The error is only set in the event
  // of a connection error. If the symbols are simply unavailable the error
  // will not be set.
  using FetchCallback = std::function<void(const Err&, const std::string&)>;

  enum class State {
    kInitializing,
    kAuth,
    kBusy,
    kReady,
    kUnreachable,
  };

  enum class AuthType {
    kOAuth,
  };

  static std::unique_ptr<SymbolServer> FromURL(Session* session,
                                               const std::string& url);

  const std::string& name() const { return name_; }

  const std::vector<std::string>& error_log() const { return error_log_; }

  State state() const { return state_; }
  void set_state_change_callback(std::function<void(SymbolServer*,State)> cb) {
    state_change_callback_ = cb;
  }

  AuthType auth_type() const { return AuthType::kOAuth; }

  virtual std::string AuthInfo() const = 0;
  virtual void Authenticate(const std::string& data,
                            std::function<void(const Err&)> cb) = 0;
  virtual void Fetch(const std::string& build_id, FetchCallback cb) = 0;

  // Query to see whether the server has symbols for the given build ID, but
  // don't actually download them. Callback receives a function which it can
  // call to continue and actually download the symbols. That function has the
  // same signature as the Fetch method. If the callback == nullptr the symbol
  // was not found. The error supplied is only set if there was a problem with
  // the connection, not if the symbols were simply unavailable.
  virtual void CheckFetch(
      const std::string& build_id,
      std::function<void(const Err&, std::function<void(FetchCallback)>)>
          cb) = 0;

 protected:
  explicit SymbolServer(Session* session, const std::string& name)
      : ClientObject(session), name_(name) {}
  void ChangeState(State state);

  std::vector<std::string> error_log_;

 private:
  State state_ = State::kInitializing;

  // URL as originally used to construct the class. This is mostly to be used
  // to identify the server in the UI. The actual URL may be processed to
  // handle custom protocol identifiers etc.
  std::string name_;

  std::function<void(SymbolServer*,State)> state_change_callback_ = nullptr;
};

}  // namespace zxdb
