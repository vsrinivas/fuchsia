// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dirent_iterator.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/zx/channel.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <zircon/fidl.h>
#include <zircon/syscalls.h>

#include <type_traits>

#include "../private.h"
#include "common_utils.h"
#include "remote_v2.h"

namespace fio = fuchsia_io;

namespace {

// Implementation of |zxio_dirent_iterator_t| for |fuchsia.io|.
class DirentIteratorImpl {
 public:
  static zx_status_t Create(zxio_dirent_iterator_t* iterator, zxio_t* directory) {
    zx::status iterator_ends = fidl::CreateEndpoints<fio::DirectoryIterator>();
    if (iterator_ends.is_error()) {
      return iterator_ends.status_value();
    }
    RemoteV2 dir(directory);
    const fidl::WireResult result =
        fidl::WireCall(fidl::UnownedClientEnd<fio::Directory2>(dir.control()))
            ->Enumerate(fio::wire::DirectoryEnumerateOptions(), std::move(iterator_ends->server));
    if (!result.ok()) {
      return result.status();
    }
    new (iterator)
        DirentIteratorImpl(directory, fidl::BindSyncClient(std::move(iterator_ends->client)));
    return ZX_OK;
  }

  ~DirentIteratorImpl() = default;

  zx_status_t Next(zxio_dirent_t* inout_entry) {
    if (index_ >= fidl_entries_.count()) {
      zx_status_t status = ReadNextBatch();
      if (status != ZX_OK) {
        return status;
      }
      if (fidl_entries_.count() == 0) {
        return ZX_ERR_NOT_FOUND;
      }
      index_ = 0;
    }

    const fio::wire::DirectoryEntry& fidl_entry = fidl_entries_[index_];
    index_++;

    if (!fidl_entry.has_name() || fidl_entry.name().size() > fio::wire::kMaxNameLength) {
      return ZX_ERR_INVALID_ARGS;
    }

    if (fidl_entry.has_protocols()) {
      ZXIO_DIRENT_SET(*inout_entry, protocols, ToZxioNodeProtocols(fidl_entry.protocols()));
    }
    if (fidl_entry.has_abilities()) {
      ZXIO_DIRENT_SET(*inout_entry, abilities, ToZxioAbilities(fidl_entry.abilities()));
    }
    if (fidl_entry.has_id()) {
      ZXIO_DIRENT_SET(*inout_entry, id, fidl_entry.id());
    }
    inout_entry->name_length = static_cast<uint8_t>(fidl_entry.name().size());
    memcpy(inout_entry->name, fidl_entry.name().data(), inout_entry->name_length);

    return ZX_OK;
  }

 private:
  DirentIteratorImpl(zxio_t* io, fidl::WireSyncClient<fio::DirectoryIterator> iterator)
      : io_(reinterpret_cast<zxio_remote_v2_t*>(io)), iterator_(std::move(iterator)) {
    static_assert(offsetof(DirentIteratorImpl, io_) == 0,
                  "zxio_dirent_iterator_t requires first field of implementation to be zxio_t");
    (void)io_;
  }

  zx_status_t ReadNextBatch() {
    fidl::BufferSpan fidl_buffer(buffer_, sizeof(buffer_));
    const fidl::WireUnownedResult result = iterator_.buffer(fidl_buffer)->GetNext();
    if (!result.ok()) {
      return result.status();
    }
    const auto& response = result.value();
    switch (response.result.Which()) {
      case fio::wire::DirectoryIteratorGetNextResult::Tag::kErr:
        return response.result.err();
      case fio::wire::DirectoryIteratorGetNextResult::Tag::kResponse:
        fidl_entries_ = response.result.response().entries;
        return ZX_OK;
    }
  }

  zxio_remote_v2_t* io_;

  // Issuing a FIDL call requires storage for both the request (16 bytes) and a potentially
  // maximally sized channel message response (64KiB).
  // TODO(https://fxbug.dev/85843): Once overlapping request and response is allowed, reduce
  // this allocation to a single channel message size.
  FIDL_ALIGNDECL uint8_t
      buffer_[fidl::SyncClientMethodBufferSizeInChannel<fio::DirectoryIterator::GetNext>()];

  fidl::VectorView<fio::wire::DirectoryEntry> fidl_entries_ = {};
  uint64_t index_ = 0;
  fidl::WireSyncClient<fio::DirectoryIterator> iterator_;
};

static_assert(sizeof(DirentIteratorImpl) <= sizeof(zxio_dirent_iterator_t),
              "DirentIteratorImpl must fit inside a zxio_dirent_iterator_t");

}  // namespace

zx_status_t zxio_remote_v2_dirent_iterator_init(zxio_t* directory,
                                                zxio_dirent_iterator_t* iterator) {
  return DirentIteratorImpl::Create(iterator, directory);
}

zx_status_t zxio_remote_v2_dirent_iterator_next(zxio_t* io, zxio_dirent_iterator_t* iterator,
                                                zxio_dirent_t* inout_entry) {
  return reinterpret_cast<DirentIteratorImpl*>(iterator)->Next(inout_entry);
}

void zxio_remote_v2_dirent_iterator_destroy(zxio_t* io, zxio_dirent_iterator_t* iterator) {
  reinterpret_cast<DirentIteratorImpl*>(iterator)->~DirentIteratorImpl();
}
