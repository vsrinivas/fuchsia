
// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/realm/builder/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/internal/errors.h>
#include <lib/sys/cpp/testing/internal/realm.h>
#include <lib/sys/cpp/testing/internal/scoped_instance.h>
#include <lib/syslog/cpp/log_level.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <algorithm>
#include <utility>
#include <variant>

namespace sys::testing::internal {

ScopedInstance::ScopedInstance(const sys::ComponentContext* context,
                               fuchsia::sys2::ChildRef child_ref, ServiceDirectory exposed_dir)
    : context_(context),
      child_ref_(child_ref),
      exposed_dir_(std::move(exposed_dir)),
      has_moved_(false) {}

ScopedInstance::~ScopedInstance() {
  if (has_moved_) {
    return;
  }
  auto realm = CreateRealmPtr(context_);
  DestroyChild(realm.get(), child_ref_);
}

ScopedInstance::ScopedInstance(ScopedInstance&& other) noexcept
    : context_(other.context_),
      child_ref_(other.child_ref_),
      exposed_dir_(std::move(other.exposed_dir_)),
      has_moved_(false) {
  other.has_moved_ = true;
}

ScopedInstance& ScopedInstance::operator=(ScopedInstance&& other) noexcept {
  this->context_ = std::move(other.context_);
  this->child_ref_ = std::move(other.child_ref_);
  this->exposed_dir_ = std::move(other.exposed_dir_);
  this->has_moved_ = false;
  other.has_moved_ = true;
  return *this;
}

ScopedInstance ScopedInstance::New(const sys::ComponentContext* context, std::string collection,
                                   std::string name, std::string url) {
  auto realm = CreateRealmPtr(context);
  CreateChild(realm.get(), collection, name, url);
  auto exposed_dir =
      BindChild(realm.get(), fuchsia::sys2::ChildRef{.name = name, .collection = collection});
  return ScopedInstance(context, fuchsia::sys2::ChildRef{.name = name, .collection = collection},
                        std::move(exposed_dir));
}

}  // namespace sys::testing::internal
