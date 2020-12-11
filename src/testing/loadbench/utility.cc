// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utility.h"

#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>
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

#include "fuchsia/kernel/cpp/fidl.h"
#include "fuchsia/scheduler/cpp/fidl.h"
#include "lib/fdio/directory.h"

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
  FX_CHECK(status == ZX_OK);

  status = fdio_service_connect(
      (std::string("/svc/") + fuchsia::scheduler::ProfileProvider::Name_).c_str(),
      channel0.release());
  FX_CHECK(status == ZX_OK);

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
  FX_CHECK(!match.empty()) << "String \"" << duration << "\" is not a valid duration!";

  FX_CHECK(match.size() == 2) << "Unexpected match size " << match.size();
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
    FX_CHECK(false) << "String duration \"" << duration << "\" has unrecognized units \"" << units
                    << "\"!";
    __builtin_unreachable();
  }
}

// Parses an expression of the form "cpu_num<+|-|*><positive integer>" and returns evaluated result
// as an integer.
size_t ParseInstancesString(const std::string& instances) {
  static const std::regex kReInstances{"(^[a-zA-Z_]+)((\\+|\\*|-)(\\d+))?$"};

  // Match[0]: Full match
  // Match[1]: "num_cpu"
  // Match[2]: <operation><argument>
  // Match[3]: <operation>
  // Match[4]: <argument>
  std::smatch match;

  FX_CHECK(std::regex_search(instances, match, kReInstances))
      << "The expression string must be in the format cpu_num<+|-|*><positive integer>.";
  FX_CHECK(match[1] == "cpu_num")
      << "The expression string must be in the format cpu_num<+|-|*><positive integer>.";
  FX_CHECK(match.size() == 5) << "Unexpected match size " << match.size();

  const std::string operation = match[3];

  if (operation == "") {
    return ReadCpuCount();
  } else {
    const size_t argument = std::stoull(match[4]);

    if (operation == "+") {
      return ReadCpuCount() + argument;
    } else if (operation == "-") {
      if (argument > ReadCpuCount()) {
        FX_LOGS(WARNING) << "Expression " << instances
                         << " yields negative number. Instances set to 0";
        return 0;
      } else {
        return ReadCpuCount() - argument;
      }
    } else if (operation == "*") {
      return ReadCpuCount() * argument;
    } else {
      __builtin_unreachable();
    }
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
  FX_CHECK(status == ZX_OK);
  FX_CHECK(fidl_status == ZX_OK);

  // Add the new profile to the map for later retrieval.
  auto [iter, okay] = profiles.emplace(key, std::move(profile));
  FX_CHECK(okay);

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
  FX_CHECK(status == ZX_OK);
  FX_CHECK(fidl_status == ZX_OK);

  // Add the new profile to the map for later retrieval.
  auto [iter, okay] = profiles.emplace(key, std::move(profile));
  FX_CHECK(okay);

  return zx::unowned_profile{iter->second.get()};
}

zx::unowned_resource GetDebugResource() {
  static zx::resource debug_resource;
  static std::mutex mutex;

  std::lock_guard<std::mutex> guard{mutex};

  if (debug_resource) {
    return zx::unowned_resource{debug_resource.get()};
  }

  // Connect to the debug resource.
  zx::channel channel0, channel1;
  zx_status_t status;

  status = zx::channel::create(0u, &channel0, &channel1);
  FX_CHECK(status == ZX_OK);

  status = fdio_service_connect(
      (std::string("/svc/") + fuchsia::kernel::DebugResource::Name_).c_str(), channel0.release());
  FX_CHECK(status == ZX_OK);

  fuchsia::kernel::DebugResource_SyncProxy proxy(std::move(channel1));
  status = proxy.Get(&debug_resource);
  FX_CHECK(status == ZX_OK);

  return zx::unowned_resource{debug_resource.get()};
}

zx::unowned_resource GetInfoResource() {
  static zx::resource info_resource;
  static std::mutex mutex;

  std::lock_guard<std::mutex> guard{mutex};

  if (info_resource) {
    return zx::unowned_resource{info_resource.get()};
  }

  // Connect to the info resource.
  zx::channel channel0, channel1;
  zx_status_t status;

  status = zx::channel::create(0u, &channel0, &channel1);
  FX_CHECK(status == ZX_OK);

  status = fdio_service_connect(
      (std::string("/svc/") + fuchsia::kernel::InfoResource::Name_).c_str(), channel0.release());
  FX_CHECK(status == ZX_OK);

  fuchsia::kernel::InfoResource_SyncProxy proxy(std::move(channel1));
  status = proxy.Get(&info_resource);
  FX_CHECK(status == ZX_OK);

  return zx::unowned_resource{info_resource.get()};
}

size_t ReadCpuCount() {
  size_t actual, available;
  const auto status =
      GetInfoResource()->get_info(ZX_INFO_CPU_STATS, nullptr, 0, &actual, &available);
  FX_CHECK(status == ZX_OK);
  return available;
}

void ReadCpuStats(zx_info_cpu_stats_t* stats_buffer, size_t record_count) {
  size_t actual, available;
  const auto buffer_size = sizeof(zx_info_cpu_stats_t) * record_count;
  const auto status = GetInfoResource()->get_info(ZX_INFO_CPU_STATS, stats_buffer, buffer_size,
                                                  &actual, &available);
  FX_CHECK(status == ZX_OK);
}
