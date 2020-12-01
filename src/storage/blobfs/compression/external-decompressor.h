// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_EXTERNAL_DECOMPRESSOR_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_EXTERNAL_DECOMPRESSOR_H_

#include <fuchsia/blobfs/internal/cpp/fidl.h>
#include <fuchsia/blobfs/internal/llcpp/fidl.h>
#include <lib/zx/fifo.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>

#include <blobfs/compression-settings.h>

#include "src/storage/blobfs/compression/seekable-decompressor.h"

namespace blobfs {

// A client class for managing the connection to the decompressor sandbox,
// sending messages, and returning the status result. This class is *not* thread
// safe.
class ExternalDecompressorClient {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(ExternalDecompressorClient);

  // Creates a DecompressorClient that takes data from `compressed_vmo` and
  // places the results in `decompressed_vmo`. This calls `Prepare()` and
  // returns a failure if it cannot succeed on the first try. Both vmos require
  // the ZX_DEFAULT_VMO_RIGHTS except that ZX_RIGHT_WRITE is not required on
  // `compressed_vmo`, this permission will be ommited before sending to the
  // external decompressor if present.
  static zx::status<std::unique_ptr<ExternalDecompressorClient>> Create(
      const zx::vmo& decompressed_vmo, const zx::vmo& compressed_vmo);

  // Sends the request over the fifo, and awaits the response before verifying
  // the resulting size and reporting the status passed from the server. This
  // succeeds only if the resulting decompressed size matches the
  // `decompressed.size`. Starts by calling `Prepare()`.
  zx_status_t SendMessage(const llcpp::fuchsia::blobfs::internal::DecompressRequest& request);

  // Convert from fidl compatible enum to local.
  static CompressionAlgorithm CompressionAlgorithmFidlToLocal(
      llcpp::fuchsia::blobfs::internal::CompressionAlgorithm algorithm);

  // Convert to fidl compatible enum from local.
  static llcpp::fuchsia::blobfs::internal::CompressionAlgorithm CompressionAlgorithmLocalToFidl(
      CompressionAlgorithm algorithm);

  // Convert to fidl compatible enum from local for partial decompresion.
  static zx::status<llcpp::fuchsia::blobfs::internal::CompressionAlgorithm>
  CompressionAlgorithmLocalToFidlForPartial(CompressionAlgorithm algorithm);

 private:
  ExternalDecompressorClient() = default;

  // If the fifo is useable nothing is done and returns ZX_OK. If the fifo is
  // not ready to use, this attempts to set one up via the DecompressorCreator.
  zx_status_t Prepare();

  // If the DecompressorCreator fidl channel is ready then nothing is done.
  // Otherwise the channel is set up.
  zx_status_t PrepareDecompressorCreator();

  // The vmo that will contain the decompressed data for requests. A copy is kept
  // so that if it needs to reconnect with the server another copy can be sent.
  zx::vmo decompressed_vmo_;

  // The vmo that will contain the compressed data for requests. A copy is kept
  // so that if it needs to reconnect with the server another copy can be sent.
  zx::vmo compressed_vmo_;

  // Fidl connection to the DecompressorCreator.
  fuchsia::blobfs::internal::DecompressorCreatorSyncPtr decompressor_creator_;

  // The fifo that communicates with the Decompressor.
  zx::fifo fifo_;
};

// A class for decompressing entire files for which there is an implementation
// of the Decompressor interface for the `algorithm`. Uses the given `client`
// for communication to the external decompressor process.
class ExternalDecompressor {
 public:
  ExternalDecompressor(ExternalDecompressorClient* client, CompressionAlgorithm algorithm);
  DISALLOW_COPY_ASSIGN_AND_MOVE(ExternalDecompressor);

  // Performs decompression for an entire archive using the provided client.
  zx_status_t Decompress(size_t uncompressed_size, size_t max_compressed_size);

 private:
  // Client used for communication with the decompressor.
  ExternalDecompressorClient* client_;

  // The algorithm to be used for this file.
  CompressionAlgorithm algorithm_;
};

// A class for decompressing parts of files for which there is an implementation
// of the SeekableDecompressor interface for the `algorithm`. Uses the given
// `client` for communication to the external decompressor process.
class ExternalSeekableDecompressor {
 public:
  ExternalSeekableDecompressor(ExternalDecompressorClient* client,
                               SeekableDecompressor* decompressor);
  DISALLOW_COPY_ASSIGN_AND_MOVE(ExternalSeekableDecompressor);

  // Decompresses exactly one area by sending a request to the provided client.
  // The range specified must be an entire completeable chunk.
  // `compressed_offset` is the offset into the `compressed_vmo_` to start
  // decompressing from.
  zx_status_t DecompressRange(size_t compressed_offset, size_t compressed_size,
                              size_t uncompressed_size);

 private:
  // Client used for communication with the decompressor.
  ExternalDecompressorClient* client_;

  // The SeekableDecompressor that would otherwise be used to decompress
  // locally, which has the CompressionMapping information.
  SeekableDecompressor* decompressor_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_EXTERNAL_DECOMPRESSOR_H_
