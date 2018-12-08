// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <algorithm>
#include <memory>

#include <blobfs/lz4.h>
#include <unittest/unittest.h>

namespace blobfs {
namespace {

// Tests the API of using an unset Compressor.
bool NullCompressor() {
    BEGIN_TEST;

    Compressor compressor;
    EXPECT_FALSE(compressor.Compressing());
    EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, compressor.Initialize(nullptr, 0));

    END_TEST;
}

std::unique_ptr<char[]> GenerateInput(unsigned seed, size_t size) {
    std::unique_ptr<char[]> input(new char[size]);
    for (size_t i = 0; i < size; i++) {
        input[i] = static_cast<char>(rand_r(&seed));
    }
    return input;
}

bool CompressionHelper(Compressor* compressor, const char* input, size_t size, size_t step,
                       std::unique_ptr<char[]>* out_compressed) {
    BEGIN_HELPER;

    size_t max_output = Compressor::BufferMax(size);
    std::unique_ptr<char[]> compressed(new char[max_output]);
    ASSERT_EQ(ZX_OK, compressor->Initialize(compressed.get(), max_output));
    EXPECT_TRUE(compressor->Compressing());

    size_t offset = 0;
    while (offset != size) {
        const void* data = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(input) + offset);
        const size_t incremental_size = std::min(step, size - offset);
        ASSERT_EQ(ZX_OK, compressor->Update(data, incremental_size));
        offset += incremental_size;
    }
    ASSERT_EQ(ZX_OK, compressor->End());
    EXPECT_GT(compressor->Size(), 0);

    *out_compressed = std::move(compressed);

    END_HELPER;
}

bool DecompressionHelper(const char* compressed, size_t compressed_size,
                         const char* expected, size_t expected_size) {
    BEGIN_HELPER;
    std::unique_ptr<char[]> output(new char[expected_size]);
    size_t target_size = expected_size;
    size_t src_size = compressed_size;
    ASSERT_EQ(ZX_OK, Decompressor::Decompress(output.get(), &target_size, compressed, &src_size));
    EXPECT_EQ(expected_size, target_size);
    EXPECT_EQ(compressed_size, src_size);
    EXPECT_EQ(0, memcmp(expected, output.get(), expected_size));

    END_HELPER;
}

// Tests a contained case of compression and decompression.
//
// kSize: The Size of the input buffer.
// kStep: The step size of updating the compression buffer.
template <size_t kSize, size_t kStep>
bool CompressDecompressRandom() {
    BEGIN_TEST;

    static_assert(kStep <= kSize, "Step size too large");

    // Generate input.
    std::unique_ptr<char[]> input(GenerateInput(0, kSize));

    // Compress a buffer.
    Compressor compressor;
    std::unique_ptr<char[]> compressed;
    ASSERT_TRUE(CompressionHelper(&compressor, input.get(), kSize, kStep, &compressed));

    // Decompress the buffer.
    ASSERT_TRUE(DecompressionHelper(compressed.get(), compressor.Size(), input.get(), kSize));

    END_TEST;
}

bool CompressDecompressReset() {
    BEGIN_TEST;

    Compressor compressor;

    size_t step = 128;
    size_t input_size = 1024;
    std::unique_ptr<char[]> input(GenerateInput(0, input_size));
    std::unique_ptr<char[]> compressed;
    ASSERT_TRUE(CompressionHelper(&compressor, input.get(), input_size, step, &compressed));
    ASSERT_TRUE(DecompressionHelper(compressed.get(), compressor.Size(), input.get(), input_size));

    // We should be able to re-use a buffer of the same size.
    compressor.Reset();
    ASSERT_TRUE(CompressionHelper(&compressor, input.get(), input_size, step, &compressed));
    ASSERT_TRUE(DecompressionHelper(compressed.get(), compressor.Size(), input.get(), input_size));

    // We should be able to re-use a buffer of a different size (larger).
    compressor.Reset();
    input_size = 2048;
    input = GenerateInput(0, input_size);
    ASSERT_TRUE(CompressionHelper(&compressor, input.get(), input_size, step, &compressed));
    ASSERT_TRUE(DecompressionHelper(compressed.get(), compressor.Size(), input.get(), input_size));

    // We should be able to re-use a buffer of a different size (smaller).
    compressor.Reset();
    input_size = 512;
    input = GenerateInput(0, input_size);
    ASSERT_TRUE(CompressionHelper(&compressor, input.get(), input_size, step, &compressed));
    ASSERT_TRUE(DecompressionHelper(compressed.get(), compressor.Size(), input.get(), input_size));

    END_TEST;
}

bool UpdateNoData() {
    BEGIN_TEST;

    Compressor compressor;
    const size_t input_size = 1024;
    std::unique_ptr<char[]> input(GenerateInput(0, input_size));
    const size_t max_output = Compressor::BufferMax(input_size);
    std::unique_ptr<char[]> compressed(new char[max_output]);
    ASSERT_EQ(ZX_OK, compressor.Initialize(compressed.get(), max_output));

    // Test that using "Update(data, 0)" acts a no-op, rather than corrupting the buffer.
    ASSERT_EQ(ZX_OK, compressor.Update(input.get(), 0));
    ASSERT_EQ(ZX_OK, compressor.Update(input.get(), input_size));
    ASSERT_EQ(ZX_OK, compressor.End());

    // Ensure that even with the addition of a zero-length buffer, we still decompress
    // to the expected output.
    ASSERT_TRUE(DecompressionHelper(compressed.get(), compressor.Size(), input.get(), input_size));

    END_TEST;
}

// Tests Compressor returns an error if we try to compress more data than the buffer can hold.
bool BufferTooSmall() {
    BEGIN_TEST;

    // Pretend we're going to compress only one byte of data.
    const size_t buf_size = Compressor::BufferMax(1);
    std::unique_ptr<char[]> buf(new char[buf_size]);
    Compressor compressor;
    ASSERT_EQ(ZX_OK, compressor.Initialize(buf.get(), buf_size));

    // Create data that is just too big to fit within this buffer size.
    size_t data_size = 0;
    while (Compressor::BufferMax(++data_size) <= buf_size) {}
    ASSERT_GT(data_size, 0);

    unsigned int seed = 0;
    std::unique_ptr<char[]> data(new char[data_size]);

    for (size_t i = 0; i < data_size; i++) {
        data[i] = static_cast<char>(rand_r(&seed));
    }

    ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, compressor.Update(&data, data_size));
    END_TEST;
}

} // namespace
} // namespace blobfs

BEGIN_TEST_CASE(blobfsCompressorTests)
RUN_TEST(blobfs::NullCompressor)
RUN_TEST((blobfs::CompressDecompressRandom<1 << 0, 1 << 0>))
RUN_TEST((blobfs::CompressDecompressRandom<1 << 1, 1 << 0>))
RUN_TEST((blobfs::CompressDecompressRandom<1 << 10, 1 << 5>))
RUN_TEST((blobfs::CompressDecompressRandom<1 << 15, 1 << 10>))
RUN_TEST(blobfs::CompressDecompressReset)
RUN_TEST(blobfs::UpdateNoData)
RUN_TEST(blobfs::BufferTooSmall)
END_TEST_CASE(blobfsCompressorTests);
