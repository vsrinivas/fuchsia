// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <getopt.h>
#include <inttypes.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/errors.h>

#include <string>
#include <vector>

#include <fbl/algorithm.h>
#include <safemath/safe_math.h>

#include "src/devices/block/drivers/ftl/tests/ndm-ram-driver.h"
#include "src/storage/lib/ftl/ftln/ndm-driver.h"
#include "src/storage/lib/ftl/ftln/volume.h"

__PRINTFLIKE(3, 4) void LogToStderr(const char* file, int line, const char* format, ...) {
  va_list args;
  fprintf(stderr, "[FTL] ");
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr, "\n");
}

__PRINTFLIKE(3, 4) void DropLog(const char*, int, const char*, ...) {}

constexpr FtlLogger kTerseLogger{
    .trace = DropLog,
    .debug = DropLog,
    .info = DropLog,
    .warn = LogToStderr,
    .error = LogToStderr,
};

constexpr TestOptions kBoringTestOptions = {-1, -1, 0, false, true, -1, false, kTerseLogger};

class FakeFtl : public ftl::FtlInstance {
 public:
  FakeFtl() = default;
  ~FakeFtl() override = default;
  bool OnVolumeAdded(uint32_t unused_page_size, uint32_t num_pages) override;
  uint32_t num_pages() { return num_pages_; }

 private:
  uint32_t num_pages_ = 0;
};

bool FakeFtl::OnVolumeAdded(uint32_t unused_page_size, uint32_t num_pages) {
  num_pages_ = num_pages;
  return true;
}

enum BlockStatus { kOk, kBadBlock, kReadFailure };

BlockStatus block_status(cpp20::span<uint8_t> data) {
  if (!memcmp(data.data(), "BADBLOCK", 8)) {
    return BlockStatus::kBadBlock;
  }
  if (!memcmp(data.data(), "READFAIL", 8)) {
    return BlockStatus::kReadFailure;
  }
  return BlockStatus::kOk;
}

// Loads from the data file into the nand data members.  Data is expected to be formatted as 4096
// bytes of data followed by 8 bytes of OOB, with the first 8 bytes of the data saying "BADBLOCK" or
// "READFAIL" if either of those conditions hold for the page.
// If an incomplete page or spare chunk is read this will return ZX_ERR_IO, or if the number of data
// pages is mismatched with the spare chunk count. Returns a populate NDM on success.
zx::result<std::unique_ptr<NdmRamDriver>> LoadData(const ftl::VolumeOptions& options, FILE* data) {
  uint32_t page_count = 0;
  TestOptions test_options = kBoringTestOptions;
  std::unique_ptr<NdmRamDriver> ndm = std::make_unique<NdmRamDriver>(options, test_options);
  if (const char* err = ndm->Init(); err != nullptr) {
    fprintf(stderr, "Failed to init NDM: %s\n", err);
    return zx::error(ZX_ERR_BAD_STATE);
  }
  std::unique_ptr<uint8_t[]> data_buf = std::make_unique<uint8_t[]>(options.page_size);
  std::unique_ptr<uint8_t[]> spare_buf = std::make_unique<uint8_t[]>(options.eb_size);
  while (!feof(data)) {
    // The input format does 4k chunks, we need 8k
    uint32_t data_offset = 0;
    uint32_t spare_offset = 0;
    for (int i = 0; i < 2; ++i) {
      ssize_t actual_read = fread(&data_buf.get()[data_offset], 1, options.page_size / 2, data);
      if (actual_read == 0 && feof(data)) {
        goto done;
      } else if (actual_read != options.page_size / 2) {
        fprintf(stderr, "ERROR: Failed to read, or read partial page for page number: %u\n",
                page_count);
        return zx::error(ZX_ERR_IO);
      }
      data_offset += options.page_size / 2;

      actual_read = fread(&spare_buf.get()[spare_offset], 1, options.eb_size / 2, data);
      if (actual_read != options.eb_size / 2) {
        fprintf(stderr, "ERROR: Failed to read oob for page number: %u\n", page_count);
        return zx::error(ZX_ERR_IO);
      }
      spare_offset += options.eb_size / 2;
    }

    auto status = block_status(cpp20::span(data_buf.get(), 8));
    switch (status) {
      case BlockStatus::kOk: {
        ndm->NandWrite(page_count, 1, data_buf.get(), spare_buf.get());
        break;
      }
      case BlockStatus::kBadBlock: {
        ndm->SetBadBlock(page_count, true);
        break;
      }
      case BlockStatus::kReadFailure: {
        fprintf(stderr, "ERROR: Page %u read failed, likely ECC Failure\n", page_count);
        ndm->SetFailEcc(page_count, true);
        break;
      }
    }
    page_count++;
  }

done:
  printf("%u pages, %u blocks\n", page_count,
         page_count / (options.block_size / options.page_size));
  return zx::success(std::move(ndm));
}
// Loads the nand up with the FTL and options, then attempts to read out
// pages from the start until one fails, possibly due to hitting the end of the
// volume. Returns false on failing to init the volume or failing to write out
// to the file. Returns true on success.
bool WriteVolume(std::unique_ptr<NdmRamDriver> ndm, const ftl::VolumeOptions& options, FILE* out) {
  FakeFtl ftl;
  ftl::VolumeImpl volume(&ftl);
  const char* err = volume.Init(std::move(ndm));
  if (err != nullptr) {
    fprintf(stderr, "ERROR: Failed to init volume: %s\n", err);
    return false;
  }

  std::string issues = volume.DiagnoseKnownIssues();
  if (!issues.empty()) {
    fprintf(stderr, "ERROR: Identified common symptoms:\n%s", issues.c_str());
  }

  std::vector<uint8_t> buf(options.page_size);
  uint32_t page;
  for (page = 0; page < ftl.num_pages() && volume.Read(page, 1, buf.data()) == ZX_OK; ++page) {
    if (fwrite(buf.data(), 1, options.page_size, out) != options.page_size) {
      fprintf(stderr, "ERROR: Failed to write out page number: %u\n", page);
      return false;
    }
  }
  fprintf(stderr, "INFO: Successfully recovered %u pages from volume.\n", page);

  return true;
}

zx::result<size_t> GetFileSize(FILE* file) {
  size_t file_size;
  if (fseek(file, 0, SEEK_END) != 0) {
    fprintf(stderr, "Failed to seek to end of input file: %s\n", strerror(errno));
    return zx::error(ZX_ERR_BAD_STATE);
  }
  if (off_t where = ftell(file); where >= 0) {
    file_size = static_cast<size_t>(where);
  } else {
    fprintf(stderr, "Failed to get end of file location of input file: %s\n", strerror(errno));
    return zx::error(ZX_ERR_BAD_STATE);
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    fprintf(stderr, "Failed to rewind input file: %s\n", strerror(errno));
    return zx::error(ZX_ERR_BAD_STATE);
  }
  return zx::success(file_size);
}

void PrintUsage(char* arg) {
  fprintf(stderr, "Usage: %s <options>*\n", arg);
  fprintf(stderr,
          "options: --data_input     Input file for volume data (required) \n"
          "         --page_size      Page size for volume data (required) \n"
          "         --spare_size     Size of spare data per page (required) \n"
          "         --block_pages    Number of pages per block (required) \n"
          "         --output_file    File to write resulting volume image. (required) \n"
          "         --max_bad_blocks Maximum number of bad blocks. (required) \n"
          "\n"
          "This tool takes two files, one for volume data and one for spare data along\n"
          "with the sizes of chunks (--page_size & --spare_size) for a single page.\n"
          "This is loaded into the FTL where it attempts to linearly dump the\n"
          "resulting image that it would normally expose out to --output_file\n");
}

std::optional<uint32_t> ParseUint32(const char* str) {
  char* pend;
  uint64_t ret = strtoul(str, &pend, 10);
  if (*pend != '\0') {
    return std::nullopt;
  }
  if (ret > std::numeric_limits<uint32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(ret);
}

int main(int argc, char** argv) {
  char* arg_data_input = nullptr;
  char* arg_output_file = nullptr;
  char* arg_page_size = nullptr;
  char* arg_spare_size = nullptr;
  char* arg_block_pages = nullptr;
  char* arg_bad_blocks = nullptr;
  while (true) {
    static struct option opts[] = {
        {"data_input", required_argument, nullptr, 'd'},
        {"page_size", required_argument, nullptr, 'p'},
        {"spare_size", required_argument, nullptr, 'q'},
        {"block_pages", required_argument, nullptr, 'b'},
        {"max_bad_blocks", required_argument, nullptr, 'm'},
        {"output_file", required_argument, nullptr, 'o'},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "b:d:m:o:p:q:", opts, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'd':
        arg_data_input = optarg;
        break;
      case 'p':
        arg_page_size = optarg;
        break;
      case 'q':
        arg_spare_size = optarg;
        break;
      case 'b':
        arg_block_pages = optarg;
        break;
      case 'm':
        arg_bad_blocks = optarg;
        break;
      case 'o':
        arg_output_file = optarg;
        break;
      default:
        PrintUsage(argv[0]);
        return 1;
    }
  }

  if (arg_data_input == nullptr || arg_page_size == nullptr || arg_spare_size == nullptr ||
      arg_block_pages == nullptr || arg_bad_blocks == nullptr || arg_output_file == nullptr) {
    fprintf(stderr, "Missing required argument.\n");
    PrintUsage(argv[0]);
    return 1;
  }

  std::optional<uint32_t> parsed = ParseUint32(arg_page_size);
  if (!parsed || *parsed == 0) {
    fprintf(stderr, "Expected positive integer for page_size but got: %s\n", arg_page_size);
    return 2;
  }
  uint32_t page_size = *parsed;

  parsed = ParseUint32(arg_spare_size);
  if (!parsed || *parsed == 0 || *parsed > 255) {
    fprintf(stderr, "Expected positive 8 bit integer for spare_size but got: %s\n", arg_spare_size);
    return 2;
  }
  uint32_t spare_size = *parsed;

  parsed = ParseUint32(arg_block_pages);
  if (!parsed || *parsed == 0) {
    fprintf(stderr, "Expected positive integer for block_pages but got: %s\n", arg_block_pages);
    return 2;
  }
  uint32_t block_pages = *parsed;

  parsed = ParseUint32(arg_bad_blocks);
  if (!parsed || *parsed == 0) {
    fprintf(stderr, "Expected positive integer for max_bad_blocks but got: %s\n", arg_bad_blocks);
    return 2;
  }
  uint32_t bad_blocks = *parsed;

  FILE* data_input = fopen(arg_data_input, "r");
  if (!data_input) {
    fprintf(stderr, "Failed to open volume data file: %s\n", arg_data_input);
    return 2;
  }

  FILE* output_file = fopen(arg_output_file, "w");
  if (!data_input) {
    fprintf(stderr, "Failed to open output file: %s\n", arg_output_file);
    return 2;
  }

  size_t file_size;
  if (auto size_or = GetFileSize(data_input); size_or.is_ok()) {
    file_size = size_or.value();
  } else {
    return 3;
  }

  if (file_size % (static_cast<size_t>(page_size + spare_size) * block_pages) != 0) {
    fprintf(stderr, "Input file of size %lu is not divisible by block size of %u\n", file_size,
            (page_size + spare_size) * block_pages);
    return 3;
  }

  ftl::VolumeOptions options = {
      safemath::checked_cast<uint32_t>(file_size) / ((page_size + spare_size) * block_pages),
      bad_blocks,
      page_size * block_pages,
      page_size,
      spare_size,
      0};
  printf("page_size: %u oob_bytes_size: %u pages_per_block: %u num_blocks: %u\n", page_size,
         spare_size, block_pages, options.num_blocks);

  std::unique_ptr<NdmRamDriver> ndm;
  if (auto ndm_or = LoadData(options, data_input); ndm_or.is_ok()) {
    ndm = std::move(ndm_or.value());
  } else {
    fprintf(stderr, "Failed to load nand data from input files based on given options.\n");
    return 3;
  }
  fclose(data_input);
  data_input = nullptr;

  if (!WriteVolume(std::move(ndm), options, output_file)) {
    fprintf(stderr, "Failed to parse and write out image.\n");
    return 4;
  }

  fclose(output_file);
  output_file = nullptr;

  return 0;
}
