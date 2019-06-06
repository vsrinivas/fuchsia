// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_SESSION_CTL_H_
#define PERIDOT_BIN_SESSIONMGR_SESSION_CTL_H_

#include <lib/vfs/cpp/pseudo_dir.h>

#include <string>

#include "fbl/ref_ptr.h"

namespace modular {

class PuppetMasterImpl;

// Builds a pseudo-directory for session inspection and control for debug and
// developer tools.
class SessionCtl {
 public:
  // Constructs a SessionCtl and adds an entry to |dir| under |entry_name|. The
  // entry is removed when destructed.
  //
  // |puppet_master_impl| is not owned and must outlive *this.
  SessionCtl(vfs::PseudoDir* dir, const std::string& entry_name,
             PuppetMasterImpl* puppet_master_impl);
  ~SessionCtl();

 private:
  vfs::PseudoDir* dir_;
  const std::string entry_name_;

  PuppetMasterImpl* const puppet_master_impl_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_SESSION_CTL_H_
