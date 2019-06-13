// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../../../isp/modules/dma-format.h"
#include "../task.h"
#include <cstddef>
#include <cstdint>
#include <ddk/debug.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fake-bti/bti.h>
#include <lib/syslog/global.h>
#include <stdint.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <zxtest/zxtest.h>

namespace gdc {
namespace {

constexpr uint32_t kWidth = 1080;
constexpr uint32_t kHeight = 764;
constexpr uint32_t kNumberOfBuffers = 4;
constexpr uint32_t kConfigSize = 1000;

// TODO(CAM-56): Once we have this moved to common code,
// re-use that instead of this local API for
// creating Fake Buffer Collection.
zx_status_t CreateFakeBufferCollection(buffer_collection_info_t* buffer_collection,
                                       zx_handle_t bti_handle, uint32_t width, uint32_t height,
                                       uint32_t num_buffers) {

    buffer_collection->format.image = {.width = width,
                                       .height = height,
                                       .layers = 2u,
                                       .pixel_format =
                                           {
                                               .type = fuchsia_sysmem_PixelFormatType_NV12,
                                               .has_format_modifier = false,
                                               .format_modifier = {.value = 0},
                                           },
                                       .color_space =
                                           {
                                               .type = fuchsia_sysmem_ColorSpaceType_SRGB,
                                           },
                                       // The planes data is not used currently:
                                       .planes = {{0, 0}, {0, 0}, {0, 0}, {0, 0}}};
    buffer_collection->buffer_count = num_buffers;
    // Get the image size for the vmo:
    camera::DmaFormat format(buffer_collection->format.image);
    buffer_collection->vmo_size = format.GetImageSize();
    zx_status_t status;
    for (uint32_t i = 0; i < buffer_collection->buffer_count; ++i) {
        status = zx_vmo_create_contiguous(bti_handle, buffer_collection->vmo_size, 0,
                                          &buffer_collection->vmos[i]);
        if (status != ZX_OK) {
            FX_LOG(ERROR, "", "Failed to allocate Buffer Collection");
            return status;
        }
    }
    return ZX_OK;
}

// Integration test for the driver defined in zircon/system/dev/camera/arm-isp.
class TaskTest : public zxtest::Test {
protected:
    void SetUpBufferCollections(uint32_t buffer_collection_count) {

        ASSERT_OK(fake_bti_create(&bti_handle_));

        zx_status_t status = CreateFakeBufferCollection(&input_buffer_collection_,
                                                        bti_handle_,
                                                        kWidth,
                                                        kHeight,
                                                        buffer_collection_count);
        ASSERT_OK(status);

        status = CreateFakeBufferCollection(&output_buffer_collection_,
                                            bti_handle_,
                                            kWidth,
                                            kHeight,
                                            buffer_collection_count);
        ASSERT_OK(status);

        status = zx_vmo_create_contiguous(bti_handle_, kConfigSize, 0, &config_vmo_);
        ASSERT_OK(status);
    }

    void TearDown() override {
        if (bti_handle_ != ZX_HANDLE_INVALID) {
            fake_bti_destroy(bti_handle_);
        }
    }

    zx_handle_t bti_handle_ = ZX_HANDLE_INVALID;
    zx_handle_t config_vmo_;
    gdc_callback_t callback_;
    buffer_collection_info_t input_buffer_collection_;
    buffer_collection_info_t output_buffer_collection_;
};

TEST_F(TaskTest, BasicCreationTest) {
    SetUpBufferCollections(kNumberOfBuffers);
    std::unique_ptr<Task> task;
    zx_status_t status = gdc::Task::Create(&input_buffer_collection_,
                                           &output_buffer_collection_,
                                           zx::vmo(config_vmo_),
                                           &callback_,
                                           zx::bti(bti_handle_),
                                           &task);
    EXPECT_OK(status);
}

TEST_F(TaskTest, InputBufferTest) {
    SetUpBufferCollections(kNumberOfBuffers);
    std::unique_ptr<Task> task;
    zx_status_t status = gdc::Task::Create(&input_buffer_collection_,
                                           &output_buffer_collection_,
                                           zx::vmo(config_vmo_),
                                           &callback_,
                                           zx::bti(bti_handle_),
                                           &task);
    EXPECT_OK(status);

    // Get the input buffers physical addresses
    // Request for physical addresses of the buffers.
    // Expecting to get error when requesting for invalid index.
    zx_paddr_t addr;
    for (uint32_t i = 0; i < kNumberOfBuffers; i++) {
        EXPECT_EQ(ZX_OK, task->GetInputBufferPhysAddr(0, &addr));
    }

    EXPECT_EQ(ZX_ERR_INVALID_ARGS, task->GetInputBufferPhysAddr(4, &addr));
}

TEST_F(TaskTest, InvalidVmoTest) {
    SetUpBufferCollections(0);

    std::unique_ptr<Task> task;
    zx_status_t status = gdc::Task::Create(&input_buffer_collection_,
                                           &output_buffer_collection_,
                                           zx::vmo(config_vmo_),
                                           &callback_,
                                           zx::bti(bti_handle_),
                                           &task);
    // Expecting Task setup to be returning an error when there are
    // no VMOs in the buffer collection. At the moment VmoPool library
    // doesn't return an error.
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
}

TEST(TaskTest, NonContigVmoTest) {
    zx_handle_t bti_handle = ZX_HANDLE_INVALID;
    gdc_callback_t callback;
    zx_handle_t config_vmo;
    buffer_collection_info_t input_buffer_collection;
    buffer_collection_info_t output_buffer_collection;
    ASSERT_OK(fake_bti_create(&bti_handle));

    zx_status_t status = CreateFakeBufferCollection(&input_buffer_collection,
                                                    bti_handle,
                                                    kWidth,
                                                    kHeight,
                                                    0);
    ASSERT_OK(status);

    status = CreateFakeBufferCollection(&output_buffer_collection,
                                        bti_handle,
                                        kWidth,
                                        kHeight,
                                        0);
    ASSERT_OK(status);

    status = zx_vmo_create(kConfigSize, 0, &config_vmo);
    ASSERT_OK(status);

    std::unique_ptr<Task> task;
    status = gdc::Task::Create(&input_buffer_collection,
                               &output_buffer_collection,
                               zx::vmo(config_vmo),
                               &callback,
                               zx::bti(bti_handle),
                               &task);
    // Expecting Task setup to be returning an error when config vmo is not contig.
    EXPECT_NE(ZX_OK, status);
}

TEST(TaskTest, InvalidBufferCollectionTest) {
    zx_handle_t bti_handle = ZX_HANDLE_INVALID;
    gdc_callback_t callback;
    zx_handle_t config_vmo;
    ASSERT_OK(fake_bti_create(&bti_handle));

    zx_status_t status = zx_vmo_create_contiguous(bti_handle, kConfigSize, 0, &config_vmo);
    ASSERT_OK(status);

    std::unique_ptr<Task> task;
    status = gdc::Task::Create(nullptr,
                               nullptr,
                               zx::vmo(config_vmo),
                               &callback,
                               zx::bti(bti_handle),
                               &task);
    EXPECT_NE(ZX_OK, status);
}

} // namespace
} // namespace gdc
