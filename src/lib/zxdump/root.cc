// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zxdump/task.h>

#include <array>
#include <string_view>

namespace zxdump {
namespace {

template <typename T, class Service, auto Member>
fitx::result<Error, T> GetFromService() {
  constexpr const char* kSvcName = fidl::DiscoverableProtocolDefaultPath<Service>;

  static constexpr auto kGetName = []() {
    constexpr std::string_view kProtocolName = fidl::DiscoverableProtocolName<Service>;
    constexpr std::string_view kSuffix{".Get"};
    std::array<char, kProtocolName.size() + kSuffix.size()> name{};
    // string_view::copy is only constexpr in C++20.
    auto out = name.begin();
    for (std::string_view str : {kProtocolName, kSuffix}) {
      for (char c : str) {
        *out++ = c;
      }
    }
    return name;
  }();

  constexpr std::string_view kCallName{kGetName.data(), kGetName.size()};

  fidl::ClientEnd<Service> client;
  if (auto result = component::Connect<Service>(); result.is_error()) {
    return fitx::error{Error{kSvcName, result.status_value()}};
  } else {
    client = std::move(result).value();
  }

  auto result = fidl::WireSyncClient(std::move(client))->Get();
  if (result.ok()) {
    return fitx::ok(std::move(result.value().*Member));
  }
  return fitx::error{Error{kCallName, result.status()}};
}

}  // namespace

fitx::result<Error, LiveTask> GetRootJob() {
  return GetFromService<LiveTask, fuchsia_kernel::RootJob,
                        &fidl::WireResponse<fuchsia_kernel::RootJob::Get>::job>();
}

}  // namespace zxdump
