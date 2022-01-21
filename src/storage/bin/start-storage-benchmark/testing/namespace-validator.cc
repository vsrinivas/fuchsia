// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <string.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#include <fbl/unique_fd.h>

constexpr std::string_view file_contents("file-contents");

int main(int argc, char* argv[]) {
  if (argc != 2) {
    printf("Wrong number of arguments.");
    return EXIT_FAILURE;
  }

  // Verify that the provided directory can be opened.
  fbl::unique_fd dir(open(argv[1], O_RDONLY | O_DIRECTORY));
  if (!dir.is_valid()) {
    printf("Failed to open %s: %s", argv[1], strerror(errno));
    return EXIT_FAILURE;
  }

  // Verify that a file can be created in the directory.
  fbl::unique_fd file(openat(dir.get(), "file", O_RDWR | O_CREAT));
  if (!file.is_valid()) {
    printf("Failed to open a file in %s: %s", argv[1], strerror(errno));
    return EXIT_FAILURE;
  }
  if (write(file.get(), file_contents.data(), file_contents.size() != file_contents.size())) {
    printf("Failed to write to a file in %s: %s", argv[1], strerror(errno));
    return EXIT_FAILURE;
  }
}
