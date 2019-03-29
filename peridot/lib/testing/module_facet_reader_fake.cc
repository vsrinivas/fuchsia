// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/module_facet_reader_fake.h"

#include <src/lib/fxl/logging.h>

namespace modular {

void ModuleFacetReaderFake::SetGetModuleManifestSink(
    fit::function<void(const std::string&, GetModuleManifestCallback)> sink) {
  sink_ = std::move(sink);
}

void ModuleFacetReaderFake::GetModuleManifest(
    const std::string& module_url, GetModuleManifestCallback callback) {
  if (sink_) {
    sink_(module_url, std::move(callback));
  } else {
    callback({});
  }
}

}  // namespace modular
