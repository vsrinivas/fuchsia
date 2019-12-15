// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_TRANSFORM_HANDLE_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_TRANSFORM_HANDLE_H_

#include <functional>
#include <ostream>

namespace flatland {
class TransformHandle;
}  // namespace flatland

namespace std {
ostream& operator<<(ostream& out, const flatland::TransformHandle& h);
}  // namespace std

namespace flatland {

// A globally scoped transform handle. The current constructor allows the calling code to specify
// the internal IDs, so it is up to the calling code to enforce uniqueness when desirable.
class TransformHandle {
 public:
  TransformHandle() = default;

  TransformHandle(uint64_t graph_id, uint64_t transform_id)
      : graph_id_(graph_id), transform_id_(transform_id) {}

  bool operator==(const TransformHandle& rhs) const {
    return graph_id_ == rhs.graph_id_ && transform_id_ == rhs.transform_id_;
  }
  bool operator!=(const TransformHandle& rhs) const {
    return graph_id_ != rhs.graph_id_ || transform_id_ != rhs.transform_id_;
  }
  bool operator<(const TransformHandle& rhs) const {
    return graph_id_ < rhs.graph_id_ ||
           (graph_id_ == rhs.graph_id_ && transform_id_ < rhs.transform_id_);
  }

 private:
  friend class std::hash<flatland::TransformHandle>;
  friend std::ostream& std::operator<<(std::ostream& out, const flatland::TransformHandle& h);

  uint64_t graph_id_ = 0;
  uint64_t transform_id_ = 0;
};

}  // namespace flatland

namespace std {

// A hash specialization for TransformHandles, so that they can be stored in maps and multimaps.
template <>
struct hash<flatland::TransformHandle> {
  size_t operator()(const flatland::TransformHandle& h) const noexcept {
    return hash<uint64_t>{}(h.graph_id_) ^ hash<uint64_t>{}(h.transform_id_);
  }
};

}  // namespace std

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_TRANSFORM_HANDLE_H_
