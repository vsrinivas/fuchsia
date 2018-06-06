// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_VECTOR_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_VECTOR_H_

// Some android sources rely on this being pulled in:
#include <log/log.h>

#include <vector>

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#if __has_attribute(no_sanitize)
#define UTILS_VECTOR_NO_CFI __attribute__((no_sanitize("cfi")))
#else
#define UTILS_VECTOR_NO_CFI
#endif

namespace android {

template <typename T>
class Vector {
 public:
  inline size_t size() const { return vector_.size(); }
  ssize_t add(const T& item);
  inline const T& itemAt(size_t index) const;
  const T& top() const;
  inline void pop();
  inline void push();
  void push(const T& item);
  T& editItemAt(size_t index);
  inline ssize_t removeAt(size_t index);
  inline void clear();
  inline bool empty() const;
  inline const T& operator[](size_t index) const;

 private:
  std::vector<T> vector_;
};

template <typename T>
inline ssize_t Vector<T>::add(const T& item) {
  ssize_t index_of_new_item = vector_.size();
  vector_.push_back(item);
  return index_of_new_item;
}

template <typename T>
inline const T& Vector<T>::itemAt(size_t index) const {
  return vector_.operator[](index);
}

template <typename T>
inline const T& Vector<T>::top() const {
  return vector_[vector_.size() - 1];
}

template <typename T>
inline void Vector<T>::pop() {
  if (vector_.empty()) {
    return;
  }
  vector_.pop_back();
}

template <typename T>
inline void Vector<T>::push() {
  vector_.emplace_back();
}

template <typename T>
inline void Vector<T>::push(const T& item) {
  vector_.push_back(item);
}

template <typename T>
inline T& Vector<T>::editItemAt(size_t index) {
  return vector_[index];
}

template <typename T>
inline ssize_t Vector<T>::removeAt(size_t index) {
  vector_.erase(vector_.begin() + index);
  return index;
}

template <typename T>
inline void Vector<T>::clear() {
  vector_.clear();
}

template <typename T>
inline bool Vector<T>::empty() const {
  return vector_.empty();
}

template <typename T>
inline const T& Vector<T>::operator[](size_t index) const {
  return vector_[index];
}

}  // namespace android

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_VECTOR_H_
