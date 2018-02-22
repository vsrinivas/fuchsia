// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdio.h>
#include <string>

namespace zxdb {

class Err;

class OutputBuffer {
 public:
  OutputBuffer() {}
  virtual ~OutputBuffer() {}

  virtual void Append(const std::string& str) = 0;

  // Outputs the given help string, applying help-style formatting.
  virtual void FormatHelp(const std::string& str) = 0;

  // Writes the given error.
  virtual void OutputErr(const Err& err) = 0;
};

class FileOutputBuffer : public OutputBuffer {
 public:
  FileOutputBuffer(FILE* file);
  ~FileOutputBuffer();

  void Append(const std::string& str) override;
  void FormatHelp(const std::string& str) override;
  void OutputErr(const Err& err) override;

 private:
  FILE* file_;
};

class StringOutputBuffer : public OutputBuffer {
 public:
  StringOutputBuffer();
  ~StringOutputBuffer();

  const std::string& str() const { return str_; }
  void clear() { str_.clear(); }

  void Append(const std::string& str) override;
  void FormatHelp(const std::string& str) override;
  void OutputErr(const Err& err) override;

 private:
  std::string str_;
};

}  // namespace zxdb
