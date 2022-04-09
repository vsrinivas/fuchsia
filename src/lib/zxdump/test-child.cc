// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <lib/stdcompat/span.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <vector>

#include <fbl/unique_fd.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

namespace {

constexpr std::string_view kStdinoutFilename = "-";

constexpr char kOptString[] = "c:e:o:t:x:";
constexpr option kLongOpts[] = {
    {"cat-from", required_argument, nullptr, 'c'},  //
    {"cat-to", required_argument, nullptr, 'o'},    //
    {"echo", required_argument, nullptr, 'e'},      //
    {"threads", required_argument, nullptr, 't'},   //
    {"exit", required_argument, nullptr, 'x'},      //
};

int Usage() {
  fprintf(stderr, "Usage: test-child [--echo=STRING] [--cat=FILE] [--threads=N]\n");
  return 1;
}

[[noreturn]] void Hang() {
  while (true) {
#ifdef __Fuchsia__
    zx_thread_legacy_yield(0);
#else
    pause();
#endif
  }
}

void Cat(fbl::unique_fd from, int to) {
  char buf[BUFSIZ];
  ssize_t nread;
  while ((nread = read(from.get(), buf, sizeof(buf))) > 0) {
    cpp20::span<const char> chunk(buf, static_cast<size_t>(nread));
    while (!chunk.empty()) {
      ssize_t nwrote = write(to, chunk.data(), chunk.size());
      if (nwrote < 0) {
        perror("write");
        exit(2);
      }
      chunk = chunk.subspan(static_cast<size_t>(nwrote));
    }
  }
  if (nread < 0) {
    perror("read");
    exit(2);
  }
}

fbl::unique_fd CatOpen(const char* filename, int stdfd, int oflags) {
  fbl::unique_fd fd{filename == kStdinoutFilename ? stdfd : open(filename, oflags, 0666)};
  if (!fd) {
    perror(filename);
    exit(2);
  }
  return fd;
}

void CatFrom(const char* filename) {
  Cat(CatOpen(filename, STDIN_FILENO, O_RDONLY), STDOUT_FILENO);
}

void CatTo(const char* filename) {
  Cat(fbl::unique_fd{STDIN_FILENO},
      CatOpen(filename, STDOUT_FILENO, O_WRONLY | O_CREAT | O_EXCL).get());
}

}  // namespace

int main(int argc, char** argv) {
  size_t thread_count = 0;

  while (true) {
    switch (getopt_long(argc, argv, kOptString, kLongOpts, nullptr)) {
      case -1:
        // This ends the loop.  All other cases continue (or return).
        break;

      case 'c':
        CatFrom(optarg);
        continue;

      case 'o':
        CatTo(optarg);
        continue;

      case 'e':
        puts(optarg);
        continue;

      case 't':
        thread_count = atoi(optarg);
        continue;

      case 'x':
        return atoi(optarg);

      default:
        return Usage();
    }
    break;
  }
  if (optind != argc) {
    return Usage();
  }

  std::vector<std::thread> threads(thread_count);
  for (std::thread& thread : threads) {
    thread = std::thread(Hang);
  }
  if (thread_count > 0) {
    printf("started %zu additional threads\n", thread_count);
  }

  Hang();

  return 0;
}
