#include "tools/fidlcat/lib/code_generator/code_generator.h"

#include <sstream>

#include <gtest/gtest.h>

#include "src/lib/fidl_codec/wire_object.h"
#include "src/lib/fidl_codec/wire_types.h"
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
    struct_def_input_ = std::make_shared<fidl_codec::Struct>("StructInput");
    struct_def_input_->AddMember("base", std::make_unique<fidl_codec::Int64Type>());
    struct_def_input_->AddMember("exponent", std::make_unique<fidl_codec::Int64Type>());

    struct_def_output_ = std::make_shared<fidl_codec::Struct>("StructOutput");
    struct_def_output_->AddMember("result", std::make_unique<fidl_codec::Int64Type>());
    struct_def_output_->AddMember("result_words", std::make_unique<fidl_codec::StringType>());

    zx_handle_t handle_id = 1234;
    zx_txid_t txid_1 = 1;
    zx_txid_t txid_2 = 2;
    zx_txid_t txid_3 = 3;
    zx_txid_t txid_4 = 4;

    struct_input_1_ = std::make_shared<fidl_codec::StructValue>(*struct_def_input_.get());
    struct_input_1_->AddField("base", std::make_unique<fidl_codec::IntegerValue>(int64_t(2)));
    struct_input_1_->AddField("exponent", std::make_unique<fidl_codec::IntegerValue>(int64_t(3)));

    call_write_1_ = std::make_shared<FidlCallInfo>(
        false, "fidl.examples.calculator", handle_id, txid_1, SyscallKind::kChannelWrite,
        "Exponentiation", struct_def_input_.get(), struct_def_output_.get(), struct_input_1_.get(),
        nullptr);

    struct_input_2_ = std::make_shared<fidl_codec::StructValue>(*struct_def_input_.get());
    struct_input_2_->AddField("base", std::make_unique<fidl_codec::IntegerValue>(int64_t(3)));
    struct_input_2_->AddField("exponent", std::make_unique<fidl_codec::IntegerValue>(int64_t(2)));

    call_write_2_ = std::make_shared<FidlCallInfo>(
        false, "fidl.examples.calculator", handle_id, txid_2, SyscallKind::kChannelWrite,
        "ExponentiationSlow", struct_def_input_.get(), struct_def_output_.get(),
        struct_input_2_.get(), nullptr);

    struct_output_1_ = std::make_shared<fidl_codec::StructValue>(*struct_def_output_.get());
    struct_output_1_->AddField("result", std::make_unique<fidl_codec::IntegerValue>(int64_t(8)));
    struct_output_1_->AddField("result_words", std::make_unique<fidl_codec::StringValue>("eight"));

    call_read_1_ = std::make_shared<FidlCallInfo>(
        false, "fidl.examples.calculator", handle_id, txid_1, SyscallKind::kChannelRead,
        "Exponentiation", nullptr, nullptr, nullptr, struct_output_1_.get());

    struct_output_2_ = std::make_shared<fidl_codec::StructValue>(*struct_def_output_.get());
    struct_output_2_->AddField("result", std::make_unique<fidl_codec::IntegerValue>(int64_t(9)));
    struct_output_2_->AddField("result_words", std::make_unique<fidl_codec::StringValue>("nine"));

    call_read_2_ = std::make_shared<FidlCallInfo>(
        false, "fidl.examples.calculator", handle_id, txid_2, SyscallKind::kChannelRead,
        "ExponentiationSlow", nullptr, nullptr, nullptr, struct_output_2_.get());

    call_sync_ = std::make_shared<FidlCallInfo>(false, "fidl.examples.calculator", handle_id,
                                                txid_3, SyscallKind::kChannelCall, "Exponentiation",
                                                struct_def_input_.get(), struct_def_output_.get(),
                                                struct_input_1_.get(), struct_output_1_.get());

    call_event_ = std::make_shared<FidlCallInfo>(
        false, "fidl.examples.calculator", handle_id, 0, SyscallKind::kChannelRead, "OnTimeout",
        nullptr, struct_def_output_.get(), nullptr, struct_output_1_.get());

    call_fire_and_forget_ = std::make_shared<FidlCallInfo>(
        false, "fidl.examples.calculator", handle_id, txid_4, SyscallKind::kChannelWrite, "TurnOn",
        struct_def_input_.get(), nullptr, struct_input_1_.get(), nullptr);
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
  std::shared_ptr<FidlCallInfo> call_event_;
  std::shared_ptr<FidlCallInfo> call_fire_and_forget_;
};

TEST_F(TestGeneratorTest, GenerateAsyncCall) {
  // Call fidl.examples.calculator/Exponentiation twice
  auto pair1 = std::pair<FidlCallInfo*, FidlCallInfo*>(call_write_1_.get(), call_read_1_.get());
  auto pair2 = std::pair<FidlCallInfo*, FidlCallInfo*>(call_write_2_.get(), call_read_2_.get());
  std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>> async_calls = {pair1, pair2};

  test_generator_.GenerateAsyncCallsFromIterator(printer_, async_calls, async_calls.begin(),
                                                 "// end of async calls\n", false);

  std::string expected =
      "int64_t in_base_0 = 2;\n"
      "int64_t in_exponent_0 = 3;\n"
      "int64_t out_result_0;\n"
      "std::string out_result_words_0;\n"
      "proxy_->Exponentiation(in_base_0, in_exponent_0, [this](int64_t out_result_0, std::string "
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
      "  proxy_->ExponentiationSlow(in_base_1, in_exponent_1, [this](int64_t out_result_1, "
      "std::string "
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

TEST_F(TestGeneratorTest, GenerateInitializationStruct) {
  fidl_codec::CppVisitor visitor("my_struct_var");

  auto struct_input_type =
      std::make_unique<fidl_codec::StructType>(*(struct_def_input_.get()), false);

  auto struct_output_type =
      std::make_unique<fidl_codec::StructType>(*(struct_def_output_.get()), false);

  auto struct_dummy_input = std::make_unique<fidl_codec::StructValue>(*struct_def_input_.get());
  struct_dummy_input->AddField("base", std::make_unique<fidl_codec::IntegerValue>(int64_t(3)));
  struct_dummy_input->AddField("exponent", std::make_unique<fidl_codec::IntegerValue>(int64_t(2)));

  auto struct_dummy_output = std::make_unique<fidl_codec::StructValue>(*struct_def_output_.get());
  struct_dummy_output->AddField("result", std::make_unique<fidl_codec::IntegerValue>(int64_t(8)));
  struct_dummy_output->AddField("result_words", std::make_unique<fidl_codec::StringValue>("eight"));

  auto struct_def_recursive = std::make_shared<fidl_codec::Struct>("struct_recursive");
  struct_def_recursive->AddMember("input", std::move(struct_input_type));
  struct_def_recursive->AddMember("output", std::move(struct_output_type));

  auto struct_recursive = std::make_shared<fidl_codec::StructValue>(*struct_def_recursive.get());
  struct_recursive->AddField("input", std::move(struct_dummy_input));
  struct_recursive->AddField("output", std::move(struct_dummy_output));

  auto struct_recursive_type =
      std::make_unique<fidl_codec::StructType>(*(struct_def_recursive.get()), false);
  struct_recursive->Visit(&visitor, struct_recursive_type.get());
  std::shared_ptr<fidl_codec::CppVariable> cpp_var = visitor.result();

  cpp_var->GenerateInitialization(printer_);

  // TODO(nimaj):
  // Generate literals and avoid creating new variables for int and strings.
  EXPECT_EQ(os_.str(),
            "int64_t my_struct_var_input_base = 3;\n"
            "int64_t my_struct_var_input_exponent = 2;\n"
            "StructInput my_struct_var_input = { my_struct_var_input_base, "
            "my_struct_var_input_exponent };\n"
            "int64_t my_struct_var_output_result = 8;\n"
            "std::string my_struct_var_output_result_words = \"eight\";\n"
            "StructOutput my_struct_var_output = { my_struct_var_output_result, "
            "my_struct_var_output_result_words };\n"
            "struct_recursive my_struct_var = { my_struct_var_input, my_struct_var_output };\n");
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
      "\n"
      "int64_t out_result_0_expected = 8;\n"
      "ASSERT_EQ(out_result_0, out_result_0_expected);\n"
      "\n"
      "std::string out_result_words_0_expected = \"eight\";\n"
      "ASSERT_EQ(out_result_words_0, out_result_words_0_expected);\n";

  EXPECT_EQ(os_.str(), expected);
}

TEST_F(TestGeneratorTest, GenerateEvent) {
  test_generator_.GenerateEvent(printer_, call_event_.get(), "// end of event\n");

  std::string expected =
      "int64_t out_result_0;\n"
      "std::string out_result_words_0;\n"
      "proxy_.events().OnTimeout = [this](int64_t out_result_0, std::string out_result_words_0) "
      "{\n"
      "  int64_t out_result_0_expected = 8;\n"
      "  ASSERT_EQ(out_result_0, out_result_0_expected);\n"
      "\n"
      "  std::string out_result_words_0_expected = \"eight\";\n"
      "  ASSERT_EQ(out_result_words_0, out_result_words_0_expected);\n"
      "\n"
      "  // end of event\n"
      "};\n";

  EXPECT_EQ(os_.str(), expected);
}

TEST_F(TestGeneratorTest, GenerateFireAndForget) {
  test_generator_.GenerateFireAndForget(printer_, call_fire_and_forget_.get());

  std::string expected =
      "int64_t in_base_0 = 2;\n"
      "int64_t in_exponent_0 = 3;\n"
      "proxy_->TurnOn(in_base_0, in_exponent_0);\n";

  EXPECT_EQ(os_.str(), expected);
}

TEST_F(TestGeneratorTest, GenerateGroup) {
  auto pair1 = std::pair<FidlCallInfo*, FidlCallInfo*>(call_write_1_.get(), call_read_1_.get());
  auto pair2 = std::pair<FidlCallInfo*, FidlCallInfo*>(call_write_2_.get(), call_read_2_.get());
  auto group_1 = std::make_unique<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>();
  group_1->push_back(pair1);
  group_1->push_back(pair2);

  auto pair3 = std::pair<FidlCallInfo*, FidlCallInfo*>(nullptr, call_event_.get());
  auto group_2 = std::make_unique<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>();
  group_2->push_back(pair3);

  std::vector<std::unique_ptr<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>> groups;
  groups.emplace_back(std::move(group_1));
  groups.emplace_back(std::move(group_2));

  test_generator_.GenerateGroup(printer_, groups, 0, false);

  std::string expected_1 =
      "void Proxy::group_0() {\n"
      "  int64_t in_base_0 = 2;\n"
      "  int64_t in_exponent_0 = 3;\n"
      "  int64_t out_result_0;\n"
      "  std::string out_result_words_0;\n"
      "  proxy_->Exponentiation(in_base_0, in_exponent_0, [this](int64_t "
      "out_result_0, std::string "
      "out_result_words_0) {\n"
      "    int64_t out_result_0_expected = 8;\n"
      "    ASSERT_EQ(out_result_0, out_result_0_expected);\n"
      "\n"
      "    std::string out_result_words_0_expected = \"eight\";\n"
      "    ASSERT_EQ(out_result_words_0, out_result_words_0_expected);\n"
      "\n"
      "    received_0_0_ = true;\n"
      "    if (received_0_1_) {\n"
      "      group_1();\n"
      "    }\n"
      "  });\n"
      "  int64_t in_base_1 = 3;\n"
      "  int64_t in_exponent_1 = 2;\n"
      "  int64_t out_result_1;\n"
      "  std::string out_result_words_1;\n"
      "  proxy_->ExponentiationSlow(in_base_1, in_exponent_1, [this"
      "](int64_t "
      "out_result_1, std::string "
      "out_result_words_1) {\n"
      "    int64_t out_result_1_expected = 9;\n"
      "    ASSERT_EQ(out_result_1, out_result_1_expected);\n"
      "\n"
      "    std::string out_result_words_1_expected = \"nine\";\n"
      "    ASSERT_EQ(out_result_words_1, out_result_words_1_expected);\n"
      "\n"
      "    received_0_1_ = true;\n"
      "    if (received_0_0_) {\n"
      "      group_1();\n"
      "    }\n"
      "  });\n"
      "}\n";

  EXPECT_EQ(os_.str(), expected_1);

  os_.str("");

  test_generator_.GenerateGroup(printer_, groups, 1, true);

  std::string expected_2 =
      "\n"
      "void Proxy::group_1() {\n"
      "  int64_t out_result_2;\n"
      "  std::string out_result_words_2;\n"
      "  proxy_.events().OnTimeout = [this](int64_t out_result_2, std::string "
      "out_result_words_2) {\n"
      "    int64_t out_result_2_expected = 8;\n"
      "    ASSERT_EQ(out_result_2, out_result_2_expected);\n"
      "\n"
      "    std::string out_result_words_2_expected = \"eight\";\n"
      "    ASSERT_EQ(out_result_words_2, out_result_words_2_expected);\n"
      "\n"
      "    loop_.Quit();\n"
      "  };\n"
      "}\n";

  EXPECT_EQ(os_.str(), expected_2);
}

TEST_F(TestGeneratorTest, SplitChannelCallsIntoGroupsOneGroup) {
  std::vector<FidlCallInfo*> calls;
  calls.push_back(call_write_1_.get());
  calls.push_back(call_write_2_.get());
  calls.push_back(call_read_1_.get());
  calls.push_back(call_read_2_.get());

  std::vector<std::unique_ptr<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>> groups =
      test_generator_.SplitChannelCallsIntoGroups(calls);

  EXPECT_EQ(groups.size(), 1u);
  EXPECT_EQ(groups[0]->size(), 2u);
  EXPECT_EQ(groups[0]->at(0).first, call_write_1_.get());
  EXPECT_EQ(groups[0]->at(0).second, call_read_1_.get());
  EXPECT_EQ(groups[0]->at(1).first, call_write_2_.get());
  EXPECT_EQ(groups[0]->at(1).second, call_read_2_.get());
}

TEST_F(TestGeneratorTest, SplitChannelCallsIntoGroupsTwoGroups) {
  std::vector<FidlCallInfo*> calls;
  calls.push_back(call_write_1_.get());
  calls.push_back(call_read_1_.get());
  calls.push_back(call_write_2_.get());
  calls.push_back(call_read_2_.get());

  std::vector<std::unique_ptr<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>> groups =
      test_generator_.SplitChannelCallsIntoGroups(calls);

  EXPECT_EQ(groups.size(), 2u);
  EXPECT_EQ(groups[0]->size(), 1u);
  EXPECT_EQ(groups[0]->at(0).first, call_write_1_.get());
  EXPECT_EQ(groups[0]->at(0).second, call_read_1_.get());

  EXPECT_EQ(groups[1]->size(), 1u);
  EXPECT_EQ(groups[1]->at(0).first, call_write_2_.get());
  EXPECT_EQ(groups[1]->at(0).second, call_read_2_.get());
}

TEST_F(TestGeneratorTest, GenerateProxy) {
  auto pair1 = std::pair<FidlCallInfo*, FidlCallInfo*>(call_write_1_.get(), call_read_1_.get());
  auto pair2 = std::pair<FidlCallInfo*, FidlCallInfo*>(call_write_2_.get(), call_read_2_.get());
  auto group_0 = std::make_unique<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>();
  group_0->push_back(pair1);
  group_0->push_back(pair2);

  auto pair3 = std::pair<FidlCallInfo*, FidlCallInfo*>(nullptr, call_event_.get());
  auto group_1 = std::make_unique<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>();
  group_1->push_back(pair3);

  std::vector<std::unique_ptr<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>> groups;
  groups.emplace_back(std::move(group_0));
  groups.emplace_back(std::move(group_1));

  ProxyPrinter pp(printer_, "/path/to/pkg", "Echo", "EchoString", groups);
  pp.GenerateProxyBooleans();

  // "bool received_1_0_ = false;\n" is skipped because group_1 has only one member
  EXPECT_EQ(os_.str(),
            "bool received_0_0_ = false;\n"
            "bool received_0_1_ = false;\n");

  os_.str("");
  pp.GenerateProxyGroupsDecl();
  EXPECT_EQ(os_.str(),
            "void group_0();\n\n"
            "void group_1();\n");
}

TEST_F(TestGeneratorTest, SplitChannelCallsIntoGroupsEvents) {
  std::vector<FidlCallInfo*> calls;
  calls.push_back(call_write_1_.get());
  calls.push_back(call_read_1_.get());
  calls.push_back(call_write_2_.get());
  calls.push_back(call_event_.get());
  calls.push_back(call_read_2_.get());

  std::vector<std::unique_ptr<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>> groups =
      test_generator_.SplitChannelCallsIntoGroups(calls);

  EXPECT_EQ(groups.size(), 2u);
  EXPECT_EQ(groups[0]->size(), 1u);
  EXPECT_EQ(groups[0]->at(0).first, call_write_1_.get());
  EXPECT_EQ(groups[0]->at(0).second, call_read_1_.get());

  EXPECT_EQ(groups[1]->size(), 2u);
  EXPECT_EQ(groups[1]->at(0).first, call_write_2_.get());
  EXPECT_EQ(groups[1]->at(0).second, call_read_2_.get());
  EXPECT_EQ(groups[1]->at(1).first, nullptr);
  EXPECT_EQ(groups[1]->at(1).second, call_event_.get());
}

TEST_F(TestGeneratorTest, SplitChannelCallsIntoGroupsFireAndForget) {
  std::vector<FidlCallInfo*> calls;
  calls.push_back(call_fire_and_forget_.get());
  calls.push_back(call_write_1_.get());
  calls.push_back(call_write_2_.get());
  calls.push_back(call_read_2_.get());
  calls.push_back(call_read_1_.get());

  std::vector<std::unique_ptr<std::vector<std::pair<FidlCallInfo*, FidlCallInfo*>>>> groups =
      test_generator_.SplitChannelCallsIntoGroups(calls);

  EXPECT_EQ(groups.size(), 2u);
  EXPECT_EQ(groups[0]->size(), 1u);
  EXPECT_EQ(groups[0]->at(0).first, call_fire_and_forget_.get());
  EXPECT_EQ(groups[0]->at(0).second, nullptr);

  EXPECT_EQ(groups[1]->size(), 2u);
  EXPECT_EQ(groups[1]->at(0).first, call_write_1_.get());
  EXPECT_EQ(groups[1]->at(0).second, call_read_1_.get());
  EXPECT_EQ(groups[1]->at(1).first, call_write_2_.get());
  EXPECT_EQ(groups[1]->at(1).second, call_read_2_.get());
}

}  // namespace fidlcat
