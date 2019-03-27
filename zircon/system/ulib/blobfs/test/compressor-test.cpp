// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <algorithm>
#include <memory>

#include <blobfs/compression/blob-compressor.h>
#include <blobfs/compression/compressor.h>
#include <blobfs/compression/lz4.h>
#include <blobfs/compression/zstd.h>
#include <unittest/unittest.h>
#include <zircon/assert.h>

namespace blobfs {
namespace {

enum class DataType {
    Compressible,
    Random,
};

std::unique_ptr<char[]> GenerateInput(DataType data_type, unsigned seed, size_t size) {
    std::unique_ptr<char[]> input(new char[size]);
    switch (data_type) {
    case DataType::Compressible: {
        size_t i = 0;
        while (i < size) {
            size_t run_length = 1 + (rand_r(&seed) % (size - i));
            char value = static_cast<char>(rand_r(&seed) % std::numeric_limits<char>::max());
            memset(input.get() + i, value, run_length);
            i += run_length;
        }
        break;
    }
    case DataType::Random:
        for (size_t i = 0; i < size; i++) {
            input[i] = static_cast<char>(rand_r(&seed));
        }
        break;
    default:
        ZX_DEBUG_ASSERT_MSG(false, "Bad Data Type");
    }
    return input;
}

template <CompressionAlgorithm Algorithm>
bool CompressionHelper(const char* input, size_t size, size_t step,
                       std::optional<BlobCompressor>* out) {
    BEGIN_HELPER;

    auto compressor = BlobCompressor::Create(Algorithm, size);
    ASSERT_TRUE(compressor);

    size_t offset = 0;
    while (offset != size) {
        const void* data = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(input) + offset);
        const size_t incremental_size = std::min(step, size - offset);
        ASSERT_EQ(ZX_OK, compressor->Update(data, incremental_size));
        offset += incremental_size;
    }
    ASSERT_EQ(ZX_OK, compressor->End());
    EXPECT_GT(compressor->Size(), 0);

    *out = std::move(compressor);
    END_HELPER;
}

template <CompressionAlgorithm Algorithm>
bool DecompressionHelper(const void* compressed, size_t compressed_size,
                         const void* expected, size_t expected_size) {
    BEGIN_HELPER;
    std::unique_ptr<char[]> output(new char[expected_size]);
    size_t target_size = expected_size;
    size_t src_size = compressed_size;
    switch (Algorithm) {
    case CompressionAlgorithm::LZ4:
        ASSERT_EQ(ZX_OK, LZ4Decompress(output.get(), &target_size, compressed, &src_size));
        break;
    case CompressionAlgorithm::ZSTD:
        ASSERT_EQ(ZX_OK, ZSTDDecompress(output.get(), &target_size, compressed, &src_size));
        break;
    default:
        ASSERT_TRUE(false, "Bad algorithm");
    }
    EXPECT_EQ(expected_size, target_size);
    EXPECT_EQ(compressed_size, src_size);
    EXPECT_EQ(0, memcmp(expected, output.get(), expected_size));

    END_HELPER;
}

// Tests a contained case of compression and decompression.
//
// kSize: The Size of the input buffer.
// kStep: The step size of updating the compression buffer.
template <CompressionAlgorithm Algorithm, DataType kDataType, size_t kSize, size_t kStep>
bool CompressDecompress() {
    BEGIN_TEST;

    static_assert(kStep <= kSize, "Step size too large");

    // Generate input.
    std::unique_ptr<char[]> input(GenerateInput(kDataType, 0, kSize));

    // Compress a buffer.
    std::optional<BlobCompressor> compressor;
    ASSERT_TRUE(CompressionHelper<Algorithm>(input.get(), kSize, kStep, &compressor));
    ASSERT_TRUE(compressor);

    // Decompress the buffer.
    ASSERT_TRUE(DecompressionHelper<Algorithm>(compressor->Data(), compressor->Size(),
                                               input.get(), kSize));

    END_TEST;
}

template <CompressionAlgorithm Algorithm>
bool UpdateNoData() {
    BEGIN_TEST;

    const size_t input_size = 1024;
    auto compressor = BlobCompressor::Create(Algorithm, input_size);
    ASSERT_TRUE(compressor);

    std::unique_ptr<char[]> input(new char[input_size]);
    memset(input.get(), 'a', input_size);

    // Test that using "Update(data, 0)" acts a no-op, rather than corrupting the buffer.
    ASSERT_EQ(ZX_OK, compressor->Update(input.get(), 0));
    ASSERT_EQ(ZX_OK, compressor->Update(input.get(), input_size));
    ASSERT_EQ(ZX_OK, compressor->End());

    // Ensure that even with the addition of a zero-length buffer, we still decompress
    // to the expected output.
    ASSERT_TRUE(DecompressionHelper<Algorithm>(compressor->Data(), compressor->Size(),
                                               input.get(), input_size));

    END_TEST;
}

// TODO(smklein): Add a test of:
// - Compress
// - Round up compressed size to block
// - Decompress
// (This mimics blobfs' usage, where the exact compressed size is not stored explicitly)

#define ALL_COMPRESSION_TESTS(ALGORITHM) \
    RUN_TEST((CompressDecompress<ALGORITHM, DataType::Random, 1 << 0, 1 << 0>)) \
    RUN_TEST((CompressDecompress<ALGORITHM, DataType::Random, 1 << 1, 1 << 0>)) \
    RUN_TEST((CompressDecompress<ALGORITHM, DataType::Random, 1 << 10, 1 << 5>)) \
    RUN_TEST((CompressDecompress<ALGORITHM, DataType::Random, 1 << 15, 1 << 10>)) \
    RUN_TEST((CompressDecompress<ALGORITHM, DataType::Compressible, 1 << 0, 1 << 0>)) \
    RUN_TEST((CompressDecompress<ALGORITHM, DataType::Compressible, 1 << 1, 1 << 0>)) \
    RUN_TEST((CompressDecompress<ALGORITHM, DataType::Compressible, 1 << 10, 1 << 5>)) \
    RUN_TEST((CompressDecompress<ALGORITHM, DataType::Compressible, 1 << 15, 1 << 10>)) \
    RUN_TEST((UpdateNoData<ALGORITHM>)) \

BEGIN_TEST_CASE(blobfsCompressorTests)
ALL_COMPRESSION_TESTS(CompressionAlgorithm::LZ4)
ALL_COMPRESSION_TESTS(CompressionAlgorithm::ZSTD)
END_TEST_CASE(blobfsCompressorTests)

} // namespace
} // namespace blobfs

