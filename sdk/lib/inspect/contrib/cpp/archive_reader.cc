// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/bridge.h>
#include <lib/fit/optional.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <unistd.h>

#include <set>
#include <string>
#include <thread>

#include <rapidjson/pointer.h>
#include <src/lib/fsl/vmo/strings.h>
#include <src/lib/fxl/strings/join_strings.h>

namespace inspect {
namespace contrib {

constexpr char kPathName[] = "path";
constexpr char kContentsName[] = "contents";

namespace {

void InnerReadBatches(fuchsia::diagnostics::BatchIteratorPtr ptr,
                      fit::bridge<std::vector<DiagnosticsData>, std::string> done,
                      std::vector<DiagnosticsData> ret) {
  ptr->GetNext(
      [ptr = std::move(ptr), done = std::move(done), ret = std::move(ret)](auto result) mutable {
        if (result.is_err()) {
          done.completer.complete_error("Batch iterator returned error: " +
                                        std::to_string(static_cast<size_t>(result.err())));
          return;
        }

        if (result.response().batch.empty()) {
          done.completer.complete_ok(std::move(ret));
          return;
        }

        for (const auto& content : result.response().batch) {
          if (!content.is_json()) {
            done.completer.complete_error("Received an unexpected content format");
            return;
          }
          std::string json;
          if (!fsl::StringFromVmo(content.json(), &json)) {
            done.completer.complete_error("Failed to read returned VMO");
            return;
          }

          rapidjson::Document document;
          document.Parse(json);

          ret.emplace_back(DiagnosticsData(std::move(document)));
        }

        InnerReadBatches(std::move(ptr), std::move(done), std::move(ret));
      });
}

fit::promise<std::vector<DiagnosticsData>, std::string> ReadBatches(
    fuchsia::diagnostics::BatchIteratorPtr ptr) {
  fit::bridge<std::vector<DiagnosticsData>, std::string> result;
  auto consumer = std::move(result.consumer);
  InnerReadBatches(std::move(ptr), std::move(result), {});
  return consumer.promise_or(fit::error("Failed to obtain consumer promise"));
}

}  // namespace

DiagnosticsData::DiagnosticsData(rapidjson::Document document) : document_(std::move(document)) {
  if (document_.HasMember(kPathName) && document_[kPathName].IsString()) {
    std::string val = document_[kPathName].GetString();

    size_t idx = val.find_last_of("/");

    if (idx != std::string::npos) {
      name_ = val.substr(idx + 1);
    } else {
      name_ = std::move(val);
    }
  }
}

const std::string& DiagnosticsData::component_name() const { return name_; }

const rapidjson::Value& DiagnosticsData::content() const {
  static rapidjson::Value default_ret;

  if (!document_.IsObject() || !document_.HasMember(kContentsName)) {
    return default_ret;
  }

  return document_[kContentsName];
}

const rapidjson::Value& DiagnosticsData::GetByPath(const std::vector<std::string>& path) const {
  static rapidjson::Value default_ret;

  std::string pointer("/");
  pointer.append(fxl::JoinStrings(path, "/"));

  rapidjson::Pointer ptr(pointer.c_str());

  const rapidjson::Value* val = ptr.Get(content());
  if (val == nullptr) {
    return default_ret;
  } else {
    return *val;
  }
}

fit::promise<std::vector<DiagnosticsData>, std::string> ArchiveReader::GetInspectSnapshot() {
  return fit::make_promise([this] {
           std::vector<fuchsia::diagnostics::SelectorArgument> selector_args;
           for (const auto& selector : selectors_) {
             fuchsia::diagnostics::SelectorArgument arg;
             arg.set_raw_selector(selector);
             selector_args.emplace_back(std::move(arg));
           }

           fuchsia::diagnostics::StreamParameters params;
           params.set_data_type(fuchsia::diagnostics::DataType::INSPECT);
           params.set_stream_mode(fuchsia::diagnostics::StreamMode::SNAPSHOT);
           params.set_format(fuchsia::diagnostics::Format::JSON);

           if (!selector_args.empty()) {
             params.set_selectors(std::move(selector_args));
           }

           fuchsia::diagnostics::BatchIteratorPtr iterator;
           archive_->StreamDiagnostics(iterator.NewRequest(), std::move(params));
           return ReadBatches(std::move(iterator));
         })
      .wrap_with(scope_);
}

fit::promise<std::vector<DiagnosticsData>, std::string> ArchiveReader::SnapshotInspectUntilPresent(
    std::vector<std::string> component_names) {
  return fit::make_promise([this, component_names = std::move(component_names)] {
           return GetInspectSnapshot().and_then(
               [this,
                component_names = std::move(component_names)](std::vector<DiagnosticsData>& result)
                   -> fit::promise<std::vector<DiagnosticsData>, std::string> {
                 std::set<std::string> remaining(component_names.begin(), component_names.end());

                 for (const auto& val : result) {
                   remaining.erase(val.component_name());
                 }

                 if (remaining.empty()) {
                   return fit::make_result_promise<std::vector<DiagnosticsData>, std::string>(
                       fit::ok(std::move(result)));
                 } else {
                   // Use a separate thread to control the delay between snapshots.
                   fit::bridge<> bridge;
                   std::thread([completer = std::move(bridge.completer)]() mutable {
                     // Hardcoded sleep of 200ms for now.
                     usleep(200000);
                     completer.complete_ok();
                   }).detach();
                   return bridge.consumer.promise_or(fit::error())
                       .then([this,
                              component_names = std::move(component_names)](fit::result<>& unused) {
                         return SnapshotInspectUntilPresent(std::move(component_names));
                       });
                 }
               });
         })
      .wrap_with(scope_);
}

}  // namespace contrib
}  // namespace inspect
