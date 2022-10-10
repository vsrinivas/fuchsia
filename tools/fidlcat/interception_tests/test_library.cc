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
// using zx;
//
// @discoverable
// protocol Echo {
//     EchoString(struct {
//         value string:optional;
//     }) -> (struct {
//         response string:optional;
//     });
//     EchoHandle(resource struct {
//         handle zx.handle;
//     }) -> (resource struct {
//         handle zx.handle;
//     });
//     -> OnPong();
// };
//     -> OnPong();
// };

std::string echo_service = R"({
  "name": "fidl.examples.echo",
  "library_dependencies": [
    {
      "name": "zx",
      "declarations": {
        "zx/rights": {
          "kind": "bits"
        },
        "zx/CHANNEL_MAX_MSG_BYTES": {
          "kind": "const"
        },
        "zx/CHANNEL_MAX_MSG_HANDLES": {
          "kind": "const"
        },
        "zx/MAX_NAME_LEN": {
          "kind": "const"
        },
        "zx/MAX_CPUS": {
          "kind": "const"
        },
        "zx/RIGHTS_BASIC": {
          "kind": "const"
        },
        "zx/RIGHTS_IO": {
          "kind": "const"
        },
        "zx/RIGHTS_PROPERTY": {
          "kind": "const"
        },
        "zx/RIGHTS_POLICY": {
          "kind": "const"
        },
        "zx/DEFAULT_CHANNEL_RIGHTS": {
          "kind": "const"
        },
        "zx/DEFAULT_EVENT_RIGHTS": {
          "kind": "const"
        },
        "zx/obj_type": {
          "kind": "enum"
        },
        "zx/handle": {
          "kind": "experimental_resource"
        },
        "zx/status": {
          "kind": "alias"
        },
        "zx/time": {
          "kind": "alias"
        },
        "zx/duration": {
          "kind": "alias"
        },
        "zx/ticks": {
          "kind": "alias"
        },
        "zx/koid": {
          "kind": "alias"
        },
        "zx/vaddr": {
          "kind": "alias"
        },
        "zx/paddr": {
          "kind": "alias"
        },
        "zx/paddr32": {
          "kind": "alias"
        },
        "zx/gpaddr": {
          "kind": "alias"
        },
        "zx/off": {
          "kind": "alias"
        },
        "zx/procarg": {
          "kind": "alias"
        },
        "zx/signals": {
          "kind": "alias"
        }
      }
    }
  ],
  "bits_declarations": [],
  "const_declarations": [],
  "enum_declarations": [],
  "experimental_resource_declarations": [],
  "protocol_declarations": [
    {
      "name": "fidl.examples.echo/Echo",
      "location": {
        "filename": "fidlbolt.fidl",
        "line": 33,
        "column": 10,
        "length": 4
      },
      "maybe_attributes": [
        {
          "name": "discoverable",
          "arguments": [],
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 32,
            "column": 1,
            "length": 13
          }
        }
      ],
      "composed_protocols": [],
      "methods": [
        {
          "ordinal": 2936880781197466513,
          "name": "EchoString",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 34,
            "column": 5,
            "length": 10
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "string",
                "nullable": true,
                "type_shape_v1": {
                  "inline_size": 16,
                  "alignment": 8,
                  "depth": 1,
                  "max_handles": 0,
                  "max_out_of_line": 4294967295,
                  "has_padding": true,
                  "has_flexible_envelope": false
                },
                "type_shape_v2": {
                  "inline_size": 16,
                  "alignment": 8,
                  "depth": 1,
                  "max_handles": 0,
                  "max_out_of_line": 4294967295,
                  "has_padding": true,
                  "has_flexible_envelope": false
                }
              },
              "name": "value",
              "location": {
                "filename": "fidlbolt.fidl",
                "line": 35,
                "column": 9,
                "length": 5
              },
              "field_shape_v1": {
                "offset": 16,
                "padding": 0
              },
              "field_shape_v2": {
                "offset": 16,
                "padding": 0
              }
            }
          ],
          "maybe_request_payload": {
            "kind": "identifier",
            "identifier": "fidl.examples.echo/EchoEchoStringRequest",
            "nullable": false,
            "type_shape_v1": {
              "inline_size": 16,
              "alignment": 8,
              "depth": 1,
              "max_handles": 0,
              "max_out_of_line": 4294967295,
              "has_padding": true,
              "has_flexible_envelope": false
            },
            "type_shape_v2": {
              "inline_size": 16,
              "alignment": 8,
              "depth": 1,
              "max_handles": 0,
              "max_out_of_line": 4294967295,
              "has_padding": true,
              "has_flexible_envelope": false
            }
          },
          "maybe_request_type_shape_v1": {
            "inline_size": 32,
            "alignment": 8,
            "depth": 1,
            "max_handles": 0,
            "max_out_of_line": 4294967295,
            "has_padding": true,
            "has_flexible_envelope": false
          },
          "maybe_request_type_shape_v2": {
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
                "nullable": true,
                "type_shape_v1": {
                  "inline_size": 16,
                  "alignment": 8,
                  "depth": 1,
                  "max_handles": 0,
                  "max_out_of_line": 4294967295,
                  "has_padding": true,
                  "has_flexible_envelope": false
                },
                "type_shape_v2": {
                  "inline_size": 16,
                  "alignment": 8,
                  "depth": 1,
                  "max_handles": 0,
                  "max_out_of_line": 4294967295,
                  "has_padding": true,
                  "has_flexible_envelope": false
                }
              },
              "name": "response",
              "location": {
                "filename": "fidlbolt.fidl",
                "line": 37,
                "column": 9,
                "length": 8
              },
              "field_shape_v1": {
                "offset": 16,
                "padding": 0
              },
              "field_shape_v2": {
                "offset": 16,
                "padding": 0
              }
            }
          ],
          "maybe_response_payload": {
            "kind": "identifier",
            "identifier": "fidl.examples.echo/EchoEchoStringTopResponse",
            "nullable": false,
            "type_shape_v1": {
              "inline_size": 16,
              "alignment": 8,
              "depth": 1,
              "max_handles": 0,
              "max_out_of_line": 4294967295,
              "has_padding": true,
              "has_flexible_envelope": false
            },
            "type_shape_v2": {
              "inline_size": 16,
              "alignment": 8,
              "depth": 1,
              "max_handles": 0,
              "max_out_of_line": 4294967295,
              "has_padding": true,
              "has_flexible_envelope": false
            }
          },
          "maybe_response_type_shape_v1": {
            "inline_size": 32,
            "alignment": 8,
            "depth": 1,
            "max_handles": 0,
            "max_out_of_line": 4294967295,
            "has_padding": true,
            "has_flexible_envelope": false
          },
          "maybe_response_type_shape_v2": {
            "inline_size": 32,
            "alignment": 8,
            "depth": 1,
            "max_handles": 0,
            "max_out_of_line": 4294967295,
            "has_padding": true,
            "has_flexible_envelope": false
          },
          "is_composed": false,
          "has_error": false
        },
        {
          "ordinal": 9059114273465311787,
          "name": "EchoHandle",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 39,
            "column": 5,
            "length": 10
          },
          "has_request": true,
          "maybe_request": [
            {
              "type": {
                "kind": "handle",
                "obj_type": 0,
                "subtype": "handle",
                "rights": 2147483648,
                "nullable": false,
                "type_shape_v1": {
                  "inline_size": 4,
                  "alignment": 4,
                  "depth": 0,
                  "max_handles": 1,
                  "max_out_of_line": 0,
                  "has_padding": false,
                  "has_flexible_envelope": false
                },
                "type_shape_v2": {
                  "inline_size": 4,
                  "alignment": 4,
                  "depth": 0,
                  "max_handles": 1,
                  "max_out_of_line": 0,
                  "has_padding": false,
                  "has_flexible_envelope": false
                }
              },
              "name": "handle",
              "location": {
                "filename": "fidlbolt.fidl",
                "line": 40,
                "column": 9,
                "length": 6
              },
              "field_shape_v1": {
                "offset": 16,
                "padding": 4
              },
              "field_shape_v2": {
                "offset": 16,
                "padding": 4
              }
            }
          ],
          "maybe_request_payload": {
            "kind": "identifier",
            "identifier": "fidl.examples.echo/EchoEchoHandleRequest",
            "nullable": false,
            "type_shape_v1": {
              "inline_size": 8,
              "alignment": 8,
              "depth": 0,
              "max_handles": 1,
              "max_out_of_line": 0,
              "has_padding": true,
              "has_flexible_envelope": false
            },
            "type_shape_v2": {
              "inline_size": 8,
              "alignment": 8,
              "depth": 0,
              "max_handles": 1,
              "max_out_of_line": 0,
              "has_padding": true,
              "has_flexible_envelope": false
            }
          },
          "maybe_request_type_shape_v1": {
            "inline_size": 24,
            "alignment": 8,
            "depth": 0,
            "max_handles": 1,
            "max_out_of_line": 0,
            "has_padding": true,
            "has_flexible_envelope": false
          },
          "maybe_request_type_shape_v2": {
            "inline_size": 24,
            "alignment": 8,
            "depth": 0,
            "max_handles": 1,
            "max_out_of_line": 0,
            "has_padding": true,
            "has_flexible_envelope": false
          },
          "has_response": true,
          "maybe_response": [
            {
              "type": {
                "kind": "handle",
                "obj_type": 0,
                "subtype": "handle",
                "rights": 2147483648,
                "nullable": false,
                "type_shape_v1": {
                  "inline_size": 4,
                  "alignment": 4,
                  "depth": 0,
                  "max_handles": 1,
                  "max_out_of_line": 0,
                  "has_padding": false,
                  "has_flexible_envelope": false
                },
                "type_shape_v2": {
                  "inline_size": 4,
                  "alignment": 4,
                  "depth": 0,
                  "max_handles": 1,
                  "max_out_of_line": 0,
                  "has_padding": false,
                  "has_flexible_envelope": false
                }
              },
              "name": "handle",
              "location": {
                "filename": "fidlbolt.fidl",
                "line": 42,
                "column": 9,
                "length": 6
              },
              "field_shape_v1": {
                "offset": 16,
                "padding": 4
              },
              "field_shape_v2": {
                "offset": 16,
                "padding": 4
              }
            }
          ],
          "maybe_response_payload": {
            "kind": "identifier",
            "identifier": "fidl.examples.echo/EchoEchoHandleTopResponse",
            "nullable": false,
            "type_shape_v1": {
              "inline_size": 8,
              "alignment": 8,
              "depth": 0,
              "max_handles": 1,
              "max_out_of_line": 0,
              "has_padding": true,
              "has_flexible_envelope": false
            },
            "type_shape_v2": {
              "inline_size": 8,
              "alignment": 8,
              "depth": 0,
              "max_handles": 1,
              "max_out_of_line": 0,
              "has_padding": true,
              "has_flexible_envelope": false
            }
          },
          "maybe_response_type_shape_v1": {
            "inline_size": 24,
            "alignment": 8,
            "depth": 0,
            "max_handles": 1,
            "max_out_of_line": 0,
            "has_padding": true,
            "has_flexible_envelope": false
          },
          "maybe_response_type_shape_v2": {
            "inline_size": 24,
            "alignment": 8,
            "depth": 0,
            "max_handles": 1,
            "max_out_of_line": 0,
            "has_padding": true,
            "has_flexible_envelope": false
          },
          "is_composed": false,
          "has_error": false
        },
        {
          "ordinal": 1120886698987607603,
          "name": "OnPong",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 44,
            "column": 8,
            "length": 6
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
          "maybe_response_type_shape_v2": {
            "inline_size": 16,
            "alignment": 8,
            "depth": 0,
            "max_handles": 0,
            "max_out_of_line": 0,
            "has_padding": false,
            "has_flexible_envelope": false
          },
          "is_composed": false,
          "has_error": false
        }
      ]
    }
  ],
  "service_declarations": [],
  "struct_declarations": [
    {
      "name": "fidl.examples.echo/EchoEchoStringRequest",
      "naming_context": [
        "Echo",
        "EchoString",
        "Request"
      ],
      "location": {
        "filename": "fidlbolt.fidl",
        "line": 34,
        "column": 16,
        "length": 45
      },
      "members": [
        {
          "type": {
            "kind": "string",
            "nullable": true,
            "type_shape_v1": {
              "inline_size": 16,
              "alignment": 8,
              "depth": 1,
              "max_handles": 0,
              "max_out_of_line": 4294967295,
              "has_padding": true,
              "has_flexible_envelope": false
            },
            "type_shape_v2": {
              "inline_size": 16,
              "alignment": 8,
              "depth": 1,
              "max_handles": 0,
              "max_out_of_line": 4294967295,
              "has_padding": true,
              "has_flexible_envelope": false
            }
          },
          "name": "value",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 35,
            "column": 9,
            "length": 5
          },
          "field_shape_v1": {
            "offset": 0,
            "padding": 0
          },
          "field_shape_v2": {
            "offset": 0,
            "padding": 0
          }
        }
      ],
      "resource": false,
      "type_shape_v1": {
        "inline_size": 16,
        "alignment": 8,
        "depth": 1,
        "max_handles": 0,
        "max_out_of_line": 4294967295,
        "has_padding": true,
        "has_flexible_envelope": false
      },
      "type_shape_v2": {
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
      "name": "fidl.examples.echo/EchoEchoStringTopResponse",
      "naming_context": [
        "Echo",
        "EchoString",
        "Response"
      ],
      "location": {
        "filename": "fidlbolt.fidl",
        "line": 36,
        "column": 12,
        "length": 48
      },
      "members": [
        {
          "type": {
            "kind": "string",
            "nullable": true,
            "type_shape_v1": {
              "inline_size": 16,
              "alignment": 8,
              "depth": 1,
              "max_handles": 0,
              "max_out_of_line": 4294967295,
              "has_padding": true,
              "has_flexible_envelope": false
            },
            "type_shape_v2": {
              "inline_size": 16,
              "alignment": 8,
              "depth": 1,
              "max_handles": 0,
              "max_out_of_line": 4294967295,
              "has_padding": true,
              "has_flexible_envelope": false
            }
          },
          "name": "response",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 37,
            "column": 9,
            "length": 8
          },
          "field_shape_v1": {
            "offset": 0,
            "padding": 0
          },
          "field_shape_v2": {
            "offset": 0,
            "padding": 0
          }
        }
      ],
      "resource": false,
      "type_shape_v1": {
        "inline_size": 16,
        "alignment": 8,
        "depth": 1,
        "max_handles": 0,
        "max_out_of_line": 4294967295,
        "has_padding": true,
        "has_flexible_envelope": false
      },
      "type_shape_v2": {
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
      "name": "fidl.examples.echo/EchoEchoHandleRequest",
      "naming_context": [
        "Echo",
        "EchoHandle",
        "Request"
      ],
      "location": {
        "filename": "fidlbolt.fidl",
        "line": 39,
        "column": 16,
        "length": 49
      },
      "members": [
        {
          "type": {
            "kind": "handle",
            "obj_type": 0,
            "subtype": "handle",
            "rights": 2147483648,
            "nullable": false,
            "type_shape_v1": {
              "inline_size": 4,
              "alignment": 4,
              "depth": 0,
              "max_handles": 1,
              "max_out_of_line": 0,
              "has_padding": false,
              "has_flexible_envelope": false
            },
            "type_shape_v2": {
              "inline_size": 4,
              "alignment": 4,
              "depth": 0,
              "max_handles": 1,
              "max_out_of_line": 0,
              "has_padding": false,
              "has_flexible_envelope": false
            }
          },
          "name": "handle",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 40,
            "column": 9,
            "length": 6
          },
          "field_shape_v1": {
            "offset": 0,
            "padding": 4
          },
          "field_shape_v2": {
            "offset": 0,
            "padding": 4
          }
        }
      ],
      "resource": true,
      "type_shape_v1": {
        "inline_size": 8,
        "alignment": 8,
        "depth": 0,
        "max_handles": 1,
        "max_out_of_line": 0,
        "has_padding": true,
        "has_flexible_envelope": false
      },
      "type_shape_v2": {
        "inline_size": 8,
        "alignment": 8,
        "depth": 0,
        "max_handles": 1,
        "max_out_of_line": 0,
        "has_padding": true,
        "has_flexible_envelope": false
      }
    },
    {
      "name": "fidl.examples.echo/EchoEchoHandleTopResponse",
      "naming_context": [
        "Echo",
        "EchoHandle",
        "Response"
      ],
      "location": {
        "filename": "fidlbolt.fidl",
        "line": 41,
        "column": 12,
        "length": 49
      },
      "members": [
        {
          "type": {
            "kind": "handle",
            "obj_type": 0,
            "subtype": "handle",
            "rights": 2147483648,
            "nullable": false,
            "type_shape_v1": {
              "inline_size": 4,
              "alignment": 4,
              "depth": 0,
              "max_handles": 1,
              "max_out_of_line": 0,
              "has_padding": false,
              "has_flexible_envelope": false
            },
            "type_shape_v2": {
              "inline_size": 4,
              "alignment": 4,
              "depth": 0,
              "max_handles": 1,
              "max_out_of_line": 0,
              "has_padding": false,
              "has_flexible_envelope": false
            }
          },
          "name": "handle",
          "location": {
            "filename": "fidlbolt.fidl",
            "line": 42,
            "column": 9,
            "length": 6
          },
          "field_shape_v1": {
            "offset": 0,
            "padding": 4
          },
          "field_shape_v2": {
            "offset": 0,
            "padding": 4
          }
        }
      ],
      "resource": true,
      "type_shape_v1": {
        "inline_size": 8,
        "alignment": 8,
        "depth": 0,
        "max_handles": 1,
        "max_out_of_line": 0,
        "has_padding": true,
        "has_flexible_envelope": false
      },
      "type_shape_v2": {
        "inline_size": 8,
        "alignment": 8,
        "depth": 0,
        "max_handles": 1,
        "max_out_of_line": 0,
        "has_padding": true,
        "has_flexible_envelope": false
      }
    }
  ],
  "external_struct_declarations": [],
  "table_declarations": [],
  "union_declarations": [],
  "alias_declarations": [],
  "declaration_order": [
    "fidl.examples.echo/EchoEchoHandleTopResponse",
    "fidl.examples.echo/EchoEchoHandleRequest",
    "fidl.examples.echo/EchoEchoStringTopResponse",
    "fidl.examples.echo/EchoEchoStringRequest",
    "fidl.examples.echo/Echo"
  ],
  "declarations": {
    "fidl.examples.echo/Echo": "protocol",
    "fidl.examples.echo/EchoEchoStringRequest": "struct",
    "fidl.examples.echo/EchoEchoStringTopResponse": "struct",
    "fidl.examples.echo/EchoEchoHandleRequest": "struct",
    "fidl.examples.echo/EchoEchoHandleTopResponse": "struct"
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
