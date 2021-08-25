// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// user defined data for an epoll_event
struct epoll_event {
    __u32  events;
    __u32  _not_used;
    __u64  data;
};

