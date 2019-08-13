// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

constexpr const char kOneProtocolNoMethods[] = R"(
{
  "name": "zx",
  "interface_declarations": [
    {
      "name": "zx/Empty",
      "maybe_attributes": [
        {
          "name": "Transport",
          "value": "Syscall"
        }
      ],
      "methods": [
      ]
    }
  ]
}
)";

static inline constexpr const char kOneProtocolOneMethod[] = R"(
{
  "name": "zx",
  "interface_declarations": [
    {
      "name": "zx/Single",
      "maybe_attributes": [
        {
          "name": "Transport",
          "value": "Syscall"
        }
      ],
      "methods": [
        {
          "name": "DoThing",
          "has_request": true,
          "maybe_attributes": [
            {
              "name": "Doc",
              "value": " This does a single thing.\n"
            }
          ],
          "maybe_request": [
            {
              "type": {
                "kind": "primitive",
                "subtype": "int32"
              },
              "name": "an_input"
            }
          ],
          "has_response": true,
          "maybe_response": [
            {
              "type": {
                "kind": "primitive",
                "subtype": "int32"
              },
              "name": "status"
            }
          ]
        }
      ]
    }
  ]
}
)";

static inline constexpr const char kOneProtocolTwoMethods[] = R"(
{
  "name": "zx",
  "interface_declarations": [
    {
      "name": "zx/Couple",
      "maybe_attributes": [
        {
          "name": "Transport",
          "value": "Syscall"
        }
      ],
      "methods": [
        {
          "name": "DoThing",
          "has_request": true,
          "maybe_attributes": [
            {
              "name": "Doc",
              "value": " This does a single thing.\n"
            }
          ],
          "maybe_request": [
            {
              "type": {
                "kind": "primitive",
                "subtype": "int32"
              },
              "name": "an_input"
            }
          ],
          "has_response": true,
          "maybe_response": [
            {
              "type": {
                "kind": "primitive",
                "subtype": "int32"
              },
              "name": "status"
            }
          ]
        },
        {
          "name": "GetStuff",
          "has_request": true,
          "maybe_attributes": [
            {
              "name": "Doc",
              "value": " Does great stuff.\n"
            }
          ],
          "maybe_request": [
            {
              "type": {
                "kind": "primitive",
                "subtype": "int32"
              },
              "name": "an_input"
            },
            {
              "type": {
                "kind": "primitive",
                "subtype": "int32"
              },
              "name": "input2"
            }
          ],
          "has_response": true,
          "maybe_response": [
            {
              "type": {
                "kind": "primitive",
                "subtype": "int32"
              },
              "name": "status"
            }
          ]
        }
      ]
    }
  ]
}
)";
