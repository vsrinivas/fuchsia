// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dockyard_proxy_grpc.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>
#include <zircon/time.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "build_info.h"
#include "dockyard_proxy.h"
#include "sample_bundle.h"
#include "src/developer/system_monitor/lib/proto/dockyard.grpc.pb.h"

namespace harvester {

namespace {

constexpr DockyardProxyStatus ToDockyardProxyStatus(
    const grpc::Status& status) {
  return status.ok() ? DockyardProxyStatus::OK : DockyardProxyStatus::ERROR;
}

}  // namespace

namespace internal {

void ExtractPathsFromSampleList(
    std::vector<const std::string*>* dockyard_strings, const SampleList& list) {
  for (size_t i = 0; i < list.size(); ++i) {
    dockyard_strings->at(i) = &list[i].first;
  }
}

void BuildSampleListById(SampleListById* by_id,
                         const std::vector<dockyard::DockyardId>& id_list,
                         const SampleList& sample_list) {
  for (size_t i = 0; i < id_list.size(); ++i) {
    dockyard::DockyardId id = id_list[i];
    uint64_t sample = sample_list[i].second;
    by_id->at(i) = {id, sample};
  }
}

dockyard_proto::LogBatch BuildLogBatch(
    const std::vector<const std::string>& batch, uint64_t monotonic_time,
    std::optional<zx_time_t> time) {
  dockyard_proto::LogBatch logs;
  for (const auto& json : batch) {
    auto log = logs.add_log_json();
    log->set_json(json);
  }
  if (time.has_value()) {
    logs.set_time(time.value());
  }
  logs.set_monotonic_time(monotonic_time);
  return logs;
}

}  // namespace internal

DockyardProxyStatus DockyardProxyGrpc::Init() {
  clock_->WaitForStart([this](zx_status_t status) {
    if (status == ZX_OK) {
      SendUtcClockStarted();
    } else {
      FX_LOGS(ERROR) << "Waiting for clock failed with status " << status;
    }
  });

  dockyard_proto::InitRequest request;
  request.set_device_name("TODO SET DEVICE NAME");
  request.set_version(dockyard::DOCKYARD_VERSION);

  std::optional<zx_time_t> nanoseconds = clock_->nanoseconds();
  if (nanoseconds.has_value()) {
    request.set_device_time_ns(nanoseconds.value());
  }

  BuildInfoValue version = GetFuchsiaBuildVersion();
  if (version.HasValue()) {
    request.set_fuchsia_version(version.Value());
  } else {
    request.set_fuchsia_version("UNKNOWN");
  }

  dockyard_proto::InitReply reply;

  grpc::ClientContext context;
  grpc::Status status = stub_->Init(&context, request, &reply);
  if (!status.ok()) {
    FX_LOGS(ERROR) << status.error_code() << ": " << status.error_message();
    FX_LOGS(ERROR) << "Unable to send to dockyard.";
    return DockyardProxyStatus::ERROR;
  }
  return DockyardProxyStatus::OK;
}

DockyardProxyStatus DockyardProxyGrpc::SendLogs(
    const std::vector<const std::string>& batch) {
  uint64_t monotonic_time = zx::clock::get_monotonic().get();
  std::optional<zx_time_t> nanoseconds = clock_->nanoseconds();

  // Data we are sending to the server.
  dockyard_proto::LogBatch logs =
      internal::BuildLogBatch(batch, monotonic_time, nanoseconds);

  grpc::ClientContext context;
  std::shared_ptr<grpc::ClientReaderWriterInterface<
      dockyard_proto::LogBatch, dockyard_proto::EmptyMessage>>
      stream(stub_->SendLogs(&context));

  stream->Write(logs);
  stream->WritesDone();
  return ToDockyardProxyStatus(stream->Finish());
}

DockyardProxyStatus DockyardProxyGrpc::SendInspectJson(
    const std::string& dockyard_path, const std::string& json) {
  std::optional<zx_time_t> nanoseconds = clock_->nanoseconds();
  dockyard::DockyardId dockyard_id;
  grpc::Status status = GetDockyardIdForPath(&dockyard_id, dockyard_path);
  if (status.ok()) {
    return ToDockyardProxyStatus(
        SendInspectJsonById(nanoseconds, dockyard_id, json));
  }
  return ToDockyardProxyStatus(status);
}

DockyardProxyStatus DockyardProxyGrpc::SendSample(
    const std::string& dockyard_path, uint64_t value) {
  std::optional<zx_time_t> nanoseconds = clock_->nanoseconds();
  dockyard::DockyardId dockyard_id;
  grpc::Status status = GetDockyardIdForPath(&dockyard_id, dockyard_path);
  if (status.ok()) {
    return ToDockyardProxyStatus(
        SendSampleById(nanoseconds, dockyard_id, value));
  }
  return ToDockyardProxyStatus(status);
}

DockyardProxyStatus DockyardProxyGrpc::SendSampleList(const SampleList& list) {
  std::optional<zx_time_t> nanoseconds = clock_->nanoseconds();
  std::vector<const std::string*> dockyard_strings(list.size());
  internal::ExtractPathsFromSampleList(&dockyard_strings, list);

  std::vector<dockyard::DockyardId> dockyard_ids;
  GetDockyardIdsForPaths(&dockyard_ids, dockyard_strings);

  SampleListById by_id(list.size());
  internal::BuildSampleListById(&by_id, dockyard_ids, list);
  return ToDockyardProxyStatus(SendSampleListById(nanoseconds, by_id));
}

DockyardProxyStatus DockyardProxyGrpc::SendStringSampleList(
    const StringSampleList& list) {
  std::optional<zx_time_t> nanoseconds = clock_->nanoseconds();
  std::vector<const std::string*> dockyard_strings;
  for (const auto& element : list) {
    // Both the key (first) and value (second) are strings. Get IDs for each.
    dockyard_strings.emplace_back(&element.first);
    dockyard_strings.emplace_back(&element.second);
  }
  // Get an ID for each string (path or otherwise). The ID will then be used to
  // in place of the strings.
  std::vector<dockyard::DockyardId> dockyard_ids;
  GetDockyardIdsForPaths(&dockyard_ids, dockyard_strings);
  SampleListById by_id;
  auto ids_iter = dockyard_ids.begin();
  for (size_t i = 0; i < list.size(); ++i) {
    dockyard::DockyardId path_id = *ids_iter++;
    dockyard::DockyardId value_id = *ids_iter++;
    by_id.emplace_back(path_id, value_id);
  }
  return ToDockyardProxyStatus(SendSampleListById(nanoseconds, by_id));
}

DockyardProxyStatus DockyardProxyGrpc::SendSamples(
    const SampleList& int_samples, const StringSampleList& string_samples) {
  std::optional<zx_time_t> nanoseconds = clock_->nanoseconds();
  std::vector<const std::string*> dockyard_strings;
  for (const auto& element : int_samples) {
    dockyard_strings.emplace_back(&element.first);
  }
  for (const auto& element : string_samples) {
    // Both the key (first) and value (second) are strings. Get IDs for each.
    dockyard_strings.emplace_back(&element.first);
    dockyard_strings.emplace_back(&element.second);
  }

  // Get an ID for each string (path or otherwise). The ID will then be used to
  // in place of the strings.
  std::vector<dockyard::DockyardId> dockyard_ids;
  GetDockyardIdsForPaths(&dockyard_ids, dockyard_strings);
  SampleListById by_id;
  auto ids_iter = dockyard_ids.begin();
  for (const auto& sample : int_samples) {
    dockyard::DockyardId path_id = *ids_iter++;
    by_id.emplace_back(path_id, sample.second);
  }
  for (size_t i = 0; i < string_samples.size(); ++i) {
    dockyard::DockyardId path_id = *ids_iter++;
    dockyard::DockyardId value_id = *ids_iter++;
    by_id.emplace_back(path_id, value_id);
  }
  return ToDockyardProxyStatus(SendSampleListById(nanoseconds, by_id));
}

grpc::Status DockyardProxyGrpc::SendUtcClockStarted() {
  dockyard_proto::UtcClockStartedRequest request;
  std::optional<zx_time_t> nanoseconds = clock_->nanoseconds();
  if (nanoseconds.has_value()) {
    request.set_device_time_ns(nanoseconds.value());
  } else {
    FX_LOGS(ERROR) << "We got a clock started message but the time is still "
                      "not available.";
  }

  dockyard_proto::UtcClockStartedReply reply;
  grpc::ClientContext context;
  grpc::Status status = stub_->UtcClockStarted(&context, request, &reply);
  if (!status.ok()) {
    FX_LOGS(ERROR) << status.error_code() << ": " << status.error_message();
    FX_LOGS(ERROR) << "Unable to send UtcClockStarted to dockyard.";
  }
  return status;
}

grpc::Status DockyardProxyGrpc::SendInspectJsonById(
    std::optional<zx_time_t> time, dockyard::DockyardId dockyard_id,
    const std::string& json) {
  // Data we are sending to the server.
  dockyard_proto::InspectJson inspect;
  if (time.has_value()) {
    inspect.set_time(time.value());
  }
  inspect.set_dockyard_id(dockyard_id);
  inspect.set_json(json);

  grpc::ClientContext context;
  std::shared_ptr<grpc::ClientReaderWriterInterface<
      dockyard_proto::InspectJson, dockyard_proto::EmptyMessage>>
      stream(stub_->SendInspectJson(&context));

  stream->Write(inspect);
  stream->WritesDone();
  return stream->Finish();
}

grpc::Status DockyardProxyGrpc::SendSampleById(std::optional<zx_time_t> time,
                                               dockyard::DockyardId dockyard_id,
                                               uint64_t value) {
  // Data we are sending to the server.
  dockyard_proto::RawSample sample;
  if (time.has_value()) {
    sample.set_time(time.value());
  }
  sample.mutable_sample()->set_key(dockyard_id);
  sample.mutable_sample()->set_value(value);

  grpc::ClientContext context;
  std::shared_ptr<grpc::ClientReaderWriterInterface<
      dockyard_proto::RawSample, dockyard_proto::EmptyMessage>>
      stream(stub_->SendSample(&context));

  stream->Write(sample);
  stream->WritesDone();
  return stream->Finish();
}

grpc::Status DockyardProxyGrpc::SendSampleListById(
    std::optional<zx_time_t> time, const SampleListById& list) {
  // Data we are sending to the server.
  dockyard_proto::RawSamples samples;
  if (time.has_value()) {
    samples.set_time(time.value());
  }
  for (const auto& iter : list) {
    auto sample = samples.add_sample();
    sample->set_key(iter.first);
    sample->set_value(iter.second);
  }

  grpc::ClientContext context;
  std::shared_ptr<grpc::ClientReaderWriterInterface<
      dockyard_proto::RawSamples, dockyard_proto::EmptyMessage>>
      stream(stub_->SendSamples(&context));

  stream->Write(samples);
  stream->WritesDone();
  return stream->Finish();
}

grpc::Status DockyardProxyGrpc::GetDockyardIdForPath(
    dockyard::DockyardId* dockyard_id, const std::string& dockyard_path) {
  std::vector<dockyard::DockyardId> dockyard_ids;
  std::vector<const std::string*> dockyard_paths = {&dockyard_path};
  grpc::Status status = GetDockyardIdsForPaths(&dockyard_ids, dockyard_paths);
  if (status.ok()) {
    *dockyard_id = dockyard_ids[0];
  }
  return status;
}

grpc::Status DockyardProxyGrpc::GetDockyardIdsForPaths(
    std::vector<dockyard::DockyardId>* dockyard_ids,
    const std::vector<const std::string*>& dockyard_paths) {
  dockyard_proto::DockyardPaths need_ids;

  std::vector<size_t> indexes;
  {
    std::lock_guard<std::mutex> lock(dockyard_path_to_id_mutex_);
    for (const auto& dockyard_path : dockyard_paths) {
      auto iter = dockyard_path_to_id_.find(*dockyard_path);
      if (iter != dockyard_path_to_id_.end()) {
        dockyard_ids->emplace_back(iter->second);
      } else {
        need_ids.add_path(*dockyard_path);
        indexes.emplace_back(dockyard_ids->size());
        dockyard_ids->emplace_back(-1);
      }
    }
  }

  if (indexes.empty()) {
    // All strings had cached IDs.
    return grpc::Status::OK;
  }
  // Missing some IDs, request them from the Dockyard.

  // Container for the data we expect from the server.
  dockyard_proto::DockyardIds reply;

  grpc::ClientContext context;
  grpc::Status status =
      stub_->GetDockyardIdsForPaths(&context, need_ids, &reply);
  if (status.ok()) {
    std::lock_guard<std::mutex> lock(dockyard_path_to_id_mutex_);
    size_t reply_index = 0;
    for (size_t id_index : indexes) {
      dockyard::DockyardId dockyard_id = reply.id(reply_index);
      ++reply_index;
      (*dockyard_ids)[id_index] = dockyard_id;
      // Memoize it.
      dockyard_path_to_id_.emplace(*dockyard_paths[id_index], dockyard_id);
    }
  }
  return status;
}

}  // namespace harvester
