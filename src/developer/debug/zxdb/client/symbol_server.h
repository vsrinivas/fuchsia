// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "src/developer/debug/zxdb/client/client_object.h"

namespace zxdb {

class SymbolServer : public ClientObject {
 public:
  explicit SymbolServer(Session* session, const std::string& name)
      : ClientObject(session), name_(name) {}

  const std::string& name() const { return name_; }

 private:
  // URL as originally used to construct the class. This is mostly to be used
  // to identify the server in the UI. The actual URL may be processed to
  // handle custom protocol identifiers etc.
  std::string name_;
};

}  // namespace zxdb
