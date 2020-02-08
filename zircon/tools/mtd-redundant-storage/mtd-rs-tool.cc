// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <lib/mtd/mtd-interface.h>
#include <lib/nand-redundant-storage/file-nand-redundant-storage.h>
#include <lib/nand-redundant-storage/nand-redundant-storage.h>
#include <string.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

#include <fbl/unique_fd.h>

const uint32_t kFilePageSize = 4096;
const uint32_t kFileBlockSize = kFilePageSize * 64;
const uint32_t kFilePermissions = 0666;
constexpr const char kOptString[] = "i:n:o:s:dexh";
constexpr const option kLongOpts[] = {
    {"input", required_argument, nullptr, 'i'},
    {"output", required_argument, nullptr, 'o'},
    {"num-copies", required_argument, nullptr, 'n'},
    {"file-size", required_argument, nullptr, 's'},
    {"no-header", no_argument, nullptr, 'x'},
    {"decode", no_argument, nullptr, 'd'},
    {"encode", no_argument, nullptr, 'e'},
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
    --no-header, -x             Writes the input file without the header
                                this file cannot be read back by this tool
    --file-size, -s  <N>        Size of the file to be read from MTD
                                (required if reading with -x set).
    --encode, -e                Specifies that |input| should be encoded
                                and redundantly written into |output|.
    --decode, -d                Specifies that |input| should be decoded
                                and written into |output|.

Examples:
    Write three copies of foo.zip to /dev/mtd0
    $ %s -i foo.zip -o /dev/mtd0 -n 3

    Read the contents of /dev/mtd0 into foo.zip
    $ %s -o foo.zip -i /dev/mtd0

    Write(encode) three copies of foo.zip to bar, a file on host
    $ %s -o bar -i foo.zip -e -n 3

    Read(decode) the contents of bar into foo.zip
    $ %s -i bar -o foo.zip -d

Notes:
    The user should have read/write permissions for any MTD devices used.

    If <out-path> specifies an MTD, then the file that <in-path> points to will
    be written to <out-path> with at most <N> minus one backup copies.

    If <in-path> specifies an MTD, then the file that <out-path> points to will
    be either created or truncated, and then read into from the MTD. If no file
    can be found, then <out-path> remains unchanged. <N> is ignored in this
    setup.

    If both <out-path> and <in-path> are an MTD, this is an error.

    Full input/output interactions are listed in the table below.

    -i is a | -o is a | -e/-d flags  | Action
    ------------------------------------------------------------
    MTD     | MTD     | <any>        | Unsupported
            |         |              |
    File    | MTD     | <none>       | input file encoded & written to MTD
    File    | MTD     | -e           | input file encoded & written to MTD
    File    | MTD     | -d           | Unsupported
            |         |              |
    MTD     | File    | <none>       | MTD decoded & written to file
    MTD     | File    | -e           | Unsupported
    MTD     | File    | -d           | MTD decoded & written to file
            |         |              |
    File    | File    | -d           | input is decoded & written to output
    File    | File    | -e           | input is encoded & written to output
    File    | File    | <none>       | Unsupported
            |         |              |
    <any>   | <any>   | -e -d        | Unsupported

)"";

struct MtdRsToolFlags {
  const char* input = nullptr;
  const char* output = nullptr;
  uint32_t num_copies = 0;
  bool no_header = false;
  size_t file_size = 0;
  bool decode = false;
  bool encode = false;
  bool help = false;
};

void Usage(const char* prog_name) {
  fprintf(stdout, kUsageFormatString, prog_name, prog_name, prog_name, prog_name, prog_name);
}

bool IsMtd(const char* path) {
  auto mtd = mtd::MtdInterface::Create(path);
  if (!mtd) {
    // Logs any informative errors. An error where the path doesn't
    // lead to a valid MTD is an uninformative error as this is expected
    // behavior for either the input or the output device.
    if (errno == EACCES) {
      fprintf(stderr, "Unable to open %s: %s\n", path, strerror(errno));
    }
    return false;
  }
  return true;
}

std::optional<MtdRsToolFlags> ParseFlags(int argc, char** argv) {
  MtdRsToolFlags flags;
  int opt;
  while ((opt = getopt_long(argc, argv, kOptString, kLongOpts, nullptr)) != -1) {
    switch (opt) {
      case 'i':
        flags.input = optarg;
        continue;
      case 'n': {
        auto optarg_end = optarg + strlen(optarg);
        uint64_t num_copies_long = strtoul(optarg, &optarg_end, 10);
        if (errno == EINVAL || num_copies_long == 0) {
          fprintf(stderr, "-n value is invalid\n");
          return std::nullopt;
        }
        flags.num_copies = static_cast<uint32_t>(num_copies_long);
        if (static_cast<uint64_t>(flags.num_copies) != num_copies_long) {
          fprintf(stderr, "Overflow on -n argument. Supply 32-bit int.\n");
          return std::nullopt;
        }
        continue;
      }
      case 'o':
        flags.output = optarg;
        continue;
      case 'x':
        flags.no_header = true;
        continue;
      case 's': {
        auto optarg_end = optarg + strlen(optarg);
        flags.file_size = strtoul(optarg, &optarg_end, 10);
        if (errno == EINVAL || flags.file_size == 0) {
          fprintf(stderr, "-s value is invalid\n");
          return std::nullopt;
        }
        continue;
      }
      case 'e':
        flags.encode = true;
        continue;
      case 'd':
        flags.decode = true;
        continue;
      case 'h':
      default:
        if (opt != 'h') {
          return std::nullopt;
        }
        flags.help = true;
    }
  }
  return flags;
}

std::unique_ptr<nand_rs::NandRedundantStorageInterface> FileInterface(const char* filename) {
  fbl::unique_fd fd(open(filename, O_RDWR | O_CREAT, kFilePermissions));
  if (!fd) {
    fprintf(stderr, "Unable to open file %s: %s\n", filename, strerror(errno));
    return nullptr;
  }

  auto interface = std::make_unique<nand_rs::FileNandRedundantStorage>(
      std::move(fd), kFileBlockSize, kFilePageSize);
  if (!interface) {
    fprintf(stderr, "Unable to open file backed MTD interface %s: %s\n", filename, strerror(errno));
    return nullptr;
  }

  return interface;
}

std::unique_ptr<nand_rs::NandRedundantStorageInterface> MtdInterface(const char* mtd) {
  auto interface = nand_rs::NandRedundantStorage::Create(mtd::MtdInterface::Create(mtd));
  if (!interface) {
    fprintf(stderr, "Unable to open MTD interface %s: %s\n", mtd, strerror(errno));
    return nullptr;
  }

  return interface;
}

int Read(std::unique_ptr<nand_rs::NandRedundantStorageInterface> interface,
         const char* interface_path, const char* output, bool skip_header, size_t file_size) {
  std::vector<uint8_t> buffer;
  if (interface->ReadToBuffer(&buffer, skip_header, file_size) != ZX_OK) {
    return 1;
  }
  fbl::unique_fd output_fd(open(output, O_CREAT | O_TRUNC | O_RDWR, kFilePermissions));
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
    fprintf(stderr, "Unable to complete write to file %s: %s: expected %zd actual %zd\n", output,
            strerror(errno), static_cast<uint64_t>(written), buffer.size());
    return 1;
  }
  fprintf(stdout, "SUCCESS: File read from %s into %s\n", interface_path, output);
  return 0;
}

int Write(std::unique_ptr<nand_rs::NandRedundantStorageInterface> interface,
          const char* interface_path, const char* input, uint32_t num_copies, bool skip_header) {
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

  uint32_t num_copies_written;
  zx_status_t status =
      interface->WriteBuffer(file_buffer, num_copies, &num_copies_written, skip_header);
  if (status == ZX_OK) {
    fprintf(stdout, "SUCCESS: Wrote %d copies of %s to %s\n", num_copies_written, input,
            interface_path);
  }
  return status;
}

int main(int argc, char** argv) {
  auto flags = ParseFlags(argc, argv);

  if (!flags) {
    return 1;
  }

  if (flags->help) {
    Usage(argv[0]);
    return 0;
  }

  if (flags->output == nullptr || flags->input == nullptr) {
    fprintf(stderr, "ERROR: -i or -o not set.\n");
    return 1;
  }

  if (strcmp(flags->input, flags->output) == 0) {
    fprintf(stderr, "ERROR: -i and -o file are the same.\n");
    return 1;
  }

  if (flags->encode && flags->decode) {
    fprintf(stderr, "ERROR: -d and -e are both set.\n");
    return 1;
  }

  bool input_is_mtd = IsMtd(flags->input);
  bool output_is_mtd = IsMtd(flags->output);

  if (input_is_mtd && output_is_mtd) {
    fprintf(stderr, "ERROR: -i and -o are both MTD's.\n");
    return 1;
  }

  if (input_is_mtd) {
    if (flags->encode) {
      fprintf(stderr, "ERROR: Unable to encode when outputting to a file.\n");
      return 1;
    }

    if (flags->no_header && flags->file_size == 0) {
      fprintf(stderr, "ERROR: -s required to read from an MTD without a header.\n");
      return 1;
    }

    auto interface = MtdInterface(flags->input);
    if (!interface) {
      return 1;
    }

    return Read(std::move(interface), flags->input, flags->output, flags->no_header,
                flags->file_size);
  }

  if (output_is_mtd) {
    if (flags->decode) {
      fprintf(stderr, "ERROR: Unable to decode when outputing to an MTD.\n");
      return 1;
    }

    if (flags->num_copies <= 0) {
      fprintf(stderr, "ERROR: -n missing.\n");
      return 1;
    }

    auto interface = MtdInterface(flags->output);
    if (!interface) {
      return 1;
    }

    return Write(std::move(interface), flags->output, flags->input, flags->num_copies,
                 flags->no_header);
  }

  if (!flags->encode && !flags->decode) {
    fprintf(stderr,
            "ERROR: Cannot determine action because both -i and -o are files."
            " Please set either -d or -e");
    return 1;
  }

  if (flags->decode) {
    auto interface = FileInterface(flags->input);
    if (!interface) {
      return 1;
    }

    return Read(std::move(interface), flags->input, flags->output, flags->no_header,
                flags->file_size);
  }

  if (flags->encode) {
    if (flags->num_copies <= 0) {
      fprintf(stderr, "ERROR: -n missing.\n");
      return 1;
    }

    auto interface = FileInterface(flags->output);
    if (!interface) {
      return 1;
    }

    return Write(std::move(interface), flags->output, flags->input, flags->num_copies,
                 flags->no_header);
  }

  Usage(argv[0]);
  return 1;
}
