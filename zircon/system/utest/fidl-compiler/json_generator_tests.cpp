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
    ASSERT_TRUE(library.Compile());

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
  "bits_declarations": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [],
  "struct_declarations": [
    {
      "name": "fidl.test.json/Simple",
      "location": {
        "filename": "json.fidl",
        "line": 4,
        "column": 8
      },
      "anonymous": false,
      "members": [
        {
          "type": {
            "kind": "primitive",
            "subtype": "uint8"
          },
          "name": "f1",
          "location": {
            "filename": "json.fidl",
            "line": 5,
            "column": 11
          },
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
          "location": {
            "filename": "json.fidl",
            "line": 6,
            "column": 10
          },
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
  "xunion_declarations": [],
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

protocol EmptyInterface {
  Send(Empty e);
  -> Receive (Empty e);
  SendAndReceive(Empty e) -> (Empty e);
};
)FIDL",
                                       R"JSON(
{
  "version": "0.0.1",
  "name": "fidl.test.json",
  "library_dependencies": [],
  "bits_declarations": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [
    {
      "name": "fidl.test.json/EmptyInterface",
      "location": {
        "filename": "json.fidl",
        "line": 7,
        "column": 10
      },
      "methods": [
        {
          "ordinal": 296942602,
          "generated_ordinal": 296942602,
          "name": "Send",
          "location": {
            "filename": "json.fidl",
            "line": 8,
            "column": 3
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "identifier",
                "identifier": "fidl.test.json/Empty",
                "nullable": false
              },
              "name": "e",
              "location": {
                "filename": "json.fidl",
                "line": 8,
                "column": 14
              },
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
          "location": {
            "filename": "json.fidl",
            "line": 9,
            "column": 6
          },
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
              "location": {
                "filename": "json.fidl",
                "line": 9,
                "column": 21
              },
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
          "location": {
            "filename": "json.fidl",
            "line": 10,
            "column": 3
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "identifier",
                "identifier": "fidl.test.json/Empty",
                "nullable": false
              },
              "name": "e",
              "location": {
                "filename": "json.fidl",
                "line": 10,
                "column": 24
              },
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
              "location": {
                "filename": "json.fidl",
                "line": 10,
                "column": 37
              },
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
      "location": {
        "filename": "json.fidl",
        "line": 4,
        "column": 8
      },
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
  "xunion_declarations": [],
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
  "bits_declarations": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [],
  "struct_declarations": [],
  "table_declarations": [
    {
      "name": "fidl.test.json/Simple",
      "location": {
        "filename": "json.fidl",
        "line": 4,
        "column": 7
      },
      "members": [
        {
          "ordinal": 1,
          "reserved": false,
          "type": {
            "kind": "primitive",
            "subtype": "uint8"
          },
          "name": "f1",
          "location": {
            "filename": "json.fidl",
            "line": 5,
            "column": 14
          },
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
          "location": {
            "filename": "json.fidl",
            "line": 6,
            "column": 13
          },
          "size": 1,
          "max_out_of_line": 0,
          "alignment": 1,
          "max_handles": 0
        },
        {
          "ordinal": 3,
          "reserved": true,
          "location": {
            "filename": "json.fidl",
            "line": 7,
            "column": 5
          }
        }
      ],
      "size": 16,
      "max_out_of_line": 48,
      "alignment": 8,
      "max_handles": 0
    }
  ],
  "union_declarations": [],
  "xunion_declarations": [],
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
  "bits_declarations": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [],
  "struct_declarations": [
    {
      "name": "fidl.test.json/Pizza",
      "location": {
        "filename": "json.fidl",
        "line": 4,
        "column": 8
      },
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
          "location": {
            "filename": "json.fidl",
            "line": 5,
            "column": 23
          },
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
      "location": {
        "filename": "json.fidl",
        "line": 8,
        "column": 8
      },
      "anonymous": false,
      "members": [
        {
          "type": {
            "kind": "string",
            "maybe_element_count": 16,
            "nullable": false
          },
          "name": "sauce",
          "location": {
            "filename": "json.fidl",
            "line": 9,
            "column": 15
          },
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
      "location": {
        "filename": "json.fidl",
        "line": 12,
        "column": 7
      },
      "members": [
        {
          "type": {
            "kind": "identifier",
            "identifier": "fidl.test.json/Pizza",
            "nullable": false
          },
          "name": "pizza",
          "location": {
            "filename": "json.fidl",
            "line": 13,
            "column": 11
          },
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
          "location": {
            "filename": "json.fidl",
            "line": 14,
            "column": 11
          },
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
  "xunion_declarations": [],
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

bool json_generator_test_xunion() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        EXPECT_TRUE(checkJSONGenerator(R"FIDL(
library fidl.test.json;

xunion xu {
  string s;
  int32 i;
};

)FIDL",
                                       R"JSON(
{
  "version": "0.0.1",
  "name": "fidl.test.json",
  "library_dependencies": [],
  "bits_declarations": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [],
  "struct_declarations": [],
  "table_declarations": [],
  "union_declarations": [],
  "xunion_declarations": [
    {
      "name": "fidl.test.json/xu",
      "location": {
        "filename": "json.fidl",
        "line": 4,
        "column": 8
      },
      "members": [
        {
          "ordinal": 730795057,
          "type": {
            "kind": "string",
            "nullable": false
          },
          "name": "s",
          "location": {
            "filename": "json.fidl",
            "line": 5,
            "column": 10
          },
          "size": 16,
          "max_out_of_line": 4294967295,
          "alignment": 8,
          "offset": 0
        },
        {
          "ordinal": 243975053,
          "type": {
            "kind": "primitive",
            "subtype": "int32"
          },
          "name": "i",
          "location": {
            "filename": "json.fidl",
            "line": 6,
            "column": 9
          },
          "size": 4,
          "max_out_of_line": 0,
          "alignment": 4,
          "offset": 0
        }
      ],
      "size": 24,
      "max_out_of_line": 4294967295,
      "alignment": 8,
      "max_handles": 0
    }
  ],
  "declaration_order": [
    "fidl.test.json/xu"
  ],
  "declarations": {
    "fidl.test.json/xu": "xunion"
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
protocol super {
   foo(string s) -> (int64 y);
};

protocol sub {
  compose super;
};

)FIDL",
                                       R"JSON({
  "version": "0.0.1",
  "name": "fidl.test.json",
  "library_dependencies": [],
  "bits_declarations": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [
    {
      "name": "fidl.test.json/super",
      "location": {
        "filename": "json.fidl",
        "line": 5,
        "column": 10
      },
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
          "location": {
            "filename": "json.fidl",
            "line": 6,
            "column": 4
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "string",
                "nullable": false
              },
              "name": "s",
              "location": {
                "filename": "json.fidl",
                "line": 6,
                "column": 15
              },
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
              "location": {
                "filename": "json.fidl",
                "line": 6,
                "column": 28
              },
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
      "location": {
        "filename": "json.fidl",
        "line": 9,
        "column": 10
      },
      "methods": [
        {
          "ordinal": 790020540,
          "generated_ordinal": 790020540,
          "name": "foo",
          "location": {
            "filename": "json.fidl",
            "line": 6,
            "column": 4
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "string",
                "nullable": false
              },
              "name": "s",
              "location": {
                "filename": "json.fidl",
                "line": 6,
                "column": 15
              },
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
              "location": {
                "filename": "json.fidl",
                "line": 6,
                "column": 28
              },
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
  "xunion_declarations": [],
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
protocol Parent {
  First(request<Parent> request);
};

protocol Child {
  compose Parent;
  Second(request<Parent> request);
};

)FIDL",
                                       R"JSON({
  "version": "0.0.1",
  "name": "fidl.test.json",
  "library_dependencies": [],
  "bits_declarations": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [
    {
      "name": "fidl.test.json/Parent",
      "location": {
        "filename": "json.fidl",
        "line": 5,
        "column": 10
      },
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
          "location": {
            "filename": "json.fidl",
            "line": 6,
            "column": 3
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "request",
                "subtype": "fidl.test.json/Parent",
                "nullable": false
              },
              "name": "request",
              "location": {
                "filename": "json.fidl",
                "line": 6,
                "column": 25
              },
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
      "location": {
        "filename": "json.fidl",
        "line": 9,
        "column": 10
      },
      "methods": [
        {
          "ordinal": 1722375644,
          "generated_ordinal": 1722375644,
          "name": "First",
          "location": {
            "filename": "json.fidl",
            "line": 6,
            "column": 3
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "request",
                "subtype": "fidl.test.json/Parent",
                "nullable": false
              },
              "name": "request",
              "location": {
                "filename": "json.fidl",
                "line": 6,
                "column": 25
              },
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
          "location": {
            "filename": "json.fidl",
            "line": 11,
            "column": 3
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "request",
                "subtype": "fidl.test.json/Parent",
                "nullable": false
              },
              "name": "request",
              "location": {
                "filename": "json.fidl",
                "line": 11,
                "column": 26
              },
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
  "xunion_declarations": [],
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

bool json_generator_test_error() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        EXPECT_TRUE(checkJSONGenerator(R"FIDL(
library fidl.test.json;

protocol Example {
   foo(string s) -> (int64 y) error uint32;
};

)FIDL",
                                       R"JSON({
  "version": "0.0.1",
  "name": "fidl.test.json",
  "library_dependencies": [],
  "bits_declarations": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [
    {
      "name": "fidl.test.json/Example",
      "location": {
        "filename": "json.fidl",
        "line": 4,
        "column": 10
      },
      "methods": [
        {
          "ordinal": 1369693400,
          "generated_ordinal": 1369693400,
          "name": "foo",
          "location": {
            "filename": "json.fidl",
            "line": 5,
            "column": 4
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "string",
                "nullable": false
              },
              "name": "s",
              "location": {
                "filename": "json.fidl",
                "line": 5,
                "column": 15
              },
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
                "kind": "identifier",
                "identifier": "fidl.test.json/Example_foo_Result",
                "nullable": false
              },
              "name": "result",
              "location": {
                "filename": "generated",
                "line": 6,
                "column": 1
              },
              "size": 16,
              "max_out_of_line": 0,
              "alignment": 8,
              "offset": 16,
              "max_handles": 0
            }
          ],
          "maybe_response_size": 32,
          "maybe_response_alignment": 8
        }
      ]
    }
  ],
  "struct_declarations": [
    {
      "name": "fidl.test.json/Example_foo_Response",
      "location": {
        "filename": "generated",
        "line": 2,
        "column": 1
      },
      "anonymous": false,
      "members": [
        {
          "type": {
            "kind": "primitive",
            "subtype": "int64"
          },
          "name": "y",
          "location": {
            "filename": "json.fidl",
            "line": 5,
            "column": 28
          },
          "size": 8,
          "max_out_of_line": 0,
          "alignment": 8,
          "offset": 0,
          "max_handles": 0
        }
      ],
      "size": 8,
      "max_out_of_line": 0,
      "alignment": 8,
      "max_handles": 0
    }
  ],
  "table_declarations": [],
  "union_declarations": [
    {
      "name": "fidl.test.json/Example_foo_Result",
      "location": {
        "filename": "generated",
        "line": 5,
        "column": 1
      },
      "maybe_attributes": [
        {
          "name": "Result",
          "value": ""
        }
      ],
      "members": [
        {
          "type": {
            "kind": "identifier",
            "identifier": "fidl.test.json/Example_foo_Response",
            "nullable": false
          },
          "name": "response",
          "location": {
            "filename": "generated",
            "line": 3,
            "column": 1
          },
          "size": 8,
          "max_out_of_line": 0,
          "alignment": 8,
          "offset": 8
        },
        {
          "type": {
            "kind": "primitive",
            "subtype": "uint32"
          },
          "name": "err",
          "location": {
            "filename": "generated",
            "line": 4,
            "column": 1
          },
          "size": 4,
          "max_out_of_line": 0,
          "alignment": 4,
          "offset": 8
        }
      ],
      "size": 16,
      "max_out_of_line": 0,
      "alignment": 8,
      "max_handles": 0
    }
  ],
  "xunion_declarations": [],
  "declaration_order": [
    "fidl.test.json/Example_foo_Response",
    "fidl.test.json/Example_foo_Result",
    "fidl.test.json/Example"
  ],
  "declarations": {
    "fidl.test.json/Example": "interface",
    "fidl.test.json/Example_foo_Response": "struct",
    "fidl.test.json/Example_foo_Result": "union"
  }
})JSON"));
    }

    END_TEST;
}

bool json_generator_test_byte_and_bytes() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        EXPECT_TRUE(checkJSONGenerator(R"FIDL(
library example;

struct ByteAndBytes {
  byte single_byte;
  bytes many_bytes;
  bytes:1024 only_one_k_bytes;
  bytes:1024? opt_only_one_k_bytes;
};

)FIDL",
                                       R"JSON({
  "version": "0.0.1",
  "name": "example",
  "library_dependencies": [],
  "bits_declarations": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [],
  "struct_declarations": [
    {
      "name": "example/ByteAndBytes",
      "location": {
        "filename": "json.fidl",
        "line": 4,
        "column": 8
      },
      "anonymous": false,
      "members": [
        {
          "type": {
            "kind": "primitive",
            "subtype": "uint8"
          },
          "name": "single_byte",
          "location": {
            "filename": "json.fidl",
            "line": 5,
            "column": 8
          },
          "size": 1,
          "max_out_of_line": 0,
          "alignment": 1,
          "offset": 0,
          "max_handles": 0
        },
        {
          "type": {
            "kind": "vector",
            "element_type": {
              "kind": "primitive",
              "subtype": "uint8"
            },
            "nullable": false
          },
          "name": "many_bytes",
          "location": {
            "filename": "json.fidl",
            "line": 6,
            "column": 9
          },
          "size": 16,
          "max_out_of_line": 4294967295,
          "alignment": 8,
          "offset": 8,
          "max_handles": 0
        },
        {
          "type": {
            "kind": "vector",
            "element_type": {
              "kind": "primitive",
              "subtype": "uint8"
            },
            "maybe_element_count": 1024,
            "nullable": false
          },
          "name": "only_one_k_bytes",
          "location": {
            "filename": "json.fidl",
            "line": 7,
            "column": 14
          },
          "size": 16,
          "max_out_of_line": 1024,
          "alignment": 8,
          "offset": 24,
          "max_handles": 0
        },
        {
          "type": {
            "kind": "vector",
            "element_type": {
              "kind": "primitive",
              "subtype": "uint8"
            },
            "maybe_element_count": 1024,
            "nullable": true
          },
          "name": "opt_only_one_k_bytes",
          "location": {
            "filename": "json.fidl",
            "line": 8,
            "column": 15
          },
          "size": 16,
          "max_out_of_line": 1024,
          "alignment": 8,
          "offset": 40,
          "max_handles": 0
        }
      ],
      "size": 56,
      "max_out_of_line": 4294967295,
      "alignment": 8,
      "max_handles": 0
    }
  ],
  "table_declarations": [],
  "union_declarations": [],
  "xunion_declarations": [],
  "declaration_order": [
    "example/ByteAndBytes"
  ],
  "declarations": {
    "example/ByteAndBytes": "struct"
  }
})JSON"));
    }

    END_TEST;
}

bool json_generator_test_bits() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        EXPECT_TRUE(checkJSONGenerator(R"FIDL(
library fidl.test.json;

bits Bits : uint64 {
    SMALLEST = 1;
    BIGGEST = 0x8000000000000000;
};

)FIDL",
                                       R"JSON(
{
  "version": "0.0.1",
  "name": "fidl.test.json",
  "library_dependencies": [],
  "bits_declarations": [
    {
      "name": "fidl.test.json/Bits",
      "type": {
        "kind": "primitive",
        "subtype": "uint64"
      },
      "mask": "9223372036854775809",
      "members": [
        {
          "name": "SMALLEST",
          "value": {
            "kind": "literal",
            "literal": {
              "kind": "numeric",
              "value": "1",
              "expression": "1"
            }
          }
        },
        {
          "name": "BIGGEST",
          "value": {
            "kind": "literal",
            "literal": {
              "kind": "numeric",
              "value": "9223372036854775808",
              "expression": "0x8000000000000000"
            }
          }
        }
      ]
    }
  ],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [],
  "struct_declarations": [],
  "table_declarations": [],
  "union_declarations": [],
  "xunion_declarations": [],
  "declaration_order": [
    "fidl.test.json/Bits"
  ],
  "declarations": {
    "fidl.test.json/Bits": "bits"
  }
}
)JSON"));
    }

    END_TEST;
}

bool json_generator_check_escaping() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library escapeme;

/// "pleaseescapethisdocommentproperly"
struct DocCommentWithQuotes {};
)FIDL");
    ASSERT_TRUE(library.Compile());
    auto json = library.GenerateJSON();
    ASSERT_STR_STR(
        json.c_str(),
        R"JSON("value": " \"pleaseescapethisdocommentproperly\"\n")JSON");

    END_TEST;
}

bool json_generator_constants() {
    BEGIN_TEST;

    for (int i = 0; i < kRepeatTestCount; i++) {
        EXPECT_TRUE(checkJSONGenerator(R"FIDL(
library values;

const uint8 UINT8 = 0b100;
const uint16 UINT16 = 0b100;
const uint32 UINT32 = 0b100;
const uint64 UINT64 = 0b100;
const int8 INT8 = 0b100;
const int16 INT16 = 0b100;
const int32 INT32 = 0b100;
const int64 INT64 = 0b100;
const float32 FLOAT32 = 3.14159;
const float64 FLOAT64 = 3.14159;
const bool BOOL = true;
const string STRING = "string";

enum Enum {
  E = 0b10101010;
};

bits Bits {
  B = 0x8;
};

struct Struct {
  int64 int64_with_default = 007;
  string string_with_default = "stuff";
  bool bool_with_default = true;
};

)FIDL",
                                       R"JSON(
{
  "version": "0.0.1",
  "name": "values",
  "library_dependencies": [],
  "bits_declarations": [
    {
      "name": "values/Bits",
      "type": {
        "kind": "primitive",
        "subtype": "uint32"
      },
      "mask": "8",
      "members": [
        {
          "name": "B",
          "value": {
            "kind": "literal",
            "literal": {
              "kind": "numeric",
              "value": "8",
              "expression": "0x8"
            }
          }
        }
      ]
    }
  ],
  "const_declarations": [
    {
      "name": "values/UINT8",
      "location": {
        "filename": "json.fidl",
        "line": 4,
        "column": 13
      },
      "type": {
        "kind": "primitive",
        "subtype": "uint8"
      },
      "value": {
        "kind": "literal",
        "literal": {
          "kind": "numeric",
          "value": "4",
          "expression": "0b100"
        }
      }
    },
    {
      "name": "values/UINT16",
      "location": {
        "filename": "json.fidl",
        "line": 5,
        "column": 14
      },
      "type": {
        "kind": "primitive",
        "subtype": "uint16"
      },
      "value": {
        "kind": "literal",
        "literal": {
          "kind": "numeric",
          "value": "4",
          "expression": "0b100"
        }
      }
    },
    {
      "name": "values/UINT32",
      "location": {
        "filename": "json.fidl",
        "line": 6,
        "column": 14
      },
      "type": {
        "kind": "primitive",
        "subtype": "uint32"
      },
      "value": {
        "kind": "literal",
        "literal": {
          "kind": "numeric",
          "value": "4",
          "expression": "0b100"
        }
      }
    },
    {
      "name": "values/UINT64",
      "location": {
        "filename": "json.fidl",
        "line": 7,
        "column": 14
      },
      "type": {
        "kind": "primitive",
        "subtype": "uint64"
      },
      "value": {
        "kind": "literal",
        "literal": {
          "kind": "numeric",
          "value": "4",
          "expression": "0b100"
        }
      }
    },
    {
      "name": "values/INT8",
      "location": {
        "filename": "json.fidl",
        "line": 8,
        "column": 12
      },
      "type": {
        "kind": "primitive",
        "subtype": "int8"
      },
      "value": {
        "kind": "literal",
        "literal": {
          "kind": "numeric",
          "value": "4",
          "expression": "0b100"
        }
      }
    },
    {
      "name": "values/INT16",
      "location": {
        "filename": "json.fidl",
        "line": 9,
        "column": 13
      },
      "type": {
        "kind": "primitive",
        "subtype": "int16"
      },
      "value": {
        "kind": "literal",
        "literal": {
          "kind": "numeric",
          "value": "4",
          "expression": "0b100"
        }
      }
    },
    {
      "name": "values/INT32",
      "location": {
        "filename": "json.fidl",
        "line": 10,
        "column": 13
      },
      "type": {
        "kind": "primitive",
        "subtype": "int32"
      },
      "value": {
        "kind": "literal",
        "literal": {
          "kind": "numeric",
          "value": "4",
          "expression": "0b100"
        }
      }
    },
    {
      "name": "values/INT64",
      "location": {
        "filename": "json.fidl",
        "line": 11,
        "column": 13
      },
      "type": {
        "kind": "primitive",
        "subtype": "int64"
      },
      "value": {
        "kind": "literal",
        "literal": {
          "kind": "numeric",
          "value": "4",
          "expression": "0b100"
        }
      }
    },
    {
      "name": "values/FLOAT32",
      "location": {
        "filename": "json.fidl",
        "line": 12,
        "column": 15
      },
      "type": {
        "kind": "primitive",
        "subtype": "float32"
      },
      "value": {
        "kind": "literal",
        "literal": {
          "kind": "numeric",
          "value": "3.14159",
          "expression": "3.14159"
        }
      }
    },
    {
      "name": "values/FLOAT64",
      "location": {
        "filename": "json.fidl",
        "line": 13,
        "column": 15
      },
      "type": {
        "kind": "primitive",
        "subtype": "float64"
      },
      "value": {
        "kind": "literal",
        "literal": {
          "kind": "numeric",
          "value": "3.14159",
          "expression": "3.14159"
        }
      }
    },
    {
      "name": "values/BOOL",
      "location": {
        "filename": "json.fidl",
        "line": 14,
        "column": 12
      },
      "type": {
        "kind": "primitive",
        "subtype": "bool"
      },
      "value": {
        "kind": "literal",
        "literal": {
          "kind": "true",
          "value": "true",
          "expression": "true"
        }
      }
    },
    {
      "name": "values/STRING",
      "location": {
        "filename": "json.fidl",
        "line": 15,
        "column": 14
      },
      "type": {
        "kind": "string",
        "nullable": false
      },
      "value": {
        "kind": "literal",
        "literal": {
          "kind": "string",
          "value": "string",
          "expression": "\"string\""
        }
      }
    }
  ],
  "enum_declarations": [
    {
      "name": "values/Enum",
      "location": {
        "filename": "json.fidl",
        "line": 17,
        "column": 6
      },
      "type": "uint32",
      "members": [
        {
          "name": "E",
          "location": {
            "filename": "json.fidl",
            "line": 18,
            "column": 3
          },
          "value": {
            "kind": "literal",
            "literal": {
              "kind": "numeric",
              "value": "170",
              "expression": "0b10101010"
            }
          }
        }
      ]
    }
  ],
  "interface_declarations": [],
  "struct_declarations": [
    {
      "name": "values/Struct",
      "location": {
        "filename": "json.fidl",
        "line": 25,
        "column": 8
      },
      "anonymous": false,
      "members": [
        {
          "type": {
            "kind": "primitive",
            "subtype": "int64"
          },
          "name": "int64_with_default",
          "location": {
            "filename": "json.fidl",
            "line": 26,
            "column": 9
          },
          "maybe_default_value": {
            "kind": "literal",
            "literal": {
              "kind": "numeric",
              "value": "007",
              "expression": "007"
            }
          },
          "size": 8,
          "max_out_of_line": 0,
          "alignment": 8,
          "offset": 0,
          "max_handles": 0
        },
        {
          "type": {
            "kind": "string",
            "nullable": false
          },
          "name": "string_with_default",
          "location": {
            "filename": "json.fidl",
            "line": 27,
            "column": 10
          },
          "maybe_default_value": {
            "kind": "literal",
            "literal": {
              "kind": "string",
              "value": "stuff",
              "expression": "\"stuff\""
            }
          },
          "size": 16,
          "max_out_of_line": 4294967295,
          "alignment": 8,
          "offset": 8,
          "max_handles": 0
        },
        {
          "type": {
            "kind": "primitive",
            "subtype": "bool"
          },
          "name": "bool_with_default",
          "location": {
            "filename": "json.fidl",
            "line": 28,
            "column": 8
          },
          "maybe_default_value": {
            "kind": "literal",
            "literal": {
              "kind": "true",
              "value": "true",
              "expression": "true"
            }
          },
          "size": 1,
          "max_out_of_line": 0,
          "alignment": 1,
          "offset": 24,
          "max_handles": 0
        }
      ],
      "size": 32,
      "max_out_of_line": 4294967295,
      "alignment": 8,
      "max_handles": 0
    }
  ],
  "table_declarations": [],
  "union_declarations": [],
  "xunion_declarations": [],
  "declaration_order": [
    "values/UINT8",
    "values/UINT64",
    "values/UINT32",
    "values/UINT16",
    "values/Struct",
    "values/STRING",
    "values/INT8",
    "values/INT64",
    "values/INT32",
    "values/INT16",
    "values/FLOAT64",
    "values/FLOAT32",
    "values/Enum",
    "values/Bits",
    "values/BOOL"
  ],
  "declarations": {
    "values/Bits": "bits",
    "values/UINT8": "const",
    "values/UINT16": "const",
    "values/UINT32": "const",
    "values/UINT64": "const",
    "values/INT8": "const",
    "values/INT16": "const",
    "values/INT32": "const",
    "values/INT64": "const",
    "values/FLOAT32": "const",
    "values/FLOAT64": "const",
    "values/BOOL": "const",
    "values/STRING": "const",
    "values/Enum": "enum",
    "values/Struct": "struct"
  }
}
)JSON"));
    }

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(json_generator_tests)
RUN_TEST(json_generator_test_empty_struct)
RUN_TEST(json_generator_test_struct)
RUN_TEST(json_generator_test_table)
RUN_TEST(json_generator_test_union)
RUN_TEST(json_generator_test_xunion)
RUN_TEST(json_generator_test_inheritance)
RUN_TEST(json_generator_test_inheritance_with_recursive_decl)
RUN_TEST(json_generator_test_error)
RUN_TEST(json_generator_test_byte_and_bytes)
RUN_TEST(json_generator_test_bits)
RUN_TEST(json_generator_check_escaping)
RUN_TEST(json_generator_constants)
END_TEST_CASE(json_generator_tests)
