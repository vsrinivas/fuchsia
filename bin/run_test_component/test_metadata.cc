// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/test_metadata.h"

#include "garnet/lib/cmx/cmx.h"
#include "lib/fxl/strings/substitute.h"

namespace run {
namespace {

using fxl::Substitute;

constexpr char kInjectedServices[] = "injected-services";

}  // namespace

bool TestMetadata::ParseFromFile(const std::string& cmx_file_path) {
  component::CmxMetadata cmx;
  cmx.ParseFromFileAt(-1, cmx_file_path, &json_parser_);
  if (json_parser_.HasError()) {
    return false;
  }
  auto& fuchisa_test = cmx.facets_meta().GetSection(kFuchsiaTest);
  if (!fuchisa_test.IsNull()) {
    null_ = false;
    if (!fuchisa_test.IsObject()) {
      json_parser_.ReportError(
          Substitute("'$0' in 'facets' should be an object.", kFuchsiaTest));
      return false;
    }
    auto services = fuchisa_test.FindMember(kInjectedServices);
    if (services != fuchisa_test.MemberEnd()) {
      if (!services->value.IsObject()) {
        json_parser_.ReportError(
            Substitute("'$0' in '$1' should be an object.", kInjectedServices,
                       kFuchsiaTest));
        return false;
      }
      for (auto itr = services->value.MemberBegin();
           itr != services->value.MemberEnd(); ++itr) {
        if (!itr->value.IsString() || !itr->name.IsString()) {
          json_parser_.ReportError(
              Substitute("'$0' in '$1' has a non-string pair.",
                         kInjectedServices, kFuchsiaTest));
        } else {
          service_url_pair_.push_back(
              {itr->name.GetString(), itr->value.GetString()});
        }
      }
    }
  }
  return !HasError();
}

}  // namespace run
