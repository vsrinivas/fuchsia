// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <lib/stdcompat/optional.h>

#include <cinttypes>

#include <gtest/gtest.h>
#include <src/lib/diagnostics/accessor2logger/log_message.h>
#include <src/lib/fsl/vmo/strings.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "lib/syslog/cpp/log_level.h"
#include "src/lib/fxl/strings/join_strings.h"

using diagnostics::accessor2logger::ConvertFormattedContentToLogMessages;
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

std::string SeverityToString(const int32_t severity) {
  if (severity == syslog::LOG_TRACE) {
    return "TRACE";
  } else if (severity == syslog::LOG_DEBUG) {
    return "DEBUG";
  } else if (severity > syslog::LOG_DEBUG && severity < syslog::LOG_INFO) {
    return fxl::StringPrintf("VLOG(%d)", syslog::LOG_INFO - severity);
  } else if (severity == syslog::LOG_INFO) {
    return "INFO";
  } else if (severity == syslog::LOG_WARNING) {
    return "WARN";
  } else if (severity == syslog::LOG_ERROR) {
    return "ERROR";
  } else if (severity == syslog::LOG_FATAL) {
    return "FATAL";
  }
  return "INVALID";
}

TEST(LogMessage, MonikerStringification) {
  constexpr char kSampleLogPayload[] =
      R"JSON([
      {
        "data_source": "Logs",
        "metadata": {
          "errors": null,
          "component_url": "",
          "timestamp": 8331458424750,
          "severity": "INFO",
          "size_bytes": 2048,
          "tags": [],
          "pid": 1102869,
          "tid": 1102871,
          "file": "src/diagnostics/lib/cpp-log-decoder/test.cc",
          "line": 24,
          "dropped": 0
        },
        "moniker": "test/path/<test_moniker>",
        "payload": {
          "root": {
            "keys": {
              "user property": 5.2
            },
            "message": {
              "value": "test message"
            }
          }
        },
        "version": 1
      }
    ])JSON";
  fsl::SizedVmo vmo;
  fsl::VmoFromString(kSampleLogPayload, &vmo);
  fuchsia::diagnostics::FormattedContent content;
  fuchsia::mem::Buffer buffer;
  buffer.vmo = std::move(vmo.vmo());
  buffer.size = sizeof(kSampleLogPayload);
  content.set_json(std::move(buffer));
  auto messages =
      diagnostics::accessor2logger::ConvertFormattedContentToLogMessages(std::move(content))
          .take_value();
  auto message = messages[0].value();
  auto encoded_message =
      fxl::StringPrintf("[%05d.%03d][%05" PRIu64 "][%05" PRIu64 "][%s] %s: %s\n",
                        static_cast<int>(message.time / 1000000000ULL),
                        static_cast<int>((message.time / 1000000ULL) % 1000ULL), message.pid,
                        message.tid, fxl::JoinStrings(message.tags, ", ").c_str(),
                        SeverityToString(message.severity).c_str(), message.msg.c_str());
  ASSERT_TRUE(encoded_message.find("[<test_moniker>] INFO: "
                                   "[src/diagnostics/lib/cpp-log-decoder/test.cc(24)] test "
                                   "message user property=5.2") != std::string::npos);
}

TEST(LogMessage, LegacyHostEncoding) {
  constexpr char kSampleLogPayload[] =
      R"JSON([
      {
        "data_source": "Logs",
        "metadata": {
          "errors": null,
          "component_url": "",
          "timestamp": 8331458424750,
          "severity": "INFO",
          "size_bytes": 2048,
          "tags": [
            "some tag",
            "some other tag"
          ],
          "pid": 1102869,
          "tid": 1102871,
          "file": "src/diagnostics/lib/cpp-log-decoder/test.cc",
          "line": 24,
          "dropped": 0
        },
        "moniker": "<test_moniker>",
        "payload": {
          "root": {
            "keys": {
              "user property": 5.2
            },
            "message": {
              "value": "test message"
            }
          }
        },
        "version": 1
      }
    ])JSON";
  fsl::SizedVmo vmo;
  fsl::VmoFromString(kSampleLogPayload, &vmo);
  fuchsia::diagnostics::FormattedContent content;
  fuchsia::mem::Buffer buffer;
  buffer.vmo = std::move(vmo.vmo());
  buffer.size = sizeof(kSampleLogPayload);
  content.set_json(std::move(buffer));
  auto messages =
      diagnostics::accessor2logger::ConvertFormattedContentToHostLogMessages(std::move(content))
          .take_value();
  auto message = messages[0].value();
  auto encoded_message =
      fxl::StringPrintf("[%05d.%03d][%05" PRIu64 "][%05" PRIu64 "][%s] %s: %s\n",
                        static_cast<int>(message.time / 1000000000ULL),
                        static_cast<int>((message.time / 1000000ULL) % 1000ULL), message.pid,
                        message.tid, fxl::JoinStrings(message.tags, ", ").c_str(),
                        SeverityToString(message.severity).c_str(), message.msg.c_str());
  ASSERT_TRUE(encoded_message.find("[some tag, some other tag] INFO: "
                                   "[src/diagnostics/lib/cpp-log-decoder/test.cc(24)] test "
                                   "message user property=5.200000") != std::string::npos);
}

struct ValidationTestCase {
  std::string input;

  // If set, check that the conversion function returned this error instead of a vector.
  cpp17::optional<std::string> expected_overall_error = cpp17::nullopt;

  // If set, assert on the exact number of messages returned.
  cpp17::optional<int> expected_count = cpp17::nullopt;

  // If set, assert that message has the given error.
  cpp17::optional<std::string> expected_error = cpp17::nullopt;

  // If set, assert that message is OK and matches the given value.
  cpp17::optional<std::string> expected_message = cpp17::nullopt;

  // If set, assert that message is OK and tags match the given value.
  cpp17::optional<std::vector<std::string>> expected_tags = cpp17::nullopt;

  // If set, assert that message is OK and dropped logs match the given value.
  cpp17::optional<uint32_t> dropped_logs = cpp17::nullopt;
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
      "severity": "INFO"
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
      "severity": "%s",
      "pid": 200,
      "tid": 300,
      "tags": ["a"]
    },
    "payload": {
      "root": {
        "message": {
          "value": "Hello world"
        },
        "keys": {
          "arbitrary_kv": 1024
        }
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
      "severity": "INFO",
      "pid": 200,
      "tid": 300,
      "tags": ["a"]
    },
    "payload": {
      "root": {
        "message": {
          "value": "Hello world"
        },
        "keys": {
          "arbitrary_kv": 1024
        }
      }
    }
  },
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "INFO",
      "pid": 200,
      "tid": 300,
      "tags": ["a"]
    },
    "payload": {
      "root": {
        "message": {
            "value": "Hello world"
        },
        "keys": {
          "arbitrary_kv": 1024
        }
      }
    }
  }
]
)JSON";

TEST(LogMessage, Valid) {
  // Ensure that both flat and nested (under "root") messages work.
  FormattedContent content;
  ASSERT_TRUE(
      fsl::VmoFromString(fxl::StringPrintf(VALID_MESSAGE_FOR_SEVERITY, "INFO"), &content.json()));
  auto one_message_result = ConvertFormattedContentToLogMessages(std::move(content));

  content = {};
  ASSERT_TRUE(fsl::VmoFromString(TWO_FLAT_VALID_INFO_MESSAGES, &content.json()));
  auto two_message_result = ConvertFormattedContentToLogMessages(std::move(content));

  ASSERT_TRUE(one_message_result.is_ok());
  ASSERT_TRUE(two_message_result.is_ok());

  std::vector<fpromise::result<LogMessage, std::string>> messages;
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
      .input = fxl::StringPrintf(VALID_MESSAGE_FOR_SEVERITY, "INFO"),
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
      "pid": 200,
      "tid": 300,
      %s
    },
    "payload": {
      "root": {
        "message": {
          "value": "Hello world"
        },
        "keys": {
          "arbitrary_kv": 1024
        }
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
      "severity": "INFO",
      "pid": 200,
      "tid": 300
    },
    "payload": {
      "root": {
        "message": {
          "value": "Hello world"
        },
        "keys": {
          "arbitrary_kv": 1024
        }
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
      "severity": "INFO"
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
      .input = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "INFO",
      "file": "test.cc",
      "line": 420
    },
    "payload": {
      "root": {
        "message": {
          "value": "Hello, world"
        }
      }
    }
  }
]
)JSON",
      .expected_message = "[test.cc(420)] Hello, world",
  });
  cases.emplace_back(ValidationTestCase{
      .input = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "INFO"
    },
    "payload": {
      "root": {
        "message": {
          "value": "Hello, world"
        },
        "keys": {
          "kv": "ok"
        }
      }
    }
  }
]
)JSON",
      .expected_message = "Hello, world kv=ok",
  });
  cases.emplace_back(ValidationTestCase{
      .input = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "INFO"
    },
    "payload": {
      "root": {
        "message": {
          "value": "Hello, world"
        },
        "keys": {
          "int": -5,
          "intp": 5,
          "repeat": 2,
          "uint": 9223400000000000000,
          "float": 5.25
        }
      }
    }
  }
]
)JSON",
      .expected_message = "Hello, world int=-5 intp=5 repeat=2 uint=9223400000000000000 float=5.25",
  });

  RunValidationCases(std::move(cases));
}

TEST(LogMessage, Tags) {
  std::vector<ValidationTestCase> cases;
  cases.emplace_back(ValidationTestCase{
      .input = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "INFO",
      "tags": ["hello"]
    },
    "payload": {
      "root": {
        "message": {
          "value": ""
        }
      }
    }
  }
]
)JSON",
      .expected_tags = std::vector<std::string>{"hello"},
  });
  cases.emplace_back(ValidationTestCase{
      .input = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "INFO",
      "tags": "hello"
    },
    "payload": {
      "root": {
        "message": {
          "value": ""
        }
      }
    }
  }
]
)JSON",
      .expected_tags = std::vector<std::string>{"hello"},
  });
  cases.emplace_back(ValidationTestCase{
      .input = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "INFO",
      "tags": ["hello", "world"]
    },
    "payload": {
      "root": {
        "message": {
          "value": ""
        }
      }
    }
  }
]
)JSON",
      .expected_tags = std::vector<std::string>{"hello", "world"},
  });
  cases.emplace_back(ValidationTestCase{
      .input = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "INFO",
      "tags": ["hello", 3]
    },
    "payload": {
      "root": {
        "message": {
          "value": ""
        }
      }
    }
  }
]
)JSON",
      .expected_error = "Tags array must contain strings",
  });
  cases.emplace_back(ValidationTestCase{
      .input = R"JSON(
[
  {
    "metadata": {
      "timestamp": 1000,
      "severity": "INFO",
      "tags": 3
    },
    "payload": {
      "root": {
        "message": {
          "value": ""
        }
      }
    }
  }
]
)JSON",
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
