// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/replay.h"

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "src/lib/fidl_codec/proto_value.h"
#include "src/lib/fidl_codec/semantic.h"
#include "tools/fidlcat/lib/event.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"
#include "tools/fidlcat/proto/session.pb.h"

namespace fidlcat {

bool Replay::DumpProto(const std::string& proto_file_name) {
  if (proto_file_name == "-") {
    return DumpProto(std::cin);
  }
  std::fstream input(proto_file_name, std::ios::in | std::ios::binary);
  if (input.fail()) {
    FX_LOGS(ERROR) << "Can't open <" << proto_file_name << "> for reading.";
    return false;
  }
  if (!DumpProto(input)) {
    FX_LOGS(ERROR) << "Failed to parse session from file <" << proto_file_name << ">.";
    return false;
  }
  return true;
}

bool Replay::DumpProto(std::istream& is) {
  proto::Session session;
  if (!session.ParseFromIstream(&is)) {
    return false;
  }
  std::cout << session.DebugString();
  return true;
}

bool Replay::ReplayProto(const std::string& proto_file_name) {
  if (proto_file_name == "-") {
    return ReplayProto("standard input", std::cin);
  }
  std::fstream input(proto_file_name, std::ios::in | std::ios::binary);
  if (input.fail()) {
    FX_LOGS(ERROR) << "Can't open <" << proto_file_name << "> for reading.";
    return false;
  }
  return ReplayProto("file <" + proto_file_name + ">", input);
}

bool Replay::ReplayProto(const std::string& file_name, std::istream& is) {
  proto::Session session;
  if (!session.ParseFromIstream(&is)) {
    FX_LOGS(ERROR) << "Failed to parse session from " << file_name << ".";
    return false;
  }
  bool ok = true;
  for (int index = 0; index < session.process_size(); ++index) {
    const proto::Process& process = session.process(index);
    if (dispatcher()->SearchProcess(process.koid()) != nullptr) {
      FX_LOGS(INFO) << "Error reading protobuf " << file_name << ": process " << process.name()
                    << " koid=" << process.koid() << " defined multiple times.";
      ok = false;
    } else {
      dispatcher()->CreateProcess(process.name(), process.koid(), nullptr);
      for (int handle_index = 0; handle_index < process.linked_handles_size(); ++handle_index) {
        const proto::LinkedHandles& linked_handles = process.linked_handles(handle_index);
        dispatcher()->inference().AddLinkedHandles(process.koid(), linked_handles.handle_0(),
                                                   linked_handles.handle_1());
      }
    }
  }
  for (int index = 0; index < session.thread_size(); ++index) {
    const proto::Thread& thread = session.thread(index);
    if (dispatcher()->SearchThread(thread.koid()) != nullptr) {
      FX_LOGS(INFO) << "Error reading protobuf " << file_name << ": thread " << thread.koid()
                    << " defined multiple times.";
      ok = false;
    } else {
      Process* process = dispatcher()->SearchProcess(thread.process_koid());
      if (process == nullptr) {
        FX_LOGS(ERROR) << "Error reading protobuf " << file_name << ": process "
                       << thread.process_koid() << " not found for thread " << thread.koid() << '.';
        ok = false;
      }
      dispatcher()->CreateThread(thread.koid(), process);
    }
  }
  for (int index = 0; index < session.handle_description_size(); ++index) {
    const proto::HandleDescription& proto_handle_description = session.handle_description(index);
    Thread* thread = dispatcher()->SearchThread(proto_handle_description.thread_koid());
    if (thread == nullptr) {
      FX_LOGS(ERROR) << "Error reading protobuf file " << file_name << ": thread "
                     << proto_handle_description.thread_koid() << " not found for handle.";
      ok = false;
    } else {
      HandleInfo* handle_info = dispatcher()->CreateHandleInfo(
          thread, proto_handle_description.handle(), proto_handle_description.creation_time(),
          proto_handle_description.startup());
      handle_info->set_object_type(proto_handle_description.object_type());
      handle_info->set_koid(proto_handle_description.koid());
      dispatcher()->inference().AddKoidHandleInfo(proto_handle_description.koid(), handle_info);
    }
    auto inferred_handle_info = std::make_unique<fidl_codec::semantic::InferredHandleInfo>(
        proto_handle_description.type(), proto_handle_description.fd(),
        proto_handle_description.path(), proto_handle_description.attributes());
    dispatcher()->inference().AddInferredHandleInfo(thread->process()->koid(),
                                                    proto_handle_description.handle(),
                                                    std::move(inferred_handle_info));
  }
  for (int index = 0; index < session.linked_koids_size(); ++index) {
    const proto::LinkedKoids& linked_koids = session.linked_koids(index);
    dispatcher()->inference().AddLinkedKoids(linked_koids.koid_0(), linked_koids.koid_1());
  }
  for (int index = 0; index < session.event_size(); ++index) {
    const proto::Event& proto_event = session.event(index);
    if (!DecodeAndDispatchEvent(proto_event)) {
      ok = false;
    }
  }
  return ok;
}

}  // namespace fidlcat
