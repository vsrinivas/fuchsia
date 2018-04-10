// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zx {

class channel;
class eventpair;
class log;
class socket;
class vmo;
class vmar;
class port;
class guest;
class fifo;
class interrupt;
class pmt;

// The default traits supports:
// - event
// - thread
// - process
// - job
// - vmo
// - bti
// - resource
// - timer
template <typename T> struct object_traits {
    static constexpr bool supports_duplication = true;
    static constexpr bool supports_user_signal = true;
    static constexpr bool supports_wait = true;
    static constexpr bool has_peer_handle = false;
};

template <> struct object_traits<channel> {
    static constexpr bool supports_duplication = false;
    static constexpr bool supports_user_signal = true;
    static constexpr bool supports_wait = true;
    static constexpr bool has_peer_handle = true;
};

template <> struct object_traits<eventpair> {
    static constexpr bool supports_duplication = true;
    static constexpr bool supports_user_signal = true;
    static constexpr bool supports_wait = true;
    static constexpr bool has_peer_handle = true;
};

template <> struct object_traits<fifo> {
    static constexpr bool supports_duplication = true;
    static constexpr bool supports_user_signal = true;
    static constexpr bool supports_wait = true;
    static constexpr bool has_peer_handle = true;
};

template <> struct object_traits<log> {
    static constexpr bool supports_duplication = true;
    static constexpr bool supports_user_signal = true;
    static constexpr bool supports_wait = true;
    static constexpr bool has_peer_handle = false;
};

template <> struct object_traits<pmt> {
    static constexpr bool supports_duplication = false;
    static constexpr bool supports_user_signal = false;
    static constexpr bool supports_wait = false;
    static constexpr bool has_peer_handle = false;
};

template <> struct object_traits<socket> {
    static constexpr bool supports_duplication = true;
    static constexpr bool supports_user_signal = true;
    static constexpr bool supports_wait = true;
    static constexpr bool has_peer_handle = true;
};

template <> struct object_traits<port> {
    static constexpr bool supports_duplication = true;
    static constexpr bool supports_user_signal = false;
    static constexpr bool supports_wait = false;
    static constexpr bool has_peer_handle = false;
};

template <> struct object_traits<vmar> {
    static constexpr bool supports_duplication = true;
    static constexpr bool supports_user_signal = false;
    static constexpr bool supports_wait = false;
    static constexpr bool has_peer_handle = false;
};

template <> struct object_traits<interrupt> {
    static constexpr bool supports_duplication = false;
    static constexpr bool supports_user_signal = false;
    static constexpr bool supports_wait = true;
    static constexpr bool has_peer_handle = false;
};

template <> struct object_traits<guest> {
    static constexpr bool supports_duplication = true;
    static constexpr bool supports_user_signal = false;
    static constexpr bool supports_wait = false;
    static constexpr bool has_peer_handle = false;
};

} // namespace zx
