// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression/external_decompressor.h"

#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/rights.h>

#include "src/lib/storage/vfs/cpp/debug.h"

namespace blobfs {

DecompressorCreatorConnector& DecompressorCreatorConnector::DefaultServiceConnector() {
  class ServiceConnector final : public DecompressorCreatorConnector {
   public:
    // DecompressorCreatorConnector interface.
    zx_status_t ConnectToDecompressorCreator(zx::channel remote_channel) final {
      return fdio_service_connect("/svc/fuchsia.blobfs.internal.DecompressorCreator",
                                  remote_channel.release());
    }
  };
  static ServiceConnector singleton{};
  return singleton;
}

zx::result<std::unique_ptr<ExternalDecompressorClient>> ExternalDecompressorClient::Create(
    DecompressorCreatorConnector* connector, const zx::vmo& decompressed_vmo,
    const zx::vmo& compressed_vmo) {
  std::unique_ptr<ExternalDecompressorClient> client;
  client.reset(new ExternalDecompressorClient());
  client->connector_ = connector;

  zx_status_t status =
      decompressed_vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS, &(client->decompressed_vmo_));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate decompressed VMO: " << zx_status_get_string(status);
    return zx::error(status);
  }
  status = compressed_vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS & (~ZX_RIGHT_WRITE),
                                    &(client->compressed_vmo_));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate compressed VMO: " << zx_status_get_string(status);
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
    FX_LOGS(ERROR) << "Failed to create remote duplicate of decompressed VMO: "
                   << zx_status_get_string(status);
    return status;
  }
  zx::vmo remote_compressed_vmo;
  status = compressed_vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, &remote_compressed_vmo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create remote duplicate of compressed VMO: "
                   << zx_status_get_string(status);
    return status;
  }

  zx::fifo remote_fifo;
  // Sized for 4 elements, allows enough pipelining to keep the remote process
  // from descheduling to have 2 in flight requests/response pairs.
  status = zx::fifo::create(4, sizeof(fuchsia_blobfs_internal::wire::DecompressRequest), 0, &fifo_,
                            &remote_fifo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed create fifo for external decompressor: "
                   << zx_status_get_string(status);
    return status;
  }

  zx_status_t fidl_status =
      decompressor_creator_->Create(std::move(remote_fifo), std::move(remote_compressed_vmo),
                                    std::move(remote_decompressed_vmo), &status);
  if (fidl_status == ZX_ERR_PEER_CLOSED) {
    decompressor_creator_.Unbind();
  }
  if (fidl_status != ZX_OK) {
    FX_LOGS(ERROR) << "FIDL error communicating with external decompressor: "
                   << zx_status_get_string(fidl_status);
    return fidl_status;
  }
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Error calling Create on DecompressorCreator service: "
                   << zx_status_get_string(status);
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
    FX_LOGS(ERROR) << "Failed to create channel pair for external decompressor.";
    return ZX_ERR_NO_RESOURCES;
  }

  zx_status_t status = connector_->ConnectToDecompressorCreator(remote_channel.TakeChannel());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to DecompressorCreator service: "
                   << zx_status_get_string(status);
    decompressor_creator_.Unbind();
  }
  return status;
}

zx_status_t ExternalDecompressorClient::SendMessage(
    const fuchsia_blobfs_internal::wire::DecompressRequest& request) {
  zx_status_t status;
  fuchsia_blobfs_internal::wire::DecompressResponse response;
  status = Prepare();
  if (status != ZX_OK) {
    return status;
  }

  status = fifo_.write(sizeof(request), &request, 1, nullptr);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to write fifo request to decompressor: "
                   << zx_status_get_string(status);
    return status;
  }

  zx_signals_t signal;
  fifo_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED, zx::time::infinite(), &signal);
  if ((signal & ZX_FIFO_READABLE) == 0) {
    fifo_.reset();
    FX_LOGS(ERROR) << "External decompressor closed the fifo.";
    return ZX_ERR_INTERNAL;
  }

  status = fifo_.read(sizeof(response), &response, 1, nullptr);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to read from fifo: " << zx_status_get_string(status);
    return status;
  }
  if (response.status != ZX_OK) {
    FX_LOGS(ERROR) << "Error from external decompressor: " << zx_status_get_string(status)
                   << " size: " << response.size;
    return response.status;
  }
  if (response.size != request.decompressed.size) {
    FX_LOGS(ERROR) << "Decompressed size did not match. Expected: " << request.decompressed.size
                   << " Got: " << response.size;
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  return ZX_OK;
}

std::optional<CompressionAlgorithm> ExternalDecompressorClient::CompressionAlgorithmFidlToLocal(
    const fuchsia_blobfs_internal::wire::CompressionAlgorithm algorithm) {
  using Fidl = fuchsia_blobfs_internal::wire::CompressionAlgorithm;
  switch (algorithm) {
    case Fidl::kUncompressed:
      return CompressionAlgorithm::kUncompressed;
    case Fidl::kChunked:
    case Fidl::kChunkedPartial:
      return CompressionAlgorithm::kChunked;
    default:
      return std::nullopt;
  }
}

fuchsia_blobfs_internal::wire::CompressionAlgorithm
ExternalDecompressorClient::CompressionAlgorithmLocalToFidl(CompressionAlgorithm algorithm) {
  using Fidl = fuchsia_blobfs_internal::wire::CompressionAlgorithm;
  switch (algorithm) {
    case CompressionAlgorithm::kUncompressed:
      return Fidl::kUncompressed;
    case CompressionAlgorithm::kChunked:
      return Fidl::kChunked;
  }

  ZX_DEBUG_ASSERT(false);
  return Fidl::kUncompressed;
}

zx::result<fuchsia_blobfs_internal::wire::CompressionAlgorithm>
ExternalDecompressorClient::CompressionAlgorithmLocalToFidlForPartial(
    CompressionAlgorithm algorithm) {
  switch (algorithm) {
    case CompressionAlgorithm::kChunked:
      return zx::ok(fuchsia_blobfs_internal::wire::CompressionAlgorithm::kChunkedPartial);
    case CompressionAlgorithm::kUncompressed:
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  ZX_DEBUG_ASSERT(false);
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

ExternalSeekableDecompressor::ExternalSeekableDecompressor(ExternalDecompressorClient* client,
                                                           CompressionAlgorithm algorithm)
    : client_(client), algorithm_(algorithm) {}

zx_status_t ExternalSeekableDecompressor::DecompressRange(size_t compressed_offset,
                                                          size_t compressed_size,
                                                          size_t uncompressed_size) {
  auto algorithm_or =
      ExternalDecompressorClient::CompressionAlgorithmLocalToFidlForPartial(algorithm_);
  if (!algorithm_or.is_ok()) {
    return algorithm_or.status_value();
  }
  fuchsia_blobfs_internal::wire::CompressionAlgorithm fidl_algorithm = algorithm_or.value();

  return client_->SendMessage({
      {0, uncompressed_size},
      {compressed_offset, compressed_size},
      fidl_algorithm,
  });
}

}  // namespace blobfs
