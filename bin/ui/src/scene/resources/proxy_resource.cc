// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/resources/proxy_resource.h"
#include <limits>
#include "apps/mozart/src/scene/resources/nodes/entity_node.h"

namespace mozart {
namespace scene {
namespace {
// Resources that are created to be ops delegates for a |ProxyResource| are not
// directly owned by the |ResourceMap|. Instead, they are  owned by the proxy
// resources themselves. So we give them a special identifier that is not part
// of any session.
constexpr ResourceId kDelegateResourceId =
    std::numeric_limits<ResourceId>::max();

ResourcePtr CreateDelegate(Session* session, mozart2::ImportSpec spec) {
  switch (spec) {
    case mozart2::ImportSpec::NODE:
      return ftl::MakeRefCounted<EntityNode>(session, kDelegateResourceId);
  }
  return nullptr;
}
}  // namespace

constexpr ResourceTypeInfo ProxyResource::kTypeInfo = {ResourceType::kProxy,
                                                       "Proxy"};

ProxyResource::ProxyResource(Session* session,
                             mozart2::ImportSpec spec,
                             mx::eventpair import_token)
    : Resource(session, ProxyResource::kTypeInfo),
      import_token_(std::move(import_token)),
      import_spec_(spec),
      delegate_(CreateDelegate(session, spec)) {
  FTL_DCHECK(delegate_);
  FTL_DCHECK(!delegate_->type_info().IsKindOf(ProxyResource::kTypeInfo));
}

ProxyResource::~ProxyResource() {
  if (bound_resource_ != nullptr) {
    bound_resource_->UnbindFromProxy(this);
    bound_resource_ = nullptr;
  }
}

Resource* ProxyResource::GetDelegate(const ResourceTypeInfo& type_info) {
  if (ProxyResource::kTypeInfo == type_info) {
    return this;
  }
  return delegate_->GetDelegate(type_info);
}

void ProxyResource::SetBoundResource(const Resource* resource) {
  bound_resource_ = resource;
}

void ProxyResource::ClearBoundResource() {
  bound_resource_ = nullptr;
}

}  // namespace scene
}  // namespace mozart
