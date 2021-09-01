// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dirent_iterator.h"

#include <fidl/fuchsia.io2/cpp/wire.h>
#include <lib/zx/channel.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <zircon/syscalls.h>

#include <type_traits>

#include "../private.h"
#include "common_utils.h"
#include "remote_v2.h"

namespace fio2 = fuchsia_io2;

namespace {

// Implementation of |zxio_dirent_iterator_t| for |fuchsia.io2|.
class DirentIteratorImpl {
 public:
  static zx_status_t Create(zxio_dirent_iterator_t* iterator, zxio_t* directory) {
    auto iterator_ends = fidl::CreateEndpoints<fio2::DirectoryIterator>();
    if (!iterator_ends.is_ok()) {
      return iterator_ends.status_value();
    }
    RemoteV2 dir(directory);
    auto status =
        fidl::WireCall(fidl::UnownedClientEnd<fio2::Directory>(dir.control()))
            .Enumerate(fio2::wire::DirectoryEnumerateOptions(), std::move(iterator_ends->server))
            .status();
    if (status != ZX_OK) {
      return status;
    }
    new (iterator)
        DirentIteratorImpl(directory, fidl::BindSyncClient(std::move(iterator_ends->client)));
    return ZX_OK;
  }

  ~DirentIteratorImpl() = default;

  zx_status_t Next(zxio_dirent_t** out_entry) {
    if (index_ >= entries_.count()) {
      zx_status_t status = ReadNextBatch();
      if (status != ZX_OK) {
        return status;
      }
      if (entries_.count() == 0) {
        return ZX_ERR_NOT_FOUND;
      }
      index_ = 0;
    }

    const auto& entry = entries_[index_];
    index_++;

    if (!entry.has_name() || entry.name().size() > fio2::wire::kMaxNameLength) {
      return ZX_ERR_INVALID_ARGS;
    }

    boxed_->current_entry = {};
    boxed_->current_entry.name = boxed_->current_entry_name;
    if (entry.has_protocols()) {
      ZXIO_DIRENT_SET(boxed_->current_entry, protocols, ToZxioNodeProtocols(entry.protocols()));
    }
    if (entry.has_abilities()) {
      ZXIO_DIRENT_SET(boxed_->current_entry, abilities, ToZxioAbilities(entry.abilities()));
    }
    if (entry.has_id()) {
      ZXIO_DIRENT_SET(boxed_->current_entry, id, entry.id());
    }
    boxed_->current_entry.name_length = static_cast<uint8_t>(entry.name().size());
    memcpy(boxed_->current_entry_name, entry.name().data(), entry.name().size());
    boxed_->current_entry_name[entry.name().size()] = '\0';
    *out_entry = &boxed_->current_entry;

    return ZX_OK;
  }

 private:
  explicit DirentIteratorImpl(zxio_t* io, fidl::WireSyncClient<fio2::DirectoryIterator> iterator)
      : io_(reinterpret_cast<zxio_remote_v2_t*>(io)),
        boxed_(std::make_unique<Boxed>()),
        iterator_(std::move(iterator)) {
    static_assert(offsetof(DirentIteratorImpl, io_) == 0,
                  "zxio_dirent_iterator_t requires first field of implementation to be zxio_t");
    (void)io_;
    (void)opaque_;
  }

  zx_status_t ReadNextBatch() {
    auto result = iterator_.GetNext(boxed_->response_buffer.view());
    if (result.status() != ZX_OK) {
      return result.status();
    }
    if (result->result.is_err()) {
      return result->result.err();
    }
    auto& response = result->result.mutable_response();
    entries_ = std::move(response.entries);
    return ZX_OK;
  }

  // This large structure is heap-allocated once, to be reused by subsequent
  // ReadDirents calls.
  struct Boxed {
    Boxed() = default;

    // Buffer used by the FIDL calls.
    fidl::Buffer<fidl::WireResponse<fio2::DirectoryIterator::GetNext>> response_buffer;

    // At each |zxio_dirent_iterator_next| call, we would extract the next
    // dirent segment from |response_buffer|, and populate |current_entry|
    // and |current_entry_name|.
    zxio_dirent_t current_entry;
    char current_entry_name[fio2::wire::kMaxNameLength + 1] = {};
  };

  // The first field must be some kind of |zxio_t| pointer, to be compatible
  // with the layout of |zxio_dirent_iterator_t|.
  zxio_remote_v2_t* io_;

  std::unique_ptr<Boxed> boxed_;
  fidl::VectorView<fio2::wire::DirectoryEntry> entries_ = {};
  uint64_t index_ = 0;
  fidl::WireSyncClient<fio2::DirectoryIterator> iterator_;
  uint64_t opaque_[2];
};

static_assert(sizeof(zxio_dirent_iterator_t) == sizeof(DirentIteratorImpl),
              "zxio_dirent_iterator_t should match DirentIteratorImpl");

}  // namespace

zx_status_t zxio_remote_v2_dirent_iterator_init(zxio_t* directory,
                                                zxio_dirent_iterator_t* iterator) {
  return DirentIteratorImpl::Create(iterator, directory);
}

zx_status_t zxio_remote_v2_dirent_iterator_next(zxio_t* io, zxio_dirent_iterator_t* iterator,
                                                zxio_dirent_t** out_entry) {
  return reinterpret_cast<DirentIteratorImpl*>(iterator)->Next(out_entry);
}

void zxio_remote_v2_dirent_iterator_destroy(zxio_t* io, zxio_dirent_iterator_t* iterator) {
  reinterpret_cast<DirentIteratorImpl*>(iterator)->~DirentIteratorImpl();
}
