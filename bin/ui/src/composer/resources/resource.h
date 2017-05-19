// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/lib/composer/types.h"
#include "apps/mozart/src/composer/resources/resource_type_info.h"
#include "lib/ftl/memory/ref_counted.h"

namespace mozart {
namespace composer {

class ErrorReporter;
class Session;

// Resource is the base class for all client-created objects (i.e. those that
// are created in response to a CreateResourceOp operation).
// TODO: use unsafe ref-counting for better performance (our architecture
// guarantees that this is safe).
class Resource : public ftl::RefCountedThreadSafe<Resource> {
 public:
  static const ResourceTypeInfo kTypeInfo;

  virtual ~Resource();

  const ResourceTypeInfo& type_info() const { return type_info_; }
  ResourceTypeFlags type_flags() const { return type_info_.flags; }
  const char* type_name() const { return type_info_.name; }
  Session* session() const { return session_; }
  ErrorReporter* error_reporter() const;
  virtual void Accept(class ResourceVisitor* visitor) = 0;

 protected:
  Resource(Session* session, const ResourceTypeInfo& type_info);

 private:
  Session* const session_;
  const ResourceTypeInfo& type_info_;
};

typedef ftl::RefPtr<Resource> ResourcePtr;

}  // namespace composer
}  // namespace mozart
