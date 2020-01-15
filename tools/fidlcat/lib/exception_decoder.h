// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_EXCEPTION_DECODER_H_
#define TOOLS_FIDLCAT_LIB_EXCEPTION_DECODER_H_

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/thread_observer.h"
#include "tools/fidlcat/lib/decoder.h"

namespace fidlcat {

class ExceptionDecoder;
class InterceptionWorkflow;
class SyscallDecoderDispatcher;
class SyscallDisplayDispatcher;

class ExceptionUse {
 public:
  ExceptionUse() = default;
  virtual ~ExceptionUse() = default;

  virtual void ExceptionDecoded(ExceptionDecoder* decoder);
  virtual void DecodingError(const DecoderError& error, ExceptionDecoder* decoder);
};

// Handles the decoding of an exception.
// The decoding starts when ExceptionDecoder::Decode is called. Then all the decoding steps are
// executed one after the other (see the comments for Decode and the following methods).
class ExceptionDecoder {
 public:
  ExceptionDecoder(InterceptionWorkflow* workflow, SyscallDecoderDispatcher* dispatcher,
                   zxdb::Thread* thread, std::unique_ptr<ExceptionUse> use)
      : workflow_(workflow),
        dispatcher_(dispatcher),
        weak_thread_(thread->GetWeakPtr()),
        process_name_(thread->GetProcess()->GetName()),
        process_id_(thread->GetProcess()->GetKoid()),
        thread_id_(thread->GetKoid()),
        use_(std::move(use)) {}

  SyscallDecoderDispatcher* dispatcher() const { return dispatcher_; }
  zxdb::Thread* get_thread() const { return weak_thread_.get(); }
  const std::string& process_name() const { return process_name_; }
  uint64_t process_id() const { return process_id_; }
  uint64_t thread_id() const { return thread_id_; }
  const std::vector<zxdb::Location>& caller_locations() const { return caller_locations_; }

  std::stringstream& Error(DecoderError::Type type) { return error_.Set(type); }

  // Asks for the full statck then display the exception.
  void Decode();

  // Displays the exception then destroy it.
  void Display();

  // Destroys this object and remove it from the |syscall_decoders_| list in the
  // SyscallDecoderDispatcher. This function is called when the syscall display
  // has been done or if we had an error and no request is pending (|has_error_|
  // is true and |pending_request_count_| is zero).
  void Destroy();

 private:
  InterceptionWorkflow* const workflow_;
  SyscallDecoderDispatcher* const dispatcher_;
  const fxl::WeakPtr<zxdb::Thread> weak_thread_;
  const std::string process_name_;
  const uint64_t process_id_;
  const uint64_t thread_id_;
  std::unique_ptr<ExceptionUse> use_;
  std::vector<zxdb::Location> caller_locations_;
  DecoderError error_;
};

class ExceptionDisplay : public ExceptionUse {
 public:
  ExceptionDisplay(SyscallDisplayDispatcher* dispatcher, std::ostream& os)
      : dispatcher_(dispatcher), os_(os) {}

  void ExceptionDecoded(ExceptionDecoder* decoder) override;
  void DecodingError(const DecoderError& error, ExceptionDecoder* decoder) override;

 private:
  SyscallDisplayDispatcher* const dispatcher_;
  std::ostream& os_;
  std::string line_header_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_EXCEPTION_DECODER_H_
