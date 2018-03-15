// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_HEADER_H_
#define LIB_FIDL_CPP_INTERNAL_HEADER_H_

#include <lib/fidl/cpp/array.h>
#include <zx/channel.h>
#include <zx/event.h>
#include <zx/eventpair.h>
#include <zx/fifo.h>
#include <zx/guest.h>
#include <zx/interrupt.h>
#include <zx/job.h>
#include <zx/log.h>
#include <zx/port.h>
#include <zx/process.h>
#include <zx/resource.h>
#include <zx/socket.h>
#include <zx/thread.h>
#include <zx/timer.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include <functional>

#include "lib/fidl/cpp/coding_traits.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/internal/proxy_controller.h"
#include "lib/fidl/cpp/internal/stub_controller.h"
#include "lib/fidl/cpp/internal/synchronous_proxy.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fidl/cpp/vector.h"

#endif  // LIB_FIDL_CPP_INTERNAL_HEADER_H_
