// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_NAMESPACE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_NAMESPACE_H_

#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

class Namespace final : public Symbol {
 public:
  // Construct with fxl::MakeRefCounted().

  // Symbol overrides.
  const Namespace* AsNamespace() const override;
  const std::string& GetAssignedName() const final { return assigned_name_; }

  // The name of the namespace. This will be empty for anonymous namespaces.
  // It will not include qualifiers for any parent namespaces.
  void set_assigned_name(std::string n) { assigned_name_ = std::move(n); }

  // Currently we don't have any notion of the stuff contained in the namespace
  // because currently there's no need. That could be added here if necessary.

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(Namespace);
  FRIEND_MAKE_REF_COUNTED(Namespace);

  Namespace();
  explicit Namespace(std::string n);
  virtual ~Namespace();

  // Symbol protected overrides.
  Identifier ComputeIdentifier() const override;

  std::string assigned_name_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_NAMESPACE_H_
