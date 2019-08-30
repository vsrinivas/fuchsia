// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_SNAPSHOT_VERSION_H_
#define SRC_UI_SCENIC_LIB_GFX_SNAPSHOT_VERSION_H_

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

#endif  // SRC_UI_SCENIC_LIB_GFX_SNAPSHOT_VERSION_H_
