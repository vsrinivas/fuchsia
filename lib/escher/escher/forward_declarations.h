// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace ftl {
template <typename T>
class RefPtr;
}  // namespace ftl

namespace escher {

class Escher;
class MeshBuilder;
struct MeshSpec;
class Mesh;
class Model;
class Stage;
struct VulkanContext;
struct VulkanSwapchain;

typedef ftl::RefPtr<Mesh> MeshPtr;
typedef ftl::RefPtr<MeshBuilder> MeshBuilderPtr;

namespace impl {
class EscherImpl;
class MeshImpl;
}  // namespace impl

}  // namespace escher
