// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a conversion script for taking nand images that were dumped using the uboot "nand read"
// and "nand read.raw" commands, then converting them to the "nandoob" format which is used by our
// more common dumping tool. Sometimes the uboot method is more convenient for folks to use, so this
// provides a way to make it consumable by the ftl-volume-extractor. The "nandoob" format is a
// simple format where each "write" page is immediately followed by the associated spare (aka OOB)
// data.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// === Constants regarding the format of images from uboot "nand read.raw" ===

// Each ECC page is 1024 bytes followed by 56 bytes of OOB data (ECC + spare).
const size_t kOobSize = 56;
// The first two bytes of the OOB are part of the spare for the page.
const size_t kSparePerEccPage = 2;
const size_t kEccPageSize = 1024;
// Each "write" page has 4 ECC pages.
const size_t kEccPagePerWritePage = 4;
const size_t kSpareSize = kSparePerEccPage * kEccPagePerWritePage;
const size_t kPagesPerBlock = 64;
// Each "write" page has 32 trailing bytes for Toshiba. None for Micron. These
// trailing bytes tend to be all 0xFF, unless it is a factory bad block. In
// which case it is all 0x00. We ignore these bytes in any case.
const size_t kToshibaSkipSize = 32;
const size_t kMicronSkipSize = 0;

// Bad blocks should mark their first byte of OOB with 0xFF, but since some bit
// errors can occur the more correct way is to verify that there are more zeroes
// than ones in the byte.
bool IsBadBlockByte(uint8_t byte) {
  int zeros = 0;
  for (int i = 0; i < 8; ++i) {
    if ((byte & 1) == 0) {
      ++zeros;
    }
    byte = byte >> 1;
  }
  return (zeros > 4);
}

void PrintUsage(char* bin_name) {
  fprintf(stderr,
          "Usage: %s [--toshiba|--micron] "
          "raw_image_with_oob ecc_corrected_volume_image output_nand_oob\n",
          bin_name);
  fprintf(stderr,
          "Combines info from a normal read with ECC with the OOB information in a raw\n"
          "image file to build a \"nandoob\" formatted file which is parseable by the\n"
          "ftl-volume-extractor\n");
  fprintf(stderr, "  --toshiba: Configure to read from Toshiba nand chips (default)\n");
  fprintf(stderr, "  --micron: Configure to read from Micron nand chips\n");
  fprintf(stderr, "  raw_image_with_oob: Path to image file from uboot \"nand read.raw\"\n");
  fprintf(stderr, "  ecc_corrected_volume_image: Path to image file from uboot \"nand read\"\n");
  fprintf(stderr, "  output_nand_oob: Path to output nandoob format for parsing\n");
}

int main(int argc, char** argv) {
  if (argc < 4 || argc > 5) {
    PrintUsage(argv[0]);
    return 1;
  }

  int arg = 1;
  size_t skip_size = kToshibaSkipSize;
  if (strncmp(argv[arg], "--", 2) == 0) {
    if (strcmp(argv[arg], "--micron") == 0) {
      skip_size = kMicronSkipSize;
    } else if (strcmp(argv[arg], "--toshiba") != 0) {
      fprintf(stderr, "Unrecognized option: %s\n", argv[arg]);
      PrintUsage(argv[0]);
      return 1;
    }
    ++arg;
  }

  FILE* raw_file = fopen(argv[arg++], "r");
  if (raw_file == NULL) {
    fprintf(stderr, "Failed to open input raw image file: %s\n", argv[arg - 1]);
    return 2;
  }
  FILE* data_file = fopen(argv[arg++], "r");
  if (data_file == NULL) {
    fprintf(stderr, "Failed to open input data image file: %s\n", argv[arg - 1]);
    return 2;
  }
  FILE* out_file = fopen(argv[arg++], "w");
  if (out_file == NULL) {
    fprintf(stderr, "Failed to open ouput data + oob image file: %s\n", argv[arg - 1]);
    return 2;
  }

  uint8_t volume_buffer[kEccPageSize];
  uint8_t raw_buffer[kEccPageSize];
  uint8_t oob_buffer[kOobSize];
  uint8_t spare_buffer[kSpareSize];

  size_t pages = 0;
  bool bad_block = false;
  bool eof = false;
  while (!feof(raw_file)) {
    for (size_t ecc_page = 0; ecc_page < kEccPagePerWritePage; ++ecc_page) {
      size_t data_size = fread(raw_buffer, kEccPageSize, 1, raw_file);
      if (data_size == 0) {
        if (feof(raw_file)) {
          // This will break the outer loop.
          eof = true;
          break;
        }
        fprintf(stderr, "Failed to read raw data at %lu, expected %lu bytes.\n", pages,
                kEccPageSize);
        return 3;
      }

      size_t oob_size = fread(oob_buffer, kOobSize, 1, raw_file);
      if (oob_size == 0) {
        fprintf(stderr, "Failed to read oob at %lu, expected %lu bytes.\n", pages, kOobSize);
        return 3;
      }

      // For the first page of a block, check for the bad block mark.
      if (pages % kPagesPerBlock == 0 && ecc_page == 0) {
        if (IsBadBlockByte(oob_buffer[0])) {
          fprintf(stderr, "Found bad block at %lu\n", pages / kPagesPerBlock);
          bad_block = true;
        } else {
          bad_block = false;
        }
      }

      // When uboot nand.read detects bad blocks, it just skips to the next one.
      // So we don't want to progress the file pointer.
      if (bad_block) {
        // Populate bad blocks with zeroes.
        memset(volume_buffer, 0, kEccPageSize);
        memset(oob_buffer, 0, kOobSize);
      } else {
        // Read the volume data file for the ECC'd version of the data to write
        // to the output file.
        size_t data_size = fread(volume_buffer, kEccPageSize, 1, data_file);
        if (data_size == 0) {
          fprintf(stderr, "Failed to read corrected data at %lu, expected %lu bytes.\n", pages,
                  kEccPageSize);
          return 3;
        }
      }

      size_t write_size = fwrite(volume_buffer, kEccPageSize, 1, out_file);
      if (write_size == 0) {
        fprintf(stderr, "Failed to write data of size %lu.\n", kEccPageSize);
        return 4;
      }

      // Accumulate the Spare bytes for writing out after the page.
      memcpy(&spare_buffer[ecc_page * kSparePerEccPage], oob_buffer, kSparePerEccPage);
    }
    if (eof) {
      break;
    }

    size_t write_size = fwrite(spare_buffer, kSpareSize, 1, out_file);
    if (write_size == 0) {
      fprintf(stderr, "Failed to write oob data of size %lu.\n", kOobSize);
      return 4;
    }

    if (skip_size > 0) {
      // Advance the file pointer past skippable bytes.
      size_t data_size = fread(raw_buffer, skip_size, 1, raw_file);
      if (data_size == 0) {
        fprintf(stderr, "Failed to read skippable bytes.\n");
        return 4;
      }
    }

    pages++;
  }

  fclose(raw_file);
  fclose(data_file);
  fclose(out_file);
  return 0;
}
