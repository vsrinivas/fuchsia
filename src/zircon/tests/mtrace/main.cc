// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cpuid.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zircon-internal/device/cpu-trace/intel-pm.h>
#include <lib/zircon-internal/mtrace.h>
#include <lib/zx/channel.h>
#include <lib/zx/resource.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

namespace {

zx::result<zx::resource> GetRootResource() {
  zx::result<fidl::ClientEnd<fuchsia_boot::RootResource>> client_end =
      component::Connect<fuchsia_boot::RootResource>();
  if (client_end.is_error()) {
    printf("mtrace: Could not connect to RootResource service: %s\n", client_end.status_string());
    return zx::error(client_end.error_value());
  }

  fidl::WireSyncClient<fuchsia_boot::RootResource> client =
      fidl::WireSyncClient(std::move(client_end.value()));
  fidl::WireResult<fuchsia_boot::RootResource::Get> result = client->Get();
  if (result.status() != ZX_OK) {
    printf("mtrace: Could not retrieve RootResource: %s\n", zx_status_get_string(result.status()));
    return zx::error(result.status());
  }
  return zx::ok(std::move(result->resource));
}

std::optional<uint8_t> IntelArchitecturalPMUVersion() {
  uint32_t eax, ebx, ecx, edx;
  if (__get_cpuid_count(0xa, 0, &eax, &ebx, &ecx, &edx) == 1) {
    return eax & 0xf;
  }
  return std::nullopt;
}

bool IsIntelPMUSupported() {
  std::optional<uint8_t> version = IntelArchitecturalPMUVersion();
  if (version) {
    return *version >= MTRACE_X86_INTEL_PMU_MIN_SUPPORTED_VERSION &&
           *version <= MTRACE_X86_INTEL_PMU_MAX_SUPPORTED_VERSION;
  }
  return false;
}

TEST(X86MtraceTestCase, GetProperties) {
  perfmon::X86PmuProperties properties;
  zx::result<zx::resource> root_resource_or_error = GetRootResource();
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

// MTRACE_KIND_PERFMON expects MTRACE_PERFMON_INIT to be called before any performance tracing
// session and MTRACE_PERFMON_FINI to be called after any session. Check that they can be called.
//
// Note that MTRACE_KIND_PERFMON is currently single-master; only a single agent needs to or can
// invoke MTRACE_PERFMON_INIT / MTRACE_PERFMON_FINI at a time.
TEST(X86MtraceTestCase, InitFini) {
  zx::result<zx::resource> root_resource_or_error = GetRootResource();
  ASSERT_TRUE(root_resource_or_error.is_ok(), "failed to get root resource: %s",
              zx_status_get_string(root_resource_or_error.error_value()));
  zx::resource root_resource = std::move(root_resource_or_error.value());

  zx_status_t status = zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON,
                                         MTRACE_PERFMON_INIT, 0, nullptr, 0);
  if (!IsIntelPMUSupported()) {
    EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
    printf("Skipping test, Intel Architectual PMU not supported\n");
    return;
  }
  EXPECT_EQ(status, ZX_OK);

  // Double init doesn't work.
  status = zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON, MTRACE_PERFMON_INIT, 0,
                             nullptr, 0);
  EXPECT_EQ(status, ZX_ERR_BAD_STATE);

  status = zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON, MTRACE_PERFMON_FINI, 0,
                             nullptr, 0);
  EXPECT_EQ(status, ZX_OK);

  // Double-fini appears to work.
  status = zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON, MTRACE_PERFMON_FINI, 0,
                             nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
}

TEST(X86MtraceTestCase, AssignBuffer) {
  zx::result<zx::resource> root_resource_or_error = GetRootResource();
  ASSERT_TRUE(root_resource_or_error.is_ok(), "failed to get root resource: %s",
              zx_status_get_string(root_resource_or_error.error_value()));
  zx::resource root_resource = std::move(root_resource_or_error.value());
  zx_status_t status = zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON,
                                         MTRACE_PERFMON_INIT, 0, nullptr, 0);
  if (!IsIntelPMUSupported()) {
    EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
    printf("Skipping test, Intel Architectual PMU not supported\n");
    return;
  }
  EXPECT_EQ(status, ZX_OK);

  // MTRACE_PERFMON_ASSIGN_BUFFER associates a user VMO with each CPU; the VMO is used as a sink
  // for PMU records.
  //
  // IMPLEMENTATION NOTE: MTRACE_PERFMON_ASSIGN_BUFFER does not currently map the buffer into the
  // kernel address space; that is deferred until MTRACE_PERFMON_START. This means that some
  // validation, such as buffer permissions, may be deferred until then, and
  // MTRACE_PERFMON_ASSIGN_BUFFER may appear to work even with invalid buffers.
  uint32_t num_cpus = zx_system_get_num_cpus();
  std::vector<zx_handle_t> vmos(num_cpus);
  for (uint i = 0; i < num_cpus; i++) {
    ASSERT_EQ(zx_vmo_create(PAGE_SIZE, 0, &vmos[i]), ZX_OK);
    int cpu = i;
    zx_pmu_buffer_t buffer = {.vmo = vmos[i]};
    status = zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON,
                               MTRACE_PERFMON_ASSIGN_BUFFER, cpu, &buffer, sizeof(buffer));
    EXPECT_EQ(status, ZX_OK);
  }

  // Cleanup.
  status = zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON, MTRACE_PERFMON_FINI, 0,
                             nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
}

// Test a full mtrace MTRACE_KIND_PERFMON cycle - initialize, allocate buffers, configure,
// start/stop tracing, and validate the returned buffers. The test uses a real hardware performance
// counter - the Intel Architectural PMU Fixed-Function Counter 0, 'Instructions Retired'.
TEST(X86MtraceTestCase, InstructionsRetiredFixedCounterTest) {
  perfmon::X86PmuProperties properties;
  zx::result<zx::resource> root_resource_or_error = GetRootResource();
  zx::resource root_resource = std::move(root_resource_or_error.value());
  ASSERT_TRUE(root_resource_or_error.is_ok(), "failed to get root resource: %s",
              zx_status_get_string(root_resource_or_error.error_value()));
  zx_status_t status =
      zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON, MTRACE_PERFMON_GET_PROPERTIES, 0,
                        &properties, sizeof(properties));
  if (!IsIntelPMUSupported()) {
    EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
    printf("Skipping test, Intel Architectual PMU not supported\n");
    return;
  }
  ASSERT_EQ(status, ZX_OK);
  status = zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON, MTRACE_PERFMON_INIT, 0,
                             nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
  uint32_t num_cpus = zx_system_get_num_cpus();
  std::vector<zx_handle_t> vmos(num_cpus);
  for (uint i = 0; i < num_cpus; i++) {
    ASSERT_EQ(zx_vmo_create(PAGE_SIZE, 0, &vmos[i]), ZX_OK);
    int cpu = i;
    zx_pmu_buffer_t buffer = {.vmo = vmos[i]};
    status = zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON,
                               MTRACE_PERFMON_ASSIGN_BUFFER, cpu, &buffer, sizeof(buffer));
    EXPECT_EQ(status, ZX_OK);
  }

  // Stage a configuation to enable the instructions retired fixed-function counter.
  perfmon::X86PmuConfig config = {};
  config.global_ctrl = (1ul << 32);          // Enable fixed counter 0.
  config.fixed_ctrl = 1;                     // Enabled fixed counter 0 at CPL=0.
  config.fixed_events[0] = (2ul << 11) | 1;  // kGroupFixed | Instructions retired.
  status = zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON, MTRACE_PERFMON_STAGE_CONFIG,
                             0, &config, sizeof(config));
  EXPECT_EQ(status, ZX_OK);

  // Start and stop tracing. Each will execute some code at CPL=0.
  status = zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON, MTRACE_PERFMON_START, 0,
                             nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
  status = zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON, MTRACE_PERFMON_STOP, 0,
                             nullptr, 0);
  EXPECT_EQ(status, ZX_OK);

  status = zx_mtrace_control(root_resource.get(), MTRACE_KIND_PERFMON, MTRACE_PERFMON_FINI, 0,
                             nullptr, 0);
  EXPECT_EQ(status, ZX_OK);
  // Examine the buffers; each buffer should have a fixed length BufferHeader followed by one or
  // more variable-length Records. Each Record has a common header.
  struct Buffer {
    perfmon::BufferHeader header;
    char data[PAGE_SIZE - sizeof(perfmon::BufferHeader)];
  };
  static_assert(sizeof(Buffer) == PAGE_SIZE);
  for (uint i = 0; i < num_cpus; i++) {
    Buffer buffer = {};
    status = zx_vmo_read(vmos[i], &buffer, 0, sizeof(Buffer));
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(buffer.header.version, 0);
    EXPECT_EQ(buffer.header.arch, 1);  // Arch X86
    // Expect at least one record.
    EXPECT_GT(buffer.header.capture_end, sizeof(buffer.header));
    // First record must be a time record.
    perfmon::RecordHeader* const record = reinterpret_cast<perfmon::RecordHeader*>(buffer.data);
    EXPECT_EQ(record->type, perfmon::kRecordTypeTime);
  }
}

}  // namespace
