// Copyright 2017 The Fuchsia Authors. All rights reserved.
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

#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/alloc_checker.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>

using digest::Digest;
using digest::MerkleTree;

static void usage(char** argv) {
    fprintf(stderr, "Usage: %s [-o OUTPUT | -m MANIFEST] FILE...\n", argv[0]);
    fprintf(stderr, "\n\
With -o, OUTPUT gets the same format normally written to stdout: HASH - FILE.\n\
With -m, MANIFEST gets \"manifest file\" format: HASH=FILE.\n\
Any argument may be \"@RSPFILE\" to be replaced with the contents of RSPFILE.\n\
");
    exit(1);
}

static int handle_argument(char** argv, FILE* outf, const char* separator,
                           const char* arg) {
    if (arg[0] == '@') {
        FILE* rspfile = fopen(&arg[1], "r");
        if (!rspfile) {
            perror(&arg[1]);
            return 1;
        }
        while (!feof(rspfile) && !ferror(rspfile)) {
            char* filename = nullptr;
            if (fscanf(rspfile, " %ms", &filename) == 1) {
                handle_argument(argv, outf, separator, filename);
                free(filename);
            }
        }
        int result = ferror(rspfile);
        if (result) {
            perror(&arg[1]);
        }
        fclose(rspfile);
        return result;
    }

    // Buffer one intermediate node's worth at a time.
    fbl::unique_ptr<uint8_t[]> tree;
    char strbuf[Digest::kLength * 2 + 1];
    Digest digest;
    struct stat info;
    if (stat(arg, &info) < 0) {
        perror("stat");
        fprintf(stderr, "[-] Unable to stat '%s'.\n", arg);
        fprintf(stderr, "usage: %s <filename>\n", argv[0]);
        return 1;
    }
    if (!S_ISREG(info.st_mode)) {
        return 0;
    }
    size_t len = MerkleTree::GetTreeLength(info.st_size);
    fbl::AllocChecker ac;
    tree.reset(new (&ac) uint8_t[len]);
    if (!ac.check()) {
        fprintf(stderr, "[-] Failed to allocate tree of %zu bytes.\n", len);
        return 1;
    }
    int fd = open(arg, O_RDONLY);
    if (fd < 0) {
        perror("open");
        fprintf(stderr, "[-] Failed to open '%s.\n", arg);
        return 1;
    }
    void* data = nullptr;
    if (info.st_size != 0) {
        data = mmap(NULL, info.st_size, PROT_READ, MAP_SHARED, fd, 0);
    }
    if (close(fd) < 0) {
        perror("close");
        fprintf(stderr, "[-] Failed to close '%s'\n", arg);
        return 1;
    }
    if (info.st_size != 0 && data == MAP_FAILED) {
        perror("mmap");
        fprintf(stderr, "[-] Failed to mmap '%s.\n", arg);
        return 1;
    }
    zx_status_t rc =
        MerkleTree::Create(data, info.st_size, tree.get(), len, &digest);
    if (info.st_size != 0 && munmap(data, info.st_size) != 0) {
        perror("munmap");
        fprintf(stderr, "[-] Failed to munmap '%s.\n", arg);
        return 1;
    }
    if (rc != ZX_OK) {
        fprintf(stderr, "[-] Merkle tree creation failed: %d\n", rc);
        return 1;
    }
    rc = digest.ToString(strbuf, sizeof(strbuf));
    if (rc != ZX_OK) {
        fprintf(stderr, "[-] Unable to print Merkle tree root: %d\n", rc);
        return 1;
    }
    fprintf(outf, "%s%s%s\n", strbuf, separator, arg);

    return 0;
}

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
    const char* separator = manifest ? "=" : " - ";

    for (; argi < argc; ++argi) {
        if (handle_argument(argv, outf, separator, argv[argi]))
            return 1;
    }
    return 0;
}
