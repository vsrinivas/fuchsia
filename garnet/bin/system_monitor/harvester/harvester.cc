// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harvester.h"

#include <memory>
#include <string>

#include <fuchsia/memory/cpp/fidl.h>
#include <zircon/status.h>

#include "lib/fxl/logging.h"

namespace harvester {

namespace {

// Utility function to label and append a cpu sample to the |list|. |cpu| is the
// index returned from the kernel. |name| is the kind of sample, e.g.
// "interrupt_count".
void AddCpuValue(SampleList* list, size_t cpu, const std::string name,
                 dockyard::SampleValue value) {
  std::ostringstream label;
  label << "cpu:" << cpu << ":" << name;
  list->emplace_back(label.str(), value);
}

}  // namespace

bool Harvester::Init() {
  dockyard_proto::InitRequest request;
  request.set_name("TODO SET DEVICE NAME");
  request.set_version(dockyard::DOCKYARD_VERSION);
  dockyard_proto::InitReply reply;

  grpc::ClientContext context;
  grpc::Status status = stub_->Init(&context, request, &reply);
  if (status.ok()) {
    return true;
  }
  FXL_LOG(ERROR) << status.error_code() << ": " << status.error_message();
  FXL_LOG(ERROR) << "Unable to send to dockyard.";
  return false;
}

grpc::Status Harvester::SendSample(const std::string& stream_name,
                                   uint64_t value) {
  // TODO(dschuyler): system_clock might be at usec resolution. Consider using
  // high_resolution_clock.
  auto now = std::chrono::system_clock::now();
  uint64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             now.time_since_epoch())
                             .count();
  dockyard::SampleStreamId stream_id;
  grpc::Status status = GetStreamIdForName(&stream_id, stream_name);
  if (status.ok()) {
    return SendSampleById(nanoseconds, stream_id, value);
  }
  return status;
}

grpc::Status Harvester::SendSampleList(const SampleList list) {
  // TODO(dschuyler): system_clock might be at usec resolution. Consider using
  // high_resolution_clock.
  auto now = std::chrono::system_clock::now();
  uint64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             now.time_since_epoch())
                             .count();
  SampleListById by_id(list.size());
  auto name_iter = list.begin();
  auto id_iter = by_id.begin();
  for (; name_iter != list.end(); ++name_iter, ++id_iter) {
    dockyard::SampleStreamId stream_id;
    grpc::Status status = GetStreamIdForName(&stream_id, name_iter->first);
    if (!status.ok()) {
      return status;
    }
    id_iter->first = stream_id;
    id_iter->second = name_iter->second;
  }
  return SendSampleListById(nanoseconds, by_id);
}

grpc::Status Harvester::SendSampleById(uint64_t time,
                                       dockyard::SampleStreamId stream_id,
                                       uint64_t value) {
  // Data we are sending to the server.
  dockyard_proto::RawSample sample;
  sample.set_time(time);
  sample.mutable_sample()->set_key(stream_id);
  sample.mutable_sample()->set_value(value);

  grpc::ClientContext context;
  std::shared_ptr<grpc::ClientReaderWriter<dockyard_proto::RawSample,
                                           dockyard_proto::EmptyMessage>>
      stream(stub_->SendSample(&context));

  stream->Write(sample);
  stream->WritesDone();
  return stream->Finish();
}

grpc::Status Harvester::SendSampleListById(uint64_t time,
                                           const SampleListById list) {
  // Data we are sending to the server.
  dockyard_proto::RawSamples samples;
  samples.set_time(time);
  for (auto iter = list.begin(); iter != list.end(); ++iter) {
    auto sample = samples.add_sample();
    sample->set_key(iter->first);
    sample->set_value(iter->second);
  }

  grpc::ClientContext context;
  std::shared_ptr<grpc::ClientReaderWriter<dockyard_proto::RawSamples,
                                           dockyard_proto::EmptyMessage>>
      stream(stub_->SendSamples(&context));

  stream->Write(samples);
  stream->WritesDone();
  return stream->Finish();
}

grpc::Status Harvester::GetStreamIdForName(dockyard::SampleStreamId* stream_id,
                                           const std::string& stream_name) {
  auto iter = stream_ids_.find(stream_name);
  if (iter != stream_ids_.end()) {
    *stream_id = iter->second;
    return grpc::Status::OK;
  }

  dockyard_proto::StreamNameMessage name;
  name.set_name(stream_name);

  // Container for the data we expect from the server.
  dockyard_proto::StreamIdMessage reply;

  grpc::ClientContext context;
  grpc::Status status = stub_->GetStreamIdForName(&context, name, &reply);
  if (status.ok()) {
    *stream_id = reply.id();
    // Memoize it.
    stream_ids_.emplace(stream_name, *stream_id);
  }
  return status;
}

void GatherCpuSamples(zx_handle_t root_resource, Harvester* harvester) {
  // TODO(dschuyler): Determine the array size at runtime (32 is arbitrary).
  zx_info_cpu_stats_t stats[32];
  size_t actual, avail;
  zx_status_t err = zx_object_get_info(root_resource, ZX_INFO_CPU_STATS, &stats,
                                       sizeof(stats), &actual, &avail);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "ZX_INFO_CPU_STATS returned " << err << "("
                   << zx_status_get_string(err) << ")";
    return;
  }
  auto now = std::chrono::high_resolution_clock::now();
  auto cpu_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      now.time_since_epoch())
                      .count();
  SampleList list;
  for (size_t i = 0; i < actual; ++i) {
    // Note: stats[i].flags are not currently recorded.

    // Kernel scheduler counters.
    AddCpuValue(&list, i, "reschedules", stats[i].reschedules);
    AddCpuValue(&list, i, "context_switches", stats[i].context_switches);
    AddCpuValue(&list, i, "irq_preempts", stats[i].irq_preempts);
    AddCpuValue(&list, i, "preempts", stats[i].preempts);
    AddCpuValue(&list, i, "yields", stats[i].yields);

    // CPU level interrupts and exceptions.
    uint64_t busy_time =
        cpu_time > stats[i].idle_time ? cpu_time - stats[i].idle_time : 0ull;
    AddCpuValue(&list, i, "busy_time", busy_time);
    AddCpuValue(&list, i, "idle_time", stats[i].idle_time);
    AddCpuValue(&list, i, "hardware_interrupts", stats[i].ints);
    AddCpuValue(&list, i, "timer_interrupts", stats[i].timer_ints);
    AddCpuValue(&list, i, "timer_callbacks", stats[i].timers);
    AddCpuValue(&list, i, "syscalls", stats[i].syscalls);

    // Inter-processor interrupts.
    AddCpuValue(&list, i, "reschedule_ipis", stats[i].reschedule_ipis);
    AddCpuValue(&list, i, "generic_ipis", stats[i].generic_ipis);
  }
  grpc::Status status = harvester->SendSampleList(list);
}

void GatherMemorySamples(zx_handle_t root_resource, Harvester* harvester) {
  zx_info_kmem_stats_t stats;
  zx_status_t err = zx_object_get_info(root_resource, ZX_INFO_KMEM_STATS,
                                       &stats, sizeof(stats), NULL, NULL);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "ZX_INFO_KMEM_STATS error " << zx_status_get_string(err);
    return;
  }

  FXL_LOG(INFO) << "free memory total " << stats.free_bytes << ", heap "
                << stats.free_heap_bytes << ", vmo " << stats.vmo_bytes
                << ", mmu " << stats.mmu_overhead_bytes << ", ipc "
                << stats.ipc_bytes;

  const std::string FREE_BYTES = "memory:free_bytes";
  const std::string FREE_HEAP_BYTES = "memory:free_heap_bytes";
  const std::string VMO_BYTES = "memory:vmo_bytes";
  const std::string MMU_OVERHEAD_BYTES = "memory:mmu_overhead_by";
  const std::string IPC_BYTES = "memory:ipc_bytes";

  SampleList list;
  list.push_back(std::make_pair(FREE_BYTES, stats.free_bytes));
  list.push_back(std::make_pair(MMU_OVERHEAD_BYTES, stats.mmu_overhead_bytes));
  list.push_back(std::make_pair(FREE_HEAP_BYTES, stats.free_heap_bytes));
  list.push_back(std::make_pair(VMO_BYTES, stats.vmo_bytes));
  list.push_back(std::make_pair(IPC_BYTES, stats.ipc_bytes));
  grpc::Status status = harvester->SendSampleList(list);
  if (!status.ok()) {
    FXL_LOG(ERROR) << "SendSampleList failed " << status.error_code() << ": "
                   << status.error_message();
  }
}

void GatherThreadSamples(zx_handle_t root_resource, Harvester* harvester) {
  // TODO(dschuyler): Actually gather the thread samples.
}

}  // namespace harvester
