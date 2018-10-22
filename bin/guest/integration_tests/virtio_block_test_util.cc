// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <iostream>
#include <vector>

#include <fbl/unique_fd.h>
#include <lib/fxl/strings/string_number_conversions.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/zx/handle.h>
#include <zircon/device/block.h>

static constexpr char kDevBlockDir[] = "/dev/class/block";

fbl::unique_fd find_block_device(uint32_t block_size, uint32_t block_count) {
  DIR* dir = opendir(kDevBlockDir);
  if (!dir) {
    return fbl::unique_fd();
  }
  struct dirent* de;
  while ((de = readdir(dir)) != nullptr) {
    std::string path = fxl::StringPrintf("%s/%s", kDevBlockDir, de->d_name);
    fbl::unique_fd fd(open(path.c_str(), O_RDWR));
    if (!fd) {
      continue;
    }
    block_info_t block_info;
    ssize_t r = ioctl_block_get_info(fd.get(), &block_info);
    if (r < 0) {
      continue;
    }
    if (block_info.block_size != block_size ||
        block_info.block_count != block_count) {
      continue;
    }
    return fd;
  }
  return fbl::unique_fd();
}

bool read_block(fbl::unique_fd fd, uint32_t block_size, uint32_t offset,
                uint8_t expected) {
  std::vector<uint8_t> data(block_size);
  ssize_t r = pread(fd.get(), &data[0], block_size, offset * block_size);
  if (r != block_size) {
    std::cout << "Failed to read: " << strerror(errno) << "\n";
    return false;
  }
  for (size_t i = 0; i != block_size; ++i) {
    if (data[i] != expected) {
      std::cout << "Read byte " << i << " as " << data[i] << ", expected "
                << expected << "\n";
      return false;
    }
  }
  return true;
}

bool write_block(fbl::unique_fd fd, uint32_t block_size, uint32_t offset,
                 uint8_t value) {
  std::vector<uint8_t> data(block_size, value);
  ssize_t r = pwrite(fd.get(), &data[0], block_size, offset * block_size);
  if (r != block_size) {
    std::cout << "Failed to write: " << strerror(errno) << "\n";
    return false;
  }
  return true;
}

template <class T>
static bool parse_number(const char* arg, T* value) {
  fxl::StringView arg_view(arg);
  if (!fxl::StringToNumberWithError(arg_view, value)) {
    return false;
  }
  return true;
}

// Accepts arguments of the following forms:
//
// vitio_block_test_util check <block size> <block count>
//   Checks that a block device with the given size and count exists.
//
// vitio_block_test_util read <block size> <block count> <offset> <expected>
//   Reads a block at <offset> and checks that each byte matches <expected>.
//
// vitio_block_test_util write <block size> <block count> <offset> <value>
//   Writes all bytes of the block at <offset> to <value>.
bool parse_args(int argc, const char** argv) {
  if (argc < 4) {
    std::cout << "Wrong number of arguments\n";
    return false;
  }
  uint32_t block_size, block_count;
  if (!parse_number(argv[2], &block_size)) {
    std::cout << "Failed to parse block size\n";
    return false;
  }
  if (!parse_number(argv[3], &block_count)) {
    std::cout << "Failed to parse block count\n";
    return false;
  }
  fbl::unique_fd fd = find_block_device(block_size, block_count);
  if (!fd) {
    std::cout << "Failed to open block device\n";
    return false;
  }
  fxl::StringView cmd_view(argv[1]);
  if (cmd_view == "check" && argc == 4) {
    return true;
  } else if (cmd_view == "read" && argc == 6) {
    uint32_t offset;
    if (!parse_number(argv[4], &offset)) {
      std::cout << "Failed to parse offset\n";
      return false;
    }
    uint8_t value;
    if (!parse_number(argv[5], &value)) {
      std::cout << "Failed to parse read value\n";
      return false;
    }
    return read_block(std::move(fd), block_size, offset, value);
  } else if (cmd_view == "write" && argc == 6) {
    uint32_t offset;
    if (!parse_number(argv[4], &offset)) {
      std::cout << "Failed to parse offset\n";
      return false;
    }
    uint8_t value;
    if (!parse_number(argv[5], &value)) {
      std::cout << "Failed to parse write value\n";
      return false;
    }
    return write_block(std::move(fd), block_size, offset, value);
  }
  std::cout << "Failed to parse arguments\n";
  return false;
}

int main(int argc, const char** argv) {
  if (parse_args(argc, argv)) {
    std::cout << "PASS\n";
  } else {
    std::cout << "FAIL\n";
  }
}