#ifndef TOOLS_FIDLCAT_LIB_CODE_GENERATOR_TEST_GENERATOR_H_
#define TOOLS_FIDLCAT_LIB_CODE_GENERATOR_TEST_GENERATOR_H_

#include <zircon/system/public/zircon/types.h>

#include <filesystem>

#include "tools/fidlcat/lib/code_generator/code_generator.h"

namespace fidlcat {

class TestGenerator : public CodeGenerator {
 public:
  TestGenerator(SyscallDecoderDispatcher* dispatcher, std::string session_id)
      : CodeGenerator(), dispatcher_(dispatcher), session_id_(session_id) {}

  void GenerateTests();

 private:
  SyscallDecoderDispatcher* const dispatcher_;
  const std::string session_id_;
};

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_CODE_GENERATOR_TEST_GENERATOR_H_
