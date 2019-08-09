// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

static constexpr const size_t kDataAlignment = 16;
static constexpr const size_t kBlockSize = 1024;
static constexpr const size_t kRootChecksumLength = 512;
static constexpr const uint32_t kFileFlags = 0xA;
static constexpr const uint32_t kDirectoryFlags = 0x9;
static constexpr const uint32_t kHardlinkFlags = 0x0;
static constexpr const uint32_t kMinHeaderSize = 0x20;  // For names with 15 or fewer characters
static constexpr const uint32_t kRootHeaderSizePos = 8;
static constexpr const uint32_t kRootHeaderChecksumPos = 12;

void usage() {
  std::cout << "Generate a flat romfs image from the provided files.\n";
  std::cout << "Usage: mkromfs {output} {files}...\n";
  std::cout << "Example: mkromfs ./out.img ~/foo.bin ~/bar.so\n";
}

// Round a value to the next multiple of a specified alignment
uint32_t roundup(uint32_t x, uint32_t align) { return ((x + align - 1) / align) * align; }

// Sum chunks of data interpreted as big-endian 32-bit values.
uint32_t checksum(const void* data, size_t size) {
  uint32_t ret = 0;
  for (size_t i = 0; i < size; ++i) {
    ret += reinterpret_cast<const unsigned char*>(data)[i] << (CHAR_BIT * (3 - (i % 4)));
  }
  return ret;
}

// Write data to an ostream, zero-padding to a specified alignment.
uint32_t write(std::ostream& s, const char* data, size_t size, size_t align = kDataAlignment) {
  s.write(data, size);
  while (s.tellp() % align) {
    s.put(0);
  }
  return checksum(data, size);
}

// Write value as big-endian to the stream.
uint32_t write(std::ostream& s, uint32_t value) {
  uint32_t value_be = htobe32(value);
  return write(s, reinterpret_cast<char*>(&value_be), sizeof(value_be), sizeof(value_be));
}

// Read the entire contents of the specified file and return it in a char vector.
std::vector<char> read(std::string path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    std::cerr << "Error: file could not be opened for reading: " << path << std::endl;
    exit(-1);
  }
  std::vector<char> contents(file.tellg());
  file.seekg(0, std::ios::beg);
  file.read(contents.data(), contents.size());
  return contents;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    usage();
    exit(-1);
  }

  // Open the output image.
  std::fstream image(argv[1], std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
  if (!image) {
    std::cerr << "Error: file could not be opened for writing: " << argv[3] << std::endl;
    exit(-1);
  }

  // Write the main header.
  static const char* magic = "-rom1fs-";
  static const char* label = "romfs";
  image.write(magic, strlen(magic));       // Magic
  write(image, 0);                         // Placeholder for size
  write(image, 0);                         // Placeholder for checksum
  write(image, label, strlen(label) + 1);  // Label

  // Write the root directory.
  {
    uint32_t csum = 0;
    uint32_t first_header = image.tellp();
    uint32_t next_header = first_header + kMinHeaderSize;
    csum += write(image, next_header | kDirectoryFlags);  // Next header
    next_header += kMinHeaderSize;
    csum += write(image, next_header);  // First file in directory
    csum += write(image, 0);            // No size
    csum += checksum(".", 1);
    write(image, csum);
    write(image, ".", 2);
    write(image, kHardlinkFlags);
    csum = write(image, first_header);
    write(image, 0);  // No size
    csum += checksum("..", 2);
    write(image, csum);
    write(image, "..", 3);
  }

  // Add file entries.
  for (int i = 2; i < argc; ++i) {
    auto contents = read(argv[i]);
    std::string path = argv[i];
    auto pos = path.rfind('/');
    std::string filename = pos == path.npos ? path : path.substr(pos + 1);
    uint32_t next = kFileFlags;
    if (i < argc - 1) {
      // Calculate next file offset
      uint32_t offset = image.tellp();
      offset += kDataAlignment;                                  // Initial header contents
      offset += roundup(filename.length() + 1, kDataAlignment);  // Name field
      offset += roundup(contents.size(), kDataAlignment);        // Data field
      next |= offset;
    }
    write(image, next);
    write(image, 0);  // Spec field unused
    write(image, contents.size());
    uint32_t csum = next;
    csum += contents.size();
    csum += checksum(filename.c_str(), filename.length());
    csum += checksum(contents.data(), contents.size());
    write(image, csum);
    write(image, filename.c_str(), filename.length() + 1);
    write(image, contents.data(), contents.size());
  }

  // Save the total size of the image.
  uint32_t image_size = image.tellp();

  // Pad the image to block size.
  std::vector<char> zeros(roundup(image.tellp(), kBlockSize) - image.tellp());
  image.write(zeros.data(), zeros.size());

  // Patch in the total image size.
  image.seekp(kRootHeaderSizePos, std::ios::beg);
  write(image, image_size);

  // Read back the image data and patch in the checksum.
  std::vector<char> base(kRootChecksumLength);
  image.seekg(0, std::ios::beg);
  image.read(base.data(), base.size());
  uint32_t csum = checksum(base.data(), base.size());
  image.seekp(kRootHeaderChecksumPos, std::ios::beg);
  write(image, -csum);

  return 0;
}
