// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utility.h"

#include <fcntl.h>
#include <lib/zx/channel.h>
#include <lib/zx/profile.h>
#include <lib/zx/resource.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <tuple>

#include "fuchsia/boot/cpp/fidl.h"
#include "fuchsia/scheduler/cpp/fidl.h"
#include "lib/fdio/directory.h"
#include "src/lib/fxl/logging.h"

namespace {

// Returns a reference to a ProfileProvider proxy. Initializes the proxy on the
// first invocation, otherwise returns a reference to the existing proxy.
fuchsia::scheduler::ProfileProvider_SyncProxy& GetProfileProvider() {
  static std::optional<fuchsia::scheduler::ProfileProvider_SyncProxy> provider;
  static std::mutex mutex;

  std::lock_guard<std::mutex> guard{mutex};

  if (provider.has_value()) {
    return *provider;
  }

  // Connect to the scheduler profile service.
  zx::channel channel0, channel1;
  zx_status_t status;

  status = zx::channel::create(0u, &channel0, &channel1);
  FXL_CHECK(status == ZX_OK);

  status = fdio_service_connect(
      (std::string("/svc/") + fuchsia::scheduler::ProfileProvider::Name_).c_str(),
      channel0.release());
  FXL_CHECK(status == ZX_OK);

  provider.emplace(std::move(channel1));
  return *provider;
}

}  // anonymous namespace

// Represents ordering of CPU affinity masks.
enum class MaskRelation {
  Less,
  Equal,
  Greater,
};

// Compares the given affinity masks, defining a total order of masks. This
// method is templatized to handle changes in the size of the affinity mask
// array in zx_cpu_set_t.
template <size_t Size>
constexpr MaskRelation CompareMasks(const uint64_t (&a)[Size], const uint64_t (&b)[Size]) {
  for (size_t i = 0; i < Size; i++) {
    if (a[i] < b[i]) {
      return MaskRelation::Less;
    }
    if (a[i] > b[i]) {
      return MaskRelation::Greater;
    }
  }
  return MaskRelation::Equal;
}

// Provide comparisons needed for std::map.
constexpr bool operator==(const zx_cpu_set_t& a, const zx_cpu_set_t& b) {
  return CompareMasks(a.mask, b.mask) == MaskRelation::Equal;
}
constexpr bool operator<(const zx_cpu_set_t& a, const zx_cpu_set_t& b) {
  return CompareMasks(a.mask, b.mask) == MaskRelation::Less;
}

std::chrono::nanoseconds ParseDurationString(const std::string& duration) {
  // Match one or more digits, optionally followed by time units m, s, ms, us,
  // or ns.
  static const std::regex kReDuration{"^\\d+(m|s|ms|us|ns)?$"};

  std::smatch match;
  std::regex_search(duration, match, kReDuration);
  FXL_CHECK(!match.empty()) << "String \"" << duration << "\" is not a valid duration!";

  FXL_CHECK(match.size() == 2) << "Unexpected match size " << match.size();
  const uint64_t scalar = std::stoull(match[0]);
  const std::string units = match[1];

  if (units == "") {
    return std::chrono::nanoseconds{scalar};
  } else if (units == "m") {
    return std::chrono::minutes{scalar};
  } else if (units == "s") {
    return std::chrono::seconds{scalar};
  } else if (units == "ms") {
    return std::chrono::milliseconds{scalar};
  } else if (units == "us") {
    return std::chrono::microseconds{scalar};
  } else if (units == "ns") {
    return std::chrono::nanoseconds{scalar};
  } else {
    FXL_CHECK(false) << "String duration \"" << duration << "\" has unrecognized units \"" << units
                     << "\"!";
    __builtin_unreachable();
  }
}

// Returns an unowned handle to a profile for the specified priority. Maintains
// an internal map of already requested profiles and returns the same handle for
// multiple requests for the same priority.
zx::unowned_profile GetProfile(int priority, std::optional<zx_cpu_set_t> affinity) {
  using Key = std::tuple<int, std::optional<zx_cpu_set_t>>;
  const Key key{priority, affinity};

  // Maintains a map of profiles for each previously requested priority/affinity.
  static std::map<Key, zx::profile> profiles;
  static std::mutex mutex;

  std::lock_guard<std::mutex> guard{mutex};

  // Return the existing handle if it's already in the map.
  auto search = profiles.find(key);
  if (search != profiles.end()) {
    return zx::unowned_profile{search->second.get()};
  }

  auto& provider = GetProfileProvider();

  zx_status_t fidl_status;
  zx::profile profile;
  const auto status = provider.GetProfile(priority, "garnet/bin/loadbench", &fidl_status, &profile);
  FXL_CHECK(status == ZX_OK);
  FXL_CHECK(fidl_status == ZX_OK);

  // Add the new profile to the map for later retrieval.
  auto [iter, okay] = profiles.emplace(key, std::move(profile));
  FXL_CHECK(okay);

  return zx::unowned_profile{iter->second.get()};
}

// Returns an unowned handle to a profile for the specified deadline parameters.
// Maintains an internal map of already requested profiles and returns the same
// handle for multiple request for the same deadline parameters.
zx::unowned_profile GetProfile(zx::duration capacity, zx::duration deadline, zx::duration period,
                               std::optional<zx_cpu_set_t> affinity) {
  using Key = std::tuple<zx_duration_t, zx_duration_t, zx_duration_t, std::optional<zx_cpu_set_t>>;
  const Key key{capacity.get(), deadline.get(), period.get(), affinity};

  // Maintains a map of profiles for each previously requested deadline/affinity.
  static std::map<Key, zx::profile> profiles;
  static std::mutex mutex;

  std::lock_guard<std::mutex> guard{mutex};

  // Return the existing handle if it's already in the map.
  auto search = profiles.find(key);
  if (search != profiles.end()) {
    return zx::unowned_profile{search->second.get()};
  }

  auto& provider = GetProfileProvider();

  zx_status_t fidl_status;
  zx::profile profile;
  const auto status = provider.GetDeadlineProfile(capacity.get(), deadline.get(), period.get(),
                                                  "garnet/bin/loadbench", &fidl_status, &profile);
  FXL_CHECK(status == ZX_OK);
  FXL_CHECK(fidl_status == ZX_OK);

  // Add the new profile to the map for later retrieval.
  auto [iter, okay] = profiles.emplace(key, std::move(profile));
  FXL_CHECK(okay);

  return zx::unowned_profile{iter->second.get()};
}

zx::unowned_resource GetRootResource() {
  static zx::resource root_resource;
  static std::mutex mutex;

  std::lock_guard<std::mutex> guard{mutex};

  if (root_resource) {
    return zx::unowned_resource{root_resource.get()};
  }

  // Connect to the root resource.
  zx::channel channel0, channel1;
  zx_status_t status;

  status = zx::channel::create(0u, &channel0, &channel1);
  FXL_CHECK(status == ZX_OK);

  status = fdio_service_connect((std::string("/svc/") + fuchsia::boot::RootResource::Name_).c_str(),
                                channel0.get());
  FXL_CHECK(status == ZX_OK);

  fuchsia::boot::RootResource_SyncProxy proxy(std::move(channel1));
  status = proxy.Get(&root_resource);
  FXL_CHECK(status == ZX_OK);

  return zx::unowned_resource{root_resource.get()};
}

size_t ReadCpuCount() {
  size_t actual, available;
  const auto status =
      GetRootResource()->get_info(ZX_INFO_CPU_STATS, nullptr, 0, &actual, &available);
  FXL_CHECK(status == ZX_OK);
  return available;
}

void ReadCpuStats(zx_info_cpu_stats_t* stats_buffer, size_t record_count) {
  size_t actual, available;
  const auto buffer_size = sizeof(zx_info_cpu_stats_t) * record_count;
  const auto status = GetRootResource()->get_info(ZX_INFO_CPU_STATS, stats_buffer, buffer_size,
                                                  &actual, &available);
  FXL_CHECK(status == ZX_OK);
}
