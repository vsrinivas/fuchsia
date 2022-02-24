// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_NAMESTRING_H_
#define SRC_STORAGE_F2FS_NAMESTRING_H_

namespace f2fs {

inline bool IsValidNameLength(std::string_view name) {
  return name.length() <= kMaxNameLen;
}

class NameString final {
 public:
  NameString() = default;
  NameString(const NameString &) = default;
  NameString(const NameString &&) = delete;
  NameString &operator=(const NameString &&) = delete;

  std::string_view GetStringView() const { return std::string_view(name_); }

  NameString &operator=(std::string_view name) {
    ZX_DEBUG_ASSERT(IsValidNameLength(name));
    name_ = name;
    name_.shrink_to_fit();
    return *this;
  }

 private:
  std::string name_;
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_NAMESTRING_H_
