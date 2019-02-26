// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
    Hey, so there's not a whole lot here. That's because this is a work in
    progress. Starting from something like a hello world program, this will
    progress into a System Monitor for Fuchsia.

    The code below is largely straight out of the grpc hello world example.

    See also: ./README.md
*/

#include "harvester.h"

#include <fcntl.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <fuchsia/memory/cpp/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <zircon/status.h>

#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"

namespace harvester {

namespace {

zx_status_t get_root_resource(zx_handle_t* root_resource) {
  const char* sysinfo = "/dev/misc/sysinfo";
  int fd = open(sysinfo, O_RDWR);
  if (fd < 0) {
    FXL_LOG(ERROR) << "Cannot open sysinfo: " << strerror(errno);
    return ZX_ERR_NOT_FOUND;
  }

  zx::channel channel;
  zx_status_t status =
      fdio_get_service_handle(fd, channel.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot obtain sysinfo channel: "
                   << zx_status_get_string(status);
    return status;
  }

  zx_status_t fidl_status = fuchsia_sysinfo_DeviceGetRootResource(
      channel.get(), &status, root_resource);
  if (fidl_status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot obtain root resource: "
                   << zx_status_get_string(fidl_status);
    return fidl_status;
  } else if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Cannot obtain root resource: "
                   << zx_status_get_string(status);
    return status;
  }
  return ZX_OK;
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
  for (size_t i = 0; i < actual; ++i) {
    {
      // "cpu:0:busy_time"
      std::ostringstream label;
      label << "cpu:" << i << ":busy_time";
      uint64_t busy_time =
          cpu_time > stats[i].idle_time ? cpu_time - stats[i].idle_time : 0ull;
      grpc::Status status = harvester->SendSample(label.str(), busy_time);
    }
  }
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

int main(int argc, char** argv) {
  // A broad 'something went wrong' error.
  constexpr int EXIT_CODE_GENERAL_ERROR = 1;

  FXL_LOG(INFO) << "System Monitor Harvester - wip 8";
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    exit(EXIT_CODE_GENERAL_ERROR);
  }
  const auto& positional_args = command_line.positional_args();
  if (positional_args.size() < 1) {
    // TODO(dschuyler): Adhere to CLI tool requirements for --help.
    std::cerr << "Please specify an IP:Port, such as localhost:50051"
              << std::endl;
    exit(EXIT_CODE_GENERAL_ERROR);
  }
  // TODO(dschuyler): This channel isn't authenticated
  // (InsecureChannelCredentials()).
  harvester::Harvester harvester(grpc::CreateChannel(
      positional_args[0], grpc::InsecureChannelCredentials()));

  if (!harvester.Init()) {
    exit(EXIT_CODE_GENERAL_ERROR);
  }

  zx_handle_t root_resource;
  zx_status_t ret = harvester::get_root_resource(&root_resource);
  if (ret != ZX_OK) {
    exit(EXIT_CODE_GENERAL_ERROR);
  }

  while (true) {
    GatherCpuSamples(root_resource, &harvester);
    GatherMemorySamples(root_resource, &harvester);
    GatherThreadSamples(root_resource, &harvester);
    // TODO(dschuyler): Make delay configurable (100 msec is arbitrary).
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  FXL_LOG(INFO) << "System Monitor Harvester - exiting";
  return 0;
}
