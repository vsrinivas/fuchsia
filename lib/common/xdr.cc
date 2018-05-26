// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/common/xdr.h"

#include <modular_auth/cpp/fidl.h>

namespace modular {

void XdrAccount_v1(XdrContext* const xdr, modular_auth::Account* const data) {
  xdr->Field("id", &data->id);
  xdr->Field("identity_provider", &data->identity_provider);
  xdr->Field("display_name", &data->display_name);
  xdr->Field("profile_url", &data->url);
  xdr->Field("image_url", &data->image_url);
}

void XdrAccount_v2(XdrContext* const xdr, modular_auth::Account* const data) {
  if (!xdr->Version(2)) {
    return;
  }
  xdr->Field("id", &data->id);
  xdr->Field("identity_provider", &data->identity_provider);
  xdr->Field("display_name", &data->display_name);
  xdr->Field("profile_url", &data->url);
  xdr->Field("image_url", &data->image_url);
}

extern const XdrFilterType<modular_auth::Account> XdrAccount[] = {
  XdrAccount_v2,
  XdrAccount_v1,
  nullptr,
};

}  // namespace modular
