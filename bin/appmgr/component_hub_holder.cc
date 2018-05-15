// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/component_hub_holder.h"
#include "garnet/bin/appmgr/hub_holder.h"

#include "garnet/bin/appmgr/application_controller_impl.h"
#include "garnet/bin/appmgr/realm.h"

#include <fbl/ref_ptr.h>
#include <fs/pseudo-dir.h>
#include <zircon/types.h>

namespace component {

ComponentHubHolder::ComponentHubHolder(fbl::RefPtr<fs::PseudoDir> root)
    : HubHolder(root) {}

ComponentHubHolder::~ComponentHubHolder() = default;

}  // namespace component
