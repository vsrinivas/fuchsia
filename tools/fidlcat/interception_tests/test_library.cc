// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_library.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "src/lib/fidl_codec/library_loader.h"

std::string echo_service = R"({
  "version": "0.0.1",
  "name": "fidl.examples.echo",
  "library_dependencies": [],
  "bits_declarations": [],
  "const_declarations": [],
  "enum_declarations": [],
  "interface_declarations": [
    {
      "name": "fidl.examples.echo/Echo",
      "location": {
        "filename": "../../garnet/examples/fidl/services/echo.test.fidl",
        "line": 8,
        "column": 10
      },
      "maybe_attributes": [
        {
          "name": "Discoverable",
          "value": ""
        }
      ],
      "methods": [
        {
          "ordinal": 4731840855269179392,
          "generated_ordinal": 2936880781197466513,
          "name": "EchoString",
          "location": {
            "filename": "../../garnet/examples/fidl/services/echo.test.fidl",
            "line": 9,
            "column": 5
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "string",
                "nullable": true
              },
              "name": "value",
              "location": {
                "filename": "../../garnet/examples/fidl/services/echo.test.fidl",
                "line": 9,
                "column": 24
              },
              "size": 16,
              "max_out_of_line": 4294967295,
              "alignment": 8,
              "offset": 16,
              "max_handles": 0,
              "field_shape_old": {
                "offset": 16,
                "padding": 0
              },
              "field_shape_v1": {
                "offset": 16,
                "padding": 0
              },
              "field_shape_v1_no_ee": {
                "offset": 16,
                "padding": 0
              }
            }
          ],
          "maybe_request_size": 32,
          "maybe_request_alignment": 8,
          "maybe_request_has_padding": true,
          "experimental_maybe_request_has_flexible_envelope": false,
          "maybe_request_type_shape_old": {
            "inline_size": 32,
            "alignment": 8,
            "depth": 1,
            "max_handles": 0,
            "max_out_of_line": 4294967295,
            "has_padding": true,
            "has_flexible_envelope": false,
            "contains_union": false
          },
          "maybe_request_type_shape_v1": {
            "inline_size": 32,
            "alignment": 8,
            "depth": 1,
            "max_handles": 0,
            "max_out_of_line": 4294967295,
            "has_padding": true,
            "has_flexible_envelope": false,
            "contains_union": false
          },
          "maybe_request_type_shape_v1_no_ee": {
            "inline_size": 32,
            "alignment": 8,
            "depth": 1,
            "max_handles": 0,
            "max_out_of_line": 4294967295,
            "has_padding": true,
            "has_flexible_envelope": false,
            "contains_union": false
          },
          "has_response": true,
          "maybe_response": [
            {
              "type": {
                "kind": "string",
                "nullable": true
              },
              "name": "response",
              "location": {
                "filename": "../../garnet/examples/fidl/services/echo.test.fidl",
                "line": 9,
                "column": 43
              },
              "size": 16,
              "max_out_of_line": 4294967295,
              "alignment": 8,
              "offset": 16,
              "max_handles": 0,
              "field_shape_old": {
                "offset": 16,
                "padding": 0
              },
              "field_shape_v1": {
                "offset": 16,
                "padding": 0
              },
              "field_shape_v1_no_ee": {
                "offset": 16,
                "padding": 0
              }
            }
          ],
          "maybe_response_size": 32,
          "maybe_response_alignment": 8,
          "maybe_response_has_padding": true,
          "experimental_maybe_response_has_flexible_envelope": false,
          "maybe_response_type_shape_old": {
            "inline_size": 32,
            "alignment": 8,
            "depth": 1,
            "max_handles": 0,
            "max_out_of_line": 4294967295,
            "has_padding": true,
            "has_flexible_envelope": false,
            "contains_union": false
          },
          "maybe_response_type_shape_v1": {
            "inline_size": 32,
            "alignment": 8,
            "depth": 1,
            "max_handles": 0,
            "max_out_of_line": 4294967295,
            "has_padding": true,
            "has_flexible_envelope": false,
            "contains_union": false
          },
          "maybe_response_type_shape_v1_no_ee": {
            "inline_size": 32,
            "alignment": 8,
            "depth": 1,
            "max_handles": 0,
            "max_out_of_line": 4294967295,
            "has_padding": true,
            "has_flexible_envelope": false,
            "contains_union": false
          },
          "is_composed": false
        }
      ]
    }
  ],
  "service_declarations": [
    {
      "name": "fidl.examples.echo/EchoService",
      "location": {
        "filename": "../../garnet/examples/fidl/services/echo.test.fidl",
        "line": 13,
        "column": 9
      },
      "maybe_attributes": [
        {
          "name": "Doc",
          "value": " A service with multiple Echo protocol implementations.\n"
        }
      ],
      "members": [
        {
          "type": {
            "kind": "identifier",
            "identifier": "fidl.examples.echo/Echo",
            "nullable": false
          },
          "name": "foo",
          "location": {
            "filename": "../../garnet/examples/fidl/services/echo.test.fidl",
            "line": 15,
            "column": 10
          },
          "maybe_attributes": [
            {
              "name": "Doc",
              "value": " An implementation of `Echo` that prefixes its output with \"foo: \".\n"
            }
          ]
        },
        {
          "type": {
            "kind": "identifier",
            "identifier": "fidl.examples.echo/Echo",
            "nullable": false
          },
          "name": "bar",
          "location": {
            "filename": "../../garnet/examples/fidl/services/echo.test.fidl",
            "line": 17,
            "column": 10
          },
          "maybe_attributes": [
            {
              "name": "Doc",
              "value": " An implementation of `Echo` that prefixes its output with \"bar: \".\n"
            }
          ]
        }
      ]
    }
  ],
  "struct_declarations": [],
  "table_declarations": [],
  "union_declarations": [],
  "xunion_declarations": [],
  "type_alias_declarations": [],
  "declaration_order": [
    "fidl.examples.echo/Echo",
    "fidl.examples.echo/EchoService"
  ],
  "declarations": {
    "fidl.examples.echo/Echo": "interface",
    "fidl.examples.echo/EchoService": "service"
  }
}
)";

static fidl_codec::LibraryLoader* test_library_loader = nullptr;

fidl_codec::LibraryLoader* GetTestLibraryLoader() {
  if (test_library_loader == nullptr) {
    test_library_loader = new fidl_codec::LibraryLoader();
    fidl_codec::LibraryReadError err;
    std::unique_ptr<std::istream> file = std::make_unique<std::istringstream>(echo_service);
    test_library_loader->Add(&file, &err);
  }
  return test_library_loader;
}
