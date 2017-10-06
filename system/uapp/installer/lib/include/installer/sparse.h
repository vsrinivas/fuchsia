// Copyright 2017 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.

#include <stdint.h>
#include <sys/types.h>

typedef struct chunk {
  off_t start;
  off_t len;
} chunk_t;

typedef struct unsparse_ctx {
  chunk_t chunk;
  // represents the number of bytes needed to complete reading the chunk_t
  off_t remaining;
  off_t prev_start;

} unsparse_ctx_t;

/*
 * Take the src file descriptor and unsparse the content of the file to the
 * dst file descriptor. buf should point to a memory buffer of size sz which
 * can be used during unsparsing of the file. Returns 0 on success.
 */
int unsparse(int src, int dst, uint8_t *buf, size_t sz);

/*
 * unsparse_buf expands up to sz bytes from the head of buf, writing the output
 * to the dst file descriptor. ctx must be initialized before the first call to
 * unsparse_buf and the same unsparse_ctx_t should be passed to subsequent calls
 * of unsparse_buf. Each call returns the number of bytes consumed from buf or a
 * negative value if an error is encountered. It is not considered an error if
 * no bytes are consumed, although it is unexpected.
 */
ssize_t unsparse_buf(uint8_t *buf, size_t sz, unsparse_ctx_t *ctx, int dst);
/*
 * Takes a file descriptor src and writes a sparse copy of the file to dst using
 * buf of size sz as a read buffer to read src. Returns 0 on success.
 */
int sparse(int src, int dst, uint8_t *buf, size_t sz);
/*
 * readn reads len bytes from the provided file descriptor and writes them into
 * data. If EOF is encountered before len is reached, EOF is returned. In this
 * case the caller does not know how many bytes were read from the file. On
 * success the return value will be equal to len, otherwise the return value is
 * the error returned from read().
 *
 * NOTE: This function is designed for cases where the data size to be read is
 * known and it is not ergonomic in cases where this value is unknown.
 */
ssize_t readn(int fd, void *data, size_t len);
/*
 * writen tries to write len bytes to the provided file descriptor from
 * data. If an error is encountered a negative value will be returned. Otherwise
 * the number of bytes written will be returned. The bytes written may be less
 * than len if the file descriptor doesn't accept all the bytes, but doesn't
 * return an error.
 */
ssize_t writen(int fd, void *data, size_t len);
/*
 * copyn copies len bytes from src to dst using buf of size buf_sz as a read
 * buffer. If a read or write error is encountered, a negative value is
 * returned. If the value returned is less than len not all the requested bytes
 * were copied. The returned value indicates the number of bytes actually
 * written to dst, which may be fewer than the number read from src.
 */
ssize_t copyn(int src, int dst, size_t len, uint8_t *buf, size_t buf_sz);

/*
 * init_unsparse_ctx initializes an unsparse_ctx_t An unsparse_ctx_t tracks
 * progress of an unsparsing session when using the unsparse_buf() function.
 */
void init_unsparse_ctx(unsparse_ctx_t *c);
