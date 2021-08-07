// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_INTERFACE_H_
#define TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_INTERFACE_H_

#include <cinttypes>
#include <cstddef>

#include "src/developer/debug/shared/arch.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/lib/fidl_codec/wire_types.h"
#include "tools/fidlcat/lib/event.h"

namespace fidlcat {

// Stage for argument retrieving.
enum class Stage {
  // Retrieve arguments at the syscall entry.
  kEntry,
  // Retrieve arguments at the syscall exit.
  kExit
};

class SyscallDecoderInterface {
 public:
  SyscallDecoderInterface() = delete;
  virtual ~SyscallDecoderInterface() = default;

  SyscallDecoderDispatcher* dispatcher() const { return dispatcher_; }
  debug::Arch arch() const { return arch_; }
  fidlcat::Thread* fidlcat_thread() const { return fidlcat_thread_; }
  const fidl_codec::semantic::MethodSemantic* semantic() const { return semantic_; }
  void set_semantic(const fidl_codec::semantic::MethodSemantic* semantic) { semantic_ = semantic; }
  const fidl_codec::StructValue* decoded_request() const { return decoded_request_; }
  void set_decoded_request(const fidl_codec::StructValue* decoded_request) {
    decoded_request_ = decoded_request;
  }
  const fidl_codec::StructValue* decoded_response() const { return decoded_response_; }
  void set_decoded_response(const fidl_codec::StructValue* decoded_response) {
    decoded_response_ = decoded_response;
  }

  // Loads the value for a buffer, a struct or an output argument.
  virtual void LoadArgument(Stage stage, int argument_index, size_t size) = 0;
  // True if the argument is loaded correctly.
  virtual bool ArgumentLoaded(Stage stage, int argument_index, size_t size) const = 0;
  // Returns the value of an argument for basic types.
  virtual uint64_t ArgumentValue(int argument_index) const = 0;
  // Returns a pointer on the argument content for buffers, structs or
  // output arguments.
  virtual uint8_t* ArgumentContent(Stage stage, int argument_index) = 0;
  // Loads a buffer.
  virtual void LoadBuffer(Stage stage, uint64_t address, size_t size) = 0;
  // True if the buffer is loaded correctly.
  virtual bool BufferLoaded(Stage stage, uint64_t address, size_t size) = 0;
  // Returns a pointer on the loaded buffer.
  virtual uint8_t* BufferContent(Stage stage, uint64_t address) = 0;

 protected:
  SyscallDecoderInterface(SyscallDecoderDispatcher* dispatcher, zxdb::Thread* thread);

  SyscallDecoderDispatcher* const dispatcher_;
  const debug::Arch arch_;
  fidlcat::Thread* fidlcat_thread_;
  const fidl_codec::semantic::MethodSemantic* semantic_ = nullptr;
  const fidl_codec::StructValue* decoded_request_ = nullptr;
  const fidl_codec::StructValue* decoded_response_ = nullptr;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_INTERFACE_H_
