// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FOSTR_ZX_TYPES_H_
#define SRC_LIB_FOSTR_ZX_TYPES_H_

#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/fifo.h>
#include <lib/zx/object.h>
#include <lib/zx/process.h>
#include <lib/zx/resource.h>
#include <lib/zx/socket.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>

#include <ostream>

namespace zx {

std::ostream& operator<<(std::ostream& os, const zx::object_base& value);
std::ostream& operator<<(std::ostream& os, const zx::channel& value);
std::ostream& operator<<(std::ostream& os, const zx::eventpair& value);
std::ostream& operator<<(std::ostream& os, const zx::fifo& value);
std::ostream& operator<<(std::ostream& os, const zx::process& value);
std::ostream& operator<<(std::ostream& os, const zx::socket& value);
std::ostream& operator<<(std::ostream& os, const zx::thread& value);
std::ostream& operator<<(std::ostream& os, const zx::duration& value);
std::ostream& operator<<(std::ostream& os, const zx::time& value);
std::ostream& operator<<(std::ostream& os, const zx::vmo& value);

}  // namespace zx

#endif  // SRC_LIB_FOSTR_ZX_TYPES_H_
