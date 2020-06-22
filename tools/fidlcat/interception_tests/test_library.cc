// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/interception_tests/test_library.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "src/lib/fidl_codec/library_loader.h"

// Generated with go/fidlbolt using this text:
// library fidl.examples.echo;
//
// [Discoverable]
// protocol Echo {
//     EchoString(string? value) -> (string? response);
//     -> OnPong();
// };

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
        "filename": "fidlbolt.fidl",
        "line": 4,
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
          "ordinal": 2936880781197466513,
          "name": "EchoString",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 5,
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
                "filename": "fidlbolt.fidl",
                "line": 5,
                "column": 24
              },
              "field_shape_v1": {
                "offset": 16,
                "padding": 0
              }
            }
          ],
          "maybe_request_payload": "fidl.examples.echo/SomeLongAnonymousPrefix0",
          "maybe_request_type_shape_v1": {
            "inline_size": 32,
            "alignment": 8,
            "depth": 1,
            "max_handles": 0,
            "max_out_of_line": 4294967295,
            "has_padding": true,
            "has_flexible_envelope": false
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
                "filename": "fidlbolt.fidl",
                "line": 5,
                "column": 43
              },
              "field_shape_v1": {
                "offset": 16,
                "padding": 0
              }
            }
          ],
          "maybe_response_payload": "fidl.examples.echo/SomeLongAnonymousPrefix1",
          "maybe_response_type_shape_v1": {
            "inline_size": 32,
            "alignment": 8,
            "depth": 1,
            "max_handles": 0,
            "max_out_of_line": 4294967295,
            "has_padding": true,
            "has_flexible_envelope": false
          },
          "is_composed": false
        },
        {
          "ordinal": 1120886698987607603,
          "name": "OnPong",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 6,
            "column": 8
          },
          "has_request": false,
          "has_response": true,
          "maybe_response": [],
          "maybe_response_type_shape_v1": {
            "inline_size": 16,
            "alignment": 8,
            "depth": 0,
            "max_handles": 0,
            "max_out_of_line": 0,
            "has_padding": false,
            "has_flexible_envelope": false
          },
          "is_composed": false
        }
      ]
    }
  ],
  "service_declarations": [],
  "struct_declarations": [
    {
      "name": "fidl.examples.echo/SomeLongAnonymousPrefix0",
      "location": {
        "filename": "fidlbolt.fidl",
        "line": 5,
        "column": 15
      },
      "anonymous": true,
      "members": [
        {
          "type": {
            "kind": "string",
            "nullable": true
          },
          "name": "value",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 5,
            "column": 24
          },
          "field_shape_v1": {
            "offset": 0,
            "padding": 0
          }
        }
      ],
      "type_shape_v1": {
        "inline_size": 16,
        "alignment": 8,
        "depth": 1,
        "max_handles": 0,
        "max_out_of_line": 4294967295,
        "has_padding": true,
        "has_flexible_envelope": false
      }
    },
    {
      "name": "fidl.examples.echo/SomeLongAnonymousPrefix1",
      "location": {
        "filename": "fidlbolt.fidl",
        "line": 5,
        "column": 34
      },
      "anonymous": true,
      "members": [
        {
          "type": {
            "kind": "string",
            "nullable": true
          },
          "name": "response",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 5,
            "column": 43
          },
          "field_shape_v1": {
            "offset": 0,
            "padding": 0
          }
        }
      ],
      "type_shape_v1": {
        "inline_size": 16,
        "alignment": 8,
        "depth": 1,
        "max_handles": 0,
        "max_out_of_line": 4294967295,
        "has_padding": true,
        "has_flexible_envelope": false
      }
    }
  ],
  "table_declarations": [],
  "union_declarations": [],
  "type_alias_declarations": [],
  "declaration_order": [
    "fidl.examples.echo/Echo"
  ],
  "declarations": {
    "fidl.examples.echo/Echo": "interface",
    "fidl.examples.echo/SomeLongAnonymousPrefix0": "struct",
    "fidl.examples.echo/SomeLongAnonymousPrefix1": "struct",
    "fidl.examples.echo/SomeLongAnonymousPrefix2": "struct"
  }
}
)";

static fidl_codec::LibraryLoader* test_library_loader = nullptr;

fidl_codec::LibraryLoader* GetTestLibraryLoader() {
  if (test_library_loader == nullptr) {
    test_library_loader = new fidl_codec::LibraryLoader();
    fidl_codec::LibraryReadError err;
    test_library_loader->AddContent(echo_service, &err);
  }
  return test_library_loader;
}
