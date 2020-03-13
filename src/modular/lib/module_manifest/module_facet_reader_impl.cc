// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/module_manifest/module_facet_reader_impl.h"

#include "lib/sys/cpp/component_context.h"
#include "src/lib/cmx/facet_parser/cmx_facet_parser.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/lib/json_parser/pretty_print.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"
#include "src/lib/pkg_url/url_resolver.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/lib/fidl/json_xdr.h"
#include "src/modular/lib/module_manifest/module_manifest_xdr.h"

namespace modular {
namespace {

constexpr char kModuleFacetName[] = "fuchsia.module";

}  // namespace

ModuleFacetReaderImpl::ModuleFacetReaderImpl(fuchsia::sys::LoaderPtr loader)
    : loader_(std::move(loader)) {}

ModuleFacetReaderImpl::~ModuleFacetReaderImpl() {}

void ModuleFacetReaderImpl::GetModuleManifest(const std::string& module_url,
                                              GetModuleManifestCallback callback) {
  auto canonical_url = component::CanonicalizeURL(module_url);
  loader_->LoadUrl(canonical_url, [canonical_url, callback = std::move(callback)](
                                      fuchsia::sys::PackagePtr package) {
    if (!package) {
      callback({});
      return;
    }
    fbl::unique_fd fd = fsl::OpenChannelAsFileDescriptor(std::move(package->directory));

    component::FuchsiaPkgUrl pkg_url;
    std::string cmx_path;
    if (pkg_url.Parse(package->resolved_url)) {
      if (!pkg_url.resource_path().empty()) {
        // If the url has a resource, assume that's the cmx.
        cmx_path = pkg_url.resource_path();
      } else {
        // It's possible the url does not have a resource, in which case
        // either the cmx exists at meta/<package_name.cmx> or it does not
        // exist.
        cmx_path = pkg_url.GetDefaultComponentCmxPath();
      }
    } else {
      callback({});
      return;
    }

    component::CmxFacetParser facet_parser;
    json::JSONParser json_parser;
    if (!facet_parser.ParseFromFileAt(fd.get(), cmx_path, &json_parser)) {
      FX_LOGS(WARNING) << "Could not parse CMX manifest " << cmx_path << ": "
                       << json_parser.error_str();
      callback({});
      return;
    }

    const auto& mod_facet = facet_parser.GetSection(kModuleFacetName);
    if (mod_facet.IsNull()) {
      callback({});
      return;
    }

    fuchsia::modular::ModuleManifestPtr module_manifest;
    auto mod_facet_str = json_parser::JsonValueToString(mod_facet);
    if (!XdrRead(mod_facet_str, &module_manifest, XdrModuleManifest)) {
      FX_LOGS(WARNING) << "Unable to parse manifest module facet for " << package->resolved_url
                       << ": " << mod_facet_str;
      callback({});
      return;
    }
    // TODO(MF-94): Deprecate ModuleManfiest.binary in favour of getting it
    // from the cmx manifest.
    module_manifest->binary = canonical_url;
    callback(std::move(module_manifest));
  });
}

}  // namespace modular
