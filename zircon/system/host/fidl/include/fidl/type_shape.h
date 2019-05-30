// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_

#include <cstdint>

class TypeShape {
public:
    constexpr TypeShape(uint32_t size,
                        uint32_t alignment,
                        uint32_t depth = 0u,
                        uint32_t max_handles = 0u,
                        uint32_t max_out_of_line = 0u,
                        bool has_padding = false)
        : size_(size),
          alignment_(alignment),
          depth_(depth),
          max_handles_(max_handles),
          max_out_of_line_(max_out_of_line),
          has_padding_(has_padding) {}
    constexpr TypeShape()
        : TypeShape(0u, 0u, 0u, 0u, 0u, false) {}

    TypeShape(const TypeShape&) = default;
    TypeShape& operator=(const TypeShape&) = default;

    uint32_t Size() const { return size_; }
    uint32_t Alignment() const { return alignment_; }
    uint32_t Depth() const { return depth_; }

    // These properties are accounted for recursively.

    uint32_t MaxHandles() const { return max_handles_; }
    uint32_t MaxOutOfLine() const { return max_out_of_line_; }
    bool HasPadding() const { return has_padding_; }

private:
    uint32_t size_;
    uint32_t alignment_;
    uint32_t depth_;
    uint32_t max_handles_;
    uint32_t max_out_of_line_;
    bool has_padding_;
};

// |FieldShape| describes a |TypeShape| that is embedded in a struct or (x)union as a member field.
// It contains additional offset and padding information.
class FieldShape {
public:
    // Constructs a |FieldShape| with zero offset and padding.
    // The offset and padding can be updated via |SetOffset| and |SetPadding| respectively.
    explicit FieldShape(TypeShape typeshape)
        : typeshape_(typeshape), offset_(0), padding_(0) {}
    FieldShape()
        : FieldShape(TypeShape()) {}

    TypeShape& Typeshape() { return typeshape_; }
    const TypeShape& Typeshape() const { return typeshape_; }

    uint32_t Size() const { return typeshape_.Size(); }
    uint32_t Alignment() const { return typeshape_.Alignment(); }
    uint32_t Depth() const { return typeshape_.Depth(); }
    uint32_t Offset() const { return offset_; }
    // Padding after this field until the next field or the end of the container.
    // See https://fuchsia.googlesource.com/fuchsia/+/master/docs/development/languages/fidl/reference/wire-format/README.md#size-and-alignment
    uint32_t Padding() const { return padding_; }
    uint32_t MaxHandles() const { return typeshape_.MaxHandles(); }
    uint32_t MaxOutOfLine() const { return typeshape_.MaxOutOfLine(); }

    void SetOffset(uint32_t offset) { offset_ = offset; }
    void SetPadding(uint32_t padding) { padding_ = padding; }

private:
    TypeShape typeshape_;
    uint32_t offset_;
    uint32_t padding_;
};

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TYPE_SHAPE_H_
