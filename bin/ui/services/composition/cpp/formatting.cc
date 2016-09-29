// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/composition/cpp/formatting.h"

#include <ostream>

namespace mozart {

class Delimiter {
 public:
  Delimiter(std::ostream& os) : os_(os) {}

  std::ostream& Append() {
    if (need_comma_)
      os_ << ", ";
    else
      need_comma_ = true;
    return os_;
  }

 private:
  std::ostream& os_;
  bool need_comma_ = false;
};

std::ostream& operator<<(std::ostream& os, const SceneToken& value) {
  return os << "<S" << value.value << ">";
}

std::ostream& operator<<(std::ostream& os, const SceneUpdate& value) {
  os << "{";
  Delimiter d(os);
  if (value.clear_resources) {
    d.Append() << "clear_resources=true";
  }
  if (value.clear_nodes) {
    d.Append() << "clear_nodes=true";
  }
  if (value.resources) {
    d.Append() << "resources=" << value.resources;
  }
  if (value.nodes) {
    d.Append() << "nodes=" << value.nodes;
  }
  os << "}";
  return os;
}

std::ostream& operator<<(std::ostream& os, const SceneMetadata& value) {
  return os << "{version=" << value.version
            << ", presentation_time=" << value.presentation_time << "}";
}

std::ostream& operator<<(std::ostream& os, const Resource& value) {
  os << "{";
  if (value.is_scene()) {
    os << "scene=" << value.get_scene();
  } else if (value.is_image()) {
    os << "imsage=" << value.get_image();
  } else {
    os << "???";
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const SceneResource& value) {
  return os << "{scene_token=" << value.scene_token << "}";
}

std::ostream& operator<<(std::ostream& os, const ImageResource& value) {
  return os << "{image=" << value.image << "}";
}

std::ostream& operator<<(std::ostream& os, const Image& value) {
  return os << "{size=" << value.size << ", stride=" << value.stride
            << ", offset=" << value.offset
            << ", pixel_format=" << &value.pixel_format
            << ", alpha_format=" << &value.alpha_format
            << ", color_space=" << &value.color_space
            << ", buffer=" << value.buffer.get().value() << "}";
}

std::ostream& operator<<(std::ostream& os, const Image::PixelFormat* value) {
  switch (*value) {
    case Image::PixelFormat::B8G8R8A8:
      return os << "B8G8R8A8";
    default:
      return os << "???";
  }
}

std::ostream& operator<<(std::ostream& os, const Image::AlphaFormat* value) {
  switch (*value) {
    case Image::AlphaFormat::OPAQUE:
      return os << "OPAQUE";
    case Image::AlphaFormat::PREMULTIPLIED:
      return os << "PREMULTIPLIED";
    case Image::AlphaFormat::NON_PREMULTIPLIED:
      return os << "NON_PREMULTIPLIED";
    default:
      return os << "???";
  }
}

std::ostream& operator<<(std::ostream& os, const Image::ColorSpace* value) {
  switch (*value) {
    case Image::ColorSpace::SRGB:
      return os << "SRGB";
    default:
      return os << "???";
  }
}

std::ostream& operator<<(std::ostream& os, const Node& value) {
  os << "{";
  Delimiter d(os);
  if (value.content_transform)
    d.Append() << "content_transform=" << value.content_transform;
  if (value.content_clip)
    d.Append() << "content_clip=" << value.content_clip;
  if (value.hit_test_behavior)
    d.Append() << "hit_test_behavior=" << value.hit_test_behavior;
  if (value.op)
    d.Append() << "op=" << value.op;
  d.Append() << "combinator=" << &value.combinator;
  if (value.child_node_ids)
    d.Append() << "child_node_ids=" << value.child_node_ids;
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const Node::Combinator* value) {
  switch (*value) {
    case Node::Combinator::MERGE:
      return os << "MERGE";
    case Node::Combinator::PRUNE:
      return os << "PRUNE";
    case Node::Combinator::FALLBACK:
      return os << "FALLBACK";
    default:
      return os << "???";
  }
}

std::ostream& operator<<(std::ostream& os, const NodeOp& value) {
  os << "{";
  if (value.is_rect()) {
    os << "rect=" << value.get_rect();
  } else if (value.is_image()) {
    os << "image=" << value.get_image();
  } else if (value.is_scene()) {
    os << "scene=" << value.get_scene();
  } else if (value.is_layer()) {
    os << "layer=" << value.get_layer();
  } else {
    os << "???";
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const RectNodeOp& value) {
  return os << "{content_rect=" << value.content_rect
            << ", color=" << value.color << "}";
}

std::ostream& operator<<(std::ostream& os, const ImageNodeOp& value) {
  return os << "{content_rect=" << value.content_rect
            << ", image_rect=" << value.image_rect
            << ", image_resource_id=" << value.image_resource_id
            << ", blend=" << value.blend << "}";
}

std::ostream& operator<<(std::ostream& os, const SceneNodeOp& value) {
  return os << "{scene_resource_id=" << value.scene_resource_id
            << ", scene_version=" << value.scene_version << "}";
}

std::ostream& operator<<(std::ostream& os, const LayerNodeOp& value) {
  return os << "{layer_rect=" << value.layer_rect << ", blend=" << value.blend
            << "}";
}

std::ostream& operator<<(std::ostream& os, const Color& value) {
  return os << "{red=" << static_cast<int>(value.red)
            << ", green=" << static_cast<int>(value.green)
            << ", blue=" << static_cast<int>(value.blue)
            << ", alpha=" << static_cast<int>(value.alpha) << "}";
}

std::ostream& operator<<(std::ostream& os, const Blend& value) {
  return os << "{alpha=" << static_cast<int>(value.alpha) << "}";
}

std::ostream& operator<<(std::ostream& os, const FrameInfo& value) {
  return os << "{frame_time=" << value.frame_time
            << ", frame_interval=" << value.frame_interval
            << ", frame_deadline=" << value.frame_deadline << "}";
}

std::ostream& operator<<(std::ostream& os, const HitTestBehavior& value) {
  return os << "{visibility=" << &value.visibility << ", prune" << value.prune
            << ", hit_rect=" << value.hit_rect << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const HitTestBehavior::Visibility* value) {
  switch (*value) {
    case HitTestBehavior::Visibility::OPAQUE:
      return os << "OPAQUE";
    case HitTestBehavior::Visibility::TRANSLUCENT:
      return os << "TRANSLUCENT";
    case HitTestBehavior::Visibility::INVISIBLE:
      return os << "INVISIBLE";
    default:
      return os << "???";
  }
}

std::ostream& operator<<(std::ostream& os, const HitTestResult& value) {
  return os << "{root=" << value.root << "}";
}

std::ostream& operator<<(std::ostream& os, const Hit& value) {
  os << "{";
  if (value.is_scene()) {
    os << "scene=" << value.get_scene();
  } else if (value.is_node()) {
    os << "node=" << value.get_node();
  } else {
    os << "???";
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const SceneHit& value) {
  return os << "{scene_token=" << value.scene_token
            << ", scene_version=" << value.scene_version
            << ", hits=" << value.hits << "}";
}

std::ostream& operator<<(std::ostream& os, const NodeHit& value) {
  return os << "{node_id=" << value.node_id << ", transform=" << value.transform
            << "}";
}

}  // namespace mozart
