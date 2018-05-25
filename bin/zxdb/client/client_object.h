// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

class Session;

// Base class for debugger client objects.
//
// Currently this doesn't do much. It's anticipated that this will provide
// bindings and such when scripting support is added.
class ClientObject {
 public:
  explicit ClientObject(Session* session);
  virtual ~ClientObject();

  Session* session() const { return session_; }

 private:
  Session* session_;
};

}  // namespace zxdb
