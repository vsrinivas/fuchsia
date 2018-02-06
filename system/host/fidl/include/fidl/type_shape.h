// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_

#include <stddef.h>

class TypeShape {
public:
    constexpr TypeShape(size_t size, size_t alignment) : size_(size), alignment_(alignment) {}
    constexpr TypeShape() : TypeShape(0u, 0u) {}

    TypeShape(const TypeShape&) = default;
    TypeShape& operator=(const TypeShape&) = default;

    size_t Size() const { return size_; }
    size_t Alignment() const { return alignment_; }

private:
    size_t size_;
    size_t alignment_;
};

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_
