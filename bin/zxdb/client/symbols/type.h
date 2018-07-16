// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "garnet/bin/zxdb/client/symbols/symbol.h"

namespace zxdb {

class Type : public Symbol {
 public:
  // Symbol overrides.
  const Type* AsType() const final;

  // Most types will have a name but some won't so this may be empty.
  const std::string& name() const { return name_; }
  void set_name(std::string n) { name_ = std::move(n); }

  // For forward-defines where the size of the structure is not known, the
  // byte size will be 0.
  uint32_t byte_size() const { return byte_size_; }
  void set_byte_size(uint32_t bs) { byte_size_ = bs; }

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(Type);
  FRIEND_MAKE_REF_COUNTED(Type);

  explicit Type(int kind);
  virtual ~Type();

 private:
  std::string name_;
  uint32_t byte_size_;
};

}  // namespace
