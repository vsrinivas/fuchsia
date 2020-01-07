// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>

#include <fidl/findings_json.h>
#include <fidl/template_string.h>
#include <fidl/utils.h>

#include "test_library.h"
#include "unittest_helpers.h"

namespace fidl {

namespace {

#define ASSERT_JSON(TEST, JSON)                 \
  ASSERT_TRUE(TEST.ExpectJson(JSON), "Failed"); \
  TEST.Reset()

#define TEST_FAILED (!current_test_info->all_ok)

bool FindingsEmitThisJson(Findings& findings, std::string expected_json) {
  BEGIN_HELPER;

  std::string actual_json = fidl::FindingsJson(findings).Produce().str();

  EXPECT_STRING_EQ(
      expected_json, actual_json,
      "To compare results, run:\n\n diff ./json_findings_tests_{expected,actual}.txt\n");

  if (TEST_FAILED) {
    std::ofstream output_actual("json_findings_tests_actual.txt");
    output_actual << actual_json;
    output_actual.close();

    std::ofstream output_expected("json_findings_tests_expected.txt");
    output_expected << expected_json;
    output_expected.close();
  }

  END_HELPER;
}

class JsonFindingsTest {
 public:
  JsonFindingsTest(std::string filename, std::string source) : default_filename_(filename) {
    AddSourceFile(filename, source);
  }

  void AddSourceFile(std::string filename, std::string source) {
    sources_.emplace(filename, SourceFile(filename, source));
  }

  struct AddFindingArgs {
    std::string filename;
    std::string check_id;
    std::string message;
    std::string violation_string;
    // If the intended violation_string is too short to match a unique pattern,
    // set the violation_string to the string that is long enough, and set
    // |forced_size| to the desired length of the |std::string_view| at that
    // location.
    size_t forced_size = std::string::npos;
  };

  // Note: |line| and |column| are 1-based
  Finding* AddFinding(AddFindingArgs args) {
    auto filename = args.filename;
    if (filename.empty()) {
      filename = default_filename_;
    }
    auto result = sources_.find(filename);
    assert(result != sources_.end());
    auto& source_file = result->second;
    std::string_view source_data = source_file.data();
    size_t start = source_data.find(args.violation_string);
    size_t size = args.violation_string.size();
    if (args.forced_size != std::string::npos) {
      size = args.forced_size;
    }
    if (start == std::string::npos) {
      std::cout << "ERROR: violation_string '" << args.violation_string
                << "' was not found in template string:" << std::endl
                << source_data;
    }
    assert(start != std::string::npos && "Bad test! violation_string not found in source data");

    source_data.remove_prefix(start);
    source_data.remove_suffix(source_data.size() - size);
    auto span = fidl::SourceSpan(source_data, source_file);

    return &findings_.emplace_back(span, args.check_id, args.message);
  }

  bool ExpectJson(std::string expected_json) {
    BEGIN_HELPER;

    ASSERT_TRUE(FindingsEmitThisJson(findings_, expected_json));

    END_HELPER;
  }

  void Reset() { findings_.clear(); }

 private:
  std::string default_filename_;
  std::map<std::string, SourceFile> sources_;
  Findings findings_;
};

bool simple_finding() {
  BEGIN_TEST;

  auto test = JsonFindingsTest("simple_finding_test_file", R"ANYLANG(Findings are
language
agnostic.
)ANYLANG");

  test.AddFinding(
      {.check_id = "check-1", .message = "Finding message", .violation_string = "Findings"});

  ASSERT_JSON(test, R"JSON([
  {
    "category": "fidl-lint/check-1",
    "message": "Finding message",
    "path": "simple_finding_test_file",
    "start_line": 1,
    "start_char": 0,
    "end_line": 1,
    "end_char": 8,
    "suggestions": []
  }
])JSON");

  END_TEST;
}

bool simple_fidl() {
  BEGIN_TEST;

  auto test = JsonFindingsTest("simple.fidl", R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> OnWard();
};
)FIDL");

  test.AddFinding({.check_id = "on-ward-check",
                   .message = "OnWard seems like a silly name for an event",
                   .violation_string = "OnWard"});

  ASSERT_JSON(test, R"JSON([
  {
    "category": "fidl-lint/on-ward-check",
    "message": "OnWard seems like a silly name for an event",
    "path": "simple.fidl",
    "start_line": 5,
    "start_char": 5,
    "end_line": 5,
    "end_char": 11,
    "suggestions": []
  }
])JSON");

  END_TEST;
}

bool zero_length_string() {
  BEGIN_TEST;

  auto test = JsonFindingsTest("simple.fidl", R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> OnWard();
};
)FIDL");
  test.AddFinding({.check_id = "check-1",
                   .message = "Finding message",
                   .violation_string = "OnWard",
                   .forced_size = 0});

  ASSERT_JSON(test, R"JSON([
  {
    "category": "fidl-lint/check-1",
    "message": "Finding message",
    "path": "simple.fidl",
    "start_line": 5,
    "start_char": 5,
    "end_line": 5,
    "end_char": 5,
    "suggestions": []
  }
])JSON");

  END_TEST;
}

bool starts_on_newline() {
  BEGIN_TEST;

  auto test = JsonFindingsTest("simple.fidl", R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> OnWard();
};
)FIDL");
  test.AddFinding(
      {.check_id = "check-1", .message = "Finding message", .violation_string = "\nlibrary"});

  ASSERT_JSON(test, R"JSON([
  {
    "category": "fidl-lint/check-1",
    "message": "Finding message",
    "path": "simple.fidl",
    "start_line": 1,
    "start_char": 0,
    "end_line": 2,
    "end_char": 7,
    "suggestions": []
  }
])JSON");

  END_TEST;
}

bool ends_on_newline() {
  BEGIN_TEST;

  auto test = JsonFindingsTest("simple.fidl", R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> OnWard();
};
)FIDL");
  test.AddFinding({.check_id = "check-1",
                   .message = "Finding message",
                   .violation_string = "TestProtocol {\n"});

  ASSERT_JSON(test, R"JSON([
  {
    "category": "fidl-lint/check-1",
    "message": "Finding message",
    "path": "simple.fidl",
    "start_line": 4,
    "start_char": 9,
    "end_line": 5,
    "end_char": 0,
    "suggestions": []
  }
])JSON");

  END_TEST;
}

bool ends_on_eof() {
  BEGIN_TEST;

  auto test = JsonFindingsTest("simple.fidl", R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> OnWard();
};
)FIDL");
  test.AddFinding(
      {.check_id = "check-1", .message = "Finding message", .violation_string = "};\n"});

  ASSERT_JSON(test, R"JSON([
  {
    "category": "fidl-lint/check-1",
    "message": "Finding message",
    "path": "simple.fidl",
    "start_line": 6,
    "start_char": 0,
    "end_line": 6,
    "end_char": 2,
    "suggestions": []
  }
])JSON");

  END_TEST;
}

bool finding_with_suggestion_no_replacement() {
  BEGIN_TEST;

  auto test = JsonFindingsTest("simple.fidl", R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> OnWard();
};
)FIDL");
  test.AddFinding(
          {.check_id = "check-1", .message = "Finding message", .violation_string = "TestProtocol"})
      ->SetSuggestion("Suggestion description");

  ASSERT_JSON(test, R"JSON([
  {
    "category": "fidl-lint/check-1",
    "message": "Finding message",
    "path": "simple.fidl",
    "start_line": 4,
    "start_char": 9,
    "end_line": 4,
    "end_char": 21,
    "suggestions": [
      {
        "description": "Suggestion description",
        "replacements": []
      }
    ]
  }
])JSON");

  END_TEST;
}

bool finding_with_replacement() {
  BEGIN_TEST;

  auto test = JsonFindingsTest("simple.fidl", R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> OnWard();
};
)FIDL");
  test.AddFinding(
          {.check_id = "check-1", .message = "Finding message", .violation_string = "TestProtocol"})
      ->SetSuggestion("Suggestion description", "BestProtocol");

  ASSERT_JSON(test, R"JSON([
  {
    "category": "fidl-lint/check-1",
    "message": "Finding message",
    "path": "simple.fidl",
    "start_line": 4,
    "start_char": 9,
    "end_line": 4,
    "end_char": 21,
    "suggestions": [
      {
        "description": "Suggestion description",
        "replacements": [
          {
            "replacement": "BestProtocol",
            "path": "simple.fidl",
            "start_line": 4,
            "start_char": 9,
            "end_line": 4,
            "end_char": 21
          }
        ]
      }
    ]
  }
])JSON");

  END_TEST;
}

bool finding_spans_2_lines() {
  BEGIN_TEST;

  auto test = JsonFindingsTest("simple.fidl", R"FIDL(
library fidl.a;

protocol
 TestProtocol {
  -> OnWard();
};
)FIDL");
  test.AddFinding({.check_id = "check-1",
                   .message = "Finding message",
                   .violation_string = "protocol\n TestProtocol"});

  ASSERT_JSON(test, R"JSON([
  {
    "category": "fidl-lint/check-1",
    "message": "Finding message",
    "path": "simple.fidl",
    "start_line": 4,
    "start_char": 0,
    "end_line": 5,
    "end_char": 13,
    "suggestions": []
  }
])JSON");

  END_TEST;
}

bool two_findings() {
  BEGIN_TEST;

  auto test = JsonFindingsTest("simple.fidl", R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> OnWard();
};
)FIDL");
  test.AddFinding(
      {.check_id = "check-1", .message = "Finding message", .violation_string = "TestProtocol"});

  test.AddFinding(
      {.check_id = "check-2", .message = "Finding message 2", .violation_string = "OnWard"});

  ASSERT_JSON(test, R"JSON([
  {
    "category": "fidl-lint/check-1",
    "message": "Finding message",
    "path": "simple.fidl",
    "start_line": 4,
    "start_char": 9,
    "end_line": 4,
    "end_char": 21,
    "suggestions": []
  },
  {
    "category": "fidl-lint/check-2",
    "message": "Finding message 2",
    "path": "simple.fidl",
    "start_line": 5,
    "start_char": 5,
    "end_line": 5,
    "end_char": 11,
    "suggestions": []
  }
])JSON");

  END_TEST;
}

bool three_findings() {
  BEGIN_TEST;

  auto test = JsonFindingsTest("simple.fidl", R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> OnWard();
};
)FIDL");
  test.AddFinding(
      {.check_id = "check-3", .message = "Finding message 3", .violation_string = "library"});

  test.AddFinding(
          {.check_id = "check-4", .message = "Finding message 4", .violation_string = "fidl.a"})
      ->SetSuggestion("Suggestion description");

  test.AddFinding({.check_id = "check-5", .message = "Finding message 5", .violation_string = "->"})
      ->SetSuggestion("Suggestion description for finding 5", "Replacement string for finding 5");

  ASSERT_JSON(test, R"JSON([
  {
    "category": "fidl-lint/check-3",
    "message": "Finding message 3",
    "path": "simple.fidl",
    "start_line": 2,
    "start_char": 0,
    "end_line": 2,
    "end_char": 7,
    "suggestions": []
  },
  {
    "category": "fidl-lint/check-4",
    "message": "Finding message 4",
    "path": "simple.fidl",
    "start_line": 2,
    "start_char": 8,
    "end_line": 2,
    "end_char": 14,
    "suggestions": [
      {
        "description": "Suggestion description",
        "replacements": []
      }
    ]
  },
  {
    "category": "fidl-lint/check-5",
    "message": "Finding message 5",
    "path": "simple.fidl",
    "start_line": 5,
    "start_char": 2,
    "end_line": 5,
    "end_char": 4,
    "suggestions": [
      {
        "description": "Suggestion description for finding 5",
        "replacements": [
          {
            "replacement": "Replacement string for finding 5",
            "path": "simple.fidl",
            "start_line": 5,
            "start_char": 2,
            "end_line": 5,
            "end_char": 4
          }
        ]
      }
    ]
  }
])JSON");

  END_TEST;
}

bool multiple_files() {
  BEGIN_TEST;

  auto test = JsonFindingsTest("simple_1.fidl", R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> OnWard();
};
)FIDL");
  test.AddSourceFile("simple_2.fidl", R"FIDL(
library fidl.b;

struct TestStruct {
  string field;
};
)FIDL");
  test.AddFinding({.filename = "simple_1.fidl",
                   .check_id = "check-1",
                   .message = "Finding message",
                   .violation_string = "TestProtocol"});

  test.AddFinding({.filename = "simple_2.fidl",
                   .check_id = "check-2",
                   .message = "Finding message 2",
                   .violation_string = "field"});

  ASSERT_JSON(test, R"JSON([
  {
    "category": "fidl-lint/check-1",
    "message": "Finding message",
    "path": "simple_1.fidl",
    "start_line": 4,
    "start_char": 9,
    "end_line": 4,
    "end_char": 21,
    "suggestions": []
  },
  {
    "category": "fidl-lint/check-2",
    "message": "Finding message 2",
    "path": "simple_2.fidl",
    "start_line": 5,
    "start_char": 9,
    "end_line": 5,
    "end_char": 14,
    "suggestions": []
  }
])JSON");

  END_TEST;
}

bool fidl_json_end_to_end() {
  BEGIN_TEST;

  auto library = std::make_unique<TestLibrary>("example.fidl", R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> Press();
};
)FIDL");

  Findings findings;
  ASSERT_FALSE(library->Lint(&findings));

  ASSERT_TRUE(FindingsEmitThisJson(findings, R"JSON([
  {
    "category": "fidl-lint/event-names-must-start-with-on",
    "message": "Event names must start with 'On'",
    "path": "example.fidl",
    "start_line": 5,
    "start_char": 5,
    "end_line": 5,
    "end_char": 10,
    "suggestions": [
      {
        "description": "change 'Press' to 'OnPress'",
        "replacements": [
          {
            "replacement": "OnPress",
            "path": "example.fidl",
            "start_line": 5,
            "start_char": 5,
            "end_line": 5,
            "end_char": 10
          }
        ]
      }
    ]
  }
])JSON"));

  END_TEST;
}

BEGIN_TEST_CASE(json_findings_tests)

RUN_TEST(simple_finding)
RUN_TEST(simple_fidl)
RUN_TEST(zero_length_string)
RUN_TEST(starts_on_newline)
RUN_TEST(ends_on_newline)
RUN_TEST(ends_on_eof)
RUN_TEST(finding_with_suggestion_no_replacement)
RUN_TEST(finding_with_replacement)
RUN_TEST(finding_spans_2_lines)
RUN_TEST(two_findings)
RUN_TEST(three_findings)
RUN_TEST(multiple_files)
RUN_TEST(fidl_json_end_to_end)

END_TEST_CASE(json_findings_tests)

}  // namespace

}  // namespace fidl
