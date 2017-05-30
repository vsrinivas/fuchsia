// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/process.h>
#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <mini-process/mini-process.h>
#include <unittest/unittest.h>

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define LOCAL_TRACE 0
#define LTRACEF(str, x...)                                  \
    do {                                                    \
        if (LOCAL_TRACE) {                                  \
            printf("%s:%d: " str, __func__, __LINE__, ##x); \
        }                                                   \
    } while (0)

// Tests that MX_INFO_TASK_STATS seems to work.
bool info_task_stats_smoke(void) {
    BEGIN_TEST;
    mx_info_task_stats_t info;
    ASSERT_EQ(mx_object_get_info(mx_process_self(), MX_INFO_TASK_STATS,
                                 &info, sizeof(info), NULL, NULL),
              NO_ERROR, "");
    ASSERT_GT(info.mem_committed_bytes, 0u, "");
    ASSERT_GE(info.mem_mapped_bytes, info.mem_committed_bytes, "");
    END_TEST;
}

// Structs to keep track of VMARs/mappings in the test child process.
typedef struct test_mapping {
    uintptr_t base;
    size_t size;
    uint32_t flags; // MX_INFO_MAPS_MMU_FLAG_PERM_{READ,WRITE,EXECUTE}
} test_mapping_t;

typedef struct test_mapping_info {
    uintptr_t vmar_base;
    size_t vmar_size;
    size_t num_mappings;
    test_mapping_t mappings[0]; // num_mappings entries
} test_mapping_info_t;

// Returns a process singleton. MX_INFO_PROCESS_MAPS can't run on the current
// process, so tests should use this instead.
// This handle is leaked, and we expect our process teardown to clean it up
// naturally.
mx_handle_t get_test_process_etc(const test_mapping_info_t** info) {
    static mx_handle_t test_process = MX_HANDLE_INVALID;
    static test_mapping_info_t* test_info = NULL;

    if (info != NULL) {
        *info = NULL;
    }
    if (test_process == MX_HANDLE_INVALID) {
        // We don't use this, but start_mini_process_etc wants a valid handle
        // that it can give to the new process (which will take ownership).
        mx_handle_t event;
        mx_status_t s = mx_event_create(0u, &event);
        if (s != NO_ERROR) {
            EXPECT_EQ(s, NO_ERROR, "mx_event_create"); // Poison the test.
            return MX_HANDLE_INVALID;
        }

        // Failures from here on will start to leak handles, but they'll
        // be cleaned up when this binary exits.

        mx_handle_t process;
        mx_handle_t vmar;
        static const char pname[] = "object-info-minipr";
        s = mx_process_create(mx_job_default(), pname, sizeof(pname),
                              /* options */ 0u, &process, &vmar);
        if (s != NO_ERROR) {
            EXPECT_EQ(s, NO_ERROR, "mx_process_create");
            return MX_HANDLE_INVALID;
        }

        mx_handle_t thread;
        static const char tname[] = "object-info-minith";
        s = mx_thread_create(process, tname, sizeof(tname),
                             /* options */ 0u, &thread);
        if (s != NO_ERROR) {
            EXPECT_EQ(s, NO_ERROR, "mx_thread_create");
            return MX_HANDLE_INVALID;
        }

        mx_handle_t minip_channel;
        // Start the process before we mess with the VMAR,
        // so we don't step on the mapping done by start_mini_process_etc.
        s = start_mini_process_etc(process, thread, vmar, event, &minip_channel);
        if (s != NO_ERROR) {
            EXPECT_EQ(s, NO_ERROR, "start_mini_process_etc");
            return MX_HANDLE_INVALID;
        }

        mx_handle_close(minip_channel);

        // Create a child VMAR and a mapping under it, so we have
        // something interesting to look at when getting the process's
        // memory maps. After this, the process maps should at least contain:
        //
        //   Root Aspace
        //   - Root VMAR
        //     - Code+stack mapping created by start_mini_process_etc
        //     - Sub VMAR created below
        //       - kNumMappings mappings created below

        static const size_t kNumMappings = 8;
        // Leaked on failure.
        test_mapping_info_t* ti = (test_mapping_info_t*)malloc(
            sizeof(*ti) + kNumMappings * sizeof(test_mapping_t));
        ti->num_mappings = kNumMappings;

        // Big enough to fit all of the mappings with some slop.
        ti->vmar_size = PAGE_SIZE * kNumMappings * 16;
        mx_handle_t sub_vmar;
        s = mx_vmar_allocate(vmar, /* offset */ 0,
                             ti->vmar_size,
                             MX_VM_FLAG_CAN_MAP_READ |
                                 MX_VM_FLAG_CAN_MAP_WRITE |
                                 MX_VM_FLAG_CAN_MAP_EXECUTE,
                             &sub_vmar, &ti->vmar_base);
        if (s != NO_ERROR) {
            EXPECT_EQ(s, NO_ERROR, "mx_vmar_allocate");
            return MX_HANDLE_INVALID;
        }

        mx_handle_t vmo;
        s = mx_vmo_create(PAGE_SIZE * kNumMappings, /* options */ 0u, &vmo);
        if (s != NO_ERROR) {
            EXPECT_EQ(s, NO_ERROR, "mx_vmo_create");
            return MX_HANDLE_INVALID;
        }

        // Map each page of the VMO to some arbitray location in the VMAR.
        for (size_t i = 0; i < kNumMappings; i++) {
            test_mapping_t* m = &ti->mappings[i];
            m->size = PAGE_SIZE;

            // Pick flags for this mapping; cycle through different
            // combinations for the test. Must always have READ set
            // to be mapped.
            m->flags = MX_VM_FLAG_PERM_READ;
            if (i & 1) {
                m->flags |= MX_VM_FLAG_PERM_WRITE;
            }
            if (i & 2) {
                m->flags |= MX_VM_FLAG_PERM_EXECUTE;
            }

            s = mx_vmar_map(sub_vmar, /* vmar_offset (ignored) */ 0,
                            vmo, /* vmo_offset */ i * PAGE_SIZE,
                            /* len */ PAGE_SIZE,
                            m->flags,
                            &m->base);
            if (s != NO_ERROR) {
                char msg[32];
                snprintf(msg, sizeof(msg), "mx_vmar_map: [%zd]", i);
                EXPECT_EQ(s, NO_ERROR, msg);
                return MX_HANDLE_INVALID;
            }
        }

        test_process = process;
        test_info = ti;
    }
    if (info != NULL) {
        *info = test_info;
    }
    return test_process;
}

mx_handle_t get_test_process(void) {
    return get_test_process_etc(NULL);
}

// Tests that MX_INFO_PROCESS_MAPS seems to work.
bool info_process_maps_smoke(void) {
    BEGIN_TEST;
    const test_mapping_info_t* test_info;
    const mx_handle_t process = get_test_process_etc(&test_info);
    ASSERT_NEQ(test_info, NULL, "get_test_process_etc");

    // Buffer big enough to read all of the test process's map entries.
    const size_t bufsize = test_info->num_mappings * 4 * sizeof(mx_info_maps_t);
    mx_info_maps_t* maps = (mx_info_maps_t*)malloc(bufsize);

    // Read the map entries.
    size_t actual;
    size_t avail;
    ASSERT_EQ(mx_object_get_info(process, MX_INFO_PROCESS_MAPS,
                                 maps, bufsize,
                                 &actual, &avail),
              NO_ERROR, "");
    EXPECT_EQ(actual, avail, "Should have read all entries");

    // The first two entries should always be the ASpace and root VMAR.
    ASSERT_GE(actual, 2u, "Root aspace/vmar missing?");
    EXPECT_EQ(maps[0].type, (uint32_t)MX_INFO_MAPS_TYPE_ASPACE, "");
    EXPECT_EQ(maps[0].depth, 0u, "ASpace depth");
    EXPECT_GT(maps[0].size, 1u * 1024 * 1024 * 1024 * 1024, "ASpace size");
    EXPECT_EQ(maps[1].type, (uint32_t)MX_INFO_MAPS_TYPE_VMAR, "");
    EXPECT_EQ(maps[1].depth, 1u, "Root VMAR depth");
    EXPECT_GT(maps[1].size, 1u * 1024 * 1024 * 1024 * 1024, "Root VMAR size");

    // Look for the VMAR and all of the mappings we created.
    bool saw_vmar = false;   // Whether we've seen our VMAR.
    bool under_vmar = false; // If we're looking at children of our VMAR.
    size_t vmar_depth = 0;
    uint32_t saw_mapping = 0u; // bitmask of mapping indices we've seen.
    ASSERT_LT(test_info->num_mappings, 32u, "");

    LTRACEF("\n");
    for (size_t i = 2; i < actual; i++) {
        mx_info_maps_t* entry = maps + i;
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "[%2zd] %*stype:%u base:0x%" PRIx64 " size:%" PRIu64,
                 i, (int)(entry->depth - 2) * 2, "",
                 entry->type, entry->base, entry->size);
        LTRACEF("%s\n", msg);
        // All entries should be children of the root VMAR.
        EXPECT_GT(entry->depth, 1u, msg);
        EXPECT_TRUE(entry->type >= MX_INFO_MAPS_TYPE_ASPACE &&
                        entry->type < MX_INFO_MAPS_TYPE_LAST,
                    msg);

        if (entry->type == MX_INFO_MAPS_TYPE_VMAR &&
            entry->base == test_info->vmar_base &&
            entry->size == test_info->vmar_size) {
            saw_vmar = true;
            under_vmar = true;
            vmar_depth = entry->depth;
        } else if (under_vmar) {
            if (entry->depth <= vmar_depth) {
                under_vmar = false;
                vmar_depth = 0;
            } else {
                // |entry| should be a child mapping of our VMAR.
                EXPECT_EQ((uint32_t)MX_INFO_MAPS_TYPE_MAPPING, entry->type,
                          msg);
                // The mapping should fit inside the VMAR.
                EXPECT_LE(test_info->vmar_base, entry->base, msg);
                EXPECT_LE(entry->base + entry->size,
                          test_info->vmar_base + test_info->vmar_size,
                          msg);
                // Look for it in the expected mappings.
                bool found = false;
                for (size_t j = 0; j < test_info->num_mappings; j++) {
                    const test_mapping_t* t = &test_info->mappings[j];
                    if (t->base == entry->base && t->size == entry->size) {
                        // Make sure we don't see duplicates.
                        EXPECT_EQ(0u, saw_mapping & (1 << j), msg);
                        saw_mapping |= 1 << j;
                        EXPECT_EQ(t->flags, entry->u.mapping.mmu_flags, msg);
                        found = true;
                        break;
                    }
                }
                EXPECT_TRUE(found, msg);
            }
        }
    }

    // Make sure we saw our VMAR and all of our mappings.
    EXPECT_TRUE(saw_vmar, "");
    EXPECT_EQ((uint32_t)(1 << test_info->num_mappings) - 1, saw_mapping, "");

    // Do one more read with a short buffer to test actual < avail.
    const size_t bufsize2 = actual * 3 / 4 * sizeof(mx_info_maps_t);
    mx_info_maps_t* maps2 = (mx_info_maps_t*)malloc(bufsize2);
    size_t actual2;
    size_t avail2;
    ASSERT_EQ(mx_object_get_info(process, MX_INFO_PROCESS_MAPS,
                                 maps2, bufsize2,
                                 &actual2, &avail2),
              NO_ERROR, "");
    EXPECT_LT(actual2, avail2, "");
    // mini-process is very simple, and won't have modified its own memory
    // maps since the previous dump. Its "committed_pages" values could be
    // different, though.
    EXPECT_EQ(avail, avail2, "");
    LTRACEF("\n");
    EXPECT_GT(actual2, 3u, ""); // Make sure we're looking at something.
    for (size_t i = 0; i < actual2; i++) {
        mx_info_maps_t* e1 = maps + i;
        mx_info_maps_t* e2 = maps2 + i;
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "[%2zd] %*stype:%u/%u base:0x%" PRIx64 "/0x%" PRIx64
                 " size:%" PRIu64 "/%" PRIu64,
                 i, (int)e1->depth * 2, "",
                 e1->type, e2->type, e1->base, e2->base, e1->size, e2->size);
        LTRACEF("%s\n", msg);
        EXPECT_EQ(e1->base, e2->base, msg);
        EXPECT_EQ(e1->size, e2->size, msg);
        EXPECT_EQ(e1->depth, e2->depth, msg);
        EXPECT_EQ(e1->type, e2->type, msg);
        if (e1->type == e2->type && e2->type == MX_INFO_MAPS_TYPE_MAPPING) {
            EXPECT_EQ(e1->u.mapping.mmu_flags, e2->u.mapping.mmu_flags, msg);
        }
    }

    free(maps);
    free(maps2);
    END_TEST;
}

bool info_process_maps_self_fails(void) {
    BEGIN_TEST;
    mx_info_maps_t maps[2];
    size_t actual;
    size_t avail;
    // It's illegal to look at your own maps, because the output buffer
    // lives inside the address space that's being examined.
    EXPECT_EQ(mx_object_get_info(mx_process_self(), MX_INFO_PROCESS_MAPS,
                                 maps, sizeof(maps), &actual, &avail),
              ERR_ACCESS_DENIED, "");
    END_TEST;
}

bool info_process_maps_invalid_handle_fails(void) {
    BEGIN_TEST;
    mx_info_maps_t maps[2];
    size_t actual;
    size_t avail;
    // Passing MX_HANDLE_INVALID should fail.
    EXPECT_EQ(mx_object_get_info(MX_HANDLE_INVALID, MX_INFO_PROCESS_MAPS,
                                 maps, sizeof(maps), &actual, &avail),
              ERR_BAD_HANDLE, "");
    END_TEST;
}

bool info_process_maps_non_process_handle_fails(void) {
    BEGIN_TEST;
    mx_info_maps_t maps[2];
    size_t actual;
    size_t avail;
    // Passing a job handle should fail.
    EXPECT_EQ(mx_object_get_info(mx_job_default(), MX_INFO_PROCESS_MAPS,
                                 maps, sizeof(maps), &actual, &avail),
              ERR_WRONG_TYPE, "");
    END_TEST;
}

bool info_process_maps_missing_rights_fails(void) {
    BEGIN_TEST;
    // Get the test process handle rights.
    mx_handle_t process = get_test_process();
    mx_info_handle_basic_t hi;
    ASSERT_EQ(mx_object_get_info(process, MX_INFO_HANDLE_BASIC,
                                 &hi, sizeof(hi), NULL, NULL),
              NO_ERROR, "");
    char msg[32];
    snprintf(msg, sizeof(msg), "rights 0x%" PRIx32, hi.rights);
    EXPECT_EQ(hi.rights & MX_RIGHT_READ, MX_RIGHT_READ, msg);

    // Create a handle without MX_RIGHT_READ.
    mx_handle_t handle;
    ASSERT_EQ(mx_handle_duplicate(get_test_process(),
                                  hi.rights & ~MX_RIGHT_READ, &handle),
              NO_ERROR, "");

    // Read should fail.
    mx_info_maps_t maps[2];
    size_t actual;
    size_t avail;
    EXPECT_EQ(mx_object_get_info(handle, MX_INFO_PROCESS_MAPS,
                                 maps, sizeof(maps), &actual, &avail),
              ERR_ACCESS_DENIED, "");

    mx_handle_close(handle);
    END_TEST;
}

bool info_process_maps_zero_buffer_succeeds(void) {
    BEGIN_TEST;
    size_t actual;
    size_t avail;
    // Passing a zero-sized null buffer should succeed.
    EXPECT_EQ(mx_object_get_info(get_test_process(), MX_INFO_PROCESS_MAPS,
                                 NULL, // buffer
                                 0,    // len
                                 &actual, &avail),
              NO_ERROR, "");
    EXPECT_EQ(0u, actual, "");
    EXPECT_GT(avail, 0u, "");
    END_TEST;
}

bool info_process_maps_null_avail_actual_succeeds(void) {
    BEGIN_TEST;
    mx_info_maps_t maps[2];
    EXPECT_EQ(mx_object_get_info(get_test_process(), MX_INFO_PROCESS_MAPS,
                                 maps, sizeof(maps),
                                 NULL,  // actual
                                 NULL), // avail
              NO_ERROR, "");
    END_TEST;
}

bool info_process_maps_bad_buffer_fails(void) {
    BEGIN_TEST;
    size_t actual;
    size_t avail;
    EXPECT_EQ(mx_object_get_info(get_test_process(), MX_INFO_PROCESS_MAPS,
                                 // Bad buffer pointer value.
                                 (mx_info_maps_t*)1,
                                 sizeof(mx_info_maps_t),
                                 &actual, &avail),
              ERR_INVALID_ARGS, "");
    END_TEST;
}

// Tests the behavior when passing a buffer that starts in mapped
// memory but crosses into unmapped memory.
bool info_process_maps_partially_unmapped_buffer_fails(void) {
    BEGIN_TEST;
    // Create a two-page VMAR.
    mx_handle_t vmar;
    uintptr_t vmar_addr;
    ASSERT_EQ(mx_vmar_allocate(mx_vmar_root_self(),
                               0, 2 * PAGE_SIZE,
                               MX_VM_FLAG_CAN_MAP_READ |
                                   MX_VM_FLAG_CAN_MAP_WRITE |
                                   MX_VM_FLAG_CAN_MAP_SPECIFIC,
                               &vmar, &vmar_addr),
              NO_ERROR, "");

    // Create a one-page VMO.
    mx_handle_t vmo;
    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &vmo), NO_ERROR, "");

    // Map the first page of the VMAR.
    uintptr_t vmo_addr;
    ASSERT_EQ(mx_vmar_map(vmar, 0, vmo, 0, PAGE_SIZE,
                          MX_VM_FLAG_SPECIFIC |
                              MX_VM_FLAG_PERM_READ |
                              MX_VM_FLAG_PERM_WRITE,
                          &vmo_addr),
              NO_ERROR, "");
    ASSERT_EQ(vmar_addr, vmo_addr, "");

    // Point to a spot in the mapped page just before the unmapped region.
    mx_info_maps_t* maps = (mx_info_maps_t*)(vmo_addr + PAGE_SIZE) - 2;

    size_t actual;
    size_t avail;
    EXPECT_EQ(mx_object_get_info(get_test_process(), MX_INFO_PROCESS_MAPS,
                                 maps, sizeof(mx_info_maps_t) * 4,
                                 &actual, &avail),
              // Bad user buffer should return ERR_INVALID_ARGS.
              ERR_INVALID_ARGS, "");

    mx_vmar_destroy(vmar);
    mx_handle_close(vmar);
    mx_handle_close(vmo);
    END_TEST;
}

bool info_process_maps_bad_actual_fails(void) {
    BEGIN_TEST;
    mx_info_maps_t maps[2];
    size_t avail;
    EXPECT_EQ(mx_object_get_info(get_test_process(), MX_INFO_PROCESS_MAPS,
                                 maps, sizeof(maps),
                                 // Bad actual pointer value.
                                 (size_t*)1,
                                 &avail),
              ERR_INVALID_ARGS, "");
    END_TEST;
}

bool info_process_maps_bad_avail_fails(void) {
    BEGIN_TEST;
    mx_info_maps_t maps[2];
    size_t actual;
    EXPECT_EQ(mx_object_get_info(get_test_process(), MX_INFO_PROCESS_MAPS,
                                 maps, sizeof(maps), &actual,
                                 // Bad available pointer value.
                                 (size_t*)1),
              ERR_INVALID_ARGS, "");
    END_TEST;
}

// MX_INFO_JOB_PROCESS/MX_INFO_JOB_CHILDREN tests

// Returns a job with the structure:
// - returned job
//   - child process 1
//   - child process 2
//   - child process 3 (kTestJobChildProcs)
//   - child job 1
//     - grandchild process 1.1
//     - grandchild job 1.1
//   - child job 2 (kTestJobChildJobs)
//     - grandchild process 2.1
//     - grandchild job 2.1
static const size_t kTestJobChildProcs = 3;
static const size_t kTestJobChildJobs = 2;
static mx_handle_t get_test_job(void) {
    static mx_handle_t test_job = MX_HANDLE_INVALID;

    if (test_job == MX_HANDLE_INVALID) {
        char msg[64];
        mx_handle_t root;
        mx_status_t s = mx_job_create(mx_job_default(), 0, &root);
        if (s != NO_ERROR) {
            EXPECT_EQ(s, NO_ERROR, "mx_job_create"); // Poison the test.
            return MX_HANDLE_INVALID;
        }
        for (size_t i = 0; i < kTestJobChildProcs; i++) {
            mx_handle_t proc;
            mx_handle_t vmar;
            s = mx_process_create(root, "child", 6, 0, &proc, &vmar);
            if (s != NO_ERROR) {
                snprintf(msg, sizeof(msg), "mx_process_create(child %zu)", i);
                goto fail;
            }
        }
        for (size_t i = 0; i < kTestJobChildJobs; i++) {
            mx_handle_t job;
            s = mx_job_create(root, 0, &job);
            if (s != NO_ERROR) {
                snprintf(msg, sizeof(msg), "mx_job_create(child %zu)", i);
                goto fail;
            }
            mx_handle_t proc;
            mx_handle_t vmar;
            s = mx_process_create(job, "grandchild", 6, 0, &proc, &vmar);
            if (s != NO_ERROR) {
                snprintf(msg, sizeof(msg), "mx_process_create(grandchild)");
                goto fail;
            }
            mx_handle_t subjob;
            s = mx_job_create(job, 0, &subjob);
            if (s != NO_ERROR) {
                snprintf(msg, sizeof(msg), "mx_job_create(grandchild)");
                goto fail;
            }
        }

        if (false) {
        fail:
            EXPECT_EQ(s, NO_ERROR, msg); // Poison the test
            mx_task_kill(root);          // Clean up all tasks; leaks handles
            return MX_HANDLE_INVALID;
        }
        test_job = root;
    }

    return test_job;
}

// The jobch_helper_* (job child helper) functions allow testing both
// MX_INFO_JOB_PROCESS and MX_INFO_JOB_CHILDREN.
bool jobch_helper_smoke(uint32_t topic, size_t expected_count) {
    BEGIN_TEST;
    mx_koid_t koids[32];
    size_t actual;
    size_t avail;
    EXPECT_EQ(mx_object_get_info(get_test_job(), topic,
                                 koids, sizeof(koids), &actual, &avail),
              NO_ERROR, "");
    EXPECT_EQ(expected_count, actual, "");
    EXPECT_EQ(expected_count, avail, "");

    // All returned koids should produce a valid handle when passed to
    // mx_object_get_child.
    for (size_t i = 0; i < actual; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "koid %zu", koids[i]);
        mx_handle_t h = MX_HANDLE_INVALID;
        EXPECT_EQ(mx_object_get_child(get_test_job(), koids[i],
                                      MX_RIGHT_SAME_RIGHTS, &h),
                  NO_ERROR, msg);
        mx_handle_close(h);
    }
    END_TEST;
}

bool info_job_processes_smoke(void) {
    return jobch_helper_smoke(MX_INFO_JOB_PROCESSES, kTestJobChildProcs);
}

bool info_job_children_smoke(void) {
    return jobch_helper_smoke(MX_INFO_JOB_CHILDREN, kTestJobChildJobs);
}

bool jobch_helper_invalid_handle_fails(uint32_t topic) {
    BEGIN_TEST;
    mx_koid_t koids[2];
    size_t actual;
    size_t avail;
    // Passing MX_HANDLE_INVALID should fail.
    EXPECT_EQ(mx_object_get_info(MX_HANDLE_INVALID, topic,
                                 koids, sizeof(koids), &actual, &avail),
              ERR_BAD_HANDLE, "");
    END_TEST;
}

bool info_job_processes_invalid_handle_fails(void) {
    return jobch_helper_invalid_handle_fails(MX_INFO_JOB_PROCESSES);
}

bool info_job_children_invalid_handle_fails(void) {
    return jobch_helper_invalid_handle_fails(MX_INFO_JOB_CHILDREN);
}

bool jobch_helper_non_job_handle_fails(uint32_t topic) {
    BEGIN_TEST;
    mx_koid_t koids[2];
    size_t actual;
    size_t avail;
    // Passing a process handle should fail.
    EXPECT_EQ(mx_object_get_info(mx_process_self(), topic, koids, sizeof(koids),
                                 &actual, &avail),
              ERR_WRONG_TYPE, "");
    END_TEST;
}

bool info_job_processes_non_job_handle_fails(void) {
    return jobch_helper_non_job_handle_fails(MX_INFO_JOB_PROCESSES);
}

bool info_job_children_non_job_handle_fails(void) {
    return jobch_helper_non_job_handle_fails(MX_INFO_JOB_CHILDREN);
}

bool jobch_helper_missing_rights_fails(uint32_t topic) {
    BEGIN_TEST;
    // Get our parent job's rights.
    mx_info_handle_basic_t hi;
    ASSERT_EQ(mx_object_get_info(get_test_job(), MX_INFO_HANDLE_BASIC,
                                 &hi, sizeof(hi), NULL, NULL),
              NO_ERROR, "");
    char msg[32];
    snprintf(msg, sizeof(msg), "rights 0x%" PRIx32, hi.rights);
    EXPECT_EQ(hi.rights & MX_RIGHT_ENUMERATE, MX_RIGHT_ENUMERATE, msg);

    // Create a handle without MX_RIGHT_ENUMERATE.
    mx_handle_t handle;
    ASSERT_EQ(mx_handle_duplicate(get_test_job(),
                                  hi.rights & ~MX_RIGHT_ENUMERATE, &handle),
              NO_ERROR, "");

    // Call should fail.
    mx_koid_t koids[2];
    size_t actual;
    size_t avail;
    EXPECT_EQ(mx_object_get_info(handle, topic, koids, sizeof(koids),
                                 &actual, &avail),
              ERR_ACCESS_DENIED, "");

    mx_handle_close(handle);
    END_TEST;
}

bool info_job_processes_missing_rights_fails(void) {
    return jobch_helper_missing_rights_fails(MX_INFO_JOB_PROCESSES);
}

bool info_job_children_missing_rights_fails(void) {
    return jobch_helper_missing_rights_fails(MX_INFO_JOB_CHILDREN);
}

bool jobch_helper_zero_buffer_succeeds(uint32_t topic, size_t expected_count) {
    BEGIN_TEST;
    size_t actual;
    size_t avail;
    // Passing a zero-sized null buffer should succeed.
    EXPECT_EQ(mx_object_get_info(get_test_job(), topic,
                                 NULL, // buffer
                                 0,    // len
                                 &actual, &avail),
              NO_ERROR, "");
    EXPECT_EQ(0u, actual, "");
    EXPECT_EQ(expected_count, avail, "");
    END_TEST;
}

bool info_job_processes_zero_buffer_succeeds(void) {
    return jobch_helper_zero_buffer_succeeds(
        MX_INFO_JOB_PROCESSES, kTestJobChildProcs);
}

bool info_job_children_zero_buffer_succeeds(void) {
    return jobch_helper_zero_buffer_succeeds(
        MX_INFO_JOB_CHILDREN, kTestJobChildJobs);
}

bool jobch_helper_short_buffer_succeeds(uint32_t topic,
                                        size_t expected_count) {
    BEGIN_TEST;
    mx_koid_t koids[1];
    size_t actual;
    size_t avail;
    // Passing a buffer shorter than avail should succeed.
    EXPECT_EQ(mx_object_get_info(get_test_job(), topic,
                                 koids,
                                 sizeof(koids),
                                 &actual, &avail),
              NO_ERROR, "");
    EXPECT_EQ(1u, actual, "");
    EXPECT_EQ(expected_count, avail, "");
    END_TEST;
}

bool info_job_processes_short_buffer_succeeds(void) {
    return jobch_helper_short_buffer_succeeds(
        MX_INFO_JOB_PROCESSES, kTestJobChildProcs);
}

bool info_job_children_short_buffer_succeeds(void) {
    return jobch_helper_short_buffer_succeeds(
        MX_INFO_JOB_CHILDREN, kTestJobChildJobs);
}

bool jobch_helper_null_avail_actual_succeeds(uint32_t topic) {
    BEGIN_TEST;
    mx_koid_t koids[2];
    EXPECT_EQ(mx_object_get_info(get_test_job(), topic,
                                 koids, sizeof(koids),
                                 NULL,  // actual
                                 NULL), // avail
              NO_ERROR, "");
    END_TEST;
}

bool info_job_processes_null_avail_actual_succeeds(void) {
    return jobch_helper_null_avail_actual_succeeds(MX_INFO_JOB_PROCESSES);
}

bool info_job_children_null_avail_actual_succeeds(void) {
    return jobch_helper_null_avail_actual_succeeds(MX_INFO_JOB_CHILDREN);
}

bool jobch_helper_bad_buffer_fails(uint32_t topic) {
    BEGIN_TEST;
    size_t actual;
    size_t avail;
    EXPECT_EQ(mx_object_get_info(get_test_job(), topic,
                                 // Bad buffer pointer value.
                                 (mx_koid_t*)1,
                                 sizeof(mx_koid_t),
                                 &actual, &avail),
              ERR_INVALID_ARGS, "");
    END_TEST;
}

bool info_job_processes_bad_buffer_fails(void) {
    return jobch_helper_bad_buffer_fails(MX_INFO_JOB_PROCESSES);
}

bool info_job_children_bad_buffer_fails(void) {
    return jobch_helper_bad_buffer_fails(MX_INFO_JOB_CHILDREN);
}

bool jobch_helper_bad_actual_fails(uint32_t topic) {
    BEGIN_TEST;
    mx_koid_t koids[2];
    size_t avail;
    EXPECT_EQ(mx_object_get_info(get_test_job(), topic,
                                 koids, sizeof(koids),
                                 // Bad actual pointer value.
                                 (size_t*)1,
                                 &avail),
              ERR_INVALID_ARGS, "");
    END_TEST;
}

bool info_job_processes_bad_actual_fails(void) {
    return jobch_helper_bad_actual_fails(MX_INFO_JOB_PROCESSES);
}

bool info_job_children_bad_actual_fails(void) {
    return jobch_helper_bad_actual_fails(MX_INFO_JOB_CHILDREN);
}

bool jobch_helper_bad_avail_fails(uint32_t topic) {
    BEGIN_TEST;
    mx_koid_t koids[2];
    size_t actual;
    EXPECT_EQ(mx_object_get_info(get_test_job(), topic,
                                 koids, sizeof(koids), &actual,
                                 // Bad available pointer value.
                                 (size_t*)1),
              ERR_INVALID_ARGS, "");
    END_TEST;
}

bool info_job_processes_bad_avail_fails(void) {
    return jobch_helper_bad_avail_fails(MX_INFO_JOB_PROCESSES);
}

bool info_job_children_bad_avail_fails(void) {
    return jobch_helper_bad_avail_fails(MX_INFO_JOB_CHILDREN);
}

// TODO(dbort): A lot of these tests would be good to run on any
// MX_INFO_* arg.

BEGIN_TEST_CASE(object_info_tests)
RUN_TEST(info_task_stats_smoke);
RUN_TEST(info_process_maps_smoke);
RUN_TEST(info_process_maps_self_fails);
RUN_TEST(info_process_maps_invalid_handle_fails);
RUN_TEST(info_process_maps_non_process_handle_fails);
RUN_TEST(info_process_maps_missing_rights_fails);
RUN_TEST(info_process_maps_zero_buffer_succeeds);
RUN_TEST(info_process_maps_null_avail_actual_succeeds);
RUN_TEST(info_process_maps_bad_buffer_fails);
RUN_TEST(info_process_maps_partially_unmapped_buffer_fails);
RUN_TEST(info_process_maps_bad_actual_fails);
RUN_TEST(info_process_maps_bad_avail_fails);
RUN_TEST(info_job_processes_smoke);
RUN_TEST(info_job_processes_invalid_handle_fails);
RUN_TEST(info_job_processes_non_job_handle_fails);
RUN_TEST(info_job_processes_missing_rights_fails);
RUN_TEST(info_job_processes_zero_buffer_succeeds);
RUN_TEST(info_job_processes_short_buffer_succeeds);
RUN_TEST(info_job_processes_null_avail_actual_succeeds);
RUN_TEST(info_job_processes_bad_buffer_fails);
RUN_TEST(info_job_processes_bad_actual_fails);
RUN_TEST(info_job_processes_bad_avail_fails);
RUN_TEST(info_job_children_smoke);
RUN_TEST(info_job_children_invalid_handle_fails);
RUN_TEST(info_job_children_non_job_handle_fails);
RUN_TEST(info_job_children_missing_rights_fails);
RUN_TEST(info_job_children_zero_buffer_succeeds);
RUN_TEST(info_job_children_short_buffer_succeeds);
RUN_TEST(info_job_children_null_avail_actual_succeeds);
RUN_TEST(info_job_children_bad_buffer_fails);
RUN_TEST(info_job_children_bad_actual_fails);
RUN_TEST(info_job_children_bad_avail_fails);
END_TEST_CASE(object_info_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
