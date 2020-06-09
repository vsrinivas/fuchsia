// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/sys_tests.h"

#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace run {

bool should_run_in_sys(const std::string& url) {
  component::FuchsiaPkgUrl furl;
  furl.Parse(url);
  std::string simplified_url = fxl::Substitute("fuchsia-pkg://$0/$1#$2", furl.host_name(),
                                               furl.package_name(), furl.resource_path());

  return kUrlSet.count(simplified_url) == 1;
}

}  // namespace run
