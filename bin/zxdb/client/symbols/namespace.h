// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/symbols/symbol.h"

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
  virtual ~Namespace();

  // Symbol protected overrides.
  std::string ComputeFullName() const override;

  std::string assigned_name_;
};

}  // namespace zxdb
