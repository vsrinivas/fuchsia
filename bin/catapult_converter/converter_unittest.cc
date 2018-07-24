// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/compiler.h>

#include "gtest/gtest.h"
#include "lib/fxl/strings/split_string.h"
#include "third_party/rapidjson/rapidjson/error/en.h"
#include "third_party/rapidjson/rapidjson/filereadstream.h"
#include "third_party/rapidjson/rapidjson/filewritestream.h"
#include "third_party/rapidjson/rapidjson/prettywriter.h"

#include "converter.h"

namespace {

void CheckParseResult(rapidjson::ParseResult parse_result) {
  EXPECT_TRUE(parse_result)
      << "JSON parse error: "
      << rapidjson::GetParseError_En(parse_result.Code())
      << " (offset " << parse_result.Offset() << ")";
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
void AssertApproxEqual(rapidjson::Document* document, rapidjson::Value* actual,
                       double expected) {
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
      << "Got value " << actual_val << ", but expected value close to "
      << expected << " (between " << expected_min << " and " << expected_max
      << ")";
  actual->SetString("compared_elsewhere", document->GetAllocator());
}

std::vector<std::string> SplitLines(const char* str) {
  return fxl::SplitStringCopy(fxl::StringView(str), "\n", fxl::kKeepWhitespace,
                              fxl::kSplitWantAll);
}

void PrintLines(const std::vector<std::string>& lines, size_t start, size_t end,
                char prefix) {
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

void AssertJsonEqual(const rapidjson::Document& doc1,
                     const rapidjson::Document& doc2) {
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

TEST(CatapultConverter, Convert) {
  const char* input_str = R"JSON(
[
    {
        "label": "ExampleNullSyscall",
        "test_suite": "my_test_suite",
        "samples": [{"values": [101.0, 102.0, 103.0, 104.0, 105.0]}],
        "unit": "nanoseconds"
    },
    {
        "label": "ExampleOtherTest",
        "test_suite": "my_test_suite",
        "samples": [{"values": [200, 6, 100, 110]}],
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
            4321
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
            "chromiumCommitPositions": "dummy_guid_0",
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
        "name": "ExampleOtherTest",
        "unit": "ms_smallerIsBetter",
        "description": "",
        "diagnostics": {
            "chromiumCommitPositions": "dummy_guid_0",
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

  rapidjson::Document input;
  CheckParseResult(input.Parse(input_str));

  rapidjson::Document expected_output;
  CheckParseResult(expected_output.Parse(expected_output_str));

  rapidjson::Document output;
  ConverterArgs args;
  args.timestamp = 4321;
  args.masters = "example_masters";
  args.bots = "example_bots";
  args.log_url = "https://ci.example.com/build/100";
  args.use_test_guids = true;
  Convert(&input, &output, &args);

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

// Test the case where the "samples" list contains multiple entries and
// these entries have their own "label" fields.
TEST(CatapultConverter, ConvertNested) {
  const char* input_str = R"JSON(
[
    {
        "label": "Example Of Split Results",
        "test_suite": "some_test_suite",
        "samples": [
            {"label": "samples 0 to 0",
             "values": [200]},
            {"label": "subsequent samples",
             "values": [50, 60, 70]}
        ],
        "unit": "nanoseconds"
    }
]
)JSON";

  const char* expected_output_str = R"JSON(
[
    {
        "guid": "dummy_guid_0",
        "type": "GenericSet",
        "values": [
            4321
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
                "https://ci.example.com/build/200"
            ]
        ]
    },
    {
        "guid": "dummy_guid_4",
        "type": "GenericSet",
        "values": [
            "some_test_suite"
        ]
    },
    {
        "name": "Example_Of_Split_Results_samples_0_to_0",
        "unit": "ms_smallerIsBetter",
        "description": "",
        "diagnostics": {
            "chromiumCommitPositions": "dummy_guid_0",
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
    },
    {
        "name": "Example_Of_Split_Results_subsequent_samples",
        "unit": "ms_smallerIsBetter",
        "description": "",
        "diagnostics": {
            "chromiumCommitPositions": "dummy_guid_0",
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
        "guid": "dummy_guid_6",
        "maxNumSampleValues": 3,
        "numNans": 0
    }
]
)JSON";

  rapidjson::Document input;
  CheckParseResult(input.Parse(input_str));

  rapidjson::Document expected_output;
  CheckParseResult(expected_output.Parse(expected_output_str));

  rapidjson::Document output;
  ConverterArgs args;
  args.timestamp = 4321;
  args.masters = "example_masters";
  args.bots = "example_bots";
  args.log_url = "https://ci.example.com/build/200";
  args.use_test_guids = true;
  Convert(&input, &output, &args);

  AssertApproxEqual(&output, &output[5]["running"][1], 0.0002);
  AssertApproxEqual(&output, &output[5]["running"][2], -8.5171);
  AssertApproxEqual(&output, &output[5]["running"][3], 0.0002);
  AssertApproxEqual(&output, &output[5]["running"][4], 0.0002);
  AssertApproxEqual(&output, &output[5]["running"][5], 0.0002);
  AssertApproxEqual(&output, &output[5]["running"][6], 0);

  AssertApproxEqual(&output, &output[6]["running"][1], 7.000e-5);
  AssertApproxEqual(&output, &output[6]["running"][2], -9.7305);
  AssertApproxEqual(&output, &output[6]["running"][3], 6.000e-5);
  AssertApproxEqual(&output, &output[6]["running"][4], 5.000e-5);
  AssertApproxEqual(&output, &output[6]["running"][5], 0.00017999);
  AssertApproxEqual(&output, &output[6]["running"][6], 1.000e-10);

  AssertJsonEqual(output, expected_output);
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
  ~TempFile() {
    EXPECT_EQ(unlink(pathname_), 0);
  }
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
    "--input", input_file.pathname(),
    "--output", output_file.pathname(),
    "--execution-timestamp-ms", "3456",
    "--masters", "example_arg_masters",
    "--log-url", "https://ci.example.com/build/300",
    "--bots", "example_arg_bots",
  };
  EXPECT_EQ(ConverterMain(countof(args), const_cast<char**>(args)), 0);

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

}
