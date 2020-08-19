// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls/object.h>

#include <cinttypes>
#include <cstdlib>
#include <memory>
#include <utility>

#include <mini-process/mini-process.h>
#include <zxtest/zxtest.h>

#include "helper.h"

namespace object_info_test {
namespace {
zx_status_t GetKoid(const zx::object_base& object, zx_koid_t* koid) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(object.get(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status == ZX_OK) {
    *koid = info.koid;
  }
  return status;
}

// Structs to keep track of the VMARs/mappings in test child process.
struct Mapping {
  uintptr_t base;
  size_t size = 0;
  // ZX_INFO_MAPS_MMU_FLAG_PERM_{READ,WRITE,EXECUTE}
  uint32_t flags;
};

// A VMO that the test process maps or has a handle to.
struct Vmo {
  zx_koid_t koid;
  size_t size = 0;
  uint32_t flags;
};

struct MappingInfo {
  uintptr_t vmar_base = 0;
  size_t vmar_size = 0;
  // num_mappings entries
  size_t num_mappings = 0;
  std::unique_ptr<Mapping[]> mappings = nullptr;
  // num_vmos entries
  size_t num_vmos = 0;
  std::unique_ptr<Vmo[]> vmos = 0;
};

class ProcessFixture : public zxtest::Test {
 public:
  static void SetUpTestCase() {
    // Create a VMO whose handle we'll give to the test process.
    // It will not be mapped into the test process's VMAR.
    zx::vmo unmapped_vmo;
    ASSERT_OK(zx::vmo::create(kUnmappedVmoSize, 0u, &unmapped_vmo),
              "Failed to create unmapped_vmo.");

    zx_koid_t unmapped_vmo_koid = ZX_KOID_INVALID;
    ASSERT_OK(GetKoid(unmapped_vmo, &unmapped_vmo_koid), "Failed to obtain koid");

    // Try to set the name, but ignore any errors.
    unmapped_vmo.set_property(ZX_PROP_NAME, kUnmappedVmoName, sizeof(kUnmappedVmoName));

    // Failures from here on will start to leak handles, but they'll
    // be cleaned up when this binary exits.

    ASSERT_OK(zx::process::create(*zx::job::default_job(), kProcessName, sizeof(kProcessName),
                                  /* options */ 0u, &process_, &vmar_));

    zx::thread thread;
    ASSERT_OK(zx::thread::create(process_, kThreadName, sizeof(kThreadName), 0u, &thread),
              "Failed to create thread.");

    zx::channel minip_channel;
    // Start the process before we mess with the VMAR,
    // so we don't step on the mapping done by start_mini_process_etc.
    ASSERT_OK(
        start_mini_process_etc(process_.get(), thread.get(), vmar_.get(), unmapped_vmo.release(),
                               true, minip_channel.reset_and_get_address()),
        "Failed to start mini process.");
    minip_channel.reset();

    // Create a child VMAR and a mapping under it, so we have
    // something interesting to look at when getting the process's
    // memory maps. After this, the process maps should at least contain:
    //
    //   Root Aspace
    //   - Root VMAR
    //     - Code+stack mapping created by start_mini_process_etc
    //     - Sub VMAR created below
    //       - kNumMappings mappings created below
    info_.num_mappings = kNumMappings;
    info_.mappings.reset(new Mapping[kNumMappings]);

    // Big enough to fit all of the mappings with some slop.
    info_.vmar_size = PAGE_SIZE * kNumMappings * 16;
    zx::vmar sub_vmar;

    ASSERT_OK(vmar_.allocate2(ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE | ZX_VM_CAN_MAP_EXECUTE, 0,
                              info_.vmar_size, &sub_vmar, &info_.vmar_base));

    zx::vmo vmo;
    constexpr size_t kVmoSize = PAGE_SIZE * kNumMappings;
    ASSERT_OK(zx::vmo::create(kVmoSize, 0u, &vmo), "Failed to create vmo.");

    zx_koid_t vmo_koid = ZX_KOID_INVALID;
    ASSERT_OK(GetKoid(vmo, &vmo_koid));

    // Try to set the name, but ignore any errors.
    static constexpr char kVmoName[] = "test:mapped";

    vmo.set_property(ZX_PROP_NAME, kVmoName, sizeof(kVmoName));

    // TODO(mdempsky): Restructure test to satisfy W^X.
    zx::vmo replace;
    vmo.replace_as_executable(zx::resource(), &replace);
    vmo = std::move(replace);

    // Record the VMOs now that we have both of them.
    info_.num_vmos = 2;
    info_.vmos.reset(new Vmo[2]);
    info_.vmos[0].koid = unmapped_vmo_koid;
    info_.vmos[0].size = kUnmappedVmoSize;
    info_.vmos[0].flags = ZX_INFO_VMO_VIA_HANDLE;
    info_.vmos[1].koid = vmo_koid;
    info_.vmos[1].size = kVmoSize;
    info_.vmos[1].flags = ZX_INFO_VMO_VIA_MAPPING;

    // Map each page of the VMO to some arbitray location in the VMAR.
    for (size_t i = 0; i < kNumMappings; ++i) {
      Mapping& m = info_.mappings[i];
      m.size = PAGE_SIZE;

      // Pick flags for this mapping; cycle through different
      // combinations for the test. Must always have READ set
      // to be mapped.
      m.flags = ZX_VM_PERM_READ;
      if (i & 1) {
        m.flags |= ZX_VM_PERM_WRITE;
      }
      if (i & 2) {
        m.flags |= ZX_VM_PERM_EXECUTE;
      }

      ASSERT_OK(sub_vmar.map(0, vmo, i * PAGE_SIZE, PAGE_SIZE, m.flags, &m.base),
                "zx::vmar::map [%zd]", i);
    }

    // Check that everything is ok.
    ASSERT_TRUE(process_.is_valid());
  }

  static void TearDownTestCase() {
    if (vmar_.is_valid()) {
      vmar_.destroy();
      vmar_.reset();
    }

    if (process_.is_valid()) {
      process_.kill();
      process_.reset();
    }
  }

  // Returns a process singleton. ZX_INFO_PROCESS_MAPS can't run on the current
  // process, so tests should use this instead.
  const zx::process& GetProcess() const { return process_; }

  // Return the MappingInfo for the process institated for this fixture.
  const MappingInfo& GetInfo() const { return info_; }

  const auto& GetHandleProvider() const { return handle_provider; }

 private:
  // Constants.
  static constexpr char kProcessName[] = "object-info-mini-proc";
  static constexpr char kUnmappedVmoName[] = "test:unmapped";
  static constexpr size_t kUnmappedVmoSize = PAGE_SIZE;
  static constexpr char kThreadName[] = "object-info-mini-thrd";
  static constexpr size_t kNumMappings = 8;

  // Singletons for the ProcessFixture
  static zx::process process_;
  static zx::vmar vmar_;
  static MappingInfo info_;

  fbl::Function<const zx::process&()> handle_provider = [this]() -> const zx::process& {
    return GetProcess();
  };
};

zx::vmar ProcessFixture::vmar_;
zx::process ProcessFixture::process_;
MappingInfo ProcessFixture::info_;

using ProcessGetInfoTest = ProcessFixture;

// Tests that ZX_INFO_PROCESS_MAPS does not return ZX_ERR_BAD_STATE
// when the process has not yet started.
TEST_F(ProcessGetInfoTest, InfoProcessMapsUnstartedSuceeds) {
  static constexpr char kProcessName[] = "object-info-unstarted";
  zx::vmar vmar;
  zx::process process;

  ASSERT_OK(zx::process::create(*zx::job::default_job(), kProcessName, sizeof(kProcessName),
                                /* options */ 0u, &process, &vmar));
  size_t actual, avail;
  zx_info_maps_t maps;
  EXPECT_OK(process.get_info(ZX_INFO_PROCESS_MAPS, &maps, 0, &actual, &avail));
}

// Tests that ZX_INFO_PROCESS_MAPS seems to work.
TEST_F(ProcessGetInfoTest, InfoProcessMapsSmokeTest) {
  const MappingInfo& test_info = GetInfo();
  const zx::process& process = GetProcess();

  // Buffer big enough to read all of the test process's map entries.
  const size_t entry_count = 4 * test_info.num_mappings;
  std::unique_ptr<zx_info_maps_t[]> maps(new zx_info_maps_t[entry_count]);

  // Read the map entries.
  size_t actual;
  size_t avail;
  ASSERT_OK(process.get_info(ZX_INFO_PROCESS_MAPS, static_cast<void*>(maps.get()),
                             entry_count * sizeof(zx_info_maps_t), &actual, &avail));
  EXPECT_EQ(actual, avail, "Should have read all entries");

  // The first two entries should always be the ASpace and root VMAR.
  ASSERT_GE(actual, 2u, "Root aspace/vmar missing?");
  EXPECT_EQ(maps[0].type, static_cast<uint32_t>(ZX_INFO_MAPS_TYPE_ASPACE));
  EXPECT_EQ(maps[0].depth, 0u, "ASpace depth");
  EXPECT_GT(maps[0].size, 1llu << 40, "ASpace size");
  EXPECT_EQ(maps[1].type, static_cast<uint32_t>(ZX_INFO_MAPS_TYPE_VMAR));
  EXPECT_EQ(maps[1].depth, 1u, "Root VMAR depth");
  EXPECT_GT(maps[1].size, 1llu << 40, "Root VMAR size");

  // Look for the VMAR and all of the mappings we created.

  // Whether we've seen our VMAR.
  bool saw_vmar = false;

  // If we're looking at children of our VMAR.
  bool under_vmar = false;
  size_t vmar_depth = 0;

  // bitmask of mapping indices we've seen.
  uint32_t saw_mapping = 0u;

  ASSERT_LT(test_info.num_mappings, 32u);

  // LTRACEF("\n");
  for (size_t i = 2; i < actual; i++) {
    const zx_info_maps_t& entry = maps[i];
    char msg[128];
    snprintf(msg, sizeof(msg), "[%2zd] %*stype:%u base:0x%" PRIx64 " size:%" PRIu64, i,
             (int)(entry.depth - 2) * 2, "", entry.type, entry.base, entry.size);
    // LTRACEF("%s\n", msg);
    // All entries should be children of the root VMAR.
    EXPECT_GT(entry.depth, 1u, "%s", msg);
    EXPECT_GE(entry.type, ZX_INFO_MAPS_TYPE_ASPACE, "%s", msg);
    EXPECT_LE(entry.type, ZX_INFO_MAPS_TYPE_MAPPING, "%s", msg);

    if (entry.type == ZX_INFO_MAPS_TYPE_VMAR && entry.base == test_info.vmar_base &&
        entry.size == test_info.vmar_size) {
      saw_vmar = true;
      under_vmar = true;
      vmar_depth = entry.depth;
    } else if (under_vmar) {
      if (entry.depth <= vmar_depth) {
        under_vmar = false;
        vmar_depth = 0;
      } else {
        // |entry| should be a child mapping of our VMAR.
        EXPECT_EQ((uint32_t)ZX_INFO_MAPS_TYPE_MAPPING, entry.type, "%s", msg);
        // The mapping should fit inside the VMAR.
        EXPECT_LE(test_info.vmar_base, entry.base, "%s", msg);
        EXPECT_LE(entry.base + entry.size, test_info.vmar_base + test_info.vmar_size, "%s", msg);
        // Look for it in the expected mappings.
        bool found = false;
        for (size_t j = 0; j < test_info.num_mappings; j++) {
          const Mapping* t = &test_info.mappings[j];
          if (t->base == entry.base && t->size == entry.size) {
            // Make sure we don't see duplicates.
            EXPECT_EQ(0u, saw_mapping & (1 << j), "%s", msg);

            saw_mapping |= 1 << j;
            EXPECT_EQ(t->flags, entry.u.mapping.mmu_flags, "%s", msg);
            found = true;
            break;
          }
        }
        EXPECT_TRUE(found, "%s", msg);
      }
    }
  }

  // Make sure we saw our VMAR and all of our mappings.
  EXPECT_TRUE(saw_vmar);
  EXPECT_EQ((uint32_t)(1 << test_info.num_mappings) - 1, saw_mapping);

  // Do one more read with a short buffer to test actual < avail.
  const size_t bufsize2 = actual * 3 / 4 * sizeof(zx_info_maps_t);
  std::unique_ptr<zx_info_maps_t[]> maps2(new zx_info_maps_t[bufsize2]);
  size_t actual2;
  size_t avail2;
  ASSERT_OK(process.get_info(ZX_INFO_PROCESS_MAPS, static_cast<void*>(maps2.get()), bufsize2,
                             &actual2, &avail2));
  EXPECT_LT(actual2, avail2);
  // mini-process is very simple, and won't have modified its own memory
  // maps since the previous dump. Its "committed_pages" values could be
  // different, though.
  EXPECT_EQ(avail, avail2);
  //    LTRACEF("\n");
  EXPECT_GT(actual2, 3u);  // Make sure we're looking at something.
  for (size_t i = 0; i < actual2; i++) {
    const zx_info_maps_t& e1 = maps[i];
    const zx_info_maps_t& e2 = maps2[i];

    char msg[128];
    snprintf(msg, sizeof(msg),
             "[%2zd] %*stype:%u/%u base:0x%" PRIx64 "/0x%" PRIx64 " size:%" PRIu64 "/%" PRIu64, i,
             (int)e1.depth * 2, "", e1.type, e2.type, e1.base, e2.base, e1.size, e2.size);
    //        LTRACEF("%s\n", msg);
    EXPECT_EQ(e1.base, e2.base, "%s", msg);
    EXPECT_EQ(e1.size, e2.size, "%s", msg);
    EXPECT_EQ(e1.depth, e2.depth, "%s", msg);
    EXPECT_EQ(e1.type, e2.type, "%s", msg);
    if (e1.type == e2.type && e2.type == ZX_INFO_MAPS_TYPE_MAPPING) {
      EXPECT_EQ(e1.u.mapping.mmu_flags, e2.u.mapping.mmu_flags, "%s", msg);
    }
  }
}

TEST_F(ProcessGetInfoTest, InfoProcessHandleStats) {
  zx_info_process_handle_stats_t info;
  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_PROCESS_HANDLE_STATS, &info, sizeof(info),
                                          nullptr, nullptr));

  EXPECT_EQ(info.handle_count[ZX_OBJ_TYPE_NONE], 0);
  EXPECT_GT(info.handle_count[ZX_OBJ_TYPE_PROCESS], 0);
  EXPECT_GT(info.handle_count[ZX_OBJ_TYPE_THREAD], 0);
  EXPECT_GT(info.handle_count[ZX_OBJ_TYPE_VMO], 0);
  EXPECT_EQ(info.handle_count[ZX_OBJ_TYPE_INTERRUPT], 0);

  uint32_t channel_count = info.handle_count[ZX_OBJ_TYPE_CHANNEL];

  // Verify updated correctly.
  zx::channel endpoint_1, endpoint_2;
  ASSERT_OK(zx::channel::create(0, &endpoint_1, &endpoint_2));

  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_PROCESS_HANDLE_STATS, &info, sizeof(info),
                                          nullptr, nullptr));

  EXPECT_EQ(info.handle_count[ZX_OBJ_TYPE_CHANNEL], channel_count + 2);
}

constexpr auto process_provider = []() -> const zx::process& {
  static const zx::unowned_process process = zx::process::self();
  return *process;
};

TEST_F(ProcessGetInfoTest, InfoProcessHandleTable) {
  zx_info_handle_extended_t handle_info[4] = {};
  auto& process = GetProcess();
  size_t actual = 0;
  size_t avail = 0;
  ASSERT_OK(
      process.get_info(ZX_INFO_HANDLE_TABLE, &handle_info, sizeof(handle_info), &actual, &avail));
  // Since the process is a mini-process we fully control the handles in SetUpTestCase() above.
  // Although the order of handles is a detail that is not guaranteed by the ABI. The handles are
  // instanciated in the order they are written (then ready) into the channel; if we ever change
  // that we need to change this test.
  EXPECT_EQ(actual, 2);
  EXPECT_EQ(avail, 2);
  EXPECT_EQ(handle_info[0].type, ZX_OBJ_TYPE_VMO);
  EXPECT_EQ(handle_info[1].type, ZX_OBJ_TYPE_CHANNEL);
  EXPECT_NE(handle_info[0].handle_value, ZX_HANDLE_INVALID);
  EXPECT_NE(handle_info[1].handle_value, ZX_HANDLE_INVALID);
  EXPECT_EQ(handle_info[0].related_koid, 0);
  EXPECT_GT(handle_info[1].related_koid, 0);
  EXPECT_EQ(handle_info[0].peer_owner_koid, 0);
  EXPECT_EQ(handle_info[1].peer_owner_koid, 0);
  EXPECT_GT(handle_info[0].koid, 0);
  EXPECT_GT(handle_info[1].koid, 0);
}

TEST_F(ProcessGetInfoTest, InfoProcessHandleTableInsufficientRights) {
  size_t avail = 0;
  zx::process selfie;
  // Create a process handle that is missing ZX_RIGHT_MANAGE_PROCESS.
  ASSERT_OK(zx::process::self()->duplicate(ZX_RIGHT_INSPECT | ZX_RIGHT_MANAGE_THREAD, &selfie));
  ASSERT_EQ(selfie.get_info(ZX_INFO_HANDLE_TABLE, nullptr, 0, nullptr, &avail),
            ZX_ERR_ACCESS_DENIED);
}

TEST_F(ProcessGetInfoTest, InfoProcessHandleTableEmpty) {
  // An empty process does not have any handles, but the syscall suceeds.
  zx::vmar vmar;
  zx::process process;
  ASSERT_OK(zx::process::create(*zx::job::default_job(), "", 0u, 0u, &process, &vmar));

  zx_info_handle_extended_t handle_info[4] = {};
  size_t actual = 0;
  size_t avail = 0;
  ASSERT_OK(
      process.get_info(ZX_INFO_HANDLE_TABLE, &handle_info, sizeof(handle_info), &actual, &avail));

  EXPECT_EQ(actual, 0);
  EXPECT_EQ(avail, 0);
}

TEST_F(ProcessGetInfoTest, InfoProcessHandleTableSelf) {
  // The current process can have many handles, in some configs upward of 70. Check
  // the pattern of calling twice works, with the first time just to know the size.
  size_t avail = 0;
  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_HANDLE_TABLE, nullptr, 0, nullptr, &avail));
  ASSERT_GT(avail, 10);
  size_t actual = 0;
  // In the second syscall there is a slack of 4 handles in case another thread has allocated
  // and object. We could loop until the call succeeds but this can mask other problems.
  avail += 4u;
  auto size = avail * sizeof(zx_info_handle_extended_t);
  std::unique_ptr<zx_info_handle_extended_t[]> handle_info(new zx_info_handle_extended_t[avail]);
  ASSERT_OK(zx::process::self()->get_info(ZX_INFO_HANDLE_TABLE, handle_info.get(), size, &actual,
                                          &avail));
  ASSERT_GE(actual, 10);
  ASSERT_EQ(actual, avail);
  // We don't know exactly what handles we have but we can do some basic checking.
  for (size_t ix = 0; ix != actual; ++ix) {
    EXPECT_NE(handle_info[ix].handle_value, ZX_HANDLE_INVALID);
    EXPECT_GT(handle_info[ix].koid, 0);
    EXPECT_NE(handle_info[ix].type, ZX_OBJ_TYPE_NONE);
    switch (handle_info[ix].type) {
      case ZX_OBJ_TYPE_CHANNEL:
      case ZX_OBJ_TYPE_SOCKET:
      case ZX_OBJ_TYPE_EVENTPAIR:
      case ZX_OBJ_TYPE_FIFO:
      case ZX_OBJ_TYPE_THREAD:
      case ZX_OBJ_TYPE_PROCESS:
        EXPECT_GT(handle_info[ix].related_koid, 0);
        break;
      case ZX_OBJ_TYPE_JOB:
        // Jobs can have |related_koid| zero or not, depending if it is the root job.
        break;
      default:
        EXPECT_EQ(handle_info[ix].related_koid, 0);
        break;
    }
  }
}

TEST_F(ProcessGetInfoTest, InfoProcessHandleTableInvalidHandleFails) {
  ASSERT_NO_FATAL_FAILURES((CheckInvalidHandleFails<zx_info_handle_extended_t>(
      ZX_INFO_HANDLE_TABLE, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessHandleTableNullAvailSuceeds) {
  ASSERT_NO_FATAL_FAILURES((CheckNullAvailSuceeds<zx_info_handle_extended_t>(
      ZX_INFO_HANDLE_TABLE, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessHandleTableNullActualAvailSuceeds) {
  ASSERT_NO_FATAL_FAILURES((CheckNullActualAndAvailSuceeds<zx_info_handle_extended_t>(
      ZX_INFO_HANDLE_TABLE, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessHandleTableInvalidBufferPointerFails) {
  ASSERT_NO_FATAL_FAILURES((CheckNullActualSuceeds<zx_info_handle_extended_t>(
      ZX_INFO_HANDLE_TABLE, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessMapsOnSelfFails) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckSelfInfoSuceeds<zx_info_maps_t>(ZX_INFO_PROCESS_MAPS, 1, process_provider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessMapsInvalidHandleFails) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckInvalidHandleFails<zx_info_maps_t>(ZX_INFO_PROCESS_MAPS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessMapsNullAvailSucceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullAvailSuceeds<zx_info_maps_t>(ZX_INFO_PROCESS_MAPS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessMapsNullActualSucceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_info_maps_t>(ZX_INFO_PROCESS_MAPS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessMapsNullActualAndAvailSucceeds) {
  ASSERT_NO_FATAL_FAILURES((CheckNullActualAndAvailSuceeds<zx_info_maps_t>(ZX_INFO_PROCESS_MAPS, 1,
                                                                           GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessMapsInvalidBufferPointerFails) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_info_maps_t>(ZX_INFO_PROCESS_MAPS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessMapsBadActualgIsInvalidArg) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_info_maps_t>(ZX_INFO_PROCESS_MAPS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessMapsBadAvailIsInvalidArg) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_info_maps_t>(ZX_INFO_PROCESS_MAPS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessMapsZeroSizedBufferIsOk) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckZeroSizeBufferSucceeds<zx_info_maps_t>(ZX_INFO_PROCESS_MAPS, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessMapsSmallBufferIsOk) {
  // We use only one entry count, because we know that the process created at the fixture has more
  // mappings.
  ASSERT_NO_FATAL_FAILURES(
      (CheckSmallBufferSucceeds<zx_info_maps_t>(ZX_INFO_PROCESS_MAPS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessMapsPartiallyUnmappedBufferIsInvalidArgs) {
  ASSERT_NO_FATAL_FAILURES((CheckPartiallyUnmappedBufferIsError<zx_info_maps_t>(
      ZX_INFO_PROCESS_MAPS, GetHandleProvider(), ZX_ERR_INVALID_ARGS)));
}

TEST_F(ProcessGetInfoTest, InfoProcessMapsRequiresInspectRights) {
  ASSERT_NO_FATAL_FAILURES(CheckMissingRightsFail<zx_info_maps_t>(
      ZX_INFO_PROCESS_MAPS, 32, ZX_RIGHT_INSPECT, GetHandleProvider()));
}

constexpr auto job_provider = []() -> const zx::job& {
  const static zx::unowned_job job = zx::job::default_job();
  return *job;
};

TEST_F(ProcessGetInfoTest, InfoProcessMapsJobHandleIsBadHandle) {
  ASSERT_NO_FATAL_FAILURES(
      CheckWrongHandleTypeFails<zx_info_maps_t>(ZX_INFO_PROCESS_MAPS, 32, job_provider));
}

constexpr auto thread_provider = []() -> const zx::thread& {
  const static zx::unowned_thread thread = zx::thread::self();
  return *thread;
};

TEST_F(ProcessGetInfoTest, InfoProcessMapsThreadHandleIsBadHandle) {
  ASSERT_NO_FATAL_FAILURES(
      CheckWrongHandleTypeFails<zx_info_maps_t>(ZX_INFO_PROCESS_MAPS, 32, thread_provider));
}

// Tests that ZX_INFO_PROCESS_VMOS seems to work.
TEST_F(ProcessGetInfoTest, InfoProcessVmosSmokeTest) {
  const MappingInfo& test_info = GetInfo();
  const zx::process& process = GetProcess();

  // Buffer big enough to read all of the test process's VMO entries.
  // There'll be one per mapping, one for the unmapped VMO, plus some
  // extras (at least the vDSO and the mini-process stack).
  const size_t entry_count = (test_info.num_mappings + 1 + 8);
  std::unique_ptr<zx_info_vmo_t[]> vmos(new zx_info_vmo_t[entry_count]);

  // Read the VMO entries.
  size_t actual;
  size_t available;
  ASSERT_OK(process.get_info(ZX_INFO_PROCESS_VMOS, vmos.get(), entry_count * sizeof(zx_info_vmo_t),
                             &actual, &available));
  EXPECT_EQ(actual, available, "Should have read all entries");

  // Look for the expected VMOs.
  uint32_t saw_vmo = 0u;  // Bitmask of VMO indices we've seen
  ASSERT_LT(test_info.num_vmos, 32u);

  // LTRACEF("\n");
  for (size_t i = 0; i < actual; i++) {
    const zx_info_vmo_t& entry = vmos[i];
    char msg[128];
    snprintf(msg, sizeof(msg),
             "[%2zd] koid:%" PRIu64 " name:'%s' size:%" PRIu64 " flags:0x%" PRIx32, i, entry.koid,
             entry.name, entry.size_bytes, entry.flags);
    //      LTRACEF("%s\n", msg);

    // Look for it in the expected VMOs. We won't find all VMOs here,
    // since we don't track the vDSO or mini-process stack.
    for (size_t j = 0; j < test_info.num_vmos; j++) {
      const Vmo& t = test_info.vmos[j];
      if (t.koid == entry.koid && t.size == entry.size_bytes) {
        // These checks aren't appropriate for all VMOs.
        // The VMOs we track are:
        // - Only mapped or via handle, not both
        // - Not clones
        // - Not shared
        EXPECT_EQ(entry.parent_koid, 0u, "%s", msg);
        EXPECT_EQ(entry.num_children, 0u, "%s", msg);
        EXPECT_EQ(entry.share_count, 1u, "%s", msg);
        EXPECT_EQ(t.flags & entry.flags, t.flags, "%s", msg);
        if (entry.flags & ZX_INFO_VMO_VIA_HANDLE) {
          EXPECT_EQ(entry.num_mappings, 0u, "%s", msg);
        } else {
          EXPECT_NE(entry.flags & ZX_INFO_VMO_VIA_MAPPING, 0u, "%s", msg);
          EXPECT_EQ(entry.num_mappings, test_info.num_mappings, "%s", msg);
        }
        EXPECT_EQ(entry.flags & ZX_INFO_VMO_IS_COW_CLONE, 0u, "%s", msg);

        saw_vmo |= 1 << j;  // Duplicates are fine and expected
        break;
      }
    }

    // All of our VMOs should be paged, not physical.
    EXPECT_EQ(ZX_INFO_VMO_TYPE(entry.flags), ZX_INFO_VMO_TYPE_PAGED, "%s", msg);

    // Each entry should be via either map or handle, but not both.
    // NOTE: This could change in the future, but currently reflects
    // the way things work.
    const uint32_t kViaMask = ZX_INFO_VMO_VIA_HANDLE | ZX_INFO_VMO_VIA_MAPPING;
    EXPECT_NE(entry.flags & kViaMask, kViaMask, "%s", msg);

    // TODO(dbort): Test more fields/flags of zx_info_vmo_t by adding some
    // clones, shared VMOs, mapped+handle VMOs, physical VMOs if possible.
    // All but committed_bytes should be predictable.
  }

  // Make sure we saw all of the expected VMOs.
  EXPECT_EQ(static_cast<uint32_t>(1ull << test_info.num_vmos) - 1, saw_vmo);

  // Do one more read with a short buffer to test actual < avail.
  const size_t entry_count_2 = actual * 3 / 4;
  std::unique_ptr<zx_info_vmo_t[]> vmos_2(new zx_info_vmo_t[entry_count_2]);
  size_t actual_2;
  size_t available_2;

  ASSERT_OK(process.get_info(ZX_INFO_PROCESS_VMOS, vmos_2.get(),
                             entry_count_2 * sizeof(zx_info_vmo_t), &actual_2, &available_2));
  EXPECT_LT(actual_2, available_2);

  // mini-process is very simple, and won't have modified its own set of VMOs
  // since the previous dump.
  EXPECT_EQ(available, available_2);

  // Make sure we're looking at something.
  EXPECT_GT(actual_2, 3u);
  for (size_t i = 0; i < actual_2; i++) {
    const zx_info_vmo_t& e1 = vmos[i];
    const zx_info_vmo_t& e2 = vmos_2[i];
    char msg[128];
    snprintf(msg, sizeof(msg),
             "[%2zd] koid:%" PRIu64 "/%" PRIu64
             " name:'%s'/'%s' "
             "size:%" PRIu64 "/%" PRIu64 " flags:0x%" PRIx32 "/0x%" PRIx32,
             i, e1.koid, e2.koid, e1.name, e2.name, e1.size_bytes, e2.size_bytes, e1.flags,
             e2.flags);
    //  LTRACEF("%s\n", "%s", msg);
    EXPECT_EQ(e1.koid, e2.koid, "%s", msg);
    EXPECT_EQ(e1.size_bytes, e2.size_bytes, "%s", msg);
    EXPECT_EQ(e1.flags, e2.flags, "%s", msg);
    if (e1.flags == e2.flags && e2.flags & ZX_INFO_VMO_VIA_HANDLE) {
      EXPECT_EQ(e1.handle_rights, e2.handle_rights, "%s", msg);
    }
  }
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosOnSelfFails) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckSelfInfoSuceeds<zx_info_vmo_t>(ZX_INFO_PROCESS_VMOS, 1, process_provider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosInvalidHandleFails) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckInvalidHandleFails<zx_info_vmo_t>(ZX_INFO_PROCESS_VMOS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosNullAvailSucceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullAvailSuceeds<zx_info_vmo_t>(ZX_INFO_PROCESS_VMOS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosNullActualSucceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_info_vmo_t>(ZX_INFO_PROCESS_VMOS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosNullActualAndAvailSucceeds) {
  ASSERT_NO_FATAL_FAILURES((
      CheckNullActualAndAvailSuceeds<zx_info_vmo_t>(ZX_INFO_PROCESS_VMOS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosInvalidBufferPointerFails) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_info_vmo_t>(ZX_INFO_PROCESS_VMOS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosBadActualgIsInvalidArg) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_info_vmo_t>(ZX_INFO_PROCESS_VMOS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosBadAvailIsInvalidArg) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_info_vmo_t>(ZX_INFO_PROCESS_VMOS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosZeroSizedBufferIsOk) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckZeroSizeBufferSucceeds<zx_info_vmo_t>(ZX_INFO_PROCESS_VMOS, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosSmallBufferIsOk) {
  // We use only one entry count, because we know that the process created at the fixture has more
  // mappings.
  ASSERT_NO_FATAL_FAILURES(
      (CheckSmallBufferSucceeds<zx_info_vmo_t>(ZX_INFO_PROCESS_VMOS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosPartiallyUnmappedBufferIsInvalidArgs) {
  ASSERT_NO_FATAL_FAILURES((CheckPartiallyUnmappedBufferIsError<zx_info_vmo_t>(
      ZX_INFO_PROCESS_VMOS, GetHandleProvider(), ZX_ERR_INVALID_ARGS)));
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosRequiresInspectRights) {
  ASSERT_NO_FATAL_FAILURES(CheckMissingRightsFail<zx_info_vmo_t>(
      ZX_INFO_PROCESS_VMOS, 32, ZX_RIGHT_INSPECT, GetHandleProvider()));
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosJobHandleIsBadHandle) {
  ASSERT_NO_FATAL_FAILURES(
      CheckWrongHandleTypeFails<zx_info_vmo_t>(ZX_INFO_PROCESS_VMOS, 32, job_provider));
}

TEST_F(ProcessGetInfoTest, InfoProcessVmosThreadHandleIsBadHandle) {
  ASSERT_NO_FATAL_FAILURES(
      CheckWrongHandleTypeFails<zx_info_vmo_t>(ZX_INFO_PROCESS_VMOS, 32, thread_provider));
}

TEST_F(ProcessGetInfoTest, InfoHandleBasicOnSelfSuceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckSelfInfoSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, process_provider())));
}

TEST_F(ProcessGetInfoTest, InfoHandleBasicInvalidHandleFails) {
  ASSERT_NO_FATAL_FAILURES((CheckInvalidHandleFails<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1,
                                                                            GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoHandleBasicNullAvailSucceeds) {
  ASSERT_NO_FATAL_FAILURES((
      CheckNullAvailSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoHandleBasicNullActualSucceeds) {
  ASSERT_NO_FATAL_FAILURES((CheckNullActualSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1,
                                                                           GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoHandleBasicNullActualAndAvailSucceeds) {
  ASSERT_NO_FATAL_FAILURES((CheckNullActualAndAvailSuceeds<zx_info_handle_basic_t>(
      ZX_INFO_HANDLE_BASIC, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoHandleBasicInvalidBufferPointerFails) {
  ASSERT_NO_FATAL_FAILURES((CheckNullActualSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1,
                                                                           GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoHandleBasicBadActualgIsInvalidArg) {
  ASSERT_NO_FATAL_FAILURES((CheckNullActualSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1,
                                                                           GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoHandleBasicBadAvailIsInvalidArg) {
  ASSERT_NO_FATAL_FAILURES((CheckNullActualSuceeds<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, 1,
                                                                           GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoHandleBasicZeroSizedFails) {
  ASSERT_NO_FATAL_FAILURES((
      CheckZeroSizeBufferFails<zx_info_handle_basic_t>(ZX_INFO_HANDLE_BASIC, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessOnSelfSuceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckSelfInfoSuceeds<zx_info_process_t>(ZX_INFO_PROCESS, 1, process_provider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessInvalidHandleFails) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckInvalidHandleFails<zx_info_process_t>(ZX_INFO_PROCESS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessNullAvailSucceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullAvailSuceeds<zx_info_process_t>(ZX_INFO_PROCESS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessNullActualSucceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_info_process_t>(ZX_INFO_PROCESS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessNullActualAndAvailSucceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualAndAvailSuceeds<zx_info_process_t>(ZX_INFO_PROCESS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessInvalidBufferPointerFails) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_info_process_t>(ZX_INFO_PROCESS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessBadActualgIsInvalidArg) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_info_process_t>(ZX_INFO_PROCESS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessBadAvailIsInvalidArg) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_info_process_t>(ZX_INFO_PROCESS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessZeroSizedBufferFails) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckZeroSizeBufferFails<zx_info_process_t>(ZX_INFO_PROCESS, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessJobHandleIsBadHandle) {
  ASSERT_NO_FATAL_FAILURES(
      CheckWrongHandleTypeFails<zx_info_process_t>(ZX_INFO_PROCESS, 32, job_provider));
}

// As reference from previous object-info test.
// ZX_INFO_PROCESS_THREADS tests.
// TODO(dbort): Use RUN_MULTI_ENTRY_TESTS instead. |short_buffer_succeeds| and
// |partially_unmapped_buffer_fails| currently fail because those tests expect
// avail > 1, but the test process only has one thread and it's not trivial to
// add more.
TEST_F(ProcessGetInfoTest, InfoProcessThreadHandleIsBadHandle) {
  ASSERT_NO_FATAL_FAILURES(
      CheckWrongHandleTypeFails<zx_info_process_t>(ZX_INFO_PROCESS, 32, thread_provider));
}

TEST_F(ProcessGetInfoTest, InfoProcessThreadsSelfSuceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckSelfInfoSuceeds<zx_koid_t>(ZX_INFO_PROCESS_THREADS, 1, process_provider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessThreadsInvalidHandleFails) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckInvalidHandleFails<zx_koid_t>(ZX_INFO_PROCESS_THREADS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessThreadsNullAvailSucceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullAvailSuceeds<zx_koid_t>(ZX_INFO_PROCESS_THREADS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessThreadsNullActualSucceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_koid_t>(ZX_INFO_PROCESS_THREADS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessThreadsNullActualAndAvailSucceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualAndAvailSuceeds<zx_koid_t>(ZX_INFO_PROCESS_THREADS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessThreadsInvalidBufferPointerFails) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_koid_t>(ZX_INFO_PROCESS_THREADS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessThreadsBadActualgIsInvalidArg) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_koid_t>(ZX_INFO_PROCESS_THREADS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessThreadsBadAvailIsInvalidArg) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckNullActualSuceeds<zx_koid_t>(ZX_INFO_PROCESS_THREADS, 1, GetHandleProvider())));
}

TEST_F(ProcessGetInfoTest, InfoProcessThreadsZeroSizedBufferSucceeds) {
  ASSERT_NO_FATAL_FAILURES(
      (CheckZeroSizeBufferSucceeds<zx_koid_t>(ZX_INFO_PROCESS_THREADS, GetHandleProvider())));
}

}  // namespace
}  // namespace object_info_test
