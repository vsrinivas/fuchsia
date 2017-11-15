// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_PUBLIC_LIB_FSL_VMO_SIZED_VMO_H_
#define GARNET_PUBLIC_LIB_FSL_VMO_SIZED_VMO_H_

#include "lib/fsl/fidl/sized_vmo_transport.fidl.h"
#include "zx/vmo.h"

namespace fsl {

// A VMO along with an associated size. The associated size may be smaller than
// the actual size of the VMO, which allows to represent data that is not
// page-aligned.
class SizedVmo {
 public:
  SizedVmo(nullptr_t = nullptr);
  SizedVmo(zx::vmo vmo, uint64_t size);
  SizedVmo(SizedVmo&& other);
  ~SizedVmo();

  // Builds a SizedVmo from a SizedVmoTransport. Returns false if the transport
  // is not valid. For the object to be valid, it must either be null, or the
  // vmo must be valid and the size must be inferior or equal to the physical
  // size of the vmo.
  static bool FromTransport(SizedVmoTransportPtr transport, SizedVmo* out);

  static bool IsSizeValid(const zx::vmo& vmo, uint64_t size);

  SizedVmo& operator=(SizedVmo&& other);

  operator bool() const { return static_cast<bool>(vmo_); }

  zx::vmo& vmo() { return vmo_; }
  const zx::vmo& vmo() const { return vmo_; }

  uint64_t size() const { return size_; }

  // Builds a SizedVmoTransport from this object. This will null this object
  // vmo.
  SizedVmoTransportPtr ToTransport() &&;

  zx_status_t Duplicate(zx_rights_t rights, SizedVmo* output) const;

 private:
  zx::vmo vmo_;
  uint64_t size_;
};

}

#endif  // GARNET_PUBLIC_LIB_FSL_VMO_SIZED_VMO_H_
