// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression/external-decompressor.h"

#include <fs/debug.h>
#include <lib/fdio/directory.h>
#include <lib/zx/time.h>
#include <zircon/rights.h>

namespace blobfs {

zx::status<std::unique_ptr<ExternalDecompressorClient>> ExternalDecompressorClient::Create(
    const zx::vmo& decompressed_vmo, const zx::vmo& compressed_vmo) {
  std::unique_ptr<ExternalDecompressorClient> client;
  client.reset(new ExternalDecompressorClient());

  zx_status_t status =
      decompressed_vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS, &(client->decompressed_vmo_));
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs] Failed to duplicate decompressed VMO: %s.\n",
                   zx_status_get_string(status));
    return zx::error(status);
  }
  status = compressed_vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS & (~ZX_RIGHT_WRITE),
                                    &(client->compressed_vmo_));
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs] Failed to duplicate compressed VMO: %s.\n",
                   zx_status_get_string(status));
    return zx::error(status);
  }
  status = client->Prepare();
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(client));
}

zx_status_t ExternalDecompressorClient::Prepare() {
  zx_signals_t signal;
  zx_status_t status =
      fifo_.wait_one(ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED, zx::time::infinite_past(), &signal);
  if (status == ZX_OK && (signal & ZX_FIFO_PEER_CLOSED) == 0 && (signal & ZX_FIFO_WRITABLE) != 0) {
    return ZX_OK;
  }

  status = PrepareDecompressorCreator();
  if (status != ZX_OK) {
    return status;
  }

  zx::vmo remote_decompressed_vmo;
  status = decompressed_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &remote_decompressed_vmo);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs] Failed to create remote duplicate of decompressed VMO: %s.\n",
                   zx_status_get_string(status));
    return status;
  }
  zx::vmo remote_compressed_vmo;
  status = compressed_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &remote_compressed_vmo);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs] Failed to create remote duplicate of compressed VMO: %s.\n",
                   zx_status_get_string(status));
    return status;
  }

  zx::fifo remote_fifo;
  // Sized for 4 elements, allows enough pipelining to keep the remote process
  // from descheduling to have 2 in flight requests/response pairs.
  status = zx::fifo::create(4, sizeof(llcpp::fuchsia::blobfs::internal::DecompressRequest), 0,
                            &fifo_, &remote_fifo);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs] Failed create fifo for external decompressor: %s.\n",
                   zx_status_get_string(status));
    return status;
  }

  zx_status_t fidl_status =
      decompressor_creator_->Create(std::move(remote_fifo), std::move(remote_compressed_vmo),
                                    std::move(remote_decompressed_vmo), &status);
  if (fidl_status == ZX_ERR_PEER_CLOSED) {
    decompressor_creator_.Unbind();
  }
  if (fidl_status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs] FIDL error communicating with external decompressor: %s.\n",
                   zx_status_get_string(fidl_status));
    return fidl_status;
  }
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs] Error calling Create on DecompressorCreator service: %s.\n",
                   zx_status_get_string(status));
  }
  return status;
}

zx_status_t ExternalDecompressorClient::PrepareDecompressorCreator() {
  if (decompressor_creator_.is_bound()) {
    zx_signals_t signal;
    zx_status_t status = decompressor_creator_.unowned_channel()->wait_one(
        ZX_CHANNEL_WRITABLE | ZX_CHANNEL_PEER_CLOSED, zx::time::infinite_past(), &signal);
    if (status == ZX_OK && (signal & ZX_CHANNEL_PEER_CLOSED) == 0 &&
        (signal & ZX_CHANNEL_WRITABLE) != 0) {
      return ZX_OK;
    } else {
      decompressor_creator_.Unbind();
    }
  }

  auto remote_channel = decompressor_creator_.NewRequest();
  if (!decompressor_creator_.is_bound()) {
    FS_TRACE_ERROR("[blobfs] Failed to create channel pair for external decompressor.\n");
    return ZX_ERR_NO_RESOURCES;
  }

  zx_status_t status =
      fdio_service_connect("/svc_blobfs/fuchsia.blobfs.internal.DecompressorCreator",
                           remote_channel.TakeChannel().release());
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs] Failed to connect to DecompressorCreator service: %s.\n",
                   zx_status_get_string(status));
    decompressor_creator_.Unbind();
  }
  return status;
}

zx_status_t ExternalDecompressorClient::SendMessage(
    const llcpp::fuchsia::blobfs::internal::DecompressRequest& request) {
  zx_status_t status;
  llcpp::fuchsia::blobfs::internal::DecompressResponse response;
  status = Prepare();
  if (status != ZX_OK) {
    return status;
  }

  status = fifo_.write(sizeof(request), &request, 1, nullptr);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs] Failed to write fifo request to decompressor: %s.\n",
                   zx_status_get_string(status));
    return status;
  }

  zx_signals_t signal;
  fifo_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED, zx::time::infinite(), &signal);
  if ((signal & ZX_FIFO_READABLE) == 0) {
    fifo_.reset();
    FS_TRACE_ERROR("[blobfs] External decompressor closed the fifo.\n");
    return ZX_ERR_INTERNAL;
  }

  status = fifo_.read(sizeof(response), &response, 1, nullptr);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs] Failed to read from fifo: %s\n", zx_status_get_string(status));
    return status;
  }
  if (response.status != ZX_OK) {
    FS_TRACE_ERROR("[blobfs] Error from external decompressor: %s size: %ld\n",
                   zx_status_get_string(status), response.size);
    return response.status;
  }
  if (response.size != request.decompressed.size) {
    FS_TRACE_ERROR("[blobfs] Decompressed size did not match. Expected: %ld Got: %ld\n",
                   request.decompressed.size, response.size);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  return ZX_OK;
}

CompressionAlgorithm ExternalDecompressorClient::CompressionAlgorithmFidlToLocal(
    const llcpp::fuchsia::blobfs::internal::CompressionAlgorithm algorithm) {
  using Fidl = llcpp::fuchsia::blobfs::internal::CompressionAlgorithm;
  switch (algorithm) {
    case Fidl::UNCOMPRESSED:
      return CompressionAlgorithm::UNCOMPRESSED;
    case Fidl::LZ4:
      return CompressionAlgorithm::LZ4;
    case Fidl::ZSTD:
      return CompressionAlgorithm::ZSTD;
    case Fidl::ZSTD_SEEKABLE:
      return CompressionAlgorithm::ZSTD_SEEKABLE;
    case Fidl::CHUNKED:
    case Fidl::CHUNKED_PARTIAL:
      return CompressionAlgorithm::CHUNKED;
  }
}

llcpp::fuchsia::blobfs::internal::CompressionAlgorithm
ExternalDecompressorClient::CompressionAlgorithmLocalToFidl(CompressionAlgorithm algorithm) {
  using Fidl = llcpp::fuchsia::blobfs::internal::CompressionAlgorithm;
  switch (algorithm) {
    case CompressionAlgorithm::UNCOMPRESSED:
      return Fidl::UNCOMPRESSED;
    case CompressionAlgorithm::LZ4:
      return Fidl::LZ4;
    case CompressionAlgorithm::ZSTD:
      return Fidl::ZSTD;
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      return Fidl::ZSTD_SEEKABLE;
    case CompressionAlgorithm::CHUNKED:
      return Fidl::CHUNKED;
  }
}

zx::status<llcpp::fuchsia::blobfs::internal::CompressionAlgorithm>
    ExternalDecompressorClient::CompressionAlgorithmLocalToFidlForPartial(
        CompressionAlgorithm algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::CHUNKED:
      return zx::ok(llcpp::fuchsia::blobfs::internal::CompressionAlgorithm::CHUNKED_PARTIAL);
    case CompressionAlgorithm::UNCOMPRESSED:
    case CompressionAlgorithm::LZ4:
    case CompressionAlgorithm::ZSTD:
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
}

ExternalDecompressor::ExternalDecompressor(ExternalDecompressorClient* client,
                                           CompressionAlgorithm algorithm)
    : client_(client), algorithm_(algorithm) {}

zx_status_t ExternalDecompressor::Decompress(size_t uncompressed_size, size_t max_compressed_size) {
  return client_->SendMessage(
      {{0, uncompressed_size},
       {0, max_compressed_size},
       ExternalDecompressorClient::CompressionAlgorithmLocalToFidl(algorithm_)});
}

ExternalSeekableDecompressor::ExternalSeekableDecompressor(ExternalDecompressorClient* client,
                                                           SeekableDecompressor* decompressor)
    : client_(client), decompressor_(decompressor) {}

zx_status_t ExternalSeekableDecompressor::DecompressRange(size_t uncompressed_offset,
                                                          size_t uncompressed_size,
                                                          size_t max_compressed_size) {
  auto algorithm_or = ExternalDecompressorClient::CompressionAlgorithmLocalToFidlForPartial(
      decompressor_->algorithm());
  if (!algorithm_or.is_ok()) {
    return algorithm_or.status_value();
  }
  llcpp::fuchsia::blobfs::internal::CompressionAlgorithm fidl_algorithm = algorithm_or.value();

  size_t cumulative_uncompressed_size = 0;
  while (cumulative_uncompressed_size < uncompressed_size) {
    zx::status<CompressionMapping> mapping_or = decompressor_->MappingForDecompressedRange(
        uncompressed_offset + cumulative_uncompressed_size, 1);
    if (mapping_or.is_error()) {
      return mapping_or.status_value();
    }
    CompressionMapping mapping = mapping_or.value();

    // Ensure forward progess
    if (mapping.decompressed_length == 0 ||
        // Ensure uncompressed contiguity.
        uncompressed_offset + cumulative_uncompressed_size != mapping.decompressed_offset ||
        // Don't go past the end of the compressed buffer.
        mapping.compressed_offset + mapping.compressed_length > max_compressed_size) {
      // Most likely that the seek table is corrupted or manipulated.
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    // Don't got past the end of the uncompressed buffer.
    if (cumulative_uncompressed_size + mapping.decompressed_length > uncompressed_size) {
      // This needs to be done in entire chunks.
      return ZX_ERR_INVALID_ARGS;
    }

    // This has a bunch of stopping and starting, it would be better to pipeline
    // these to keep the server busy, but this repeated path will probably never
    // get exercised.
    zx_status_t status = client_->SendMessage({
        {mapping.decompressed_offset, mapping.decompressed_length},
        {mapping.compressed_offset, mapping.compressed_length},
        fidl_algorithm,
    });
    if (status != ZX_OK) {
      return status;
    }

    cumulative_uncompressed_size += mapping.decompressed_length;
  }
  return ZX_OK;
}

}  // namespace blobfs
