#ifndef TOOLS_FIDLCAT_LIB_CODE_GENERATOR_CODE_GENERATOR_H_
#define TOOLS_FIDLCAT_LIB_CODE_GENERATOR_CODE_GENERATOR_H_

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>

#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/printer.h"
#include "tools/fidlcat/lib/event.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

std::string ToSnakeCase(std::string_view str);

class FidlCallInfo {
 public:
  FidlCallInfo(bool crashed, std::string_view enclosing_interface_name, zx_handle_t handle_id,
               SyscallKind kind, std::string_view method_name)
      : crashed_(crashed),
        enclosing_interface_name_(enclosing_interface_name),
        handle_id_(handle_id),
        kind_(kind),
        method_name_(method_name) {}

  bool crashed() const { return crashed_; }

  zx_handle_t handle_id() const { return handle_id_; }

  SyscallKind kind() const { return kind_; }

  const std::string& method_name() const { return method_name_; }

  const std::string& enclosing_interface_name() const { return enclosing_interface_name_; }

 private:
  // True if server crashes in response to a zx_channel_call
  const bool crashed_ = false;

  // Interface name for the FIDL call (e.g. fidl.examples.echo/Echo)
  const std::string enclosing_interface_name_;

  // Handle id of the FIDL call, used to reconcile writes and reads
  const zx_handle_t handle_id_;

  // The system call used as part of the FIDL call
  const SyscallKind kind_;

  // FIDL method name (e.g. EchoString)
  const std::string method_name_;
};

std::unique_ptr<FidlCallInfo> OutputEventToFidlCallInfo(OutputEvent* output_event);

class CodeGenerator {
 public:
  CodeGenerator() {}

  const std::map<zx_handle_t, std::vector<std::unique_ptr<FidlCallInfo>>>& call_log() {
    return call_log_;
  }

  void AddEventToLog(std::unique_ptr<FidlCallInfo> call_info) {
    call_log_[call_info->handle_id()].emplace_back(std::move(call_info));
  }

 private:
  std::map<zx_handle_t, std::vector<std::unique_ptr<FidlCallInfo>>> call_log_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_CODE_GENERATOR_CODE_GENERATOR_H_
