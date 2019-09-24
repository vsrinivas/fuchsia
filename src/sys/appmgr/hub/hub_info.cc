// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hub_info.h"

#include <fbl/string.h>
#include <fs/pseudo-dir.h>

namespace component {

HubInfo::HubInfo(fbl::String label, fbl::String koid, fbl::RefPtr<fs::PseudoDir> hub_dir)
    : label_(std::move(label)), koid_(std::move(koid)), hub_dir_(std::move(hub_dir)) {}

HubInfo::~HubInfo() = default;

}  // namespace component
