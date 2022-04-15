// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.sysinfo/cpp/wire.h>
#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <inttypes.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/hw/arch_ops.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fidl/llcpp/channel.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>
#include <zircon/limits.h>
#include <zircon/pixelformat.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <limits>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "lib/zx/object.h"

// To dump a corpus file for sysmem_fuzz.cc test, enable SYSMEM_FUZZ_CORPUS. Files can be found
// under /data/cache/r/sys/fuchsia.com:sysmem-test:0#meta:sysmem.cmx/ on the device.
#define SYSMEM_FUZZ_CORPUS 0

// We assume one sysmem since boot, for now.
const char* kSysmemDevicePath = "/dev/class/sysmem/000";

namespace {

// This test observer is used to get the name of the current test to send to sysmem to identify the
// client.
std::string current_test_name;
class TestObserver : public zxtest::LifecycleObserver {
 public:
  void OnTestStart(const zxtest::TestCase& test_case, const zxtest::TestInfo& test_info) final {
    current_test_name = std::string(test_case.name()) + "." + std::string(test_info.name());
  }
};

TestObserver test_observer;

zx::status<fidl::WireSyncClient<fuchsia_sysmem::Allocator>> connect_to_sysmem_service();
zx_status_t verify_connectivity(fidl::WireSyncClient<fuchsia_sysmem::Allocator>& allocator);

bool have_waited_for_sysmem_availability = false;

// Wait for the sysmem devnode to exist. If this test runs immediately after boot then the sysmem
// driver may not have been created yet, so we need to wait.
void WaitForSysmemAvailability() {
  if (have_waited_for_sysmem_availability)
    return;

  have_waited_for_sysmem_availability = true;

  // The sysmem connector service handle waiting for the device to become available, so once we have
  // connectivity through that we should have connectivity directly.
  zx::status status = connect_to_sysmem_service();
  ASSERT_OK(status.status_value());
  ASSERT_OK(verify_connectivity(status.value()));
}

#define IF_FAILURES_RETURN()           \
  do {                                 \
    if (CURRENT_TEST_HAS_FAILURES()) { \
      return;                          \
    }                                  \
  } while (0)

#define IF_FAILURES_RETURN_FALSE()     \
  do {                                 \
    if (CURRENT_TEST_HAS_FAILURES()) { \
      return false;                    \
    }                                  \
  } while (0)

zx::status<fidl::WireSyncClient<fuchsia_sysmem::Allocator>> connect_to_sysmem_driver() {
  WaitForSysmemAvailability();

  auto client_end = service::Connect<fuchsia_sysmem::DriverConnector>(kSysmemDevicePath);
  EXPECT_OK(client_end);
  if (!client_end.is_ok()) {
    return zx::error(client_end.status_value());
  }
  auto connector = fidl::BindSyncClient(std::move(client_end.value()));

  auto allocator_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::Allocator>();
  EXPECT_OK(allocator_endpoints);
  if (!allocator_endpoints.is_ok()) {
    return zx::error(allocator_endpoints.status_value());
  }

  auto connect_result = connector->Connect(std::move(allocator_endpoints->server));
  EXPECT_OK(connect_result);
  if (!connect_result.ok()) {
    return zx::error(connect_result.status());
  }

  auto allocator = fidl::BindSyncClient(std::move(allocator_endpoints->client));
  allocator->SetDebugClientInfo(fidl::StringView::FromExternal(current_test_name), 0u);
  return zx::ok(std::move(allocator));
}

zx::status<fidl::WireSyncClient<fuchsia_sysmem::Allocator>> connect_to_sysmem_service() {
  auto client_end = service::Connect<fuchsia_sysmem::Allocator>();
  EXPECT_OK(client_end);
  if (!client_end.is_ok()) {
    return zx::error(client_end.status_value());
  }
  auto allocator = fidl::BindSyncClient(std::move(client_end.value()));
  allocator->SetDebugClientInfo(fidl::StringView::FromExternal(current_test_name), 0u);
  return zx::ok(std::move(allocator));
}

zx_koid_t get_koid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    fprintf(stderr, "status: %d\n", status);
  }
  ZX_ASSERT(status == ZX_OK);
  return info.koid;
}

zx_koid_t get_related_koid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  EXPECT_EQ(status, ZX_OK, "");
  ZX_ASSERT(status == ZX_OK);
  return info.related_koid;
}

zx_status_t verify_connectivity(fidl::WireSyncClient<fuchsia_sysmem::Allocator>& allocator) {
  zx::status collection_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  EXPECT_OK(collection_endpoints);
  if (!collection_endpoints.is_ok()) {
    return collection_endpoints.status_value();
  }
  auto [collection_client_end, collection_server_end] = std::move(*collection_endpoints);

  auto result = allocator->AllocateNonSharedCollection(std::move(collection_server_end));
  EXPECT_OK(result);
  if (!result.ok()) {
    return result.status();
  }

  auto collection = fidl::BindSyncClient(std::move(collection_client_end));
  auto sync_result = collection->Sync();
  EXPECT_OK(sync_result);
  if (!sync_result.ok()) {
    return sync_result.status();
  }
  return ZX_OK;
}

static void SetDefaultCollectionName(
    fidl::WireSyncClient<fuchsia_sysmem::BufferCollection>& collection) {
  constexpr uint32_t kPriority = 1000000;
  const char* kName = "sysmem-test";
  collection->SetName(kPriority, fidl::StringView::FromExternal(kName));
}

zx::status<fidl::WireSyncClient<fuchsia_sysmem::BufferCollection>>
make_single_participant_collection() {
  // We could use AllocateNonSharedCollection() to implement this function, but we're already
  // using AllocateNonSharedCollection() during verify_connectivity(), so instead just set up the
  // more general (and more real) way here.

  auto allocator = connect_to_sysmem_driver();
  EXPECT_OK(allocator.status_value());
  if (!allocator.is_ok()) {
    return zx::error(allocator.status_value());
  }

  zx::status token_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  EXPECT_OK(token_endpoints);
  if (!token_endpoints.is_ok()) {
    return zx::error(token_endpoints.status_value());
  }
  auto [token_client_end, token_server_end] = std::move(*token_endpoints);

  auto new_collection_result = allocator->AllocateSharedCollection(std::move(token_server_end));
  EXPECT_OK(new_collection_result);
  if (!new_collection_result.ok()) {
    return zx::error(new_collection_result.status());
  }

  zx::status collection_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  EXPECT_OK(collection_endpoints);
  if (!collection_endpoints.is_ok()) {
    return zx::error(collection_endpoints.status_value());
  }
  auto [collection_client_end, collection_server_end] = std::move(*collection_endpoints);

  EXPECT_NE(token_client_end.channel().get(), ZX_HANDLE_INVALID, "");
  auto bind_result = allocator->BindSharedCollection(std::move(token_client_end),
                                                     std::move(collection_server_end));
  EXPECT_OK(bind_result);
  if (!bind_result.ok()) {
    return zx::error(bind_result.status());
  }

  auto collection = fidl::BindSyncClient(std::move(collection_client_end));

  SetDefaultCollectionName(collection);

  return zx::ok(std::move(collection));
}

const std::string& GetBoardName() {
  static std::string s_board_name;
  if (s_board_name.empty()) {
    auto client_end = service::Connect<fuchsia_sysinfo::SysInfo>();
    ZX_ASSERT(client_end.is_ok());

    auto sysinfo = fidl::BindSyncClient(std::move(client_end.value()));
    auto result = sysinfo->GetBoardName();
    ZX_ASSERT(result.ok());
    ZX_ASSERT(result->status == ZX_OK);

    s_board_name = result->name.get();
    printf("\nFound board %s\n", s_board_name.c_str());
  }
  return s_board_name;
}

bool is_board_astro() { return GetBoardName() == "astro"; }

bool is_board_sherlock() { return GetBoardName() == "sherlock"; }

bool is_board_luis() { return GetBoardName() == "luis"; }

bool is_board_nelson() { return GetBoardName() == "nelson"; }

bool is_board_with_amlogic_secure() {
  if (is_board_astro()) {
    return true;
  }
  if (is_board_sherlock()) {
    return true;
  }
  if (is_board_luis()) {
    return true;
  }
  if (is_board_nelson()) {
    return true;
  }
  return false;
}

bool is_board_with_amlogic_secure_vdec() { return is_board_with_amlogic_secure(); }

void nanosleep_duration(zx::duration duration) {
  zx_status_t status = zx::nanosleep(zx::deadline_after(duration));
  ZX_ASSERT(status == ZX_OK);
}

// Faulting on write to a mapping to the VMO can't be checked currently
// because maybe it goes into CPU cache without faulting because 34580?
class SecureVmoReadTester {
 public:
  explicit SecureVmoReadTester(zx::vmo secure_vmo);
  explicit SecureVmoReadTester(zx::unowned_vmo unowned_secure_vmo);
  ~SecureVmoReadTester();
  bool IsReadFromSecureAThing();
  // When we're trying to read from an actual secure VMO, expect_read_success is false.
  // When we're trying tor read from an aux VMO, expect_read_success is true.
  void AttemptReadFromSecure(bool expect_read_success = false);

 private:
  void Init();

  zx::vmo secure_vmo_to_delete_;
  zx::unowned_vmo unowned_secure_vmo_;
  zx::vmar child_vmar_;
  // volatile only so reads in the code actually read despite the value being
  // discarded.
  volatile uint8_t* map_addr_ = {};
  // This is set to true just before the attempt to read.
  std::atomic<bool> is_read_from_secure_attempted_ = false;
  std::atomic<bool> is_read_from_secure_a_thing_ = false;
  std::thread let_die_thread_;
  std::atomic<bool> is_let_die_started_ = false;

  std::uint64_t kSeed = 0;
  std::mt19937_64 prng_{kSeed};
  std::uniform_int_distribution<uint32_t> distribution_{0, std::numeric_limits<uint32_t>::max()};
};

SecureVmoReadTester::SecureVmoReadTester(zx::vmo secure_vmo_to_delete)
    : secure_vmo_to_delete_(std::move(secure_vmo_to_delete)),
      unowned_secure_vmo_(secure_vmo_to_delete_) {
  Init();
}

SecureVmoReadTester::SecureVmoReadTester(zx::unowned_vmo unowned_secure_vmo)
    : unowned_secure_vmo_(std::move(unowned_secure_vmo)) {
  Init();
}

void SecureVmoReadTester::Init() {
  // We need a child VMAR so we can clean up robustly without relying on a fault
  // to occur at location where a VMO was recently mapped but which
  // theoretically something else could be mapped unless we're specific with a
  // VMAR that isn't letting something else get mapped there yet.
  zx_vaddr_t child_vaddr;
  zx_status_t status = zx::vmar::root_self()->allocate(
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0,
      zx_system_get_page_size(), &child_vmar_, &child_vaddr);
  ZX_ASSERT(status == ZX_OK);

  uint64_t vmo_size;
  zx_status_t get_size_status = unowned_secure_vmo_->get_size(&vmo_size);
  ZX_ASSERT(get_size_status == ZX_OK);
  ZX_ASSERT(vmo_size % zx_system_get_page_size() == 0);
  uint64_t vmo_offset =
      (distribution_(prng_) % (vmo_size / zx_system_get_page_size())) * zx_system_get_page_size();

  uintptr_t map_addr_raw;
  status =
      child_vmar_.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC | ZX_VM_MAP_RANGE, 0,
                      *unowned_secure_vmo_, vmo_offset, zx_system_get_page_size(), &map_addr_raw);
  ZX_ASSERT(status == ZX_OK);
  map_addr_ = reinterpret_cast<uint8_t*>(map_addr_raw);
  ZX_ASSERT(reinterpret_cast<uint8_t*>(child_vaddr) == map_addr_);

  // No data should be in CPU cache for a secure VMO; no fault should happen here.
  status = unowned_secure_vmo_->op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, vmo_offset,
                                         zx_system_get_page_size(), nullptr, 0);
  ZX_ASSERT(status == ZX_OK);

  // But currently the read doesn't visibly fault while the vaddr is mapped to
  // a secure page.  Instead the read gets stuck and doesn't complete (perhaps
  // internally faulting from kernel's point of view).  While that's not ideal,
  // we can check that the thread doing the reading doesn't get anything from
  // the read while mapped to a secure page, and then let the thread fault
  // normally by unmapping the secure VMO.
  let_die_thread_ = std::thread([this] {
    is_let_die_started_ = true;
    // Ensure is_read_from_secure_attempted_ becomes true before we start
    // waiting.  This just increases the likelihood that we wait long enough
    // for the read itself to potentially execute (expected to fault instead).
    while (!is_read_from_secure_attempted_) {
      nanosleep_duration(zx::msec(10));
    }
    // Wait 10ms for the read attempt to succeed; the read attempt should not
    // succeed.  The read attempt may fail immediately or may get stuck.  It's
    // possible we might very occasionally not wait long enough for the read
    // to have actually started - if that occurs the test will "pass" without
    // having actually attempted the read.
    nanosleep_duration(zx::msec(10));
    // Let thread running fn die if it hasn't already (if it got stuck, let it
    // no longer be stuck).
    //
    // By removing ZX_VM_PERM_READ, if the read is stuck, the read will cause a
    // process-visible fault instead.  We don't zx_vmar_unmap() here because the
    // syscall docs aren't completely clear on whether zx_vmar_unmap() might
    // make the vaddr page available for other uses.
    zx_status_t status;
    status =
        child_vmar_.protect(0, reinterpret_cast<uintptr_t>(map_addr_), zx_system_get_page_size());
    ZX_ASSERT(status == ZX_OK);
  });

  while (!is_let_die_started_) {
    nanosleep_duration(zx::msec(10));
  }
}

SecureVmoReadTester::~SecureVmoReadTester() {
  if (let_die_thread_.joinable()) {
    let_die_thread_.join();
  }

  child_vmar_.destroy();
}

bool SecureVmoReadTester::IsReadFromSecureAThing() {
  ZX_ASSERT(is_let_die_started_);
  ZX_ASSERT(is_read_from_secure_attempted_);
  return is_read_from_secure_a_thing_;
}

void SecureVmoReadTester::AttemptReadFromSecure(bool expect_read_success) {
  ZX_ASSERT(is_let_die_started_);
  ZX_ASSERT(!is_read_from_secure_attempted_);
  is_read_from_secure_attempted_ = true;
  // This attempt to read from a vaddr that's mapped to a secure paddr won't succeed.  For now the
  // read gets stuck while mapped to secure memory, and then faults when we've unmapped the VMO.
  // This address is in a child VMAR so we know nothing else will be getting mapped to the vaddr.
  //
  // The loop is mainly for the benefit of debugging/fixing the test should the very first write,
  // flush, read not force and fence a fault.
  for (uint32_t i = 0; i < zx_system_get_page_size(); ++i) {
    map_addr_[i] = 0xF0;
    zx_status_t status = zx_cache_flush((const void*)&map_addr_[i], 1,
                                        ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
    ZX_ASSERT(status == ZX_OK);
    uint8_t value = map_addr_[i];
    // Despite the flush above often causing the fault to be sync, sometimes the fault doesn't
    // happen but we read zero.  For now, only complain if we read back something other than zero.
    if (value != 0) {
      is_read_from_secure_a_thing_ = true;
    }
    if (!expect_read_success) {
      if (i % 64 == 0) {
        fprintf(stderr, "%08x: ", i);
      }
      fprintf(stderr, "%02x ", value);
      if ((i + 1) % 64 == 0) {
        fprintf(stderr, "\n");
      }
    }
  }
  if (!expect_read_success) {
    fprintf(stderr, "\n");
    // If we made it through the whole page without faulting, yet only read zero, consider that
    // success in the sense that we weren't able to read anything in secure memory.  Cause the thead
    // to "die" here on purpose so the test can pass.  This is not the typical case, but can happen
    // at least on sherlock.  Typically we fault during the write, flush, read of byte 0 above.
    ZX_PANIC("didn't fault, but also didn't read non-zero, so pretend to fault");
  }
}

// Some helpers to test equality of buffer collection infos and related types.
template <typename T, size_t S>
bool ArrayEqual(const ::fidl::Array<T, S>& a, const ::fidl::Array<T, S>& b);

bool Equal(const fuchsia_sysmem::wire::VmoBuffer& a, const fuchsia_sysmem::wire::VmoBuffer& b) {
  return a.vmo == b.vmo && a.vmo_usable_start == b.vmo_usable_start;
}

bool Equal(const fuchsia_sysmem::wire::PixelFormat& a, const fuchsia_sysmem::wire::PixelFormat& b) {
  return a.type == b.type && a.has_format_modifier == b.has_format_modifier &&
         a.format_modifier.value == b.format_modifier.value;
}

bool Equal(const fuchsia_sysmem::wire::ImageFormatConstraints& a,
           const fuchsia_sysmem::wire::ImageFormatConstraints& b) {
  return Equal(a.pixel_format, b.pixel_format) && a.color_spaces_count == b.color_spaces_count &&
         ArrayEqual(a.color_space, b.color_space) && a.min_coded_width == b.min_coded_width &&
         a.max_coded_width == b.max_coded_width && a.min_coded_height == b.min_coded_height &&
         a.max_coded_height == b.max_coded_height && a.min_bytes_per_row == b.min_bytes_per_row &&
         a.max_bytes_per_row == b.max_bytes_per_row &&
         a.max_coded_width_times_coded_height == b.max_coded_width_times_coded_height &&
         a.layers == b.layers && a.coded_width_divisor == b.coded_width_divisor &&
         a.coded_height_divisor == b.coded_height_divisor &&
         a.bytes_per_row_divisor == b.bytes_per_row_divisor &&
         a.start_offset_divisor == b.start_offset_divisor &&
         a.display_width_divisor == b.display_width_divisor &&
         a.display_height_divisor == b.display_height_divisor &&
         a.required_min_coded_width == b.required_min_coded_width &&
         a.required_max_coded_width == b.required_max_coded_width &&
         a.required_min_coded_height == b.required_min_coded_height &&
         a.required_max_coded_height == b.required_max_coded_height &&
         a.required_min_bytes_per_row == b.required_min_bytes_per_row &&
         a.required_max_bytes_per_row == b.required_max_bytes_per_row;
}

bool Equal(const fuchsia_sysmem::wire::BufferMemorySettings& a,
           const fuchsia_sysmem::wire::BufferMemorySettings& b) {
  return a.size_bytes == b.size_bytes && a.is_physically_contiguous == b.is_physically_contiguous &&
         a.is_secure == b.is_secure && a.coherency_domain == b.coherency_domain && a.heap == b.heap;
}

bool Equal(const fuchsia_sysmem::wire::SingleBufferSettings& a,
           const fuchsia_sysmem::wire::SingleBufferSettings& b) {
  return Equal(a.buffer_settings, b.buffer_settings) &&
         a.has_image_format_constraints == b.has_image_format_constraints &&
         Equal(a.image_format_constraints, b.image_format_constraints);
}

bool Equal(const fuchsia_sysmem::wire::BufferCollectionInfo2& a,
           const fuchsia_sysmem::wire::BufferCollectionInfo2& b) {
  return a.buffer_count == b.buffer_count && Equal(a.settings, b.settings) &&
         ArrayEqual(a.buffers, b.buffers);
}

bool Equal(const fuchsia_sysmem::wire::ColorSpace& a, const fuchsia_sysmem::wire::ColorSpace& b) {
  return a.type == b.type;
}

template <typename T, size_t S>
bool ArrayEqual(const ::fidl::Array<T, S>& a, const ::fidl::Array<T, S>& b) {
  for (size_t i = 0; i < S; i++) {
    if (!Equal(a[i], b[i])) {
      return false;
    }
  }
  return true;
}

bool AttachTokenSucceeds(
    bool attach_before_also, bool fail_attached_early,
    fit::function<void(fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify)>
        modify_constraints_initiator,
    fit::function<void(fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify)>
        modify_constraints_participant,
    fit::function<void(fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify)>
        modify_constraints_attached,
    fit::function<void(fuchsia_sysmem::wire::BufferCollectionInfo2& to_verify)> verify_info,
    uint32_t expected_buffer_count = 6) {
  ZX_DEBUG_ASSERT(!fail_attached_early || attach_before_also);
  auto allocator = connect_to_sysmem_driver();
  EXPECT_OK(allocator);
  IF_FAILURES_RETURN_FALSE();

  zx::status token_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  EXPECT_OK(token_endpoints_1);
  IF_FAILURES_RETURN_FALSE();
  auto [token_client_1, token_server_1] = std::move(*token_endpoints_1);

  // Client 1 creates a token and new LogicalBufferCollection using
  // AllocateSharedCollection().
  EXPECT_OK(allocator->AllocateSharedCollection(std::move(token_server_1)));
  IF_FAILURES_RETURN_FALSE();

  zx::status token_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  EXPECT_OK(token_endpoints_2);
  IF_FAILURES_RETURN_FALSE();
  auto [token_client_2, token_server_2] = std::move(*token_endpoints_2);

  // Client 1 duplicates its token and gives the duplicate to client 2 (this
  // test is single proc, so both clients are coming from this client
  // process - normally the two clients would be in separate processes with
  // token_client_2 transferred to another participant).
  auto token_1 = fidl::BindSyncClient(std::move(token_client_1));
  EXPECT_OK(token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_2)));
  IF_FAILURES_RETURN_FALSE();

  // Client 3 is attached later.

  zx::status collection_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  EXPECT_OK(collection_endpoints_1);
  IF_FAILURES_RETURN_FALSE();
  auto [collection_client_1, collection_server_1] = std::move(*collection_endpoints_1);
  auto collection_1 = fidl::BindSyncClient(std::move(collection_client_1));

  EXPECT_NE(token_1.client_end().channel().get(), ZX_HANDLE_INVALID, "");
  EXPECT_OK(
      allocator->BindSharedCollection(token_1.TakeClientEnd(), std::move(collection_server_1)));
  IF_FAILURES_RETURN_FALSE();

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_1;
  constraints_1.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints_1.min_buffer_count_for_camping = 3;
  constraints_1.has_buffer_memory_constraints = true;
  constraints_1.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      // This min_size_bytes is intentionally too small to hold the min_coded_width and
      // min_coded_height in NV12
      // format.
      .min_size_bytes = 64 * 1024,
      // Allow a max that's just large enough to accommodate the size implied
      // by the min frame size and PixelFormat.
      .max_size_bytes = (512 * 512) * 3 / 2,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  constraints_1.image_format_constraints_count = 1;
  fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints_1 =
      constraints_1.image_format_constraints[0];
  image_constraints_1.pixel_format.type = fuchsia_sysmem::wire::PixelFormatType::kNv12;
  image_constraints_1.color_spaces_count = 1;
  image_constraints_1.color_space[0] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec709,
  };
  // The min dimensions intentionally imply a min size that's larger than
  // buffer_memory_constraints.min_size_bytes.
  image_constraints_1.min_coded_width = 256;
  image_constraints_1.max_coded_width = std::numeric_limits<uint32_t>::max();
  image_constraints_1.min_coded_height = 256;
  image_constraints_1.max_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints_1.min_bytes_per_row = 256;
  image_constraints_1.max_bytes_per_row = std::numeric_limits<uint32_t>::max();
  image_constraints_1.max_coded_width_times_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints_1.layers = 1;
  image_constraints_1.coded_width_divisor = 2;
  image_constraints_1.coded_height_divisor = 2;
  image_constraints_1.bytes_per_row_divisor = 2;
  image_constraints_1.start_offset_divisor = 2;
  image_constraints_1.display_width_divisor = 1;
  image_constraints_1.display_height_divisor = 1;

  // Start with constraints_2 a copy of constraints_1.  There are no handles
  // in the constraints struct so a struct copy instead of clone is fine here.
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_2(constraints_1);
  // Modify constraints_2 to require double the width and height.
  constraints_2.image_format_constraints[0].min_coded_width = 512;
  constraints_2.image_format_constraints[0].min_coded_height = 512;

#if SYSMEM_FUZZ_CORPUS
  FILE* ofp = fopen("/cache/sysmem_fuzz_corpus_multi_buffer_collecton_constraints.dat", "wb");
  if (ofp) {
    fwrite(&constraints_1, sizeof(fuchsia_sysmem::wire::BufferCollectionConstraints), 1, ofp);
    fwrite(&constraints_2, sizeof(fuchsia_sysmem::wire::BufferCollectionConstraints), 1, ofp);
    fclose(ofp);
  } else {
    fprintf(stderr, "Failed to write sysmem multi BufferCollectionConstraints corpus file.\n");
    fflush(stderr);
  }
#endif  // SYSMEM_FUZZ_CORPUS

  modify_constraints_initiator(constraints_1);

  EXPECT_OK(collection_1->SetConstraints(true, std::move(constraints_1)));
  IF_FAILURES_RETURN_FALSE();

  // Client 2 connects to sysmem separately.
  auto allocator_2 = connect_to_sysmem_driver();
  EXPECT_OK(allocator_2);
  IF_FAILURES_RETURN_FALSE();

  zx::status collection_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  EXPECT_OK(collection_endpoints_2);
  IF_FAILURES_RETURN_FALSE();
  auto [collection_client_2, collection_server_2] = std::move(*collection_endpoints_2);
  auto collection_2 = fidl::BindSyncClient(std::move(collection_client_2));

  // Just because we can, perform this sync as late as possible, just before
  // the BindSharedCollection() via allocator2_client_2.  Without this Sync(),
  // the BindSharedCollection() might arrive at the server before the
  // Duplicate() that delivered the server end of token_client_2 to sysmem,
  // which would cause sysmem to not recognize the token.
  EXPECT_OK(collection_1->Sync());
  IF_FAILURES_RETURN_FALSE();

  EXPECT_NE(token_client_2.channel().get(), ZX_HANDLE_INVALID, "");
  EXPECT_OK(
      allocator_2->BindSharedCollection(std::move(token_client_2), std::move(collection_server_2)));
  IF_FAILURES_RETURN_FALSE();

  // Not all constraints have been input, so the buffers haven't been
  // allocated yet.
  auto check_1_result = collection_1->CheckBuffersAllocated();
  EXPECT_OK(check_1_result);
  EXPECT_EQ(check_1_result->status, ZX_ERR_UNAVAILABLE);
  IF_FAILURES_RETURN_FALSE();

  auto check_2_result = collection_2->CheckBuffersAllocated();
  EXPECT_OK(check_2_result);
  EXPECT_EQ(check_2_result->status, ZX_ERR_UNAVAILABLE);
  IF_FAILURES_RETURN_FALSE();

  fidl::ClientEnd<fuchsia_sysmem::BufferCollectionToken> token_client_3;
  fidl::ServerEnd<fuchsia_sysmem::BufferCollectionToken> token_server_3;

  auto use_collection_2_to_attach_token_3 = [&collection_2, &token_client_3, &token_server_3] {
    token_client_3.reset();
    ZX_DEBUG_ASSERT(!token_server_3);
    zx::status token_endpoints_3 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
    EXPECT_OK(token_endpoints_3);
    IF_FAILURES_RETURN();
    token_client_3 = std::move(token_endpoints_3->client);
    token_server_3 = std::move(token_endpoints_3->server);

    EXPECT_OK(collection_2->AttachToken(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_3)));
    IF_FAILURES_RETURN();
    // Since we're not doing any Duplicate()s first or anything like that (which could allow us to
    // share the round trip), go ahead and Sync() the token creation to sysmem now.
    EXPECT_OK(collection_2->Sync());
    IF_FAILURES_RETURN();
  };

  if (attach_before_also) {
    use_collection_2_to_attach_token_3();
  }
  IF_FAILURES_RETURN_FALSE();

  // The AttachToken() participant needs to set constraints also, but it will never hold up initial
  // allocation.

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_3;
  constraints_3.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints_3.has_buffer_memory_constraints = true;
  constraints_3.buffer_memory_constraints.cpu_domain_supported = true;
  modify_constraints_attached(constraints_3);

  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection_3;

  auto collection_client_3_set_constraints = [&allocator_2, &token_client_3, &constraints_3,
                                              &collection_3] {
    EXPECT_NE(allocator_2.value().client_end().channel().get(), ZX_HANDLE_INVALID, "");
    EXPECT_NE(token_client_3.channel().get(), ZX_HANDLE_INVALID, "");
    IF_FAILURES_RETURN();
    collection_3 = {};
    ZX_DEBUG_ASSERT(!collection_3.is_valid());

    zx::status collection_endpoints_3 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
    EXPECT_OK(collection_endpoints_3);
    IF_FAILURES_RETURN();
    auto [collection_client_3, collection_server_3] = std::move(*collection_endpoints_3);
    collection_3 = fidl::BindSyncClient(std::move(collection_client_3));

    EXPECT_OK(allocator_2->BindSharedCollection(std::move(token_client_3),
                                                std::move(collection_server_3)));
    IF_FAILURES_RETURN();
    fuchsia_sysmem::wire::BufferCollectionConstraints constraints_3_copy(constraints_3);

    EXPECT_OK(collection_3->SetConstraints(true, constraints_3_copy));
    IF_FAILURES_RETURN();
  };

  if (attach_before_also) {
    collection_client_3_set_constraints();
    IF_FAILURES_RETURN_FALSE();
    if (fail_attached_early) {
      // Also close the channel to simulate early client 3 failure before allocation.
      collection_3 = {};
    }
  }

  //
  // Only after all non-AttachToken() participants have SetConstraints() will the initial allocation
  // be successful.  The initial allocation will always succeed regardless of how uncooperative the
  // AttachToken() client 3 is being with its constraints.
  //

  modify_constraints_participant(constraints_2);
  EXPECT_OK(collection_2->SetConstraints(true, std::move(constraints_2)));
  IF_FAILURES_RETURN_FALSE();

  auto allocate_result_1 = collection_1->WaitForBuffersAllocated();

  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  EXPECT_OK(allocate_result_1);
  EXPECT_OK(allocate_result_1->status);
  IF_FAILURES_RETURN_FALSE();

  auto check_result_good_1 = collection_1->CheckBuffersAllocated();
  EXPECT_OK(check_result_good_1);
  EXPECT_OK(check_result_good_1->status);
  IF_FAILURES_RETURN_FALSE();

  auto check_result_good_2 = collection_2->CheckBuffersAllocated();
  EXPECT_OK(check_result_good_2);
  EXPECT_OK(check_result_good_2->status);
  IF_FAILURES_RETURN_FALSE();

  auto allocate_result_2 = collection_2->WaitForBuffersAllocated();
  EXPECT_OK(allocate_result_2);
  EXPECT_OK(allocate_result_2->status);
  IF_FAILURES_RETURN_FALSE();

  //
  // buffer_collection_info_1 and buffer_collection_info_2 should be exactly
  // equal except their non-zero handle values, which should be different.  We
  // verify the handle values then check that the structs are exactly the same
  // with handle values zeroed out.
  //
  fuchsia_sysmem::wire::BufferCollectionInfo2* buffer_collection_info_1 =
      &allocate_result_1->buffer_collection_info;
  fuchsia_sysmem::wire::BufferCollectionInfo2* buffer_collection_info_2 =
      &allocate_result_2->buffer_collection_info;
  fuchsia_sysmem::wire::BufferCollectionInfo2 buffer_collection_info_3;

  for (uint32_t i = 0; i < std::size(buffer_collection_info_1->buffers); ++i) {
    EXPECT_EQ(buffer_collection_info_1->buffers[i].vmo.get() != ZX_HANDLE_INVALID,
              buffer_collection_info_2->buffers[i].vmo.get() != ZX_HANDLE_INVALID, "");
    IF_FAILURES_RETURN_FALSE();
    if (buffer_collection_info_1->buffers[i].vmo.get() != ZX_HANDLE_INVALID) {
      // The handle values must be different.
      EXPECT_NE(buffer_collection_info_1->buffers[i].vmo.get(),
                buffer_collection_info_2->buffers[i].vmo.get(), "");
      IF_FAILURES_RETURN_FALSE();
      // For now, the koid(s) are expected to be equal.  This is not a
      // fundamental check, in that sysmem could legitimately change in
      // future to vend separate child VMOs (of the same portion of a
      // non-copy-on-write parent VMO) to the two participants and that
      // would still be potentially valid overall.
      zx_koid_t koid_1 = get_koid(buffer_collection_info_1->buffers[i].vmo.get());
      zx_koid_t koid_2 = get_koid(buffer_collection_info_2->buffers[i].vmo.get());
      EXPECT_EQ(koid_1, koid_2, "");
      IF_FAILURES_RETURN_FALSE();
    }
  }

  //
  // Verify that buffer_collection_info_1 paid attention to constraints_2, and
  // that buffer_collection_info_2 makes sense.  This also indirectly confirms
  // that buffer_collection_info_3 paid attention to constraints_2.
  //

  EXPECT_EQ(buffer_collection_info_1->buffer_count, expected_buffer_count, "");
  // The size should be sufficient for the whole NV12 frame, not just
  // min_size_bytes.  In other words, the portion of the VMO the client can
  // use is large enough to hold the min image size, despite the min buffer
  // size being smaller.
  EXPECT_GE(buffer_collection_info_1->settings.buffer_settings.size_bytes, (512 * 512) * 3 / 2, "");
  EXPECT_EQ(buffer_collection_info_1->settings.buffer_settings.is_physically_contiguous, false, "");
  EXPECT_EQ(buffer_collection_info_1->settings.buffer_settings.is_secure, false, "");
  // We specified image_format_constraints so the result must also have
  // image_format_constraints.
  EXPECT_EQ(buffer_collection_info_1->settings.has_image_format_constraints, true, "");
  IF_FAILURES_RETURN_FALSE();

  for (uint32_t i = 0; i < 64; ++i) {
    if (i < expected_buffer_count) {
      EXPECT_NE(buffer_collection_info_1->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
      EXPECT_NE(buffer_collection_info_2->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
      IF_FAILURES_RETURN_FALSE();

      uint64_t size_bytes_1 = 0;
      EXPECT_OK(buffer_collection_info_1->buffers[i].vmo.get_size(&size_bytes_1));
      IF_FAILURES_RETURN_FALSE();

      uint64_t size_bytes_2 = 0;
      EXPECT_OK(buffer_collection_info_2->buffers[i].vmo.get_size(&size_bytes_2));
      IF_FAILURES_RETURN_FALSE();

      // The vmo has room for the nominal size of the portion of the VMO
      // the client can use.  These checks should pass even if sysmem were
      // to vend different child VMOs to the two participants.
      EXPECT_LE(buffer_collection_info_1->buffers[i].vmo_usable_start +
                    buffer_collection_info_1->settings.buffer_settings.size_bytes,
                size_bytes_1, "");
      EXPECT_LE(buffer_collection_info_2->buffers[i].vmo_usable_start +
                    buffer_collection_info_2->settings.buffer_settings.size_bytes,
                size_bytes_2, "");
      IF_FAILURES_RETURN_FALSE();
    } else {
      EXPECT_EQ(buffer_collection_info_1->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
      EXPECT_EQ(buffer_collection_info_2->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
      IF_FAILURES_RETURN_FALSE();
    }
  }

  if (attach_before_also && !collection_3.is_valid()) {
    // We already failed collection_client_3 early, so AttachToken() can't succeed, but we've
    // checked that initial allocation did succeed despite the pre-allocation
    // failure of client 3.
    return false;
  }

  if (attach_before_also) {
    auto allocate_result_3 = collection_3->WaitForBuffersAllocated();
    if (!allocate_result_3.ok() || allocate_result_3->status != ZX_OK) {
      return false;
    }
  }

  const uint32_t kIterationCount = 3;
  for (uint32_t i = 0; i < kIterationCount; ++i) {
    if (i != 0 || !attach_before_also) {
      use_collection_2_to_attach_token_3();
      collection_client_3_set_constraints();
    }

    // The collection_client_3_set_constraints() above closed the old collection_client_3, which the
    // sysmem server treats as a client 3 failure, but because client 3 was created via
    // AttachToken(), the failure of client 3 doesn't cause failure of the LogicalBufferCollection.
    //
    // Give some time to fail if it were going to (but it shouldn't).
    nanosleep_duration(zx::msec(250));
    EXPECT_OK(collection_1->Sync());
    // LogicalBufferCollection still ok.
    IF_FAILURES_RETURN_FALSE();

    auto allocate_result_3 = collection_3->WaitForBuffersAllocated();
    if (!allocate_result_3.ok() || allocate_result_3->status != ZX_OK) {
      return false;
    }

    buffer_collection_info_3 = std::move(allocate_result_3->buffer_collection_info);

    for (uint32_t i = 0; i < std::size(buffer_collection_info_1->buffers); ++i) {
      EXPECT_EQ(buffer_collection_info_1->buffers[i].vmo.get() != ZX_HANDLE_INVALID,
                buffer_collection_info_3.buffers[i].vmo.get() != ZX_HANDLE_INVALID, "");
      IF_FAILURES_RETURN_FALSE();
      if (buffer_collection_info_1->buffers[i].vmo.get() != ZX_HANDLE_INVALID) {
        // The handle values must be different.
        EXPECT_NE(buffer_collection_info_1->buffers[i].vmo.get(),
                  buffer_collection_info_3.buffers[i].vmo.get(), "");
        EXPECT_NE(buffer_collection_info_2->buffers[i].vmo.get(),
                  buffer_collection_info_3.buffers[i].vmo.get(), "");
        IF_FAILURES_RETURN_FALSE();
        // For now, the koid(s) are expected to be equal.  This is not a
        // fundamental check, in that sysmem could legitimately change in
        // future to vend separate child VMOs (of the same portion of a
        // non-copy-on-write parent VMO) to the two participants and that
        // would still be potentially valid overall.
        zx_koid_t koid_1 = get_koid(buffer_collection_info_1->buffers[i].vmo.get());
        zx_koid_t koid_3 = get_koid(buffer_collection_info_3.buffers[i].vmo.get());
        EXPECT_EQ(koid_1, koid_3, "");
        IF_FAILURES_RETURN_FALSE();
      }
    }
  }

  // Clear out vmo handles and compare all three collections
  for (uint32_t i = 0; i < std::size(buffer_collection_info_1->buffers); ++i) {
    buffer_collection_info_1->buffers[i].vmo.reset();
    buffer_collection_info_2->buffers[i].vmo.reset();
    buffer_collection_info_3.buffers[i].vmo.reset();
  }

  // Check that buffer_collection_info_1 and buffer_collection_info_2 are
  // consistent.
  EXPECT_TRUE(Equal(*buffer_collection_info_1, *buffer_collection_info_2), "");
  IF_FAILURES_RETURN_FALSE();

  // Check that buffer_collection_info_1 and buffer_collection_info_3 are
  // consistent.
  EXPECT_TRUE(Equal(*buffer_collection_info_1, buffer_collection_info_3), "");
  IF_FAILURES_RETURN_FALSE();

  return true;
}

}  // namespace

TEST(Sysmem, DriverConnection) {
  auto allocator = connect_to_sysmem_driver();
  ASSERT_OK(allocator);
  ASSERT_OK(verify_connectivity(allocator.value()));
}

TEST(Sysmem, ServiceConnection) {
  auto allocator = connect_to_sysmem_service();
  ASSERT_OK(allocator);
  ASSERT_OK(verify_connectivity(allocator.value()));
}

TEST(Sysmem, VerifyBufferCollectionToken) {
  auto allocator = connect_to_sysmem_driver();
  ASSERT_OK(allocator);

  auto token_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints);
  auto [token_client, token_server] = std::move(*token_endpoints);
  auto token = fidl::BindSyncClient(std::move(token_client));

  ASSERT_OK(allocator->AllocateSharedCollection(std::move(token_server)));

  auto token2_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token2_endpoints);
  auto [token2_client, token2_server] = std::move(*token2_endpoints);
  auto token2 = fidl::BindSyncClient(std::move(token2_client));

  ASSERT_OK(token->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token2_server)));

  auto not_token_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(not_token_endpoints);
  auto [not_token_client, not_token_server] = std::move(*not_token_endpoints);

  ASSERT_OK(token->Sync());
  ASSERT_OK(token2->Sync());

  auto validate_result_1 = allocator->ValidateBufferCollectionToken(
      get_related_koid(token.client_end().channel().get()));
  ASSERT_OK(validate_result_1);
  ASSERT_TRUE(validate_result_1->is_known);

  auto validate_result_2 = allocator->ValidateBufferCollectionToken(
      get_related_koid(token2.client_end().channel().get()));
  ASSERT_OK(validate_result_2);
  ASSERT_TRUE(validate_result_2->is_known);

  auto validate_result_not_known =
      allocator->ValidateBufferCollectionToken(get_related_koid(not_token_client.channel().get()));
  ASSERT_OK(validate_result_not_known);
  ASSERT_FALSE(validate_result_not_known->is_known);
}

TEST(Sysmem, TokenOneParticipantNoImageConstraints) {
  auto collection = make_single_participant_collection();
  ASSERT_OK(collection);

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 3;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 64 * 1024,
      .max_size_bytes = 128 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);
  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocation_result = collection->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocation_result);
  ASSERT_OK(allocation_result->status);

  auto buffer_collection_info = &allocation_result->buffer_collection_info;
  ASSERT_EQ(buffer_collection_info->buffer_count, 3, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, 64 * 1024, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
            fuchsia_sysmem::wire::CoherencyDomain::kCpu, "");
  ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");

  for (uint32_t i = 0; i < 64; ++i) {
    if (i < 3) {
      ASSERT_NE(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
      uint64_t size_bytes = 0;
      auto status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo.get(), &size_bytes);
      ASSERT_EQ(status, ZX_OK, "");
      ASSERT_EQ(size_bytes, 64 * 1024, "");
    } else {
      ASSERT_EQ(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
    }
  }
}

TEST(Sysmem, TokenOneParticipantColorspaceRanking) {
  auto collection = make_single_participant_collection();
  ASSERT_OK(collection);

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 64 * 1024,
      .max_size_bytes = 128 * 1024,
      .cpu_domain_supported = true,
  };
  constraints.image_format_constraints_count = 1;
  fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints =
      constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia_sysmem::wire::PixelFormatType::kNv12;
  image_constraints.color_spaces_count = 3;
  image_constraints.color_space[0] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec601Pal,
  };
  image_constraints.color_space[1] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec601PalFullRange,
  };
  image_constraints.color_space[2] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec709,
  };

  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocation_result = collection->WaitForBuffersAllocated();
  ASSERT_OK(allocation_result);
  ASSERT_OK(allocation_result->status);

  auto buffer_collection_info = &allocation_result->buffer_collection_info;
  ASSERT_EQ(buffer_collection_info->buffer_count, 1, "");
  ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, true, "");
  ASSERT_EQ(buffer_collection_info->settings.image_format_constraints.pixel_format.type,
            fuchsia_sysmem::wire::PixelFormatType::kNv12, "");
  ASSERT_EQ(buffer_collection_info->settings.image_format_constraints.color_spaces_count, 1, "");
  ASSERT_EQ(buffer_collection_info->settings.image_format_constraints.color_space[0].type,
            fuchsia_sysmem::wire::ColorSpaceType::kRec709, "");
}

TEST(Sysmem, AttachLifetimeTracking) {
  auto collection = make_single_participant_collection();
  ASSERT_OK(collection);

  ASSERT_OK(collection->Sync());

  constexpr uint32_t kNumBuffers = 3;
  constexpr uint32_t kNumEventpairs = kNumBuffers + 3;
  zx::eventpair client[kNumEventpairs];
  zx::eventpair server[kNumEventpairs];
  for (uint32_t i = 0; i < kNumEventpairs; ++i) {
    auto status = zx::eventpair::create(/*options=*/0, &client[i], &server[i]);
    ASSERT_OK(status, "");
    collection->AttachLifetimeTracking(std::move(server[i]), i);
  }

  nanosleep_duration(zx::msec(500));

  for (uint32_t i = 0; i < kNumEventpairs; ++i) {
    zx_signals_t pending_signals;
    auto status =
        client[i].wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite_past(), &pending_signals);
    ASSERT_EQ(status, ZX_ERR_TIMED_OUT, "");
    // Buffers are not allocated yet, so lifetime tracking is pending, since we don't yet know how
    // many buffers there will be.
    ASSERT_EQ(pending_signals & ZX_EVENTPAIR_PEER_CLOSED, 0, "");
  }

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = kNumBuffers;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 64 * 1024,
      .max_size_bytes = 128 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);
  auto set_constraints_result = collection->SetConstraints(true, std::move(constraints));
  ASSERT_TRUE(set_constraints_result.ok());

  // Enough time to typically notice if server accidentally closes server 0..kNumEventpairs-1.
  nanosleep_duration(zx::msec(200));

  // Now that we've set constraints, allocation can happen, and ZX_EVENTPAIR_PEER_CLOSED should be
  // seen for eventpair(s) >= kNumBuffers.
  for (uint32_t i = 0; i < kNumEventpairs; ++i) {
    zx_signals_t pending_signals;
    auto status = client[i].wait_one(
        ZX_EVENTPAIR_PEER_CLOSED,
        i >= kNumBuffers ? zx::time::infinite() : zx::time::infinite_past(), &pending_signals);
    ASSERT_TRUE(status == (i >= kNumBuffers) ? ZX_OK : ZX_ERR_TIMED_OUT, "");
    ASSERT_TRUE(!!(pending_signals & ZX_EVENTPAIR_PEER_CLOSED) == (i >= kNumBuffers), "");
  }

  auto wait_for_buffers_allocated_result = collection->WaitForBuffersAllocated();
  ASSERT_OK(wait_for_buffers_allocated_result.status(), "");
  auto& response = wait_for_buffers_allocated_result.value();
  ASSERT_OK(response.status, "");
  auto& info = response.buffer_collection_info;
  ASSERT_EQ(info.buffer_count, kNumBuffers, "");

  // ZX_EVENTPAIR_PEER_CLOSED should be seen for eventpair(s) >= kNumBuffers.
  for (uint32_t i = 0; i < kNumEventpairs; ++i) {
    zx_signals_t pending_signals;
    auto status = client[i].wait_one(
        ZX_EVENTPAIR_PEER_CLOSED,
        i >= kNumBuffers ? zx::time::infinite() : zx::time::infinite_past(), &pending_signals);
    ASSERT_TRUE(status == (i >= kNumBuffers) ? ZX_OK : ZX_ERR_TIMED_OUT, "");
    ASSERT_TRUE(!!(pending_signals & ZX_EVENTPAIR_PEER_CLOSED) == (i >= kNumBuffers), "");
  }

  zx::status attached_token_endpoints =
      fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(attached_token_endpoints);
  auto [attached_token_client, attached_token_server] = std::move(*attached_token_endpoints);

  ASSERT_OK(collection->AttachToken(std::numeric_limits<uint32_t>::max(),
                                    std::move(attached_token_server)));

  ASSERT_OK(collection->Sync());

  zx::status attached_collection_endpoints =
      fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(attached_collection_endpoints);
  auto [attached_collection_client, attached_collection_server] =
      std::move(*attached_collection_endpoints);
  auto attached_collection = fidl::BindSyncClient(std::move(attached_collection_client));

  auto allocator = connect_to_sysmem_driver();
  ASSERT_OK(allocator);
  auto bind_result = allocator->BindSharedCollection(std::move(attached_token_client),
                                                     std::move(attached_collection_server));
  ASSERT_OK(bind_result);
  auto sync_result_3 = attached_collection->Sync();
  ASSERT_OK(sync_result_3);

  zx::eventpair attached_lifetime_client, attached_lifetime_server;
  zx::eventpair::create(/*options=*/0, &attached_lifetime_client, &attached_lifetime_server);
  // With a buffers_remaining of 0, normally this would require 0 buffers remaining in the
  // LogicalBuffercollection to close attached_lifetime_server, but because we're about to force
  // logical allocation failure, it'll close as soon as we hit logical allocation failure for the
  // attached token.  The logical allocation failure of the attached token doesn't impact collection
  // in any way.
  attached_collection->AttachLifetimeTracking(std::move(attached_lifetime_server),
                                              /*buffers_remaining=*/0);
  fuchsia_sysmem::wire::BufferCollectionConstraints attached_constraints;
  attached_constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  // We won't be able to logically allocate, because original allocation didn't make room for this
  // buffer.
  attached_constraints.min_buffer_count_for_camping = 1;
  attached_constraints.has_buffer_memory_constraints = true;
  attached_constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 64 * 1024,
      .max_size_bytes = 128 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  ZX_DEBUG_ASSERT(attached_constraints.image_format_constraints_count == 0);
  auto attached_set_constraints_result =
      attached_collection->SetConstraints(true, std::move(attached_constraints));
  ASSERT_TRUE(attached_set_constraints_result.ok());
  zx_signals_t attached_pending_signals;
  zx_status_t status = attached_lifetime_client.wait_one(
      ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite(), &attached_pending_signals);
  ASSERT_OK(status, "");
  ASSERT_TRUE(attached_pending_signals & ZX_EVENTPAIR_PEER_CLOSED, "");

  collection.value() = {};

  // ZX_EVENTPAIR_PEER_CLOSED should be seen for eventpair(s) >= kNumBuffers.
  for (uint32_t i = 0; i < kNumEventpairs; ++i) {
    zx_signals_t pending_signals;
    status = client[i].wait_one(ZX_EVENTPAIR_PEER_CLOSED,
                                i >= kNumBuffers ? zx::time::infinite() : zx::time::infinite_past(),
                                &pending_signals);
    ASSERT_TRUE(status == (i >= kNumBuffers) ? ZX_OK : ZX_ERR_TIMED_OUT, "");
    ASSERT_TRUE(!!(pending_signals & ZX_EVENTPAIR_PEER_CLOSED) == (i >= kNumBuffers), "");
  }

  for (uint32_t j = 0; j < kNumBuffers; ++j) {
    info.buffers[j].vmo.reset();
    for (uint32_t i = 0; i < kNumBuffers; ++i) {
      zx_signals_t pending_signals;
      status = client[i].wait_one(
          ZX_EVENTPAIR_PEER_CLOSED,
          i >= kNumBuffers - (j + 1) ? zx::time::infinite() : zx::time::infinite_past(),
          &pending_signals);
      ASSERT_TRUE(status == (i >= kNumBuffers - (j + 1)) ? ZX_OK : ZX_ERR_TIMED_OUT, "");
      ASSERT_TRUE(!!(pending_signals & ZX_EVENTPAIR_PEER_CLOSED) == (i >= kNumBuffers - (j + 1)),
                  "");
    }
  }
}

TEST(Sysmem, TokenOneParticipantWithImageConstraints) {
  auto collection = make_single_participant_collection();
  ASSERT_OK(collection);

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 3;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      // This min_size_bytes is intentionally too small to hold the min_coded_width and
      // min_coded_height in NV12
      // format.
      .min_size_bytes = 64 * 1024,
      .max_size_bytes = 128 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  constraints.image_format_constraints_count = 1;
  fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints =
      constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia_sysmem::wire::PixelFormatType::kNv12;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec709,
  };
  // The min dimensions intentionally imply a min size that's larger than
  // buffer_memory_constraints.min_size_bytes.
  image_constraints.min_coded_width = 256;
  image_constraints.max_coded_width = std::numeric_limits<uint32_t>::max();
  image_constraints.min_coded_height = 256;
  image_constraints.max_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints.min_bytes_per_row = 256;
  image_constraints.max_bytes_per_row = std::numeric_limits<uint32_t>::max();
  image_constraints.max_coded_width_times_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints.layers = 1;
  image_constraints.coded_width_divisor = 2;
  image_constraints.coded_height_divisor = 2;
  image_constraints.bytes_per_row_divisor = 2;
  image_constraints.start_offset_divisor = 2;
  image_constraints.display_width_divisor = 1;
  image_constraints.display_height_divisor = 1;

#if SYSMEM_FUZZ_CORPUS
  FILE* ofp = fopen("/cache/sysmem_fuzz_corpus_buffer_collecton_constraints.dat", "wb");
  if (ofp) {
    fwrite(&constraints, sizeof(fuchsia_sysmem::wire::BufferCollectionConstraints), 1, ofp);
    fclose(ofp);
  } else {
    fprintf(stderr, "Failed to write sysmem BufferCollectionConstraints corpus file.\n");
    fflush(stderr);
  }
#endif  // SYSMEM_FUZZ_CORPUS

  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocation_result = collection->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocation_result);
  ASSERT_OK(allocation_result->status);

  auto buffer_collection_info = &allocation_result->buffer_collection_info;

  ASSERT_EQ(buffer_collection_info->buffer_count, 3, "");
  // The size should be sufficient for the whole NV12 frame, not just min_size_bytes.
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, 64 * 1024 * 3 / 2, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
            fuchsia_sysmem::wire::CoherencyDomain::kCpu, "");
  // We specified image_format_constraints so the result must also have
  // image_format_constraints.
  ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, true, "");

  for (uint32_t i = 0; i < 64; ++i) {
    if (i < 3) {
      ASSERT_NE(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
      uint64_t size_bytes = 0;
      auto status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo.get(), &size_bytes);
      ASSERT_EQ(status, ZX_OK, "");
      // The portion of the VMO the client can use is large enough to hold the min image size,
      // despite the min buffer size being smaller.
      ASSERT_GE(buffer_collection_info->settings.buffer_settings.size_bytes, 64 * 1024 * 3 / 2, "");
      // The vmo has room for the nominal size of the portion of the VMO the client can use.
      ASSERT_LE(buffer_collection_info->buffers[i].vmo_usable_start +
                    buffer_collection_info->settings.buffer_settings.size_bytes,
                size_bytes, "");
    } else {
      ASSERT_EQ(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
    }
  }
}

TEST(Sysmem, MinBufferCount) {
  auto collection = make_single_participant_collection();

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 3;
  constraints.min_buffer_count = 5;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 64 * 1024,
      .max_size_bytes = 128 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);
  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocation_result = collection->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocation_result);
  ASSERT_OK(allocation_result->status);
  ASSERT_EQ(allocation_result->buffer_collection_info.buffer_count, 5, "");
}

TEST(Sysmem, BufferName) {
  auto collection = make_single_participant_collection();

  const char kSysmemName[] = "abcdefghijkl\0mnopqrstuvwxyz";
  const char kLowPrioName[] = "low_pri";
  // Override default set in make_single_participant_collection)
  collection->SetName(2000000, kSysmemName);
  collection->SetName(0, kLowPrioName);

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);
  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocation_result = collection->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocation_result);
  ASSERT_OK(allocation_result->status);

  ASSERT_EQ(allocation_result->buffer_collection_info.buffer_count, 1, "");
  zx_handle_t vmo = allocation_result->buffer_collection_info.buffers[0].vmo.get();
  char vmo_name[ZX_MAX_NAME_LEN];
  ASSERT_EQ(ZX_OK, zx_object_get_property(vmo, ZX_PROP_NAME, vmo_name, sizeof(vmo_name)));

  // Should be equal up to the first null, plus an index
  EXPECT_EQ(std::string("abcdefghijkl:0"), std::string(vmo_name));
  EXPECT_EQ(0u, vmo_name[ZX_MAX_NAME_LEN - 1]);
}

TEST(Sysmem, NoToken) {
  auto allocator = connect_to_sysmem_driver();
  zx::status collection_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints);
  auto [collection_client_end, collection_server_end] = std::move(*collection_endpoints);
  auto collection = fidl::BindSyncClient(std::move(collection_client_end));

  ASSERT_OK(allocator->AllocateNonSharedCollection(std::move(collection_server_end)));

  SetDefaultCollectionName(collection);

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  // Ask for display usage to encourage using the ram coherency domain.
  constraints.usage.display = fuchsia_sysmem::wire::kDisplayUsageLayer;
  constraints.min_buffer_count_for_camping = 3;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 64 * 1024,
      .max_size_bytes = 128 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = true,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);
  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocation_result = collection->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocation_result);
  ASSERT_OK(allocation_result->status);

  auto buffer_collection_info = &allocation_result->buffer_collection_info;
  ASSERT_EQ(buffer_collection_info->buffer_count, 3, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, 64 * 1024, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
            fuchsia_sysmem::wire::CoherencyDomain::kRam, "");
  ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");

  for (uint32_t i = 0; i < 64; ++i) {
    if (i < 3) {
      ASSERT_NE(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
      uint64_t size_bytes = 0;
      auto status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo.get(), &size_bytes);
      ASSERT_EQ(status, ZX_OK, "");
      ASSERT_EQ(size_bytes, 64 * 1024, "");
    } else {
      ASSERT_EQ(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
    }
  }
}

TEST(Sysmem, NoSync) {
  auto allocator_1 = connect_to_sysmem_driver();
  ASSERT_OK(allocator_1);

  auto token_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_1);
  auto [token_client_1, token_server_1] = std::move(*token_endpoints_1);
  auto token_1 = fidl::BindSyncClient(std::move(token_client_1));

  ASSERT_OK(allocator_1->AllocateSharedCollection(std::move(token_server_1)));

  const char* kAllocatorName = "TestAllocator";
  allocator_1->SetDebugClientInfo(fidl::StringView::FromExternal(kAllocatorName), 1u);

  const char* kClientName = "TestClient";
  token_1->SetDebugClientInfo(fidl::StringView::FromExternal(kClientName), 2u);

  // Make another token so we can bind it and set a name on the collection.
  fidl::WireSyncClient<fuchsia_sysmem::BufferCollection> collection_3;

  {
    auto token_endpoints_3 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
    ASSERT_OK(token_endpoints_3);
    auto [token_client_3, token_server_3] = std::move(*token_endpoints_3);

    auto collection_endpoints_3 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
    ASSERT_OK(collection_endpoints_3);
    auto [collection_client_3, collection_server_3] = std::move(*collection_endpoints_3);

    ASSERT_OK(token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_3)));
    ASSERT_OK(token_1->Sync());

    ASSERT_OK(allocator_1->BindSharedCollection(std::move(token_client_3),
                                                std::move(collection_server_3)));
    collection_3 = fidl::BindSyncClient(std::move(collection_client_3));

    const char* kCollectionName = "TestCollection";
    collection_3->SetName(1u, fidl::StringView::FromExternal(kCollectionName));
  }

  auto token_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_2);
  auto [token_client_2, token_server_2] = std::move(*token_endpoints_2);
  auto token_2 = fidl::BindSyncClient(std::move(token_client_2));

  auto collection_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_1);
  auto [collection_client_1, collection_server_1] = std::move(*collection_endpoints_1);
  auto collection_1 = fidl::BindSyncClient(std::move(collection_client_1));

  const char* kClient2Name = "TestClient2";
  token_2->SetDebugClientInfo(fidl::StringView::FromExternal(kClient2Name), 3u);

  // Close to prevent Sync on token_client_1 from failing later due to LogicalBufferCollection
  // failure caused by the token handle closing.
  token_2->Close();

  ASSERT_OK(
      allocator_1->BindSharedCollection(token_2.TakeClientEnd(), std::move(collection_server_1)));

  // Duplicate has not been sent (or received) so this should fail.
  auto sync_result = collection_1->Sync();
  EXPECT_NE(sync_result.status(), ZX_OK, "");

  SetDefaultCollectionName(collection_1);

  // The duplicate/sync should print out an error message but succeed.
  ASSERT_OK(token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_2)));
  ASSERT_OK(token_1->Sync());
}

TEST(Sysmem, MultipleParticipants) {
  auto allocator = connect_to_sysmem_driver();

  auto token_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_1);
  auto [token_client_1, token_server_1] = std::move(*token_endpoints_1);
  auto token_1 = fidl::BindSyncClient(std::move(token_client_1));

  // Client 1 creates a token and new LogicalBufferCollection using
  // AllocateSharedCollection().
  ASSERT_OK(allocator->AllocateSharedCollection(std::move(token_server_1)));

  auto token_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_2);
  auto [token_client_2, token_server_2] = std::move(*token_endpoints_2);

  // Client 1 duplicates its token and gives the duplicate to client 2 (this
  // test is single proc, so both clients are coming from this client
  // process - normally the two clients would be in separate processes with
  // token_client_2 transferred to another participant).
  ASSERT_OK(token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_2)));

  auto token_endpoints_3 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_3);
  auto [token_client_3, token_server_3] = std::move(*token_endpoints_3);

  // Client 3 is used to test a participant that doesn't set any constraints
  // and only wants a notification that the allocation is done.
  ASSERT_OK(token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_3)));

  auto collection_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_1);
  auto [collection_client_1, collection_server_1] = std::move(*collection_endpoints_1);
  auto collection_1 = fidl::BindSyncClient(std::move(collection_client_1));

  ASSERT_NE(token_1.client_end().channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator->BindSharedCollection(token_1.TakeClientEnd(), std::move(collection_server_1)));

  SetDefaultCollectionName(collection_1);

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_1;
  constraints_1.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints_1.min_buffer_count_for_camping = 3;
  constraints_1.has_buffer_memory_constraints = true;
  constraints_1.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      // This min_size_bytes is intentionally too small to hold the min_coded_width and
      // min_coded_height in NV12
      // format.
      .min_size_bytes = 64 * 1024,
      // Allow a max that's just large enough to accommodate the size implied
      // by the min frame size and PixelFormat.
      .max_size_bytes = (512 * 512) * 3 / 2,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  constraints_1.image_format_constraints_count = 1;
  fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints_1 =
      constraints_1.image_format_constraints[0];
  image_constraints_1.pixel_format.type = fuchsia_sysmem::wire::PixelFormatType::kNv12;
  image_constraints_1.color_spaces_count = 1;
  image_constraints_1.color_space[0] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec709,
  };
  // The min dimensions intentionally imply a min size that's larger than
  // buffer_memory_constraints.min_size_bytes.
  image_constraints_1.min_coded_width = 256;
  image_constraints_1.max_coded_width = std::numeric_limits<uint32_t>::max();
  image_constraints_1.min_coded_height = 256;
  image_constraints_1.max_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints_1.min_bytes_per_row = 256;
  image_constraints_1.max_bytes_per_row = std::numeric_limits<uint32_t>::max();
  image_constraints_1.max_coded_width_times_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints_1.layers = 1;
  image_constraints_1.coded_width_divisor = 2;
  image_constraints_1.coded_height_divisor = 2;
  image_constraints_1.bytes_per_row_divisor = 2;
  image_constraints_1.start_offset_divisor = 2;
  image_constraints_1.display_width_divisor = 1;
  image_constraints_1.display_height_divisor = 1;

  // Start with constraints_2 a copy of constraints_1.  There are no handles
  // in the constraints struct so a struct copy instead of clone is fine here.
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_2(constraints_1);
  // Modify constraints_2 to require double the width and height.
  constraints_2.image_format_constraints[0].min_coded_width = 512;
  constraints_2.image_format_constraints[0].min_coded_height = 512;

#if SYSMEM_FUZZ_CORPUS
  FILE* ofp = fopen("/cache/sysmem_fuzz_corpus_multi_buffer_collecton_constraints.dat", "wb");
  if (ofp) {
    fwrite(&constraints_1, sizeof(fuchsia_sysmem::wire::BufferCollectionConstraints), 1, ofp);
    fwrite(&constraints_2, sizeof(fuchsia_sysmem::wire::BufferCollectionConstraints), 1, ofp);
    fclose(ofp);
  } else {
    fprintf(stderr, "Failed to write sysmem multi BufferCollectionConstraints corpus file.\n");
    fflush(stderr);
  }
#endif  // SYSMEM_FUZZ_CORPUS

  ASSERT_OK(collection_1->SetConstraints(true, std::move(constraints_1)));

  // Client 2 connects to sysmem separately.
  auto allocator_2 = connect_to_sysmem_driver();
  ASSERT_OK(allocator_2);

  auto collection_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_2);
  auto [collection_client_2, collection_server_2] = std::move(*collection_endpoints_2);
  auto collection_2 = fidl::BindSyncClient(std::move(collection_client_2));

  // Just because we can, perform this sync as late as possible, just before
  // the BindSharedCollection() via allocator2_client_2.  Without this Sync(),
  // the BindSharedCollection() might arrive at the server before the
  // Duplicate() that delivered the server end of token_client_2 to sysmem,
  // which would cause sysmem to not recognize the token.
  ASSERT_OK(collection_1->Sync());

  // For the moment, cause the server to count some fake churn, enough times to cause the server
  // to re-alloc all the server's held FIDL tables 4 times before we continue.  These are
  // synchronous calls, so the 4 re-allocs are done by the time this loop completes.
  //
  // TODO(fxbug.dev/33670): Switch to creating real churn instead, once we have new messages that
  // can create real churn.
  constexpr uint32_t kChurnCount = 256 * 2;  // 256 * 4;
  for (uint32_t i = 0; i < kChurnCount; ++i) {
    ASSERT_OK(collection_1->Sync());
  }

  ASSERT_NE(token_client_2.channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator_2->BindSharedCollection(std::move(token_client_2), std::move(collection_server_2)));

  auto collection_endpoints_3 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_3);
  auto [collection_client_3, collection_server_3] = std::move(*collection_endpoints_3);
  auto collection_3 = fidl::BindSyncClient(std::move(collection_client_3));

  ASSERT_NE(token_client_3.channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator_2->BindSharedCollection(std::move(token_client_3), std::move(collection_server_3)));

  fuchsia_sysmem::wire::BufferCollectionConstraints empty_constraints;
  ASSERT_OK(collection_3->SetConstraints(false, std::move(empty_constraints)));

  // Not all constraints have been input, so the buffers haven't been
  // allocated yet.
  auto check_result_1 = collection_1->CheckBuffersAllocated();
  ASSERT_OK(check_result_1);
  EXPECT_EQ(check_result_1->status, ZX_ERR_UNAVAILABLE, "");
  auto check_result_2 = collection_2->CheckBuffersAllocated();
  ASSERT_OK(check_result_2);
  EXPECT_EQ(check_result_2->status, ZX_ERR_UNAVAILABLE, "");

  ASSERT_OK(collection_2->SetConstraints(true, std::move(constraints_2)));

  //
  // Only after both participants (both clients) have SetConstraints() will
  // the allocation be successful.
  //

  auto allocate_result_1 = collection_1->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocate_result_1);
  ASSERT_OK(allocate_result_1->status);

  auto check_result_allocated_1 = collection_1->CheckBuffersAllocated();
  ASSERT_OK(check_result_allocated_1);
  EXPECT_OK(check_result_allocated_1->status);
  auto check_result_allocated_2 = collection_2->CheckBuffersAllocated();
  ASSERT_OK(check_result_allocated_2);
  EXPECT_OK(check_result_allocated_2->status);

  auto allocate_result_2 = collection_2->WaitForBuffersAllocated();
  ASSERT_OK(allocate_result_2);
  ASSERT_OK(allocate_result_2->status);

  auto allocate_result_3 = collection_3->WaitForBuffersAllocated();
  ASSERT_OK(allocate_result_3);
  ASSERT_OK(allocate_result_3->status);

  //
  // buffer_collection_info_1 and buffer_collection_info_2 should be exactly
  // equal except their non-zero handle values, which should be different.  We
  // verify the handle values then check that the structs are exactly the same
  // with handle values zeroed out.
  //
  auto buffer_collection_info_1 = &allocate_result_1->buffer_collection_info;
  auto buffer_collection_info_2 = &allocate_result_2->buffer_collection_info;
  auto buffer_collection_info_3 = &allocate_result_3->buffer_collection_info;

  for (uint32_t i = 0; i < std::size(buffer_collection_info_1->buffers); ++i) {
    ASSERT_EQ(buffer_collection_info_1->buffers[i].vmo.get() != ZX_HANDLE_INVALID,
              buffer_collection_info_2->buffers[i].vmo.get() != ZX_HANDLE_INVALID, "");
    if (buffer_collection_info_1->buffers[i].vmo.get() != ZX_HANDLE_INVALID) {
      // The handle values must be different.
      ASSERT_NE(buffer_collection_info_1->buffers[i].vmo.get(),
                buffer_collection_info_2->buffers[i].vmo.get(), "");
      // For now, the koid(s) are expected to be equal.  This is not a
      // fundamental check, in that sysmem could legitimately change in
      // future to vend separate child VMOs (of the same portion of a
      // non-copy-on-write parent VMO) to the two participants and that
      // would still be potentially valid overall.
      zx_koid_t koid_1 = get_koid(buffer_collection_info_1->buffers[i].vmo.get());
      zx_koid_t koid_2 = get_koid(buffer_collection_info_2->buffers[i].vmo.get());
      ASSERT_EQ(koid_1, koid_2, "");
    }

    // Buffer collection 3 passed false to SetConstraints(), so we get no VMOs.
    ASSERT_EQ(ZX_HANDLE_INVALID, buffer_collection_info_3->buffers[i].vmo.get(), "");
  }

  //
  // Verify that buffer_collection_info_1 paid attention to constraints_2, and
  // that buffer_collection_info_2 makes sense.
  //

  // Because each specified min_buffer_count_for_camping 3, and each
  // participant camping count adds together since they camp independently.
  ASSERT_EQ(buffer_collection_info_1->buffer_count, 6, "");
  // The size should be sufficient for the whole NV12 frame, not just
  // min_size_bytes.  In other words, the portion of the VMO the client can
  // use is large enough to hold the min image size, despite the min buffer
  // size being smaller.
  ASSERT_GE(buffer_collection_info_1->settings.buffer_settings.size_bytes, (512 * 512) * 3 / 2, "");
  ASSERT_EQ(buffer_collection_info_1->settings.buffer_settings.is_physically_contiguous, false, "");
  ASSERT_EQ(buffer_collection_info_1->settings.buffer_settings.is_secure, false, "");
  // We specified image_format_constraints so the result must also have
  // image_format_constraints.
  ASSERT_EQ(buffer_collection_info_1->settings.has_image_format_constraints, true, "");

  for (uint32_t i = 0; i < 64; ++i) {
    if (i < 6) {
      ASSERT_NE(buffer_collection_info_1->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
      ASSERT_NE(buffer_collection_info_2->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");

      uint64_t size_bytes_1 = 0;
      ASSERT_OK(buffer_collection_info_1->buffers[i].vmo.get_size(&size_bytes_1));

      uint64_t size_bytes_2 = 0;
      ASSERT_OK(zx_vmo_get_size(buffer_collection_info_2->buffers[i].vmo.get(), &size_bytes_2));

      // The vmo has room for the nominal size of the portion of the VMO
      // the client can use.  These checks should pass even if sysmem were
      // to vend different child VMOs to the two participants.
      ASSERT_LE(buffer_collection_info_1->buffers[i].vmo_usable_start +
                    buffer_collection_info_1->settings.buffer_settings.size_bytes,
                size_bytes_1, "");
      ASSERT_LE(buffer_collection_info_2->buffers[i].vmo_usable_start +
                    buffer_collection_info_2->settings.buffer_settings.size_bytes,
                size_bytes_2, "");

      // Clear out vmo handles for memcmp below
      buffer_collection_info_1->buffers[i].vmo.reset();
      buffer_collection_info_2->buffers[i].vmo.reset();
    } else {
      ASSERT_EQ(buffer_collection_info_1->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
      ASSERT_EQ(buffer_collection_info_2->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
    }
  }

  int32_t memcmp_result = memcmp(buffer_collection_info_1, buffer_collection_info_2,
                                 sizeof(fuchsia_sysmem::wire::BufferCollectionInfo2));
  // Check that buffer_collection_info_1 and buffer_collection_info_2 are
  // consistent.
  ASSERT_EQ(memcmp_result, 0, "");

  memcmp_result = memcmp(buffer_collection_info_1, buffer_collection_info_3,
                         sizeof(fuchsia_sysmem::wire::BufferCollectionInfo2));
  // Check that buffer_collection_info_1 and buffer_collection_info_3 are
  // consistent, except for the vmos.
  ASSERT_EQ(memcmp_result, 0, "");

  // Close to ensure grabbing null constraints from a closed collection
  // doesn't crash
  EXPECT_OK(collection_3->Close());
}

// This test is mainly to have something in the fuzzer corpus using format modifiers.
TEST(Sysmem, ComplicatedFormatModifiers) {
  auto allocator = connect_to_sysmem_driver();

  auto token_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_1);
  auto [token_client_1, token_server_1] = std::move(*token_endpoints_1);
  auto token_1 = fidl::BindSyncClient(std::move(token_client_1));

  ASSERT_OK(allocator->AllocateSharedCollection(std::move(token_server_1)));

  auto token_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_2);
  auto [token_client_2, token_server_2] = std::move(*token_endpoints_2);

  ASSERT_OK(token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_2)));

  auto collection_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_1);
  auto [collection_client_1, collection_server_1] = std::move(*collection_endpoints_1);
  auto collection_1 = fidl::BindSyncClient(std::move(collection_client_1));

  ASSERT_NE(token_1.client_end().channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator->BindSharedCollection(token_1.TakeClientEnd(), std::move(collection_server_1)));

  SetDefaultCollectionName(collection_1);

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_1;
  constraints_1.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints_1.min_buffer_count_for_camping = 1;

  constexpr uint64_t kFormatModifiers[] = {
      fuchsia_sysmem::wire::kFormatModifierLinear,
      fuchsia_sysmem::wire::kFormatModifierIntelI915XTiled,
      fuchsia_sysmem::wire::kFormatModifierArmAfbc16X16SplitBlockSparseYuvTeTiledHeader,
      fuchsia_sysmem::wire::kFormatModifierArmAfbc16X16Te};
  constraints_1.image_format_constraints_count = std::size(kFormatModifiers);

  for (uint32_t i = 0; i < std::size(kFormatModifiers); i++) {
    fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints_1 =
        constraints_1.image_format_constraints[i];

    image_constraints_1.pixel_format.type = i < 2 ? fuchsia_sysmem::wire::PixelFormatType::kR8G8B8A8
                                                  : fuchsia_sysmem::wire::PixelFormatType::kBgra32;
    image_constraints_1.pixel_format.has_format_modifier = true;
    image_constraints_1.pixel_format.format_modifier.value = kFormatModifiers[i];
    image_constraints_1.color_spaces_count = 1;
    image_constraints_1.color_space[0] = fuchsia_sysmem::wire::ColorSpace{
        .type = fuchsia_sysmem::wire::ColorSpaceType::kSrgb,
    };
  }

  // Start with constraints_2 a copy of constraints_1.  There are no handles
  // in the constraints struct so a struct copy instead of clone is fine here.
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_2(constraints_1);

  for (uint32_t i = 0; i < constraints_2.image_format_constraints_count; i++) {
    // Modify constraints_2 to require nonzero image dimensions.
    constraints_2.image_format_constraints[i].required_max_coded_height = 512;
    constraints_2.image_format_constraints[i].required_max_coded_width = 512;
  }

#if SYSMEM_FUZZ_CORPUS
  FILE* ofp = fopen("/cache/sysmem_fuzz_corpus_multi_buffer_format_modifier_constraints.dat", "wb");
  if (ofp) {
    fwrite(&constraints_1, sizeof(fuchsia_sysmem::wire::BufferCollectionConstraints), 1, ofp);
    fwrite(&constraints_2, sizeof(fuchsia_sysmem::wire::BufferCollectionConstraints), 1, ofp);
    fclose(ofp);
  } else {
    fprintf(stderr,
            "Failed to write sysmem multi BufferCollectionConstraints corpus file at line %d\n",
            __LINE__);
    fflush(stderr);
  }
#endif  // SYSMEM_FUZZ_CORPUS

  ASSERT_OK(collection_1->SetConstraints(true, std::move(constraints_1)));

  auto collection_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_2);
  auto [collection_client_2, collection_server_2] = std::move(*collection_endpoints_2);
  auto collection_2 = fidl::BindSyncClient(std::move(collection_client_2));

  ASSERT_OK(collection_1->Sync());

  ASSERT_NE(token_client_2.channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator->BindSharedCollection(std::move(token_client_2), std::move(collection_server_2)));

  ASSERT_OK(collection_2->SetConstraints(true, std::move(constraints_2)));

  //
  // Only after both participants (both clients) have SetConstraints() will
  // the allocation be successful.
  //
  auto allocate_result_1 = collection_1->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocate_result_1);
  ASSERT_OK(allocate_result_1->status);
}

TEST(Sysmem, MultipleParticipantsColorspaceRanking) {
  auto allocator = connect_to_sysmem_driver();

  auto token_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_1);
  auto [token_client_1, token_server_1] = std::move(*token_endpoints_1);
  auto token_1 = fidl::BindSyncClient(std::move(token_client_1));

  ASSERT_OK(allocator->AllocateSharedCollection(std::move(token_server_1)));

  auto token_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_2);
  auto [token_client_2, token_server_2] = std::move(*token_endpoints_2);

  ASSERT_OK(token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_2)));

  auto collection_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_1);
  auto [collection_client_1, collection_server_1] = std::move(*collection_endpoints_1);
  auto collection_1 = fidl::BindSyncClient(std::move(collection_client_1));

  ASSERT_NE(token_1.client_end().channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator->BindSharedCollection(token_1.TakeClientEnd(), std::move(collection_server_1)));

  SetDefaultCollectionName(collection_1);

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_1;
  constraints_1.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints_1.min_buffer_count_for_camping = 1;
  constraints_1.has_buffer_memory_constraints = true;
  constraints_1.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 64 * 1024,
      .max_size_bytes = 128 * 1024,
      .cpu_domain_supported = true,
  };
  constraints_1.image_format_constraints_count = 1;
  fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints_1 =
      constraints_1.image_format_constraints[0];
  image_constraints_1.pixel_format.type = fuchsia_sysmem::wire::PixelFormatType::kNv12;
  image_constraints_1.color_spaces_count = 3;
  image_constraints_1.color_space[0] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec601Pal,
  };
  image_constraints_1.color_space[1] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec601PalFullRange,
  };
  image_constraints_1.color_space[2] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec709,
  };

  // Start with constraints_2 a copy of constraints_1.  There are no handles
  // in the constraints struct so a struct copy instead of clone is fine here.
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_2(constraints_1);
  fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints_2 =
      constraints_2.image_format_constraints[0];
  image_constraints_2.color_spaces_count = 2;
  image_constraints_2.color_space[0] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec601PalFullRange,
  };
  image_constraints_2.color_space[1] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec709,
  };

  ASSERT_OK(collection_1->SetConstraints(true, std::move(constraints_1)));

  auto collection_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_2);
  auto [collection_client_2, collection_server_2] = std::move(*collection_endpoints_2);
  auto collection_2 = fidl::BindSyncClient(std::move(collection_client_2));

  ASSERT_OK(collection_1->Sync());

  ASSERT_NE(token_client_2.channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator->BindSharedCollection(std::move(token_client_2), std::move(collection_server_2)));

  ASSERT_OK(collection_2->SetConstraints(true, std::move(constraints_2)));

  // Both collections should yield the same results
  auto check_allocation_results =
      [](const ::fidl::WireResult<::fuchsia_sysmem::BufferCollection::WaitForBuffersAllocated>
             allocation_result) {
        ASSERT_OK(allocation_result);
        ASSERT_OK(allocation_result->status);

        auto buffer_collection_info = &allocation_result->buffer_collection_info;
        ASSERT_EQ(buffer_collection_info->buffer_count, 2, "");
        ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, true, "");
        ASSERT_EQ(buffer_collection_info->settings.image_format_constraints.pixel_format.type,
                  fuchsia_sysmem::wire::PixelFormatType::kNv12, "");
        ASSERT_EQ(buffer_collection_info->settings.image_format_constraints.color_spaces_count, 1,
                  "");
        ASSERT_EQ(buffer_collection_info->settings.image_format_constraints.color_space[0].type,
                  fuchsia_sysmem::wire::ColorSpaceType::kRec709, "");
      };

  check_allocation_results(collection_1->WaitForBuffersAllocated());
  check_allocation_results(collection_2->WaitForBuffersAllocated());
}

TEST(Sysmem, DuplicateSync) {
  auto allocator = connect_to_sysmem_driver();

  auto token_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_1);
  auto [token_client_1, token_server_1] = std::move(*token_endpoints_1);
  auto token_1 = fidl::BindSyncClient(std::move(token_client_1));

  ASSERT_OK(allocator->AllocateSharedCollection(std::move(token_server_1)));

  zx_rights_t rights_attenuation_masks[] = {ZX_RIGHT_SAME_RIGHTS};
  auto duplicate_result =
      token_1->DuplicateSync(fidl::VectorView<zx_rights_t>::FromExternal(rights_attenuation_masks));
  ASSERT_OK(duplicate_result);
  ASSERT_EQ(duplicate_result->tokens.count(), 1);
  auto token_client_2 = std::move(duplicate_result->tokens[0]);

  auto collection_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_1);
  auto [collection_client_1, collection_server_1] = std::move(*collection_endpoints_1);
  auto collection_1 = fidl::BindSyncClient(std::move(collection_client_1));

  ASSERT_NE(token_1.client_end().channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator->BindSharedCollection(token_1.TakeClientEnd(), std::move(collection_server_1)));

  SetDefaultCollectionName(collection_1);

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_1;
  constraints_1.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints_1.min_buffer_count_for_camping = 1;
  constraints_1.has_buffer_memory_constraints = true;
  constraints_1.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 64 * 1024,
      .cpu_domain_supported = true,
  };

  // Start with constraints_2 a copy of constraints_1.  There are no handles
  // in the constraints struct so a struct copy instead of clone is fine here.
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_2(constraints_1);
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_3(constraints_1);
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_4(constraints_1);

  ASSERT_OK(collection_1->SetConstraints(true, std::move(constraints_1)));

  auto collection_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_2);
  auto collection_2 = fidl::BindSyncClient(std::move(collection_endpoints_2->client));

  auto token_2 = fidl::BindSyncClient(std::move(token_client_2));
  // Remove write from last token
  zx_rights_t rights_attenuation_masks_2[] = {ZX_RIGHT_SAME_RIGHTS,
                                              ZX_DEFAULT_VMO_RIGHTS & ~ZX_RIGHT_WRITE};
  auto duplicate_result_2 = token_2->DuplicateSync(
      fidl::VectorView<zx_rights_t>::FromExternal(rights_attenuation_masks_2));
  ASSERT_OK(duplicate_result_2);
  ASSERT_EQ(duplicate_result_2->tokens.count(), 2);

  ASSERT_NE(token_2.client_end().channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(allocator->BindSharedCollection(token_2.TakeClientEnd(),
                                            std::move(collection_endpoints_2->server)));

  ASSERT_OK(collection_2->SetConstraints(true, std::move(constraints_2)));

  auto collection_endpoints_3 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_3);
  auto collection_3 = fidl::BindSyncClient(std::move(collection_endpoints_3->client));

  auto collection_endpoints_4 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_4);
  auto collection_4 = fidl::BindSyncClient(std::move(collection_endpoints_4->client));

  ASSERT_NE(duplicate_result_2->tokens[0].channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(allocator->BindSharedCollection(std::move(duplicate_result_2->tokens[0]),
                                            std::move(collection_endpoints_3->server)));

  ASSERT_NE(duplicate_result_2->tokens[1].channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(allocator->BindSharedCollection(std::move(duplicate_result_2->tokens[1]),
                                            std::move(collection_endpoints_4->server)));

  ASSERT_OK(collection_3->SetConstraints(true, constraints_3));
  ASSERT_OK(collection_4->SetConstraints(true, constraints_4));

  //
  // Only after all participants have SetConstraints() will
  // the allocation be successful.
  //
  auto allocate_result_1 = collection_1->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocate_result_1);
  ASSERT_OK(allocate_result_1->status);

  auto allocate_result_3 = collection_3->WaitForBuffersAllocated();
  ASSERT_OK(allocate_result_3);
  ASSERT_OK(allocate_result_3->status);

  auto allocate_result_4 = collection_4->WaitForBuffersAllocated();
  ASSERT_OK(allocate_result_4);
  ASSERT_OK(allocate_result_4->status);

  // Check rights
  zx_info_handle_basic_t info = {};
  ASSERT_OK(allocate_result_3->buffer_collection_info.buffers[0].vmo.get_info(
      ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(info.rights & ZX_RIGHT_WRITE, ZX_RIGHT_WRITE);

  ASSERT_OK(allocate_result_4->buffer_collection_info.buffers[0].vmo.get_info(
      ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(info.rights & ZX_RIGHT_WRITE, 0);
}

TEST(Sysmem, CloseWithOutstandingWait) {
  auto allocator_1 = connect_to_sysmem_driver();
  ASSERT_OK(allocator_1);

  auto token_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_1);
  auto [token_client_1, token_server_1] = std::move(*token_endpoints_1);
  auto token_1 = fidl::BindSyncClient(std::move(token_client_1));

  ASSERT_OK(allocator_1->AllocateSharedCollection(std::move(token_server_1)));

  auto token_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_2);
  auto [token_client_2, token_server_2] = std::move(*token_endpoints_2);

  ASSERT_OK(token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_2)));

  auto collection_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_1);
  auto [collection_client_1, collection_server_1] = std::move(*collection_endpoints_1);
  auto collection_1 = fidl::BindSyncClient(std::move(collection_client_1));

  ASSERT_NE(token_1.client_end().channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator_1->BindSharedCollection(token_1.TakeClientEnd(), std::move(collection_server_1)));

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_1;
  constraints_1.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints_1.min_buffer_count_for_camping = 1;

  constraints_1.image_format_constraints_count = 1;

  fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints_1 =
      constraints_1.image_format_constraints[0];

  image_constraints_1.pixel_format.type = fuchsia_sysmem::wire::PixelFormatType::kR8G8B8A8;
  image_constraints_1.pixel_format.has_format_modifier = true;
  image_constraints_1.pixel_format.format_modifier.value =
      fuchsia_sysmem::wire::kFormatModifierLinear;
  image_constraints_1.color_spaces_count = 1;
  image_constraints_1.color_space[0] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kSrgb,
  };

  ASSERT_OK(collection_1->SetConstraints(true, std::move(constraints_1)));

  std::thread wait_thread([&collection_1]() {
    auto allocate_result_1 = collection_1->WaitForBuffersAllocated();
    EXPECT_EQ(allocate_result_1.status(), ZX_ERR_PEER_CLOSED, "");
  });

  // Try to wait until the wait has been processed by the server.
  zx_nanosleep(zx_deadline_after(ZX_SEC(5)));

  auto collection_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_2);
  auto [collection_client_2, collection_server_2] = std::move(*collection_endpoints_2);
  auto collection_2 = fidl::BindSyncClient(std::move(collection_client_2));

  ASSERT_OK(collection_1->Sync());

  ASSERT_NE(token_client_2.channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator_1->BindSharedCollection(std::move(token_client_2), std::move(collection_server_2)));

  collection_2 = {};

  wait_thread.join();

  ASSERT_OK(verify_connectivity(*allocator_1));
}

TEST(Sysmem, ConstraintsRetainedBeyondCleanClose) {
  auto allocator = connect_to_sysmem_driver();

  auto token_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_1);
  auto [token_client_1, token_server_1] = std::move(*token_endpoints_1);
  auto token_1 = fidl::BindSyncClient(std::move(token_client_1));

  // Client 1 creates a token and new LogicalBufferCollection using
  // AllocateSharedCollection().
  ASSERT_OK(allocator->AllocateSharedCollection(std::move(token_server_1)));

  auto token_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_2);
  auto [token_client_2, token_server_2] = std::move(*token_endpoints_2);

  // Client 1 duplicates its token and gives the duplicate to client 2 (this
  // test is single proc, so both clients are coming from this client
  // process - normally the two clients would be in separate processes with
  // token_client_2 transferred to another participant).
  ASSERT_OK(token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_2)));

  auto collection_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_1);
  auto [collection_client_1, collection_server_1] = std::move(*collection_endpoints_1);
  auto collection_1 = fidl::BindSyncClient(std::move(collection_client_1));

  ASSERT_NE(token_1.client_end().channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator->BindSharedCollection(token_1.TakeClientEnd(), std::move(collection_server_1)));

  SetDefaultCollectionName(collection_1);

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_1;
  constraints_1.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints_1.min_buffer_count_for_camping = 2;
  constraints_1.has_buffer_memory_constraints = true;
  constraints_1.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 64 * 1024,
      .max_size_bytes = 64 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };

  // constraints_2 is just a copy of constraints_1 - since both participants
  // specify min_buffer_count_for_camping 2, the total number of allocated
  // buffers will be 4.  There are no handles in the constraints struct so a
  // struct copy instead of clone is fine here.
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_2(constraints_1);
  ASSERT_EQ(constraints_2.min_buffer_count_for_camping, 2, "");

  ASSERT_OK(collection_1->SetConstraints(true, constraints_1));

  // Client 2 connects to sysmem separately.
  auto allocator_2 = connect_to_sysmem_driver();
  ASSERT_OK(allocator_2);

  auto collection_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_2);
  auto [collection_client_2, collection_server_2] = std::move(*collection_endpoints_2);
  auto collection_2 = fidl::BindSyncClient(std::move(collection_client_2));

  // Just because we can, perform this sync as late as possible, just before
  // the BindSharedCollection() via allocator2_client_2.  Without this Sync(),
  // the BindSharedCollection() might arrive at the server before the
  // Duplicate() that delivered the server end of token_client_2 to sysmem,
  // which would cause sysmem to not recognize the token.
  ASSERT_OK(collection_1->Sync());

  // client 1 will now do a clean Close(), but client 1's constraints will be
  // retained by the LogicalBufferCollection.
  ASSERT_OK(collection_1->Close());
  // close client 1's channel.
  collection_1 = {};

  // Wait briefly so that LogicalBufferCollection will have seen the channel
  // closure of client 1 before client 2 sets constraints.  If we wanted to
  // eliminate this sleep we could add a call to query how many
  // BufferCollection views still exist per LogicalBufferCollection, but that
  // call wouldn't be meant to be used by normal clients, so it seems best to
  // avoid adding such a call.
  nanosleep_duration(zx::msec(250));

  ASSERT_NE(token_client_2.channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator_2->BindSharedCollection(std::move(token_client_2), std::move(collection_server_2)));

  SetDefaultCollectionName(collection_2);

  // Not all constraints have been input (client 2 hasn't SetConstraints()
  // yet), so the buffers haven't been allocated yet.
  auto check_result_2 = collection_2->CheckBuffersAllocated();
  ASSERT_OK(check_result_2);
  EXPECT_EQ(check_result_2->status, ZX_ERR_UNAVAILABLE, "");

  ASSERT_OK(collection_2->SetConstraints(true, std::move(constraints_2)));

  //
  // Now that client 2 has SetConstraints(), the allocation will proceed, with
  // client 1's constraints included despite client 1 having done a clean
  // Close().
  //
  auto allocate_result_2 = collection_2->WaitForBuffersAllocated();
  ASSERT_OK(allocate_result_2);
  ASSERT_OK(allocate_result_2->status);

  // The fact that this is 4 instead of 2 proves that client 1's constraints
  // were taken into account.
  ASSERT_EQ(allocate_result_2->buffer_collection_info.buffer_count, 4, "");
}

TEST(Sysmem, HeapConstraints) {
  auto collection = make_single_participant_collection();

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = fuchsia_sysmem::wire::kVulkanUsageTransferDst;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = true,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem::wire::HeapType::kSystemRam}};

  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocate_result = collection->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocate_result);
  ASSERT_OK(allocate_result->status);
  ASSERT_EQ(allocate_result->buffer_collection_info.buffer_count, 1, "");
  ASSERT_EQ(allocate_result->buffer_collection_info.settings.buffer_settings.coherency_domain,
            fuchsia_sysmem::wire::CoherencyDomain::kInaccessible, "");
  ASSERT_EQ(allocate_result->buffer_collection_info.settings.buffer_settings.heap,
            fuchsia_sysmem::wire::HeapType::kSystemRam, "");
  ASSERT_EQ(
      allocate_result->buffer_collection_info.settings.buffer_settings.is_physically_contiguous,
      true, "");
}

TEST(Sysmem, CpuUsageAndInaccessibleDomainFails) {
  auto collection = make_single_participant_collection();

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = true,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem::wire::HeapType::kSystemRam}};

  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocate_result = collection->WaitForBuffersAllocated();
  // usage.cpu != 0 && inaccessible_domain_supported is expected to result in failure to
  // allocate.
  ASSERT_NE(allocate_result.status(), ZX_OK, "");
}

TEST(Sysmem, SystemRamHeapSupportsAllDomains) {
  fuchsia_sysmem::wire::CoherencyDomain domains[] = {
      fuchsia_sysmem::wire::CoherencyDomain::kCpu,
      fuchsia_sysmem::wire::CoherencyDomain::kRam,
      fuchsia_sysmem::wire::CoherencyDomain::kInaccessible,
  };

  for (const fuchsia_sysmem::wire::CoherencyDomain domain : domains) {
    auto collection = make_single_participant_collection();

    fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
    constraints.usage.vulkan = fuchsia_sysmem::wire::kVulkanImageUsageTransferDst;
    constraints.min_buffer_count_for_camping = 1;
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
        .min_size_bytes = 4 * 1024,
        .max_size_bytes = 4 * 1024,
        .physically_contiguous_required = true,
        .secure_required = false,
        .ram_domain_supported = (domain == fuchsia_sysmem::wire::CoherencyDomain::kRam),
        .cpu_domain_supported = (domain == fuchsia_sysmem::wire::CoherencyDomain::kCpu),
        .inaccessible_domain_supported =
            (domain == fuchsia_sysmem::wire::CoherencyDomain::kInaccessible),
        .heap_permitted_count = 1,
        .heap_permitted = {fuchsia_sysmem::wire::HeapType::kSystemRam}};
    ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

    auto allocate_result = collection->WaitForBuffersAllocated();
    ASSERT_OK(allocate_result, "Failed Allocate(): domain supported = %u", domain);

    ASSERT_EQ(domain,
              allocate_result->buffer_collection_info.settings.buffer_settings.coherency_domain,
              "Coherency domain doesn't match constraints");
  }
}

TEST(Sysmem, RequiredSize) {
  auto collection = make_single_participant_collection();

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = false;
  constraints.image_format_constraints_count = 1;
  fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints =
      constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia_sysmem::wire::PixelFormatType::kNv12;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec709,
  };
  image_constraints.min_coded_width = 256;
  image_constraints.max_coded_width = std::numeric_limits<uint32_t>::max();
  image_constraints.min_coded_height = 256;
  image_constraints.max_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints.min_bytes_per_row = 256;
  image_constraints.max_bytes_per_row = std::numeric_limits<uint32_t>::max();
  image_constraints.max_coded_width_times_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints.layers = 1;
  image_constraints.coded_width_divisor = 1;
  image_constraints.coded_height_divisor = 1;
  image_constraints.bytes_per_row_divisor = 1;
  image_constraints.start_offset_divisor = 1;
  image_constraints.display_width_divisor = 1;
  image_constraints.display_height_divisor = 1;
  image_constraints.required_max_coded_width = 512;
  image_constraints.required_max_coded_height = 1024;

  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocate_result = collection->WaitForBuffersAllocated();
  ASSERT_OK(allocate_result);
  ASSERT_OK(allocate_result->status);

  size_t vmo_size;
  zx_status_t status =
      zx_vmo_get_size(allocate_result->buffer_collection_info.buffers[0].vmo.get(), &vmo_size);
  ASSERT_EQ(status, ZX_OK, "");

  // Image must be at least 512x1024 NV12, due to the required max sizes
  // above.
  EXPECT_LE(1024 * 512 * 3 / 2, vmo_size);
}

TEST(Sysmem, CpuUsageAndNoBufferMemoryConstraints) {
  auto allocator_1 = connect_to_sysmem_driver();
  ASSERT_OK(allocator_1);

  auto token_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_1);
  auto [token_client_1, token_server_1] = std::move(*token_endpoints_1);
  auto token_1 = fidl::BindSyncClient(std::move(token_client_1));

  ASSERT_OK(allocator_1->AllocateSharedCollection(std::move(token_server_1)));

  auto token_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_2);
  auto [token_client_2, token_server_2] = std::move(*token_endpoints_2);

  ASSERT_OK(token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_2)));

  auto collection_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_1);
  auto [collection_client_1, collection_server_1] = std::move(*collection_endpoints_1);
  auto collection_1 = fidl::BindSyncClient(std::move(collection_client_1));

  ASSERT_NE(token_1.client_end().channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator_1->BindSharedCollection(token_1.TakeClientEnd(), std::move(collection_server_1)));

  SetDefaultCollectionName(collection_1);

  // First client has CPU usage constraints but no buffer memory constraints.
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_1;
  constraints_1.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints_1.min_buffer_count_for_camping = 1;
  constraints_1.has_buffer_memory_constraints = false;

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_2;
  constraints_2.usage.display = fuchsia_sysmem::wire::kDisplayUsageLayer;
  constraints_2.min_buffer_count_for_camping = 1;
  constraints_2.has_buffer_memory_constraints = true;
  constraints_2.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 1,  // must be at least 1 else no participant has specified min size
      .max_size_bytes = 0xffffffff,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = true,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };

  ASSERT_OK(collection_1->SetConstraints(true, std::move(constraints_1)));

  auto allocator_2 = connect_to_sysmem_driver();
  ASSERT_OK(allocator_2);

  auto collection_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_2);
  auto [collection_client_2, collection_server_2] = std::move(*collection_endpoints_2);
  auto collection_2 = fidl::BindSyncClient(std::move(collection_client_2));

  ASSERT_OK(collection_1->Sync());

  ASSERT_NE(token_client_2.channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator_2->BindSharedCollection(std::move(token_client_2), std::move(collection_server_2)));

  ASSERT_OK(collection_2->SetConstraints(true, std::move(constraints_2)));

  auto allocate_result_1 = collection_1->WaitForBuffersAllocated();
  ASSERT_OK(allocate_result_1);
  ASSERT_OK(allocate_result_1->status);
  ASSERT_EQ(allocate_result_1->buffer_collection_info.settings.buffer_settings.coherency_domain,
            fuchsia_sysmem::wire::CoherencyDomain::kCpu, "");
}

TEST(Sysmem, ContiguousSystemRamIsCached) {
  auto collection = make_single_participant_collection();

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.vulkan = fuchsia_sysmem::wire::kVulkanUsageTransferDst;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = true,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      // Constraining this to SYSTEM_RAM is redundant for now.
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem::wire::HeapType::kSystemRam}};

  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocate_result = collection->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocate_result);
  ASSERT_OK(allocate_result->status);
  ASSERT_EQ(allocate_result->buffer_collection_info.buffer_count, 1, "");
  ASSERT_EQ(allocate_result->buffer_collection_info.settings.buffer_settings.coherency_domain,
            fuchsia_sysmem::wire::CoherencyDomain::kCpu, "");
  ASSERT_EQ(allocate_result->buffer_collection_info.settings.buffer_settings.heap,
            fuchsia_sysmem::wire::HeapType::kSystemRam, "");
  ASSERT_EQ(
      allocate_result->buffer_collection_info.settings.buffer_settings.is_physically_contiguous,
      true, "");

  // We could potentially map and try some non-aligned accesses, but on x64
  // that'd just work anyway IIRC, so just directly check if the cache policy
  // is cached so that non-aligned accesses will work on aarch64.
  //
  // We're intentionally only requiring this to be true in a test that
  // specifies CoherencyDomain::kCpu - intentionally don't care for
  // CoherencyDomain::kRam or CoherencyDomain::kInaccessible (when not protected).
  // CoherencyDomain::kInaccessible + protected has a separate test (
  // test_sysmem_protected_ram_is_uncached).
  zx_info_vmo_t vmo_info{};
  zx_status_t status = allocate_result->buffer_collection_info.buffers[0].vmo.get_info(
      ZX_INFO_VMO, &vmo_info, sizeof(vmo_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(vmo_info.cache_policy, ZX_CACHE_POLICY_CACHED, "");
}

TEST(Sysmem, ContiguousSystemRamIsRecycled) {
  // This needs to be larger than RAM, to know that this test is really checking if the allocations
  // are being recycled, regardless of what allocation strategy sysmem might be using.
  //
  // Unfortunately, at least under QEMU, allocating zx_system_get_physmem() * 2 takes longer than
  // the test watchdog, so instead of timing out, we early out with printf and fake "success" if
  // that happens.
  //
  // This test currently relies on timeliness/ordering of the ZX_VMO_ZERO_CHILDREN signal and
  // notification to sysmem of that signal vs. allocation of more BufferCollection(s), which to some
  // extent could be viewed as an invalid thing to depend on, but on the other hand, if those
  // mechanisms _are_ delayed too much, in practice we might have problems, so ... for now the test
  // is not ashamed to be relying on that.
  uint64_t total_bytes_to_allocate = zx_system_get_physmem() * 2;
  uint64_t total_bytes_allocated = 0;
  constexpr uint64_t kBytesToAllocatePerPass = 2 * 1024 * 1024;
  zx::time deadline_time = zx::deadline_after(zx::sec(10));
  int64_t iteration_count = 0;
  zx::time start_time = zx::clock::get_monotonic();
  while (total_bytes_allocated < total_bytes_to_allocate) {
    if (zx::clock::get_monotonic() > deadline_time) {
      // Otherwise, we'd potentially trigger the test watchdog.  So far we've only seen this happen
      // in QEMU environments.
      printf(
          "\ntest_sysmem_contiguous_system_ram_is_recycled() internal timeout - fake success - "
          "total_bytes_allocated so far: %zu\n",
          total_bytes_allocated);
      break;
    }

    auto collection = make_single_participant_collection();

    fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
    constraints.usage.vulkan = fuchsia_sysmem::wire::kVulkanUsageTransferDst;
    constraints.min_buffer_count_for_camping = 1;
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
        .min_size_bytes = kBytesToAllocatePerPass,
        .max_size_bytes = kBytesToAllocatePerPass,
        .physically_contiguous_required = true,
        .secure_required = false,
        .ram_domain_supported = false,
        .cpu_domain_supported = true,
        .inaccessible_domain_supported = false,
        // Constraining this to SYSTEM_RAM is redundant for now.
        .heap_permitted_count = 1,
        .heap_permitted = {fuchsia_sysmem::wire::HeapType::kSystemRam}};

    ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

    auto allocate_result = collection->WaitForBuffersAllocated();
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_OK(allocate_result);
    ASSERT_OK(allocate_result->status);

    ASSERT_EQ(allocate_result->buffer_collection_info.buffer_count, 1, "");
    ASSERT_EQ(allocate_result->buffer_collection_info.settings.buffer_settings.coherency_domain,
              fuchsia_sysmem::wire::CoherencyDomain::kCpu, "");
    ASSERT_EQ(allocate_result->buffer_collection_info.settings.buffer_settings.heap,
              fuchsia_sysmem::wire::HeapType::kSystemRam, "");
    ASSERT_EQ(
        allocate_result->buffer_collection_info.settings.buffer_settings.is_physically_contiguous,
        true, "");

    total_bytes_allocated += kBytesToAllocatePerPass;

    iteration_count++;

    // ~collection_client and ~buffer_collection_info should recycle the space used by the VMOs for
    // re-use so that more can be allocated.
  }
  zx::time end_time = zx::clock::get_monotonic();
  zx::duration duration_per_iteration = (end_time - start_time) / iteration_count;

  printf("duration_per_iteration: %" PRId64 "us, or %" PRId64 "ms\n",
         duration_per_iteration.to_usecs(), duration_per_iteration.to_msecs());

  if (total_bytes_allocated >= total_bytes_to_allocate) {
    printf("\ntest_sysmem_contiguous_system_ram_is_recycled() real success\n");
  }
}

TEST(Sysmem, OnlyNoneUsageFails) {
  auto collection = make_single_participant_collection();

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.none = fuchsia_sysmem::wire::kNoneUsage;
  constraints.min_buffer_count_for_camping = 3;
  constraints.min_buffer_count = 5;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 64 * 1024,
      .max_size_bytes = 128 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);
  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocate_result = collection->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  //
  // If the aggregate usage only has "none" usage, allocation should fail.
  // Because we weren't waiting at the time that allocation failed, we don't
  // necessarily get a response from the wait.
  //
  // TODO(dustingreen): Issue async client request to put the wait in flight
  // before the SetConstraints() so we can verify that the wait succeeds but
  // the allocation_status is ZX_ERR_NOT_SUPPORTED.
  ASSERT_TRUE(allocate_result.status() == ZX_ERR_PEER_CLOSED ||
              allocate_result->status == ZX_ERR_NOT_SUPPORTED);
}

TEST(Sysmem, NoneUsageAndOtherUsageFromSingleParticipantFails) {
  auto collection = make_single_participant_collection();

  const char* kClientName = "TestClient";
  collection->SetDebugClientInfo(fidl::StringView::FromExternal(kClientName), 6u);

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  // Specify both "none" and "cpu" usage from a single participant, which will
  // cause allocation failure.
  constraints.usage.none = fuchsia_sysmem::wire::kNoneUsage;
  constraints.usage.cpu = fuchsia_sysmem::wire::kCpuUsageReadOften;
  constraints.min_buffer_count_for_camping = 3;
  constraints.min_buffer_count = 5;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = 64 * 1024,
      .max_size_bytes = 128 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);
  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocate_result = collection->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  //
  // If the aggregate usage has both "none" usage and "cpu" usage from a
  // single participant, allocation should fail.
  //
  // TODO(dustingreen): Issue async client request to put the wait in flight
  // before the SetConstraints() so we can verify that the wait succeeds but
  // the allocation_status is ZX_ERR_NOT_SUPPORTED.
  ASSERT_TRUE(allocate_result.status() == ZX_ERR_PEER_CLOSED ||
              allocate_result->status == ZX_ERR_NOT_SUPPORTED);
}

TEST(Sysmem, NoneUsageWithSeparateOtherUsageSucceeds) {
  auto allocator = connect_to_sysmem_driver();

  auto token_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_1);
  auto [token_client_1, token_server_1] = std::move(*token_endpoints_1);
  auto token_1 = fidl::BindSyncClient(std::move(token_client_1));

  // Client 1 creates a token and new LogicalBufferCollection using
  // AllocateSharedCollection().
  ASSERT_OK(allocator->AllocateSharedCollection(std::move(token_server_1)));

  auto token_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_2);
  auto [token_client_2, token_server_2] = std::move(*token_endpoints_2);

  // Client 1 duplicates its token and gives the duplicate to client 2 (this
  // test is single proc, so both clients are coming from this client
  // process - normally the two clients would be in separate processes with
  // token_client_2 transferred to another participant).
  ASSERT_OK(token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_2)));

  auto collection_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_1);
  auto [collection_client_1, collection_server_1] = std::move(*collection_endpoints_1);
  auto collection_1 = fidl::BindSyncClient(std::move(collection_client_1));

  ASSERT_NE(token_1.client_end().channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator->BindSharedCollection(token_1.TakeClientEnd(), std::move(collection_server_1)));

  SetDefaultCollectionName(collection_1);

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_1;
  constraints_1.usage.none = fuchsia_sysmem::wire::kNoneUsage;
  constraints_1.min_buffer_count_for_camping = 3;
  constraints_1.has_buffer_memory_constraints = true;
  constraints_1.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      // This min_size_bytes is intentionally too small to hold the min_coded_width and
      // min_coded_height in NV12
      // format.
      .min_size_bytes = 64 * 1024,
      // Allow a max that's just large enough to accommodate the size implied
      // by the min frame size and PixelFormat.
      .max_size_bytes = (512 * 512) * 3 / 2,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };

  // Start with constraints_2 a copy of constraints_1.  There are no handles
  // in the constraints struct so a struct copy instead of clone is fine here.
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints_2(constraints_1);
  // Modify constraints_2 to set non-"none" usage.
  constraints_2.usage.none = 0;
  constraints_2.usage.vulkan = fuchsia_sysmem::wire::kVulkanUsageTransferDst;

  ASSERT_OK(collection_1->SetConstraints(true, std::move(constraints_1)));

  // Client 2 connects to sysmem separately.
  auto allocator_2 = connect_to_sysmem_driver();
  ASSERT_OK(allocator_2);

  auto collection_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints_2);
  auto [collection_client_2, collection_server_2] = std::move(*collection_endpoints_2);
  auto collection_2 = fidl::BindSyncClient(std::move(collection_client_2));

  // Just because we can, perform this sync as late as possible, just before
  // the BindSharedCollection() via allocator2_client_2.  Without this Sync(),
  // the BindSharedCollection() might arrive at the server before the
  // Duplicate() that delivered the server end of token_client_2 to sysmem,
  // which would cause sysmem to not recognize the token.
  ASSERT_OK(collection_1->Sync());

  ASSERT_NE(token_client_2.channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(
      allocator_2->BindSharedCollection(std::move(token_client_2), std::move(collection_server_2)));

  ASSERT_OK(collection_2->SetConstraints(true, std::move(constraints_2)));

  //
  // Only after both participants (both clients) have SetConstraints() will
  // the allocation be successful.
  //
  auto allocate_result_1 = collection_1->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocate_result_1);

  // Success when at least one participant specifies "none" usage and at least
  // one participant specifies a usage other than "none".
  ASSERT_OK(allocate_result_1->status);
}

TEST(Sysmem, PixelFormatBgr24) {
  constexpr uint32_t kWidth = 600;
  constexpr uint32_t kHeight = 1;
  constexpr uint32_t kStride = kWidth * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_888);
  constexpr uint32_t divisor = 32;
  constexpr uint32_t kStrideAlign = (kStride + divisor - 1) & ~(divisor - 1);

  auto collection = make_single_participant_collection();

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 3;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = kStride,
      .max_size_bytes = kStrideAlign,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = true,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem::wire::HeapType::kSystemRam}};
  constraints.image_format_constraints_count = 1;
  fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints =
      constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia_sysmem::wire::PixelFormatType::kBgr24;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kSrgb,
  };
  // The min dimensions intentionally imply a min size that's larger than
  // buffer_memory_constraints.min_size_bytes.
  image_constraints.min_coded_width = kWidth;
  image_constraints.max_coded_width = std::numeric_limits<uint32_t>::max();
  image_constraints.min_coded_height = kHeight;
  image_constraints.max_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints.min_bytes_per_row = kStride;
  image_constraints.max_bytes_per_row = std::numeric_limits<uint32_t>::max();
  image_constraints.max_coded_width_times_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints.layers = 1;
  image_constraints.coded_width_divisor = 1;
  image_constraints.coded_height_divisor = 1;
  image_constraints.bytes_per_row_divisor = divisor;
  image_constraints.start_offset_divisor = divisor;
  image_constraints.display_width_divisor = 1;
  image_constraints.display_height_divisor = 1;

  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocate_result = collection->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocate_result);
  ASSERT_OK(allocate_result->status);

  auto buffer_collection_info = &allocate_result->buffer_collection_info;
  ASSERT_EQ(buffer_collection_info->buffer_count, 3, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, kStrideAlign, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
            fuchsia_sysmem::wire::CoherencyDomain::kCpu, "");
  // We specified image_format_constraints so the result must also have
  // image_format_constraints.
  ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, true, "");

  ASSERT_EQ(buffer_collection_info->settings.image_format_constraints.pixel_format.type,
            fuchsia_sysmem::wire::PixelFormatType::kBgr24, "");

  for (uint32_t i = 0; i < 64; ++i) {
    if (i < 3) {
      ASSERT_NE(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
      uint64_t size_bytes = 0;
      auto status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo.get(), &size_bytes);
      ASSERT_EQ(status, ZX_OK, "");
      // The portion of the VMO the client can use is large enough to hold the min image size,
      // despite the min buffer size being smaller.
      ASSERT_GE(buffer_collection_info->settings.buffer_settings.size_bytes, kStrideAlign, "");
      // The vmo has room for the nominal size of the portion of the VMO the client can use.
      ASSERT_LE(buffer_collection_info->buffers[i].vmo_usable_start +
                    buffer_collection_info->settings.buffer_settings.size_bytes,
                size_bytes, "");
    } else {
      ASSERT_EQ(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
    }
  }

  EXPECT_OK(collection->Close());
}

// Test that closing a token handle that's had Close() called on it doesn't crash sysmem.
TEST(Sysmem, CloseToken) {
  auto allocator = connect_to_sysmem_driver();

  auto token_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_1);
  auto [token_client_1, token_server_1] = std::move(*token_endpoints_1);
  auto token_1 = fidl::BindSyncClient(std::move(token_client_1));

  ASSERT_OK(allocator->AllocateSharedCollection(std::move(token_server_1)));

  auto token_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints_2);
  auto [token_client_2, token_server_2] = std::move(*token_endpoints_2);
  auto token_2 = fidl::BindSyncClient(std::move(token_client_2));

  ASSERT_OK(token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_2)));

  ASSERT_OK(token_1->Sync());
  ASSERT_OK(token_1->Close());
  token_1 = {};

  // Try to ensure sysmem processes the token closure before the sync.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));

  EXPECT_OK(token_2->Sync());
}

TEST(Sysmem, HeapAmlogicSecure) {
  if (!is_board_with_amlogic_secure()) {
    return;
  }

  for (uint32_t i = 0; i < 64; ++i) {
    bool need_aux = (i % 4 == 0);
    bool allow_aux = (i % 2 == 0);
    auto collection = make_single_participant_collection();

    if (need_aux) {
      fuchsia_sysmem::wire::BufferCollectionConstraintsAuxBuffers aux_constraints;
      aux_constraints.need_clear_aux_buffers_for_secure = true;
      aux_constraints.allow_clear_aux_buffers_for_secure = true;
      ASSERT_OK(collection->SetConstraintsAuxBuffers(std::move(aux_constraints)));
    }

    fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
    constraints.usage.video = fuchsia_sysmem::wire::kVideoUsageHwDecoder;
    constexpr uint32_t kBufferCount = 4;
    constraints.min_buffer_count_for_camping = kBufferCount;
    constraints.has_buffer_memory_constraints = true;
    constexpr uint32_t kBufferSizeBytes = 64 * 1024;
    constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
        .min_size_bytes = kBufferSizeBytes,
        .max_size_bytes = 128 * 1024,
        .physically_contiguous_required = true,
        .secure_required = true,
        .ram_domain_supported = false,
        .cpu_domain_supported = false,
        .inaccessible_domain_supported = true,
        .heap_permitted_count = 1,
        .heap_permitted = {fuchsia_sysmem::wire::HeapType::kAmlogicSecure},
    };
    ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);

    ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

    auto allocate_result = collection->WaitForBuffersAllocated();
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_OK(allocate_result);
    ASSERT_OK(allocate_result->status);
    auto buffer_collection_info = &allocate_result->buffer_collection_info;
    EXPECT_EQ(buffer_collection_info->buffer_count, kBufferCount, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, kBufferSizeBytes, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, true, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, true, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
              fuchsia_sysmem::wire::CoherencyDomain::kInaccessible, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.heap,
              fuchsia_sysmem::wire::HeapType::kAmlogicSecure, "");
    EXPECT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");

    fuchsia_sysmem::wire::BufferCollectionInfo2 aux_buffer_collection_info;
    if (need_aux || allow_aux) {
      auto aux_result = collection->GetAuxBuffers();
      ASSERT_OK(aux_result);
      ASSERT_OK(aux_result->status);

      EXPECT_EQ(aux_result->buffer_collection_info_aux_buffers.buffer_count,
                buffer_collection_info->buffer_count, "");
      aux_buffer_collection_info = std::move(aux_result->buffer_collection_info_aux_buffers);
    }

    for (uint32_t i = 0; i < 64; ++i) {
      if (i < kBufferCount) {
        EXPECT_NE(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
        uint64_t size_bytes = 0;
        auto status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo.get(), &size_bytes);
        ASSERT_EQ(status, ZX_OK, "");
        EXPECT_EQ(size_bytes, kBufferSizeBytes, "");
        if (need_aux) {
          EXPECT_NE(aux_buffer_collection_info.buffers[i].vmo, ZX_HANDLE_INVALID, "");
          uint64_t aux_size_bytes = 0;
          status =
              zx_vmo_get_size(aux_buffer_collection_info.buffers[i].vmo.get(), &aux_size_bytes);
          ASSERT_EQ(status, ZX_OK, "");
          EXPECT_EQ(aux_size_bytes, kBufferSizeBytes, "");
        } else if (allow_aux) {
          // This is how v1 indicates that aux buffers weren't allocated.
          EXPECT_EQ(aux_buffer_collection_info.buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
        }
      } else {
        EXPECT_EQ(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
        if (need_aux) {
          EXPECT_EQ(aux_buffer_collection_info.buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
        }
      }
    }

    zx::vmo the_vmo = std::move(buffer_collection_info->buffers[0].vmo);
    buffer_collection_info->buffers[0].vmo = zx::vmo();
    SecureVmoReadTester tester(std::move(the_vmo));
    ASSERT_DEATH(([&] { tester.AttemptReadFromSecure(); }));
    ASSERT_FALSE(tester.IsReadFromSecureAThing());

    if (need_aux) {
      zx::vmo aux_vmo = std::move(aux_buffer_collection_info.buffers[0].vmo);
      aux_buffer_collection_info.buffers[0].vmo = zx::vmo();
      SecureVmoReadTester aux_tester(std::move(aux_vmo));
      // This shouldn't crash for the aux VMO.
      aux_tester.AttemptReadFromSecure(/*expect_read_success=*/true);
      // Read from aux VMO using REE (rich execution environment, in contrast to the TEE (trusted
      // execution environment)) CPU should work.  In actual usage, only the non-encrypted parts of
      // the data will be present in the VMO, and the encrypted parts will be all 0xFF.  The point
      // of the aux VMO is to allow reading and parsing the non-encrypted parts using REE CPU.
      ASSERT_TRUE(aux_tester.IsReadFromSecureAThing());
    }
  }
}

TEST(Sysmem, HeapAmlogicSecureMiniStress) {
  if (!is_board_with_amlogic_secure()) {
    return;
  }

  // 256 64 KiB chunks, and well below protected_memory_size, even accounting for fragmentation.
  const uint32_t kBlockSize = 64 * 1024;
  const uint32_t kTotalSizeThreshold = 256 * kBlockSize;

  // For generating different sequences of random ranges, but still being able to easily repro any
  // failure by putting the uint64_t seed inside the {} here.
  static constexpr std::optional<uint64_t> kForcedSeed{};
  std::random_device random_device;
  std::uint_fast64_t seed{kForcedSeed ? *kForcedSeed : random_device()};
  std::mt19937_64 prng{seed};
  std::uniform_int_distribution<uint32_t> op_distribution(0, 104);
  std::uniform_int_distribution<uint32_t> key_distribution(0, std::numeric_limits<uint32_t>::max());
  // Buffers aren't required to be block aligned.
  const uint32_t max_buffer_size = 4 * kBlockSize;
  std::uniform_int_distribution<uint32_t> size_distribution(0, max_buffer_size);

  struct Vmo {
    uint32_t size;
    zx::vmo vmo;
  };
  using BufferMap = std::multimap<uint32_t, Vmo>;
  BufferMap buffers;
  // random enough for this test; buffers at the end of a big gap are more likely to be selected,
  // but that's fine since which buffers those are is also random due to random key when the buffer
  // is added; still, not claiming this is rigorously random
  auto get_random_buffer = [&key_distribution, &prng, &buffers]() -> BufferMap::iterator {
    if (buffers.empty()) {
      return buffers.end();
    }
    uint32_t key = key_distribution(prng);
    auto iter = buffers.upper_bound(key);
    if (iter == buffers.end()) {
      iter = buffers.begin();
    }
    return iter;
  };

  uint32_t total_size = 0;
  auto add = [&key_distribution, &size_distribution, &prng, &total_size, &buffers] {
    uint32_t size = fbl::round_up(size_distribution(prng), zx_system_get_page_size());
    total_size += size;

    auto collection = make_single_participant_collection();
    fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
    constraints.usage.video = fuchsia_sysmem::wire::kVideoUsageHwDecoder;
    constexpr uint32_t kBufferCount = 1;
    constraints.min_buffer_count_for_camping = 1;
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
        .min_size_bytes = size,
        .max_size_bytes = size,
        .physically_contiguous_required = true,
        .secure_required = true,
        .ram_domain_supported = false,
        .cpu_domain_supported = false,
        .inaccessible_domain_supported = true,
        .heap_permitted_count = 1,
        .heap_permitted = {fuchsia_sysmem::wire::HeapType::kAmlogicSecure},
    };
    ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);

    ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    auto allocate_result = collection->WaitForBuffersAllocated();

    ASSERT_OK(allocate_result);
    ASSERT_OK(allocate_result->status);
    auto buffer_collection_info = &allocate_result->buffer_collection_info;
    EXPECT_EQ(buffer_collection_info->buffer_count, kBufferCount, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, size, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, true, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, true, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
              fuchsia_sysmem::wire::CoherencyDomain::kInaccessible, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.heap,
              fuchsia_sysmem::wire::HeapType::kAmlogicSecure, "");
    EXPECT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");
    EXPECT_EQ(buffer_collection_info->buffers[0].vmo_usable_start, 0);

    zx::vmo buffer = std::move(buffer_collection_info->buffers[0].vmo);

    buffers.emplace(
        std::make_pair(key_distribution(prng), Vmo{.size = size, .vmo = std::move(buffer)}));
  };
  auto remove = [&get_random_buffer, &buffers, &total_size] {
    auto random_buffer = get_random_buffer();
    ZX_ASSERT(random_buffer != buffers.end());
    total_size -= random_buffer->second.size;
    buffers.erase(random_buffer);
  };
  auto check_random_buffer_page = [&get_random_buffer] {
    auto random_buffer_iter = get_random_buffer();
    SecureVmoReadTester tester(zx::unowned_vmo(random_buffer_iter->second.vmo));
    ASSERT_DEATH(([&] { tester.AttemptReadFromSecure(); }));
    ASSERT_FALSE(tester.IsReadFromSecureAThing());
  };

  const uint32_t kIterations = 1000;
  for (uint32_t i = 0; i < kIterations; ++i) {
    if (i % 100 == 0) {
      printf("iteration: %u\n", i);
    }
    while (total_size > kTotalSizeThreshold) {
      remove();
    }
    uint32_t roll = op_distribution(prng);
    switch (roll) {
      // add
      case 0 ... 48:
        add();
        break;
      // fill
      case 49:
        while (total_size < kTotalSizeThreshold) {
          add();
        }
        break;
      // remove
      case 50 ... 98:
        if (total_size) {
          remove();
        }
        break;
      // clear
      case 99:
        while (total_size) {
          remove();
        }
        break;
      case 100 ... 104:
        if (total_size) {
          check_random_buffer_page();
        }
        break;
    }
  }

  for (uint32_t i = 0; i < 64; ++i) {
    bool need_aux = (i % 4 == 0);
    bool allow_aux = (i % 2 == 0);
    auto collection = make_single_participant_collection();

    if (need_aux) {
      fuchsia_sysmem::wire::BufferCollectionConstraintsAuxBuffers aux_constraints;
      aux_constraints.need_clear_aux_buffers_for_secure = true;
      aux_constraints.allow_clear_aux_buffers_for_secure = true;
      ASSERT_OK(collection->SetConstraintsAuxBuffers(std::move(aux_constraints)));
    }

    fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
    constraints.usage.video = fuchsia_sysmem::wire::kVideoUsageHwDecoder;
    constexpr uint32_t kBufferCount = 4;
    constraints.min_buffer_count_for_camping = kBufferCount;
    constraints.has_buffer_memory_constraints = true;
    constexpr uint32_t kBufferSizeBytes = 64 * 1024;
    constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
        .min_size_bytes = kBufferSizeBytes,
        .max_size_bytes = 128 * 1024,
        .physically_contiguous_required = true,
        .secure_required = true,
        .ram_domain_supported = false,
        .cpu_domain_supported = false,
        .inaccessible_domain_supported = true,
        .heap_permitted_count = 1,
        .heap_permitted = {fuchsia_sysmem::wire::HeapType::kAmlogicSecure},
    };
    ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);

    ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

    auto allocate_result = collection->WaitForBuffersAllocated();
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_OK(allocate_result);
    ASSERT_OK(allocate_result->status);
    auto buffer_collection_info = &allocate_result->buffer_collection_info;
    EXPECT_EQ(buffer_collection_info->buffer_count, kBufferCount, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, kBufferSizeBytes, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, true, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, true, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
              fuchsia_sysmem::wire::CoherencyDomain::kInaccessible, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.heap,
              fuchsia_sysmem::wire::HeapType::kAmlogicSecure, "");
    EXPECT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");

    fuchsia_sysmem::wire::BufferCollectionInfo2 aux_buffer_collection_info;
    if (need_aux || allow_aux) {
      auto aux_result = collection->GetAuxBuffers();
      ASSERT_OK(aux_result);
      ASSERT_OK(aux_result->status);

      EXPECT_EQ(aux_result->buffer_collection_info_aux_buffers.buffer_count,
                buffer_collection_info->buffer_count, "");
      aux_buffer_collection_info = std::move(aux_result->buffer_collection_info_aux_buffers);
    }

    for (uint32_t i = 0; i < 64; ++i) {
      if (i < kBufferCount) {
        EXPECT_NE(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
        uint64_t size_bytes = 0;
        auto status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo.get(), &size_bytes);
        ASSERT_EQ(status, ZX_OK, "");
        EXPECT_EQ(size_bytes, kBufferSizeBytes, "");
        if (need_aux) {
          EXPECT_NE(aux_buffer_collection_info.buffers[i].vmo, ZX_HANDLE_INVALID, "");
          uint64_t aux_size_bytes = 0;
          status =
              zx_vmo_get_size(aux_buffer_collection_info.buffers[i].vmo.get(), &aux_size_bytes);
          ASSERT_EQ(status, ZX_OK, "");
          EXPECT_EQ(aux_size_bytes, kBufferSizeBytes, "");
        } else if (allow_aux) {
          // This is how v1 indicates that aux buffers weren't allocated.
          EXPECT_EQ(aux_buffer_collection_info.buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
        }
      } else {
        EXPECT_EQ(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
        if (need_aux) {
          EXPECT_EQ(aux_buffer_collection_info.buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
        }
      }
    }

    zx::vmo the_vmo = std::move(buffer_collection_info->buffers[0].vmo);
    buffer_collection_info->buffers[0].vmo = zx::vmo();
    SecureVmoReadTester tester(std::move(the_vmo));
    ASSERT_DEATH(([&] { tester.AttemptReadFromSecure(); }));
    ASSERT_FALSE(tester.IsReadFromSecureAThing());

    if (need_aux) {
      zx::vmo aux_vmo = std::move(aux_buffer_collection_info.buffers[0].vmo);
      aux_buffer_collection_info.buffers[0].vmo = zx::vmo();
      SecureVmoReadTester aux_tester(std::move(aux_vmo));
      // This shouldn't crash for the aux VMO.
      aux_tester.AttemptReadFromSecure(/*expect_read_success=*/true);
      // Read from aux VMO using REE (rich execution environment, in contrast to the TEE (trusted
      // execution environment)) CPU should work.  In actual usage, only the non-encrypted parts of
      // the data will be present in the VMO, and the encrypted parts will be all 0xFF.  The point
      // of the aux VMO is to allow reading and parsing the non-encrypted parts using REE CPU.
      ASSERT_TRUE(aux_tester.IsReadFromSecureAThing());
    }
  }
}

TEST(Sysmem, HeapAmlogicSecureOnlySupportsInaccessible) {
  if (!is_board_with_amlogic_secure()) {
    return;
  }

  fuchsia_sysmem::wire::CoherencyDomain domains[] = {
      fuchsia_sysmem::wire::CoherencyDomain::kCpu,
      fuchsia_sysmem::wire::CoherencyDomain::kRam,
      fuchsia_sysmem::wire::CoherencyDomain::kInaccessible,
  };

  for (const fuchsia_sysmem::wire::CoherencyDomain domain : domains) {
    auto collection = make_single_participant_collection();

    fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
    constraints.usage.video = fuchsia_sysmem::wire::kVideoUsageHwDecoder;
    constexpr uint32_t kBufferCount = 4;
    constraints.min_buffer_count_for_camping = kBufferCount;
    constraints.has_buffer_memory_constraints = true;
    constexpr uint32_t kBufferSizeBytes = 64 * 1024;
    constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
        .min_size_bytes = kBufferSizeBytes,
        .max_size_bytes = 128 * 1024,
        .physically_contiguous_required = true,
        .secure_required = true,
        .ram_domain_supported = (domain == fuchsia_sysmem::wire::CoherencyDomain::kRam),
        .cpu_domain_supported = (domain == fuchsia_sysmem::wire::CoherencyDomain::kCpu),
        .inaccessible_domain_supported =
            (domain == fuchsia_sysmem::wire::CoherencyDomain::kInaccessible),
        .heap_permitted_count = 1,
        .heap_permitted = {fuchsia_sysmem::wire::HeapType::kAmlogicSecure},
    };
    ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);
    ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

    auto allocate_result = collection->WaitForBuffersAllocated();

    if (domain == fuchsia_sysmem::wire::CoherencyDomain::kInaccessible) {
      // This is the first round-trip to/from sysmem.  A failure here can be due
      // to any step above failing async.
      ASSERT_OK(allocate_result);
      ASSERT_OK(allocate_result->status);
      auto buffer_collection_info = &allocate_result->buffer_collection_info;

      EXPECT_EQ(buffer_collection_info->buffer_count, kBufferCount, "");
      EXPECT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, kBufferSizeBytes, "");
      EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, true,
                "");
      EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, true, "");
      EXPECT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
                fuchsia_sysmem::wire::CoherencyDomain::kInaccessible, "");
      EXPECT_EQ(buffer_collection_info->settings.buffer_settings.heap,
                fuchsia_sysmem::wire::HeapType::kAmlogicSecure, "");
      EXPECT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");
    } else {
      ASSERT_TRUE(allocate_result.status() != ZX_OK || allocate_result->status != ZX_OK,
                  "Sysmem should not allocate memory from secure heap with coherency domain %u",
                  domain);
    }
  }
}

TEST(Sysmem, HeapAmlogicSecureVdec) {
  if (!is_board_with_amlogic_secure_vdec()) {
    return;
  }

  for (uint32_t i = 0; i < 64; ++i) {
    auto collection = make_single_participant_collection();

    fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
    constraints.usage.video = fuchsia_sysmem::wire::kVideoUsageDecryptorOutput |
                              fuchsia_sysmem::wire::kVideoUsageHwDecoder;
    constexpr uint32_t kBufferCount = 4;
    constraints.min_buffer_count_for_camping = kBufferCount;
    constraints.has_buffer_memory_constraints = true;
    constexpr uint32_t kBufferSizeBytes = 64 * 1024 - 1;
    constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
        .min_size_bytes = kBufferSizeBytes,
        .max_size_bytes = 128 * 1024,
        .physically_contiguous_required = true,
        .secure_required = true,
        .ram_domain_supported = false,
        .cpu_domain_supported = false,
        .inaccessible_domain_supported = true,
        .heap_permitted_count = 1,
        .heap_permitted = {fuchsia_sysmem::wire::HeapType::kAmlogicSecureVdec},
    };
    ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);
    ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

    auto allocate_result = collection->WaitForBuffersAllocated();
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_OK(allocate_result);
    ASSERT_OK(allocate_result->status);
    auto buffer_collection_info = &allocate_result->buffer_collection_info;

    EXPECT_EQ(buffer_collection_info->buffer_count, kBufferCount, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, kBufferSizeBytes, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, true, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, true, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
              fuchsia_sysmem::wire::CoherencyDomain::kInaccessible, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.heap,
              fuchsia_sysmem::wire::HeapType::kAmlogicSecureVdec, "");
    EXPECT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");

    auto expected_size = fbl::round_up(kBufferSizeBytes, zx_system_get_page_size());
    for (uint32_t i = 0; i < 64; ++i) {
      if (i < kBufferCount) {
        EXPECT_NE(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
        uint64_t size_bytes = 0;
        auto status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo.get(), &size_bytes);
        ASSERT_EQ(status, ZX_OK, "");
        EXPECT_EQ(size_bytes, expected_size, "");
      } else {
        EXPECT_EQ(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
      }
    }

    zx::vmo the_vmo = std::move(buffer_collection_info->buffers[0].vmo);
    buffer_collection_info->buffers[0].vmo = zx::vmo();
    SecureVmoReadTester tester(std::move(the_vmo));
    ASSERT_DEATH(([&] { tester.AttemptReadFromSecure(); }));
    ASSERT_FALSE(tester.IsReadFromSecureAThing());
  }
}

TEST(Sysmem, CpuUsageAndInaccessibleDomainSupportedSucceeds) {
  auto collection = make_single_participant_collection();

  constexpr uint32_t kBufferCount = 3;
  constexpr uint32_t kBufferSize = 64 * 1024;
  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = kBufferCount;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      .min_size_bytes = kBufferSize,
      .max_size_bytes = 128 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);
  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocate_result = collection->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocate_result);
  ASSERT_OK(allocate_result->status);
  auto buffer_collection_info = &allocate_result->buffer_collection_info;

  ASSERT_EQ(buffer_collection_info->buffer_count, kBufferCount, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, kBufferSize, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
            fuchsia_sysmem::wire::CoherencyDomain::kCpu, "");
  ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");

  for (uint32_t i = 0; i < 64; ++i) {
    if (i < kBufferCount) {
      ASSERT_NE(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
      uint64_t size_bytes = 0;
      auto status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo.get(), &size_bytes);
      ASSERT_EQ(status, ZX_OK, "");
      ASSERT_EQ(size_bytes, kBufferSize, "");
    } else {
      ASSERT_EQ(buffer_collection_info->buffers[i].vmo.get(), ZX_HANDLE_INVALID, "");
    }
  }
}

TEST(Sysmem, AllocatedBufferZeroInRam) {
  constexpr uint32_t kBufferCount = 1;
  // Since we're reading from buffer start to buffer end, let's not allocate too large a buffer,
  // since perhaps that'd hide problems if the cache flush is missing in sysmem.
  constexpr uint32_t kBufferSize = 64 * 1024;
  constexpr uint32_t kIterationCount = 200;

  auto zero_buffer = std::make_unique<uint8_t[]>(kBufferSize);
  ZX_ASSERT(zero_buffer);
  auto tmp_buffer = std::make_unique<uint8_t[]>(kBufferSize);
  ZX_ASSERT(tmp_buffer);
  for (uint32_t iter = 0; iter < kIterationCount; ++iter) {
    auto collection = make_single_participant_collection();

    fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
    constraints.usage.cpu =
        fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
    constraints.min_buffer_count_for_camping = kBufferCount;
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
        .min_size_bytes = kBufferSize,
        .max_size_bytes = kBufferSize,
        .physically_contiguous_required = false,
        .secure_required = false,
        .ram_domain_supported = false,
        .cpu_domain_supported = true,
        .inaccessible_domain_supported = false,
        .heap_permitted_count = 0,
        .heap_permitted = {},
    };
    ZX_DEBUG_ASSERT(constraints.image_format_constraints_count == 0);
    ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

    auto allocate_result = collection->WaitForBuffersAllocated();
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_OK(allocate_result);
    ASSERT_OK(allocate_result->status);

    // We intentionally don't check a bunch of stuff here.  We assume that sysmem allocated
    // kBufferCount (1) buffer of kBufferSize (64 KiB).  That way we're comparing ASAP after buffer
    // allocation, in case that helps catch any failure to actually zero in RAM.  Ideally we'd read
    // using a DMA in this test instead of using CPU reads, but that wouldn't be a portable test.
    auto buffer_collection_info = &allocate_result->buffer_collection_info;
    zx::vmo vmo = std::move(buffer_collection_info->buffers[0].vmo);
    buffer_collection_info->buffers[0].vmo = zx::vmo();

    // Before we read from the VMO, we need to invalidate cache for the VMO.  We do this via a
    // syscall since it seems like mapping would have a greater chance of doing a fence.
    // Unfortunately none of these steps are guaranteed not to hide a problem with flushing or fence
    // in sysmem...
    auto status =
        vmo.op_range(ZX_VMO_OP_CACHE_INVALIDATE, /*offset=*/0, kBufferSize, /*buffer=*/nullptr,
                     /*buffer_size=*/0);
    ASSERT_EQ(status, ZX_OK);

    // Read using a syscall instead of mapping, just in case mapping would do a bigger fence.
    status = vmo.read(tmp_buffer.get(), 0, kBufferSize);
    ASSERT_EQ(status, ZX_OK);

    // Any non-zero bytes could be a problem with sysmem's zeroing, or cache flushing, or fencing of
    // the flush (depending on whether a given architecture is willing to cancel a cache line flush
    // on later cache line invalidate, which would seem at least somewhat questionable, and may not
    // be a thing).  This not catching a problem doesn't mean there are no problems, so that's why
    // we loop kIterationCount times to see if we can detect a problem.
    EXPECT_EQ(0, memcmp(zero_buffer.get(), tmp_buffer.get(), kBufferSize));

    // These should be noticed by sysmem before we've allocated enough space in the loop to cause
    // any trouble allocating:
    // ~vmo
    // ~collection_client
  }
}

// Test that most image format constraints don't need to be specified.
TEST(Sysmem, DefaultAttributes) {
  auto collection = make_single_participant_collection();

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = false;
  constraints.image_format_constraints_count = 1;
  fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints =
      constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia_sysmem::wire::PixelFormatType::kNv12;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec709,
  };
  image_constraints.required_max_coded_width = 512;
  image_constraints.required_max_coded_height = 1024;

  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocate_result = collection->WaitForBuffersAllocated();
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_OK(allocate_result);
  ASSERT_OK(allocate_result->status);
  auto buffer_collection_info = &allocate_result->buffer_collection_info;

  size_t vmo_size;
  auto status = zx_vmo_get_size(buffer_collection_info->buffers[0].vmo.get(), &vmo_size);
  ASSERT_EQ(status, ZX_OK, "");

  // Image must be at least 512x1024 NV12, due to the required max sizes
  // above.
  EXPECT_LE(512 * 1024 * 3 / 2, vmo_size);
}

// Check that the server validates how many image format constraints there are.
TEST(Sysmem, TooManyFormats) {
  auto allocator = connect_to_sysmem_driver();
  ASSERT_OK(allocator);

  zx::status token_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
  ASSERT_OK(token_endpoints);
  auto [token_client_end, token_server_end] = std::move(*token_endpoints);

  ASSERT_OK(allocator->AllocateSharedCollection(std::move(token_server_end)));

  zx::status collection_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints);
  auto [collection_client_end, collection_server_end] = std::move(*collection_endpoints);

  EXPECT_NE(token_client_end.channel().get(), ZX_HANDLE_INVALID, "");
  ASSERT_OK(allocator->BindSharedCollection(std::move(token_client_end),
                                            std::move(collection_server_end)));

  auto collection = fidl::BindSyncClient(std::move(collection_client_end));

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = false;
  constraints.image_format_constraints_count = 100;
  for (uint32_t i = 0; i < 32; i++) {
    fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints =
        constraints.image_format_constraints[i];
    image_constraints.pixel_format.type = fuchsia_sysmem::wire::PixelFormatType::kNv12;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value =
        fuchsia_sysmem::wire::kFormatModifierLinear;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0] = fuchsia_sysmem::wire::ColorSpace{
        .type = fuchsia_sysmem::wire::ColorSpaceType::kRec709,
    };
    image_constraints.required_max_coded_width = 512;
    image_constraints.required_max_coded_height = 1024;
  }

  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocate_result = collection->WaitForBuffersAllocated();
  EXPECT_NE(allocate_result.status(), ZX_OK, "");

  verify_connectivity(*allocator);
}

// Check that the server checks for min_buffer_count too large.
TEST(Sysmem, TooManyBuffers) {
  auto allocator = connect_to_sysmem_driver();

  zx::status collection_endpoints = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
  ASSERT_OK(collection_endpoints);
  auto [collection_client_end, collection_server_end] = std::move(*collection_endpoints);
  auto collection = fidl::BindSyncClient(std::move(collection_client_end));

  ASSERT_OK(allocator->AllocateNonSharedCollection(std::move(collection_server_end)));

  SetDefaultCollectionName(collection);

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.min_size_bytes = zx_system_get_page_size();
  constraints.min_buffer_count =
      1024 * 1024 * 1024 / constraints.buffer_memory_constraints.min_size_bytes;
  constraints.buffer_memory_constraints.cpu_domain_supported = true;
  constraints.usage.cpu = fuchsia_sysmem::wire::kCpuUsageRead;
  ASSERT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocation_result = collection->WaitForBuffersAllocated();
  EXPECT_NE(allocation_result.status(), ZX_OK, "");

  verify_connectivity(*allocator);
}

bool BasicAllocationSucceeds(
    fit::function<void(fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify)>
        modify_constraints) {
  auto collection = make_single_participant_collection();

  fuchsia_sysmem::wire::BufferCollectionConstraints constraints;
  constraints.usage.cpu =
      fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 3;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
      // This min_size_bytes is intentionally too small to hold the min_coded_width and
      // min_coded_height in NV12
      // format.
      .min_size_bytes = 64 * 1024,
      .max_size_bytes = 128 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  constraints.image_format_constraints_count = 1;
  fuchsia_sysmem::wire::ImageFormatConstraints& image_constraints =
      constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia_sysmem::wire::PixelFormatType::kNv12;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] = fuchsia_sysmem::wire::ColorSpace{
      .type = fuchsia_sysmem::wire::ColorSpaceType::kRec709,
  };
  // The min dimensions intentionally imply a min size that's larger than
  // buffer_memory_constraints.min_size_bytes.
  image_constraints.min_coded_width = 256;
  image_constraints.max_coded_width = std::numeric_limits<uint32_t>::max();
  image_constraints.min_coded_height = 256;
  image_constraints.max_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints.min_bytes_per_row = 256;
  image_constraints.max_bytes_per_row = std::numeric_limits<uint32_t>::max();
  image_constraints.max_coded_width_times_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints.layers = 1;
  image_constraints.coded_width_divisor = 2;
  image_constraints.coded_height_divisor = 2;
  image_constraints.bytes_per_row_divisor = 2;
  image_constraints.start_offset_divisor = 2;
  image_constraints.display_width_divisor = 1;
  image_constraints.display_height_divisor = 1;

  modify_constraints(constraints);
  EXPECT_OK(collection->SetConstraints(true, std::move(constraints)));

  auto allocate_result = collection->WaitForBuffersAllocated();

  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  if (allocate_result.status() != ZX_OK) {
    printf("WaitForBuffersAllocated failed - status: %d\n", allocate_result.status());
    return false;
  }

  if (allocate_result->status != ZX_OK) {
    printf("WaitForBuffersAllocated failed - allocation_status: %d\n", allocate_result->status);
    return false;
  }

  // Check if the contents in allocated VMOs are already filled with zero.
  // If the allocated VMO is readable, then we would expect it could be cleared by sysmem;
  // otherwise we just skip this check.
  auto buffer_collection_info = &allocate_result->buffer_collection_info;

  zx::vmo allocated_vmo = std::move(buffer_collection_info->buffers[0].vmo);
  size_t vmo_size;
  auto status = allocated_vmo.get_size(&vmo_size);
  if (status != ZX_OK) {
    printf("ERROR: Cannot get size of allocated_vmo: %d\n", status);
    return false;
  }

  size_t size_bytes = std::min(
      vmo_size, static_cast<size_t>(buffer_collection_info->settings.buffer_settings.size_bytes));
  std::vector<uint8_t> bytes(size_bytes, 0xff);

  status = allocated_vmo.read(bytes.data(), 0u, size_bytes);
  if (status == ZX_ERR_NOT_SUPPORTED) {
    // If the allocated VMO is not readable, then we just skip value check,
    // since we do not expect it being cleared by write syscalls.
    printf("INFO: allocated_vmo doesn't support zx_vmo_read, skip value check\n");
    return true;
  }

  // Check if all the contents we read from the VMO are filled with zero.
  return *std::max_element(bytes.begin(), bytes.end()) == 0u;
}

TEST(Sysmem, BasicAllocationSucceeds) {
  EXPECT_TRUE(BasicAllocationSucceeds(
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify_nop) {}));
}

TEST(Sysmem, ZeroMinSizeBytesFails) {
  EXPECT_FALSE(
      BasicAllocationSucceeds([](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        // Disable image_format_constraints so that the client is not specifying any min size via
        // implied by image_format_constraints.
        to_modify.image_format_constraints_count = 0;
        // Also set 0 min_size_bytes, so that implied minimum overall size is 0.
        to_modify.buffer_memory_constraints.min_size_bytes = 0;
      }));
}

TEST(Sysmem, ZeroMaxBufferCount_SucceedsOnlyForNow) {
  // With sysmem2 this will be expected to fail.  With sysmem(1), this succeeds because 0 is
  // interpreted as replace with default.
  EXPECT_TRUE(
      BasicAllocationSucceeds([](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.max_buffer_count = 0;
      }));
}

TEST(Sysmem, ZeroRequiredMinCodedWidth_SucceedsOnlyForNow) {
  // With sysmem2 this will be expected to fail.  With sysmem(1), this succeeds because 0 is
  // interpreted as replace with default.
  EXPECT_TRUE(
      BasicAllocationSucceeds([](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.image_format_constraints[0].required_min_coded_width = 0;
      }));
}

TEST(Sysmem, ZeroRequiredMinCodedHeight_SucceedsOnlyForNow) {
  // With sysmem2 this will be expected to fail.  With sysmem(1), this succeeds because 0 is
  // interpreted as replace with default.
  EXPECT_TRUE(
      BasicAllocationSucceeds([](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.image_format_constraints[0].required_min_coded_height = 0;
      }));
}

TEST(Sysmem, ZeroRequiredMinBytesPerRow_SucceedsOnlyForNow) {
  // With sysmem2 this will be expected to fail.  With sysmem(1), this succeeds because 0 is
  // interpreted as replace with default.
  EXPECT_TRUE(
      BasicAllocationSucceeds([](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.image_format_constraints[0].required_min_bytes_per_row = 0;
      }));
}

TEST(Sysmem, DuplicateConstraintsFails) {
  EXPECT_FALSE(
      BasicAllocationSucceeds([](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.image_format_constraints_count = 2;
        to_modify.image_format_constraints[1] = to_modify.image_format_constraints[0];
      }));
}

TEST(Sysmem, AttachToken_BeforeAllocate_Success) {
  EXPECT_TRUE(AttachTokenSucceeds(
      true, false, [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionInfo2& to_verify) {}));
  IF_FAILURES_RETURN();
}

TEST(Sysmem, AttachToken_AfterAllocate_Success) {
  EXPECT_TRUE(AttachTokenSucceeds(
      false, false, [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionInfo2& to_verify) {}));
}

TEST(Sysmem, AttachToken_BeforeAllocate_AttachedFailedEarly_Failure) {
  // Despite the attached token failing early, this still verifies that the non-attached tokens
  // are still ok and the LogicalBufferCollection is still ok.
  EXPECT_FALSE(AttachTokenSucceeds(
      true, true, [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionInfo2& to_verify) {}));
}

TEST(Sysmem, AttachToken_BeforeAllocate_Failure_BufferSizes) {
  EXPECT_FALSE(AttachTokenSucceeds(
      true, false, [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.buffer_memory_constraints.max_size_bytes = (512 * 512) * 3 / 2 - 1;
      },
      [](fuchsia_sysmem::wire::BufferCollectionInfo2& to_verify) {}));
}

TEST(Sysmem, AttachToken_AfterAllocate_Failure_BufferSizes) {
  EXPECT_FALSE(AttachTokenSucceeds(
      false, false, [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.buffer_memory_constraints.max_size_bytes = (512 * 512) * 3 / 2 - 1;
      },
      [](fuchsia_sysmem::wire::BufferCollectionInfo2& to_verify) {}));
}

TEST(Sysmem, AttachToken_BeforeAllocate_Success_BufferCounts) {
  const uint32_t kAttachTokenBufferCount = 2;
  uint32_t buffers_needed = 0;
  EXPECT_TRUE(AttachTokenSucceeds(
      true, false,
      [&buffers_needed](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        // 3
        buffers_needed += to_modify.min_buffer_count_for_camping;
      },
      [&buffers_needed](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        // 3
        buffers_needed += to_modify.min_buffer_count_for_camping;
        // 8
        to_modify.min_buffer_count = buffers_needed + kAttachTokenBufferCount;
      },
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        // 2
        to_modify.min_buffer_count_for_camping = kAttachTokenBufferCount;
      },
      [](fuchsia_sysmem::wire::BufferCollectionInfo2& to_verify) {},
      8  // max(8, 3 + 3 + 2)
      ));
}

TEST(Sysmem, AttachToken_AfterAllocate_Success_BufferCounts) {
  const uint32_t kAttachTokenBufferCount = 2;
  uint32_t buffers_needed = 0;
  EXPECT_TRUE(AttachTokenSucceeds(
      false, false,
      [&buffers_needed](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        // 3
        buffers_needed += to_modify.min_buffer_count_for_camping;
      },
      [&buffers_needed](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        // 3
        buffers_needed += to_modify.min_buffer_count_for_camping;
        // 8
        to_modify.min_buffer_count = buffers_needed + kAttachTokenBufferCount;
      },
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        // 2
        to_modify.min_buffer_count_for_camping = kAttachTokenBufferCount;
      },
      [](fuchsia_sysmem::wire::BufferCollectionInfo2& to_verify) {},
      8  // max(8, 3 + 3)
      ));
}

TEST(Sysmem, AttachToken_BeforeAllocate_Failure_BufferCounts) {
  EXPECT_FALSE(AttachTokenSucceeds(
      true, false, [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.min_buffer_count_for_camping = 1;
      },
      [](fuchsia_sysmem::wire::BufferCollectionInfo2& to_verify) {},
      // Only 6 get allocated, despite AttachToken() before allocation, because we intentionally
      // want AttachToken() before vs. after initial allocation to behave as close to the same as
      // possible.
      6));
}

TEST(Sysmem, AttachToken_AfterAllocate_Failure_BufferCounts) {
  EXPECT_FALSE(AttachTokenSucceeds(
      false, false, [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {},
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.min_buffer_count_for_camping = 1;
      },
      [](fuchsia_sysmem::wire::BufferCollectionInfo2& to_verify) {},
      // Only 6 get allocated at first, then AttachToken() sequence started after initial allocation
      // fails (it would have failed even if it had started before initial allocation though).
      6));
}

TEST(Sysmem, AttachToken_SelectsSameDomainAsInitialAllocation) {
  // The first part is mostly to verify that we have a way of influencing an initial allocation
  // to pick RAM coherency domain, for the benefit of the second AttachTokenSucceeds() call below.
  EXPECT_TRUE(AttachTokenSucceeds(
      false, false,
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.buffer_memory_constraints.cpu_domain_supported = true;
        to_modify.buffer_memory_constraints.ram_domain_supported = true;
        to_modify.buffer_memory_constraints.inaccessible_domain_supported = false;
        // This will influence the initial allocation to pick RAM.
        to_modify.usage.display = fuchsia_sysmem::wire::kDisplayUsageLayer;
        EXPECT_TRUE(to_modify.usage.cpu != 0);
      },
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.buffer_memory_constraints.cpu_domain_supported = true;
        to_modify.buffer_memory_constraints.ram_domain_supported = true;
        to_modify.buffer_memory_constraints.inaccessible_domain_supported = false;
        EXPECT_TRUE(to_modify.usage.cpu != 0);
      },
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.buffer_memory_constraints.cpu_domain_supported = true;
        to_modify.buffer_memory_constraints.ram_domain_supported = true;
        to_modify.buffer_memory_constraints.inaccessible_domain_supported = false;
        EXPECT_TRUE(to_modify.usage.cpu != 0);
      },
      [](fuchsia_sysmem::wire::BufferCollectionInfo2& to_verify) {
        EXPECT_EQ(fuchsia_sysmem::wire::CoherencyDomain::kRam,
                  to_verify.settings.buffer_settings.coherency_domain);
      },
      6));
  // Now verify that if the initial allocation is CPU coherency domain, an attached token that would
  // normally prefer RAM domain can succeed but will get CPU because the initial allocation already
  // picked CPU.
  EXPECT_TRUE(AttachTokenSucceeds(
      false, false,
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.buffer_memory_constraints.cpu_domain_supported = true;
        to_modify.buffer_memory_constraints.ram_domain_supported = true;
        to_modify.buffer_memory_constraints.inaccessible_domain_supported = false;
        EXPECT_TRUE(to_modify.usage.cpu != 0);
      },
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.buffer_memory_constraints.cpu_domain_supported = true;
        to_modify.buffer_memory_constraints.ram_domain_supported = true;
        to_modify.buffer_memory_constraints.inaccessible_domain_supported = false;
        EXPECT_TRUE(to_modify.usage.cpu != 0);
      },
      [](fuchsia_sysmem::wire::BufferCollectionConstraints& to_modify) {
        to_modify.buffer_memory_constraints.cpu_domain_supported = true;
        to_modify.buffer_memory_constraints.ram_domain_supported = true;
        to_modify.buffer_memory_constraints.inaccessible_domain_supported = false;
        // This would normally influence to pick RAM coherency domain, but because the existing
        // BufferCollectionInfo says CPU, the attached participant will get CPU instead of its
        // normally-preferred RAM.
        to_modify.usage.display = fuchsia_sysmem::wire::kDisplayUsageLayer;
        EXPECT_TRUE(to_modify.usage.cpu != 0);
      },
      [](fuchsia_sysmem::wire::BufferCollectionInfo2& to_verify) {
        EXPECT_EQ(fuchsia_sysmem::wire::CoherencyDomain::kCpu,
                  to_verify.settings.buffer_settings.coherency_domain);
      },
      6));
}

TEST(Sysmem, SetDispensable) {
  enum class Variant { kDispensableFailureBeforeAllocation, kDispensableFailureAfterAllocation };
  constexpr Variant variants[] = {Variant::kDispensableFailureBeforeAllocation,
                                  Variant::kDispensableFailureAfterAllocation};
  for (Variant variant : variants) {
    auto allocator = connect_to_sysmem_driver();

    auto token_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
    ASSERT_OK(token_endpoints_1);
    auto [token_client_1, token_server_1] = std::move(*token_endpoints_1);
    auto token_1 = fidl::BindSyncClient(std::move(token_client_1));

    // Client 1 creates a token and new LogicalBufferCollection using
    // AllocateSharedCollection().
    ASSERT_OK(allocator->AllocateSharedCollection(std::move(token_server_1)));

    auto token_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollectionToken>();
    ASSERT_OK(token_endpoints_2);
    auto [token_client_2, token_server_2] = std::move(*token_endpoints_2);
    auto token_2 = fidl::BindSyncClient(std::move(token_client_2));

    // Client 1 duplicates its token and gives the duplicate to client 2 (this
    // test is single proc, so both clients are coming from this client
    // process - normally the two clients would be in separate processes with
    // token_client_2 transferred to another participant).
    ASSERT_OK(token_1->Duplicate(ZX_RIGHT_SAME_RIGHTS, std::move(token_server_2)));

    // Client 1 calls SetDispensable() on token 2.  Client 2's constraints will be part of the
    // initial allocation, but post-allocation, client 2 failure won't cause failure of the
    // LogicalBufferCollection.
    ASSERT_OK(token_2->SetDispensable());

    auto collection_endpoints_1 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
    ASSERT_OK(collection_endpoints_1);
    auto [collection_client_1, collection_server_1] = std::move(*collection_endpoints_1);
    auto collection_1 = fidl::BindSyncClient(std::move(collection_client_1));

    ASSERT_NE(token_1.client_end().channel().get(), ZX_HANDLE_INVALID, "");
    ASSERT_OK(
        allocator->BindSharedCollection(token_1.TakeClientEnd(), std::move(collection_server_1)));

    fuchsia_sysmem::wire::BufferCollectionConstraints constraints_1;
    constraints_1.usage.cpu =
        fuchsia_sysmem::wire::kCpuUsageReadOften | fuchsia_sysmem::wire::kCpuUsageWriteOften;
    constraints_1.min_buffer_count_for_camping = 2;
    constraints_1.has_buffer_memory_constraints = true;
    constraints_1.buffer_memory_constraints = fuchsia_sysmem::wire::BufferMemoryConstraints{
        .min_size_bytes = 64 * 1024,
        .max_size_bytes = 64 * 1024,
        .physically_contiguous_required = false,
        .secure_required = false,
        .ram_domain_supported = false,
        .cpu_domain_supported = true,
        .inaccessible_domain_supported = false,
        .heap_permitted_count = 0,
        .heap_permitted = {},
    };

    // constraints_2 is just a copy of constraints_1 - since both participants
    // specify min_buffer_count_for_camping 2, the total number of allocated
    // buffers will be 4.  There are no handles in the constraints struct so a
    // struct copy instead of clone is fine here.
    fuchsia_sysmem::wire::BufferCollectionConstraints constraints_2(constraints_1);
    ASSERT_EQ(constraints_2.min_buffer_count_for_camping, 2, "");

    // Client 2 connects to sysmem separately.
    auto allocator_2 = connect_to_sysmem_driver();
    ASSERT_OK(allocator_2);

    auto collection_endpoints_2 = fidl::CreateEndpoints<fuchsia_sysmem::BufferCollection>();
    ASSERT_OK(collection_endpoints_2);
    auto [collection_client_2, collection_server_2] = std::move(*collection_endpoints_2);
    auto collection_2 = fidl::BindSyncClient(std::move(collection_client_2));
    ;

    // Just because we can, perform this sync as late as possible, just before
    // the BindSharedCollection() via allocator2_client_2.  Without this Sync(),
    // the BindSharedCollection() might arrive at the server before the
    // Duplicate() that delivered the server end of token_client_2 to sysmem,
    // which would cause sysmem to not recognize the token.
    ASSERT_OK(collection_1->Sync());

    ASSERT_NE(token_2.client_end().channel().get(), ZX_HANDLE_INVALID, "");
    ASSERT_OK(
        allocator_2->BindSharedCollection(token_2.TakeClientEnd(), std::move(collection_server_2)));

    ASSERT_OK(collection_1->SetConstraints(true, constraints_1));

    if (variant == Variant::kDispensableFailureBeforeAllocation) {
      // Client 2 will now abruptly close its channel.  Since client 2 hasn't provided constraints
      // yet, the LogicalBufferCollection will fail.
      collection_2 = {};
    } else {
      // Client 2 SetConstraints().

      ASSERT_OK(collection_2->SetConstraints(true, constraints_2));
    }

    //
    // kDispensableFailureAfterAllocation - The LogicalBufferCollection won't fail.
    auto allocate_result_1 = collection_1->WaitForBuffersAllocated();

    if (variant == Variant::kDispensableFailureBeforeAllocation) {
      // The LogicalBufferCollection will be failed, because client 2 failed before providing
      // constraints.
      ASSERT_EQ(allocate_result_1.status(), ZX_ERR_PEER_CLOSED, "");
      // next variant, if any
      continue;
    }
    ZX_DEBUG_ASSERT(variant == Variant::kDispensableFailureAfterAllocation);

    // The LogicalBufferCollection will not be failed, because client 2 didn't fail before
    // allocation.
    ASSERT_EQ(allocate_result_1.status(), ZX_OK, "");
    ASSERT_EQ(allocate_result_1->status, ZX_OK, "");
    // This being 4 instead of 2 proves that client 2's constraints were used.
    ASSERT_EQ(allocate_result_1->buffer_collection_info.buffer_count, 4, "");

    // Now that we know allocation is done, client 2 will abruptly close its channel, which the
    // server treats as client 2 failure.  Since client 2 has already provided constraints, this
    // won't fail the LogicalBufferCollection.
    collection_2 = {};

    // Give the LogicalBufferCollection time to fail if it were going to fail, which it isn't.
    nanosleep_duration(zx::msec(250));

    // Verify LogicalBufferCollection still ok.
    ASSERT_OK(collection_1->Sync());

    // next variant, if any
  }
}

int main(int argc, char** argv) {
  setlinebuf(stdout);
  zxtest::Runner::GetInstance()->AddObserver(&test_observer);

  return RUN_ALL_TESTS(argc, argv);
}
