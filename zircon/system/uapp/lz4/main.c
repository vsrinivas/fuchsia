// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <lz4/lz4frame.h>

#define BLOCK_SIZE 65536

#define WR_NEWFILE O_WRONLY | O_CREAT | O_TRUNC
#define PERM_644 S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH

static void usage(const char* arg0) {
  printf("usage: %s [-1|-9] [-d] <input file> <output file>\n", arg0);
  printf("   -1  fast compression (default)\n");
  printf("   -9  high compression (slower)\n");
  printf("   -d  decompress\n");
}

static int do_decompress(const char* infile, const char* outfile) {
  int infd, outfd;

  infd = open(infile, O_RDONLY);
  if (infd < 0) {
    fprintf(stderr, "could not open %s: %s\n", infile, strerror(errno));
    return -1;
  }

  outfd = open(outfile, WR_NEWFILE, PERM_644);
  if (outfd < 0) {
    fprintf(stderr, "could not open %s: %s\n", outfile, strerror(errno));
    close(infd);
    return -1;
  }

  LZ4F_decompressionContext_t dctx;
  LZ4F_errorCode_t errc = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
  if (LZ4F_isError(errc)) {
    fprintf(stderr, "could not initialize decompression: %s\n", LZ4F_getErrorName(errc));
    close(outfd);
    close(infd);
    return -1;
  }

  uint8_t inbuf[BLOCK_SIZE];
  uint8_t outbuf[BLOCK_SIZE];

  // Read first 4 bytes to let LZ4 tell us how much it expects in the first
  // pass.
  size_t src_sz = 4;
  size_t dst_sz = 0;
  ssize_t nr = read(infd, inbuf, src_sz);
  if (nr < (ssize_t)src_sz) {
    fprintf(stderr, "could not read from %s", infile);
    if (nr < 0) {
      fprintf(stderr, ": %s", strerror(errno));
    }
    fprintf(stderr, "\n");
    goto done;
  }
  size_t to_read = LZ4F_decompress(dctx, outbuf, &dst_sz, inbuf, &src_sz, NULL);
  if (LZ4F_isError(to_read)) {
    fprintf(stderr, "could not decompress %s: %s\n", infile, LZ4F_getErrorName(to_read));
    goto done;
  }
  if (to_read > BLOCK_SIZE) {
    to_read = BLOCK_SIZE;
  }

  while ((nr = read(infd, inbuf, to_read)) > 0) {
    src_sz = nr;
    ssize_t pos = 0;
    size_t next = 0;

    while (pos < nr) {
      dst_sz = BLOCK_SIZE;
      next = LZ4F_decompress(dctx, outbuf, &dst_sz, inbuf + pos, &src_sz, NULL);
      if (LZ4F_isError(next)) {
        fprintf(stderr, "could not decompress %s: %s\n", infile, LZ4F_getErrorName(to_read));
        goto done;
      }

      if (dst_sz) {
        ssize_t nw = write(outfd, outbuf, dst_sz);
        if (nw != (ssize_t)dst_sz) {
          fprintf(stderr, "could not write to %s", outfile);
          if (nw < 0) {
            fprintf(stderr, ": %s", strerror(errno));
          }
          fprintf(stderr, "\n");
          goto done;
        }
      }
      pos += src_sz;
      src_sz = nr - pos;
    }

    to_read = next;
    if (to_read > BLOCK_SIZE || to_read == 0) {
      to_read = BLOCK_SIZE;
    }
  }

  if (nr < 0) {
    fprintf(stderr, "error reading %s: %s\n", infile, strerror(errno));
    goto done;
  }

done:
  LZ4F_freeDecompressionContext(dctx);
  close(outfd);
  close(infd);
  return 0;
}

static int do_compress(const char* infile, const char* outfile, int clevel) {
  int infd, outfd;

  infd = open(infile, O_RDONLY);
  if (infd < 0) {
    fprintf(stderr, "could not open %s: %s\n", infile, strerror(errno));
    return -1;
  }

  outfd = open(outfile, WR_NEWFILE, PERM_644);
  if (outfd < 0) {
    fprintf(stderr, "could not open %s: %s\n", outfile, strerror(errno));
    close(infd);
    return -1;
  }

  LZ4F_preferences_t prefs;
  memset(&prefs, 0, sizeof(prefs));
  prefs.compressionLevel = clevel;

  uint8_t inbuf[BLOCK_SIZE];
  size_t outsize = LZ4F_compressFrameBound(BLOCK_SIZE, &prefs);
  uint8_t* outbuf = malloc(outsize);
  if (!outbuf) {
    fprintf(stderr, "out of memory\n");
    close(outfd);
    close(infd);
    return ENOMEM;
  }

  // TODO: do the whole file in one frame, using the LZ4F begin/update/end
  // functions.
  ssize_t nr = 0;
  while ((nr = read(infd, inbuf, BLOCK_SIZE)) > 0) {
    ssize_t csz = LZ4F_compressFrame(outbuf, outsize, inbuf, nr, &prefs);
    if (LZ4F_isError(csz)) {
      fprintf(stderr, "error compressing %s: %s\n", infile, LZ4F_getErrorName(csz));
      goto done;
    }

    ssize_t nw = write(outfd, outbuf, csz);
    if (nw != csz) {
      fprintf(stderr, "could not write to %s", outfile);
      if (nw < 0) {
        fprintf(stderr, ": %s", strerror(errno));
      }
      fprintf(stderr, "\n");
      goto done;
    }
  }

  if (nr < 0) {
    fprintf(stderr, "error reading %s: %s\n", infile, strerror(errno));
    goto done;
  }

done:
  free(outbuf);
  close(outfd);
  close(infd);
  return 0;
}

int main(int argc, char* argv[]) {
  int clevel = 1;
  bool decompress = false;
  const char* infile = NULL;
  const char* outfile = NULL;

  for (int i = 1; i < argc; i++) {
    if (!strcmp("-d", argv[i])) {
      decompress = true;
      continue;
    }
    if (!strcmp("-9", argv[i])) {
      clevel = 9;
      continue;
    }
    if (!strcmp("-h", argv[i])) {
      usage(argv[0]);
      return 0;
    }

    if (!infile) {
      infile = argv[i];
      continue;
    }
    if (!outfile) {
      outfile = argv[i];
      continue;
    }

    fprintf(stderr, "Unknown argument: %s\n", argv[i]);
    usage(argv[0]);
    return -1;
  }

  if (!infile || !outfile) {
    usage(argv[0]);
    return 0;
  }

  printf("%scompressing %s into %s", decompress ? "de" : "", infile, outfile);
  if (!decompress) {
    printf(" at level %d", clevel);
  }
  printf("\n");

  if (decompress) {
    return do_decompress(infile, outfile);
  } else {
    return do_compress(infile, outfile, clevel);
  }
}
