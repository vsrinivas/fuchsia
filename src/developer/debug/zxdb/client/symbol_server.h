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

  AuthType auth_type() const { return AuthType::kOAuth; }

  virtual std::string AuthInfo() const = 0;
  virtual void Authenticate(const std::string& data,
                            std::function<void(const Err&)> cb) = 0;
  virtual void Fetch(
      const std::string& build_id,
      std::function<void(const Err&, const std::string&)> cb) = 0;

 protected:
  explicit SymbolServer(Session* session, const std::string& name)
      : ClientObject(session), name_(name) {}

  std::vector<std::string> error_log_;

  State state_ = State::kInitializing;

 private:
  // URL as originally used to construct the class. This is mostly to be used
  // to identify the server in the UI. The actual URL may be processed to
  // handle custom protocol identifiers etc.
  std::string name_;
};

}  // namespace zxdb
