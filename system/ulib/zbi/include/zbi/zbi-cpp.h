// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This Header is a thin C++ wrapper around the C ZBI processing API provided in
// ulib/zbi/*
//
// Documentation for methods can also be found in ulib/zbi/include/zbi/zbi.h

#pragma once

#include <stddef.h>
#include <zircon/boot/image.h>
#include <zbi/zbi.h>

namespace zbi {

class Zbi {
  public:
    explicit Zbi(uint8_t* base) : base_(base) {}

    zbi_result_t Check(zbi_header_t** err) {
        return zbi_check(base_, err);
    }

    zbi_result_t ForEach(zbi_foreach_cb_t cb, void* cookie) {
        return zbi_for_each(base_, cb, cookie);
    }

  private:
    uint8_t* base_;
};

} // namespace zbi