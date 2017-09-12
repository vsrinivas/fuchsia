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

// The default traits supports:
// - event
// - thread
// - process
// - job
// - vmar
// - vmo
template <typename T> struct object_traits {
    static const bool supports_duplication = true;
    static const bool supports_user_signal = true;
    static const bool has_peer_handle = false;
};

template <> struct object_traits<channel> {
    static const bool supports_duplication = false;
    static const bool supports_user_signal = true;
    static const bool has_peer_handle = true;
};

template <> struct object_traits<eventpair> {
    static const bool supports_duplication = true;
    static const bool supports_user_signal = true;
    static const bool has_peer_handle = true;
};

template <> struct object_traits<log> {
    static const bool supports_duplication = true;
    static const bool supports_user_signal = false;
    static const bool has_peer_handle = false;
};

template <> struct object_traits<socket> {
    static const bool supports_duplication = true;
    static const bool supports_user_signal = true;
    static const bool has_peer_handle = true;
};

} // namespace zx
