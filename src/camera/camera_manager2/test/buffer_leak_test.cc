// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fzl/memory-probe.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>

#include <vector>

#include <gtest/gtest.h>
#include <src/lib/syslog/cpp/logger.h>

namespace camera {
namespace {

constexpr uint32_t kCampingBufferCount = 2;

// If enabled, verify all mapped pages. If disabled, verifies first and last page only.
// TODO(fxb/42252): support memory-probe range checks
constexpr bool kEnableFullMemoryCheck = false;

class BufferLeakTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    context_ = sys::ComponentContext::Create();
    ASSERT_EQ(context_->svc()->Connect(allocator_.NewRequest()), ZX_OK);
    allocator_.set_error_handler(MakeErrorHandler("Sysmem Allocator"));
    RunLoopUntilIdle();
  }

  void TearDown() override {
    allocator_ = nullptr;
    context_ = nullptr;
    RunLoopUntilIdle();
  }

  // NOTE: The commented out streams below are causing the tests to
  //       timeout. The timeout is caused because the frames are not
  //       getting released somewhere in the pipeline.
  //       TODO(43029): Enable buffer leak tests for all streams supported
  //       by HAL.
  static std::vector<fuchsia::camera2::StreamConstraints> GetStreamConstraints() {
    fuchsia::camera2::CameraStreamType types[] = {
      fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
      fuchsia::camera2::CameraStreamType::MACHINE_LEARNING |
          fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
#if 0
        fuchsia::camera2::CameraStreamType::MACHINE_LEARNING |
        fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION,
#endif
      fuchsia::camera2::CameraStreamType::MONITORING,
#if 0
      fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE |
          fuchsia::camera2::CameraStreamType::MACHINE_LEARNING |
          fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
#endif
    };
    std::vector<fuchsia::camera2::StreamConstraints> constraints_ret;
    for (auto type : types) {
      fuchsia::camera2::StreamProperties properties;
      properties.set_stream_type(type);
      fuchsia::camera2::StreamConstraints constraints;
      constraints.set_properties(std::move(properties));
      constraints_ret.push_back(std::move(constraints));
    }
    return constraints_ret;
  }

  static fuchsia::sysmem::BufferCollectionConstraints GetCollectionConstraints() {
    fuchsia::sysmem::BufferCollectionConstraints constraints;
    constraints.min_buffer_count_for_camping = kCampingBufferCount;
    constraints.image_format_constraints_count = 0;
    constraints.usage.cpu = fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageWrite;
    return constraints;
  }

  static fit::function<void(zx_status_t status)> MakeErrorHandler(std::string server) {
    return [server](zx_status_t status) {
      ADD_FAILURE() << server << " server disconnected - " << status;
    };
  }

  // Verify whether the process has access to the memory reported by buffers consistent with format.
  static void VerifyAccess(const fuchsia::sysmem::BufferCollectionInfo_2& buffers,
                           const fuchsia::sysmem::ImageFormat_2& format) {
    fzl::VmoMapper mapper;
    for (uint32_t i = 0; i < buffers.buffer_count; ++i) {
      uint32_t bad_page_reads = 0;
      uint32_t bad_page_writes = 0;
      ASSERT_EQ(mapper.Map(buffers.buffers[i].vmo, buffers.buffers[i].vmo_usable_start,
                           buffers.settings.buffer_settings.size_bytes,
                           ZX_VM_PERM_READ | ZX_VM_PERM_WRITE),
                ZX_OK);
      if (kEnableFullMemoryCheck) {
        for (uint32_t row = 0; row < format.coded_height; ++row) {
          for (uint32_t offset = 0; offset < format.bytes_per_row; offset += PAGE_SIZE) {
            char* test_addr = static_cast<char*>(mapper.start()) + offset;
            if (!probe_for_read(test_addr)) {
              ++bad_page_reads;
            }
            if (!probe_for_write(test_addr)) {
              ++bad_page_writes;
            }
          }
        }
      } else {
        char* addr_first = static_cast<char*>(mapper.start());
        char* addr_last = addr_first + format.bytes_per_row * format.coded_height - 1;
        if (!probe_for_read(addr_first) || !probe_for_read(addr_last)) {
          ++bad_page_reads;
        }
        if (!probe_for_write(addr_first) || !probe_for_write(addr_last)) {
          ++bad_page_writes;
        }
      }
      EXPECT_EQ(bad_page_reads, 0u) << "Buffer " << i << " memory not readable";
      EXPECT_EQ(bad_page_writes, 0u) << "Buffer " << i << " memory not writable";
      mapper.Unmap();
    }
  }

  // Attempts to connect to the camera stream using the given constraints. Sets oom_out to true if
  // sysmem reports oom during allocation. Returns the allocated buffers in buffers_out.
  // Note that these are out params instead of return values due to the GTEST macros requiring
  // methods to return void.
  void TryConnect(fuchsia::camera2::StreamConstraints stream_constraints, bool* oom_out,
                  fuchsia::sysmem::BufferCollectionInfo_2* buffers_out) {
    ZX_ASSERT(oom_out);
    ZX_ASSERT(buffers_out);

    *oom_out = false;
    *buffers_out = fuchsia::sysmem::BufferCollectionInfo_2{};

    fuchsia::camera2::ManagerPtr manager;
    ASSERT_EQ(context_->svc()->Connect(manager.NewRequest()), ZX_OK);
    manager.set_error_handler(MakeErrorHandler("Camera Manager"));

    fuchsia::sysmem::BufferCollectionTokenPtr token;
    allocator_->AllocateSharedCollection(token.NewRequest());
    token.set_error_handler(MakeErrorHandler("Token"));

    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> manager_token;
    token->Duplicate(ZX_RIGHT_SAME_RIGHTS, manager_token.NewRequest());

    fuchsia::sysmem::BufferCollectionPtr collection;
    allocator_->BindSharedCollection(std::move(token), collection.NewRequest());
    collection.set_error_handler(MakeErrorHandler("Collection"));
    collection->SetConstraints(true, GetCollectionConstraints());
    bool sync_returned = false;
    collection->Sync([&]() { sync_returned = true; });
    while (!sync_returned) {
      RunLoopUntilIdle();
    }

    fuchsia::camera2::StreamPtr stream;
    fuchsia::sysmem::ImageFormat_2 format_ret;
    bool connect_returned = false;
    manager->ConnectToStream(0, std::move(stream_constraints), std::move(manager_token),
                             stream.NewRequest(), [&](fuchsia::sysmem::ImageFormat_2 format) {
                               format_ret = std::move(format);
                               connect_returned = true;
                             });

    fuchsia::sysmem::BufferCollectionInfo_2 buffers_ret;
    bool wait_returned = false;
    collection->WaitForBuffersAllocated(
        [&](zx_status_t status, fuchsia::sysmem::BufferCollectionInfo_2 buffers) {
          wait_returned = true;
          if (status == ZX_ERR_NO_MEMORY) {
            *oom_out = true;
          } else {
            ASSERT_EQ(status, ZX_OK) << "Failed to allocate buffers";
          }
          buffers_ret = std::move(buffers);
        });

    while (!wait_returned || !connect_returned) {
      RunLoopUntilIdle();
    }
    if (*oom_out) {
      return;
    }

    collection->Close();

    RunLoopUntilIdle();

    ASSERT_GE(buffers_ret.buffer_count, kCampingBufferCount);
    VerifyAccess(buffers_ret, format_ret);

    bool frame_received = false;
    stream.events().OnFrameAvailable = [&](fuchsia::camera2::FrameAvailableInfo info) {
      stream->ReleaseFrame(info.buffer_id);
      frame_received = true;
    };
    stream->Start();
    while (!frame_received) {
      RunLoopUntilIdle();
    }

    collection = nullptr;
    stream = nullptr;

    RunLoopUntilIdle();

    *buffers_out = std::move(buffers_ret);
  }

  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sysmem::AllocatorPtr allocator_;
};

TEST_F(BufferLeakTest, RepeatConnections) {
  // Repeatedly connect to streams until the total memory allocated is at least twice the size of
  // all physical memory.
  uint64_t allocation_sum = 0;
  uint64_t allocation_sum_target = zx_system_get_physmem() * 2;
  for (uint32_t i = 0; allocation_sum < allocation_sum_target; ++i) {
    for (auto& constraints : GetStreamConstraints()) {
      uint32_t percent_complete = (allocation_sum * 100) / allocation_sum_target;
      std::cout << "\rAllocated " << allocation_sum << " of " << allocation_sum_target << " bytes ("
                << percent_complete << "%)";
      std::cout.flush();
      bool oom_ret = false;
      fuchsia::sysmem::BufferCollectionInfo_2 buffers_ret{};
      TryConnect(std::move(constraints), &oom_ret, &buffers_ret);
      ASSERT_FALSE(HasFatalFailure());
      ASSERT_FALSE(oom_ret) << "SYSMEM OUT OF MEMORY at iteration " << i << " after allocating "
                            << allocation_sum << " bytes";
      allocation_sum += buffers_ret.buffer_count * buffers_ret.settings.buffer_settings.size_bytes;
    }
  }
  std::cout << std::endl;
}

}  // namespace
}  // namespace camera
