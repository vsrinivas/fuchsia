// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/inspect_ptr.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/result.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <string>
#include <vector>

#include "src/developer/feedback/utils/promise.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/inspect_deprecated/query/discover.h"
#include "src/lib/inspect_deprecated/query/json_formatter.h"
#include "src/lib/inspect_deprecated/query/location.h"
#include "src/lib/inspect_deprecated/query/read.h"
#include "src/lib/inspect_deprecated/query/source.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

fit::promise<fuchsia::mem::Buffer> CollectInspectData(async_dispatcher_t* timeout_dispatcher,
                                                      zx::duration timeout,
                                                      async::Executor* collection_executor) {
  std::unique_ptr<Inspect> inspect =
      std::make_unique<Inspect>(timeout_dispatcher, collection_executor);

  // We must store the promise in a variable due to the fact that the order of evaluation of
  // function parameters is undefined.
  auto inspect_data = inspect->Collect(timeout);
  return ExtendArgsLifetimeBeyondPromise(/*promise=*/std::move(inspect_data),
                                         /*args=*/std::move(inspect));
}

Inspect::Inspect(async_dispatcher_t* timeout_dispatcher, async::Executor* collection_executor)
    : timeout_dispatcher_(timeout_dispatcher), collection_executor_(collection_executor) {}

fit::promise<fuchsia::mem::Buffer> Inspect::Collect(zx::duration timeout) {
  FXL_CHECK(!has_called_collect_) << "Collect() is not intended to be called twice";
  has_called_collect_ = true;

  // We use a fit::bridge to create a fit::promise that will be completed when the Inspect data
  // collection is done, returning the Inspect data.
  //
  // We use a shared_ptr to share the bridge between this, the dispatcher on which we post the
  // delayed task to timeout and the executor on which we run the Inspect data collection.
  collection_done_ = std::make_shared<fit::bridge<fuchsia::mem::Buffer>>();
  collection_done_lock_ = std::make_shared<std::mutex>();

  // fit::promise does not have the notion of a timeout. So we post a delayed task that will call
  // the completer after the timeout and return an error.
  if (const zx_status_t status = async::PostDelayedTask(
          timeout_dispatcher_,
          [collection_done = collection_done_, collection_done_lock = collection_done_lock_] {
            {  // We keep the lock_guard's scope to a minimum.
              std::lock_guard<std::mutex> lock(*collection_done_lock);
              if (!collection_done->completer) {
                return;
              }
              collection_done->completer.complete_error();
            }

            FX_LOGS(ERROR) << "Inspect data collection timed out";
          },
          timeout);
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to post delayed task";
    FX_LOGS(ERROR) << "Skipping Inspect data collection as it is not safe without a timeout";
    return fit::make_result_promise<fuchsia::mem::Buffer>(fit::error());
  }

  // The Inspect data collection has two steps:
  //   * synchronous discovery of all the Inspect entrypoints under the realm of the calling
  //     component.
  //   * asynchronous connection to each entrypoint and read of its Inspect data.
  collection_executor_->schedule_task(
      // We wrap the synchronous discovery in a fit::promise to guarantee that it is executed on the
      // executor and not on this thread.
      fit::make_promise(
          []() -> fit::result<std::vector<fit::promise<inspect_deprecated::Source, std::string>>> {
            auto locations = inspect_deprecated::SyncFindPaths("/hub");

            if (locations.empty()) {
              FX_LOGS(ERROR) << "Failed to find any Inspect locations";
              return fit::error();
            }

            std::vector<fit::promise<inspect_deprecated::Source, std::string>> sources;
            for (auto location : locations) {
              if (location.directory_path.find("system_objects") == std::string::npos) {
                sources.push_back(inspect_deprecated::ReadLocation(std::move(location)));
              }
            }

            if (sources.empty()) {
              FX_LOGS(ERROR) << "Failed to find any non-system-objects Inspect locations";
              return fit::error();
            }

            return fit::ok(std::move(sources));
          })
          .and_then(
              [](std::vector<fit::promise<inspect_deprecated::Source, std::string>>& sources) {
                return fit::join_promise_vector(std::move(sources));
              })
          .and_then([](std::vector<fit::result<inspect_deprecated::Source, std::string>>& sources)
                        -> fit::result<fuchsia::mem::Buffer> {
            std::vector<inspect_deprecated::Source> ok_sources;
            for (auto& source : sources) {
              if (source.is_ok()) {
                inspect_deprecated::Source ok_source = source.take_value();
                ok_source.SortHierarchy();
                ok_sources.push_back(std::move(ok_source));
              } else {
                FX_LOGS(ERROR) << "Failed to read one Inspect source: " << source.take_error();
              }
            }

            if (ok_sources.empty()) {
              FX_LOGS(WARNING) << "No valid Inspect sources found";
              return fit::error();
            }

            fsl::SizedVmo vmo;
            if (!fsl::VmoFromString(inspect_deprecated::JsonFormatter(
                                        inspect_deprecated::JsonFormatter::Options{},
                                        inspect_deprecated::Formatter::PathFormat::ABSOLUTE)
                                        .FormatSourcesRecursive(ok_sources),
                                    &vmo)) {
              FX_LOGS(ERROR) << "Failed to convert Inspect data JSON string to vmo";
              return fit::error();
            }
            return fit::ok(std::move(vmo).ToTransport());
          })
          .then([collection_done = collection_done_, collection_done_lock = collection_done_lock_](
                    fit::result<fuchsia::mem::Buffer>& result) {
            std::lock_guard<std::mutex> lock(*collection_done_lock);
            if (!collection_done->completer) {
              return;
            }

            if (result.is_error()) {
              collection_done->completer.complete_error();
            } else {
              collection_done->completer.complete_ok(result.take_value());
            }
          }));

  return collection_done_->consumer.promise_or(fit::error()).or_else([]() {
    FX_LOGS(ERROR) << "Failed to get Inspect data";
    return fit::error();
  });
}

}  // namespace feedback
