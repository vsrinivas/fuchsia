// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

struct sockaddr_in6;

int netboot_open(const char* hostname, unsigned port, struct sockaddr_in6* addr_out);