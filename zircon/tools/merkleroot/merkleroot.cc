// Copyright 2017, 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_fd.h>

namespace {

using digest::Digest;
using digest::MerkleTreeCreator;

struct FileEntry {
  std::string filename;
  char digest[digest::kSha256HexLength + 1]{};
};

void usage(char** argv) {
  fprintf(stderr, "Usage: %s [-o OUTPUT | -m MANIFEST] FILE...\n", argv[0]);
  fprintf(stderr,
          "\n\
With -o, OUTPUT gets the same format normally written to stdout: HASH - FILE.\n\
With -m, MANIFEST gets \"manifest file\" format: HASH=FILE.\n\
Any argument may be \"@RSPFILE\" to be replaced with the contents of RSPFILE.\n\
");
  exit(1);
}

int handle_argument(char** argv, const char* arg, std::vector<FileEntry>* entries) {
  if (arg[0] == '@') {
    FILE* rspfile = fopen(&arg[1], "r");
    if (!rspfile) {
      perror(&arg[1]);
      return 1;
    }
    while (!feof(rspfile) && !ferror(rspfile)) {
      // 2018 macOS hasn't caught up with C99 yet, so can't use %ms here.
      char filename[4096];
      if (fscanf(rspfile, " %4095s", filename) == 1) {
        handle_argument(argv, filename, entries);
      }
    }
    int result = ferror(rspfile);
    if (result) {
      perror(&arg[1]);
    }
    fclose(rspfile);
    return result;
  } else {
    entries->push_back({arg});
    return 0;
  }
}

void handle_entry(FileEntry* entry) {
  fbl::unique_fd fd{open(entry->filename.c_str(), O_RDONLY)};
  if (!fd) {
    perror(entry->filename.c_str());
    exit(1);
  }

  struct stat info;
  if (fstat(fd.get(), &info) < 0) {
    perror("fstat");
    exit(1);
  }
  if (!S_ISREG(info.st_mode)) {
    return;
  }

  // Buffer one intermediate node's worth at a time.
  void* data = nullptr;
  if (info.st_size != 0) {
    data = mmap(NULL, info.st_size, PROT_READ, MAP_SHARED, fd.get(), 0);
  }
  if (info.st_size != 0 && data == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }
  std::unique_ptr<uint8_t[]> tree;
  size_t len;
  Digest digest;
  zx_status_t rc = MerkleTreeCreator::Create(data, info.st_size, &tree, &len, &digest);
  if (info.st_size != 0 && munmap(data, info.st_size) != 0) {
    perror("munmap");
    exit(1);
  }
  if (rc != ZX_OK) {
    fprintf(stderr, "%s: Merkle tree creation failed: %d\n", entry->filename.c_str(), rc);
    exit(1);
  }
  snprintf(entry->digest, sizeof(entry->digest), "%s", digest.ToString().c_str());
}

}  // namespace

int main(int argc, char** argv) {
  FILE* outf = stdout;
  if (argc < 2) {
    usage(argv);
  }

  int argi = 1;
  bool manifest = !strcmp(argv[1], "-m");
  if (manifest || !strcmp(argv[1], "-o")) {
    if (argc < 4) {
      usage(argv);
    }
    argi = 3;
    outf = fopen(argv[2], "w");
    if (!outf) {
      perror(argv[2]);
      return 1;
    }
  }

  std::vector<FileEntry> entries;
  for (; argi < argc; ++argi) {
    if (handle_argument(argv, argv[argi], &entries))
      return 1;
  }

  std::vector<std::thread> threads;
  std::mutex mtx;
  size_t next_entry = 0;
  size_t n_threads = std::thread::hardware_concurrency();
  if (!n_threads) {
    n_threads = 4;
  }
  if (n_threads > entries.size()) {
    n_threads = entries.size();
  }
  for (size_t i = n_threads; i > 0; --i) {
    threads.push_back(std::thread([&] {
      while (true) {
        mtx.lock();
        auto j = next_entry++;
        mtx.unlock();
        if (j >= entries.size()) {
          return;
        }
        handle_entry(&entries[j]);
      }
    }));
  }
  for (unsigned i = 0; i < threads.size(); ++i) {
    threads[i].join();
  }

  for (const auto& entry : entries) {
    fprintf(outf, "%s%s%s\n", entry.digest, manifest ? "=" : " - ", entry.filename.c_str());
  }

  return 0;
}
