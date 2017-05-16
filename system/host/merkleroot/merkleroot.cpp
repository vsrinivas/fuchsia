// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <mxalloc/new.h>
#include <merkle/digest.h>
#include <merkle/tree.h>
#include <mxtl/unique_ptr.h>

int main(int argc, char** argv) {
    if (argc == 1) {
        fprintf(stderr, "[-] missing input file.\n");
        fprintf(stderr, "usage: %s <filename>\n", argv[0]);
        return 1;
    }
    // Buffer one intermediate node's worth at a time.
    struct stat info;
    merkle::Tree mt;
    AllocChecker ac;
    void* data = nullptr;
    mxtl::unique_ptr<uint8_t[]> tree(nullptr);
    char strbuf[merkle::Digest::kLength * 2 + 1];
    merkle::Digest digest;
    for (size_t i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (stat(arg, &info) < 0) {
            perror("stat");
            fprintf(stderr, "[-] Unable to stat '%s'.\n", arg);
            fprintf(stderr, "usage: %s <filename>\n", argv[0]);
            return 1;
        }
        if (!S_ISREG(info.st_mode)) {
            continue;
        }
        size_t tree_len = merkle::Tree::GetTreeLength(info.st_size);
        tree.reset(new (&ac) uint8_t[tree_len]);
        if (!ac.check()) {
            fprintf(stderr, "[-] Failed to allocate tree of %zu bytes.\n",
                    tree_len);
            return 1;
        }
        int fd = open(arg, O_RDONLY);
        if (fd < 0) {
            perror("open");
            fprintf(stderr, "[-] Failed to open '%s.\n", arg);
            return 1;
        }
        if (info.st_size != 0) {
            data = mmap(NULL, info.st_size, PROT_READ, MAP_SHARED, fd, 0);
        }
        if (close(fd) < 0) {
            perror("close");
            fprintf(stderr, "[-] Failed to close '%s'\n", arg);
            return 1;
        }
        if (info.st_size == 0 && data == MAP_FAILED) {
            perror("mmap");
            fprintf(stderr, "[-] Failed to mmap '%s.\n", arg);
            return 1;
        }
        mx_status_t rc =
            mt.Create(data, info.st_size, tree.get(), tree_len, &digest);
        if (info.st_size != 0 && munmap(data, info.st_size) != 0) {
            perror("munmap");
            fprintf(stderr, "[-] Failed to munmap '%s.\n", arg);
            return 1;
        }
        if (rc != NO_ERROR) {
            fprintf(stderr, "[-] Merkle tree creation failed: %d\n", rc);
            return 1;
        }
        rc = digest.ToString(strbuf, sizeof(strbuf));
        if (rc != NO_ERROR) {
            fprintf(stderr, "[-] Unable to print Merkle tree root: %d\n", rc);
            return 1;
        }
        printf("%s - %s\n", strbuf, arg);
    }
    return 0;
}
