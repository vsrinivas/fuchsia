// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/replay.h"

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "src/lib/fidl_codec/proto_value.h"
#include "src/lib/fidl_codec/semantic.h"
#include "tools/fidlcat/lib/event.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"
#include "tools/fidlcat/proto/session.pb.h"

namespace fidlcat {

std::shared_ptr<InvokedEvent> CreateInvoked(SyscallDisplayDispatcher* dispatcher,
                                            uint64_t timestamp, uint64_t process_id,
                                            uint64_t thread_id, Syscall* syscall) {
  return std::make_shared<InvokedEvent>(
      timestamp, dispatcher->CreateThread("foo", process_id, thread_id, nullptr), syscall);
}

Syscall* ReplayBuffer::GetSyscall(SyscallDisplayDispatcher* dispatcher) const {
  switch (kind_) {
    case Kind::kRead:
      return dispatcher->SearchSyscall(etc_ ? "zx_channel_read_etc" : "zx_channel_read");
    case Kind::kWrite:
      return dispatcher->SearchSyscall(etc_ ? "zx_channel_write_etc" : "zx_channel_write");
    case Kind::kCall:
      return dispatcher->SearchSyscall(etc_ ? "zx_channel_call_etc" : "zx_channel_call");
  }
}

void ReplayBuffer::AddWriteBytes(std::istream& stream) {
  // Bytes are specified in hexadecimal (without any leading 0x).
  // Up to 32 bytes can be specified on a line.
  stream >> std::hex;
  size_t count = write_byte_count_ - write_bytes_.size();
  // The dump must have 32 bytes (unless there are less bytes remaining).
  if (count > 32) {
    count = 32;
  }
  while (count > 0) {
    uint32_t data;
    stream >> data;
    write_bytes_.emplace_back(data);
    --count;
  }
  stream >> std::dec;
}

void ReplayBuffer::AddWriteHandles(std::istream& stream) {
  // Handles are specified in hexadecimal (without any leading 0x).
  // Up to 8 handles can be specified on a line.
  stream >> std::hex;
  size_t count = write_handle_count_ - write_handles_.size();
  // The dump must have 8 handles (unless there are less handles remaining).
  if (count > 8) {
    count = 8;
  }
  zx_handle_disposition_t handle = {.operation = fidl_codec::kNoHandleDisposition,
                                    .handle = 0,
                                    .type = ZX_OBJ_TYPE_NONE,
                                    .rights = 0,
                                    .result = ZX_OK};
  while (count > 0) {
    stream >> handle.handle;
    write_handles_.emplace_back(handle);
    --count;
  }
  stream >> std::dec;
}

void ReplayBuffer::AddWriteEtcHandle(std::istream& stream) {
  // Only one handle disposition is specified per line. The fields are:
  // - operation (0 or 1).
  // - handle (in hexdecimal wiout any leading 0x).
  // - rights (in hexdecimal wiout any leading 0x).
  // - type (in decimal).
  zx_handle_disposition_t handle = {.operation = fidl_codec::kNoHandleDisposition,
                                    .handle = 0,
                                    .type = ZX_OBJ_TYPE_NONE,
                                    .rights = 0,
                                    .result = ZX_OK};
  stream >> handle.operation >> std::hex >> handle.handle >> handle.rights >> std::dec >>
      handle.type >> handle.result;
  write_handles_.emplace_back(handle);
}

void ReplayBuffer::AddReadBytes(std::istream& stream) {
  // Bytes are specified in hexadecimal (without any leading 0x).
  // Up to 32 bytes can be specified on a line.
  stream >> std::hex;
  size_t count = read_byte_count_ - read_bytes_.size();
  // The dump must have 32 bytes (unless there are less bytes remaining).
  if (count > 32) {
    count = 32;
  }
  while (count > 0) {
    uint32_t data;
    stream >> data;
    read_bytes_.emplace_back(data);
    --count;
  }
  stream >> std::dec;
}

void ReplayBuffer::AddReadHandles(std::istream& stream) {
  // Handles are specified in hexadecimal (without any leading 0x).
  // Up to 8 handles can be specified on a line.
  stream >> std::hex;
  size_t count = read_handle_count_ - read_handles_.size();
  // The dump must have 8 handles (unless there are less handles remaining).
  if (count > 8) {
    count = 8;
  }
  zx_handle_disposition_t handle = {.operation = fidl_codec::kNoHandleDisposition,
                                    .handle = 0,
                                    .type = ZX_OBJ_TYPE_NONE,
                                    .rights = 0,
                                    .result = ZX_OK};
  while (count > 0) {
    stream >> handle.handle;
    read_handles_.emplace_back(handle);
    --count;
  }
  stream >> std::dec;
}

void ReplayBuffer::Dispatch(SyscallDisplayDispatcher* dispatcher) {
  // Gets the definition of the syscall (from kind_ and etc_).
  Syscall* syscall = GetSyscall(dispatcher);

  // Creates the invoked event.
  std::shared_ptr<InvokedEvent> invoked_event =
      CreateInvoked(dispatcher, invoked_timestamp_, process_id_, thread_id_, syscall);

  // Sets the inline fields shared by all the channel syscalls.
  zx_handle_disposition_t handle;
  handle.operation = fidl_codec::kNoHandleDisposition;
  handle.handle = channel_;
  handle.rights = 0;
  handle.type = ZX_OBJ_TYPE_NONE;
  handle.result = ZX_OK;
  invoked_event->AddInlineField(syscall->SearchInlineMember("handle", /*invoked=*/true),
                                std::make_unique<fidl_codec::HandleValue>(handle));
  invoked_event->AddInlineField(
      syscall->SearchInlineMember("options", /*invoked=*/true),
      std::make_unique<fidl_codec::IntegerValue>(/*absolute_value=*/0, /*negative=*/false));

  if ((kind_ == Kind::kWrite) || kind_ == Kind::kCall) {
    // Decodes the outgoing message.
    fidl_codec::DecodedMessage message;
    std::stringstream error_stream;
    message.DecodeMessage(dispatcher->MessageDecoderDispatcher(), process_id_, channel_,
                          write_bytes_.data(), write_byte_count_, write_handles_.data(),
                          write_handle_count_,
                          (kind_ == Kind::kCall) ? fidl_codec::SyscallFidlType::kOutputRequest
                                                 : fidl_codec::SyscallFidlType::kOutputMessage,
                          error_stream);
    invoked_event->AddOutlineField(
        syscall->SearchOutlineMember("", /*invoked=*/true),
        std::make_unique<fidl_codec::FidlMessageValue>(&message, error_stream.str(),
                                                       write_bytes_.data(), write_byte_count_,
                                                       write_handles_.data(), write_handle_count_));
  }
  dispatcher->AddInvokedEvent(invoked_event);

  // Creates the output event.
  auto output_event = std::make_shared<OutputEvent>(output_timestamp_, invoked_event->thread(),
                                                    syscall, status_, invoked_event);

  if (((kind_ == Kind::kRead) || (kind_ == Kind::kCall)) && (status_ == ZX_OK)) {
    // Decodes the incoming message.
    fidl_codec::DecodedMessage message;
    std::stringstream error_stream;
    message.DecodeMessage(dispatcher->MessageDecoderDispatcher(), process_id_, channel_,
                          read_bytes_.data(), read_byte_count_, read_handles_.data(),
                          read_handle_count_,
                          (kind_ == Kind::kCall) ? fidl_codec::SyscallFidlType::kInputResponse
                                                 : fidl_codec::SyscallFidlType::kInputMessage,
                          error_stream);
    output_event->AddOutlineField(syscall->SearchOutlineMember("", /*invoked=*/false),
                                  std::make_unique<fidl_codec::FidlMessageValue>(
                                      &message, error_stream.str(), read_bytes_.data(),
                                      read_byte_count_, read_handles_.data(), read_handle_count_));
  }
  dispatcher->AddOutputEvent(std::move(output_event));
}

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

void Replay::DecodeTrace(std::istream& is) {
  // Decodes a trace stream, line per line, until the end of the stream.
  while (!is.eof()) {
    DecodeTraceLine(is);
  }
}

void Replay::DecodeTraceLine(std::istream& is) {
  // Decodes one trace line.
  std::string line;
  std::getline(is, line);
  auto position = line.find("syscall ");
  if (position == std::string::npos) {
    // If the line doesn't include the keyword syscall, it's a standard trace line. In that case
    // the line is output without modification (pass through).
    dispatcher()->os() << line << '\n';
  } else {
    std::stringstream stream(line);
    stream.seekg(position + 8, stream.cur);
    // Format for all decoded traces:
    // syscall |instance_id| |action| ...
    uintptr_t instance;
    std::string action;
    stream >> std::hex >> instance >> std::dec >> action;
    if (action == "process") {
      // Defines the name of a process. The format is:
      // syscall |instance_id| process |process_id| |process_name|
      // For example:
      // syscall 0x7ffd6863e9c0 process 2916209 FfxDoctor
      if (position > 0) {
        // Pass through any text before the keyword "syscall".
        dispatcher()->os() << line.substr(0, position) << '\n';
      }
      std::string process_name;
      uint64_t process_id;
      stream >> process_id >> process_name;
      Process* process = dispatcher()->SearchProcess(process_id);
      if (process == nullptr) {
        process = dispatcher()->CreateProcess(process_name, process_id, nullptr);
      }
      return;
    }
    if (action == "startup") {
      // Defines a startup handle. That is a handle which is available to the user code either
      // because the handle was given to the process (Fuchsia case) or because the handle has a
      // special handling (Linux and other OS case).
      // The format is (all fields on one line):
      // syscall |instance_id| startup |process_id| |thread_id| |handle_type|(|handle|)
      //   |type| |path|
      // For example:
      // syscall 0x7ffd68636f90 startup 2916209 2916210 Channel(1) dir /svc
      if (position > 0) {
        // Pass through any text before the keyword "syscall".
        dispatcher()->os() << line.substr(0, position) << '\n';
      }
      uint64_t process_id;
      uint64_t thread_id;
      stream >> process_id >> thread_id >> std::ws;
      char handle_type[100];
      stream.get(handle_type, 100, '(');
      stream.seekg(1, stream.cur);
      uint32_t handle;
      stream >> handle;
      stream.seekg(1, stream.cur);
      std::string type;
      std::string path;
      stream >> type >> path;
      Process* process = dispatcher()->SearchProcess(process_id);
      if (process != nullptr) {
        HandleInfo* handle_info = process->SearchHandleInfo(handle);
        if (handle_info != nullptr) {
          handle_info->set_startup();
        }
      }
      dispatcher()->inference().AddInferredHandleInfo(process_id, handle, type, path, "");
      return;
    }
    if (action == "channel_create") {
      // Defines a call to zx_channel_create. The format is (on one line):
      // syscall |instance_id| channel_create |timestamp| |process_id| |thread_id| |out0| |out1|
      //   |status|
      // The fields out0 and out1 are in hexadecimal without a leading 0x.
      // For example:
      // syscall 0x7ffd68637fd0 channel_create 1234 2916209 2916210 9 a 0
      if (position > 0) {
        // Pass through any text before the keyword "syscall".
        dispatcher()->os() << line.substr(0, position) << '\n';
      }
      uint64_t timestamp;
      uint64_t process_id;
      uint64_t thread_id;
      uint32_t out0;
      uint32_t out1;
      zx_status_t status;
      stream >> timestamp >> process_id >> thread_id >> std::hex >> out0 >> out1 >> std::dec >>
          status;
      Thread* thread = dispatcher()->CreateThread("foo", process_id, thread_id, nullptr);

      // Specifies that both handles are channels.
      dispatcher()
          ->CreateHandleInfo(thread, out0, 0, /*startup=*/false)
          ->set_object_type(ZX_OBJ_TYPE_CHANNEL);
      dispatcher()
          ->CreateHandleInfo(thread, out1, 0, /*startup=*/false)
          ->set_object_type(ZX_OBJ_TYPE_CHANNEL);

      // Specifies that the two channels are linked.
      dispatcher()->inference().AddLinkedHandles(process_id, out0, out1);
      dispatcher()->inference().AddLinkedHandles(process_id, out1, out0);

      // Creates and adds the invoked and the output events.
      Syscall* syscall = dispatcher()->SearchSyscall("zx_channel_create");
      std::shared_ptr<InvokedEvent> invoked_event =
          CreateInvoked(dispatcher(), timestamp, process_id, thread_id, syscall);
      dispatcher()->AddInvokedEvent(invoked_event);
      auto output_event = std::make_shared<OutputEvent>(timestamp, invoked_event->thread(), syscall,
                                                        status, invoked_event);
      zx_handle_disposition_t handle;
      handle.operation = fidl_codec::kNoHandleDisposition;
      handle.handle = out0;
      handle.rights = 0;
      handle.type = ZX_OBJ_TYPE_NONE;
      handle.result = ZX_OK;
      output_event->AddInlineField(syscall->SearchInlineMember("out0", /*invoked=*/false),
                                   std::make_unique<fidl_codec::HandleValue>(handle));
      handle.handle = out1;
      output_event->AddInlineField(syscall->SearchInlineMember("out1", /*invoked=*/false),
                                   std::make_unique<fidl_codec::HandleValue>(handle));
      dispatcher()->AddOutputEvent(std::move(output_event));
      return;
    }
    if ((action == "channel_call") || (action == "channel_call_etc")) {
      // Defines a zx_channel_call or a zx_channel_call_etc syscall. The format is (on one line):
      // syscall |instance_id| channel_call |timestamp| |process_id| |thread_id| |channel| |bytes|
      //   |handles|
      // The field channel is in hexdecimal without a leading 0x.
      // The field bytes specifies the number of bytes to be written.
      // The field handles specifies the number of handles to be written.
      // If bytes or handles are not zero, this line will be followed by one or several lines which
      // define the bytes and handles. Each of these lines will have the same instance_id.
      // For example:
      // syscall 0x5591382ba060 channel_call 1234 2916209 2916210 a 96 0
      if (position > 0) {
        // Pass through any text before the keyword "syscall".
        dispatcher()->os() << line.substr(0, position) << '\n';
      }
      uint64_t timestamp;
      uint64_t process_id;
      uint64_t thread_id;
      uint32_t channel;
      uint32_t write_byte_count;
      uint32_t write_handle_count;
      stream >> timestamp >> process_id >> thread_id >> std::hex >> channel >> std::dec >>
          write_byte_count >> write_handle_count;

      // Creates a ReplayBuffer used to keep the context while the bytes and handles are read.
      auto buffer = std::make_unique<ReplayBuffer>(timestamp, process_id, thread_id,
                                                   ReplayBuffer::Kind::kCall,
                                                   /*etc=*/action == "channel_call_etc", channel);
      buffer->SetWrite(write_byte_count, write_handle_count);
      buffers_[instance] = std::move(buffer);
      return;
    }
    if ((action == "channel_write") || (action == "channel_write_etc")) {
      // Defines a zx_channel_write or a zx_channel_write_etc syscall. The format is (on one line):
      // syscall |instance_id| channel_write |timestamp| |process_id| |thread_id| |channel| |bytes|
      //   |handles|
      // The field channel is in hexdecimal without a leading 0x.
      // The field bytes specifies the number of bytes to be written.
      // The field handles specifies the number of handles to be written.
      // If bytes or handles are not zero, this line will be followed by one or several lines which
      // define the bytes and handles. Each of these lines will have the same instance_id.
      // For example:
      // syscall 0x5591382ba060 channel_write_etc 1234 2916209 2916210 a 96 0
      if (position > 0) {
        // Pass through any text before the keyword "syscall".
        dispatcher()->os() << line.substr(0, position) << '\n';
      }
      uint64_t timestamp;
      uint64_t process_id;
      uint64_t thread_id;
      uint32_t channel;
      uint32_t write_byte_count;
      uint32_t write_handle_count;
      stream >> timestamp >> process_id >> thread_id >> std::hex >> channel >> std::dec >>
          write_byte_count >> write_handle_count;

      // Creates a ReplayBuffer used to keep the context while the bytes and handles are read.
      auto buffer = std::make_unique<ReplayBuffer>(timestamp, process_id, thread_id,
                                                   ReplayBuffer::Kind::kWrite,
                                                   /*etc=*/action == "channel_write_etc", channel);
      buffer->SetWrite(write_byte_count, write_handle_count);
      buffers_[instance] = std::move(buffer);
      return;
    }
    if ((action == "channel_read") || (action == "channel_read_etc")) {
      // Defines a zx_channel_read or a zx_channel_read_etc syscall. The format is (on one line):
      // syscall |instance_id| channel_read |timestamp| |process_id| |thread_id| |channel| |status|
      //   |bytes| |handles|
      // The field channel is in hexdecimal without a leading 0x.
      // The field bytes specifies the number of bytes to be written.
      // The field handles specifies the number of handles to be written.
      // If bytes or handles are not zero, this line will be followed by one or several lines which
      // define the bytes and handles. Each of these lines will have the same instance_id.
      // For example:
      // syscall 0x7ffd686381a0 channel_read 1234 2916209 2916209 9 0 96 0
      if (position > 0) {
        // Pass through any text before the keyword "syscall".
        dispatcher()->os() << line.substr(0, position) << '\n';
      }
      uint64_t timestamp;
      uint64_t process_id;
      uint64_t thread_id;
      uint32_t channel;
      zx_status_t status;
      uint32_t read_byte_count;
      uint32_t read_handle_count;
      stream >> timestamp >> process_id >> thread_id >> std::hex >> channel >> std::dec >> status >>
          read_byte_count >> read_handle_count;

      // Creates a ReplayBuffer used to keep the context while the bytes and handles are read.
      auto buffer = std::make_unique<ReplayBuffer>(timestamp, process_id, thread_id,
                                                   ReplayBuffer::Kind::kRead,
                                                   /*etc=*/action == "channel_read_etc", channel);
      buffer->SetRead(read_byte_count, read_handle_count);
      buffer->SetStatus(timestamp, status);
      if (buffer->DecodeOk()) {
        // Case for which there is no bytes or handles. This happends when the status is not ZX_OK.
        buffer->Dispatch(dispatcher());
      } else {
        buffers_[instance] = std::move(buffer);
      }
      return;
    }

    // The line is not a header line. Search for a pending buffer with the instance id.
    auto buffer = SearchBuffer(instance);
    if (buffer == nullptr) {
      // No buffer found. The line is passed through.
      dispatcher()->os() << line << '\n';
      return;
    }

    // Checks for possible actions on a buffer.
    if (action == "call_status") {
      // Defines the status for a zx_channel_call. The format is:
      // syscall |instance_id| call_status |timestamp| |status| |bytes| |channels|
      // For example:
      // syscall 0x559138249750 call_status 1234 0 48 0
      // If bytes or handles are not zero, this line will be followed by one or several lines which
      // define the bytes and handles. Each of these lines will have the same instance_id.
      uint64_t timestamp;
      zx_status_t status;
      uint32_t read_byte_count;
      uint32_t read_handle_count;
      stream >> timestamp >> status >> read_byte_count >> read_handle_count;
      buffer->SetStatus(timestamp, status);
      buffer->SetRead(read_byte_count, read_handle_count);
    } else if (action == "write_status") {
      // Defines the status for a zx_channel_write. The format is:
      // syscall |instance_id| write_status |timestamp| |status|
      // For example:
      // syscall 0x559138249750 write_status 1234 0
      uint64_t timestamp;
      zx_status_t status;
      stream >> timestamp >> status;
      buffer->SetStatus(timestamp, status);
    } else if (action == "write_bytes") {
      buffer->AddWriteBytes(stream);
    } else if (action == "write_handles") {
      buffer->AddWriteHandles(stream);
    } else if (action == "write_etc_handle") {
      buffer->AddWriteEtcHandle(stream);
    } else if (action == "read_bytes") {
      buffer->AddReadBytes(stream);
    } else if (action == "read_handles") {
      buffer->AddReadHandles(stream);
    } else {
      // No valid action found. The line is passed through.
      dispatcher()->os() << line << '\n';
      return;
    }

    if (position > 0) {
      // Pass through any text before the keyword "syscall".
      dispatcher()->os() << line.substr(0, position) << '\n';
    }

    // If the buffer is fully decoded, dispatches it and destroys it.
    if (buffer->DecodeOk()) {
      buffer->Dispatch(dispatcher());
      buffers_.erase(instance);
    }
  }
}

}  // namespace fidlcat
