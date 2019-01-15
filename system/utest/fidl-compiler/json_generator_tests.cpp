// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

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
            }).base(),
            s.end());
}

bool checkJSONGenerator(std::string raw_source_code, std::string expected_json) {
    TestLibrary library("json.fidl", raw_source_code);
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
        EXPECT_TRUE(checkJSONGenerator(R"FIDL(
library fidl.test.json;

struct Simple {
    uint8 f1;
    bool f2;
};

)FIDL",
                                       R"JSON(
{
  "version": "0.0.1",
  "name": "fidl.test.json",
  "library_dependencies": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [],
  "struct_declarations": [
    {
      "name": "fidl.test.json/Simple",
      "anonymous": false,
      "members": [
        {
          "type": {
            "kind": "primitive",
            "subtype": "uint8"
          },
          "name": "f1",
          "size": 1,
          "max_out_of_line": 0,
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
          "max_out_of_line": 0,
          "alignment": 1,
          "offset": 1,
          "max_handles": 0
        }
      ],
      "size": 2,
      "max_out_of_line": 0,
      "alignment": 1,
      "max_handles": 0
    }
  ],
  "table_declarations": [],
  "union_declarations": [],
  "declaration_order": [
    "fidl.test.json/Simple"
  ],
  "declarations": {
    "fidl.test.json/Simple": "struct"
  }
}
)JSON"));
    }

    END_TEST;
}

bool json_generator_test_empty_struct() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        EXPECT_TRUE(checkJSONGenerator(R"FIDL(
library fidl.test.json;

struct Empty {
};

interface EmptyInterface {
  5: Send(Empty e);
  -> Receive (Empty e);
  SendAndReceive(Empty e) -> (Empty e);
};
)FIDL",
                                       R"JSON(
{
  "version": "0.0.1",
  "name": "fidl.test.json",
  "library_dependencies": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [
    {
      "name": "fidl.test.json/EmptyInterface",
      "methods": [
        {
          "ordinal": 5,
          "generated_ordinal": 296942602,
          "name": "Send",
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "identifier",
                "identifier": "fidl.test.json/Empty",
                "nullable": false
              },
              "name": "e",
              "size": 1,
              "max_out_of_line": 0,
              "alignment": 1,
              "offset": 16,
              "max_handles": 0
            }
          ],
          "maybe_request_size": 24,
          "maybe_request_alignment": 8,
          "has_response": false
        },
        {
          "ordinal": 939543845,
          "generated_ordinal": 939543845,
          "name": "Receive",
          "has_request": false,
          "has_response": true,
          "maybe_response": [
            {
              "type": {
                "kind": "identifier",
                "identifier": "fidl.test.json/Empty",
                "nullable": false
              },
              "name": "e",
              "size": 1,
              "max_out_of_line": 0,
              "alignment": 1,
              "offset": 16,
              "max_handles": 0
            }
          ],
          "maybe_response_size": 24,
          "maybe_response_alignment": 8
        },
        {
          "ordinal": 556045674,
          "generated_ordinal": 556045674,
          "name": "SendAndReceive",
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "identifier",
                "identifier": "fidl.test.json/Empty",
                "nullable": false
              },
              "name": "e",
              "size": 1,
              "max_out_of_line": 0,
              "alignment": 1,
              "offset": 16,
              "max_handles": 0
            }
          ],
          "maybe_request_size": 24,
          "maybe_request_alignment": 8,
          "has_response": true,
          "maybe_response": [
            {
              "type": {
                "kind": "identifier",
                "identifier": "fidl.test.json/Empty",
                "nullable": false
              },
              "name": "e",
              "size": 1,
              "max_out_of_line": 0,
              "alignment": 1,
              "offset": 16,
              "max_handles": 0
            }
          ],
          "maybe_response_size": 24,
          "maybe_response_alignment": 8
        }
      ]
    }
  ],
  "struct_declarations": [
    {
      "name": "fidl.test.json/Empty",
      "anonymous": false,
      "members": [],
      "size": 1,
      "max_out_of_line": 0,
      "alignment": 1,
      "max_handles": 0
    }
  ],
  "table_declarations": [],
  "union_declarations": [],
  "declaration_order": [
    "fidl.test.json/Empty",
    "fidl.test.json/EmptyInterface"
  ],
  "declarations": {
    "fidl.test.json/EmptyInterface": "interface",
    "fidl.test.json/Empty": "struct"
  }
}
)JSON"));
    }

    END_TEST;
}

bool json_generator_test_table() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        EXPECT_TRUE(checkJSONGenerator(R"FIDL(
library fidl.test.json;

table Simple {
    1: uint8 f1;
    2: bool f2;
    3: reserved;
};

)FIDL",
                                       R"JSON(
{
  "version": "0.0.1",
  "name": "fidl.test.json",
  "library_dependencies": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [],
  "struct_declarations": [],
  "table_declarations": [
    {
      "name": "fidl.test.json/Simple",
      "members": [
        {
          "ordinal": 1,
          "reserved": false,
          "type": {
            "kind": "primitive",
            "subtype": "uint8"
          },
          "name": "f1",
          "size": 1,
          "max_out_of_line": 0,
          "alignment": 1,
          "max_handles": 0
        },
        {
          "ordinal": 2,
          "reserved": false,
          "type": {
            "kind": "primitive",
            "subtype": "bool"
          },
          "name": "f2",
          "size": 1,
          "max_out_of_line": 0,
          "alignment": 1,
          "max_handles": 0
        },
        {
          "ordinal": 3,
          "reserved": true
        }
      ],
      "size": 16,
      "max_out_of_line": 48,
      "alignment": 8,
      "max_handles": 0
    }
  ],
  "union_declarations": [],
  "declaration_order": [
    "fidl.test.json/Simple"
  ],
  "declarations": {
    "fidl.test.json/Simple": "table"
  }
}
)JSON"));
    }

    END_TEST;
}

bool json_generator_test_union() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        EXPECT_TRUE(checkJSONGenerator(R"FIDL(
library fidl.test.json;

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

)FIDL",
                                       R"JSON(
{
  "version": "0.0.1",
  "name": "fidl.test.json",
  "library_dependencies": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [],
  "struct_declarations": [
    {
      "name": "fidl.test.json/Pizza",
      "anonymous": false,
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
          "max_out_of_line": 4294967295,
          "alignment": 8,
          "offset": 0,
          "max_handles": 0
        }
      ],
      "size": 16,
      "max_out_of_line": 4294967295,
      "alignment": 8,
      "max_handles": 0
    },
    {
      "name": "fidl.test.json/Pasta",
      "anonymous": false,
      "members": [
        {
          "type": {
            "kind": "string",
            "maybe_element_count": 16,
            "nullable": false
          },
          "name": "sauce",
          "size": 16,
          "max_out_of_line": 16,
          "alignment": 8,
          "offset": 0,
          "max_handles": 0
        }
      ],
      "size": 16,
      "max_out_of_line": 16,
      "alignment": 8,
      "max_handles": 0
    }
  ],
  "table_declarations": [],
  "union_declarations": [
    {
      "name": "fidl.test.json/PizzaOrPasta",
      "members": [
        {
          "type": {
            "kind": "identifier",
            "identifier": "fidl.test.json/Pizza",
            "nullable": false
          },
          "name": "pizza",
          "size": 16,
          "max_out_of_line": 4294967295,
          "alignment": 8,
          "offset": 8
        },
        {
          "type": {
            "kind": "identifier",
            "identifier": "fidl.test.json/Pasta",
            "nullable": false
          },
          "name": "pasta",
          "size": 16,
          "max_out_of_line": 16,
          "alignment": 8,
          "offset": 8
        }
      ],
      "size": 24,
      "max_out_of_line": 4294967295,
      "alignment": 8,
      "max_handles": 0
    }
  ],
  "declaration_order": [
    "fidl.test.json/Pizza",
    "fidl.test.json/Pasta",
    "fidl.test.json/PizzaOrPasta"
  ],
  "declarations": {
    "fidl.test.json/Pizza": "struct",
    "fidl.test.json/Pasta": "struct",
    "fidl.test.json/PizzaOrPasta": "union"
  }
}
)JSON"));
    }

    END_TEST;
}

// This test ensures that inherited methods have the same ordinal / signature /
// etc as the method from which they are inheriting.
bool json_generator_test_inheritance() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        EXPECT_TRUE(checkJSONGenerator(R"FIDL(
library fidl.test.json;

[FragileBase]
interface super {
   foo(string s) -> (int64 y);
};

interface sub : super {
};

)FIDL",
                                       R"JSON({
  "version": "0.0.1",
  "name": "fidl.test.json",
  "library_dependencies": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [
    {
      "name": "fidl.test.json/super",
      "maybe_attributes": [
        {
          "name": "FragileBase",
          "value": ""
        }
      ],
      "methods": [
        {
          "ordinal": 790020540,
          "generated_ordinal": 790020540,
          "name": "foo",
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "string",
                "nullable": false
              },
              "name": "s",
              "size": 16,
              "max_out_of_line": 4294967295,
              "alignment": 8,
              "offset": 16,
              "max_handles": 0
            }
          ],
          "maybe_request_size": 32,
          "maybe_request_alignment": 8,
          "has_response": true,
          "maybe_response": [
            {
              "type": {
                "kind": "primitive",
                "subtype": "int64"
              },
              "name": "y",
              "size": 8,
              "max_out_of_line": 0,
              "alignment": 8,
              "offset": 16,
              "max_handles": 0
            }
          ],
          "maybe_response_size": 24,
          "maybe_response_alignment": 8
        }
      ]
    },
    {
      "name": "fidl.test.json/sub",
      "methods": [
        {
          "ordinal": 790020540,
          "generated_ordinal": 790020540,
          "name": "foo",
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "string",
                "nullable": false
              },
              "name": "s",
              "size": 16,
              "max_out_of_line": 4294967295,
              "alignment": 8,
              "offset": 16,
              "max_handles": 0
            }
          ],
          "maybe_request_size": 32,
          "maybe_request_alignment": 8,
          "has_response": true,
          "maybe_response": [
            {
              "type": {
                "kind": "primitive",
                "subtype": "int64"
              },
              "name": "y",
              "size": 8,
              "max_out_of_line": 0,
              "alignment": 8,
              "offset": 16,
              "max_handles": 0
            }
          ],
          "maybe_response_size": 24,
          "maybe_response_alignment": 8
        }
      ]
    }
  ],
  "struct_declarations": [],
  "table_declarations": [],
  "union_declarations": [],
  "declaration_order": [
    "fidl.test.json/super",
    "fidl.test.json/sub"
  ],
  "declarations": {
    "fidl.test.json/super": "interface",
    "fidl.test.json/sub": "interface"
  }
})JSON"));
    }

    END_TEST;
}

bool json_generator_test_inheritance_with_recursive_decl() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        EXPECT_TRUE(checkJSONGenerator(R"FIDL(
library fidl.test.json;

[FragileBase]
interface Parent {
  First(request<Parent> request);
};

interface Child : Parent {
  Second(request<Parent> request);
};

)FIDL",
                                       R"JSON({
  "version": "0.0.1",
  "name": "fidl.test.json",
  "library_dependencies": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [
    {
      "name": "fidl.test.json/Parent",
      "maybe_attributes": [
        {
          "name": "FragileBase",
          "value": ""
        }
      ],
      "methods": [
        {
          "ordinal": 1722375644,
          "generated_ordinal": 1722375644,
          "name": "First",
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "request",
                "subtype": "fidl.test.json/Parent",
                "nullable": false
              },
              "name": "request",
              "size": 4,
              "max_out_of_line": 0,
              "alignment": 4,
              "offset": 16,
              "max_handles": 1
            }
          ],
          "maybe_request_size": 24,
          "maybe_request_alignment": 8,
          "has_response": false
        }
      ]
    },
    {
      "name": "fidl.test.json/Child",
      "methods": [
        {
          "ordinal": 1722375644,
          "generated_ordinal": 1722375644,
          "name": "First",
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "request",
                "subtype": "fidl.test.json/Parent",
                "nullable": false
              },
              "name": "request",
              "size": 4,
              "max_out_of_line": 0,
              "alignment": 4,
              "offset": 16,
              "max_handles": 1
            }
          ],
          "maybe_request_size": 24,
          "maybe_request_alignment": 8,
          "has_response": false
        },
        {
          "ordinal": 19139766,
          "generated_ordinal": 19139766,
          "name": "Second",
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "request",
                "subtype": "fidl.test.json/Parent",
                "nullable": false
              },
              "name": "request",
              "size": 4,
              "max_out_of_line": 0,
              "alignment": 4,
              "offset": 16,
              "max_handles": 1
            }
          ],
          "maybe_request_size": 24,
          "maybe_request_alignment": 8,
          "has_response": false
        }
      ]
    }
  ],
  "struct_declarations": [],
  "table_declarations": [],
  "union_declarations": [],
  "declaration_order": [
    "fidl.test.json/Parent",
    "fidl.test.json/Child"
  ],
  "declarations": {
    "fidl.test.json/Parent": "interface",
    "fidl.test.json/Child": "interface"
  }
})JSON"));
    }

    END_TEST;
}
} // namespace

BEGIN_TEST_CASE(json_generator_tests);
RUN_TEST(json_generator_test_empty_struct);
RUN_TEST(json_generator_test_struct);
RUN_TEST(json_generator_test_table);
RUN_TEST(json_generator_test_union);
RUN_TEST(json_generator_test_inheritance);
RUN_TEST(json_generator_test_inheritance_with_recursive_decl);
END_TEST_CASE(json_generator_tests);
