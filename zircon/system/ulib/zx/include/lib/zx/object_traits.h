// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_OBJECT_TRAITS_H_
#define LIB_ZX_OBJECT_TRAITS_H_

namespace zx {

class channel;
class eventpair;
class exception;
class fifo;
class guest;
class interrupt;
class job;
class log;
class msi;
class port;
class process;
class pmt;
class resource;
class socket;
class thread;
class vmar;
class vmo;

// The default traits supports:
// - bti
// - event
// - iommu
// - profile
// - timer
// - vmo
template <typename T>
struct object_traits {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = true;
  static constexpr bool supports_wait = true;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = false;
};

template <>
struct object_traits<channel> {
  static constexpr bool supports_duplication = false;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = true;
  static constexpr bool supports_wait = true;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = true;
};

template <>
struct object_traits<eventpair> {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = true;
  static constexpr bool supports_wait = true;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = true;
};

template <>
struct object_traits<fifo> {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = true;
  static constexpr bool supports_wait = true;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = true;
};

template <>
struct object_traits<log> {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = true;
  static constexpr bool supports_wait = true;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = false;
};

template <>
struct object_traits<pmt> {
  static constexpr bool supports_duplication = false;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = false;
  static constexpr bool supports_wait = false;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = false;
};

template <>
struct object_traits<socket> {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = true;
  static constexpr bool supports_wait = true;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = true;
};

template <>
struct object_traits<port> {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = false;
  static constexpr bool supports_wait = false;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = false;
};

template <>
struct object_traits<vmar> {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = false;
  static constexpr bool supports_wait = false;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = false;
};

template <>
struct object_traits<interrupt> {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = false;
  static constexpr bool supports_wait = true;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = false;
};

template <>
struct object_traits<guest> {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = false;
  static constexpr bool supports_wait = false;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = false;
};

template <>
struct object_traits<exception> {
  static constexpr bool supports_duplication = false;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = false;
  static constexpr bool supports_wait = false;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = false;
};

template <>
struct object_traits<job> {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = true;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = true;
  static constexpr bool supports_wait = true;
  static constexpr bool supports_kill = true;
  static constexpr bool has_peer_handle = false;
};

template <>
struct object_traits<process> {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = true;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = true;
  static constexpr bool supports_wait = true;
  static constexpr bool supports_kill = true;
  static constexpr bool has_peer_handle = false;
};

template <>
struct object_traits<thread> {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = true;
  static constexpr bool supports_user_signal = true;
  static constexpr bool supports_wait = true;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = false;
};

template <>
struct object_traits<resource> {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = true;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = true;
  static constexpr bool supports_wait = true;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = false;
};

template <>
struct object_traits<msi> {
  static constexpr bool supports_duplication = true;
  static constexpr bool supports_get_child = false;
  static constexpr bool supports_set_profile = false;
  static constexpr bool supports_user_signal = false;
  static constexpr bool supports_wait = true;
  static constexpr bool supports_kill = false;
  static constexpr bool has_peer_handle = false;
};

}  // namespace zx

#endif  // LIB_ZX_OBJECT_TRAITS_H_
