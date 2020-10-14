// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "converter.h"

#include <zircon/compiler.h>

#include <iterator>

#include <gtest/gtest.h>

#include "rapidjson/error/en.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"
#include "src/lib/fxl/strings/split_string.h"

namespace {

void CheckParseResult(rapidjson::ParseResult parse_result) {
  EXPECT_TRUE(parse_result) << "JSON parse error: "
                            << rapidjson::GetParseError_En(parse_result.Code()) << " (offset "
                            << parse_result.Offset() << ")";
}

void TestConverter(const char* json_input_string, rapidjson::Document* output,
                   bool product_versions_available = false) {
  rapidjson::Document input;
  CheckParseResult(input.Parse(json_input_string));

  ConverterArgs args;
  // Test a timestamp value that does not fit into a 32-bit int type.
  args.timestamp = 123004005006;
  args.masters = "example_masters";
  args.bots = "example_bots";
  args.log_url = "https://ci.example.com/build/100";
  args.use_test_guids = true;
  if (product_versions_available) {
    args.product_versions = "0.001.20.3";
  }
  Convert(&input, output, &args);

  // Check that the output serializes successfully as JSON.  The rapidjson
  // library allows rapidjson::Values to contain invalid JSON, such as NaN
  // or infinite floating point values, which are not allowed in JSON.
  rapidjson::StringBuffer buf;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buf);
  output->Accept(writer);
  EXPECT_TRUE(writer.IsComplete());
}

// This function checks that the JSON value |actual| is a number that is
// approximately equal to |expected|.
//
// This changes |actual| to be a placeholder string value so that later
// comparisons can ignore the numeric value.
//
// The reason for doing an approximate check is that rapidjson does not
// always preserve exact floating point numbers across a write+read+write:
// The last significant digit of a number will sometimes change across a
// read+write round-trip, even for JSON data that rapidjson previously
// wrote.
void AssertApproxEqual(rapidjson::Document* document, rapidjson::Value* actual, double expected) {
  ASSERT_TRUE(actual->IsNumber());
  double actual_val = actual->GetDouble();
  double tolerance = 1.0001;
  double expected_min = expected * tolerance;
  double expected_max = expected / tolerance;
  if (expected_min > expected_max) {
    // Handle negative values.
    std::swap(expected_min, expected_max);
  }
  EXPECT_TRUE(expected_min <= actual_val && actual_val <= expected_max)
      << "Got value " << actual_val << ", but expected value close to " << expected << " (between "
      << expected_min << " and " << expected_max << ")";
  actual->SetString("compared_elsewhere", document->GetAllocator());
}

std::vector<std::string> SplitLines(const char* str) {
  return fxl::SplitStringCopy(str, "\n", fxl::kKeepWhitespace, fxl::kSplitWantAll);
}

void PrintLines(const std::vector<std::string>& lines, size_t start, size_t end, char prefix) {
  for (size_t i = start; i < end; ++i) {
    printf("%c%s\n", prefix, lines[i].c_str());
  }
}

// Print a simple line-based diff comparing the given strings.  This uses a
// primitive diff algorithm that only discounts matching lines at the
// starts and ends of the string.
void PrintDiff(const char* str1, const char* str2) {
  std::vector<std::string> lines1 = SplitLines(str1);
  std::vector<std::string> lines2 = SplitLines(str2);
  size_t i = 0;
  size_t j = 0;
  // Searching from the start, find the first lines that differ.
  while (lines1[i] == lines2[i])
    ++i;
  // Searching from the end, find the first lines that differ.
  while (lines1[lines1.size() - j - 1] == lines2[lines2.size() - j - 1])
    ++j;
  // Print the common lines at the start.
  PrintLines(lines1, 0, i, ' ');
  // Print the differing lines.
  PrintLines(lines1, i, lines1.size() - j, '-');
  PrintLines(lines2, i, lines2.size() - j, '+');
  // Print the common lines at the end.
  PrintLines(lines1, lines1.size() - j, lines1.size(), ' ');
}

void AssertJsonEqual(const rapidjson::Document& doc1, const rapidjson::Document& doc2) {
  rapidjson::StringBuffer buf1;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer1(buf1);
  doc1.Accept(writer1);

  rapidjson::StringBuffer buf2;
  rapidjson::PrettyWriter<rapidjson::StringBuffer> writer2(buf2);
  doc2.Accept(writer2);

  EXPECT_EQ(doc1, doc2);
  if (doc1 != doc2) {
    printf("Comparison:\n");
    PrintDiff(buf1.GetString(), buf2.GetString());
  }
}

TEST(TestTools, SplitLines) {
  auto lines = SplitLines(" aa \n  bb\n\ncc \n");
  ASSERT_EQ(lines.size(), 5u);
  EXPECT_STREQ(lines[0].c_str(), " aa ");
  EXPECT_STREQ(lines[1].c_str(), "  bb");
  EXPECT_STREQ(lines[2].c_str(), "");
  EXPECT_STREQ(lines[3].c_str(), "cc ");
  EXPECT_STREQ(lines[4].c_str(), "");
}

// Test the basic case that covers multiple time units.
// This also covers converting spaces to underscores in
// the test name.
TEST(CatapultConverter, Convert) {
  const char* input_str = R"JSON(
[
    {
        "label": "ExampleNullSyscall",
        "test_suite": "my_test_suite",
        "values": [101.0, 102.0, 103.0, 104.0, 105.0],
        "unit": "nanoseconds"
    },
    {
        "label": "Example Other Test",
        "test_suite": "my_test_suite",
        "values": [200, 6, 100, 110],
        "unit": "ms"
    }
]
)JSON";

  const char* expected_output_str = R"JSON(
[
    {
        "guid": "dummy_guid_0",
        "type": "GenericSet",
        "values": [
            123004005006
        ]
    },
    {
        "guid": "dummy_guid_1",
        "type": "GenericSet",
        "values": [
            "example_bots"
        ]
    },
    {
        "guid": "dummy_guid_2",
        "type": "GenericSet",
        "values": [
            "example_masters"
        ]
    },
    {
        "guid": "dummy_guid_3",
        "type": "GenericSet",
        "values": [
            [
                "Build Log",
                "https://ci.example.com/build/100"
            ]
        ]
    },
    {
        "guid": "dummy_guid_4",
        "type": "GenericSet",
        "values": [
            "my_test_suite"
        ]
    },
    {
        "name": "ExampleNullSyscall",
        "unit": "ms_smallerIsBetter",
        "description": "",
        "diagnostics": {
            "pointId": "dummy_guid_0",
            "bots": "dummy_guid_1",
            "masters": "dummy_guid_2",
            "logUrls": "dummy_guid_3",
            "benchmarks": "dummy_guid_4"
        },
        "running": [
            5,
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere"
        ],
        "guid": "dummy_guid_5",
        "maxNumSampleValues": 5,
        "numNans": 0
    },
    {
        "name": "Example_Other_Test",
        "unit": "ms_smallerIsBetter",
        "description": "",
        "diagnostics": {
            "pointId": "dummy_guid_0",
            "bots": "dummy_guid_1",
            "masters": "dummy_guid_2",
            "logUrls": "dummy_guid_3",
            "benchmarks": "dummy_guid_4"
        },
        "running": [
            4,
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere"
        ],
        "guid": "dummy_guid_6",
        "maxNumSampleValues": 4,
        "numNans": 0
    }
]
)JSON";

  rapidjson::Document expected_output;
  CheckParseResult(expected_output.Parse(expected_output_str));

  rapidjson::Document output;
  TestConverter(input_str, &output);

  AssertApproxEqual(&output, &output[5]["running"][1], 0.000105);
  AssertApproxEqual(&output, &output[5]["running"][2], -9.180875);
  AssertApproxEqual(&output, &output[5]["running"][3], 0.000103);
  AssertApproxEqual(&output, &output[5]["running"][4], 0.000101);
  AssertApproxEqual(&output, &output[5]["running"][5], 0.000515);
  AssertApproxEqual(&output, &output[5]["running"][6], 2.5e-12);

  AssertApproxEqual(&output, &output[6]["running"][1], 200);
  AssertApproxEqual(&output, &output[6]["running"][2], 4.098931);
  AssertApproxEqual(&output, &output[6]["running"][3], 104);
  AssertApproxEqual(&output, &output[6]["running"][4], 6);
  AssertApproxEqual(&output, &output[6]["running"][5], 416);
  AssertApproxEqual(&output, &output[6]["running"][6], 6290.666);

  AssertJsonEqual(output, expected_output);
}

// Test the basic case with product_versions available.
TEST(CatapultConverter, ConvertWithReleaseVersion) {
  const char* input_str = R"JSON(
[
    {
        "label": "ExampleNullSyscall",
        "test_suite": "my_test_suite",
        "values": [101.0, 102.0, 103.0, 104.0, 105.0],
        "unit": "nanoseconds"
    },
    {
        "label": "Example Other Test",
        "test_suite": "my_test_suite",
        "values": [200, 6, 100, 110],
        "unit": "ms"
    }
]
)JSON";

  const char* expected_output_str = R"JSON(
[
    {
        "guid": "dummy_guid_0",
        "type": "GenericSet",
        "values": [
            123004005006
        ]
    },
    {
        "guid": "dummy_guid_1",
        "type": "GenericSet",
        "values": [
            "example_bots"
        ]
    },
    {
        "guid": "dummy_guid_2",
        "type": "GenericSet",
        "values": [
            "example_masters"
        ]
    },
    {
        "guid": "dummy_guid_3",
        "type": "GenericSet",
        "values": [
            "0.001.20.3"
        ]
    },
    {
        "guid": "dummy_guid_4",
        "type": "GenericSet",
        "values": [
            [
                "Build Log",
                "https://ci.example.com/build/100"
            ]
        ]
    },
    {
        "guid": "dummy_guid_5",
        "type": "GenericSet",
        "values": [
            "my_test_suite"
        ]
    },
    {
        "name": "ExampleNullSyscall",
        "unit": "ms_smallerIsBetter",
        "description": "",
        "diagnostics": {
            "pointId": "dummy_guid_0",
            "bots": "dummy_guid_1",
            "masters": "dummy_guid_2",
            "a_productVersions": "dummy_guid_3",
            "logUrls": "dummy_guid_4",
            "benchmarks": "dummy_guid_5"
        },
        "running": [
            5,
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere"
        ],
        "guid": "dummy_guid_6",
        "maxNumSampleValues": 5,
        "numNans": 0
    },
    {
        "name": "Example_Other_Test",
        "unit": "ms_smallerIsBetter",
        "description": "",
        "diagnostics": {
            "pointId": "dummy_guid_0",
            "bots": "dummy_guid_1",
            "masters": "dummy_guid_2",
            "a_productVersions": "dummy_guid_3",
            "logUrls": "dummy_guid_4",
            "benchmarks": "dummy_guid_5"
        },
        "running": [
            4,
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere"
        ],
        "guid": "dummy_guid_7",
        "maxNumSampleValues": 4,
        "numNans": 0
    }
]
)JSON";

  rapidjson::Document expected_output;
  CheckParseResult(expected_output.Parse(expected_output_str));

  rapidjson::Document output;
  TestConverter(input_str, &output, true /* product_versions_available */);
  AssertApproxEqual(&output, &output[6]["running"][1], 0.000105);
  AssertApproxEqual(&output, &output[6]["running"][2], -9.180875);
  AssertApproxEqual(&output, &output[6]["running"][3], 0.000103);
  AssertApproxEqual(&output, &output[6]["running"][4], 0.000101);
  AssertApproxEqual(&output, &output[6]["running"][5], 0.000515);
  AssertApproxEqual(&output, &output[6]["running"][6], 2.5e-12);

  AssertApproxEqual(&output, &output[7]["running"][1], 200);
  AssertApproxEqual(&output, &output[7]["running"][2], 4.098931);
  AssertApproxEqual(&output, &output[7]["running"][3], 104);
  AssertApproxEqual(&output, &output[7]["running"][4], 6);
  AssertApproxEqual(&output, &output[7]["running"][5], 416);
  AssertApproxEqual(&output, &output[7]["running"][6], 6290.666);

  AssertJsonEqual(output, expected_output);
}

TEST(CatapultConverter, ConvertThroughputUnits) {
  // The example value here is 99 * 1024 * 1024 (99 mebibytes/second).
  const char* input_str = R"JSON(
[
    {
        "label": "ExampleThroughput",
        "test_suite": "my_test_suite",
        "values": [103809024],
        "unit": "bytes/second"
    }
]
)JSON";

  const char* expected_output_str = R"JSON(
[
    {
        "guid": "dummy_guid_0",
        "type": "GenericSet",
        "values": [
            123004005006
        ]
    },
    {
        "guid": "dummy_guid_1",
        "type": "GenericSet",
        "values": [
            "example_bots"
        ]
    },
    {
        "guid": "dummy_guid_2",
        "type": "GenericSet",
        "values": [
            "example_masters"
        ]
    },
    {
        "guid": "dummy_guid_3",
        "type": "GenericSet",
        "values": [
            [
                "Build Log",
                "https://ci.example.com/build/100"
            ]
        ]
    },
    {
        "guid": "dummy_guid_4",
        "type": "GenericSet",
        "values": [
            "my_test_suite"
        ]
    },
    {
        "name": "ExampleThroughput",
        "unit": "unitless_biggerIsBetter",
        "description": "",
        "diagnostics": {
            "pointId": "dummy_guid_0",
            "bots": "dummy_guid_1",
            "masters": "dummy_guid_2",
            "logUrls": "dummy_guid_3",
            "benchmarks": "dummy_guid_4"
        },
        "running": [
            1,
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere"
        ],
        "guid": "dummy_guid_5",
        "maxNumSampleValues": 1,
        "numNans": 0
    }
]
)JSON";

  rapidjson::Document expected_output;
  CheckParseResult(expected_output.Parse(expected_output_str));

  rapidjson::Document output;
  TestConverter(input_str, &output);

  AssertApproxEqual(&output, &output[5]["running"][1], 99);
  AssertApproxEqual(&output, &output[5]["running"][2], 4.595119);
  AssertApproxEqual(&output, &output[5]["running"][3], 99);
  AssertApproxEqual(&output, &output[5]["running"][4], 99);
  AssertApproxEqual(&output, &output[5]["running"][5], 99);
  AssertApproxEqual(&output, &output[5]["running"][6], 0);

  AssertJsonEqual(output, expected_output);
}

TEST(CatapultConverter, ConvertBytesUnit) {
  const char* input_str = R"JSON(
[
    {
        "label": "ExampleWithBytes",
        "test_suite": "my_test_suite",
        "values": [200, 6, 100, 110],
        "unit": "bytes"
    }
]
)JSON";

  const char* expected_output_str = R"JSON(
[
    {
        "guid": "dummy_guid_0",
        "type": "GenericSet",
        "values": [
            123004005006
        ]
    },
    {
        "guid": "dummy_guid_1",
        "type": "GenericSet",
        "values": [
            "example_bots"
        ]
    },
    {
        "guid": "dummy_guid_2",
        "type": "GenericSet",
        "values": [
            "example_masters"
        ]
    },
    {
        "guid": "dummy_guid_3",
        "type": "GenericSet",
        "values": [
            [
                "Build Log",
                "https://ci.example.com/build/100"
            ]
        ]
    },
    {
        "guid": "dummy_guid_4",
        "type": "GenericSet",
        "values": [
            "my_test_suite"
        ]
    },
    {
        "name": "ExampleWithBytes",
        "unit": "sizeInBytes_smallerIsBetter",
        "description": "",
        "diagnostics": {
            "pointId": "dummy_guid_0",
            "bots": "dummy_guid_1",
            "masters": "dummy_guid_2",
            "logUrls": "dummy_guid_3",
            "benchmarks": "dummy_guid_4"
        },
        "running": [
            4,
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere"
        ],
        "guid": "dummy_guid_5",
        "maxNumSampleValues": 4,
        "numNans": 0
    }]
)JSON";

  rapidjson::Document expected_output;
  CheckParseResult(expected_output.Parse(expected_output_str));

  rapidjson::Document output;
  TestConverter(input_str, &output);

  AssertApproxEqual(&output, &output[5]["running"][1], 200);
  AssertApproxEqual(&output, &output[5]["running"][2], 4.098931);
  AssertApproxEqual(&output, &output[5]["running"][3], 104);
  AssertApproxEqual(&output, &output[5]["running"][4], 6);
  AssertApproxEqual(&output, &output[5]["running"][5], 416);
  AssertApproxEqual(&output, &output[5]["running"][6], 6290.666);

  AssertJsonEqual(output, expected_output);
}

TEST(CatapultConverter, ConvertPercentageUnit) {
  const char* input_str = R"JSON(
[
    {
        "label": "ExampleWithPercentages",
        "test_suite": "my_test_suite",
        "values": [0.001, 19.3224, 100.0],
        "unit": "percent"
    }
]
)JSON";

  const char* expected_output_str = R"JSON(
[
    {
        "guid": "dummy_guid_0",
        "type": "GenericSet",
        "values": [
            123004005006
        ]
    },
    {
        "guid": "dummy_guid_1",
        "type": "GenericSet",
        "values": [
            "example_bots"
        ]
    },
    {
        "guid": "dummy_guid_2",
        "type": "GenericSet",
        "values": [
            "example_masters"
        ]
    },
    {
        "guid": "dummy_guid_3",
        "type": "GenericSet",
        "values": [
            [
                "Build Log",
                "https://ci.example.com/build/100"
            ]
        ]
    },
    {
        "guid": "dummy_guid_4",
        "type": "GenericSet",
        "values": [
            "my_test_suite"
        ]
    },
    {
        "name": "ExampleWithPercentages",
        "unit": "n%_smallerIsBetter",
        "description": "",
        "diagnostics": {
            "pointId": "dummy_guid_0",
            "bots": "dummy_guid_1",
            "masters": "dummy_guid_2",
            "logUrls": "dummy_guid_3",
            "benchmarks": "dummy_guid_4"
        },
        "running": [
            3,
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere",
            "compared_elsewhere"
        ],
        "guid": "dummy_guid_5",
        "maxNumSampleValues": 3,
        "numNans": 0
    }]
)JSON";

  rapidjson::Document expected_output;
  CheckParseResult(expected_output.Parse(expected_output_str));

  rapidjson::Document output;
  TestConverter(input_str, &output);

  AssertApproxEqual(&output, &output[5]["running"][1], 100);
  AssertApproxEqual(&output, &output[5]["running"][2], 0.21955998);
  AssertApproxEqual(&output, &output[5]["running"][3], 39.7741);
  AssertApproxEqual(&output, &output[5]["running"][4], 0.001);
  AssertApproxEqual(&output, &output[5]["running"][5], 119.3224);
  AssertApproxEqual(&output, &output[5]["running"][6], 2813.705);

  AssertJsonEqual(output, expected_output);
}

// Test handling of zero values.  The meanlogs field in the output should
// be 'null' in this case.
TEST(CatapultConverter, ZeroValues) {
  const char* input_str = R"JSON(
[
    {
        "label": "ExampleValues",
        "test_suite": "my_test_suite",
        "values": [0],
        "unit": "milliseconds"
    }
]
)JSON";

  rapidjson::Document output;
  TestConverter(input_str, &output);

  rapidjson::Value null;
  rapidjson::Value& meanlogs_field = output[5]["running"][2];
  EXPECT_EQ(meanlogs_field, null);
}

// Test handling of negative values.  The meanlogs field in the output
// should be 'null' in this case.
TEST(CatapultConverter, NegativeValues) {
  const char* input_str = R"JSON(
[
    {
        "label": "ExampleValues",
        "test_suite": "my_test_suite",
        "values": [-1],
        "unit": "milliseconds"
    }
]
)JSON";

  rapidjson::Document output;
  TestConverter(input_str, &output);

  rapidjson::Value null;
  rapidjson::Value& meanlogs_field = output[5]["running"][2];
  EXPECT_EQ(meanlogs_field, null);
}

class TempFile {
 public:
  TempFile(const char* contents) {
    int fd = mkstemp(pathname_);
    EXPECT_GE(fd, 0);
    ssize_t len = strlen(contents);
    EXPECT_EQ(write(fd, contents, len), len);
    EXPECT_EQ(close(fd), 0);
  }
  ~TempFile() { EXPECT_EQ(unlink(pathname_), 0); }
  const char* pathname() { return pathname_; }

 private:
  char pathname_[26] = "/tmp/catapult_test_XXXXXX";
};

// Test the ConverterMain() entry point.  This does not check the contents
// of the JSON output; it only checks that the output is valid JSON.
TEST(CatapultConverter, ConverterMain) {
  TempFile input_file("[]");
  TempFile output_file("");

  const char* args[] = {
      "unused_argv0",
      "--input",
      input_file.pathname(),
      "--output",
      output_file.pathname(),
      "--execution-timestamp-ms",
      "3456",
      "--masters",
      "example_arg_masters",
      "--log-url",
      "https://ci.example.com/build/300",
      "--bots",
      "example_arg_bots",
      "--product-versions",
      "0.001.20.3",
  };
  EXPECT_EQ(ConverterMain(std::size(args), const_cast<char**>(args)), 0);

  // Check just that the output file contains valid JSON.
  FILE* fp = fopen(output_file.pathname(), "r");
  ASSERT_TRUE(fp);
  char buffer[100];
  rapidjson::FileReadStream input_stream(fp, buffer, sizeof(buffer));
  rapidjson::Document input;
  rapidjson::ParseResult parse_result = input.ParseStream(input_stream);
  EXPECT_TRUE(parse_result);
  fclose(fp);
}

// Code copied from "//src/lib/uuid/uuid.cc", which does not currently
// successfully compile as a host side tool.
inline bool IsHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

inline bool IsLowerHexDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

bool IsValidUuidInternal(const std::string& guid, bool strict) {
  constexpr size_t kUUIDLength = 36U;
  if (guid.length() != kUUIDLength)
    return false;
  for (size_t i = 0; i < guid.length(); ++i) {
    char current = guid[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (current != '-')
        return false;
    } else {
      if ((strict && !IsLowerHexDigit(current)) || !IsHexDigit(current))
        return false;
    }
  }
  return true;
}

// Returns true if the input string conforms to the version 4 UUID format.
// Note that this does NOT check if the hexadecimal values "a" through "f"
// are in lower case characters, as Version 4 RFC says they're
// case insensitive. (Use IsValidOutputString for checking if the
// given string is valid output string)
bool IsValidUuid(const std::string& guid) { return IsValidUuidInternal(guid, false /* strict */); }

// Returns true if the input string is valid version 4 UUID output string.
// This also checks if the hexadecimal values "a" through "f" are in lower
// case characters.
bool IsValidUuidOutputString(const std::string& guid) {
  return IsValidUuidInternal(guid, true /* strict */);
}

TEST(CatapultConverter, GenerateUuid) {
  for (int i = 0; i < 256; ++i) {
    auto uuid = GenerateUuid();
    EXPECT_TRUE(IsValidUuid(uuid));
    EXPECT_TRUE(IsValidUuidOutputString(uuid));
  }
}

}  // namespace
