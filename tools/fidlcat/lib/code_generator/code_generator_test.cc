#include "tools/fidlcat/lib/code_generator/code_generator.h"

#include <sstream>

#include <gtest/gtest.h>

#include "tools/fidlcat/lib/code_generator/test_generator.h"

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
  std::stringstream os_;

  fidl_codec::PrettyPrinter printer_ =
      fidl_codec::PrettyPrinter(os_, fidl_codec::WithoutColors, true, "", 0, false);

  CodeGenerator code_generator_;
};

TEST(CodeGenerator, FidlMethodToIncludePath) {
  ASSERT_EQ(FidlMethodToIncludePath("fidl.examples.echo"), "fidl/examples/echo/cpp/fidl.h");
}

TEST_F(CodeGeneratorTest, GenerateFidlIncludes) {
  code_generator_.GenerateFidlIncludes(printer_);

  std::string expected = "#include <fidl/examples/echo/cpp/fidl.h>\n";
  ASSERT_EQ(os_.str(), expected);
}

TEST_F(CodeGeneratorTest, GenerateIncludes) {
  code_generator_.GenerateIncludes(printer_);
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

class TestGeneratorTest : public ::testing::Test {
 public:
  TestGeneratorTest() : test_generator_(nullptr, "") {
    struct_def_input_ = std::make_shared<fidl_codec::Struct>("struct_input");
    struct_def_input_->AddMember("base", std::make_unique<fidl_codec::Int64Type>());
    struct_def_input_->AddMember("exponent", std::make_unique<fidl_codec::Int64Type>());

    struct_def_output_ = std::make_shared<fidl_codec::Struct>("struct_output");
    struct_def_output_->AddMember("result", std::make_unique<fidl_codec::Int64Type>());
    struct_def_output_->AddMember("result_words", std::make_unique<fidl_codec::StringType>());

    zx_handle_t handle_id = 1234;

    struct_input_1_ = std::make_shared<fidl_codec::StructValue>(*struct_def_input_.get());
    struct_input_1_->AddField("base", std::make_unique<fidl_codec::IntegerValue>(int64_t(2)));
    struct_input_1_->AddField("exponent", std::make_unique<fidl_codec::IntegerValue>(int64_t(3)));

    call_write_1_ = std::make_shared<FidlCallInfo>(
        false, "fidl.examples.calculator", handle_id, SyscallKind::kChannelWrite, "Exponentiation",
        struct_def_input_.get(), struct_def_output_.get(), struct_input_1_.get(), nullptr);

    struct_input_2_ = std::make_shared<fidl_codec::StructValue>(*struct_def_input_.get());
    struct_input_2_->AddField("base", std::make_unique<fidl_codec::IntegerValue>(int64_t(3)));
    struct_input_2_->AddField("exponent", std::make_unique<fidl_codec::IntegerValue>(int64_t(2)));

    call_write_2_ = std::make_shared<FidlCallInfo>(
        false, "fidl.examples.calculator", handle_id, SyscallKind::kChannelWrite, "Exponentiation",
        struct_def_input_.get(), struct_def_output_.get(), struct_input_2_.get(), nullptr);

    struct_output_1_ = std::make_shared<fidl_codec::StructValue>(*struct_def_output_.get());
    struct_output_1_->AddField("result", std::make_unique<fidl_codec::IntegerValue>(int64_t(8)));
    struct_output_1_->AddField("result_words", std::make_unique<fidl_codec::StringValue>("eight"));

    call_read_1_ = std::make_shared<FidlCallInfo>(
        false, "fidl.examples.calculator", handle_id, SyscallKind::kChannelRead, "Exponentiation",
        nullptr, nullptr, nullptr, struct_output_1_.get());

    struct_output_2_ = std::make_shared<fidl_codec::StructValue>(*struct_def_output_.get());
    struct_output_2_->AddField("result", std::make_unique<fidl_codec::IntegerValue>(int64_t(9)));
    struct_output_2_->AddField("result_words", std::make_unique<fidl_codec::StringValue>("nine"));

    call_read_2_ = std::make_shared<FidlCallInfo>(
        false, "fidl.examples.calculator", handle_id, SyscallKind::kChannelRead, "Exponentiation",
        nullptr, nullptr, nullptr, struct_output_2_.get());

    call_sync_ = std::make_shared<FidlCallInfo>(false, "fidl.examples.calculator", handle_id,
                                                SyscallKind::kChannelCall, "Exponentiation",
                                                struct_def_input_.get(), struct_def_output_.get(),
                                                struct_input_1_.get(), struct_output_1_.get());
  }

  void SetUp() { os_.str(""); }

 protected:
  TestGenerator test_generator_;

  std::stringstream os_;
  fidl_codec::PrettyPrinter printer_ =
      fidl_codec::PrettyPrinter(os_, fidl_codec::WithoutColors, true, "", 0, false);

  std::shared_ptr<fidl_codec::StructValue> struct_input_1_;
  std::shared_ptr<fidl_codec::StructValue> struct_input_2_;
  std::shared_ptr<fidl_codec::StructValue> struct_output_1_;
  std::shared_ptr<fidl_codec::StructValue> struct_output_2_;
  std::shared_ptr<fidl_codec::Struct> struct_def_input_;
  std::shared_ptr<fidl_codec::Struct> struct_def_output_;
  std::shared_ptr<FidlCallInfo> call_write_1_;
  std::shared_ptr<FidlCallInfo> call_read_1_;
  std::shared_ptr<FidlCallInfo> call_write_2_;
  std::shared_ptr<FidlCallInfo> call_read_2_;
  std::shared_ptr<FidlCallInfo> call_sync_;
};

TEST_F(TestGeneratorTest, GenerateAsyncCall) {
  // Call fidl.examples.calculator/Exponentiation twice
  std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>> async_calls = {
      std::pair<FidlCallInfo*, FidlCallInfo*>(call_write_1_.get(), call_read_1_.get()),
      std::pair<FidlCallInfo*, FidlCallInfo*>(call_write_2_.get(), call_read_2_.get())};

  test_generator_.GenerateAsyncCallsFromIterator(printer_, async_calls, async_calls.begin(),
                                                 "// end of async calls\n");

  std::string expected =
      "int64_t in_base_0 = 2;\n"
      "int64_t in_exponent_0 = 3;\n"
      "int64_t out_result_0;\n"
      "std::string out_result_words_0;\n"
      "proxy_->Exponentiation(in_base_0, in_exponent_0, [](int64_t out_result_0, std::string "
      "out_result_words_0) {\n"
      "  int64_t out_result_0_expected = 8;\n"
      "  ASSERT_EQ(out_result_0, out_result_0_expected);\n"
      "\n"
      "  std::string out_result_words_0_expected = \"eight\";\n"
      "  ASSERT_EQ(out_result_words_0, out_result_words_0_expected);\n"
      "\n"
      "  int64_t in_base_1 = 3;\n"
      "  int64_t in_exponent_1 = 2;\n"
      "  int64_t out_result_1;\n"
      "  std::string out_result_words_1;\n"
      "  proxy_->Exponentiation(in_base_1, in_exponent_1, [](int64_t out_result_1, std::string "
      "out_result_words_1) {\n"
      "    int64_t out_result_1_expected = 9;\n"
      "    ASSERT_EQ(out_result_1, out_result_1_expected);\n"
      "\n"
      "    std::string out_result_words_1_expected = \"nine\";\n"
      "    ASSERT_EQ(out_result_words_1, out_result_words_1_expected);\n"
      "\n"
      "    // end of async calls\n"
      "  });\n"
      "});\n";

  EXPECT_EQ(os_.str(), expected);
}

TEST_F(TestGeneratorTest, GenerateInputInitializers) {
  test_generator_.GenerateInputInitializers(printer_, call_write_1_.get());
  EXPECT_EQ(os_.str(),
            "int64_t in_base_0 = 2;\n"
            "int64_t in_exponent_0 = 3;\n");
}

TEST_F(TestGeneratorTest, GenerateOutputDeclarations) {
  test_generator_.GenerateOutputDeclarations(printer_, call_read_1_.get());
  EXPECT_EQ(os_.str(),
            "int64_t out_result_0;\n"
            "std::string out_result_words_0;\n");
}

TEST_F(TestGeneratorTest, CollectArgumentsFromDecodedValue) {
  std::vector<std::shared_ptr<fidl_codec::CppVariable>> vars1 =
      test_generator_.CollectArgumentsFromDecodedValue("in_", struct_input_1_.get());

  EXPECT_EQ(vars1[0]->name(), "in_base_0");
  EXPECT_EQ(vars1[1]->name(), "in_exponent_0");

  // Variables will have the same prefix, so AcquireUniqueName will bump up the counter
  std::vector<std::shared_ptr<fidl_codec::CppVariable>> vars2 =
      test_generator_.CollectArgumentsFromDecodedValue("in_", struct_input_1_.get());

  EXPECT_EQ(vars2[0]->name(), "in_base_1");
  EXPECT_EQ(vars2[1]->name(), "in_exponent_1");
}

TEST_F(TestGeneratorTest, AcquireUniqueName) {
  EXPECT_EQ(test_generator_.AcquireUniqueName("foo"), "foo_0");
  EXPECT_EQ(test_generator_.AcquireUniqueName("bar"), "bar_0");
  EXPECT_EQ(test_generator_.AcquireUniqueName("foo"), "foo_1");
  EXPECT_EQ(test_generator_.AcquireUniqueName("bar"), "bar_1");
}

TEST_F(TestGeneratorTest, GenerateSyncCall) {
  test_generator_.GenerateSyncCall(printer_, call_sync_.get());

  std::string expected =
      "int64_t in_base_0 = 2;\n"
      "int64_t in_exponent_0 = 3;\n"
      "int64_t out_result_0;\n"
      "std::string out_result_words_0;\n"
      "proxy_sync_->Exponentiation(in_base_0, in_exponent_0, &out_result_0, &out_result_words_0);\n"
      "int64_t out_result_0_expected = 8;\n"
      "ASSERT_EQ(out_result_0, out_result_0_expected);\n"
      "\n"
      "std::string out_result_words_0_expected = \"eight\";\n"
      "ASSERT_EQ(out_result_words_0, out_result_words_0_expected);\n";

  EXPECT_EQ(os_.str(), expected);
}

}  // namespace fidlcat
