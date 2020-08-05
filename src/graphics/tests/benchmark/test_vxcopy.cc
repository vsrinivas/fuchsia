// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <chrono>
#include <vector>

#include <VX/vx.h>  // nogncheck
#include <gtest/gtest.h>

constexpr uint32_t kWidth = 1920;
constexpr uint32_t kHeight = 1080;
constexpr uint32_t kPixelWidth = 2;

static void VX_CALLBACK s_log_callback(vx_context context, vx_reference ref, vx_status status,
                                       const vx_char *str) {
  printf("%s:%d VX log callback for object %p status %d %s", __FILE__, __LINE__, ref, status, str);
}

class VxCopyTest {
 public:
  ~VxCopyTest();

  void Initialize(uint32_t width, uint32_t height);
  bool Exec(bool check);

  vx_reference src_ref() { return reinterpret_cast<vx_reference>(src_); }
  vx_reference dst_ref() { return reinterpret_cast<vx_reference>(dst_); }
  vx_reference ctx_ref() { return reinterpret_cast<vx_reference>(context_); }
  vx_reference graph_ref() { return reinterpret_cast<vx_reference>(graph_); }

 private:
  vx_context context_ = nullptr;
  vx_image src_ = nullptr;
  vx_image dst_ = nullptr;
  vx_graph graph_ = nullptr;
};

VxCopyTest::~VxCopyTest() {
  if (graph_) {
    EXPECT_EQ(VX_SUCCESS, vxReleaseGraph(&graph_));
  }
  if (src_) {
    EXPECT_EQ(VX_SUCCESS, vxReleaseReference(reinterpret_cast<vx_reference *>(&src_)));
  }
  if (dst_) {
    EXPECT_EQ(VX_SUCCESS, vxReleaseReference(reinterpret_cast<vx_reference *>(&dst_)));
  }
  if (context_) {
    EXPECT_EQ(VX_SUCCESS, vxReleaseContext(&context_));
  }
}

void VxCopyTest::Initialize(uint32_t width, uint32_t height) {
  context_ = vxCreateContext();

  EXPECT_EQ(VX_SUCCESS, vxGetStatus(ctx_ref())) << "vxCreateContext failed";

  vxRegisterLogCallback(context_, s_log_callback, vx_true_e);

  static_assert(kPixelWidth == 2, "Format doesn't match pixel width");
  src_ = vxCreateImage(context_, width, height, VX_DF_IMAGE_S16);

  EXPECT_EQ(VX_SUCCESS, vxGetStatus(src_ref())) << "vxCreateImage failed";

  static_assert(kPixelWidth == 2, "Format doesn't match pixel width");
  dst_ = vxCreateImage(context_, width, height, VX_DF_IMAGE_S16);

  EXPECT_EQ(VX_SUCCESS, vxGetStatus(dst_ref())) << "vxCreateImage failed";

  graph_ = vxCreateGraph(context_);

  EXPECT_EQ(VX_SUCCESS, vxGetStatus(graph_ref())) << "vxCreateGraph failed";

  {
    vx_rectangle_t rect;
    EXPECT_EQ(VX_SUCCESS, vxGetValidRegionImage(src_, &rect));

    void *ptr;
    vx_map_id map_id;
    vx_imagepatch_addressing_t addr;

    EXPECT_EQ(VX_SUCCESS, vxMapImagePatch(src_, &rect, 0, &map_id, &addr, &ptr, VX_WRITE_ONLY,
                                          VX_MEMORY_TYPE_HOST, 0))
        << "vxMapImagePatch failed";

    for (vx_size i = 0; i < addr.dim_x * addr.dim_y; i++) {
      void *pixel = vxFormatImagePatchAddress1d(ptr, i, &addr);
      static_assert(kPixelWidth == 2, "Format doesn't match pixel width");
      *reinterpret_cast<uint16_t *>(pixel) = i;
    }

    EXPECT_EQ(VX_SUCCESS, vxUnmapImagePatch(src_, map_id)) << "vxUnmapImagePatch failed";
  }
}

bool VxCopyTest::Exec(bool check) {
  {
    vx_node node = vxCopyNode(graph_, src_ref(), dst_ref());

    EXPECT_EQ(VX_SUCCESS, vxGetStatus(reinterpret_cast<vx_reference>(node)));

    EXPECT_EQ(VX_SUCCESS, vxVerifyGraph(graph_));

    EXPECT_EQ(VX_SUCCESS, vxProcessGraph(graph_));

    EXPECT_EQ(VX_SUCCESS, vxRemoveNode(&node));
  }

  if (check) {
    vx_rectangle_t rect;
    EXPECT_EQ(VX_SUCCESS, vxGetValidRegionImage(dst_, &rect)) << "vxGetValidRegionImage failed";

    void *ptr;
    vx_map_id map_id;
    vx_imagepatch_addressing_t addr;

    EXPECT_EQ(VX_SUCCESS, vxMapImagePatch(dst_, &rect, 0, &map_id, &addr, &ptr, VX_READ_ONLY,
                                          VX_MEMORY_TYPE_HOST, 0))
        << "vxMapImagePatch failed";

    uint32_t mismatch_count = 0;

    for (vx_uint32 i = 0; i < addr.dim_x * addr.dim_y; i++) {
      void *pixel = vxFormatImagePatchAddress1d(ptr, i, &addr);
      static_assert(kPixelWidth == 2, "Format doesn't match pixel width");
      EXPECT_EQ(i, *reinterpret_cast<uint16_t *>(pixel));
      if (++mismatch_count > 10)
        break;
    }

    EXPECT_EQ(VX_SUCCESS, vxUnmapImagePatch(dst_, map_id)) << "vxUnmapImagePatch failed";
    return mismatch_count == 0;
  }

  return true;
}

TEST(VxCopy, Check) {
  VxCopyTest test;
  test.Initialize(kWidth, kHeight);
  test.Exec(true);
}

TEST(VxCopy, Perf) {
  VxCopyTest test;
  test.Initialize(kWidth, kHeight);

  constexpr uint32_t kIterations = 5000;
  uint64_t buffer_size = kWidth * kHeight * kPixelWidth;

  printf("Copying buffer size %lu iterations %u...\n", buffer_size, kIterations);
  fflush(stdout);

  auto start = std::chrono::high_resolution_clock::now();

  for (uint32_t iter = 0; iter < kIterations; iter++) {
    ASSERT_TRUE(test.Exec(false));
  }

  std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - start;

  const uint32_t kMB = 1024 * 1024;
  printf("Copy rate %g MB/s\n",
         static_cast<double>(buffer_size) * kIterations / kMB / elapsed.count());
}
