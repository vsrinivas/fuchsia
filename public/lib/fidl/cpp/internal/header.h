// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_HEADER_H_
#define LIB_FIDL_CPP_INTERNAL_HEADER_H_

#include <functional>
#include <ostream>

#include <lib/fidl/cpp/array.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/fifo.h>
#include <lib/zx/guest.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/job.h>
#include <lib/zx/log.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/resource.h>
#include <lib/zx/socket.h>
#include <lib/zx/thread.h>
#include <lib/zx/timer.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include "lib/fidl/cpp/coding_traits.h"
#include "lib/fidl/cpp/enum.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/internal/proxy_controller.h"
#include "lib/fidl/cpp/internal/stub_controller.h"
#include "lib/fidl/cpp/internal/synchronous_proxy.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fidl/cpp/vector.h"

#endif  // LIB_FIDL_CPP_INTERNAL_HEADER_H_
