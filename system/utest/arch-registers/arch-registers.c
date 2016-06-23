// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/prctl.h>
#include <magenta/syscalls.h>

static void fail(const char* function, int line, const char* message) {
    printf("arch_registers test failure in " __FILE__ ": %s: line %d:%s\n", function, line,
           message);
    _magenta_exit(-1);
}

typedef uintptr_t (*register_getter)(mx_handle_t);
typedef void (*register_setter)(mx_handle_t, uintptr_t);

typedef struct register_ops {
    register_getter get;
    register_setter set;
    const char* name;
} register_ops;

#if defined(__aarch64__)
static uintptr_t tpidrro_el0_get(mx_handle_t handle) {
    uintptr_t value;
    __asm__ volatile(
        "mrs %0,"
        "tpidrro_el0"
        : "=r"(value));
    return value;
}
static void tpidrro_el0_set(mx_handle_t handle, uintptr_t value) {
    mx_status_t status = _magenta_thread_arch_prctl(handle, ARCH_SET_TPIDRRO_EL0, &value);
    if (status != NO_ERROR)
        fail(__FUNCTION__, __LINE__, " failed to set!");
}
static register_ops ops[] = {
    {
        &tpidrro_el0_get, &tpidrro_el0_set, "tpidrro_el0",
    },
};
static uintptr_t make_valid_value(uintptr_t value) {
    return value;
}
#elif defined(__arm__)
static uintptr_t cp15_readonly_get(mx_handle_t handle) {
    uintptr_t value;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3"
                     : "=r"(value));
    return value;
}
static void cp15_readonly_set(mx_handle_t handle, uintptr_t value) {
    mx_status_t status = _magenta_thread_arch_prctl(handle, ARCH_SET_CP15_READONLY, &value);
    if (status != NO_ERROR)
        fail(__FUNCTION__, __LINE__, " failed to set!");
}
static uintptr_t cp15_readwrite_get(mx_handle_t handle) {
    uintptr_t value;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 2"
                     : "=r"(value));
    return value;
}
static void cp15_readwrite_set(mx_handle_t handle, uintptr_t value) {
    __asm__ volatile("mcr p15, 0, %0, c13, c0, 2"
                     :
                     : "r"(value));
}
static register_ops ops[] = {
    {
        &cp15_readonly_get, &cp15_readonly_set, "cp15_readonly",
    },
    {
        &cp15_readwrite_get, &cp15_readwrite_set, "cp15_readwrite",
    },
};
static uintptr_t make_valid_value(uintptr_t value) {
    return value;
}
#elif defined(__x86_64__)
static uintptr_t fs_get(mx_handle_t handle) {
    uintptr_t value;
    mx_status_t status = _magenta_thread_arch_prctl(handle, ARCH_GET_FS, &value);
    if (status != NO_ERROR)
        fail(__FUNCTION__, __LINE__, " failed to get!");
    return value;
}
static void fs_set(mx_handle_t handle, uintptr_t value) {
    mx_status_t status = _magenta_thread_arch_prctl(handle, ARCH_SET_FS, &value);
    if (status != NO_ERROR)
        fail(__FUNCTION__, __LINE__, " failed to set!");
}
static uintptr_t gs_get(mx_handle_t handle) {
    uintptr_t value;
    mx_status_t status = _magenta_thread_arch_prctl(handle, ARCH_GET_GS, &value);
    if (status != NO_ERROR)
        fail(__FUNCTION__, __LINE__, " failed to get!");
    return value;
}
static void gs_set(mx_handle_t handle, uintptr_t value) {
    mx_status_t status = _magenta_thread_arch_prctl(handle, ARCH_SET_GS, &value);
    if (status != NO_ERROR)
        fail(__FUNCTION__, __LINE__, " failed to set!");
}
static register_ops ops[] = {
    {
        &fs_get, &fs_set, "fs",
    },
    {
        &gs_get, &gs_set, "gs",
    },
};

static uint8_t vaddr_bits(void) {
    uint32_t eax, ebx, ecx, edx;
    eax = 0x80000008;
    __asm__ __volatile__("cpuid"
                         : "+a"(eax), "=c"(ecx), "=d"(edx), "=b"(ebx));
    return (eax >> 8) & 0xff;
}

static uintptr_t make_valid_value(uintptr_t value) {
    uint8_t vaddr_len = vaddr_bits();
    if (vaddr_len < 32) {
        fail(__FUNCTION__, __LINE__, " invalid vaddr len");
    }
    value &= (1 << vaddr_len) - 1;
    bool sign = !!(value & (1 << (vaddr_len - 1)));
    if (sign) {
        value |= ~((1 << vaddr_len) - 1);
    }
    return value;
}
#else
#error Unsupported architecture!
#endif

typedef struct context {
    uintptr_t key; // A different bit per thread so they test different values.
    mx_handle_t* thread;
} context;

static uint64_t values[] = {
    0x0000000000000000ull, 0xffffffffffffffffull, 0x5555555555555555ull,
    0xaaaaaaaaaaaaaaaaull, 0x0123456789abcdefull, 0xfedcba9876543210ull,
    0xababababababababull, 0x912f277f61b583a5ull, 0x3b7c08b96d727cedull,
};

static int test_entry_point(void* arg) {
    context* c = (context*)arg;

    // Test setting valid values.
    for (size_t idx = 0; idx < sizeof(values) / sizeof(*values); ++idx) {
        uintptr_t value = values[idx] ^ c->key;
        for (uintptr_t iteration = 0; iteration < 0x10ull; ++iteration) {
            value ^= (iteration << 12);
            for (size_t op_idx = 0; op_idx < sizeof(ops) / sizeof(*ops); ++op_idx) {
                register_ops* o = ops + op_idx;
                value ^= ((uintptr_t)op_idx << 24);
                uintptr_t real_value = make_valid_value(value);
                sched_yield();
                o->set(*c->thread, real_value);
                sched_yield();
                uintptr_t new_value = o->get(*c->thread);
                if (new_value != real_value)
                    fail(__FUNCTION__, __LINE__, o->name);
            }
        }
    }

    // Test bad op.
    uintptr_t value = (uintptr_t)0xabcdabcdabcdabcdull;
    mx_status_t status = _magenta_thread_arch_prctl(*c->thread, 42, &value);
    if (status != ERR_INVALID_ARGS)
        fail(__FUNCTION__, __LINE__, "failed to reject bad op");
    for (size_t op_idx = 0; op_idx < sizeof(ops) / sizeof(*ops); ++op_idx) {
        uintptr_t current_value = ops[op_idx].get(*c->thread);
        if (current_value == value)
            fail(__FUNCTION__, __LINE__, "modified value in invalid call");
    }

    // TODO(kulakowski) Re-enable this part of the test once we figure
    // out the right for this call.

    // Test bad handle. Assumes 0 is a valid prctl op.
    // uintptr_t original_value = (uintptrt_t)0x5678567856785678ull;
    // value = original_value;
    // status = _magenta_thread_arch_prctl(MX_HANDLE_INVALID, 0, &value);
    // if (status != ERR_INVALID_ARGS)
    //     fail(__FUNCTION__, __LINE__, "failed to reject bad handle");
    // if (value != original_value) fail(__FUNCTION__, __LINE__, "modified value in invalid call");
    // for (size_t op_idx = 0; op_idx < sizeof(ops) / sizeof(*ops); ++op_idx) {
    //     uintptr_t current_value = ops[op_idx].get(*c->thread);
    //     if (current_value == original_value)
    //         fail(__FUNCTION__, __LINE__, "modified arch register in invalid call");
    // }

    _magenta_thread_exit();
    return 0;
}

int main(void) {
    printf("Starting arch register test.\n");

#define num_threads 64

    mx_handle_t threads[num_threads] = {0};
    context contexts[num_threads] = {0};

    for (uintptr_t idx = 0; idx < num_threads; ++idx) {
        const char* thread_name = "arch register";

        contexts[idx] = (context){(1ull << idx), threads + idx};
        threads[idx] = _magenta_thread_create(test_entry_point, contexts + idx, thread_name, strlen(thread_name));
        if (threads[idx] < 0)
            fail(__FUNCTION__, __LINE__, "failed to create thread");
    }

    for (uintptr_t idx = 0; idx < num_threads; ++idx) {
        mx_status_t result = _magenta_handle_wait_one(threads[idx], MX_SIGNAL_SIGNALED,
                                                      MX_TIME_INFINITE, NULL, NULL);
        if (result != NO_ERROR)
            fail(__FUNCTION__, __LINE__, "failed to join thread");
        result = _magenta_handle_close(threads[idx]);
        if (result != NO_ERROR)
            fail(__FUNCTION__, __LINE__, "failed to close thread handle");
    }

    printf("Finishing arch register test.\n");
    return 0;
}
