// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstdlib>

#include <gtest/gtest.h>

#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/compression/decompressor_sandbox/decompressor_impl.h"
#include "src/storage/blobfs/compression/external_decompressor.h"
#include "src/storage/blobfs/compression_settings.h"

namespace blobfs {
namespace {

using namespace fuchsia_blobfs_internal;

// These settings currently achieve about 60% compression.
constexpr int kCompressionLevel = 5;
constexpr double kDataRandomnessRatio = 0.25;

constexpr size_t kDataSize = 500 * 1024;  // 500KiB
constexpr size_t kMapSize = kDataSize * 2;

// Generates a data set of size with sequences of the same bytes and random
// values appearing with frequency kDataRandomnessRatio.
void GenerateData(size_t size, uint8_t* dst) {
  srand(testing::UnitTest::GetInstance()->random_seed());
  for (size_t i = 0; i < size; i++) {
    if ((rand() % 1000) / 1000.0l >= kDataRandomnessRatio) {
      dst[i] = 12;
    } else {
      dst[i] = static_cast<uint8_t>(rand() % 256);
    }
  }
}

void CompressData(std::unique_ptr<Compressor> compressor, void* input_data, size_t* size) {
  ASSERT_EQ(ZX_OK, compressor->Update(input_data, kDataSize));
  ASSERT_EQ(ZX_OK, compressor->End());
  *size = compressor->Size();
}

class DecompressorSandboxTest : public ::testing::Test {
 public:
  void SetUp() override {
    GenerateData(kDataSize, input_data_);

    zx::vmo compressed_vmo;
    ASSERT_EQ(ZX_OK, zx::vmo::create(kMapSize, 0, &compressed_vmo));
    zx::vmo remote_compressed_vmo;
    ASSERT_EQ(ZX_OK, compressed_vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS & (~ZX_RIGHT_WRITE),
                                              &remote_compressed_vmo));
    ASSERT_EQ(ZX_OK, compressed_mapper_.Map(std::move(compressed_vmo), kMapSize));

    zx::vmo decompressed_vmo;
    ASSERT_EQ(ZX_OK, zx::vmo::create(kMapSize, 0, &decompressed_vmo));
    zx::vmo remote_decompressed_vmo;
    ASSERT_EQ(ZX_OK, decompressed_vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS, &remote_decompressed_vmo));
    ASSERT_EQ(ZX_OK, decompressed_mapper_.Map(std::move(decompressed_vmo), kMapSize));

    zx::fifo remote_fifo;
    ASSERT_EQ(ZX_OK,
              zx::fifo::create(16, sizeof(wire::DecompressRequest), 0, &fifo_, &remote_fifo));

    zx_status_t status;
    decompressor_.Create(std::move(remote_fifo), std::move(remote_compressed_vmo),
                         std::move(remote_decompressed_vmo),
                         [&status](zx_status_t s) { status = s; });
    ASSERT_EQ(ZX_OK, status);
  }

  void TearDown() override {
    size_t actual;
    size_t avail;
    zx_info_vmo_t info;
    ASSERT_EQ(ZX_OK, decompressed_mapper_.vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), &actual,
                                                         &avail));
    ASSERT_EQ(2ul, info.num_mappings);

    // This should close down the remote thread and unmap the decompression vmo.
    ASSERT_TRUE(fifo_.is_valid());
    fifo_.reset();

    size_t total_sleep = 0;
    ASSERT_EQ(ZX_OK, decompressed_mapper_.vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), &actual,
                                                         &avail));
    while (info.num_mappings >= 2ul) {
      ASSERT_GT(2000ul, total_sleep) << "Timed out waiting for thread to clean up.";
      zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
      total_sleep += 10;

      ASSERT_EQ(ZX_OK, decompressed_mapper_.vmo().get_info(ZX_INFO_VMO, &info, sizeof(info),
                                                           &actual, &avail));
    }
  }

 protected:
  void SendRequest(wire::DecompressRequest* request, wire::DecompressResponse* response) {
    ASSERT_EQ(ZX_OK, fifo_.write(sizeof(*request), request, 1, nullptr));
    zx_signals_t signal;
    fifo_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED, zx::time::infinite(), &signal);
    ASSERT_TRUE(signal & ZX_FIFO_READABLE) << "Got ZX_FIFO_PEER_CLOSED: " << signal;
    ASSERT_EQ(ZX_OK, fifo_.read(sizeof(*response), response, 1, nullptr));
  }

  uint8_t input_data_[kDataSize];
  DecompressorImpl decompressor_;
  fzl::OwnedVmoMapper compressed_mapper_;
  fzl::OwnedVmoMapper decompressed_mapper_;
  zx::fifo fifo_;
};

// Decompress all chunks from a chunked compressed file as a single call.
TEST_F(DecompressorSandboxTest, ChunkedFullDecompression) {
  size_t compressed_size;
  std::unique_ptr<ChunkedCompressor> compressor = nullptr;
  ASSERT_EQ(ZX_OK, ChunkedCompressor::Create({CompressionAlgorithm::kChunked, kCompressionLevel},
                                             kDataSize, &compressed_size, &compressor));
  ASSERT_EQ(ZX_OK, compressor->SetOutput(compressed_mapper_.start(), kMapSize));
  CompressData(std::move(compressor), input_data_, &compressed_size);

  wire::DecompressRequest request = {
      {0, kDataSize},
      {0, compressed_size},
      fuchsia_blobfs_internal::wire::CompressionAlgorithm::kChunked,
  };

  wire::DecompressResponse response;
  SendRequest(&request, &response);
  ASSERT_EQ(ZX_OK, response.status);
  ASSERT_EQ(kDataSize, response.size);
  ASSERT_EQ(0, memcmp(input_data_, decompressed_mapper_.start(), kDataSize));
}

// Simple success case for chunked decompression, but done on each chunk just
// to verify success.
TEST_F(DecompressorSandboxTest, ChunkedPartialDecompression) {
  size_t compressed_size;
  std::unique_ptr<ChunkedCompressor> compressor = nullptr;
  ASSERT_EQ(ZX_OK, ChunkedCompressor::Create({CompressionAlgorithm::kChunked, kCompressionLevel},
                                             kDataSize, &compressed_size, &compressor));
  ASSERT_EQ(ZX_OK, compressor->SetOutput(compressed_mapper_.start(), kMapSize));
  CompressData(std::move(compressor), input_data_, &compressed_size);

  std::unique_ptr<SeekableDecompressor> local_decompressor;
  ASSERT_EQ(ZX_OK, SeekableChunkedDecompressor::CreateDecompressor(
                       cpp20::span(static_cast<const uint8_t*>(compressed_mapper_.start()),
                                   compressed_size),
                       compressed_size, &local_decompressor));

  size_t total_size = 0;
  size_t iterations = 0;
  while (total_size < kDataSize) {
    zx::result<CompressionMapping> mapping_or = local_decompressor->MappingForDecompressedRange(
        total_size, 1, std::numeric_limits<size_t>::max());
    ASSERT_TRUE(mapping_or.is_ok());
    CompressionMapping mapping = mapping_or.value();

    wire::DecompressRequest request = {
        {mapping.decompressed_offset, mapping.decompressed_length},
        {mapping.compressed_offset, mapping.compressed_length},
        fuchsia_blobfs_internal::wire::CompressionAlgorithm::kChunkedPartial,
    };
    wire::DecompressResponse response;
    SendRequest(&request, &response);
    ASSERT_EQ(ZX_OK, response.status);
    ASSERT_EQ(mapping.decompressed_length, response.size);

    iterations++;
    total_size += response.size;
  }

  ASSERT_EQ(0, memcmp(input_data_, decompressed_mapper_.start(), kDataSize));
  // Ensure that we're testing multiple chunks and not one large chunk.
  ASSERT_GT(iterations, 1ul);
}

// Put junk the in the compressed vmo to verify an error signal.
TEST_F(DecompressorSandboxTest, CorruptedInput) {
  memcpy(compressed_mapper_.start(), input_data_, kDataSize);
  wire::DecompressRequest request = {
      {0, kDataSize},
      {0, kDataSize},
      ExternalDecompressorClient::CompressionAlgorithmLocalToFidl(CompressionAlgorithm::kChunked)};
  wire::DecompressResponse response;
  SendRequest(&request, &response);
  // Error is really specific to the compression lib. Just verify that it failed.
  ASSERT_NE(ZX_OK, response.status);

  request = {
      {0, kDataSize},
      {0, kDataSize},
      ExternalDecompressorClient::CompressionAlgorithmLocalToFidl(CompressionAlgorithm::kChunked)};
  SendRequest(&request, &response);
  // Error is really specific to the compression lib. Just verify that it failed.
  ASSERT_NE(ZX_OK, response.status);
}

// Verify the error signal of using unsupported algorithms.
TEST_F(DecompressorSandboxTest, UnsupportedCompression) {
  wire::DecompressRequest request = {
      {0, kDataSize},
      {0, kDataSize},
      ExternalDecompressorClient::CompressionAlgorithmLocalToFidl(
          CompressionAlgorithm::kUncompressed),
  };
  wire::DecompressResponse response;
  SendRequest(&request, &response);
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, response.status);
}

// Verify the error signal of using offsets with full decompression.
TEST_F(DecompressorSandboxTest, NonzeroOffsetsForFullDecompression) {
  wire::DecompressRequest request = {
      {12, kDataSize},
      {0, kDataSize},
      ExternalDecompressorClient::CompressionAlgorithmLocalToFidl(CompressionAlgorithm::kChunked)};
  wire::DecompressResponse response;
  SendRequest(&request, &response);
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, response.status);
}

// Rejects outright attempts at accessing outside the mapped vmo range.
TEST_F(DecompressorSandboxTest, BadVmoRange) {
  wire::DecompressRequest request = {
      {1, kMapSize},
      {0, kDataSize},
      fuchsia_blobfs_internal::wire::CompressionAlgorithm::kChunkedPartial};
  wire::DecompressResponse response;
  SendRequest(&request, &response);
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, response.status);
}

}  // namespace
}  // namespace blobfs
