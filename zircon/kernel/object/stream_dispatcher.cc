// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/stream_dispatcher.h"

#include <lib/counters.h>
#include <zircon/rights.h>

#include <fbl/alloc_checker.h>

KCOUNTER(dispatcher_stream_create_count, "dispatcher.stream.create")
KCOUNTER(dispatcher_stream_destroy_count, "dispatcher.stream.destroy")

// static
zx_status_t StreamDispatcher::Create(uint32_t options, fbl::RefPtr<VmObjectDispatcher> vmo,
                                     zx_off_t seek, KernelHandle<StreamDispatcher>* handle,
                                     zx_rights_t* rights) {
  fbl::AllocChecker ac;
  KernelHandle new_handle(fbl::AdoptRef(new (&ac) StreamDispatcher(options, ktl::move(vmo), seek)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_rights_t new_rights = default_rights();

  if (options & ZX_STREAM_MODE_READ) {
    new_rights |= ZX_RIGHT_READ;
  }
  if (options & ZX_STREAM_MODE_WRITE) {
    new_rights |= ZX_RIGHT_WRITE;
  }

  *rights = new_rights;
  *handle = ktl::move(new_handle);
  return ZX_OK;
}

StreamDispatcher::StreamDispatcher(uint32_t options, fbl::RefPtr<VmObjectDispatcher> vmo,
                                   zx_off_t seek)
    : options_(options), vmo_(ktl::move(vmo)), seek_(seek) {
  kcounter_add(dispatcher_stream_create_count, 1);
  (void)options_;
}

StreamDispatcher::~StreamDispatcher() { kcounter_add(dispatcher_stream_destroy_count, 1); }

zx_status_t StreamDispatcher::Seek(zx_stream_seek_origin_t whence, int64_t offset,
                                   zx_off_t* out_seek) {
  canary_.Assert();

  Guard<fbl::Mutex> guard{get_lock()};

  zx_off_t target;
  switch (whence) {
    case ZX_STREAM_SEEK_ORIGIN_START: {
      if (offset < 0) {
        return ZX_ERR_INVALID_ARGS;
      }
      target = static_cast<zx_off_t>(offset);
      break;
    }
    case ZX_STREAM_SEEK_ORIGIN_CURRENT: {
      if (add_overflow(seek_, offset, &target)) {
        return ZX_ERR_INVALID_ARGS;
      }
      break;
    }
    case ZX_STREAM_SEEK_ORIGIN_END: {
      uint64_t content_size = vmo_->GetContentSize();
      if (add_overflow(content_size, offset, &target)) {
        return ZX_ERR_INVALID_ARGS;
      }
      break;
    }
    default: {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  seek_ = target;
  *out_seek = seek_;
  return ZX_OK;
}
