// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/console.h"

#include "src/developer/debug/zxdb/console/command_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

Console* Console::singleton_ = nullptr;

Console::Console(Session* session) : context_(session), weak_factory_(this) {
  FXL_DCHECK(!singleton_);
  singleton_ = this;
}

Console::~Console() {
  FXL_DCHECK(singleton_ == this);
  singleton_ = nullptr;
}

fxl::WeakPtr<Console> Console::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void Console::Output(const std::string& s) {
  OutputBuffer buffer;
  buffer.Append(s);
  Output(buffer);
}

void Console::Output(const Err& err) {
  OutputBuffer buffer;
  buffer.Append(err);
  Output(buffer);
}

}  // namespace zxdb
