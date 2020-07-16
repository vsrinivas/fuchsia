// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_XDR_H_
#define SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_XDR_H_

#include <fuchsia/modular/session/cpp/fidl.h>

#include "src/modular/lib/fidl/json_xdr.h"

namespace modular {

// Serialization and deserialization of Modular config tables to and from JSON
void XdrModularConfig_v1(XdrContext* const xdr,
                         fuchsia::modular::session::ModularConfig* const data);

void XdrBasemgrConfig_v1(XdrContext* const xdr,
                         fuchsia::modular::session::BasemgrConfig* const data);

void XdrSessionmgrConfig_v1(XdrContext* const xdr,
                            fuchsia::modular::session::SessionmgrConfig* const data);

constexpr XdrFilterType<fuchsia::modular::session::ModularConfig> XdrModularConfig[] = {
    XdrModularConfig_v1,
    nullptr,
};

constexpr XdrFilterType<fuchsia::modular::session::BasemgrConfig> XdrBasemgrConfig[] = {
    XdrBasemgrConfig_v1,
    nullptr,
};

constexpr XdrFilterType<fuchsia::modular::session::SessionmgrConfig> XdrSessionmgrConfig[] = {
    XdrSessionmgrConfig_v1,
    nullptr,
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_MODULAR_CONFIG_MODULAR_CONFIG_XDR_H_
