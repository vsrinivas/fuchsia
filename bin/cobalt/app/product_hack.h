// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file uses a hack in order to compute the product_name field in Cobalt
// system profile. It reads the package file system looking for the presence of
// well-known packages that are in particular layers of the Fuchsia cake.
// This depends upon the Cobalt process being sandboxed such that it can read
// the package file system. This is something we will want to avoid in the
// future.
//
// Do not deploy to production ever!!!!!!!

#ifndef GARNET_BIN_COBALT_APP_PRODUCT_HACK_H_
#define GARNET_BIN_COBALT_APP_PRODUCT_HACK_H_

namespace cobalt {
namespace hack {

std::string GetLayer() {
  std::ifstream ifs;

  // If the System UI is there, the layer is topaz.
  ifs.open("/pkgfs/packages/sysui");
  if (ifs.good()) {
    ifs.close();
    return "topaz";
  }
  ifs.close();

  // If the Ledger is there, the layer is peridot.
  ifs.open("/pkgfs/packages/ledger");
  if (ifs.good()) {
    ifs.close();
    return "peridot";
  }
  ifs.close();

  // Since the Cobalt client is in the garnet layer, this is the lowest layer
  // we could be running on.
  return "garnet";
}

}  // namespace hack
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_PRODUCT_HACK_H_
