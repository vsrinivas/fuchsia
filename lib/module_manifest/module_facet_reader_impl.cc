// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/module_manifest/module_facet_reader_impl.h"

#include "lib/cmx_facet_parser/cmx_facet_parser.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fsl/io/fd.h"
#include "lib/json/json_parser.h"
#include "lib/pkg_url/fuchsia_pkg_url.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/module_manifest/module_manifest_xdr.h"

namespace modular {
namespace {

constexpr char kModuleFacetName[] = "fuchsia.module";

// Copied from //garnet/lib/pkg_url/url_resolver.cc
std::string CanonicalizeURL(const std::string& url) {
  constexpr char kFileUriPrefix[] = "file://";
  if (!url.empty() && url.find(':') == std::string::npos)
    return kFileUriPrefix + url;
  return url;
}

}  // namespace

ModuleFacetReaderImpl::ModuleFacetReaderImpl(fuchsia::sys::LoaderPtr loader)
    : loader_(std::move(loader)) {}

ModuleFacetReaderImpl::~ModuleFacetReaderImpl() {}

void ModuleFacetReaderImpl::GetModuleManifest(
    const std::string& module_url,
    std::function<void(fuchsia::modular::ModuleManifestPtr manifest)>
        callback) {
  auto canonical_url = CanonicalizeURL(module_url);
  loader_->LoadComponent(canonical_url, [canonical_url, callback](
                                            fuchsia::sys::PackagePtr package) {
    if (!package) {
      FXL_LOG(ERROR) << "Could not resolve URL: " << canonical_url;
      callback({});
      return;
    }
    fxl::UniqueFD fd =
        fsl::OpenChannelAsFileDescriptor(std::move(package->directory));

    component::FuchsiaPkgUrl pkg_url;
    if (!pkg_url.Parse(package->resolved_url)) {
      FXL_LOG(ERROR) << "Could not parse package URL: "
                     << package->resolved_url;
      callback({});
    }

    auto default_cmx_path = pkg_url.GetDefaultComponentCmxPath();
    component::CmxFacetParser facet_parser;
    json::JSONParser json_parser;
    if (!facet_parser.ParseFromFileAt(fd.get(), default_cmx_path,
                                      &json_parser)) {
      FXL_LOG(ERROR) << "Could not parse CMX manifest " << default_cmx_path
                     << ": " << json_parser.error_str();
      callback({});
      return;
    }

    const auto& mod_facet = facet_parser.GetSection(kModuleFacetName);
    if (mod_facet.IsNull()) {
      FXL_LOG(INFO) << "No module facet declared for module="
                    << package->resolved_url;
      callback({});
      return;
    }

    fuchsia::modular::ModuleManifestPtr module_manifest;
    auto mod_facet_str = JsonValueToString(mod_facet);
    if (!XdrRead(mod_facet_str, &module_manifest, XdrModuleManifest)) {
      FXL_LOG(WARNING) << "Unable to parse manifest module facet for "
                       << package->resolved_url << ": " << mod_facet_str;
      callback({});
      return;
    }

    callback(std::move(module_manifest));
  });
}

}  // namespace modular
