// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/outputs.h"
#include "tools/kazoo/syscall_library.h"
#include "tools/kazoo/test.h"
#include "tools/kazoo/test_ir_test_kernel_cases.test.h"
#include "tools/kazoo/test_ir_test_out_voidptr.test.h"
#include "tools/kazoo/test_ir_test_rights_specs.test.h"
#include "tools/kazoo/test_ir_test_rust_selection.test.h"
#include "tools/kazoo/test_ir_test_selection.test.h"

namespace {

TEST(JsonOutput, KernelCases) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_kernel_cases, &library));

  Writer writer;
  ASSERT_TRUE(JsonOutput(library, &writer));

  EXPECT_EQ(writer.Out(),
            R"({
  "syscalls": [
    {
      "name": "kernelcases_bti_pin",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "handle",
          "type": "zx_handle_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "options",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "vmo",
          "type": "zx_handle_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "offset",
          "type": "uint64_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "size",
          "type": "uint64_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "addrs",
          "type": "zx_paddr_t",
          "is_array": true,
          "attributes": [
          ]
        },
        {
          "name": "num_addrs",
          "type": "size_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "pmt",
          "type": "zx_handle_t",
          "is_array": true,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    },
    {
      "name": "kernelcases_thread_exit",
      "attributes": [
        "*",
        "noreturn"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
      ],
      "return_type": "void"
    },
    {
      "name": "kernelcases_mtrace_control",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "handle",
          "type": "zx_handle_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "kind",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "action",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "options",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "ptr",
          "type": "any",
          "is_array": true,
          "attributes": [
          ]
        },
        {
          "name": "ptr_size",
          "type": "size_t",
          "is_array": false,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    },
    {
      "name": "kernelcases_read",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "handle",
          "type": "zx_handle_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "options",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "bytes",
          "type": "any",
          "is_array": true,
          "attributes": [
          ]
        },
        {
          "name": "num_bytes",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "handles",
          "type": "zx_handle_t",
          "is_array": true,
          "attributes": [
          ]
        },
        {
          "name": "num_handles",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "actual_bytes",
          "type": "uint32_t",
          "is_array": true,
          "attributes": [
          ]
        },
        {
          "name": "actual_handles",
          "type": "uint32_t",
          "is_array": true,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    },
    {
      "name": "kernelcases_compiled_out_in_non_test",
      "attributes": [
        "*",
        "testonly"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "a",
          "type": "int32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "b",
          "type": "int32_t",
          "is_array": false,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    }
  ]
}
)");
}

TEST(JsonOutput, RustCases) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_rust_selection, &library));

  Writer writer;
  ASSERT_TRUE(JsonOutput(library, &writer));

  EXPECT_EQ(writer.Out(),
            R"({
  "syscalls": [
    {
      "name": "rust_simple_case",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
      ],
      "return_type": "zx_time_t"
    },
    {
      "name": "rust_multiple_in_handles",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "handles",
          "type": "zx_handle_t",
          "is_array": true,
          "attributes": [
            "IN"
          ]
        },
        {
          "name": "num_handles",
          "type": "size_t",
          "is_array": false,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    },
    {
      "name": "rust_ano_ret_func",
      "attributes": [
        "*",
        "noreturn"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
      ],
      "return_type": "void"
    },
    {
      "name": "rust_no_return_value",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "x",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        }
      ],
      "return_type": "void"
    },
    {
      "name": "rust_inout_args",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "handle",
          "type": "zx_handle_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "op",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "offset",
          "type": "uint64_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "size",
          "type": "uint64_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "buffer",
          "type": "any",
          "is_array": true,
          "attributes": [
          ]
        },
        {
          "name": "buffer_size",
          "type": "size_t",
          "is_array": false,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    },
    {
      "name": "rust_const_input",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "input",
          "type": "uint8_t",
          "is_array": true,
          "attributes": [
            "IN"
          ]
        },
        {
          "name": "num_input",
          "type": "size_t",
          "is_array": false,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    },
    {
      "name": "rust_various_basic_type_names",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "a",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "b",
          "type": "uint8_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "d",
          "type": "int32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "e",
          "type": "int64_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "f",
          "type": "uint16_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "g",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "h",
          "type": "uint64_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "i",
          "type": "size_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "j",
          "type": "uintptr_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "k",
          "type": "any",
          "is_array": true,
          "attributes": [
          ]
        },
        {
          "name": "l",
          "type": "zx_time_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "m",
          "type": "zx_ticks_t",
          "is_array": false,
          "attributes": [
          ]
        }
      ],
      "return_type": "void"
    }
  ]
}
)");
}

TEST(JsonOutput, SelectionCases) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_selection, &library));

  Writer writer;
  ASSERT_TRUE(JsonOutput(library, &writer));

  EXPECT_EQ(writer.Out(),
            R"({
  "syscalls": [
    {
      "name": "selection_futex_requeue",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "value_ptr",
          "type": "zx_futex_t",
          "is_array": true,
          "attributes": [
            "IN"
          ]
        },
        {
          "name": "wake_count",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "current_value",
          "type": "zx_futex_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "requeue_ptr",
          "type": "zx_futex_t",
          "is_array": true,
          "attributes": [
            "IN"
          ]
        },
        {
          "name": "requeue_count",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "new_requeue_owner",
          "type": "zx_handle_t",
          "is_array": false,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    },
    {
      "name": "selection_object_wait_one",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "handle",
          "type": "zx_handle_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "signals",
          "type": "zx_signals_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "deadline",
          "type": "zx_time_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "observed",
          "type": "zx_signals_t",
          "is_array": true,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    },
    {
      "name": "selection_ktrace_read",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "handle",
          "type": "zx_handle_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "data",
          "type": "any",
          "is_array": true,
          "attributes": [
          ]
        },
        {
          "name": "offset",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "data_size",
          "type": "size_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "actual",
          "type": "size_t",
          "is_array": true,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    },
    {
      "name": "selection_pci_cfg_pio_rw",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "handle",
          "type": "zx_handle_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "bus",
          "type": "uint8_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "dev",
          "type": "uint8_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "func",
          "type": "uint8_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "offset",
          "type": "uint8_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "val",
          "type": "uint32_t",
          "is_array": true,
          "attributes": [
          ]
        },
        {
          "name": "width",
          "type": "size_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "write",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    },
    {
      "name": "selection_job_set_policy",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "handle",
          "type": "zx_handle_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "options",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "topic",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "policy",
          "type": "any",
          "is_array": true,
          "attributes": [
            "IN"
          ]
        },
        {
          "name": "policy_size",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    },
    {
      "name": "selection_clock_get",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "clock_id",
          "type": "zx_clock_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "out",
          "type": "zx_time_t",
          "is_array": true,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    }
  ]
}
)");
}

TEST(JsonOutput, RightsSpecs) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_rights_specs, &library));

  Writer writer;
  ASSERT_TRUE(JsonOutput(library, &writer));

  EXPECT_EQ(writer.Out(),
            R"({
  "syscalls": [
    {
      "name": "rights_do_thing",
      "attributes": [
        "*"
      ],
      "top_description": [
        "Create", "an", "exception", "channel", "for", "a", "given", "job", ",", "process", ",", "or", "thread", "."
      ],
      "requirements": [
        "handle", "must", "have", "ZX_RIGHT_INSPECT", "and", "have", "ZX_RIGHT_DUPLICATE", "and", "have", "ZX_RIGHT_TRANSFER", "and", "have", "ZX_RIGHT_MANAGE_THREAD", ".",
        "If", "handle", "is", "of", "type", "ZX_OBJ_TYPE_JOB", "or", "ZX_OBJ_TYPE_PROCESS", ",", "it", "must", "have", "ZX_RIGHT_ENUMERATE", "."
      ],
      "arguments": [
        {
          "name": "handle",
          "type": "zx_handle_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "options",
          "type": "uint32_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "out",
          "type": "zx_handle_t",
          "is_array": true,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    },
    {
      "name": "rights_no_short_desc",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
        "handle", "must", "have", "ZX_RIGHT_DESTROY", "."
      ],
      "arguments": [
        {
          "name": "handle",
          "type": "zx_handle_t",
          "is_array": false,
          "attributes": [
          ]
        }
      ],
      "return_type": "void"
    }
  ]
}
)");
}

TEST(JsonOutput, OutVoidptr) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_out_voidptr, &library));

  Writer writer;
  ASSERT_TRUE(JsonOutput(library, &writer));

  EXPECT_EQ(writer.Out(),
            R"({
  "syscalls": [
    {
      "name": "ovp_void_pointer_out",
      "attributes": [
        "*"
      ],
      "top_description": [
      ],
      "requirements": [
      ],
      "arguments": [
        {
          "name": "handle",
          "type": "zx_handle_t",
          "is_array": false,
          "attributes": [
          ]
        },
        {
          "name": "details",
          "type": "any",
          "is_array": true,
          "attributes": [
          ]
        }
      ],
      "return_type": "zx_status_t"
    }
  ]
}
)");
}

}  // namespace
