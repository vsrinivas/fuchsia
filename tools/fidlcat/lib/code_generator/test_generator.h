#ifndef TOOLS_FIDLCAT_LIB_CODE_GENERATOR_TEST_GENERATOR_H_
#define TOOLS_FIDLCAT_LIB_CODE_GENERATOR_TEST_GENERATOR_H_

#include <zircon/system/public/zircon/types.h>

#include <filesystem>

#include "tools/fidlcat/lib/code_generator/code_generator.h"
#include "tools/fidlcat/lib/code_generator/cpp_visitor.h"

namespace fidlcat {

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
      std::string_view final_statement);

  void GenerateTests();

  void WriteTestToFile(std::string_view protocol_name);

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
