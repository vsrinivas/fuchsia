// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cpuid.h>
#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/service/llcpp/service.h>
#include <lib/zircon-internal/device/cpu-trace/intel-pm.h>
#include <lib/zircon-internal/mtrace.h>
#include <lib/zx/channel.h>
#include <lib/zx/resource.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

namespace {

zx::status<zx::resource> GetRootResource() {
  zx::status<fidl::ClientEnd<fuchsia_boot::RootResource>> client_end =
      service::Connect<fuchsia_boot::RootResource>();
  if (client_end.is_error()) {
    printf("mtrace: Could not connect to RootResource service: %s\n", client_end.status_string());
    return zx::error(client_end.error_value());
  }

  fidl::WireSyncClient<fuchsia_boot::RootResource> client =
      fidl::BindSyncClient(std::move(client_end.value()));
  fidl::WireResult<fuchsia_boot::RootResource::Get> result = client.Get();
  if (result.status() != ZX_OK) {
    printf("mtrace: Could not retrieve RootResource: %s\n", zx_status_get_string(result.status()));
    return zx::error(result.status());
  }
  return zx::ok(std::move(result.Unwrap()->resource));
}

std::optional<uint8_t> IntelArchitecturalPMUVersion() {
  uint32_t eax, ebx, ecx, edx;
  if (__get_cpuid_count(0xa, 0, &eax, &ebx, &ecx, &edx) == 1) {
    return eax & 0xf;
  }
  return std::nullopt;
}

bool IsIntelPMUSupported() {
  static constexpr uint8_t kMinimumSupportedArchitecturalPMUVersion = 4;
  std::optional<uint8_t> version = IntelArchitecturalPMUVersion();
  if (version) {
    return *version >= kMinimumSupportedArchitecturalPMUVersion;
  }
  return false;
}

TEST(X86MtraceTestCase, GetProperties) {
  perfmon::X86PmuProperties properties;
  zx::status<zx::resource> root_resource_or_error = GetRootResource();
  zx::resource root_resource = std::move(root_resource_or_error.value());
  ASSERT_TRUE(root_resource_or_error.is_ok(), "failed to get root resource: %s",
              zx_status_get_string(root_resource_or_error.error_value()));
  zx_status_t status =
      zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON, MTRACE_PERFMON_GET_PROPERTIES, 0,
                        &properties, sizeof(properties));
  if (IsIntelPMUSupported()) {
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(properties.common.pm_version, IntelArchitecturalPMUVersion());
  } else {
    printf("Skipping test, Intel Architectual PMU not supported\n");
    EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
  }
}

}  // namespace
