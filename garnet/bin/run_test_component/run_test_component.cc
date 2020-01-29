// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/run_test_component.h"

#include <fuchsia/sys/index/cpp/fidl.h>
#include <glob.h>
#include <lib/fit/defer.h>
#include <lib/sys/cpp/service_directory.h>

#include <regex>
#include <string>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace run {

using fuchsia::sys::index::ComponentIndex_FuzzySearch_Result;
using fuchsia::sys::index::ComponentIndexSyncPtr;

static constexpr char kComponentIndexerUrl[] =
    "fuchsia-pkg://fuchsia.com/component_index#meta/component_index.cmx";

static constexpr char kLabelArgPrefix[] = "--realm-label=";

ParseArgsResult ParseArgs(const std::shared_ptr<sys::ServiceDirectory>& services, int argc,
                          const char** argv) {
  ParseArgsResult result;
  result.error = false;
  int url_or_matcher_argi = 1;

  std::string url;
  while (true) {
    if (argc < url_or_matcher_argi + 1) {
      result.error = true;
      result.error_msg = "Missing test URL, or matcher argument";
      return result;
    }

    std::string argument = argv[url_or_matcher_argi];
    const size_t kLabelArgPrefixLength = strlen(kLabelArgPrefix);
    if (argument.substr(0, kLabelArgPrefixLength) == kLabelArgPrefix) {
      result.realm_label = argument.substr(kLabelArgPrefixLength);
      url_or_matcher_argi++;
      continue;
    }

    url = argument;
    break;
  }

  if (!component::FuchsiaPkgUrl::IsFuchsiaPkgScheme(url)) {
    fuchsia::sys::LaunchInfo index_launch_info;
    index_launch_info.url = kComponentIndexerUrl;
    auto index_provider =
        sys::ServiceDirectory::CreateWithRequest(&index_launch_info.directory_request);

    // Connect to the Launcher service through our static environment.
    fuchsia::sys::LauncherSyncPtr launcher;
    services->Connect(launcher.NewRequest());
    fuchsia::sys::ComponentControllerPtr component_index_controller;
    launcher->CreateComponent(std::move(index_launch_info),
                              component_index_controller.NewRequest());

    ComponentIndexSyncPtr index;
    index_provider->Connect(index.NewRequest());

    std::string test_name = url;
    ComponentIndex_FuzzySearch_Result fuzzy_search_result;
    zx_status_t status = index->FuzzySearch(test_name, &fuzzy_search_result);
    if (status != ZX_OK) {
      result.error = true;
      result.error_msg = fxl::StringPrintf(
          "\"%s\" is not a valid URL. Attempted to match to a URL with "
          "fuchsia.sys.index.FuzzySearch, but the service is not available.",
          test_name.c_str());
      return result;
    }

    if (fuzzy_search_result.is_err()) {
      result.error = true;
      result.error_msg = fxl::StringPrintf(
          "\"%s\" contains unsupported characters for fuzzy "
          "matching. Valid characters are [A-Z a-z 0-9 / _ - .].\n",
          test_name.c_str());
      return result;
    } else {
      std::vector<std::string> uris = fuzzy_search_result.response().uris;
      if (uris.size() == 0) {
        result.error = true;
        result.error_msg =
            fxl::StringPrintf("\"%s\" did not match any components.\n", test_name.c_str());
        return result;
      } else {
        for (auto& uri : uris) {
          result.matching_urls.push_back(uri);
        }
        if (uris.size() > 1) {
          return result;
        }
        url = uris[0];
      }
    }
  }

  result.launch_info.url = url;
  result.launch_info.arguments.emplace();
  for (int i = url_or_matcher_argi + 1; i < argc; i++) {
    result.launch_info.arguments->push_back(argv[i]);
  }
  return result;
}

}  // namespace run
