// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <glob.h>

#include <lib/fit/defer.h>

#include "garnet/bin/run_test_component/run_test_component.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

#include <regex>
#include <string>

namespace run {

// path is <package_name>/*/meta/<test>.cmx
static const std::regex* const kCmxPath =
    new std::regex("^([^/]+)/[^/]+/(meta/[^\\.]+\\.cmx)$");

std::string GetComponentManifestPath(const std::string& url) {
  if (component::FuchsiaPkgUrl::IsFuchsiaPkgScheme(url)) {
    component::FuchsiaPkgUrl fp;
    if (!fp.Parse(url)) {
      return "";
    }
    return fxl::StringPrintf("%s/%s", fp.pkgfs_dir_path().c_str(),
                             fp.resource_path().c_str());
  }
  return "";
}

std::string GenerateComponentUrl(const std::string& cmx_file_path) {
  std::smatch sm;
  if (!(std::regex_search(cmx_file_path, sm, *kCmxPath))) {
    return "";
  }
  return fxl::StringPrintf("fuchsia-pkg://fuchsia.com/%s#%s",
                           sm[1].str().c_str(), sm[2].str().c_str());
}

ParseArgsResult ParseArgs(int argc, const char** argv,
                          const std::string& glob_dir) {
  ParseArgsResult result;
  result.error = false;
  if (argc < 2) {
    result.error = true;
    result.error_msg = "Pass atleast one argument";
    return result;
  }
  std::string url = argv[1];
  result.cmx_file_path = GetComponentManifestPath(url);

  if (result.cmx_file_path == "") {
    // try to find cmx files
    std::string test_prefix = argv[1];
    if (test_prefix.find('*') != std::string::npos) {
      result.error = true;
      result.error_msg = "test prefix should not contain '*'";
      return result;
    }
    auto glob_str =
        fxl::StringPrintf("%s/*/*/meta/%s*.cmx", glob_dir.c_str(), argv[1]);
    glob_t globbuf;
    auto status = glob(glob_str.c_str(), 0, nullptr, &globbuf);
    if (status != 0) {
      result.error = true;
      if (status == GLOB_NOMATCH) {
        result.error_msg = fxl::StringPrintf(
            "cannot find test component with prefix '%s'", argv[1]);
      } else {
        result.error_msg =
            fxl::StringPrintf("glob failed on %s, glob return: %d, error: %s",
                              argv[1], status, strerror(errno));
      }
      return result;
    }
    auto guard = fit::defer([&]() { globfree(&globbuf); });
    for (size_t i = 0; i < globbuf.gl_pathc; i++) {
      result.matching_urls.push_back(
          GenerateComponentUrl(globbuf.gl_pathv[i] + glob_dir.length() + 1)
              .c_str());
    }
    result.cmx_file_path = globbuf.gl_pathv[0];
    if (globbuf.gl_pathc > 1) {
      return result;
    }
    url = result.matching_urls[0];
  } else {
  }
  result.launch_info.url = url;
  for (int i = 2; i < argc; i++) {
    result.launch_info.arguments.push_back(argv[i]);
  }
  return result;
}

}  // namespace run
