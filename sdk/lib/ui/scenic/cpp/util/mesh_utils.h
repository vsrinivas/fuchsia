// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_UTIL_MESH_UTILS_H_
#define LIB_UI_SCENIC_CPP_UTIL_MESH_UTILS_H_

namespace scenic {
namespace mesh_utils {

// A convienence mesh constructor to reduce boiler plate needed to use
// Meshes. This method does not support texture coordinates or normals and
// only supports float vertices and uint32_t indices. |vertices| must
// contain exactly 3 floats per vertex specifying the vertex position and
// indices must contain 3 uint32_t's per primitive.
// This method may perform poorly on very large meshes so use with care.
std::unique_ptr<Mesh> NewMeshWithVertices(Session* session,
                                          const std::vector<float>& vertices,
                                          const std::vector<uint32_t>& indices);

}  // namespace mesh_utils
}  // namespace scenic

#endif  // LIB_UI_SCENIC_CPP_UTIL_MESH_UTILS_H_
