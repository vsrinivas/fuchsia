#ifndef TOOLS_FIDLCAT_LIB_CODE_GENERATOR_TEST_GENERATOR_H_
#define TOOLS_FIDLCAT_LIB_CODE_GENERATOR_TEST_GENERATOR_H_

#include <zircon/system/public/zircon/types.h>

#include <filesystem>

#include "tools/fidlcat/lib/code_generator/code_generator.h"
#include "tools/fidlcat/lib/code_generator/cpp_visitor.h"

namespace fidlcat {

class ProxyPrinter {
 public:
  ProxyPrinter(
      fidl_codec::PrettyPrinter& printer, std::string_view path, std::string_view interface_name,
      std::string_view method_name,
      const std::vector<std::unique_ptr<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>>&
          groups)
      : printer_(printer),
        path_(path),
        interface_name_(interface_name),
        method_name_(method_name),
        groups_(groups) {}

  void GenerateProxyClass();

  void GenerateProxyRun();

  void GenerateProxyGroupsDecl();

  void GenerateProxyBooleans();

  void GenerateProxyPrivateVars();

  void GenerateProxySetup();

 private:
  fidl_codec::PrettyPrinter& printer_;
  std::string path_;
  std::string interface_name_;
  std::string method_name_;
  const std::vector<std::unique_ptr<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>>& groups_;
};

class TestGenerator : public CodeGenerator {
 public:
  TestGenerator(SyscallDecoderDispatcher* dispatcher, const std::string& output_directory)
      : CodeGenerator(), dispatcher_(dispatcher), output_directory_(output_directory) {}

  std::vector<std::shared_ptr<fidl_codec::CppVariable>> CollectArgumentsFromDecodedValue(
      const std::string& variable_prefix, const fidl_codec::StructValue* struct_value);

  std::vector<std::shared_ptr<fidl_codec::CppVariable>> GenerateInputInitializers(
      fidl_codec::PrettyPrinter& printer, FidlCallInfo* call_info);

  std::vector<std::shared_ptr<fidl_codec::CppVariable>> GenerateOutputDeclarations(
      fidl_codec::PrettyPrinter& printer, FidlCallInfo* call_info);

  void GenerateAsyncCallsFromIterator(
      fidl_codec::PrettyPrinter& printer,
      const std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>& async_calls,
      std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>::iterator iterator,
      std::string_view final_statement, bool prepend_new_line);

  void GenerateAsyncCall(fidl_codec::PrettyPrinter& printer,
                         std::pair<FidlCallInfo*, FidlCallInfo*> call_info_pair,
                         std::string_view final_statement);

  void GenerateEvent(fidl_codec::PrettyPrinter& printer, FidlCallInfo* call,
                     std::string_view final_statement);

  void GenerateFireAndForget(fidl_codec::PrettyPrinter& printer, FidlCallInfo* call);

  std::string GenerateSynchronizingConditionalWithinGroup(
      std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>* batch, size_t index, size_t req_index,
      std::string_view final_statement);

  void GenerateGroup(
      fidl_codec::PrettyPrinter& printer,
      std::vector<std::unique_ptr<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>>& groups,
      size_t index, bool prepend_new_line);

  void GenerateTests();

  void GenerateSyncCall(fidl_codec::PrettyPrinter& printer, FidlCallInfo* call_info);

  void WriteTestToFile(std::string_view protocol_name, std::string_view method_name,
                       zx_handle_t handle_id, const std::vector<FidlCallInfo*>& calls);

  std::vector<std::unique_ptr<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>>
  SplitChannelCallsIntoGroups(const std::vector<FidlCallInfo*>& calls);

 private:
  // The dispatcher that the test generator belongs to.
  // We extract process name and events from this field.
  SyscallDecoderDispatcher* const dispatcher_;

  // Path to the directory that tests are going to be written in.
  const std::filesystem::path output_directory_;

  // Unique numeric id for test files.
  std::map<std::string, int> test_counter_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_CODE_GENERATOR_TEST_GENERATOR_H_
