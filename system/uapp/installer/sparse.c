// Copyright 2017 The Fuchsia Authors. All rights reserved.
// User of this source code is governed by a BSD-style license that be be found
// in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <installer/sparse.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int open_files(char *src, char *dst, int* out) {
  out[0] = open(src, O_RDONLY);
  if (out[0] < 0) {
    fprintf(stderr, "error: failed opening '%s' for reading\n", src);
    return -1;
  }

  out[1] = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (out[1] < 0) {
    close(out[0]);
    fprintf(stderr, "error: failed opening '%s' for writing\n", src);
    return -1;
  }
  return 0;
}

static void usage() {
    fprintf(stderr, "Command not understood\n");
    fprintf(stderr, "  usage: sparser [-s|-u] <infile> <outfile>\n");
}

int main(int argc, char **argv) {
  uint8_t buf[256 * 1024];
  int fds[2] = {-1,-1};

  if (argc != 4) {
    usage();
    return -1;
  }

  int mksparse = 0;
  if (!strcmp("-u", argv[1])) {
    mksparse = 1;
  } else if (!strcmp("-s", argv[1])) {
    mksparse = -1;
  } else {
    usage();
    return -1;
  }

  if (open_files(argv[2], argv[3], fds)) {
    return -1;
  }

  if (mksparse > 0) {
    if (!unsparse(fds[0], fds[1], buf, sizeof(buf))) {
      fprintf(stdout, "File unsparsed successfully\n");
    } else {
      fprintf(stdout, "Unsparsing file failed.\n");
      return -1;
    }
  } else {
    if (!sparse(fds[0], fds[1], buf, sizeof(buf))) {
      fprintf(stdout, "File sparsing successful.\n");
    } else {
      fprintf(stdout, "Error when sparsing file.\n");
      return -1;
    }
  }

  close(fds[0]);

  if (close(fds[1])) {
    fprintf(stderr, "error: %s when closing destination\n", strerror(errno));
    return -1;
  }

  return 0;
}
