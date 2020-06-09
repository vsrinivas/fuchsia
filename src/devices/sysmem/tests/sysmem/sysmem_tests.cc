// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <inttypes.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fidl-async-2/fidl_struct.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <lib/zx/event.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/errors.h>
#include <zircon/limits.h>
#include <zircon/pixelformat.h>

#include <limits>
#include <string>
#include <thread>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <hw/arch_ops.h>
#include <zxtest/zxtest.h>

// We assume one sysmem since boot, for now.
const char* kSysmemDevicePath = "/dev/class/sysmem/000";

using BufferCollectionConstraints = FidlStruct<fuchsia_sysmem_BufferCollectionConstraints,
                                               llcpp::fuchsia::sysmem::BufferCollectionConstraints>;
using BufferCollectionInfo = FidlStruct<fuchsia_sysmem_BufferCollectionInfo_2,
                                        llcpp::fuchsia::sysmem::BufferCollectionInfo_2>;

namespace {

zx_status_t connect_to_sysmem_driver(zx::channel* allocator2_client_param) {
  zx_status_t status;

  zx::channel driver_client;
  zx::channel driver_server;
  status = zx::channel::create(0, &driver_client, &driver_server);
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_service_connect(kSysmemDevicePath, driver_server.release());
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  zx::channel allocator2_client;
  zx::channel allocator2_server;
  status = zx::channel::create(0, &allocator2_client, &allocator2_server);
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  status = fuchsia_sysmem_DriverConnectorConnect(driver_client.get(), allocator2_server.release());
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  *allocator2_client_param = std::move(allocator2_client);
  return ZX_OK;
}

zx_status_t connect_to_sysmem_service(zx::channel* allocator2_client_param) {
  zx_status_t status;

  zx::channel allocator2_client;
  zx::channel allocator2_server;
  status = zx::channel::create(0, &allocator2_client, &allocator2_server);
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator", allocator2_server.release());
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  *allocator2_client_param = std::move(allocator2_client);
  return ZX_OK;
}

zx_koid_t get_koid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  EXPECT_EQ(status, ZX_OK, "");
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

zx_status_t verify_connectivity(zx::channel& allocator2_client) {
  zx_status_t status;

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  status = fuchsia_sysmem_AllocatorAllocateNonSharedCollection(allocator2_client.get(),
                                                               collection_server.release());
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  status = fuchsia_sysmem_BufferCollectionSync(collection_client.get());
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t make_single_participant_collection(zx::channel* collection_client_channel) {
  // We could use AllocateNonSharedCollection() to implement this function, but we're already
  // using AllocateNonSharedCollection() during verify_connectivity(), so instead just set up the
  // more general (and more real) way here.

  zx_status_t status;
  zx::channel allocator2_client;
  status = connect_to_sysmem_driver(&allocator2_client);
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator2_client.get(),
                                                            token_server.release());
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  EXPECT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator2_client.get(), token_client.release(), collection_server.release());
  EXPECT_EQ(status, ZX_OK, "");
  if (status != ZX_OK) {
    return status;
  }

  *collection_client_channel = std::move(collection_client);
  return ZX_OK;
}

const std::string& GetBoardName() {
  static std::string s_board_name;
  if (s_board_name.empty()) {
    constexpr char kSysInfoPath[] = "/svc/fuchsia.sysinfo.SysInfo";
    fbl::unique_fd sysinfo(open(kSysInfoPath, O_RDWR));
    ZX_ASSERT(sysinfo);
    zx::channel channel;
    zx_status_t status =
        fdio_get_service_handle(sysinfo.release(), channel.reset_and_get_address());
    ZX_ASSERT(status == ZX_OK);

    char board_name[fuchsia_sysinfo_BOARD_NAME_LEN + 1];
    size_t actual_size;
    zx_status_t fidl_status = fuchsia_sysinfo_SysInfoGetBoardName(
        channel.get(), &status, board_name, sizeof(board_name), &actual_size);
    ZX_ASSERT(fidl_status == ZX_OK);
    ZX_ASSERT(status == ZX_OK);
    board_name[actual_size] = '\0';
    printf("\nFound board %s\n", board_name);
    s_board_name = std::string(board_name, actual_size);
  }
  return s_board_name;
}

bool is_board_astro() { return GetBoardName() == "astro"; }

bool is_board_sherlock() { return GetBoardName() == "sherlock"; }

bool is_board_astro_or_sherlock() {
  if (is_board_astro()) {
    return true;
  }
  if (is_board_sherlock()) {
    return true;
  }
  return false;
}

// TODO(37686): (or jbauman) Change to is_board_astro_or_sherlock() once
// AMLOGIC_SECURE is working on sherlock.
bool is_board_with_amlogic_secure() { return is_board_astro_or_sherlock(); }

// TODO(37686): (or jbauman) Change to is_board_astro_or_sherlock() once
// AMLOGIC_SECURE_VDEC is working on sherlock.
bool is_board_with_amlogic_secure_vdec() { return is_board_astro_or_sherlock(); }

void nanosleep_duration(zx::duration duration) {
  zx_status_t status = zx::nanosleep(zx::deadline_after(duration));
  ZX_ASSERT(status == ZX_OK);
}

// Faulting on write to a mapping to the VMO can't be checked currently
// because maybe it goes into CPU cache without faulting because 34580?
class SecureVmoReadTester {
 public:
  SecureVmoReadTester(zx::vmo secure_vmo);
  ~SecureVmoReadTester();
  bool IsReadFromSecureAThing();
  void AttemptReadFromSecure();

 private:
  zx::vmo secure_vmo_;
  zx::vmar child_vmar_;
  // volatile only so reads in the code actually read despite the value being
  // discarded.
  volatile uint8_t* map_addr_ = {};
  // This is set to true just before the attempt to read.
  std::atomic<bool> is_read_from_secure_attempted_ = false;
  std::atomic<bool> is_read_from_secure_a_thing_ = false;
  std::thread let_die_thread_;
  std::atomic<bool> is_let_die_started_ = false;
};

SecureVmoReadTester::SecureVmoReadTester(zx::vmo secure_vmo) : secure_vmo_(std::move(secure_vmo)) {
  // We need a child VMAR so we can clean up robustly without relying on a fault
  // to occur at location where a VMO was recently mapped but which
  // theoretically something else could be mapped unless we're specific with a
  // VMAR that isn't letting something else get mapped there yet.
  zx_vaddr_t child_vaddr;
  zx_status_t status = zx::vmar::root_self()->allocate2(
      ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_SPECIFIC, 0, ZX_PAGE_SIZE,
      &child_vmar_, &child_vaddr);
  ZX_ASSERT(status == ZX_OK);

  uintptr_t map_addr_raw;
  status = child_vmar_.map(0, secure_vmo_, 0, ZX_PAGE_SIZE,
                           ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_SPECIFIC | ZX_VM_MAP_RANGE,
                           &map_addr_raw);
  ZX_ASSERT(status == ZX_OK);
  map_addr_ = reinterpret_cast<uint8_t*>(map_addr_raw);
  ZX_ASSERT(reinterpret_cast<uint8_t*>(child_vaddr) == map_addr_);

  status = secure_vmo_.op_range(ZX_VMO_OP_CACHE_INVALIDATE, 0, ZX_PAGE_SIZE, nullptr, 0);
  ZX_ASSERT(status == ZX_OK);

  // But currently the read doesn't visibily fault while the vaddr is mapped to
  // a secure page.  Instead the read gets stuck and doesn't complete (perhaps
  // internally faulting from kernel's point of view).  While that's not ideal,
  // we can check that the thread doing the reading doesn't get anything from
  // the read while mapped to a secure page, and then let the thread fault
  // normally by unmapping the secure VMO.
  let_die_thread_ = std::thread([this] {
    is_let_die_started_ = true;
    // Ensure is_read_from_secure_attempted_ becomes true before we start
    // waiting.  This just increases the liklihood that we wait long enough
    // for the read itself to potentially execute (expected to fault instead).
    while (!is_read_from_secure_attempted_) {
      nanosleep_duration(zx::msec(10));
    }
    // Wait 500ms for the read attempt to succed; the read attempt should not
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
    status = child_vmar_.protect2(0, reinterpret_cast<uintptr_t>(map_addr_), ZX_PAGE_SIZE);
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

void SecureVmoReadTester::AttemptReadFromSecure() {
  ZX_ASSERT(is_let_die_started_);
  ZX_ASSERT(!is_read_from_secure_attempted_);
  is_read_from_secure_attempted_ = true;
  // This attempt to read from a vaddr that's mapped to a secure paddr won't
  // succeed.  For now the read gets stuck while mapped to secure memory, and
  // then faults when we've unmapped the VMO.  This address is in a child VMAR
  // so we know nothing else will be getting mapped to the vaddr.
  //
  // The loop is mainly for the benefit of debugging/fixing the test should the very first write,
  // flush, read not force and fence a fault.
  for (uint32_t i = 0; i < ZX_PAGE_SIZE; ++i) {
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
    if (i % 64 == 0) {
      fprintf(stderr, "%08x: ", i);
    }
    fprintf(stderr, "%02x ", value);
    if ((i + 1) % 64 == 0) {
      fprintf(stderr, "\n");
    }
  }
  fprintf(stderr, "\n");
  // If we made it through the whole page without faulting, yet only read zero, consider that
  // success.  Cause the thead to "die" here on purpose so the test can pass.  This is not the
  // typical case, but can happen at least on sherlock.  Typically we fault during the write, flush,
  // read of byte 0 above.
  ZX_PANIC("didn't fault, but also didn't read non-zero, so pretend to fault");
}

}  // namespace

TEST(Sysmem, DriverConnection) {
  zx_status_t status;
  zx::channel allocator2_client;
  status = connect_to_sysmem_driver(&allocator2_client);
  ASSERT_EQ(status, ZX_OK, "");

  status = verify_connectivity(allocator2_client);
  ASSERT_EQ(status, ZX_OK, "");
}

TEST(Sysmem, ServiceConnection) {
  zx_status_t status;
  zx::channel allocator2_client;
  status = connect_to_sysmem_service(&allocator2_client);
  ASSERT_EQ(status, ZX_OK, "");

  status = verify_connectivity(allocator2_client);
  ASSERT_EQ(status, ZX_OK, "");
}

TEST(Sysmem, VerifyBufferCollectionToken) {
  zx_status_t status;
  zx::channel allocator_client;
  status = connect_to_sysmem_driver(&allocator_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token2_client;
  zx::channel token2_server;
  status = zx::channel::create(0, &token2_client, &token2_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_EQ(fuchsia_sysmem_BufferCollectionTokenDuplicate(token_client.get(), ZX_RIGHT_SAME_RIGHTS,
                                                          token2_server.release()),
            ZX_OK, "");

  zx::channel not_token_client;
  zx::channel not_token_server;
  status = zx::channel::create(0, &not_token_client, &not_token_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_EQ(fuchsia_sysmem_BufferCollectionTokenSync(token_client.get()), ZX_OK, "");
  ASSERT_EQ(fuchsia_sysmem_BufferCollectionTokenSync(token2_client.get()), ZX_OK, "");

  bool is_token_valid = false;
  ASSERT_EQ(fuchsia_sysmem_AllocatorValidateBufferCollectionToken(
                allocator_client.get(), get_related_koid(token_client.get()), &is_token_valid),
            ZX_OK, "");
  ASSERT_EQ(is_token_valid, true, "");
  ASSERT_EQ(fuchsia_sysmem_AllocatorValidateBufferCollectionToken(
                allocator_client.get(), get_related_koid(token2_client.get()), &is_token_valid),
            ZX_OK, "");
  ASSERT_EQ(is_token_valid, true, "");

  ASSERT_EQ(fuchsia_sysmem_AllocatorValidateBufferCollectionToken(
                allocator_client.get(), get_related_koid(not_token_client.get()), &is_token_valid),
            ZX_OK, "");
  ASSERT_EQ(is_token_valid, false, "");
}

TEST(Sysmem, TokenOneParticipantNoImageConstraints) {
  zx::channel collection_client;
  zx_status_t status = make_single_participant_collection(&collection_client);
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  constraints->min_buffer_count_for_camping = 3;
  constraints->has_buffer_memory_constraints = true;
  constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
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
  ZX_DEBUG_ASSERT(constraints->image_format_constraints_count == 0);
  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");

  ASSERT_EQ(buffer_collection_info->buffer_count, 3, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, 64 * 1024, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
            fuchsia_sysmem_CoherencyDomain_CPU, "");
  ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");

  for (uint32_t i = 0; i < 64; ++i) {
    if (i < 3) {
      ASSERT_NE(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
      uint64_t size_bytes = 0;
      status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo, &size_bytes);
      ASSERT_EQ(status, ZX_OK, "");
      ASSERT_EQ(size_bytes, 64 * 1024, "");
    } else {
      ASSERT_EQ(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
    }
  }
}

TEST(Sysmem, TokenOneParticipantWithImageConstraints) {
  zx_status_t status;
  zx::channel allocator2_client;
  status = connect_to_sysmem_driver(&allocator2_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator2_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator2_client.get(), token_client.release(), collection_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  constraints->min_buffer_count_for_camping = 3;
  constraints->has_buffer_memory_constraints = true;
  constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
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
  constraints->image_format_constraints_count = 1;
  fuchsia_sysmem_ImageFormatConstraints& image_constraints =
      constraints->image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_NV12;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] = fuchsia_sysmem_ColorSpace{
      .type = fuchsia_sysmem_ColorSpaceType_REC709,
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

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");

  ASSERT_EQ(buffer_collection_info->buffer_count, 3, "");
  // The size should be sufficient for the whole NV12 frame, not just min_size_bytes.
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, 64 * 1024 * 3 / 2, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
            fuchsia_sysmem_CoherencyDomain_CPU, "");
  // We specified image_format_constraints so the result must also have
  // image_format_constraints.
  ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, true, "");

  for (uint32_t i = 0; i < 64; ++i) {
    if (i < 3) {
      ASSERT_NE(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
      uint64_t size_bytes = 0;
      status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo, &size_bytes);
      ASSERT_EQ(status, ZX_OK, "");
      // The portion of the VMO the client can use is large enough to hold the min image size,
      // despite the min buffer size being smaller.
      ASSERT_GE(buffer_collection_info->settings.buffer_settings.size_bytes, 64 * 1024 * 3 / 2, "");
      // The vmo has room for the nominal size of the portion of the VMO the client can use.
      ASSERT_LE(buffer_collection_info->buffers[i].vmo_usable_start +
                    buffer_collection_info->settings.buffer_settings.size_bytes,
                size_bytes, "");
    } else {
      ASSERT_EQ(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
    }
  }
}

TEST(Sysmem, MinBufferCount) {
  zx_status_t status;
  zx::channel allocator_client;
  status = connect_to_sysmem_driver(&allocator_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client.get(), token_client.release(), collection_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  constraints->min_buffer_count_for_camping = 3;
  constraints->min_buffer_count = 5;
  constraints->has_buffer_memory_constraints = true;
  constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
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
  ZX_DEBUG_ASSERT(constraints->image_format_constraints_count == 0);
  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");

  ASSERT_EQ(buffer_collection_info->buffer_count, 5, "");
}

TEST(Sysmem, BufferName) {
  zx_status_t status;
  zx::channel allocator_client;
  status = connect_to_sysmem_driver(&allocator_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client.get(), token_client.release(), collection_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  const char kSysmemName[] = "abcdefghijkl\0mnopqrstuvwxyz";
  fuchsia_sysmem_BufferCollectionSetName(collection_client.get(), 10, kSysmemName,
                                         sizeof(kSysmemName));
  const char kLowPrioName[] = "low_pri";
  fuchsia_sysmem_BufferCollectionSetName(collection_client.get(), 0, kLowPrioName,
                                         sizeof(kLowPrioName));
  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  constraints->min_buffer_count = 1;
  constraints->has_buffer_memory_constraints = true;
  constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 0,
      .heap_permitted = {},
  };
  ZX_DEBUG_ASSERT(constraints->image_format_constraints_count == 0);
  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");

  ASSERT_EQ(buffer_collection_info->buffer_count, 1, "");
  zx_handle_t vmo = buffer_collection_info->buffers[0].vmo;
  char vmo_name[ZX_MAX_NAME_LEN];
  ASSERT_EQ(ZX_OK, zx_object_get_property(vmo, ZX_PROP_NAME, vmo_name, sizeof(vmo_name)));

  // Should be equal up to the first null, plus an index
  EXPECT_EQ(std::string("abcdefghijkl:0"), std::string(vmo_name));
  EXPECT_EQ(0u, vmo_name[ZX_MAX_NAME_LEN - 1]);
}

TEST(Sysmem, NoToken) {
  zx_status_t status;
  zx::channel allocator2_client;
  status = connect_to_sysmem_driver(&allocator2_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateNonSharedCollection(allocator2_client.get(),
                                                               collection_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  // Ask for display usage to encourage using the ram coherency domain.
  constraints->usage.display = fuchsia_sysmem_displayUsageLayer;
  constraints->min_buffer_count_for_camping = 3;
  constraints->has_buffer_memory_constraints = true;
  constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
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
  ZX_DEBUG_ASSERT(constraints->image_format_constraints_count == 0);
  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");

  ASSERT_EQ(buffer_collection_info->buffer_count, 3, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, 64 * 1024, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
            fuchsia_sysmem_CoherencyDomain_RAM, "");
  ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");

  for (uint32_t i = 0; i < 64; ++i) {
    if (i < 3) {
      ASSERT_NE(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
      uint64_t size_bytes = 0;
      status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo, &size_bytes);
      ASSERT_EQ(status, ZX_OK, "");
      ASSERT_EQ(size_bytes, 64 * 1024, "");
    } else {
      ASSERT_EQ(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
    }
  }
}

TEST(Sysmem, MultipleParticipants) {
  zx_status_t status;
  zx::channel allocator2_client_1;
  status = connect_to_sysmem_driver(&allocator2_client_1);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client_1;
  zx::channel token_server_1;
  status = zx::channel::create(0, &token_client_1, &token_server_1);
  ASSERT_EQ(status, ZX_OK, "");

  // Client 1 creates a token and new LogicalBufferCollection using
  // AllocateSharedCollection().
  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator2_client_1.get(),
                                                            token_server_1.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client_2;
  zx::channel token_server_2;
  status = zx::channel::create(0, &token_client_2, &token_server_2);
  ASSERT_EQ(status, ZX_OK, "");

  // Client 1 duplicates its token and gives the duplicate to client 2 (this
  // test is single proc, so both clients are coming from this client
  // process - normally the two clients would be in separate processes with
  // token_client_2 transferred to another participant).
  status = fuchsia_sysmem_BufferCollectionTokenDuplicate(token_client_1.get(), ZX_RIGHT_SAME_RIGHTS,
                                                         token_server_2.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client_3;
  zx::channel token_server_3;
  status = zx::channel::create(0, &token_client_3, &token_server_3);
  ASSERT_EQ(status, ZX_OK, "");

  // Client 3 is used to test a participant that doesn't set any constraints
  // and only wants a notification that the allocation is done.
  status = fuchsia_sysmem_BufferCollectionTokenDuplicate(token_client_1.get(), ZX_RIGHT_SAME_RIGHTS,
                                                         token_server_3.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client_1;
  zx::channel collection_server_1;
  status = zx::channel::create(0, &collection_client_1, &collection_server_1);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client_1.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator2_client_1.get(), token_client_1.release(), collection_server_1.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints_1(BufferCollectionConstraints::Default);
  constraints_1->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  constraints_1->min_buffer_count_for_camping = 3;
  constraints_1->has_buffer_memory_constraints = true;
  constraints_1->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
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
  constraints_1->image_format_constraints_count = 1;
  fuchsia_sysmem_ImageFormatConstraints& image_constraints_1 =
      constraints_1->image_format_constraints[0];
  image_constraints_1.pixel_format.type = fuchsia_sysmem_PixelFormatType_NV12;
  image_constraints_1.color_spaces_count = 1;
  image_constraints_1.color_space[0] = fuchsia_sysmem_ColorSpace{
      .type = fuchsia_sysmem_ColorSpaceType_REC709,
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
  BufferCollectionConstraints constraints_2(*constraints_1.get());
  // Modify constraints_2 to require double the width and height.
  constraints_2->image_format_constraints[0].min_coded_width = 512;
  constraints_2->image_format_constraints[0].min_coded_height = 512;

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_1.get(), true,
                                                         constraints_1.release());
  ASSERT_EQ(status, ZX_OK, "");

  // Client 2 connects to sysmem separately.
  zx::channel allocator2_client_2;
  status = connect_to_sysmem_driver(&allocator2_client_2);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client_2;
  zx::channel collection_server_2;
  status = zx::channel::create(0, &collection_client_2, &collection_server_2);
  ASSERT_EQ(status, ZX_OK, "");

  // Just because we can, perform this sync as late as possible, just before
  // the BindSharedCollection() via allocator2_client_2.  Without this Sync(),
  // the BindSharedCollection() might arrive at the server before the
  // Duplicate() that delivered the server end of token_client_2 to sysmem,
  // which would cause sysmem to not recognize the token.
  status = fuchsia_sysmem_BufferCollectionSync(collection_client_1.get());
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client_2.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator2_client_2.get(), token_client_2.release(), collection_server_2.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client_3;
  zx::channel collection_server_3;
  status = zx::channel::create(0, &collection_client_3, &collection_server_3);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client_3.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator2_client_2.get(), token_client_3.release(), collection_server_3.release());
  ASSERT_EQ(status, ZX_OK, "");

  fuchsia_sysmem_BufferCollectionConstraints empty_constraints = {};

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_3.get(), false,
                                                         &empty_constraints);
  ASSERT_EQ(status, ZX_OK, "");

  // Not all constraints have been input, so the buffers haven't been
  // allocated yet.
  zx_status_t check_status;
  status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_1.get(),
                                                                &check_status);
  ASSERT_EQ(status, ZX_OK, "");
  EXPECT_EQ(check_status, ZX_ERR_UNAVAILABLE, "");
  status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_2.get(),
                                                                &check_status);
  ASSERT_EQ(status, ZX_OK, "");
  EXPECT_EQ(check_status, ZX_ERR_UNAVAILABLE, "");

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_2.get(), true,
                                                         constraints_2.release());
  ASSERT_EQ(status, ZX_OK, "");

  //
  // Only after both participants (both clients) have SetConstraints() will
  // the allocation be successful.
  //

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info_1(BufferCollectionInfo::Default);
  // This helps with a later exact equality check.
  memset(buffer_collection_info_1.get(), 0, sizeof(*buffer_collection_info_1.get()));
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client_1.get(), &allocation_status, buffer_collection_info_1.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");

  status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_1.get(),
                                                                &check_status);
  ASSERT_EQ(status, ZX_OK, "");
  EXPECT_EQ(check_status, ZX_OK, "");
  status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_2.get(),
                                                                &check_status);
  ASSERT_EQ(status, ZX_OK, "");
  EXPECT_EQ(check_status, ZX_OK, "");

  BufferCollectionInfo buffer_collection_info_2(BufferCollectionInfo::Default);
  // This helps with a later exact equality check.
  memset(buffer_collection_info_2.get(), 0, sizeof(*buffer_collection_info_2.get()));
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client_2.get(), &allocation_status, buffer_collection_info_2.get());
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");

  BufferCollectionInfo buffer_collection_info_3(BufferCollectionInfo::Default);
  memset(buffer_collection_info_3.get(), 0, sizeof(*buffer_collection_info_3.get()));
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client_3.get(), &allocation_status, buffer_collection_info_3.get());
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");

  //
  // buffer_collection_info_1 and buffer_collection_info_2 should be exactly
  // equal except their non-zero handle values, which should be different.  We
  // verify the handle values then check that the structs are exactly the same
  // with handle values zeroed out.
  //

  // copy_1 and copy_2 intentionally don't manage their handle values.

  // struct copy
  fuchsia_sysmem_BufferCollectionInfo_2 copy_1 = *buffer_collection_info_1.get();
  // struct copy
  fuchsia_sysmem_BufferCollectionInfo_2 copy_2 = *buffer_collection_info_2.get();
  for (uint32_t i = 0; i < std::size(buffer_collection_info_1->buffers); ++i) {
    ASSERT_EQ(buffer_collection_info_1->buffers[i].vmo != ZX_HANDLE_INVALID,
              buffer_collection_info_2->buffers[i].vmo != ZX_HANDLE_INVALID, "");
    if (buffer_collection_info_1->buffers[i].vmo != ZX_HANDLE_INVALID) {
      // The handle values must be different.
      ASSERT_NE(buffer_collection_info_1->buffers[i].vmo, buffer_collection_info_2->buffers[i].vmo,
                "");
      // For now, the koid(s) are expected to be equal.  This is not a
      // fundamental check, in that sysmem could legitimately change in
      // future to vend separate child VMOs (of the same portion of a
      // non-copy-on-write parent VMO) to the two participants and that
      // would still be potentially valid overall.
      zx_koid_t koid_1 = get_koid(buffer_collection_info_1->buffers[i].vmo);
      zx_koid_t koid_2 = get_koid(buffer_collection_info_2->buffers[i].vmo);
      ASSERT_EQ(koid_1, koid_2, "");

      // Prepare the copies for memcmp().
      copy_1.buffers[i].vmo = ZX_HANDLE_INVALID;
      copy_2.buffers[i].vmo = ZX_HANDLE_INVALID;
    }

    // Buffer collection 3 never got a SetConstraints(), so we get no VMOs.
    ASSERT_EQ(ZX_HANDLE_INVALID, buffer_collection_info_3->buffers[i].vmo, "");
  }
  int32_t memcmp_result = memcmp(&copy_1, &copy_2, sizeof(copy_1));
  // Check that buffer_collection_info_1 and buffer_collection_info_2 are
  // consistent.
  ASSERT_EQ(memcmp_result, 0, "");

  memcmp_result = memcmp(&copy_1, buffer_collection_info_3.get(), sizeof(copy_1));
  // Check that buffer_collection_info_1 and buffer_collection_info_3 are
  // consistent, except for the vmos.
  ASSERT_EQ(memcmp_result, 0, "");

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
      ASSERT_NE(buffer_collection_info_1->buffers[i].vmo, ZX_HANDLE_INVALID, "");
      ASSERT_NE(buffer_collection_info_2->buffers[i].vmo, ZX_HANDLE_INVALID, "");

      uint64_t size_bytes_1 = 0;
      status = zx_vmo_get_size(buffer_collection_info_1->buffers[i].vmo, &size_bytes_1);
      ASSERT_EQ(status, ZX_OK, "");

      uint64_t size_bytes_2 = 0;
      status = zx_vmo_get_size(buffer_collection_info_2->buffers[i].vmo, &size_bytes_2);
      ASSERT_EQ(status, ZX_OK, "");

      // The vmo has room for the nominal size of the portion of the VMO
      // the client can use.  These checks should pass even if sysmem were
      // to vend different child VMOs to the two participants.
      ASSERT_LE(buffer_collection_info_1->buffers[i].vmo_usable_start +
                    buffer_collection_info_1->settings.buffer_settings.size_bytes,
                size_bytes_1, "");
      ASSERT_LE(buffer_collection_info_2->buffers[i].vmo_usable_start +
                    buffer_collection_info_2->settings.buffer_settings.size_bytes,
                size_bytes_2, "");
    } else {
      ASSERT_EQ(buffer_collection_info_1->buffers[i].vmo, ZX_HANDLE_INVALID, "");
      ASSERT_EQ(buffer_collection_info_2->buffers[i].vmo, ZX_HANDLE_INVALID, "");
    }
  }

  // Close to ensure grabbing null constraints from a closed collection
  // doesn't crash
  zx_status_t close_status = fuchsia_sysmem_BufferCollectionClose(collection_client_3.get());
  EXPECT_EQ(close_status, ZX_OK, "");
}

TEST(Sysmem, ConstraintsRetainedBeyondCleanClose) {
  zx_status_t status;
  zx::channel allocator2_client_1;
  status = connect_to_sysmem_driver(&allocator2_client_1);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client_1;
  zx::channel token_server_1;
  status = zx::channel::create(0, &token_client_1, &token_server_1);
  ASSERT_EQ(status, ZX_OK, "");

  // Client 1 creates a token and new LogicalBufferCollection using
  // AllocateSharedCollection().
  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator2_client_1.get(),
                                                            token_server_1.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client_2;
  zx::channel token_server_2;
  status = zx::channel::create(0, &token_client_2, &token_server_2);
  ASSERT_EQ(status, ZX_OK, "");

  // Client 1 duplicates its token and gives the duplicate to client 2 (this
  // test is single proc, so both clients are coming from this client
  // process - normally the two clients would be in separate processes with
  // token_client_2 transferred to another participant).
  status = fuchsia_sysmem_BufferCollectionTokenDuplicate(token_client_1.get(), ZX_RIGHT_SAME_RIGHTS,
                                                         token_server_2.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client_1;
  zx::channel collection_server_1;
  status = zx::channel::create(0, &collection_client_1, &collection_server_1);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client_1.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator2_client_1.get(), token_client_1.release(), collection_server_1.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints_1(BufferCollectionConstraints::Default);
  constraints_1->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  constraints_1->min_buffer_count_for_camping = 2;
  constraints_1->has_buffer_memory_constraints = true;
  constraints_1->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
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
  BufferCollectionConstraints constraints_2(*constraints_1.get());
  ASSERT_EQ(constraints_2->min_buffer_count_for_camping, 2, "");

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_1.get(), true,
                                                         constraints_1.release());
  ASSERT_EQ(status, ZX_OK, "");

  // Client 2 connects to sysmem separately.
  zx::channel allocator2_client_2;
  status = connect_to_sysmem_driver(&allocator2_client_2);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client_2;
  zx::channel collection_server_2;
  status = zx::channel::create(0, &collection_client_2, &collection_server_2);
  ASSERT_EQ(status, ZX_OK, "");

  // Just because we can, perform this sync as late as possible, just before
  // the BindSharedCollection() via allocator2_client_2.  Without this Sync(),
  // the BindSharedCollection() might arrive at the server before the
  // Duplicate() that delivered the server end of token_client_2 to sysmem,
  // which would cause sysmem to not recognize the token.
  status = fuchsia_sysmem_BufferCollectionSync(collection_client_1.get());
  ASSERT_EQ(status, ZX_OK, "");

  // client 1 will now do a clean Close(), but client 1's constraints will be
  // retained by the LogicalBufferCollection.
  status = fuchsia_sysmem_BufferCollectionClose(collection_client_1.get());
  ASSERT_EQ(status, ZX_OK, "");
  // close client 1's channel.
  collection_client_1.reset();

  // Wait briefly so that LogicalBufferCollection will have seen the channel
  // closure of client 1 before client 2 sets constraints.  If we wanted to
  // eliminate this sleep we could add a call to query how many
  // BufferCollection views still exist per LogicalBufferCollection, but that
  // call wouldn't be meant to be used by normal clients, so it seems best to
  // avoid adding such a call.
  nanosleep_duration(zx::msec(250));

  ASSERT_NE(token_client_2.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator2_client_2.get(), token_client_2.release(), collection_server_2.release());
  ASSERT_EQ(status, ZX_OK, "");

  // Not all constraints have been input (client 2 hasn't SetConstraints()
  // yet), so the buffers haven't been allocated yet.
  zx_status_t check_status;
  status = fuchsia_sysmem_BufferCollectionCheckBuffersAllocated(collection_client_2.get(),
                                                                &check_status);
  ASSERT_EQ(status, ZX_OK, "");
  EXPECT_EQ(check_status, ZX_ERR_UNAVAILABLE, "");

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_2.get(), true,
                                                         constraints_2.release());
  ASSERT_EQ(status, ZX_OK, "");

  //
  // Now that client 2 has SetConstraints(), the allocation will proceed, with
  // client 1's constraints included despite client 1 having done a clean
  // Close().
  //

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info_2(BufferCollectionInfo::Default);
  // This helps with a later exact equality check.
  memset(buffer_collection_info_2.get(), 0, sizeof(*buffer_collection_info_2.get()));
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client_2.get(), &allocation_status, buffer_collection_info_2.get());
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");

  // The fact that this is 4 instead of 2 proves that client 1's constraints
  // were taken into account.
  ASSERT_EQ(buffer_collection_info_2->buffer_count, 4, "");
}

TEST(Sysmem, HeapConstraints) {
  zx_status_t status;
  zx::channel allocator_client;
  status = connect_to_sysmem_driver(&allocator_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client.get(), token_client.release(), collection_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.vulkan = fuchsia_sysmem_vulkanUsageTransferDst;
  constraints->min_buffer_count_for_camping = 1;
  constraints->has_buffer_memory_constraints = true;
  constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = true,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem_HeapType_SYSTEM_RAM}};

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");
  ASSERT_EQ(buffer_collection_info->buffer_count, 1, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
            fuchsia_sysmem_CoherencyDomain_INACCESSIBLE, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.heap,
            fuchsia_sysmem_HeapType_SYSTEM_RAM, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, true, "");
}

TEST(Sysmem, CpuUsageAndInaccessibleDomainFails) {
  zx_status_t status;
  zx::channel allocator_client;
  status = connect_to_sysmem_driver(&allocator_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client.get(), token_client.release(), collection_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  constraints->min_buffer_count_for_camping = 1;
  constraints->has_buffer_memory_constraints = true;
  constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = true,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = false,
      .inaccessible_domain_supported = true,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem_HeapType_SYSTEM_RAM}};

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // usage.cpu != 0 && inaccessible_domain_supported is expected to result in failure to
  // allocate.
  ASSERT_NE(status, ZX_OK, "");
}

TEST(Sysmem, RequiredSize) {
  zx_status_t status;
  zx::channel allocator_client;
  status = connect_to_sysmem_driver(&allocator_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client.get(), token_client.release(), collection_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  constraints->min_buffer_count_for_camping = 1;
  constraints->has_buffer_memory_constraints = false;
  constraints->image_format_constraints_count = 1;
  fuchsia_sysmem_ImageFormatConstraints& image_constraints =
      constraints->image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_NV12;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] = fuchsia_sysmem_ColorSpace{
      .type = fuchsia_sysmem_ColorSpaceType_REC709,
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

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  ASSERT_EQ(status, ZX_OK, "");

  size_t vmo_size;
  status = zx_vmo_get_size(buffer_collection_info->buffers[0].vmo, &vmo_size);
  ASSERT_EQ(status, ZX_OK, "");

  // Image must be at least 512x1024 NV12, due to the required max sizes
  // above.
  EXPECT_LE(1024 * 512 * 3 / 2, vmo_size);
}

TEST(Sysmem, CpuUsageAndNoBufferMemoryConstraints) {
  zx_status_t status;
  zx::channel allocator_client_1;
  status = connect_to_sysmem_driver(&allocator_client_1);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client_1;
  zx::channel token_server_1;
  status = zx::channel::create(0, &token_client_1, &token_server_1);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client_1.get(),
                                                            token_server_1.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client_2;
  zx::channel token_server_2;
  status = zx::channel::create(0, &token_client_2, &token_server_2);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_BufferCollectionTokenDuplicate(token_client_1.get(), ZX_RIGHT_SAME_RIGHTS,
                                                         token_server_2.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client_1;
  zx::channel collection_server_1;
  status = zx::channel::create(0, &collection_client_1, &collection_server_1);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client_1.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client_1.get(), token_client_1.release(), collection_server_1.release());
  ASSERT_EQ(status, ZX_OK, "");

  // First client has CPU usage constraints but no buffer memory constraints.
  BufferCollectionConstraints constraints_1(BufferCollectionConstraints::Default);
  constraints_1->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  constraints_1->min_buffer_count_for_camping = 1;
  constraints_1->has_buffer_memory_constraints = false;

  BufferCollectionConstraints constraints_2(BufferCollectionConstraints::Default);
  constraints_2->usage.display = fuchsia_sysmem_displayUsageLayer;
  constraints_2->min_buffer_count_for_camping = 1;
  constraints_2->has_buffer_memory_constraints = true;
  constraints_2->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
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

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_1.get(), true,
                                                         constraints_1.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel allocator_client_2;
  status = connect_to_sysmem_driver(&allocator_client_2);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client_2;
  zx::channel collection_server_2;
  status = zx::channel::create(0, &collection_client_2, &collection_server_2);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_BufferCollectionSync(collection_client_1.get());
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client_2.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client_2.get(), token_client_2.release(), collection_server_2.release());
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_2.get(), true,
                                                         constraints_2.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info_1(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client_1.get(), &allocation_status, buffer_collection_info_1.get());
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");
  ASSERT_EQ(buffer_collection_info_1->settings.buffer_settings.coherency_domain,
            fuchsia_sysmem_CoherencyDomain_CPU, "");
}

TEST(Sysmem, ContiguousSystemRamIsCached) {
  zx_status_t status;
  zx::channel allocator_client;
  status = connect_to_sysmem_driver(&allocator_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client.get(), token_client.release(), collection_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.vulkan = fuchsia_sysmem_vulkanUsageTransferDst;
  constraints->min_buffer_count_for_camping = 1;
  constraints->has_buffer_memory_constraints = true;
  constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
      .min_size_bytes = 4 * 1024,
      .max_size_bytes = 4 * 1024,
      .physically_contiguous_required = true,
      .secure_required = false,
      .ram_domain_supported = false,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      // Constraining this to SYSTEM_RAM is redundant for now.
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem_HeapType_SYSTEM_RAM}};

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");
  ASSERT_EQ(buffer_collection_info->buffer_count, 1, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
            fuchsia_sysmem_CoherencyDomain_CPU, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.heap,
            fuchsia_sysmem_HeapType_SYSTEM_RAM, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, true, "");

  // We could potentially map and try some non-aligned accesses, but on x64
  // that'd just work anyway IIRC, so just directly check if the cache policy
  // is cached so that non-aligned accesses will work on aarch64.
  //
  // We're intentionally only requiring this to be true in a test that
  // specifies CoherencyDomain_CPU - intentionally don't care for
  // CoherencyDomain_RAM or CoherencyDomain_INACCESSIBLE (when not protected).
  // CoherencyDomain_INACCESSIBLE + protected has a separate test (
  // test_sysmem_protected_ram_is_uncached).
  zx::unowned_vmo the_vmo(buffer_collection_info->buffers[0].vmo);
  zx_info_vmo_t vmo_info{};
  status = the_vmo->get_info(ZX_INFO_VMO, &vmo_info, sizeof(vmo_info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(vmo_info.cache_policy, ZX_CACHE_POLICY_CACHED, "");
}

TEST(Sysmem, ContiguousSystemRamIsRecycled) {
  zx_status_t status;
  zx::channel allocator_client;
  status = connect_to_sysmem_driver(&allocator_client);
  ASSERT_EQ(status, ZX_OK, "");

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
  constexpr uint64_t kBytesToAllocatePerPass = 4 * 1024 * 1024;
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

    zx::channel token_client;
    zx::channel token_server;
    status = zx::channel::create(0, &token_client, &token_server);
    ASSERT_EQ(status, ZX_OK, "");

    status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                              token_server.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx::channel collection_client;
    zx::channel collection_server;
    status = zx::channel::create(0, &collection_client, &collection_server);
    ASSERT_EQ(status, ZX_OK, "");

    ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
    status = fuchsia_sysmem_AllocatorBindSharedCollection(
        allocator_client.get(), token_client.release(), collection_server.release());
    ASSERT_EQ(status, ZX_OK, "");

    BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
    constraints->usage.vulkan = fuchsia_sysmem_vulkanUsageTransferDst;
    constraints->min_buffer_count_for_camping = 1;
    constraints->has_buffer_memory_constraints = true;
    constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
        .min_size_bytes = kBytesToAllocatePerPass,
        .max_size_bytes = kBytesToAllocatePerPass,
        .physically_contiguous_required = true,
        .secure_required = false,
        .ram_domain_supported = false,
        .cpu_domain_supported = true,
        .inaccessible_domain_supported = false,
        // Constraining this to SYSTEM_RAM is redundant for now.
        .heap_permitted_count = 1,
        .heap_permitted = {fuchsia_sysmem_HeapType_SYSTEM_RAM}};

    status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                           constraints.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx_status_t allocation_status;
    BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        collection_client.get(), &allocation_status, buffer_collection_info.get());
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_EQ(allocation_status, ZX_OK, "");
    ASSERT_EQ(buffer_collection_info->buffer_count, 1, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
              fuchsia_sysmem_CoherencyDomain_CPU, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.heap,
              fuchsia_sysmem_HeapType_SYSTEM_RAM, "");
    ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, true, "");

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
  zx_status_t status;
  zx::channel allocator_client;
  status = connect_to_sysmem_driver(&allocator_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client.get(), token_client.release(), collection_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.none = fuchsia_sysmem_noneUsage;
  constraints->min_buffer_count_for_camping = 3;
  constraints->min_buffer_count = 5;
  constraints->has_buffer_memory_constraints = true;
  constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
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
  ZX_DEBUG_ASSERT(constraints->image_format_constraints_count == 0);
  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status{};
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  //
  // If the aggregate usage only has "none" usage, allocation should fail.
  // Because we weren't waiting at the time that allocation failed, we don't
  // necessarily get a response from the wait.
  //
  // TODO(dustingreen): Once we're able to issue async client requests using
  // llcpp, put the wait in flight before the SetConstraints() so we can
  // verify that the wait succeeds but the allocation_status is
  // ZX_ERR_NOT_SUPPORTED.
  ASSERT_TRUE(status == ZX_ERR_PEER_CLOSED || allocation_status == ZX_ERR_NOT_SUPPORTED);
}

TEST(Sysmem, NoneUsageAndOtherUsageFromSingleParticipantFails) {
  zx_status_t status;
  zx::channel allocator_client;
  status = connect_to_sysmem_driver(&allocator_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client.get(), token_client.release(), collection_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  // Specify both "none" and "cpu" usage from a single participant, which will
  // cause allocation failure.
  constraints->usage.none = fuchsia_sysmem_noneUsage;
  constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften;
  constraints->min_buffer_count_for_camping = 3;
  constraints->min_buffer_count = 5;
  constraints->has_buffer_memory_constraints = true;
  constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
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
  ZX_DEBUG_ASSERT(constraints->image_format_constraints_count == 0);
  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  //
  // If the aggregate usage has both "none" usage and "cpu" usage from a
  // single participant, allocation should fail.
  //
  // TODO(dustingreen): Once we're able to issue async client requests using
  // llcpp, put the wait in flight before the SetConstraints() so we can
  // verify that the wait succeeds but the allocation_status is
  // ZX_ERR_NOT_SUPPORTED.
  ASSERT_TRUE(status == ZX_ERR_PEER_CLOSED || allocation_status == ZX_ERR_NOT_SUPPORTED);
}

TEST(Sysmem, NoneUsageWithSeparateOtherUsageSucceeds) {
  zx_status_t status;
  zx::channel allocator2_client_1;
  status = connect_to_sysmem_driver(&allocator2_client_1);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client_1;
  zx::channel token_server_1;
  status = zx::channel::create(0, &token_client_1, &token_server_1);
  ASSERT_EQ(status, ZX_OK, "");

  // Client 1 creates a token and new LogicalBufferCollection using
  // AllocateSharedCollection().
  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator2_client_1.get(),
                                                            token_server_1.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client_2;
  zx::channel token_server_2;
  status = zx::channel::create(0, &token_client_2, &token_server_2);
  ASSERT_EQ(status, ZX_OK, "");

  // Client 1 duplicates its token and gives the duplicate to client 2 (this
  // test is single proc, so both clients are coming from this client
  // process - normally the two clients would be in separate processes with
  // token_client_2 transferred to another participant).
  status = fuchsia_sysmem_BufferCollectionTokenDuplicate(token_client_1.get(), ZX_RIGHT_SAME_RIGHTS,
                                                         token_server_2.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client_1;
  zx::channel collection_server_1;
  status = zx::channel::create(0, &collection_client_1, &collection_server_1);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client_1.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator2_client_1.get(), token_client_1.release(), collection_server_1.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints_1(BufferCollectionConstraints::Default);
  constraints_1->usage.none = fuchsia_sysmem_noneUsage;
  constraints_1->min_buffer_count_for_camping = 3;
  constraints_1->has_buffer_memory_constraints = true;
  constraints_1->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
      // This min_size_bytes is intentionally too small to hold the min_coded_width and
      // min_coded_height in NV12
      // format.
      .min_size_bytes = 64 * 1024,
      // Allow a max that's just large enough to accomodate the size implied
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
  BufferCollectionConstraints constraints_2(*constraints_1.get());
  // Modify constraints_2 to set non-"none" usage.
  constraints_2->usage.none = 0;
  constraints_2->usage.vulkan = fuchsia_sysmem_vulkanUsageTransferDst;

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_1.get(), true,
                                                         constraints_1.release());
  ASSERT_EQ(status, ZX_OK, "");

  // Client 2 connects to sysmem separately.
  zx::channel allocator2_client_2;
  status = connect_to_sysmem_driver(&allocator2_client_2);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client_2;
  zx::channel collection_server_2;
  status = zx::channel::create(0, &collection_client_2, &collection_server_2);
  ASSERT_EQ(status, ZX_OK, "");

  // Just because we can, perform this sync as late as possible, just before
  // the BindSharedCollection() via allocator2_client_2.  Without this Sync(),
  // the BindSharedCollection() might arrive at the server before the
  // Duplicate() that delivered the server end of token_client_2 to sysmem,
  // which would cause sysmem to not recognize the token.
  status = fuchsia_sysmem_BufferCollectionSync(collection_client_1.get());
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client_2.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator2_client_2.get(), token_client_2.release(), collection_server_2.release());
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client_2.get(), true,
                                                         constraints_2.release());
  ASSERT_EQ(status, ZX_OK, "");

  //
  // Only after both participants (both clients) have SetConstraints() will
  // the allocation be successful.
  //

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info_1(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client_1.get(), &allocation_status, buffer_collection_info_1.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_EQ(status, ZX_OK, "");

  // Success when at least one participant specifies "none" usage and at least
  // one participant specifies a usage other than "none".
  ASSERT_EQ(allocation_status, ZX_OK, "");
}

TEST(Sysmem, PixelFormatBgr24) {
  constexpr uint32_t kWidth = 600;
  constexpr uint32_t kHeight = 1;
  constexpr uint32_t kStride = kWidth * ZX_PIXEL_FORMAT_BYTES(ZX_PIXEL_FORMAT_RGB_888);
  constexpr uint32_t divisor = 32;
  constexpr uint32_t kStrideAlign = (kStride + divisor - 1) & ~(divisor - 1);
  zx_status_t status;
  zx::channel allocator2_client;
  status = connect_to_sysmem_driver(&allocator2_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator2_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator2_client.get(), token_client.release(), collection_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  constraints->min_buffer_count_for_camping = 3;
  constraints->has_buffer_memory_constraints = true;
  constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
      .min_size_bytes = kStride,
      .max_size_bytes = kStrideAlign,
      .physically_contiguous_required = false,
      .secure_required = false,
      .ram_domain_supported = true,
      .cpu_domain_supported = true,
      .inaccessible_domain_supported = false,
      .heap_permitted_count = 1,
      .heap_permitted = {fuchsia_sysmem_HeapType_SYSTEM_RAM}};
  constraints->image_format_constraints_count = 1;
  fuchsia_sysmem_ImageFormatConstraints& image_constraints =
      constraints->image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_BGR24;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] = fuchsia_sysmem_ColorSpace{
      .type = fuchsia_sysmem_ColorSpaceType_SRGB,
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

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");

  ASSERT_EQ(buffer_collection_info->buffer_count, 3, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, kStrideAlign, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
            fuchsia_sysmem_CoherencyDomain_CPU, "");
  // We specified image_format_constraints so the result must also have
  // image_format_constraints.
  ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, true, "");

  ASSERT_EQ(buffer_collection_info->settings.image_format_constraints.pixel_format.type,
            fuchsia_sysmem_PixelFormatType_BGR24, "");

  for (uint32_t i = 0; i < 64; ++i) {
    if (i < 3) {
      ASSERT_NE(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
      uint64_t size_bytes = 0;
      status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo, &size_bytes);
      ASSERT_EQ(status, ZX_OK, "");
      // The portion of the VMO the client can use is large enough to hold the min image size,
      // despite the min buffer size being smaller.
      ASSERT_GE(buffer_collection_info->settings.buffer_settings.size_bytes, kStrideAlign, "");
      // The vmo has room for the nominal size of the portion of the VMO the client can use.
      ASSERT_LE(buffer_collection_info->buffers[i].vmo_usable_start +
                    buffer_collection_info->settings.buffer_settings.size_bytes,
                size_bytes, "");
    } else {
      ASSERT_EQ(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
    }
  }

  zx_status_t close_status = fuchsia_sysmem_BufferCollectionClose(collection_client.get());
  EXPECT_EQ(close_status, ZX_OK, "");
}

// Test that closing a token handle that's had Close() called on it doesn't crash sysmem.
TEST(Sysmem, CloseToken) {
  zx_status_t status;
  zx::channel allocator_client;
  status = connect_to_sysmem_driver(&allocator_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token2_client;
  zx::channel token2_server;
  status = zx::channel::create(0, &token2_client, &token2_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_EQ(fuchsia_sysmem_BufferCollectionTokenDuplicate(token_client.get(), ZX_RIGHT_SAME_RIGHTS,
                                                          token2_server.release()),
            ZX_OK, "");

  ASSERT_EQ(fuchsia_sysmem_BufferCollectionTokenSync(token_client.get()), ZX_OK, "");
  ASSERT_EQ(fuchsia_sysmem_BufferCollectionTokenClose(token_client.get()), ZX_OK, "");
  token_client.reset();

  // Try to ensure sysmem processes the token closure before the sync.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));

  EXPECT_EQ(fuchsia_sysmem_BufferCollectionTokenSync(token2_client.get()), ZX_OK, "");
}

TEST(Sysmem, HeapAmlogicSecure) {
  if (!is_board_with_amlogic_secure()) {
    return;
  }

  for (uint32_t i = 0; i < 64; ++i) {
    zx::channel collection_client;
    zx_status_t status = make_single_participant_collection(&collection_client);
    ASSERT_EQ(status, ZX_OK, "");

    BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
    constraints->usage.video = fuchsia_sysmem_videoUsageHwDecoder;
    constexpr uint32_t kBufferCount = 4;
    constraints->min_buffer_count_for_camping = kBufferCount;
    constraints->has_buffer_memory_constraints = true;
    constexpr uint32_t kBufferSizeBytes = 64 * 1024;
    constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
        .min_size_bytes = kBufferSizeBytes,
        .max_size_bytes = 128 * 1024,
        .physically_contiguous_required = true,
        .secure_required = true,
        .ram_domain_supported = false,
        .cpu_domain_supported = false,
        .inaccessible_domain_supported = true,
        .heap_permitted_count = 1,
        .heap_permitted = {fuchsia_sysmem_HeapType_AMLOGIC_SECURE},
    };
    ZX_DEBUG_ASSERT(constraints->image_format_constraints_count == 0);
    status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                           constraints.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx_status_t allocation_status;
    BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        collection_client.get(), &allocation_status, buffer_collection_info.get());
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_EQ(allocation_status, ZX_OK, "");

    EXPECT_EQ(buffer_collection_info->buffer_count, kBufferCount, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, kBufferSizeBytes, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, true, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, true, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
              fuchsia_sysmem_CoherencyDomain_INACCESSIBLE, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.heap,
              fuchsia_sysmem_HeapType_AMLOGIC_SECURE, "");
    EXPECT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");

    for (uint32_t i = 0; i < 64; ++i) {
      if (i < kBufferCount) {
        EXPECT_NE(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
        uint64_t size_bytes = 0;
        status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo, &size_bytes);
        ASSERT_EQ(status, ZX_OK, "");
        EXPECT_EQ(size_bytes, kBufferSizeBytes, "");
      } else {
        EXPECT_EQ(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
      }
    }

    zx::vmo the_vmo = zx::vmo(buffer_collection_info->buffers[0].vmo);
    buffer_collection_info->buffers[0].vmo = ZX_HANDLE_INVALID;
    SecureVmoReadTester tester(std::move(the_vmo));
    ASSERT_DEATH(([&] { tester.AttemptReadFromSecure(); }));
    ASSERT_FALSE(tester.IsReadFromSecureAThing());
  }
}

TEST(Sysmem, HeapAmlogicSecureVdec) {
  if (!is_board_with_amlogic_secure_vdec()) {
    return;
  }

  for (uint32_t i = 0; i < 64; ++i) {
    zx::channel collection_client;
    zx_status_t status = make_single_participant_collection(&collection_client);
    ASSERT_EQ(status, ZX_OK, "");

    BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
    constraints->usage.video =
        fuchsia_sysmem_videoUsageDecryptorOutput | fuchsia_sysmem_videoUsageHwDecoder;
    constexpr uint32_t kBufferCount = 4;
    constraints->min_buffer_count_for_camping = kBufferCount;
    constraints->has_buffer_memory_constraints = true;
    constexpr uint32_t kBufferSizeBytes = 64 * 1024 - 1;
    constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
        .min_size_bytes = kBufferSizeBytes,
        .max_size_bytes = 128 * 1024,
        .physically_contiguous_required = true,
        .secure_required = true,
        .ram_domain_supported = false,
        .cpu_domain_supported = false,
        .inaccessible_domain_supported = true,
        .heap_permitted_count = 1,
        .heap_permitted = {fuchsia_sysmem_HeapType_AMLOGIC_SECURE_VDEC},
    };
    ZX_DEBUG_ASSERT(constraints->image_format_constraints_count == 0);
    status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                           constraints.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx_status_t allocation_status;
    BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        collection_client.get(), &allocation_status, buffer_collection_info.get());
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_EQ(allocation_status, ZX_OK, "");

    EXPECT_EQ(buffer_collection_info->buffer_count, kBufferCount, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, kBufferSizeBytes, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, true, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, true, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
              fuchsia_sysmem_CoherencyDomain_INACCESSIBLE, "");
    EXPECT_EQ(buffer_collection_info->settings.buffer_settings.heap,
              fuchsia_sysmem_HeapType_AMLOGIC_SECURE_VDEC, "");
    EXPECT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");

    auto expected_size = fbl::round_up(kBufferSizeBytes, ZX_PAGE_SIZE);
    for (uint32_t i = 0; i < 64; ++i) {
      if (i < kBufferCount) {
        EXPECT_NE(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
        uint64_t size_bytes = 0;
        status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo, &size_bytes);
        ASSERT_EQ(status, ZX_OK, "");
        EXPECT_EQ(size_bytes, expected_size, "");
      } else {
        EXPECT_EQ(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
      }
    }

    zx::vmo the_vmo = zx::vmo(buffer_collection_info->buffers[0].vmo);
    buffer_collection_info->buffers[0].vmo = ZX_HANDLE_INVALID;
    SecureVmoReadTester tester(std::move(the_vmo));
    ASSERT_DEATH(([&] { tester.AttemptReadFromSecure(); }));
    ASSERT_FALSE(tester.IsReadFromSecureAThing());
  }
}

TEST(Sysmem, CpuUsageAndInaccessibleDomainSupportedSucceeds) {
  zx::channel collection_client;
  zx_status_t status = make_single_participant_collection(&collection_client);
  ASSERT_EQ(status, ZX_OK, "");

  constexpr uint32_t kBufferCount = 3;
  constexpr uint32_t kBufferSize = 64 * 1024;
  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  constraints->min_buffer_count_for_camping = kBufferCount;
  constraints->has_buffer_memory_constraints = true;
  constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
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
  ZX_DEBUG_ASSERT(constraints->image_format_constraints_count == 0);
  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  // This is the first round-trip to/from sysmem.  A failure here can be due
  // to any step above failing async.
  ASSERT_EQ(status, ZX_OK, "");
  ASSERT_EQ(allocation_status, ZX_OK, "");

  ASSERT_EQ(buffer_collection_info->buffer_count, kBufferCount, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.size_bytes, kBufferSize, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_physically_contiguous, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.is_secure, false, "");
  ASSERT_EQ(buffer_collection_info->settings.buffer_settings.coherency_domain,
            fuchsia_sysmem_CoherencyDomain_CPU, "");
  ASSERT_EQ(buffer_collection_info->settings.has_image_format_constraints, false, "");

  for (uint32_t i = 0; i < 64; ++i) {
    if (i < kBufferCount) {
      ASSERT_NE(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
      uint64_t size_bytes = 0;
      status = zx_vmo_get_size(buffer_collection_info->buffers[i].vmo, &size_bytes);
      ASSERT_EQ(status, ZX_OK, "");
      ASSERT_EQ(size_bytes, kBufferSize, "");
    } else {
      ASSERT_EQ(buffer_collection_info->buffers[i].vmo, ZX_HANDLE_INVALID, "");
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
    zx::channel collection_client;
    zx_status_t status = make_single_participant_collection(&collection_client);
    ASSERT_EQ(status, ZX_OK, "");

    BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
    constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
    constraints->min_buffer_count_for_camping = kBufferCount;
    constraints->has_buffer_memory_constraints = true;
    constraints->buffer_memory_constraints = fuchsia_sysmem_BufferMemoryConstraints{
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
    ZX_DEBUG_ASSERT(constraints->image_format_constraints_count == 0);
    status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                           constraints.release());
    ASSERT_EQ(status, ZX_OK, "");

    zx_status_t allocation_status;
    BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        collection_client.get(), &allocation_status, buffer_collection_info.get());
    // This is the first round-trip to/from sysmem.  A failure here can be due
    // to any step above failing async.
    ASSERT_EQ(status, ZX_OK, "");
    ASSERT_EQ(allocation_status, ZX_OK, "");

    // We intentionally don't check a bunch of stuff here.  We assume that sysmem allocated
    // kBufferCount (1) buffer of kBufferSize (64 KiB).  That way we're comparing ASAP after buffer
    // allocation, in case that helps catch any failure to actually zero in RAM.  Ideally we'd read
    // using a DMA in this test instead of using CPU reads, but that wouldn't be a portable test.

    zx::vmo vmo(buffer_collection_info->buffers[0].vmo);
    buffer_collection_info->buffers[0].vmo = ZX_HANDLE_INVALID;

    // Before we read from the VMO, we need to invalidate cache for the VMO.  We do this via a
    // syscall since it seems like mapping would have a greater chance of doing a fence.
    // Unfortunately none of these steps are guarnteed not to hide a problem with flushing or fence
    // in sysmem...
    status = vmo.op_range(ZX_VMO_OP_CACHE_INVALIDATE, /*offset=*/0, kBufferSize, /*buffer=*/nullptr,
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
  zx_status_t status;
  zx::channel allocator_client;
  status = connect_to_sysmem_driver(&allocator_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client.get(), token_client.release(), collection_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  constraints->min_buffer_count_for_camping = 1;
  constraints->has_buffer_memory_constraints = false;
  constraints->image_format_constraints_count = 1;
  fuchsia_sysmem_ImageFormatConstraints& image_constraints =
      constraints->image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_NV12;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] = fuchsia_sysmem_ColorSpace{
      .type = fuchsia_sysmem_ColorSpaceType_REC709,
  };
  image_constraints.required_max_coded_width = 512;
  image_constraints.required_max_coded_height = 1024;

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  ASSERT_EQ(status, ZX_OK, "");

  size_t vmo_size;
  status = zx_vmo_get_size(buffer_collection_info->buffers[0].vmo, &vmo_size);
  ASSERT_EQ(status, ZX_OK, "");

  // Image must be at least 512x1024 NV12, due to the required max sizes
  // above.
  EXPECT_LE(512 * 1024 * 3 / 2, vmo_size);
}

// Perform a sync IPC to ensure the server is still alive.
static void VerifyServerAlive(const zx::channel& allocator_client) {
  zx::channel token_client;
  zx::channel token_server;
  zx_status_t status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");
  // Ensure server is still alive.
  status = fuchsia_sysmem_BufferCollectionTokenSync(token_client.get());
  EXPECT_EQ(status, ZX_OK, "");
}

// Check that the server validates how many image format constraints there are.
TEST(Sysmem, TooManyFormats) {
  zx_status_t status;
  zx::channel allocator_client;
  status = connect_to_sysmem_driver(&allocator_client);
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel token_client;
  zx::channel token_server;
  status = zx::channel::create(0, &token_client, &token_server);
  ASSERT_EQ(status, ZX_OK, "");

  status = fuchsia_sysmem_AllocatorAllocateSharedCollection(allocator_client.get(),
                                                            token_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx::channel collection_client;
  zx::channel collection_server;
  status = zx::channel::create(0, &collection_client, &collection_server);
  ASSERT_EQ(status, ZX_OK, "");

  ASSERT_NE(token_client.get(), ZX_HANDLE_INVALID, "");
  status = fuchsia_sysmem_AllocatorBindSharedCollection(
      allocator_client.get(), token_client.release(), collection_server.release());
  ASSERT_EQ(status, ZX_OK, "");

  BufferCollectionConstraints constraints(BufferCollectionConstraints::Default);
  constraints->usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
  constraints->min_buffer_count_for_camping = 1;
  constraints->has_buffer_memory_constraints = false;
  constraints->image_format_constraints_count = 100;
  for (uint32_t i = 0; i < 32; i++) {
    fuchsia_sysmem_ImageFormatConstraints& image_constraints =
        constraints->image_format_constraints[i];
    image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_NV12;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value = fuchsia_sysmem_FORMAT_MODIFIER_LINEAR;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0] = fuchsia_sysmem_ColorSpace{
        .type = fuchsia_sysmem_ColorSpaceType_REC709,
    };
    image_constraints.required_max_coded_width = 512;
    image_constraints.required_max_coded_height = 1024;
  }

  status = fuchsia_sysmem_BufferCollectionSetConstraints(collection_client.get(), true,
                                                         constraints.release());
  ASSERT_EQ(status, ZX_OK, "");

  zx_status_t allocation_status;
  BufferCollectionInfo buffer_collection_info(BufferCollectionInfo::Default);
  status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
      collection_client.get(), &allocation_status, buffer_collection_info.get());
  EXPECT_NE(status, ZX_OK, "");

  VerifyServerAlive(allocator_client);
}

// TODO(dustingreen): Add tests to cover more failure cases.
