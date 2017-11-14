// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_PUBLIC_LIB_FSL_VMO_SIZED_VMO_H_
#define GARNET_PUBLIC_LIB_FSL_VMO_SIZED_VMO_H_

#include "zx/vmo.h"

namespace fsl {

// A VMO along with an associated size. The associated size may be smaller than
// the actual size of the VMO, which allows to represent data that is not
// page-aligned.
class SizedVmo {
 public:
  SizedVmo();
  SizedVmo(zx::vmo vmo, uint64_t size);
  SizedVmo(SizedVmo&& other);
  ~SizedVmo();

  static bool IsSizeValid(const zx::vmo& vmo, uint64_t size);

  SizedVmo& operator=(SizedVmo&& other);

  operator bool() const { return static_cast<bool>(vmo_); }

  zx::vmo& vmo() { return vmo_; }
  const zx::vmo& vmo() const { return vmo_; }

  uint64_t size() const { return size_; }

  zx_status_t Duplicate(zx_rights_t rights, SizedVmo* output) const;

 private:
  zx::vmo vmo_;
  uint64_t size_;
};

}

#endif  // GARNET_PUBLIC_LIB_FSL_VMO_SIZED_VMO_H_
