#ifndef TOOLS_FIDLCAT_LIB_CODE_GENERATOR_TEST_GENERATOR_H_
#define TOOLS_FIDLCAT_LIB_CODE_GENERATOR_TEST_GENERATOR_H_

#include <zircon/system/public/zircon/types.h>

#include <filesystem>

#include "tools/fidlcat/lib/code_generator/code_generator.h"

namespace fidlcat {

class TestGenerator : public CodeGenerator {
 public:
  TestGenerator(std::string process_name, std::string session_id)
      : CodeGenerator(), process_name_(process_name), session_id_(session_id) {}

  void GenerateTests() {
    std::cout << "Writing tests on disk"
              << " (session id: " << session_id_ << ", process name: " << process_name_ << ")\n";
  }

 private:
  std::string process_name_;
  std::string session_id_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_CODE_GENERATOR_TEST_GENERATOR_H_
