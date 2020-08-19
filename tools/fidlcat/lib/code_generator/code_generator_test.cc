#include "tools/fidlcat/lib/code_generator/code_generator.h"

#include <sstream>

#include <gtest/gtest.h>

namespace fidlcat {

TEST(CodeGenerator, ToSnakeCase) {
  ASSERT_EQ(ToSnakeCase("fidl.examples.echo/EchoString"), "fidl_examples_echo__echo_string");
  ASSERT_EQ(ToSnakeCase("EchoString"), "echo_string");
  ASSERT_EQ(ToSnakeCase("TheFIDLMessage"), "the_fidlmessage");
}

}  // namespace fidlcat
