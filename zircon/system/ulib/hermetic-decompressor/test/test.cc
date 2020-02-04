// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hermetic-decompressor/hermetic-decompressor.h>
#include <lib/zx/vmo.h>

#include <fbl/auto_call.h>
#include <lz4/lz4frame.h>
#include <zstd/zstd.h>
#include <zxtest/zxtest.h>

namespace {

// Create a VMO and map it in for the life of this object.
class DataVmo {
 public:
  DataVmo(size_t size) : size_((size + PAGE_SIZE - 1) & -size_t{PAGE_SIZE}) {
    ASSERT_OK(zx::vmo::create(size_, 0, &vmo_));
    ASSERT_OK(
        zx::vmar::root_self()->map(0, vmo_, 0, size_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &ptr_));
  }

  ~DataVmo() { ASSERT_OK(zx::vmar::root_self()->unmap(ptr_, size_)); }

  const zx::vmo& vmo() { return vmo_; }

  std::byte* data() { return reinterpret_cast<std::byte*>(ptr_); }
  size_t size() { return size_; }

 private:
  zx::vmo vmo_;
  uintptr_t ptr_ = 0;
  size_t size_ = 0;
};

// Get some data that's random, but not too random, so it compresses somewhat.
std::string RandomData() {
  constexpr size_t kDataSize = PAGE_SIZE;
  std::string data(kDataSize, 0);
  static_assert(kDataSize % ZX_CPRNG_DRAW_MAX_LEN == 0);
  for (size_t i = 0; i < kDataSize / ZX_CPRNG_DRAW_MAX_LEN; i += 2) {
    zx_cprng_draw(&data.data()[i * ZX_CPRNG_DRAW_MAX_LEN], ZX_CPRNG_DRAW_MAX_LEN);
    memcpy(&data.data()[(i + 1) * ZX_CPRNG_DRAW_MAX_LEN], &data.data()[i * ZX_CPRNG_DRAW_MAX_LEN],
           ZX_CPRNG_DRAW_MAX_LEN);
  }
  return data;
}

}  // namespace

TEST(HermeticDecompressorTests, BadMagicTest) {
  zx::vmo input, output;
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &input));
  ASSERT_OK(zx::vmo::create(PAGE_SIZE, 0, &output));

  EXPECT_EQ(ZX_ERR_NOT_FOUND, HermeticDecompressor()(input, 0, PAGE_SIZE, output, 0, PAGE_SIZE));
}

TEST(HermeticDecompressorTests, Lz4fTest) {
  auto data = RandomData();

  constexpr const LZ4F_compressOptions_t kCompressOpt = {1, {}};
  LZ4F_preferences_t prefs{};
  prefs.frameInfo.contentSize = data.size();
  prefs.frameInfo.blockSizeID = LZ4F_max64KB;
  prefs.frameInfo.blockMode = LZ4F_blockIndependent;

  DataVmo compressed(LZ4F_compressBound(data.size(), &prefs));
  ASSERT_NO_FATAL_FAILURES();

  LZ4F_compressionContext_t ctx{};
  LZ4F_errorCode_t ret = LZ4F_createCompressionContext(&ctx, LZ4F_VERSION);
  ASSERT_FALSE(LZ4F_isError(ret), "LZ4F_createCompressionContext: %s", LZ4F_getErrorName(ret));
  auto cleanup = fbl::MakeAutoCall([&] { LZ4F_freeCompressionContext(ctx); });

  char* buffer = reinterpret_cast<char*>(compressed.data());
  size_t buffer_left = compressed.size();

  ret = LZ4F_compressBegin(ctx, buffer, buffer_left, &prefs);
  ASSERT_FALSE(LZ4F_isError(ret), "LZ4F_compressBegin: %s", LZ4F_getErrorName(ret));
  buffer += ret;
  buffer_left -= ret;
  ret = LZ4F_compressUpdate(ctx, buffer, buffer_left, data.data(), data.size(), &kCompressOpt);
  ASSERT_FALSE(LZ4F_isError(ret), "LZ4F_compressUpdate: %s", LZ4F_getErrorName(ret));
  buffer += ret;
  buffer_left -= ret;
  ret = LZ4F_compressEnd(ctx, buffer, buffer_left, &kCompressOpt);
  ASSERT_FALSE(LZ4F_isError(ret), "LZ4F_compressEnd: %s", LZ4F_getErrorName(ret));
  buffer += ret;
  buffer_left -= ret;

  size_t compressed_size = compressed.size() - buffer_left;

  DataVmo output(data.size());
  ASSERT_NO_FATAL_FAILURES();

  ASSERT_OK(
      HermeticDecompressor()(compressed.vmo(), 0, compressed_size, output.vmo(), 0, output.size()));

  EXPECT_EQ(0, memcmp(data.data(), output.data(), data.size()));
}

TEST(HermeticDecompressorTests, ZstdTest) {
  auto data = RandomData();
  DataVmo compressed(ZSTD_compressBound(data.size()));
  ASSERT_NO_FATAL_FAILURES();

  auto ret = ZSTD_compress(compressed.data(), compressed.size(), data.data(), data.size(),
                           ZSTD_CLEVEL_DEFAULT);
  ASSERT_FALSE(ZSTD_isError(ret), "ZSTD_compress -> %s", ZSTD_getErrorName(ret));
  ASSERT_LT(ret, data.size(), "ZSTD_compress");

  DataVmo output(data.size());
  ASSERT_NO_FATAL_FAILURES();

  ASSERT_OK(HermeticDecompressor()(compressed.vmo(), 0, ret, output.vmo(), 0, output.size()));

  EXPECT_EQ(0, memcmp(data.data(), output.data(), data.size()));
}
