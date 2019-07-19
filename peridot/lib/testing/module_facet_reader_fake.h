// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_TESTING_MODULE_FACET_READER_FAKE_H_
#define PERIDOT_LIB_TESTING_MODULE_FACET_READER_FAKE_H_

#include <functional>
#include <string>

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/lib/module_manifest/module_facet_reader.h"

namespace modular {

// A fake implementation of ModuleFacetReader used for testing. Use |set_sink()|
// to capture GetModuleManifest calls.
class ModuleFacetReaderFake : public ModuleFacetReader {
 public:
  // Call this method to supply a sink for
  // ModuleFacetReader.GetModuleManifest().
  void SetGetModuleManifestSink(
      fit::function<void(const std::string&, GetModuleManifestCallback)> sink);

 private:
  // |modular::ModuleFacetReader|
  void GetModuleManifest(const std::string& module_url, GetModuleManifestCallback) override;

  fit::function<void(const std::string&, GetModuleManifestCallback)> sink_;
};

}  // namespace modular

#endif  // PERIDOT_LIB_TESTING_MODULE_FACET_READER_FAKE_H_
