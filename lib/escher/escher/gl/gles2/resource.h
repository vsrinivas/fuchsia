// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/gl/gles2/bindings.h"
#include "escher/util/need.h"

namespace escher {
namespace gles2 {

template <typename ResourceSpec>
class Resource {
 public:
  // It is up to the caller to ensure that the resource spec matches the
  // resource identified by 'id'.
  explicit Resource(ResourceSpec spec,
                    GLuint id,
                    ftl::RefPtr<Need> need = nullptr)
      : spec_(spec), id_(id), need_(std::move(need)) {}

  Resource() : id_(0) {}
  Resource(const Resource<ResourceSpec>& other) = default;
  Resource(Resource<ResourceSpec>&& other) = default;

  Resource& operator=(const Resource<ResourceSpec>& other) = default;
  Resource& operator=(Resource<ResourceSpec>&& other) = default;

  const ResourceSpec& spec() const { return spec_; }
  const GLuint id() const { return id_; }

  explicit operator bool() const { return static_cast<bool>(id_); }

  // Return true if some manager will be notified when the resource is no
  // longer needed, otherwise false.
  bool IsManaged() const { return static_cast<bool>(need_); }

 private:
  ResourceSpec spec_;
  GLuint id_;
  ftl::RefPtr<Need> need_;
};

}  // namespace gles2
}  // namespace escher
