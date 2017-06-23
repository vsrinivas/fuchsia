// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/resources/proxy_resource.h"
#include <limits>
#include "apps/mozart/src/scene/resources/nodes/entity_node.h"

namespace mozart {
namespace scene {

// Resources that are created to be ops delegates for a |ProxyResource| are not
// directly owned by the |ResourceMap|. Instead, they are  owned by the proxy
// resources themselves. So we give them a special identifier that is not part
// of any session.
static constexpr ResourceId kOpsTargetResourceId =
    std::numeric_limits<ResourceId>::max();

constexpr ResourceTypeInfo ProxyResource::kTypeInfo = {ResourceType::kProxy,
                                                       "Proxy"};

static ResourcePtr OpsTargetForSpec(Session* session,
                                    mozart2::ImportSpec spec) {
  switch (spec) {
    case mozart2::ImportSpec::NODE:
      return ftl::MakeRefCounted<EntityNode>(session, kOpsTargetResourceId);
  }
  return nullptr;
}

ProxyResource::ProxyResource(Session* session,
                             mozart2::ImportSpec spec,
                             mx::eventpair import_token)
    : Resource(session, ProxyResource::kTypeInfo),
      import_token_(std::move(import_token)),
      import_spec_(spec),
      ops_delegate_(OpsTargetForSpec(session, spec)) {
  FTL_DCHECK(ops_delegate_);
  FTL_DCHECK(!ops_delegate_->type_info().IsKindOf(ProxyResource::kTypeInfo));
}

ProxyResource::~ProxyResource() {
  if (imported_resource_ != nullptr) {
    imported_resource_->UnbindFromProxy(this);
    imported_resource_ = nullptr;
  }
}

void ProxyResource::Accept(class ResourceVisitor* visitor) {}

Resource* ProxyResource::GetOpsDelegate(const ResourceTypeInfo& type_info) {
  if (ProxyResource::kTypeInfo == type_info) {
    return this;
  }
  return ops_delegate_->GetOpsDelegate(type_info);
}

void ProxyResource::SetBoundResource(const Resource* resource) {
  imported_resource_ = resource;
}

void ProxyResource::ClearBoundResource() {
  imported_resource_ = nullptr;
}

}  // namespace scene
}  // namespace mozart
