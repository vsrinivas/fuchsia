// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback_agent/inspect.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/inspect/query/discover.h>
#include <lib/inspect/query/json_formatter.h>
#include <lib/inspect/query/location.h>
#include <lib/inspect/query/read.h>
#include <lib/inspect/query/source.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "src/lib/fxl/functional/cancelable_callback.h"

namespace fuchsia {
namespace feedback {

fit::promise<fuchsia::mem::Buffer> CollectInspectData(zx::duration timeout) {
  using Locations = std::vector<::inspect::Location>;
  // First, we discover all the Inspect entrypoints under the realm of the
  // calling component.

  // We use a fit::bridge to create a fit::promise that will be completed when
  // the discovery is done, returning the discovered locations.
  //
  // We use a shared_ptr to share the bridge between this function, the async
  // loop on which we post the delayed task to timeout and the second thread on
  // which we run the discovery.
  std::shared_ptr<fit::bridge<Locations, void>> discovery_done =
      std::make_shared<fit::bridge<Locations, void>>();

  // fit::promise does not have the notion of a timeout. So we post a delayed
  // task that will call the completer after the timeout and return an error.
  //
  // We wrap the delayed task in a CancelableClosure so we can cancel it when
  // the fit::bridge is completed another way.
  std::unique_ptr<fxl::CancelableClosure> discovery_done_after_timeout =
      std::make_unique<fxl::CancelableClosure>([discovery_done] {
        if (discovery_done->completer) {
          FX_LOGS(ERROR) << "Inspect data discovery timed out";
          discovery_done->completer.complete_error();
        }
      });
  const zx_status_t post_status = async::PostDelayedTask(
      async_get_default_dispatcher(),
      [cb = discovery_done_after_timeout->callback()] { cb(); }, timeout);
  if (post_status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to post delayed task: " << post_status << " ("
                   << zx_status_get_string(post_status) << ")";
    FX_LOGS(ERROR) << "Skipping Inspect data collection as Inspect discovery "
                      "is not safe without a timeout";
    return fit::make_result_promise<fuchsia::mem::Buffer>(fit::error());
  }

  // We run the discovery in a separate thread as the calling component will
  // itself be discovered and we don't want to deadlock it, cf. CF-756.
  //
  // Note that this thread could be left dangling if it hangs forever trying to
  // opendir() a currently serving out/ directory from one of the discovered
  // components. It is okay to have potentially dangling threads as we run each
  // fuchsia.feedback.DataProvider request in a separate process that exits when
  // the connection with the client is closed.
  std::thread([discovery_done, discovery_done_after_timeout = std::move(
                                   discovery_done_after_timeout)]() mutable {
    Locations locations = ::inspect::SyncFindPaths("/hub");

    discovery_done_after_timeout->Cancel();
    if (locations.empty()) {
      FX_LOGS(ERROR) << "Failed to find any Inspect location";
      if (discovery_done->completer) {
        discovery_done->completer.complete_error();
      }
      return;
    }
    if (discovery_done->completer) {
      discovery_done->completer.complete_ok(std::move(locations));
    }
  }).detach();

  // Then, we connect to each entrypoint and read asynchronously its Inspect
  // data.
  return discovery_done->consumer.promise_or(fit::error())
      .and_then([](Locations& locations) {
        std::vector<fit::promise<::inspect::Source, std::string>> sources;
        for (auto location : locations) {
          sources.push_back(::inspect::ReadLocation(std::move(location)));
        }

        return fit::join_promise_vector(std::move(sources))
            .and_then(
                [](std::vector<fit::result<::inspect::Source, std::string>>&
                       sources) -> fit::result<fuchsia::mem::Buffer> {
                  std::vector<::inspect::Source> ok_sources;
                  for (auto& source : sources) {
                    if (source.is_ok()) {
                      ok_sources.emplace_back(source.take_value());
                    } else {
                      FX_LOGS(ERROR) << "Failed to read one Inspect location: "
                                     << source.take_error();
                    }
                  }

                  fsl::SizedVmo vmo;
                  if (!fsl::VmoFromString(
                          ::inspect::JsonFormatter(
                              ::inspect::JsonFormatter::Options{},
                              ::inspect::Formatter::PathFormat::ABSOLUTE)
                              .FormatSourcesRecursive(ok_sources),
                          &vmo)) {
                    FX_LOGS(ERROR)
                        << "Failed to convert Inspect data JSON string to vmo";
                    return fit::error();
                  }
                  return fit::ok(std::move(vmo).ToTransport());
                })
            .or_else([]() {
              FX_LOGS(ERROR) << "Failed to get Inspect data";
              return fit::error();
            });
      });
}

}  // namespace feedback
}  // namespace fuchsia
