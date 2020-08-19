#include "tools/fidlcat/lib/code_generator/code_generator.h"

#include <sstream>

#include <gtest/gtest.h>

namespace fidlcat {

TEST(CodeGenerator, ToSnakeCase) {
  ASSERT_EQ(ToSnakeCase("fidl.examples.echo/EchoString"), "fidl_examples_echo__echo_string");
  ASSERT_EQ(ToSnakeCase("EchoString"), "echo_string");
  ASSERT_EQ(ToSnakeCase("TheFIDLMessage"), "the_fidlmessage");
}

class CodeGeneratorTest : public ::testing::Test {
 public:
  CodeGeneratorTest() { code_generator_.AddFidlHeaderForInterface("fidl.examples.echo"); }

  void SetUp() { os_.str(""); }

 protected:
  CodeGenerator code_generator_;
  std::stringstream os_;
};

TEST(CodeGenerator, FidlMethodToIncludePath) {
  ASSERT_EQ(FidlMethodToIncludePath("fidl.examples.echo"), "fidl/examples/echo/cpp/fidl.h");
}

TEST_F(CodeGeneratorTest, GenerateFidlIncludes) {
  code_generator_.GenerateFidlIncludes(os_);

  std::string expected = "#include <fidl/examples/echo/cpp/fidl.h>\n";
  ASSERT_EQ(os_.str(), expected);
}

TEST_F(CodeGeneratorTest, GenerateIncludes) {
  code_generator_.GenerateIncludes(os_);
  ASSERT_EQ(os_.str(),
            "#include <lib/async-loop/cpp/loop.h>\n"
            "#include <lib/async-loop/default.h>\n"
            "#include <lib/async/default.h>\n"
            "#include <lib/syslog/cpp/macros.h>\n"
            "\n"
            "#include <gtest/gtest.h>\n"
            "\n"
            "#include \"lib/sys/cpp/component_context.h\"\n"
            "\n"
            "#include <fidl/examples/echo/cpp/fidl.h>\n"
            "\n");
}

}  // namespace fidlcat
