// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/channel.h>

#include <memory>

#include <fbl/unique_fd.h>

#include "abr.h"

namespace abr {

// Interface for interacting with ABR data.
class Client {
 public:
  // Factory create method.
  static zx_status_t Create(fbl::unique_fd devfs_root, const zx::channel& svc_root,
                            std::unique_ptr<abr::Client>* out);

  virtual ~Client() = default;

  // Accessor for underlying Data structure.
  virtual const abr::Data& Data() const = 0;

  // Persists data to storage. May not do anything if data is unchanged.
  virtual zx_status_t Persist(abr::Data data) = 0;

  // Validates that the ABR metadata is valid.
  bool IsValid() const;

 protected:
  // Updates CRC stored inside of data.
  static void UpdateCrc(abr::Data* data);
};

class AstroClient {
 public:
  static zx_status_t Create(fbl::unique_fd devfs_root, std::unique_ptr<abr::Client>* out);
};

class SherlockClient {
 public:
  static zx_status_t Create(fbl::unique_fd devfs_root, std::unique_ptr<abr::Client>* out);
};

}  // namespace abr
