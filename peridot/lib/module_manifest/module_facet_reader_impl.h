// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_MODULE_FACET_READER_IMPL_H_
#define PERIDOT_LIB_MODULE_MANIFEST_MODULE_FACET_READER_IMPL_H_

#include <fuchsia/sys/cpp/fidl.h>

#include "src/lib/fxl/macros.h"
#include "peridot/lib/module_manifest/module_facet_reader.h"

namespace modular {

class ModuleFacetReaderImpl : public ModuleFacetReader {
 public:
  // This ModuleFacetReader implementation uses the fuchsia.sys.Loader interface
  // to retreive information for where the module facet is located.
  explicit ModuleFacetReaderImpl(fuchsia::sys::LoaderPtr loader);
  ~ModuleFacetReaderImpl() override;

 private:
  void GetModuleManifest(const std::string& module_url,
                         GetModuleManifestCallback callback) override;

  fuchsia::sys::LoaderPtr loader_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleFacetReaderImpl);
};

}  // namespace modular

#endif  // PERIDOT_LIB_MODULE_MANIFEST_MODULE_FACET_READER_IMPL_H_
