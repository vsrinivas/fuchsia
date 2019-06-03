// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <fbl/unique_fd.h>
#include <lib/cksum.h>
#include <lib/mtd/mtd-interface.h>
#include <lib/nand-redundant-storage/nand-redundant-storage.h>

constexpr const char kOptString[] = "i:n:o:h";
constexpr const option kLongOpts[] = {
    {"input", required_argument, nullptr, 'i'},
    {"output", required_argument, nullptr, 'o'},
    {"num-copies", required_argument, nullptr, 'n'},
    {"help", no_argument, nullptr, 'h'},
    {nullptr, no_argument, nullptr, 0},
};

constexpr const char kUsageFormatString[] = R""(Usage: %s -o <out-path> -i <in-path> -n <N>

MTD Redundant Storage Tool.

Options:
    --help, -h                  print this message, then exit
    --input, -i <in-path>       the input file
    --num-copies, -n  <N>       the number of copies to be written to MTD
                                (required if writing).
    --output, -o <out-path>     the output file. Overrides -n and -i.

Examples:
    Write three copies of foo.zip to /dev/mtd0
    $ %s -i foo.zip -o /dev/mtd0 -n 3

    Read the contents of /dev/mtd0 into /tmp/foo.zip
    $ %s -o /tmp/foo.zip -i /dev/mtd0

Notes:
    The user should have read/write permissions for any MTD devices used.

    If <out-path> specifies an MTD, then the file that <in-path> points to will
    be written to <out-path> with at most <N> minus one backup copies.

    If <in-path> specifies an MTD, then the file that <out-path> points to will
    be either created or truncated, and then read into from the MTD. If no file
    can be found, then <out-path> remains unchanged. <N> is ignored in this
    setup.

    If both <out-path> and <in-path> are an MTD, this is an error.

)"";

void Usage(const char* prog_name) {
    fprintf(stdout, kUsageFormatString, prog_name, prog_name, prog_name);
}

bool IsMtd(const char* path) {
    auto mtd = mtd::MtdInterface::Create(path);
    if (!mtd) {
        // Logs any informative errors. An ioctl error where the path doesn't
        // lead to a valid MTD is an uninformative error as this is expected
        // behavior for either the input or the output device.
        if (errno == EACCES) {
            fprintf(stderr, "Unable to open %s: %s\n", path, strerror(errno));
        }
        return false;
    }
    return true;
}

int ReadMtd(const char* output, const char* mtd) {
    auto mtd_iface = nand_rs::NandRedundantStorage::Create(mtd::MtdInterface::Create(mtd));
    if (!mtd_iface) {
        fprintf(stderr, "Unable to open MTD interface %s: %s\n", mtd, strerror(errno));
        return 1;
    }
    std::vector<uint8_t> buffer;
    if (mtd_iface->ReadToBuffer(&buffer) != ZX_OK) {
        return 1;
    }
    fbl::unique_fd output_fd(open(output, O_CREAT | O_TRUNC | O_RDWR, 0666));
    if (!output_fd) {
        fprintf(stderr, "Unable to open file %s: %s\n", output, strerror(errno));
        return 1;
    }
    ssize_t written = write(output_fd.get(), buffer.data(), buffer.size());
    if (written < 0) {
        fprintf(stderr, "Unable to write to file %s: %s\n", output, strerror(errno));
        return 1;
    }
    if (static_cast<uint64_t>(written) != buffer.size()) {
        fprintf(stderr, "Unable to complete write to file %s: %s: expected %zd actual %zd\n",
                output, strerror(errno), static_cast<uint64_t>(written), buffer.size());
        return 1;
    }
    fprintf(stdout, "SUCCESS: File read from %s into %s\n", mtd, output);
    return 0;
}

int WriteMtd(const char* input, const char* mtd, uint32_t num_copies) {
    fbl::unique_fd input_fd(open(input, O_RDONLY));
    if (!input_fd) {
        fprintf(stderr, "Unable to open input file %s: %s\n", input, strerror(errno));
        return 1;
    }
    ssize_t input_file_size = lseek(input_fd.get(), 0L, SEEK_END);
    std::vector<uint8_t> file_buffer(input_file_size);
    ssize_t ret = pread(input_fd.get(), file_buffer.data(), input_file_size, 0L);
    if (ret != input_file_size) {
        fprintf(stderr, "Unable to read file to buffer %s: %s\n", input, strerror(errno));
        return 1;
    }

    auto mtd_iface = nand_rs::NandRedundantStorage::Create(mtd::MtdInterface::Create(mtd));
    if (!mtd_iface) {
        fprintf(stderr, "Unable to open MTD interface %s: %s\n", mtd, strerror(errno));
        return 1;
    }
    uint32_t num_copies_written;
    zx_status_t status = mtd_iface->WriteBuffer(file_buffer, num_copies, &num_copies_written);
    if (status == ZX_OK) {
        fprintf(stdout, "SUCCESS: Wrote %d copies of %s to %s\n", num_copies_written, input, mtd);
    }
    return status;
}

int main(int argc, char** argv) {
    const char* input_file = nullptr;
    const char* output_file = nullptr;
    uint32_t num_copies = 0;
    int opt;
    while ((opt = getopt_long(argc, argv, kOptString, kLongOpts, nullptr)) != -1) {
        switch (opt) {
        case 'i':
            input_file = optarg;
            continue;
        case 'n': {
            auto optarg_end = optarg + strlen(optarg);
            uint64_t num_copies_long = strtoul(optarg, &optarg_end, 10);
            if (errno == EINVAL || num_copies_long == 0) {
                fprintf(stderr, "-n value is invalid\n");
                return 1;
            }
            num_copies = static_cast<uint32_t>(num_copies_long);
            if (static_cast<uint64_t>(num_copies) != num_copies_long) {
                fprintf(stderr, "Overflow on -n argument. Supply 32-bit int.\n");
                return 1;
            }
            continue;
        }
        case 'o':
            output_file = optarg;
            continue;
        case 'h':
        default:
            Usage(argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }
    if (output_file == nullptr || input_file == nullptr) {
        Usage(argv[0]);
        return 1;
    }

    if (strcmp(input_file, output_file) == 0) {
        fprintf(stderr, "ERROR: -i and -o file are the same.\n");
        return 1;
    }
    bool input_is_mtd = IsMtd(input_file);
    bool output_is_mtd = IsMtd(output_file);
    if (input_is_mtd && output_is_mtd) {
        fprintf(stderr, "ERROR: -i and -o are both MTD's.\n");
        return 1;
    }
    if (!input_is_mtd && !output_is_mtd) {
        fprintf(stderr, "ERROR: neither -i nor -o can be used as an MTD.\n");
        return 1;
    }
    if (input_is_mtd) {
        return ReadMtd(output_file, input_file);
    }
    if (output_is_mtd && num_copies > 0) {
        return WriteMtd(input_file, output_file, num_copies);
    }
    if (num_copies == 0) {
        fprintf(stderr, "ERROR: -n missing.\n");
        return 1;
    }
    Usage(argv[0]);
    return 1;
}
