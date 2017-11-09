// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_COMMON_XDR_H_
#define PERIDOT_LIB_COMMON_XDR_H_

#include "peridot/lib/fidl/json_xdr.h"

namespace modular {

void XdrAccount(XdrContext* const xdr, auth::Account* const data) {
  xdr->Field("id", &data->id);
  xdr->Field("identity_provider", &data->identity_provider);
  xdr->Field("display_name", &data->display_name);
  xdr->Field("profile_url", &data->url);
  xdr->Field("image_url", &data->image_url);
}

}  // namespace modular

#endif  // PERIDOT_LIB_LEDGER_CLIENT_PAGE_CLIENT_H_
