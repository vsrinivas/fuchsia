// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_FILES_SCOPED_FD_H_
#define LIB_FTL_FILES_SCOPED_FD_H_

#include "lib/ftl/macros.h"

namespace ftl {

class ScopedFD {
 public:
  ScopedFD();
  explicit ScopedFD(int fd);
  ~ScopedFD();

  ScopedFD(ScopedFD&& other);
  ScopedFD& operator=(ScopedFD&& other);

  bool is_valid() const { return fd_ != -1; }
  int get() const { return fd_; }

  void Close();

 private:
  int fd_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ScopedFD);
};

}  // namespace ftl

#endif  // LIB_FTL_FILES_SCOPED_FD_H_
