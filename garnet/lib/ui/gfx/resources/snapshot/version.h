// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_SNAPSHOT_VERSION_H_
#define GARNET_LIB_UI_GFX_RESOURCES_SNAPSHOT_VERSION_H_

namespace scenic_impl {
namespace gfx {

// Defines the structure of a snapshot buffer.
typedef struct {
  enum SnapshotType { kFlatBuffer, kGLTF } type;
  enum SnapshotVersion { v1_0 = 1 } version;
  uint8_t data[];
} SnapshotData;

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_SNAPSHOT_VERSION_H_
