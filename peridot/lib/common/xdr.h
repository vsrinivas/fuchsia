// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_COMMON_XDR_H_
#define PERIDOT_LIB_COMMON_XDR_H_

#include "peridot/lib/fidl/json_xdr.h"

namespace fuchsia {
namespace modular {
namespace auth {
class Account;
}  // namespace auth
}  // namespace modular
}  // namespace fuchsia

namespace modular {

extern const XdrFilterType<fuchsia::modular::auth::Account> XdrAccount[];

}  // namespace modular

#endif  // PERIDOT_LIB_COMMON_XDR_H_
