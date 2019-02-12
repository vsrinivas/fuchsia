// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULE_MANIFEST_MODULE_FACET_READER_H_
#define PERIDOT_LIB_MODULE_MANIFEST_MODULE_FACET_READER_H_

#include <string>

#include <fuchsia/modular/cpp/fidl.h>

namespace modular {

// |ModuleFacetReader| provides a way to read the the module facet declared in
// a module's component manifest.
class ModuleFacetReader {
 public:
  // Given a module URL, returns the |fuchsia.modular.ModuleManifestPtr|
  // declared in the module's component manifest. A null |manifest| is returned
  // if the module does not have a module facet declared.
  using GetModuleManifestCallback =
      fit::function<void(fuchsia::modular::ModuleManifestPtr manifest)>;
  virtual void GetModuleManifest(const std::string& module_url,
                                 GetModuleManifestCallback callback) = 0;

  virtual ~ModuleFacetReader() = default;
};

}  // namespace modular

#endif  // PERIDOT_LIB_MODULE_MANIFEST_MODULE_FACET_READER_H_
