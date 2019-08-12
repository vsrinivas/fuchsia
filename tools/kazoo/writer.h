// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_KAZOO_WRITER_H_
#define TOOLS_KAZOO_WRITER_H_

#include <fbl/macros.h>
#include <stdio.h>

#include <string>

#include "src/lib/fxl/compiler_specific.h"

class Writer {
 public:
  Writer();
  virtual ~Writer() {}

  // Unformatted string output to the underlying location. Returns true on
  // success, or false with a message logged.
  virtual bool Puts(const std::string& str) = 0;

  // Formatted output to the underlying location. Returns true on success, or
  // false with a message logged.
  FXL_PRINTF_FORMAT(2, 3) bool Printf(const char* format, ...);

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(Writer);
};

class FileWriter : public Writer {
 public:
  FileWriter();
  ~FileWriter() override;

  // Prepares the object for writing to the given file. Returns true on
  // success, or false with a message logged.
  bool Open(const std::string& filename);

  // Writer:
  bool Puts(const std::string& str) override;

 private:
  FILE* outf_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(FileWriter);
};

class StringWriter : public Writer {
 public:
  StringWriter();
  ~StringWriter() override;

  // Writer:
  bool Puts(const std::string& str) override;

  const std::string& Out() const { return out_; }

 private:
  std::string out_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(StringWriter);
};

#endif  // TOOLS_KAZOO_WRITER_H_
