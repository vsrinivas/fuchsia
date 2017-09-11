// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/internal/connector.h"
#include "lib/fidl/cpp/bindings/internal/message_header_validator.h"
#include "lib/fidl/cpp/bindings/internal/router.h"
#include "lib/fidl/cpp/bindings/internal/validation_errors.h"
#include "lib/fidl/cpp/bindings/message.h"
#include "lib/fidl/cpp/bindings/tests/validation_test_input_parser.h"
#include "lib/fidl/cpp/bindings/tests/validation_util.h"
#include "lib/fxl/macros.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "mojo/public/cpp/test_support/test_support.h"
#include "mojo/public/cpp/utility/run_loop.h"
#include "lib/fidl/compiler/interfaces/tests/validation_test_interfaces.fidl.h"

namespace fidl {

using internal::MessageValidator;
using internal::MessageValidatorList;
using internal::ValidationError;
using internal::ValidationErrorToString;

namespace test {
namespace {

template <typename T>
void Append(std::vector<uint8_t>* data_vector, T data) {
  size_t pos = data_vector->size();
  data_vector->resize(pos + sizeof(T));
  memcpy(&(*data_vector)[pos], &data, sizeof(T));
}

bool TestInputParser(const std::string& input,
                     bool expected_result,
                     const std::vector<uint8_t>& expected_data,
                     size_t expected_num_handles) {
  std::vector<uint8_t> data;
  size_t num_handles;
  std::string error_message;

  bool result =
      ParseValidationTestInput(input, &data, &num_handles, &error_message);
  if (expected_result) {
    if (result && error_message.empty() && expected_data == data &&
        expected_num_handles == num_handles) {
      return true;
    }

    // Compare with an empty string instead of checking |error_message.empty()|,
    // so that the message will be printed out if the two are not equal.
    EXPECT_EQ(std::string(), error_message);
    EXPECT_EQ(expected_data, data);
    EXPECT_EQ(expected_num_handles, num_handles);
    return false;
  }

  EXPECT_FALSE(error_message.empty());
  return !result && !error_message.empty();
}

void RunValidationTests(const std::string& prefix,
                        const MessageValidatorList& validators,
                        MessageReceiver* test_message_receiver) {
  std::vector<std::string> tests = validation_util::GetMatchingTests(prefix);

  for (size_t i = 0; i < tests.size(); ++i) {
    std::string expected;
    std::vector<uint8_t> data;
    size_t num_handles;
    ASSERT_TRUE(validation_util::ReadTestCase(tests[i], &data, &num_handles,
                                              &expected));

    Message message;
    message.AllocUninitializedData(data.size());
    if (!data.empty())
      memcpy(message.mutable_data(), &data[0], data.size());
    message.mutable_handles()->resize(num_handles);

    std::string actual;
    auto result = RunValidatorsOnMessage(validators, &message, nullptr);
    if (result == ValidationError::NONE) {
      ignore_result(test_message_receiver->Accept(&message));
      actual = "PASS";
    } else {
      actual = ValidationErrorToString(result);
    }

    EXPECT_EQ(expected, actual) << "failed test: " << tests[i];
  }
}

class DummyMessageReceiver : public MessageReceiver {
 public:
  bool Accept(Message* message) override {
    return true;  // Any message is OK.
  }
};

class ValidationIntegrationTest : public testing::Test {
 public:
  ValidationIntegrationTest() : test_message_receiver_(nullptr) {}

  ~ValidationIntegrationTest() override {}

  void SetUp() override {
    mx::channel tester_endpoint;
    ASSERT_EQ(MX_OK,
              CreateMessagePipe(nullptr, &tester_endpoint, &testee_endpoint_));
    test_message_receiver_ =
        new TestMessageReceiver(this, tester_endpoint.Pass());
  }

  void TearDown() override {
    delete test_message_receiver_;
    test_message_receiver_ = nullptr;

    // Make sure that the other end receives the OnConnectionError()
    // notification.
    PumpMessages();
  }

  MessageReceiver* test_message_receiver() { return test_message_receiver_; }

  mx::channel testee_endpoint() { return testee_endpoint_.Pass(); }

 private:
  class TestMessageReceiver : public MessageReceiver {
   public:
    TestMessageReceiver(ValidationIntegrationTest* owner, mx::channel handle)
        : owner_(owner), connector_(handle.Pass()) {
      connector_.set_enforce_errors_from_incoming_receiver(false);
    }
    ~TestMessageReceiver() override {}

    bool Accept(Message* message) override {
      bool rv = connector_.Accept(message);
      owner_->PumpMessages();
      return rv;
    }

   public:
    ValidationIntegrationTest* owner_;
    fidl::internal::Connector connector_;
  };

  void PumpMessages() { loop_.RunUntilIdle(); }

  RunLoop loop_;
  TestMessageReceiver* test_message_receiver_;
  mx::channel testee_endpoint_;
};

class IntegrationTestInterfaceImpl : public IntegrationTestInterface {
 public:
  ~IntegrationTestInterfaceImpl() override {}

  void Method0(BasicStructPtr param0,
               const Method0Callback& callback) override {
    callback.Run(Array<uint8_t>::New(0u));
  }
};

class FailingValidator : public fidl::internal::MessageValidator {
 public:
  explicit FailingValidator(ValidationError err) : err_(err) {}
  ValidationError Validate(const Message* message, std::string* err) override {
    return err_;
  }

 private:
  ValidationError err_;
};

TEST(ValidationTest, InputParser) {
  {
    // The parser, as well as Append() defined above, assumes that this code is
    // running on a little-endian platform. Test whether that is true.
    uint16_t x = 1;
    ASSERT_EQ(1, *(reinterpret_cast<char*>(&x)));
  }
  {
    // Test empty input.
    std::string input;
    std::vector<uint8_t> expected;

    EXPECT_TRUE(TestInputParser(input, true, expected, 0));
  }
  {
    // Test input that only consists of comments and whitespaces.
    std::string input = "    \t  // hello world \n\r \t// the answer is 42   ";
    std::vector<uint8_t> expected;

    EXPECT_TRUE(TestInputParser(input, true, expected, 0));
  }
  {
    std::string input =
        "[u1]0x10// hello world !! \n\r  \t [u2]65535 \n"
        "[u4]65536 [u8]0xFFFFFFFFFFFFFFFF 0 0Xff";
    std::vector<uint8_t> expected;
    Append(&expected, static_cast<uint8_t>(0x10));
    Append(&expected, static_cast<uint16_t>(65535));
    Append(&expected, static_cast<uint32_t>(65536));
    Append(&expected, static_cast<uint64_t>(0xffffffffffffffff));
    Append(&expected, static_cast<uint8_t>(0));
    Append(&expected, static_cast<uint8_t>(0xff));

    EXPECT_TRUE(TestInputParser(input, true, expected, 0));
  }
  {
    std::string input = "[s8]-0x800 [s1]-128\t[s2]+0 [s4]-40";
    std::vector<uint8_t> expected;
    Append(&expected, -static_cast<int64_t>(0x800));
    Append(&expected, static_cast<int8_t>(-128));
    Append(&expected, static_cast<int16_t>(0));
    Append(&expected, static_cast<int32_t>(-40));

    EXPECT_TRUE(TestInputParser(input, true, expected, 0));
  }
  {
    std::string input = "[b]00001011 [b]10000000  // hello world\r [b]00000000";
    std::vector<uint8_t> expected;
    Append(&expected, static_cast<uint8_t>(11));
    Append(&expected, static_cast<uint8_t>(128));
    Append(&expected, static_cast<uint8_t>(0));

    EXPECT_TRUE(TestInputParser(input, true, expected, 0));
  }
  {
    std::string input = "[f]+.3e9 [d]-10.03";
    std::vector<uint8_t> expected;
    Append(&expected, +.3e9f);
    Append(&expected, -10.03);

    EXPECT_TRUE(TestInputParser(input, true, expected, 0));
  }
  {
    std::string input = "[dist4]foo 0 [dist8]bar 0 [anchr]foo [anchr]bar";
    std::vector<uint8_t> expected;
    Append(&expected, static_cast<uint32_t>(14));
    Append(&expected, static_cast<uint8_t>(0));
    Append(&expected, static_cast<uint64_t>(9));
    Append(&expected, static_cast<uint8_t>(0));

    EXPECT_TRUE(TestInputParser(input, true, expected, 0));
  }
  {
    std::string input = "// This message has handles! \n[handles]50 [u8]2";
    std::vector<uint8_t> expected;
    Append(&expected, static_cast<uint64_t>(2));

    EXPECT_TRUE(TestInputParser(input, true, expected, 50));
  }

  // Test some failure cases.
  {
    const char* error_inputs[] = {"/ hello world",
                                  "[u1]x",
                                  "[u2]-1000",
                                  "[u1]0x100",
                                  "[s2]-0x8001",
                                  "[b]1",
                                  "[b]1111111k",
                                  "[dist4]unmatched",
                                  "[anchr]hello [dist8]hello",
                                  "[dist4]a [dist4]a [anchr]a",
                                  "[dist4]a [anchr]a [dist4]a [anchr]a",
                                  "0 [handles]50",
                                  nullptr};

    for (size_t i = 0; error_inputs[i]; ++i) {
      std::vector<uint8_t> expected;
      if (!TestInputParser(error_inputs[i], false, expected, 0))
        ADD_FAILURE() << "Unexpected test result for: " << error_inputs[i];
    }
  }
}

TEST(ValidationTest, Conformance) {
  DummyMessageReceiver dummy_receiver;
  MessageValidatorList validators;
  validators.push_back(std::unique_ptr<MessageValidator>(
      new fidl::internal::MessageHeaderValidator));
  validators.push_back(std::unique_ptr<MessageValidator>(
      new ConformanceTestInterface::RequestValidator_));

  RunValidationTests("conformance_", validators, &dummy_receiver);
}

// This test is similar to Conformance test but its goal is specifically
// do bounds-check testing of message validation. For example we test the
// detection of off-by-one errors in method ordinals.
TEST(ValidationTest, BoundsCheck) {
  DummyMessageReceiver dummy_receiver;
  MessageValidatorList validators;
  validators.push_back(std::unique_ptr<MessageValidator>(
      new fidl::internal::MessageHeaderValidator));
  validators.push_back(std::unique_ptr<MessageValidator>(
      new BoundsCheckTestInterface::RequestValidator_));

  RunValidationTests("boundscheck_", validators, &dummy_receiver);
}

// This test is similar to the Conformance test but for responses.
TEST(ValidationTest, ResponseConformance) {
  DummyMessageReceiver dummy_receiver;
  MessageValidatorList validators;
  validators.push_back(std::unique_ptr<MessageValidator>(
      new fidl::internal::MessageHeaderValidator));
  validators.push_back(std::unique_ptr<MessageValidator>(
      new ConformanceTestInterface::ResponseValidator_));

  RunValidationTests("resp_conformance_", validators, &dummy_receiver);
}

// This test is similar to the BoundsCheck test but for responses.
TEST(ValidationTest, ResponseBoundsCheck) {
  DummyMessageReceiver dummy_receiver;
  MessageValidatorList validators;
  validators.push_back(std::unique_ptr<MessageValidator>(
      new fidl::internal::MessageHeaderValidator));
  validators.push_back(std::unique_ptr<MessageValidator>(
      new BoundsCheckTestInterface::ResponseValidator_));

  RunValidationTests("resp_boundscheck_", validators, &dummy_receiver);
}

// Test that InterfacePtr<X> applies the correct validators and they don't
// conflict with each other:
//   - MessageHeaderValidator
//   - X::ResponseValidator_
TEST_F(ValidationIntegrationTest, InterfacePtr) {
  IntegrationTestInterfacePtr interface_ptr =
      IntegrationTestInterfacePtr::Create(
          InterfaceHandle<IntegrationTestInterface>(testee_endpoint(), 0u));
  interface_ptr.internal_state()->router_for_testing()->EnableTestingMode();

  fidl::internal::MessageValidatorList validators;
  validators.push_back(std::unique_ptr<MessageValidator>(
      new fidl::internal::MessageHeaderValidator));
  validators.push_back(std::unique_ptr<MessageValidator>(
      new typename IntegrationTestInterface::ResponseValidator_));

  RunValidationTests("integration_intf_resp", validators,
                     test_message_receiver());
  RunValidationTests("integration_msghdr", validators, test_message_receiver());
}

// Test that Binding<X> applies the correct validators and they don't
// conflict with each other:
//   - MessageHeaderValidator
//   - X::RequestValidator_
TEST_F(ValidationIntegrationTest, Binding) {
  IntegrationTestInterfaceImpl interface_impl;
  Binding<IntegrationTestInterface> binding(
      &interface_impl,
      InterfaceRequest<IntegrationTestInterface>(testee_endpoint().Pass()));
  binding.internal_router()->EnableTestingMode();

  fidl::internal::MessageValidatorList validators;
  validators.push_back(std::unique_ptr<MessageValidator>(
      new fidl::internal::MessageHeaderValidator));
  validators.push_back(std::unique_ptr<MessageValidator>(
      new typename IntegrationTestInterface::RequestValidator_));

  RunValidationTests("integration_intf_rqst", validators,
                     test_message_receiver());
  RunValidationTests("integration_msghdr", validators, test_message_receiver());
}

// Test pointer validation (specifically, that the encoded offset is 32-bit)
TEST(ValidationTest, ValidateEncodedPointer) {
  uint64_t offset;

  offset = 0ULL;
  EXPECT_TRUE(fidl::internal::ValidateEncodedPointer(&offset));

  offset = 1ULL;
  EXPECT_TRUE(fidl::internal::ValidateEncodedPointer(&offset));

  // offset must be <= 32-bit.
  offset = std::numeric_limits<uint32_t>::max() + 1ULL;
  EXPECT_FALSE(fidl::internal::ValidateEncodedPointer(&offset));
}

TEST(ValidationTest, RunValidatorsOnMessageTest) {
  Message msg;
  fidl::internal::MessageValidatorList validators;

  validators.push_back(std::unique_ptr<MessageValidator>(
      new fidl::internal::PassThroughValidator));
  EXPECT_EQ(ValidationError::NONE,
            RunValidatorsOnMessage(validators, &msg, nullptr));

  validators.push_back(std::unique_ptr<MessageValidator>(
      new FailingValidator(ValidationError::MESSAGE_HEADER_INVALID_FLAGS)));
  EXPECT_EQ(ValidationError::MESSAGE_HEADER_INVALID_FLAGS,
            RunValidatorsOnMessage(validators, &msg, nullptr));

  validators.insert(validators.begin(),
                    std::unique_ptr<MessageValidator>(
                        new FailingValidator(ValidationError::ILLEGAL_HANDLE)));
  EXPECT_EQ(ValidationError::ILLEGAL_HANDLE,
            RunValidatorsOnMessage(validators, &msg, nullptr));
}

// Tests the IsValidValue() function generated for BasicEnum.
TEST(EnumValueValidationTest, BasicEnum) {
  // BasicEnum can have -3,0,1,10 as possible integral values.
  EXPECT_FALSE(BasicEnum_IsValidValue(static_cast<BasicEnum>(-4)));
  EXPECT_TRUE(BasicEnum_IsValidValue(static_cast<BasicEnum>(-3)));
  EXPECT_FALSE(BasicEnum_IsValidValue(static_cast<BasicEnum>(-2)));
  EXPECT_FALSE(BasicEnum_IsValidValue(static_cast<BasicEnum>(-1)));
  EXPECT_TRUE(BasicEnum_IsValidValue(static_cast<BasicEnum>(0)));
  EXPECT_TRUE(BasicEnum_IsValidValue(static_cast<BasicEnum>(1)));
  EXPECT_FALSE(BasicEnum_IsValidValue(static_cast<BasicEnum>(2)));
  EXPECT_FALSE(BasicEnum_IsValidValue(static_cast<BasicEnum>(9)));
  // In the mojom, we represent this value as hex (0xa).
  EXPECT_TRUE(BasicEnum_IsValidValue(static_cast<BasicEnum>(10)));
  EXPECT_FALSE(BasicEnum_IsValidValue(static_cast<BasicEnum>(11)));
}

// Tests the IsValidValue() method generated for StructWithEnum.
TEST(EnumValueValidationTest, EnumWithin) {
  // StructWithEnum::EnumWithin can have [0,4] as possible integral values.
  EXPECT_FALSE(StructWithEnum::EnumWithin_IsValidValue(
      static_cast<StructWithEnum::EnumWithin>(-1)));
  EXPECT_TRUE(StructWithEnum::EnumWithin_IsValidValue(
      static_cast<StructWithEnum::EnumWithin>(0)));
  EXPECT_TRUE(StructWithEnum::EnumWithin_IsValidValue(
      static_cast<StructWithEnum::EnumWithin>(1)));
  EXPECT_TRUE(StructWithEnum::EnumWithin_IsValidValue(
      static_cast<StructWithEnum::EnumWithin>(2)));
  EXPECT_TRUE(StructWithEnum::EnumWithin_IsValidValue(
      static_cast<StructWithEnum::EnumWithin>(3)));
  EXPECT_FALSE(StructWithEnum::EnumWithin_IsValidValue(
      static_cast<StructWithEnum::EnumWithin>(4)));
}

}  // namespace
}  // namespace test
}  // namespace fidl
