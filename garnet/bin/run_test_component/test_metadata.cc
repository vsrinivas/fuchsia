// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/test_metadata.h"

#include <unordered_set>

#include "garnet/lib/cmx/cmx.h"
#include "src/lib/fxl/strings/substitute.h"

#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/net/stack/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>

namespace run {
namespace {

using fxl::Substitute;

constexpr char kInjectedServices[] = "injected-services";
constexpr char kSystemServices[] = "system-services";

const std::unordered_set<std::string> kAllowedSystemServices = {
    fuchsia::net::Connectivity::Name_,
    fuchsia::net::SocketProvider::Name_,
    fuchsia::net::stack::Stack::Name_,
    fuchsia::netstack::Netstack::Name_,
};
}  // namespace

TestMetadata::TestMetadata() {}
TestMetadata::~TestMetadata() {}

fuchsia::sys::LaunchInfo TestMetadata::GetLaunchInfo(
    const rapidjson::Document::ValueType& value, const std::string& name) {
  fuchsia::sys::LaunchInfo launch_info;
  if (value.IsString()) {
    launch_info.url = value.GetString();
    return launch_info;
  }

  if (value.IsArray()) {
    const auto& array = value.GetArray();
    // If the element is an array, ensure it is non-empty and all values are
    // strings.
    if (!array.Empty() && std::all_of(array.begin(), array.end(),
                                      [](const rapidjson::Value& val) {
                                        return val.IsString();
                                      })) {
      launch_info.url = array[0].GetString();
      for (size_t i = 1; i < array.Size(); ++i) {
        launch_info.arguments.push_back(array[i].GetString());
      }
      return launch_info;
    }
  }

  json_parser_.ReportError(Substitute(
      "'$0' must be a string or a non-empty array of strings.", name));
  return launch_info;
}

bool TestMetadata::ParseFromFile(const std::string& cmx_file_path) {
  component::CmxMetadata cmx;
  cmx.ParseFromFileAt(-1, cmx_file_path, &json_parser_);
  if (json_parser_.HasError()) {
    return false;
  }
  auto& fuchsia_test = cmx.GetFacet(kFuchsiaTest);
  if (!fuchsia_test.IsNull()) {
    null_ = false;
    if (!fuchsia_test.IsObject()) {
      json_parser_.ReportError(
          Substitute("'$0' in 'facets' should be an object.", kFuchsiaTest));
      return false;
    }
    auto allow_network_services = fuchsia_test.FindMember(kSystemServices);
    if (allow_network_services != fuchsia_test.MemberEnd()) {
      if (!allow_network_services->value.IsArray()) {
        json_parser_.ReportError(
            Substitute("'$0' in '$1' should be a string array.",
                       kSystemServices, kFuchsiaTest));
      } else {
        const auto& array = allow_network_services->value.GetArray();
        std::all_of(
            array.begin(), array.end(), [&](const rapidjson::Value& val) {
              if (!val.IsString()) {
                json_parser_.ReportError(
                    Substitute("'$0' in '$1' should be a string array.",
                               kSystemServices, kFuchsiaTest));
                return false;
              }
              std::string service = val.GetString();
              if (kAllowedSystemServices.count(service) == 0) {
                json_parser_.ReportError(fxl::Substitute(
                    "'$0' cannot contain '$1'.", kSystemServices, service));
                return false;
              }
              system_services_.push_back(service);
              return true;
            });
      }
    }
    auto services = fuchsia_test.FindMember(kInjectedServices);
    if (services != fuchsia_test.MemberEnd()) {
      if (!services->value.IsObject()) {
        json_parser_.ReportError(Substitute("'$0' in '$1' should be an object.",
                                            kInjectedServices, kFuchsiaTest));
        return false;
      }
      for (auto itr = services->value.MemberBegin();
           itr != services->value.MemberEnd(); ++itr) {
        if (!itr->name.IsString()) {
          json_parser_.ReportError(
              Substitute("'$0' in '$1' should define string service names.",
                         kInjectedServices, kFuchsiaTest));
          return false;
        }
        auto name = itr->name.GetString();
        auto launch_info = GetLaunchInfo(itr->value, name);
        service_url_pair_.push_back(
            std::make_pair(name, std::move(launch_info)));
      }
    }
  }
  return !HasError();
}

}  // namespace run
