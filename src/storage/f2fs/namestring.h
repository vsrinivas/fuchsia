// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_NAMESTRING_H_
#define SRC_STORAGE_F2FS_NAMESTRING_H_

namespace f2fs {
class NameString final {
 public:
  NameString() : len_(0){};
  NameString(const NameString &) = default;
  NameString(std::string_view name) {
    ZX_ASSERT(name.length() <= kMaxNameLen);
    len_ = static_cast<uint16_t>(name.length());
    memcpy(name_, name.data(), len_);
  }
  ~NameString() = default;

  std::string_view GetStringView() const { return std::string_view(name_, len_); };
  char *GetData() { return name_; };
  uint16_t GetLen() const { return len_; };

  NameString &operator=(std::string_view name) {
    ZX_ASSERT(name.length() <= kMaxNameLen);
    len_ = static_cast<uint16_t>(name.length());
    memcpy(name_, name.data(), len_);
    name_[len_] = 0;
    return *this;
  }

 private:
  char name_[kMaxNameLen + 1];
  uint16_t len_;
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_NAMESTRING_H_
