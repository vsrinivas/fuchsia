// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_HEADER_H_
#define LIB_FIDL_CPP_INTERNAL_HEADER_H_

#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/fit/variant.h>

#include <array>
#include <functional>
#include <ostream>

#ifdef __Fuchsia__
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/event.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/exception.h>
#include <lib/zx/fifo.h>
#include <lib/zx/guest.h>
#include <lib/zx/handle.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/job.h>
#include <lib/zx/object.h>
#include <lib/zx/pmt.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/profile.h>
#include <lib/zx/resource.h>
#include <lib/zx/socket.h>
#include <lib/zx/stream.h>
#include <lib/zx/suspend_token.h>
#include <lib/zx/task.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <lib/zx/timer.h>
#include <lib/zx/vcpu.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#endif

#include "lib/fidl/cpp/coding_traits.h"
#include "lib/fidl/cpp/enum.h"
#include "lib/fidl/cpp/internal/logging.h"

#ifdef __Fuchsia__
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/internal/proxy_controller.h"
#include "lib/fidl/cpp/internal/stub_controller.h"
#include "lib/fidl/cpp/internal/synchronous_proxy.h"
#include "lib/fidl/cpp/member_connector.h"
#include "lib/fidl/cpp/service_handler_base.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#endif

#include "lib/fidl/cpp/comparison.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fidl/cpp/vector.h"

// clone.h must be imported before any of the generated Clone methods are
// defined, so that calls to Clone in clone.h are referencing the ADL
// implementation and are not ambiguous.
#include "lib/fidl/cpp/clone.h"

#endif  // LIB_FIDL_CPP_INTERNAL_HEADER_H_
