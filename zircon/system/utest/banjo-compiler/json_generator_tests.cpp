// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <banjo/flat_ast.h>
#include <banjo/lexer.h>
#include <banjo/parser.h>
#include <banjo/source_file.h>

#include <fstream>

#include "examples.h"
#include "test_library.h"

namespace {

// We repeat each test in a loop in order to catch situations where memory layout
// determines what JSON is produced (this is often manifested due to using a std::map<Foo*,...>
// in compiler source code).
static const int kRepeatTestCount = 100;

static inline void trim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
                return !std::isspace(ch) && ch != '\n';
            }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
                return !std::isspace(ch) && ch != '\n';
            })
                .base(),
            s.end());
}

bool checkJSONGenerator(std::string raw_source_code, std::string expected_json) {
    TestLibrary library("json.banjo", raw_source_code);
    EXPECT_TRUE(library.Compile());

    // actual
    auto actual = library.GenerateJSON();
    trim(actual);

    // expected
    trim(expected_json);

    if (actual.compare(expected_json) == 0) {
        return true;
    }

    // On error, we output both the actual and expected to allow simple
    // diffing to debug the test.

    std::ofstream output_actual("json_generator_tests_actual.txt");
    output_actual << actual;
    output_actual.close();

    std::ofstream output_expected("json_generator_tests_expected.txt");
    output_expected << expected_json;
    output_expected.close();

    return false;
}

bool json_generator_test_struct() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        EXPECT_TRUE(checkJSONGenerator(R"BANJO(
library banjo.test.json;

struct Simple {
    uint8 f1;
    bool f2;
};

)BANJO",
                                       R"JSON(
{
  "version": "0.0.1",
  "name": "banjo.test.json",
  "library_dependencies": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [],
  "struct_declarations": [
    {
      "name": "banjo.test.json/Simple",
      "members": [
        {
          "type": {
            "kind": "primitive",
            "subtype": "uint8"
          },
          "name": "f1",
          "size": 1,
          "alignment": 1,
          "offset": 0,
          "max_handles": 0
        },
        {
          "type": {
            "kind": "primitive",
            "subtype": "bool"
          },
          "name": "f2",
          "size": 1,
          "alignment": 1,
          "offset": 1,
          "max_handles": 0
        }
      ],
      "size": 2,
      "alignment": 1,
      "max_handles": 0
    }
  ],
  "union_declarations": [],
  "declaration_order": [
    "banjo.test.json/Simple"
  ],
  "declarations": {
    "banjo.test.json/Simple": "struct"
  }
}
)JSON"));
    }

    END_TEST;
}

bool json_generator_test_union() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        EXPECT_TRUE(checkJSONGenerator(R"BANJO(
library banjo.test.json;

struct Pizza {
    vector<string:16> toppings;
};

struct Pasta {
    string:16 sauce;
};

union PizzaOrPasta {
    Pizza pizza;
    Pasta pasta;
};

)BANJO",
                                       R"JSON(
{
  "version": "0.0.1",
  "name": "banjo.test.json",
  "library_dependencies": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [],
  "struct_declarations": [
    {
      "name": "banjo.test.json/Pizza",
      "members": [
        {
          "type": {
            "kind": "vector",
            "element_type": {
              "kind": "string",
              "maybe_element_count": 16,
              "nullable": false
            },
            "nullable": false
          },
          "name": "toppings",
          "size": 16,
          "alignment": 8,
          "offset": 0,
          "max_handles": 0
        }
      ],
      "size": 16,
      "alignment": 8,
      "max_handles": 0
    },
    {
      "name": "banjo.test.json/Pasta",
      "members": [
        {
          "type": {
            "kind": "string",
            "maybe_element_count": 16,
            "nullable": false
          },
          "name": "sauce",
          "size": 16,
          "alignment": 8,
          "offset": 0,
          "max_handles": 0
        }
      ],
      "size": 16,
      "alignment": 8,
      "max_handles": 0
    }
  ],
  "union_declarations": [
    {
      "name": "banjo.test.json/PizzaOrPasta",
      "members": [
        {
          "type": {
            "kind": "identifier",
            "identifier": "banjo.test.json/Pizza",
            "nullable": false
          },
          "name": "pizza",
          "size": 16,
          "alignment": 8,
          "offset": 8
        },
        {
          "type": {
            "kind": "identifier",
            "identifier": "banjo.test.json/Pasta",
            "nullable": false
          },
          "name": "pasta",
          "size": 16,
          "alignment": 8,
          "offset": 8
        }
      ],
      "size": 24,
      "alignment": 8,
      "max_handles": 0
    }
  ],
  "declaration_order": [
    "banjo.test.json/Pasta",
    "banjo.test.json/Pizza",
    "banjo.test.json/PizzaOrPasta"
  ],
  "declarations": {
    "banjo.test.json/Pizza": "struct",
    "banjo.test.json/Pasta": "struct",
    "banjo.test.json/PizzaOrPasta": "union"
  }
}
)JSON"));
    }

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(json_generator_tests);
RUN_TEST(json_generator_test_struct);
RUN_TEST(json_generator_test_union);
END_TEST_CASE(json_generator_tests);
