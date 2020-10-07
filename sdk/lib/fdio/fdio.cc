// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fdio.h>
#include <lib/zxio/null.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>

#include "private.h"

struct fdio {
  // The operation function table which encapsulates specialized I/O
  // transport under a common interface.
  const fdio_ops_t* ops;

  // The number of references on this object. Note that each appearance
  // in the fd table counts as one reference on the corresponding object.
  // Ongoing operations will also contribute to the refcount.
  std::atomic_int_fast32_t refcount;

  // The number of times this fdio object appears in the fd table.
  int32_t dupcount;

  // |ioflag| contains mutable properties of this object, shared by
  // different transports. Possible values are |IOFLAG_*| in private.h.
  uint32_t ioflag;

  // The zxio object, if the zxio transport is selected in |ops|.
  zxio_storage_t storage;

  // Used to implement SO_RCVTIMEO. See `man 7 socket` for details.
  zx::duration rcvtimeo;

  // Used to implement SO_SNDTIMEO. See `man 7 socket` for details.
  zx::duration sndtimeo;
};

// fdio_reserved_io is a globally shared fdio_t that is used to represent a
// reservation in the fdtab. If a user observes fdio_reserved_io there is a race
// condition in their code or they are looking up fd's by number.
// fdio_reserved_io is used in the time between a user requesting an operation
// that creates and fd, and the time when a remote operation to create the
// backing fdio_t is created, without holding the fdtab lock. Examples include
// open() of a file, or accept() on a socket.
static fdio_t fdio_reserved_io = {
    // TODO(raggi): It may be ideal to replace these operations with ones that
    // more directly encode the result that a user must have implemented a race
    // in order to invoke them.
    .ops = nullptr,
    .refcount = 1,
    .dupcount = 1,
    .ioflag = 0,
    .storage = {},
    .rcvtimeo = zx::duration::infinite(),
    .sndtimeo = zx::duration::infinite(),
};

fdio_t* fdio_get_reserved_io() { return &fdio_reserved_io; }

__EXPORT
zxio_t* fdio_get_zxio(fdio_t* io) { return &io->storage.io; }

const fdio_ops_t* fdio_get_ops(const fdio_t* io) { return io->ops; }

int32_t fdio_get_dupcount(const fdio_t* io) { return io->dupcount; }

void fdio_dupcount_acquire(fdio_t* io) { io->dupcount++; }

void fdio_dupcount_release(fdio_t* io) { io->dupcount--; }

uint32_t* fdio_get_ioflag(fdio_t* io) { return &io->ioflag; }

zxio_storage_t* fdio_get_zxio_storage(fdio_t* io) { return &io->storage; }

fdio_t* fdio_alloc(const fdio_ops_t* ops) {
  return new fdio_t{
      .ops = ops,
      .refcount = 1,
      .dupcount = 0,
      .ioflag = 0,
      .storage = {},
      .rcvtimeo = zx::duration::infinite(),
      .sndtimeo = zx::duration::infinite(),
  };
}

zx::duration* fdio_get_rcvtimeo(fdio_t* io) { return &io->rcvtimeo; }
zx::duration* fdio_get_sndtimeo(fdio_t* io) { return &io->sndtimeo; }

void fdio_acquire(fdio_t* io) { io->refcount.fetch_add(1); }

zx_status_t fdio_release(fdio_t* io) {
  if (io->refcount.fetch_sub(1) == 1) {
    zx_status_t status = fdio_get_ops(io)->close(io);
    delete io;
    return status;
  }
  return ZX_OK;
}

bool fdio_is_last_reference(fdio_t* io) { return io->refcount.load() == 1; }
