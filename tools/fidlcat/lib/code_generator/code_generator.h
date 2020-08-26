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
#include "src/lib/fidl_codec/wire_object.h"
#include "tools/fidlcat/lib/event.h"
#include "tools/fidlcat/lib/syscall_decoder_dispatcher.h"

namespace fidlcat {

std::string FidlMethodToIncludePath(std::string_view identifier);
std::string ToSnakeCase(std::string_view str);

class FidlCallInfo {
 public:
  FidlCallInfo(bool crashed, std::string_view enclosing_interface_name, zx_handle_t handle_id,
               zx_txid_t txid, SyscallKind kind, std::string_view method_name,
               const fidl_codec::Struct* struct_input, const fidl_codec::Struct* struct_output,
               const fidl_codec::StructValue* decoded_input_value,
               const fidl_codec::StructValue* decoded_output_value)
      : crashed_(crashed),
        enclosing_interface_name_(enclosing_interface_name),
        handle_id_(handle_id),
        txid_(txid),
        kind_(kind),
        method_name_(method_name),
        struct_input_(struct_input),
        struct_output_(struct_output),
        decoded_input_value_(decoded_input_value),
        decoded_output_value_(decoded_output_value) {}

  bool crashed() const { return crashed_; }

  zx_handle_t handle_id() const { return handle_id_; }

  zx_txid_t txid() const { return txid_; }

  SyscallKind kind() const { return kind_; }

  const std::string& method_name() const { return method_name_; }

  const std::string& enclosing_interface_name() const { return enclosing_interface_name_; }

  const fidl_codec::Struct* struct_input() const { return struct_input_; }

  const fidl_codec::Struct* struct_output() const { return struct_output_; }

  const fidl_codec::StructValue* decoded_input_value() const { return decoded_input_value_; }

  const fidl_codec::StructValue* decoded_output_value() const { return decoded_output_value_; }

  size_t sequence_number() const { return sequence_number_; }
  void SetSequenceNumber(size_t sequence_number) { sequence_number_ = sequence_number; }

 private:
  // True if server crashes in response to a zx_channel_call
  const bool crashed_ = false;

  // Interface name for the FIDL call (e.g. fidl.examples.echo/Echo)
  const std::string enclosing_interface_name_;

  // Handle id of the FIDL call, used to reconcile writes and reads
  const zx_handle_t handle_id_;

  // Transaction id of the syscall, used to reconcile writes and reads
  const zx_txid_t txid_;

  // The system call used as part of the FIDL call
  const SyscallKind kind_;

  // FIDL method name (e.g. EchoString)
  const std::string method_name_;

  // Input struct definition
  const fidl_codec::Struct* const struct_input_;

  // Output struct definition
  const fidl_codec::Struct* const struct_output_;

  // Decoded input value
  const fidl_codec::StructValue* const decoded_input_value_;

  // Decoded output value
  const fidl_codec::StructValue* const decoded_output_value_;

  // Sequence number in the channel
  size_t sequence_number_;
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

  void AddFidlHeaderForInterface(std::string_view enclosing_interface_name) {
    fidl_headers_.insert(FidlMethodToIncludePath(enclosing_interface_name));
  }

  std::string AcquireUniqueName(const std::string& prefix) {
    return prefix + "_" + std::to_string(unique_name_counter_[prefix]++);
  }

  void GenerateIncludes(fidl_codec::PrettyPrinter& printer);

  void GenerateFidlIncludes(fidl_codec::PrettyPrinter& printer);

 private:
  // A log of processed events.
  std::map<zx_handle_t, std::vector<std::unique_ptr<FidlCallInfo>>> call_log_;

  // Paths for FIDL-related #include directives.
  std::set<std::string> fidl_headers_;

  // Counter for unique variable ids.
  std::map<std::string, int> unique_name_counter_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_CODE_GENERATOR_CODE_GENERATOR_H_
