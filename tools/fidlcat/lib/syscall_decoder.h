// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_H_
#define TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_H_

#include <zircon/system/public/zircon/types.h>

#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/developer/debug/zxdb/client/register.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"
#include "tools/fidlcat/lib/type_decoder.h"

namespace fidlcat {

class InterceptingThreadObserver;
class Syscall;
class SyscallDecoder;
class SyscallDecoderDispatcher;
class SyscallDisplayDispatcher;

class SyscallDecoderError {
 public:
  enum class Type { kNone, kCantReadMemory, kUnknownArchitecture };

  SyscallDecoderError() = default;

  Type type() const { return type_; }
  std::string message() const { return message_.str(); }

  std::stringstream& Set(Type type) {
    if (type_ == Type::kNone) {
      type_ = type;
    } else {
      message_ << '\n';
    }
    return message_;
  }

 private:
  Type type_ = Type::kNone;
  std::stringstream message_;
};

class SyscallUse {
 public:
  SyscallUse() = default;
  virtual ~SyscallUse() = default;

  virtual void SyscallInputsDecoded(SyscallDecoder* syscall);
  virtual void SyscallOutputsDecoded(SyscallDecoder* syscall);
  virtual void SyscallDecodingError(const SyscallDecoderError& error, SyscallDecoder* syscall);
};

// Handles the decoding of a syscall argument. At the end, it holds the value
// of the argument in |value_| for basic types or in |loaded_values_| for
// buffers and structs.
class SyscallDecoderArgument {
 public:
  explicit SyscallDecoderArgument(uint64_t value) : value_(value) {}

  // For input basic arguments, the value is directly available.
  uint64_t value() const { return value_; }

  // For input struct or buffer arguments or for output arguments (any
  // argument which is Type*), we need to load the data from memory.
  // When we ask for the data, |loading_| is set to true (it's an asynchronous
  // load). When we receive the data, |loading_| stays at true and
  // |loaded_values_| is filled with the data bytes. If the size of
  // |loaded_values_| is less than expected, that means that we had a load
  // error.
  const std::vector<uint8_t>& loaded_values() const { return loaded_values_; }
  std::vector<uint8_t>& loaded_values() { return loaded_values_; }
  bool loading() const { return loading_; }
  void set_loading() { loading_ = true; }
  void clear_loading() { loading_ = false; }

 private:
  uint64_t value_;
  std::vector<uint8_t> loaded_values_;
  bool loading_ = false;
};

class SyscallDecoderBuffer {
 public:
  SyscallDecoderBuffer() = default;

  const std::vector<uint8_t>& loaded_values() const { return loaded_values_; }
  std::vector<uint8_t>& loaded_values() { return loaded_values_; }
  bool loading() const { return loading_; }
  void set_loading() { loading_ = true; }
  void clear_loading() { loading_ = false; }

 private:
  std::vector<uint8_t> loaded_values_;
  bool loading_ = false;
};

// Handles the decoding of a syscall.
// The decoding starts when SyscallDecoder::Decode is called. Then all the
// decoding steps are executed one after the other (see the comments for Decode
// and the following methods).
class SyscallDecoder {
 public:
  SyscallDecoder(SyscallDecoderDispatcher* dispatcher, InterceptingThreadObserver* thread_observer,
                 zxdb::Thread* thread, uint64_t thread_id, const Syscall* syscall,
                 std::unique_ptr<SyscallUse> use)
      : dispatcher_(dispatcher),
        thread_observer_(thread_observer),
        thread_(thread->GetWeakPtr()),
        thread_id_(thread_id),
        syscall_(syscall),
        arch_(thread->session()->arch()),
        use_(std::move(use)) {}

  SyscallDecoderDispatcher* dispatcher() const { return dispatcher_; }
  zxdb::Thread* thread() const { return thread_.get(); }
  uint64_t thread_id() const { return thread_id_; }
  const Syscall* syscall() const { return syscall_; }
  uint64_t return_address() const { return return_address_; }
  uint64_t syscall_return_value() const { return syscall_return_value_; }

  std::stringstream& Error(SyscallDecoderError::Type type) { return error_.Set(type); }

  // Load the value for a buffer or a struct (field or argument).
  void LoadMemory(uint64_t address, size_t size, std::vector<uint8_t>* destination);

  // Loads the value for a buffer, a struct or an output argument.
  void LoadArgument(int argument_index, size_t size);

  // True if the argument is loaded correctly.
  bool ArgumentLoaded(int argument_index, size_t size) const {
    return decoded_arguments_[argument_index].loaded_values().size() == size;
  }

  // Returns the value of an argument for basic types.
  uint64_t ArgumentValue(int argument_index) const {
    return decoded_arguments_[argument_index].value();
  }

  // Returns a pointer on the argument content for buffers, structs or
  // output arguments.
  uint8_t* ArgumentContent(int argument_index) {
    SyscallDecoderArgument& argument = decoded_arguments_[argument_index];
    if (argument.value() == 0) {
      return nullptr;
    }
    return argument.loaded_values().data();
  }

  // Loads a buffer.
  void LoadBuffer(uint64_t address, size_t size);

  // True if the buffer is loaded correctly.
  bool BufferLoaded(uint64_t address, size_t size) {
    return (address == 0) ? true : buffers_[address].loaded_values().size() == size;
  }

  // Returns a pointer on the loaded buffer.
  uint8_t* BufferContent(uint64_t address) {
    return (address == 0) ? nullptr : buffers_[address].loaded_values().data();
  }

  // Display the argument.
  void Display(int argument_index, std::string_view name, SyscallType sub_type,
               std::ostream& os) const;

  // Starts the syscall decoding. Gets the values of arguments which stay within
  // a register. Then calls LoadStack.
  void Decode();

  // Loads the needed stacks. Then calls LoadInputs.
  void LoadStack();

  // Loads the inputs for buffers and structs. Then call StepToReturnAddress.
  // If several things need to be loaded, they are loaded in parallel.
  void LoadInputs();

  // Puts a breakpoint at the return address (the address just after the call to
  // the syscall) and restarts the stopped thread. When the breakpoint is
  // reached, it calls LoadSyscallReturnValue.
  void StepToReturnAddress();

  // Reads the returned value of the syscall. Then calls LoadOutputs.
  void LoadSyscallReturnValue();

  // Loads the output arguments of the syscall. This is called several time:
  // each time something is loaded, we check if it unlocks a load. This is, for
  // example the case of an output buffer. For an output buffer, we already
  // know the pointer to the buffer (it was in the input arguments) but we need
  // to load the size (because the size is an output argument, we only have a
  // pointer to it). Once we have loaded the size, we are able to load the
  // buffer.
  // When everything is loaded, it calls DecodeAndDisplay.
  // If several independent things need to be loaded, they are loaded in
  // parallel.
  void LoadOutputs();

  // When this function is called, everything we need to display the syscall
  // has been loaded. This function display the syscall. Then, it calls Destroy.
  void DecodeAndDisplay();

  // Destroys this object and remove it from the |syscall_decoders_| list in the
  // SyscallDecoderDispatcher. This function is called when the syscall display
  // has been done or if we had an error and no request is pending (|has_error_|
  // is true and |pending_request_count_| is zero).
  void Destroy();

 private:
  SyscallDecoderDispatcher* const dispatcher_;
  InterceptingThreadObserver* const thread_observer_;
  const fxl::WeakPtr<zxdb::Thread> thread_;
  const uint64_t thread_id_;
  const Syscall* const syscall_;
  const debug_ipc::Arch arch_;
  std::unique_ptr<SyscallUse> use_;
  uint64_t entry_sp_ = 0;
  uint64_t return_address_ = 0;
  std::vector<SyscallDecoderArgument> decoded_arguments_;
  std::map<uint64_t, SyscallDecoderBuffer> buffers_;
  uint64_t syscall_return_value_ = 0;
  int pending_request_count_ = 0;
  bool input_arguments_loaded_ = false;
  SyscallDecoderError error_;
};

class SyscallDisplay : public SyscallUse {
 public:
  SyscallDisplay(SyscallDisplayDispatcher* dispatcher, std::ostream& os)
      : dispatcher_(dispatcher), os_(os) {}

  void SyscallInputsDecoded(SyscallDecoder* syscall) override;
  void SyscallOutputsDecoded(SyscallDecoder* syscall) override;
  void SyscallDecodingError(const SyscallDecoderError& error, SyscallDecoder* syscall) override;

 private:
  SyscallDisplayDispatcher* const dispatcher_;
  std::ostream& os_;
  std::string line_header_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_SYSCALL_DECODER_H_
