// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/outputs.h"
#include "tools/kazoo/syscall_library.h"
#include "tools/kazoo/test.h"
#include "tools/kazoo/test_ir_test_kernel_cases.test.h"
#include "tools/kazoo/test_ir_test_rights_specs.test.h"
#include "tools/kazoo/test_ir_test_rust_selection.test.h"
#include "tools/kazoo/test_ir_test_selection.test.h"

namespace {

TEST(JsonOutput, KernelCases) {
  SyscallLibrary library;
  ASSERT_TRUE(SyscallLibraryLoader::FromJson(k_test_kernel_cases, &library));

  StringWriter writer;
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
          "is_array": false
        },
        {
          "name": "options",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "vmo",
          "type": "zx_handle_t",
          "is_array": false
        },
        {
          "name": "offset",
          "type": "uint64_t",
          "is_array": false
        },
        {
          "name": "size",
          "type": "uint64_t",
          "is_array": false
        },
        {
          "name": "addrs",
          "type": "zx_paddr_t",
          "is_array": true
        },
        {
          "name": "num_addrs",
          "type": "size_t",
          "is_array": false
        },
        {
          "name": "pmt",
          "type": "zx_handle_t",
          "is_array": true
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
          "is_array": false
        },
        {
          "name": "kind",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "action",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "options",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "ptr",
          "type": "any",
          "is_array": true
        },
        {
          "name": "ptr_size",
          "type": "size_t",
          "is_array": false
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
          "is_array": false
        },
        {
          "name": "options",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "bytes",
          "type": "any",
          "is_array": true
        },
        {
          "name": "num_bytes",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "handles",
          "type": "zx_handle_t",
          "is_array": true
        },
        {
          "name": "num_handles",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "actual_bytes",
          "type": "uint32_t",
          "is_array": true
        },
        {
          "name": "actual_handles",
          "type": "uint32_t",
          "is_array": true
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

  StringWriter writer;
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
          "is_array": true
        },
        {
          "name": "num_handles",
          "type": "size_t",
          "is_array": false
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
          "is_array": false
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
          "is_array": false
        },
        {
          "name": "op",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "offset",
          "type": "uint64_t",
          "is_array": false
        },
        {
          "name": "size",
          "type": "uint64_t",
          "is_array": false
        },
        {
          "name": "buffer",
          "type": "any",
          "is_array": true
        },
        {
          "name": "buffer_size",
          "type": "size_t",
          "is_array": false
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
          "is_array": true
        },
        {
          "name": "num_input",
          "type": "size_t",
          "is_array": false
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
          "type": "bool",
          "is_array": false
        },
        {
          "name": "b",
          "type": "uint8_t",
          "is_array": false
        },
        {
          "name": "d",
          "type": "int32_t",
          "is_array": false
        },
        {
          "name": "e",
          "type": "int64_t",
          "is_array": false
        },
        {
          "name": "f",
          "type": "uint16_t",
          "is_array": false
        },
        {
          "name": "g",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "h",
          "type": "uint64_t",
          "is_array": false
        },
        {
          "name": "i",
          "type": "size_t",
          "is_array": false
        },
        {
          "name": "j",
          "type": "uintptr_t",
          "is_array": false
        },
        {
          "name": "k",
          "type": "any",
          "is_array": true
        },
        {
          "name": "l",
          "type": "zx_time_t",
          "is_array": false
        },
        {
          "name": "m",
          "type": "zx_ticks_t",
          "is_array": false
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

  StringWriter writer;
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
          "is_array": true
        },
        {
          "name": "wake_count",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "current_value",
          "type": "zx_futex_t",
          "is_array": false
        },
        {
          "name": "requeue_ptr",
          "type": "zx_futex_t",
          "is_array": true
        },
        {
          "name": "requeue_count",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "new_requeue_owner",
          "type": "zx_handle_t",
          "is_array": false
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
          "is_array": false
        },
        {
          "name": "signals",
          "type": "zx_signals_t",
          "is_array": false
        },
        {
          "name": "deadline",
          "type": "zx_time_t",
          "is_array": false
        },
        {
          "name": "observed",
          "type": "zx_signals_t",
          "is_array": true
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
          "is_array": false
        },
        {
          "name": "data",
          "type": "any",
          "is_array": true
        },
        {
          "name": "offset",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "data_size",
          "type": "size_t",
          "is_array": false
        },
        {
          "name": "actual",
          "type": "size_t",
          "is_array": true
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
          "is_array": false
        },
        {
          "name": "bus",
          "type": "uint8_t",
          "is_array": false
        },
        {
          "name": "dev",
          "type": "uint8_t",
          "is_array": false
        },
        {
          "name": "func",
          "type": "uint8_t",
          "is_array": false
        },
        {
          "name": "offset",
          "type": "uint8_t",
          "is_array": false
        },
        {
          "name": "val",
          "type": "uint32_t",
          "is_array": true
        },
        {
          "name": "width",
          "type": "size_t",
          "is_array": false
        },
        {
          "name": "write",
          "type": "bool",
          "is_array": false
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
          "is_array": false
        },
        {
          "name": "options",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "topic",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "policy",
          "type": "any",
          "is_array": true
        },
        {
          "name": "policy_size",
          "type": "uint32_t",
          "is_array": false
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
          "is_array": false
        },
        {
          "name": "out",
          "type": "zx_time_t",
          "is_array": true
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

  StringWriter writer;
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
          "is_array": false
        },
        {
          "name": "options",
          "type": "uint32_t",
          "is_array": false
        },
        {
          "name": "out",
          "type": "zx_handle_t",
          "is_array": true
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
          "is_array": false
        }
      ],
      "return_type": "void"
    }
  ]
}
)");
}

}  // namespace
