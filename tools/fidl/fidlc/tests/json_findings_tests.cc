// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <fstream>
#include <string_view>

#include "tools/fidl/fidlc/include/fidl/findings_json.h"
#include "tools/fidl/fidlc/tests/test_library.h"
#include "tools/fidl/fidlc/tests/unittest_helpers.h"

namespace fidl {

namespace {

#define ASSERT_JSON(TEST, JSON)              \
  ASSERT_NO_FAILURES(TEST.ExpectJson(JSON)); \
  TEST.Reset()

void FindingsEmitThisJson(Findings& findings, std::string_view expected_json) {
  std::string actual_json = fidl::FindingsJson(findings).Produce().str();

  if (expected_json != actual_json) {
    std::ofstream output_actual("json_findings_tests_actual.txt");
    output_actual << actual_json;
    output_actual.close();

    std::ofstream output_expected("json_findings_tests_expected.txt");
    output_expected << expected_json;
    output_expected.close();
  }

  EXPECT_STRING_EQ(
      expected_json, actual_json,
      "To compare results, run:\n\n diff ./json_findings_tests_{expected,actual}.txt\n");
}

class JsonFindingsTest {
 public:
  JsonFindingsTest(const std::string& filename, std::string source) : default_filename_(filename) {
    AddSourceFile(filename, std::move(source));
  }

  void AddSourceFile(std::string filename, std::string source) {
    sources_.emplace(std::move(filename), SourceFile(filename, std::move(source)));
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
    ZX_ASSERT(result != sources_.end());
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
      ZX_PANIC("violation_string not found in template string");
    }

    source_data.remove_prefix(start);
    source_data.remove_suffix(source_data.size() - size);
    auto span = fidl::SourceSpan(source_data, source_file);

    return &findings_.emplace_back(span, args.check_id, args.message);
  }

  void ExpectJson(std::string_view expected_json) {
    FindingsEmitThisJson(findings_, expected_json);
  }

  void Reset() { findings_.clear(); }

 private:
  std::string default_filename_;
  std::map<std::string, SourceFile> sources_;
  Findings findings_;
};

TEST(JsonFindingsTests, SimpleFinding) {
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
}

TEST(JsonFindingsTests, SimpleFidl) {
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
}

TEST(JsonFindingsTests, ZeroLengthString) {
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
}

TEST(JsonFindingsTests, StartsOnNewline) {
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
}

TEST(JsonFindingsTests, EndsOnNewline) {
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
}

TEST(JsonFindingsTests, EndsOnEof) {
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
}

TEST(JsonFindingsTests, FindingWithSuggestionNoReplacement) {
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
}

TEST(JsonFindingsTests, FindingWithReplacement) {
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
}

TEST(JsonFindingsTests, FindingSpans2Lines) {
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
}

TEST(JsonFindingsTests, TwoFindings) {
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
}

TEST(JsonFindingsTests, ThreeFindings) {
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
}

TEST(JsonFindingsTests, MultipleFiles) {
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
}

TEST(JsonFindingsTests, FidlJsonEndToEnd) {
  TestLibrary library(R"FIDL(
library fidl.a;

protocol TestProtocol {
  -> Press();
};
)FIDL");

  Findings findings;
  ASSERT_FALSE(library.Lint(&findings));

  ASSERT_NO_FAILURES(FindingsEmitThisJson(findings, R"JSON([
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
}

}  // namespace

}  // namespace fidl
