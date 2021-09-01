// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_REMOTE_V2_COMMON_UTILS_H_
#define LIB_ZXIO_REMOTE_V2_COMMON_UTILS_H_

#include <fidl/fuchsia.io2/cpp/wire.h>
#include <lib/zxio/ops.h>

// Conversion adaptors between zxio and FIDL types.

zxio_node_protocols_t ToZxioNodeProtocols(fuchsia_io2::wire::NodeProtocols protocols);

fuchsia_io2::wire::NodeProtocols ToIo2NodeProtocols(zxio_node_protocols_t zxio_protocols);

zxio_abilities_t ToZxioAbilities(fuchsia_io2::wire::Operations abilities);

fuchsia_io2::wire::Operations ToIo2Abilities(zxio_abilities_t zxio_abilities);

#endif  // LIB_ZXIO_REMOTE_V2_COMMON_UTILS_H_
