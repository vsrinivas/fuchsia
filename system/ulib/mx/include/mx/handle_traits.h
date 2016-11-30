// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace mx {

class channel;
class eventpair;
class log;
class socket;
class vmo;
class waitset;

// The default traits supports:
// - event
// - thread
// - process
// - job
// - vmar
// - vmo
template <typename T> struct handle_traits {
    static const bool supports_duplication = true;
    static const bool supports_user_signal = true;
    static const bool has_peer_handle = false;
};

template <> struct handle_traits<channel> {
    static const bool supports_duplication = false;
    static const bool supports_user_signal = true;
    static const bool has_peer_handle = true;
};

template <> struct handle_traits<eventpair> {
    static const bool supports_duplication = true;
    static const bool supports_user_signal = true;
    static const bool has_peer_handle = true;
};

template <> struct handle_traits<log> {
    static const bool supports_duplication = true;
    static const bool supports_user_signal = false;
    static const bool has_peer_handle = false;
};

template <> struct handle_traits<socket> {
    static const bool supports_duplication = true;
    static const bool supports_user_signal = true;
    static const bool has_peer_handle = true;
};

template <> struct handle_traits<waitset> {
    static const bool supports_duplication = false;
    static const bool supports_user_signal = true;
    static const bool has_peer_handle = false;
};

} // namespace mx
