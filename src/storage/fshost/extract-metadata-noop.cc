// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extract-metadata.h"

namespace fshost {

bool ExtractMetadataEnabled() { return false; }

void MaybeDumpMetadata(fbl::unique_fd device_fd, DumpMetadataOptions options) {}

}  // namespace fshost
