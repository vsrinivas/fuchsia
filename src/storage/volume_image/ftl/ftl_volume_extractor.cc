// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "src/storage/volume_image/ftl/ftl_test_helper.h"
#include "zircon/system/ulib/ftl/include/lib/ftl/volume.h"

using namespace storage::volume_image;

class FakeFtl : public ftl::FtlInstance {
 public:
  FakeFtl() = default;
  ~FakeFtl() override = default;
  bool OnVolumeAdded(uint32_t page_size, uint32_t num_pages) override { return true; }
};

// Loads data from the data and spare files into the nand data members. Reads
// complete pages spare chunks sized based on the nand options provided. If an
// incomplete page or spare chunk is read this will return false, or if the
// number of data pages is mismatched with the spare chunk count. Returns true
// on success.
bool LoadData(InMemoryRawNand* nand, FILE* data, FILE* spare) {
  uint32_t page_count = 0;
  std::vector<uint8_t> data_buf;
  while (!feof(data)) {
    data_buf.resize(nand->options.page_size);
    size_t actual_read = fread(&data_buf[0], 1, nand->options.page_size, data);
    if (actual_read == 0 && feof(data)) {
      break;
    } else if (actual_read != nand->options.page_size) {
      fprintf(stderr, "Failed to read, or read partial page for page number: %u\n", page_count);
      return false;
    }
    nand->page_data[page_count++] = std::move(data_buf);
  }

  uint32_t spare_count = 0;
  std::vector<uint8_t> spare_buf;
  while (!feof(spare)) {
    spare_buf.resize(nand->options.oob_bytes_size);
    size_t actual_read = fread(&spare_buf[0], 1, nand->options.oob_bytes_size, spare);
    if (actual_read == 0 && feof(spare)) {
      break;
    } else if (actual_read != nand->options.oob_bytes_size) {
      fprintf(stderr, "Failed to read, or read partial spare for page number: %u\n", spare_count);
      return false;
    }
    nand->page_oob[spare_count++] = std::move(spare_buf);
  }

  if (page_count != spare_count) {
    fprintf(stderr, "Found a mismatch of data pages (%u) vs spare blocks (%u)\n", page_count,
            spare_count);
    return false;
  }
  nand->options.page_count = page_count;
  return true;
}

// Loads the nand up with the bad_blocks information and attempts to read out
// pages from the start until one fails, possibly due to hitting the end of the
// volume. Returns false on failing to init the volume or failing to write out
// to the file. Returns true on success.
bool WriteVolume(InMemoryRawNand* nand, uint32_t bad_blocks, FILE* out) {
  FakeFtl ftl;
  ftl::VolumeImpl volume(&ftl);
  auto ndm = std::make_unique<InMemoryNdm>(nand, nand->options.page_size,
                                           nand->options.oob_bytes_size, bad_blocks);
  const char* err = volume.Init(std::move(ndm));
  if (err != nullptr) {
    fprintf(stderr, "Failed to init volume: %s\n", err);
    return false;
  }

  std::vector<uint8_t> buf(nand->options.page_size);
  uint32_t page;
  for (page = 0; volume.Read(page, 1, &buf[0]) == ZX_OK; ++page) {
    if (fwrite(&buf[0], 1, nand->options.page_size, out) != nand->options.page_size) {
      fprintf(stderr, "Failed to write out page number: %u\n", page);
      return false;
    }
  }
  fprintf(stderr, "Successfully recovered %u pages from volume.\n", page);

  return true;
}

void PrintUsage(char* arg) {
  fprintf(stderr, "Usage: %s <options>*\n", arg);
  fprintf(stderr,
          "options: --data_input     Input file for volume data (required) \n"
          "         --spare_input    Input file for spare data (required) \n"
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
  char* arg_spare_input = nullptr;
  char* arg_output_file = nullptr;
  char* arg_page_size = nullptr;
  char* arg_spare_size = nullptr;
  char* arg_block_pages = nullptr;
  char* arg_bad_blocks = nullptr;
  while (1) {
    static struct option opts[] = {
        {"data_input", required_argument, nullptr, 'd'},
        {"spare_input", required_argument, nullptr, 's'},
        {"page_size", required_argument, nullptr, 'p'},
        {"spare_size", required_argument, nullptr, 'q'},
        {"block_pages", required_argument, nullptr, 'b'},
        {"max_bad_blocks", required_argument, nullptr, 'm'},
        {"output_file", required_argument, nullptr, 'o'},
    };
    int opt_index;
    int c = getopt_long(argc, argv, "b:d:m:o:p:q:s:", opts, &opt_index);
    if (c < 0) {
      break;
    }
    switch (c) {
      case 'd':
        arg_data_input = optarg;
        break;
      case 's':
        arg_spare_input = optarg;
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

  if (arg_data_input == nullptr || arg_spare_input == nullptr || arg_page_size == nullptr ||
      arg_spare_size == nullptr || arg_block_pages == nullptr || arg_bad_blocks == nullptr ||
      arg_output_file == nullptr) {
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

  FILE* spare_input = fopen(arg_spare_input, "r");
  if (!spare_input) {
    fprintf(stderr, "Failed to open volume spare file: %s\n", arg_spare_input);
    return 2;
  }

  FILE* output_file = fopen(arg_output_file, "w");
  if (!data_input) {
    fprintf(stderr, "Failed to open output file: %s\n", arg_output_file);
    return 2;
  }

  InMemoryRawNand nand;
  nand.options.page_size = page_size;
  nand.options.oob_bytes_size = static_cast<uint8_t>(spare_size);
  nand.options.pages_per_block = block_pages;

  if (!LoadData(&nand, data_input, spare_input)) {
    fprintf(stderr, "Failed to load nand data from input files based on given options.\n");
    return 3;
  }
  fclose(data_input);
  data_input = nullptr;
  fclose(spare_input);
  spare_input = nullptr;

  if (!WriteVolume(&nand, bad_blocks, output_file)) {
    fprintf(stderr, "Failed to parse and write out image.\n");
    return 4;
  }

  fclose(output_file);
  output_file = nullptr;

  return 0;
}
