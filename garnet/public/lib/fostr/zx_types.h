// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FOSTR_ZX_TYPES_H_
#define LIB_FOSTR_ZX_TYPES_H_

#include <ostream>

#include <zx/channel.h>
#include <zx/eventpair.h>
#include <zx/fifo.h>
#include <zx/object.h>
#include <zx/process.h>
#include <zx/resource.h>
#include <zx/socket.h>
#include <zx/thread.h>
#include <zx/time.h>
#include <zx/vmo.h>

namespace zx {

std::ostream& operator<<(std::ostream& os, const zx::object_base& value);
std::ostream& operator<<(std::ostream& os, const zx::channel& value);
std::ostream& operator<<(std::ostream& os, const zx::eventpair& value);
std::ostream& operator<<(std::ostream& os, const zx::fifo& value);
std::ostream& operator<<(std::ostream& os, const zx::process& value);
std::ostream& operator<<(std::ostream& os, const zx::socket& value);
std::ostream& operator<<(std::ostream& os, const zx::thread& value);
std::ostream& operator<<(std::ostream& os, const zx::duration& value);
std::ostream& operator<<(std::ostream& os, const zx::vmo& value);

}  // namespace zx

#endif  // LIB_FOSTR_ZX_TYPES_H_
