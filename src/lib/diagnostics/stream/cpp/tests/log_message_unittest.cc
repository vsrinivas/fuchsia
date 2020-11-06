// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fit/optional.h>

#include <gtest/gtest.h>
#include <src/lib/diagnostics/stream/cpp/log_message.h>
#include <src/lib/fsl/vmo/strings.h>
#include <src/lib/fxl/strings/string_printf.h>

using diagnostics::stream::ConvertFormattedContentToLogMessages;
using fuchsia::diagnostics::FormattedContent;
using fuchsia::logger::LogMessage;

namespace {

TEST(LogMessage, Empty) {
  FormattedContent content;

  ASSERT_TRUE(fsl::VmoFromString("[]", &content.json()));

  auto messages = ConvertFormattedContentToLogMessages(std::move(content));
  ASSERT_TRUE(messages.is_ok());
  EXPECT_EQ(0u, messages.value().size());
}

TEST(LogMessage, WrongType) {
  FormattedContent content;

  EXPECT_TRUE(ConvertFormattedContentToLogMessages(std::move(content)).is_error());
}

struct ValidationTestCase {
  std::string input;

  // If set, check that the conversion function returned this error instead of a vector.
  fit::optional<std::string> expected_overall_error = fit::nullopt;

  // If set, assert on the exact number of messages returned.
  fit::optional<int> expected_count = fit::nullopt;

  // If set, assert that message has the given error.
  fit::optional<std::string> expected_error = fit::nullopt;

  // If set, assert that message is OK and matches the given value.
  fit::optional<std::string> expected_message = fit::nullopt;

  // If set, assert that message is OK and tags match the given value.
  fit::optional<std::vector<std::string>> expected_tags = fit::nullopt;

  // If set, assert that message is OK and dropped logs match the given value.
  fit::optional<uint32_t> dropped_logs = fit::nullopt;
};

void RunValidationCases(std::vector<ValidationTestCase> cases) {
  for (const auto& test_case : cases) {
    FormattedContent content;

    ASSERT_TRUE(fsl::VmoFromString(test_case.input, &content.json()));
    auto results = ConvertFormattedContentToLogMessages(std::move(content));
    EXPECT_EQ(!test_case.expected_overall_error.has_value(), results.is_ok()) << test_case.input;
    if (test_case.expected_overall_error.has_value()) {
      ASSERT_TRUE(results.is_error());
      EXPECT_EQ(test_case.expected_overall_error.value(), results.error());
    } else if (results.is_ok()) {
      if (test_case.expected_count.has_value()) {
        EXPECT_EQ(static_cast<size_t>(test_case.expected_count.value()), results.value().size())
            << test_case.input;
      }

      if (test_case.expected_message.has_value()) {
        ASSERT_LE(1u, results.value().size())
            << "Must have at least one output to check expected message against";
        for (const auto& res : results.value()) {
          ASSERT_TRUE(res.is_ok()) << test_case.input;
          EXPECT_EQ(test_case.expected_message.value(), res.value().msg) << test_case.input;
        }
      }
      if (test_case.expected_tags.has_value()) {
        ASSERT_LE(1u, results.value().size())
            << "Must have at least one output to check expected tags against";
        for (const auto& res : results.value()) {
          ASSERT_TRUE(res.is_ok()) << test_case.input;
          ASSERT_EQ(test_case.expected_tags.value().size(), res.value().tags.size())
              << test_case.input;
          for (size_t i = 0; i < res.value().tags.size(); i++) {
            EXPECT_EQ(test_case.expected_tags.value()[i], res.value().tags[i])
                << "tag index " << i << " " << test_case.input;
          }
        }
      }
      if (test_case.dropped_logs.has_value()) {
        ASSERT_LE(1u, results.value().size())
            << "Must have at least one output to check expected dropped logs";
        for (const auto& res : results.value()) {
          ASSERT_TRUE(res.is_ok()) << test_case.input;
          EXPECT_EQ(test_case.dropped_logs.value(), res.value().dropped_logs) << test_case.input;
        }
      }
      if (test_case.expected_error.has_value()) {
        ASSERT_LE(1u, results.value().size())
            << "Must have at least one output to check expected errors";
        for (const auto& res : results.value()) {
          ASSERT_TRUE(res.is_error()) << test_case.input;
          EXPECT_EQ(test_case.expected_error.value(), res.error()) << test_case.input;
        }
      }
    }
  }
}

const char PAYLOAD_TEMPLATE[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "Info"
    },
    "payload": %s
  }
]
)JSON";

const char VALID_MESSAGE_FOR_SEVERITY[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "%s"
    },
    "payload": {
      "root": {
        "message": "Hello world",
        "pid": 200,
        "tid": 300,
        "tag": "a",
        "arbitrary_kv": 1024
      }
    }
  }
]
)JSON";

const char TWO_FLAT_VALID_INFO_MESSAGES[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "Info"
    },
    "payload": {
      "message": "Hello world",
      "pid": 200,
      "tid": 300,
      "tag": "a",
      "arbitrary_kv": 1024
    }
  },
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "Info"
    },
    "payload": {
      "message": "Hello world",
      "pid": 200,
      "tid": 300,
      "tag": "a",
      "arbitrary_kv": 1024
    }
  }
]
)JSON";

TEST(LogMessage, Valid) {
  // Ensure that both flat and nested (under "root") messages work.
  FormattedContent content;
  ASSERT_TRUE(
      fsl::VmoFromString(fxl::StringPrintf(VALID_MESSAGE_FOR_SEVERITY, "Info"), &content.json()));
  auto one_message_result = ConvertFormattedContentToLogMessages(std::move(content));

  content = {};
  ASSERT_TRUE(fsl::VmoFromString(TWO_FLAT_VALID_INFO_MESSAGES, &content.json()));
  auto two_message_result = ConvertFormattedContentToLogMessages(std::move(content));

  ASSERT_TRUE(one_message_result.is_ok());
  ASSERT_TRUE(two_message_result.is_ok());

  std::vector<fit::result<LogMessage, std::string>> messages;
  messages.insert(messages.end(), one_message_result.value().begin(),
                  one_message_result.value().end());
  messages.insert(messages.end(), two_message_result.value().begin(),
                  two_message_result.value().end());
  ASSERT_EQ(3u, messages.size());
  for (const auto& val : messages) {
    ASSERT_TRUE(val.is_ok());
    EXPECT_EQ("Hello world arbitrary_kv=1024", val.value().msg);
    EXPECT_EQ(200u, val.value().pid);
    EXPECT_EQ(300u, val.value().tid);
    ASSERT_EQ(1u, val.value().tags.size());
    EXPECT_EQ("a", val.value().tags[0]);
    EXPECT_EQ(1000u, val.value().time);
    EXPECT_EQ(0u, val.value().dropped_logs);
    EXPECT_EQ(static_cast<int32_t>(fuchsia::logger::LogLevelFilter::INFO), val.value().severity);
  }
}

TEST(LogMessage, ValidSeverityTests) {
  struct TestCase {
    std::string input;
    fuchsia::logger::LogLevelFilter severity;
  };

  std::vector<TestCase> cases;
  cases.emplace_back(TestCase{
      .input = fxl::StringPrintf(VALID_MESSAGE_FOR_SEVERITY, "Info"),
      .severity = fuchsia::logger::LogLevelFilter::INFO,
  });
  cases.emplace_back(TestCase{
      .input = fxl::StringPrintf(VALID_MESSAGE_FOR_SEVERITY, "info"),
      .severity = fuchsia::logger::LogLevelFilter::INFO,
  });
  cases.emplace_back(TestCase{
      .input = fxl::StringPrintf(VALID_MESSAGE_FOR_SEVERITY, "INFO"),
      .severity = fuchsia::logger::LogLevelFilter::INFO,
  });
  cases.emplace_back(TestCase{
      .input = fxl::StringPrintf(VALID_MESSAGE_FOR_SEVERITY, "TRACE"),
      .severity = fuchsia::logger::LogLevelFilter::TRACE,
  });
  cases.emplace_back(TestCase{
      .input = fxl::StringPrintf(VALID_MESSAGE_FOR_SEVERITY, "DEBUG"),
      .severity = fuchsia::logger::LogLevelFilter::DEBUG,
  });
  cases.emplace_back(TestCase{
      .input = fxl::StringPrintf(VALID_MESSAGE_FOR_SEVERITY, "WARN"),
      .severity = fuchsia::logger::LogLevelFilter::WARN,
  });
  cases.emplace_back(TestCase{
      .input = fxl::StringPrintf(VALID_MESSAGE_FOR_SEVERITY, "ERROR"),
      .severity = fuchsia::logger::LogLevelFilter::ERROR,
  });
  cases.emplace_back(TestCase{
      .input = fxl::StringPrintf(VALID_MESSAGE_FOR_SEVERITY, "FATAL"),
      .severity = fuchsia::logger::LogLevelFilter::FATAL,
  });
  cases.emplace_back(TestCase{
      .input = fxl::StringPrintf(VALID_MESSAGE_FOR_SEVERITY, "unknown"),
      .severity = fuchsia::logger::LogLevelFilter::INFO,
  });

  for (const auto& test_case : cases) {
    FormattedContent content;

    ASSERT_TRUE(fsl::VmoFromString(test_case.input, &content.json()));
    auto messages = ConvertFormattedContentToLogMessages(std::move(content));
    ASSERT_TRUE(messages.is_ok());
    ASSERT_EQ(1u, messages.value().size());
    ASSERT_TRUE(messages.value()[0].is_ok());
    EXPECT_EQ(static_cast<int32_t>(test_case.severity), messages.value()[0].value().severity);
  }
}

const char META_TEMPLATE[] = R"JSON(
[
  {
    "metadata": {
      %s
    },
    "payload": {
      "root": {
        "message": "Hello world",
        "pid": 200,
        "tid": 300,
        "arbitrary_kv": 1024
      }
    }
  }
]
)JSON";

const char MONIKER_TEMPLATE[] = R"JSON(
[
  {
    "moniker": "%s",
    "metadata": {
      "timestamp": 1000,
      "severity": "INFO"
    },
    "payload": {
      "root": {
        "message": "Hello world",
        "pid": 200,
        "tid": 300,
        "arbitrary_kv": 1024
      }
    }
  }
]
)JSON";

TEST(LogMessage, MetadataValidation) {
  std::vector<ValidationTestCase> cases;

  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(META_TEMPLATE, R"("severity": "INFO", "timestamp": 1000)"),
      .expected_count = 1});
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(META_TEMPLATE, R"("severity": "INFO", "timestamp": "string")"),
      .expected_error = "Expected metadata.timestamp key",
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(META_TEMPLATE, R"("severity": "INFO")"),
      .expected_error = "Expected metadata.timestamp key",
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(META_TEMPLATE, R"("timestamp": 1000)"),
      .expected_error = "Expected metadata.severity key",
  });

  RunValidationCases(std::move(cases));
}

const char ROOT_TEMPLATE[] = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "Info"
    },
    "payload": {
      "root": %s
    }
  }
]
)JSON";

TEST(LogMessage, PayloadValidation) {
  std::vector<ValidationTestCase> cases;
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(ROOT_TEMPLATE, R"({"message": "Hello"})"),
      .expected_count = 1,
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(ROOT_TEMPLATE, R"("invalid type")"),
      .expected_error = "Expected payload.root to be an object if present",
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(ROOT_TEMPLATE, R"(1000)"),
      .expected_error = "Expected payload.root to be an object if present",
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"message": "Hello"})"),
      .expected_count = 1,
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"("invalid type")"),
      .expected_error = "Expected metadata and payload objects",
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"(1000)"),
      .expected_error = "Expected metadata and payload objects",
  });

  RunValidationCases(std::move(cases));
}

TEST(LogMessage, JsonValidation) {
  std::vector<ValidationTestCase> cases;
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(ROOT_TEMPLATE, R"({"message": "Hello"})"),
      .expected_count = 1,
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(ROOT_TEMPLATE, R"({"message": "Hello"},)"),
      .expected_overall_error =
          "Failed to parse content as JSON. Offset 139: Missing a name for object member.",
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"message": "Hello"})"),
      .expected_count = 1,
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"message": "Hello"},)"),
      .expected_overall_error =
          "Failed to parse content as JSON. Offset 121: Missing a name for object member.",
  });

  RunValidationCases(std::move(cases));
}

TEST(LogMessage, FileValidation) {
  std::vector<ValidationTestCase> cases;
  cases.emplace_back(ValidationTestCase{
      .input = "[]",
      .expected_count = 0,
  });
  cases.emplace_back(ValidationTestCase{
      .input = "[3]",
      .expected_count = 1,
      .expected_error = "Value is not an object",
  });
  cases.emplace_back(ValidationTestCase{
      .input = R"(["a", "b"])",
      .expected_count = 2,
      .expected_error = "Value is not an object",
  });
  cases.emplace_back(ValidationTestCase{
      .input = R"({"payload": {}})",
      .expected_overall_error = "Expected content to contain an array",
  });

  RunValidationCases(std::move(cases));
}

TEST(LogMessage, MessageFormatting) {
  std::vector<ValidationTestCase> cases;
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"message": "Hello, world"})"),
      .expected_message = "Hello, world",
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"message": "Hello, world", "kv": "ok"})"),
      .expected_message = "Hello, world kv=ok",
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(
          PAYLOAD_TEMPLATE,
          R"({"message": "Hello, world", "int": -5, "intp": 5, "repeat": 2, "repeat": 2, "uint": 9223400000000000000, "float": 5.25})"),
      .expected_message =
          "Hello, world int=-5 intp=5 repeat=2 repeat=2 uint=9223400000000000000 float=5.25",
  });

  RunValidationCases(std::move(cases));
}

TEST(LogMessage, Tags) {
  std::vector<ValidationTestCase> cases;
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"tag": "hello"})"),
      .expected_tags = std::vector<std::string>{"hello"},
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"tags": "hello"})"),
      .expected_tags = std::vector<std::string>{"hello"},
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"tag": "hello", "tag": "world"})"),
      .expected_tags = std::vector<std::string>{"hello", "world"},
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"tags": "hello", "tags": "world"})"),
      .expected_tags = std::vector<std::string>{"hello", "world"},
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"tags": ["hello", "world"]})"),
      .expected_tags = std::vector<std::string>{"hello", "world"},
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"tag": ["hello", "world"]})"),
      .expected_error = "Tag field must contain a single string value",
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"tags": ["hello", 3]})"),
      .expected_error = "Tags array must contain strings",
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"tag": "hello", "tag": 3})"),
      .expected_error = "Tag field must contain a single string value",
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(PAYLOAD_TEMPLATE, R"({"tags": "hello", "tags": 3})"),
      .expected_error = "Tags must be a string or array of strings",
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(MONIKER_TEMPLATE, "test.cmx"),
      .expected_tags = std::vector<std::string>{"test.cmx"},
  });

  RunValidationCases(std::move(cases));
}

TEST(LogMessage, DroppedLogs) {
  std::vector<ValidationTestCase> cases;
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(META_TEMPLATE,
                                 R"("timestamp": 1000, "severity": "INFO", "errors": ["test"])"),
      .dropped_logs = 0,
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(
          META_TEMPLATE,
          R"("timestamp": 1000, "severity": "INFO", "errors": [{"dropped_logs": {"count": 100}}])"),
      .dropped_logs = 100,
  });
  cases.emplace_back(ValidationTestCase{
      .input = fxl::StringPrintf(META_TEMPLATE,
                                 R"(
              "timestamp": 1000,
              "severity": "INFO",
              "errors": [
                {"dropped_logs": {"count": 100}},
                {"dropped_logs": {"count": 200}}
              ])"),
      .dropped_logs = 300,
  });

  RunValidationCases(std::move(cases));
}

}  // namespace
