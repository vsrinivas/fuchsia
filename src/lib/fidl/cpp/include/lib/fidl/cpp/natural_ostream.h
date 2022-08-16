// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_OSTREAM_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_OSTREAM_H_

#include <iostream>
#include <optional>

#ifdef __Fuchsia__
#include <lib/zx/object.h>
#endif

namespace fidl::ostream {
// Wrapper type to disambiguate formatting operator overloads.
//
// This file avoids defining any overloads for types in the std namespace. To correctly format
// arrays, vectors and unique pointers, this wrapper is used so we can define an overload for
// e.g. fidl::ostream::Formatted<std::unique_ptr<T>> instead of defining one for std::unique_ptr<T>.
// Consequently, this wrapper must be used for the supported std types. The wrapper has no effect
// for other types, so it can safely be applied to any value.
//
//     std::vector<int32_t> my_vector;
//     os << fidl::ostream::Formatted(my_vector);
//
template <typename T>
struct Formatted {
  explicit Formatted(const T& v) : value(v) {}
  const T& value;
};

template <typename T, typename = void>
struct Formatter;

template <typename T, typename F = Formatter<T>>
std::ostream& operator<<(std::ostream& os, const Formatted<T>& value) {
  return F::Format(os, value.value);
}

template <>
struct Formatter<bool> {
  static std::ostream& Format(std::ostream& os, bool value) {
    return os << (value ? "true" : "false");
  }
};

template <>
struct Formatter<uint8_t> {
  static std::ostream& Format(std::ostream& os, uint8_t value) {
    return os << static_cast<unsigned int>(value);
  }
};

template <>
struct Formatter<int8_t> {
  static std::ostream& Format(std::ostream& os, int8_t value) {
    return os << static_cast<int>(value);
  }
};

template <typename T>
struct Formatter<T, std::enable_if_t<std::is_integral_v<T> || std::is_floating_point_v<T>>> {
  static_assert(sizeof(T) > 1, "There's special handling for bytes");
  static std::ostream& Format(std::ostream& os, T value) { return os << value; }
};

template <>
struct Formatter<std::string> {
  static std::ostream& Format(std::ostream& os, const std::string& value) {
    char escape[8];
    os << '"';
    for (auto const& ch : value) {
      if (isprint(ch) && ch != '"') {
        os << ch;
      } else {
        // Note: this will use \x## for bytes rather than decode
        // UTF-8 into \u####
        snprintf(escape, 8, "\\x%02x", ch & 0xFF);
        os << escape;
      }
    }
    return os << '"';
  }
};

template <>
struct Formatter<std::optional<std::string>> {
  static std::ostream& Format(std::ostream& os, const std::optional<std::string>& value) {
    if (value.has_value()) {
      return Formatter<std::string>::Format(os, value.value());
    } else {
      return os << "null";
    }
  }
};

#ifdef __Fuchsia__
template <typename T>
struct Formatter<T, std::enable_if_t<std::is_base_of_v<zx::object_base, T>>> {
  static std::ostream& Format(std::ostream& os, const T& value) {
    zx_handle_t handle_value = value.get();
    switch (T::TYPE) {
      case ZX_OBJ_TYPE_BTI:
        return os << "bti(" << handle_value << ")";
      case ZX_OBJ_TYPE_CHANNEL:
        return os << "channel(" << handle_value << ")";
      case ZX_OBJ_TYPE_CLOCK:
        return os << "clock(" << handle_value << ")";
      case ZX_OBJ_TYPE_EVENT:
        return os << "event(" << handle_value << ")";
      case ZX_OBJ_TYPE_EVENTPAIR:
        return os << "eventpair(" << handle_value << ")";
      case ZX_OBJ_TYPE_EXCEPTION:
        return os << "exception(" << handle_value << ")";
      case ZX_OBJ_TYPE_FIFO:
        return os << "fifo(" << handle_value << ")";
      case ZX_OBJ_TYPE_GUEST:
        return os << "guest(" << handle_value << ")";
      case ZX_OBJ_TYPE_INTERRUPT:
        return os << "interrupt(" << handle_value << ")";
      case ZX_OBJ_TYPE_IOMMU:
        return os << "iommu(" << handle_value << ")";
      case ZX_OBJ_TYPE_JOB:
        return os << "job(" << handle_value << ")";
      case ZX_OBJ_TYPE_DEBUGLOG:
        return os << "debuglog(" << handle_value << ")";
      case ZX_OBJ_TYPE_MSI:
        return os << "msi(" << handle_value << ")";
      case ZX_OBJ_TYPE_PAGER:
        return os << "pager(" << handle_value << ")";
      case ZX_OBJ_TYPE_PCI_DEVICE:
        return os << "pci_device(" << handle_value << ")";
      case ZX_OBJ_TYPE_PMT:
        return os << "pmt(" << handle_value << ")";
      case ZX_OBJ_TYPE_PORT:
        return os << "port(" << handle_value << ")";
      case ZX_OBJ_TYPE_PROCESS:
        return os << "process(" << handle_value << ")";
      case ZX_OBJ_TYPE_PROFILE:
        return os << "profile(" << handle_value << ")";
      case ZX_OBJ_TYPE_RESOURCE:
        return os << "resource(" << handle_value << ")";
      case ZX_OBJ_TYPE_SOCKET:
        return os << "socket(" << handle_value << ")";
      case ZX_OBJ_TYPE_STREAM:
        return os << "stream(" << handle_value << ")";
      case ZX_OBJ_TYPE_SUSPEND_TOKEN:
        return os << "suspend_token(" << handle_value << ")";
      case ZX_OBJ_TYPE_THREAD:
        return os << "thread(" << handle_value << ")";
      case ZX_OBJ_TYPE_TIMER:
        return os << "timer(" << handle_value << ")";
      case ZX_OBJ_TYPE_VCPU:
        return os << "vcpu(" << handle_value << ")";
      case ZX_OBJ_TYPE_VMAR:
        return os << "vmar(" << handle_value << ")";
      case ZX_OBJ_TYPE_VMO:
        return os << "vmo(" << handle_value << ")";
      default:
        return os << "handle(" << handle_value << ")";
    }
  }
};
#endif

template <typename T>
struct Formatter<std::vector<T>> {
  static std::ostream& Format(std::ostream& os, const std::vector<T>& value) {
    os << "[ ";
    for (const auto& i : value) {
      os << Formatted(i) << ", ";
    }
    os << "]";
    return os;
  }
};

template <typename T>
struct Formatter<std::optional<std::vector<T>>> {
  static std::ostream& Format(std::ostream& os, const std::optional<std::vector<T>>& value) {
    if (value.has_value()) {
      return Formatter<std::vector<T>>::Format(os, value.value());
    } else {
      return os << "null";
    }
  }
};

template <typename T, size_t N>
struct Formatter<std::array<T, N>> {
  static std::ostream& Format(std::ostream& os, const std::array<T, N>& value) {
    os << "[ ";
    for (size_t i = 0; i < N; i++) {
      os << Formatted(value[i]) << ", ";
    }
    os << "]";
    return os;
  }
};

template <typename T>
struct Formatter<std::optional<T>> {
  static std::ostream& Format(std::ostream& os, const std::optional<T>& value) {
    if (value.has_value()) {
      return Formatter<T>::Format(os, value.value());
    } else {
      return os << "null";
    }
  }
};

template <typename T>
struct Formatter<std::unique_ptr<T>> {
  static std::ostream& Format(std::ostream& os, const std::unique_ptr<T>& value) {
    if (value) {
      return Formatter<T>::Format(os, *value);
    } else {
      return os << "null";
    }
  }
};

}  // namespace fidl::ostream

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_OSTREAM_H_
