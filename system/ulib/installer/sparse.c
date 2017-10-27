// Copyright 2017 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <installer/sparse.h>

#define FOUR_K (4 * 1024)

// TODO(jmatt) endianness could be an issue, read/write in defined endianness
int unsparse(int src, int dst, uint8_t *buf, size_t sz) {
  chunk_t chunk;
  off_t prev_off = 0;

  while (readn(src, &chunk, sizeof(chunk)) == sizeof(chunk)) {
    if (chunk.start < prev_off) {
      if (chunk.start == 0) {
        // last header has a start value of 0
        ftruncate(dst, chunk.len);
        return 0;
      } else {
        // it isn't legal to go backwards
        return -1;
      }
    }

    if (lseek(dst, chunk.start, SEEK_SET) != chunk.start) {
      return -1;
    }

    if (copyn(src, dst, chunk.len, buf, sz) != chunk.len) {
      return -1;
    }

    prev_off = chunk.start + chunk.len;
  }
  return 0;
}

void init_unsparse_ctx(unsparse_ctx_t *c) {
  c->chunk.start = 0;
  c->chunk.len = 0;
  c->prev_start = 0;
  c->remaining = sizeof(c->chunk);
}

ssize_t unsparse_buf(uint8_t *buf, const size_t len, unsparse_ctx_t *ctx,
                     int dst) {
  size_t sz = len;
  while (sz > 0) {
    // see if the context is incomplete
    if (ctx->remaining > 0) {
      uint8_t *p = (uint8_t*) &ctx->chunk;
      p += sizeof(ctx->chunk) - ctx->remaining;

      size_t cpy_sz = (size_t) ctx->remaining < sz ? (size_t) ctx->remaining : sz;
      memcpy(p, buf, cpy_sz);
      ctx->remaining -= cpy_sz;
      sz -= cpy_sz;
      buf += cpy_sz;
    }

    if (sz <= 0) {
      break;
    }

    ssize_t wr_sz = (size_t) ctx->chunk.len > sz ? sz : (size_t) ctx->chunk.len;
    if (lseek(dst, ctx->chunk.start, SEEK_SET) != ctx->chunk.start) {
      return -1;
    }

    ssize_t r = writen(dst, buf, wr_sz);
    ctx->chunk.len -= r;
    ctx->chunk.start += r;
    sz -= (size_t) r;
    buf += r;

    if (r < 0) {
      return -1;
    }

    if (wr_sz != r) {
      return len - sz;
    }

    if (sz <= 0) {
      break;
    }

    ctx->prev_start = ctx->chunk.start;

    // read a new chunk header
    ctx->remaining = sizeof(ctx->chunk);
    wr_sz = (size_t) ctx->remaining < sz ? (size_t) ctx->remaining : sz;
    memcpy(&ctx->chunk, buf, wr_sz);
    ctx->remaining -= wr_sz;
    sz -= wr_sz;
    buf += wr_sz;
  }

  // if the sparse header is complete and this is the last sparse header,
  // truncate the file to the correct size
  if (ctx->remaining == 0 && ctx->chunk.start < ctx->prev_start) {
    if (ctx->chunk.start == 0) {
      ftruncate(dst, ctx->chunk.len);
    } else {
      // we moved backwards
      return -1;
    }
  }

  return len;
}

int sparse(int src, int dst, uint8_t *buf, size_t sz) {
  #ifndef __Fuchsia__
  chunk_t chunk;
  chunk.start = 0;
  chunk.len = 0;
  off_t pos = 0;

  struct stat info;
  fstat(src, &info);
  // TODO(jmatt) support non-4K files
  if (info.st_size % FOUR_K != 0) {
    fprintf(stderr, "Incompatible file size, must be a multiple of %i\n",
            FOUR_K);
    return -1;
  }

  #if defined(__APPLE__) && defined(__MACH__)
  pos = lseek(src, 0, SEEK_SET);
  if (pos != 0) {
    return -1;
  }

  chunk.len = info.st_size;
  if (sizeof(chunk) != writen(dst, &chunk, sizeof(chunk))) {
    return -1;
  }

  if (copyn(src, dst, chunk.len, buf, sz) != chunk.len) {
    return -1;
  }
  #else
  while ((pos = lseek(src, chunk.start + chunk.len, SEEK_DATA)) >= 0) {
    // 4K-align the start
    chunk.start = pos - pos % FOUR_K;

    off_t end = lseek(src, chunk.start, SEEK_HOLE);
    off_t leftover = end % FOUR_K;
    if (leftover != 0) {
      end += (FOUR_K - leftover);
    }

    chunk.len = end - chunk.start;
    if (chunk.len < 0) {
      return -1;
    }

    if (lseek(src, chunk.start, SEEK_SET) != chunk.start) {
      return -1;
    }
    if (sizeof(chunk) != writen(dst, &chunk, sizeof(chunk))) {
      return -1;
    }

    if (copyn(src, dst, chunk.len, buf, sz) != chunk.len) {
      return -1;
    }
  }
  #endif

  chunk.start = 0;
  chunk.len = info.st_size;
  if (sizeof(chunk) != writen(dst, &chunk, sizeof(chunk))) {
    return -1;
  }
  return 0;
  #endif
  #ifdef __Fuchsia__
  fprintf(stderr, "ERROR:Sparsing not supported on Fuchsia!\n");
  return -1;
  #endif
}

ssize_t readn(int fd, void *d, const size_t sz) {
  ssize_t r;
  uint8_t *data = (uint8_t*) d;
  size_t len = sz;
  while(len > 0) {
    r = read(fd, data, len);
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      } else {
        return r;
      }
    }
    len -= r;
    data += r;
  }

  return sz - len;
}

ssize_t writen(int fd, void *d, const size_t sz) {
  ssize_t r;
  uint8_t *data = (uint8_t*) d;
  size_t len = sz;
  while (len > 0) {
    r = write(fd, data, len);
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      } else {
        return -1;
      }
    } else if (r == 0) {
      // this is only known to happen if you try to write directly to a
      // block device and the size of the write is not a multiple of block size
      break;
    }

    len -= r;
    data += r;
  }

  return sz - len;
}

ssize_t copyn(int src, int dst, const size_t sz, uint8_t *buf, size_t buf_sz) {
  size_t len = sz;
  while (len > 0) {
    size_t chunk = buf_sz > len ? len : buf_sz;
    if (readn(src, buf, chunk) != (ssize_t) chunk) {
      return sz - len;
    }
    ssize_t r = writen(dst, buf, chunk);
    if (r != (ssize_t) chunk) {
      return r > 0 ? sz - len - r : sz - len;
    }

    len -= chunk;
  }

  return sz;
}
