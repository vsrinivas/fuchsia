#ifndef TOOLS_FIDLCAT_LIB_CODE_GENERATOR_TEST_GENERATOR_H_
#define TOOLS_FIDLCAT_LIB_CODE_GENERATOR_TEST_GENERATOR_H_

#include <zircon/system/public/zircon/types.h>

#include <filesystem>

#include "tools/fidlcat/lib/code_generator/code_generator.h"

namespace fidlcat {

class TestGenerator : public CodeGenerator {
 public:
  TestGenerator(SyscallDecoderDispatcher* dispatcher, const std::string& output_directory)
      : CodeGenerator(), dispatcher_(dispatcher), output_directory_(output_directory) {
    std::filesystem::create_directories(output_directory_);
  }

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
