// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace devmgr {

class EncryptedVolumeInterface {
 public:
  virtual ~EncryptedVolumeInterface() = default;

  // Does everything it can to ensure that by the time this function returns,
  // there is an unsealed block device exposed.  If none of the available keys
  // can unseal the device, then it is permissible for the implementation to
  // reformat the backing store to make *some* storage available.
  zx_status_t EnsureUnsealedAndFormatIfNeeded();

 private:
  // Attempt to unseal the underlying volume.
  virtual zx_status_t Unseal() = 0;

  // Format the underlying volume with the best available key source.  This will
  // destroy any data contained therein, but will ensure that we can
  // subsequently unseal the newly-formatted volume, rather than getting stuck.
  virtual zx_status_t Format() = 0;
};

}  // namespace devmgr
