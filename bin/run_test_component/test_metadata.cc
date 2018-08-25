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

TestMetadata::TestMetadata() {}

TestMetadata::~TestMetadata() {}

TestMetadata TestMetadata::CreateFromFile(const std::string& cmx_file_path) {
  TestMetadata test_metadata;
  test_metadata.has_error_ = true;
  // TODO(geb): Use JSONParser to report parsing errors.
  component::CmxMetadata cmx;
  cmx.ParseFromFileAt(-1, cmx_file_path);
  if (cmx.HasError()) {
    test_metadata.errors_.push_back(
        Substitute("parse error: $0", cmx.error_str()));
    return test_metadata;
  }
  auto& fuchisa_test = cmx.facets_meta().GetSection(kFuchsiaTest);
  if (!fuchisa_test.IsNull()) {
    test_metadata.null_ = false;
    if (!fuchisa_test.IsObject()) {
      test_metadata.errors_.push_back(
          Substitute("'$0' in 'facets' should be an object", kFuchsiaTest));
      return test_metadata;
    }
    auto services = fuchisa_test.FindMember(kInjectedServices);
    if (services != fuchisa_test.MemberEnd()) {
      if (!services->value.IsObject()) {
        test_metadata.errors_.push_back(
            Substitute("'$0' in '$1' should be an object", kInjectedServices,
                       kFuchsiaTest));
        return test_metadata;
      }
      for (auto itr = services->value.MemberBegin();
           itr != services->value.MemberEnd(); ++itr) {
        if (!itr->value.IsString() || !itr->name.IsString()) {
          test_metadata.errors_.push_back(
              Substitute("'$0' in '$1' should define string pairs.",
                         kInjectedServices, kFuchsiaTest));
          return test_metadata;
        }
        test_metadata.service_url_pair_.push_back(
            {itr->name.GetString(), itr->value.GetString()});
      }
    }
  }
  test_metadata.has_error_ = false;
  return test_metadata;
}

}  // namespace run
