// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/process.h>
#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/exception.h>
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

namespace {

// A function that returns a handle to get the info of.
// Typically get_test_process, get_test_job, mx_process_self, mx_job_default.
typedef mx_handle_t (*handle_source_fn)();

bool handle_valid_on_valid_handle_succeeds() {
    BEGIN_TEST;
    EXPECT_EQ(mx_object_get_info(mx_process_self(), MX_INFO_HANDLE_VALID,
                                 nullptr, 0, nullptr, nullptr),
              MX_OK);
    END_TEST;
}

bool handle_valid_on_closed_handle_fails() {
    BEGIN_TEST;
    // Create an event and show that it's valid.
    mx_handle_t event;
    ASSERT_EQ(mx_event_create(0u, &event), MX_OK);
    EXPECT_EQ(mx_object_get_info(event, MX_INFO_HANDLE_VALID,
                                 nullptr, 0, nullptr, nullptr),
              MX_OK);

    // Close the handle and show that it becomes invalid.
    mx_handle_close(event);
    EXPECT_NE(mx_object_get_info(event, MX_INFO_HANDLE_VALID,
                                 nullptr, 0, nullptr, nullptr),
              MX_OK);
    END_TEST;
}

// Tests that MX_INFO_TASK_STATS seems to work.
bool task_stats_smoke() {
    BEGIN_TEST;
    mx_info_task_stats_t info;
    ASSERT_EQ(mx_object_get_info(mx_process_self(), MX_INFO_TASK_STATS,
                                 &info, sizeof(info), nullptr, nullptr),
              MX_OK);
    ASSERT_GT(info.mem_private_bytes, 0u);
    ASSERT_GT(info.mem_shared_bytes, 0u);
    ASSERT_GE(info.mem_mapped_bytes,
              info.mem_private_bytes + info.mem_shared_bytes);

    ASSERT_GT(info.mem_scaled_shared_bytes, 0u);
    ASSERT_GT(info.mem_shared_bytes, info.mem_scaled_shared_bytes);
    END_TEST;
}

// Structs to keep track of VMARs/mappings in the test child process.
typedef struct test_mapping {
    uintptr_t base;
    size_t size;
    uint32_t flags; // MX_INFO_MAPS_MMU_FLAG_PERM_{READ,WRITE,EXECUTE}
} test_mapping_t;

// A VMO that the test process maps or has a handle to.
typedef struct test_vmo {
    mx_koid_t koid;
    size_t size;
    uint32_t flags; // MX_INFO_VMO_VIA_{HANDLE,MAPPING}
} test_vmo_t;

typedef struct test_mapping_info {
    uintptr_t vmar_base;
    size_t vmar_size;
    size_t num_mappings;
    test_mapping_t* mappings; // num_mappings entries
    size_t num_vmos;
    test_vmo_t* vmos; // num_vmos entries
} test_mapping_info_t;

// Gets the koid of the object pointed to by |handle|.
mx_status_t get_koid(mx_handle_t handle, mx_koid_t* koid) {
    mx_info_handle_basic_t info;
    mx_status_t s = mx_object_get_info(handle, MX_INFO_HANDLE_BASIC,
                                       &info, sizeof(info), nullptr, nullptr);
    if (s == MX_OK) {
        *koid = info.koid;
    }
    return s;
}

// Returns a process singleton. MX_INFO_PROCESS_MAPS can't run on the current
// process, so tests should use this instead.
// This handle is leaked, and we expect our process teardown to clean it up
// naturally.
mx_handle_t get_test_process_etc(const test_mapping_info_t** info) {
    static mx_handle_t test_process = MX_HANDLE_INVALID;
    static test_mapping_info_t* test_info = nullptr;

    if (info != nullptr) {
        *info = nullptr;
    }
    if (test_process == MX_HANDLE_INVALID) {
        // Create a VMO whose handle we'll give to the test process.
        // It will not be mapped into the test process's VMAR.
        const size_t unmapped_vmo_size = PAGE_SIZE;
        mx_handle_t unmapped_vmo;
        mx_status_t s = mx_vmo_create(
            unmapped_vmo_size, /* options */ 0u, &unmapped_vmo);
        if (s != MX_OK) {
            EXPECT_EQ(s, MX_OK, "mx_vmo_create"); // Poison the test.
            return MX_HANDLE_INVALID;
        }
        mx_koid_t unmapped_vmo_koid;
        s = get_koid(unmapped_vmo, &unmapped_vmo_koid);
        if (s != MX_OK) {
            EXPECT_EQ(s, MX_OK, "get_koid");
            return MX_HANDLE_INVALID;
        }
        // Try to set the name, but ignore any errors.
        static const char unmapped_vmo_name[] = "test:unmapped";
        mx_object_set_property(unmapped_vmo, MX_PROP_NAME,
                               unmapped_vmo_name, sizeof(unmapped_vmo_name));

        // Failures from here on will start to leak handles, but they'll
        // be cleaned up when this binary exits.

        mx_handle_t process;
        mx_handle_t vmar;
        static const char pname[] = "object-info-minipr";
        s = mx_process_create(mx_job_default(), pname, sizeof(pname),
                              /* options */ 0u, &process, &vmar);
        if (s != MX_OK) {
            EXPECT_EQ(s, MX_OK, "mx_process_create");
            return MX_HANDLE_INVALID;
        }

        mx_handle_t thread;
        static const char tname[] = "object-info-minith";
        s = mx_thread_create(process, tname, sizeof(tname),
                             /* options */ 0u, &thread);
        if (s != MX_OK) {
            EXPECT_EQ(s, MX_OK, "mx_thread_create");
            return MX_HANDLE_INVALID;
        }

        mx_handle_t minip_channel;
        // Start the process before we mess with the VMAR,
        // so we don't step on the mapping done by start_mini_process_etc.
        s = start_mini_process_etc(process, thread, vmar, unmapped_vmo,
                                   &minip_channel);
        if (s != MX_OK) {
            EXPECT_EQ(s, MX_OK, "start_mini_process_etc");
            return MX_HANDLE_INVALID;
        }
        unmapped_vmo = MX_HANDLE_INVALID; // Transferred to the test process.
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
        // Leaked on failure. Never freed on success.
        test_mapping_info_t* ti = (test_mapping_info_t*)malloc(sizeof(*ti));
        ti->num_mappings = kNumMappings;
        ti->mappings =
            (test_mapping_t*)malloc(kNumMappings * sizeof(test_mapping_t));

        // Big enough to fit all of the mappings with some slop.
        ti->vmar_size = PAGE_SIZE * kNumMappings * 16;
        mx_handle_t sub_vmar;
        s = mx_vmar_allocate(vmar, /* offset */ 0,
                             ti->vmar_size,
                             MX_VM_FLAG_CAN_MAP_READ |
                                 MX_VM_FLAG_CAN_MAP_WRITE |
                                 MX_VM_FLAG_CAN_MAP_EXECUTE,
                             &sub_vmar, &ti->vmar_base);
        if (s != MX_OK) {
            EXPECT_EQ(s, MX_OK, "mx_vmar_allocate");
            return MX_HANDLE_INVALID;
        }

        mx_handle_t vmo;
        const size_t vmo_size = PAGE_SIZE * kNumMappings;
        s = mx_vmo_create(vmo_size, /* options */ 0u, &vmo);
        if (s != MX_OK) {
            EXPECT_EQ(s, MX_OK, "mx_vmo_create");
            return MX_HANDLE_INVALID;
        }
        mx_koid_t vmo_koid;
        s = get_koid(vmo, &vmo_koid);
        if (s != MX_OK) {
            EXPECT_EQ(s, MX_OK, "get_koid");
            return MX_HANDLE_INVALID;
        }
        // Try to set the name, but ignore any errors.
        static const char vmo_name[] = "test:mapped";
        mx_object_set_property(vmo, MX_PROP_NAME, vmo_name, sizeof(vmo_name));

        // Record the VMOs now that we have both of them.
        ti->num_vmos = 2;
        ti->vmos = (test_vmo_t*)malloc(2 * sizeof(test_vmo_t));
        ti->vmos[0].koid = unmapped_vmo_koid;
        ti->vmos[0].size = unmapped_vmo_size;
        ti->vmos[0].flags = MX_INFO_VMO_VIA_HANDLE;
        ti->vmos[1].koid = vmo_koid;
        ti->vmos[1].size = vmo_size;
        ti->vmos[1].flags = MX_INFO_VMO_VIA_MAPPING;

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
            if (s != MX_OK) {
                char msg[32];
                snprintf(msg, sizeof(msg), "mx_vmar_map: [%zd]", i);
                EXPECT_EQ(s, MX_OK, msg);
                return MX_HANDLE_INVALID;
            }
        }
        mx_handle_close(vmo); // Kept alive by the VMAR.
        mx_handle_close(sub_vmar); // Kept alive by the process.

        test_process = process;
        test_info = ti;
    }
    if (info != nullptr) {
        *info = test_info;
    }
    return test_process;
}

mx_handle_t get_test_process() {
    return get_test_process_etc(nullptr);
}

// Tests that MX_INFO_PROCESS_MAPS seems to work.
bool process_maps_smoke() {
    BEGIN_TEST;
    const test_mapping_info_t* test_info;
    const mx_handle_t process = get_test_process_etc(&test_info);
    ASSERT_NONNULL(test_info, "get_test_process_etc");

    // Buffer big enough to read all of the test process's map entries.
    const size_t bufsize = test_info->num_mappings * 4 * sizeof(mx_info_maps_t);
    mx_info_maps_t* maps = (mx_info_maps_t*)malloc(bufsize);

    // Read the map entries.
    size_t actual;
    size_t avail;
    ASSERT_EQ(mx_object_get_info(process, MX_INFO_PROCESS_MAPS,
                                 maps, bufsize,
                                 &actual, &avail),
              MX_OK);
    EXPECT_EQ(actual, avail, "Should have read all entries");

    // The first two entries should always be the ASpace and root VMAR.
    ASSERT_GE(actual, 2u, "Root aspace/vmar missing?");
    EXPECT_EQ(maps[0].type, (uint32_t)MX_INFO_MAPS_TYPE_ASPACE);
    EXPECT_EQ(maps[0].depth, 0u, "ASpace depth");
    EXPECT_GT(maps[0].size, 1u * 1024 * 1024 * 1024 * 1024, "ASpace size");
    EXPECT_EQ(maps[1].type, (uint32_t)MX_INFO_MAPS_TYPE_VMAR);
    EXPECT_EQ(maps[1].depth, 1u, "Root VMAR depth");
    EXPECT_GT(maps[1].size, 1u * 1024 * 1024 * 1024 * 1024, "Root VMAR size");

    // Look for the VMAR and all of the mappings we created.
    bool saw_vmar = false; // Whether we've seen our VMAR.
    bool under_vmar = false; // If we're looking at children of our VMAR.
    size_t vmar_depth = 0;
    uint32_t saw_mapping = 0u; // bitmask of mapping indices we've seen.
    ASSERT_LT(test_info->num_mappings, 32u);

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
    EXPECT_TRUE(saw_vmar);
    EXPECT_EQ((uint32_t)(1 << test_info->num_mappings) - 1, saw_mapping);

    // Do one more read with a short buffer to test actual < avail.
    const size_t bufsize2 = actual * 3 / 4 * sizeof(mx_info_maps_t);
    mx_info_maps_t* maps2 = (mx_info_maps_t*)malloc(bufsize2);
    size_t actual2;
    size_t avail2;
    ASSERT_EQ(mx_object_get_info(process, MX_INFO_PROCESS_MAPS,
                                 maps2, bufsize2,
                                 &actual2, &avail2),
              MX_OK);
    EXPECT_LT(actual2, avail2);
    // mini-process is very simple, and won't have modified its own memory
    // maps since the previous dump. Its "committed_pages" values could be
    // different, though.
    EXPECT_EQ(avail, avail2);
    LTRACEF("\n");
    EXPECT_GT(actual2, 3u); // Make sure we're looking at something.
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

template <uint32_t Topic, typename EntryType>
bool self_fails() {
    BEGIN_TEST;
    EntryType entries[2];
    size_t actual;
    size_t avail;
    // It's illegal to look at your own entries, because the output buffer
    // lives inside the address space that's being examined.
    EXPECT_EQ(mx_object_get_info(mx_process_self(), Topic,
                                 entries, sizeof(entries), &actual, &avail),
              MX_ERR_ACCESS_DENIED);
    END_TEST;
}

template <uint32_t Topic, typename EntryType>
bool invalid_handle_fails() {
    BEGIN_TEST;
    EntryType entries[2];
    size_t actual;
    size_t avail;
    // Passing MX_HANDLE_INVALID should fail.
    EXPECT_EQ(mx_object_get_info(MX_HANDLE_INVALID, Topic,
                                 entries, sizeof(entries), &actual, &avail),
              MX_ERR_BAD_HANDLE);
    END_TEST;
}

template <uint32_t Topic, typename EntryType, handle_source_fn GetWrongHandle>
bool wrong_handle_type_fails() {
    BEGIN_TEST;
    EntryType entries[2];
    size_t actual;
    size_t avail;
    // Passing a handle to an unsupported object type should fail.
    EXPECT_NE(mx_object_get_info(GetWrongHandle(), Topic,
                                 entries, sizeof(entries), &actual, &avail),
              MX_OK);
    END_TEST;
}

template <uint32_t Topic, typename EntryType,
          handle_source_fn GetHandle, mx_rights_t MissingRights>
bool missing_rights_fails() {
    BEGIN_TEST;
    // Call should succeed with the default rights.
    mx_handle_t obj = GetHandle();
    EntryType entries[2];
    size_t actual;
    size_t avail;
    EXPECT_EQ(mx_object_get_info(obj, Topic,
                                 entries, sizeof(entries), &actual, &avail),
              MX_OK);

    // Get the test object handle rights.
    mx_info_handle_basic_t hi;
    ASSERT_EQ(mx_object_get_info(obj, MX_INFO_HANDLE_BASIC,
                                 &hi, sizeof(hi), nullptr, nullptr),
              MX_OK);
    char msg[32];
    snprintf(msg, sizeof(msg), "rights 0x%" PRIx32, hi.rights);
    EXPECT_EQ(hi.rights & MissingRights, MissingRights, msg);

    // Create a handle without the important rights.
    mx_handle_t handle;
    ASSERT_EQ(mx_handle_duplicate(obj, hi.rights & ~MissingRights, &handle),
              MX_OK);

    // Call should fail without these rights.
    EXPECT_EQ(mx_object_get_info(handle, Topic,
                                 entries, sizeof(entries), &actual, &avail),
              MX_ERR_ACCESS_DENIED);

    mx_handle_close(handle);
    END_TEST;
}

template <uint32_t Topic, typename EntryType, handle_source_fn GetHandle>
bool single_zero_buffer_fails() {
    BEGIN_TEST;
    EntryType entry;
    size_t actual;
    size_t avail;
    // Passing a zero-sized buffer to a topic that expects a single
    // in/out entry should fail.
    EXPECT_EQ(mx_object_get_info(GetHandle(), Topic,
                                 &entry, // buffer
                                 0, // len
                                 &actual, &avail),
              MX_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(0u, actual);
    EXPECT_GT(avail, 0u);
    END_TEST;
}

template <uint32_t Topic, handle_source_fn GetHandle>
bool multi_zero_buffer_succeeds() {
    BEGIN_TEST;
    size_t actual;
    size_t avail;
    // Passing a zero-sized null buffer to a topic that can handle multiple
    // in/out entries should succeed.
    EXPECT_EQ(mx_object_get_info(GetHandle(), Topic,
                                 nullptr, // buffer
                                 0, // len
                                 &actual, &avail),
              MX_OK);
    EXPECT_EQ(0u, actual);
    EXPECT_GT(avail, 0u);
    END_TEST;
}

template <uint32_t Topic, typename EntryType, handle_source_fn GetHandle>
bool short_buffer_succeeds() {
    BEGIN_TEST;
    EntryType entries[1];
    size_t actual;
    size_t avail;
    // Passing a buffer shorter than avail should succeed.
    EXPECT_EQ(mx_object_get_info(GetHandle(), Topic,
                                 entries,
                                 sizeof(entries),
                                 &actual, &avail),
              MX_OK);
    EXPECT_EQ(1u, actual);
    EXPECT_GT(avail, actual);
    END_TEST;
}

template <uint32_t Topic, typename EntryType, handle_source_fn GetHandle>
bool null_avail_actual_succeeds() {
    BEGIN_TEST;
    EntryType entries[2];
    EXPECT_EQ(mx_object_get_info(GetHandle(), Topic,
                                 entries, sizeof(entries),
                                 nullptr, // actual
                                 nullptr), // avail
              MX_OK);
    END_TEST;
}

template <uint32_t Topic, typename EntryType, handle_source_fn GetHandle>
bool bad_buffer_fails() {
    BEGIN_TEST;
    size_t actual;
    size_t avail;
    EXPECT_EQ(mx_object_get_info(GetHandle(), Topic,
                                 // Bad buffer pointer value.
                                 (EntryType*)1,
                                 sizeof(EntryType),
                                 &actual, &avail),
              MX_ERR_INVALID_ARGS);
    END_TEST;
}

// Tests the behavior when passing a buffer that starts in mapped
// memory but crosses into unmapped memory.
template <uint32_t Topic, typename EntryType, handle_source_fn GetHandle>
bool partially_unmapped_buffer_fails() {
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
              MX_OK);

    // Create a one-page VMO.
    mx_handle_t vmo;
    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &vmo), MX_OK);

    // Map the first page of the VMAR.
    uintptr_t vmo_addr;
    ASSERT_EQ(mx_vmar_map(vmar, 0, vmo, 0, PAGE_SIZE,
                          MX_VM_FLAG_SPECIFIC |
                              MX_VM_FLAG_PERM_READ |
                              MX_VM_FLAG_PERM_WRITE,
                          &vmo_addr),
              MX_OK);
    ASSERT_EQ(vmar_addr, vmo_addr);

    // Point to a spot in the mapped page just before the unmapped region:
    // the first entry will hit mapped memory, the second entry will hit
    // unmapped memory.
    EntryType* entries = (EntryType*)(vmo_addr + PAGE_SIZE) - 1;

    size_t actual;
    size_t avail;
    EXPECT_EQ(mx_object_get_info(GetHandle(), Topic,
                                 entries, sizeof(EntryType) * 4,
                                 &actual, &avail),
              // Bad user buffer should return MX_ERR_INVALID_ARGS.
              MX_ERR_INVALID_ARGS);

    mx_vmar_destroy(vmar);
    mx_handle_close(vmar);
    mx_handle_close(vmo);
    END_TEST;
}

template <uint32_t Topic, typename EntryType, handle_source_fn GetHandle>
bool bad_actual_fails() {
    BEGIN_TEST;
    EntryType entries[2];
    size_t avail;
    EXPECT_EQ(mx_object_get_info(GetHandle(), Topic,
                                 entries, sizeof(entries),
                                 // Bad actual pointer value.
                                 (size_t*)1,
                                 &avail),
              MX_ERR_INVALID_ARGS);
    END_TEST;
}

template <uint32_t Topic, typename EntryType, handle_source_fn GetHandle>
bool bad_avail_fails() {
    BEGIN_TEST;
    EntryType entries[2];
    size_t actual;
    EXPECT_EQ(mx_object_get_info(GetHandle(), Topic,
                                 entries, sizeof(entries), &actual,
                                 // Bad available pointer value.
                                 (size_t*)1),
              MX_ERR_INVALID_ARGS);
    END_TEST;
}

// Tests that MX_INFO_PROCESS_VMOS seems to work.
bool process_vmos_smoke() {
    BEGIN_TEST;
    const test_mapping_info_t* test_info;
    const mx_handle_t process = get_test_process_etc(&test_info);
    ASSERT_NONNULL(test_info, "get_test_process_etc");

    // Buffer big enough to read all of the test process's VMO entries.
    // There'll be one per mapping, one for the unmapped VMO, plus some
    // extras (at least the vDSO and the mini-process stack).
    const size_t bufsize =
        (test_info->num_mappings + 1 + 8) * sizeof(mx_info_vmo_t);
    mx_info_vmo_t* vmos = (mx_info_vmo_t*)malloc(bufsize);

    // Read the VMO entries.
    size_t actual;
    size_t avail;
    ASSERT_EQ(mx_object_get_info(process, MX_INFO_PROCESS_VMOS,
                                 vmos, bufsize,
                                 &actual, &avail),
              MX_OK);
    EXPECT_EQ(actual, avail, "Should have read all entries");

    // Look for the expected VMOs.
    uint32_t saw_vmo = 0u; // Bitmask of VMO indices we've seen
    ASSERT_LT(test_info->num_vmos, 32u);

    LTRACEF("\n");
    for (size_t i = 0; i < actual; i++) {
        mx_info_vmo_t* entry = vmos + i;
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "[%2zd] koid:%" PRIu64 " name:'%s' size:%" PRIu64
                 " flags:0x%" PRIx32,
                 i, entry->koid, entry->name, entry->size_bytes, entry->flags);
        LTRACEF("%s\n", msg);

        // Look for it in the expected VMOs. We won't find all VMOs here,
        // since we don't track the vDSO or mini-process stack.
        for (size_t j = 0; j < test_info->num_vmos; j++) {
            const test_vmo_t* t = &test_info->vmos[j];
            if (t->koid == entry->koid && t->size == entry->size_bytes) {
                // These checks aren't appropriate for all VMOs.
                // The VMOs we track are:
                // - Only mapped or via handle, not both
                // - Not clones
                // - Not shared
                EXPECT_EQ(entry->parent_koid, 0u, msg);
                EXPECT_EQ(entry->num_children, 0u, msg);
                EXPECT_EQ(entry->share_count, 1u, msg);
                EXPECT_EQ(t->flags & entry->flags, t->flags, msg);
                if (entry->flags & MX_INFO_VMO_VIA_HANDLE) {
                    EXPECT_EQ(entry->num_mappings, 0u, msg);
                } else {
                    EXPECT_NE(entry->flags & MX_INFO_VMO_VIA_MAPPING, 0u, msg);
                    EXPECT_EQ(
                        entry->num_mappings, test_info->num_mappings, msg);
                }
                EXPECT_EQ(entry->flags & MX_INFO_VMO_IS_COW_CLONE, 0u, msg);

                saw_vmo |= 1 << j; // Duplicates are fine and expected
                break;
            }
        }

        // All of our VMOs should be paged, not physical.
        EXPECT_EQ(MX_INFO_VMO_TYPE(entry->flags), MX_INFO_VMO_TYPE_PAGED, msg);

        // Each entry should be via either map or handle, but not both.
        // NOTE: This could change in the future, but currently reflects
        // the way things work.
        const uint32_t kViaMask =
            MX_INFO_VMO_VIA_HANDLE | MX_INFO_VMO_VIA_MAPPING;
        EXPECT_NE(entry->flags & kViaMask, kViaMask, msg);

        // TODO(dbort): Test more fields/flags of mx_info_vmo_t by adding some
        // clones, shared VMOs, mapped+handle VMOs, physical VMOs if possible.
        // All but committed_bytes should be predictable.
    }

    // Make sure we saw all of the expected VMOs.
    EXPECT_EQ((uint32_t)(1 << test_info->num_vmos) - 1, saw_vmo);

    // Do one more read with a short buffer to test actual < avail.
    const size_t bufsize2 = actual * 3 / 4 * sizeof(mx_info_vmo_t);
    mx_info_vmo_t* vmos2 = (mx_info_vmo_t*)malloc(bufsize2);
    size_t actual2;
    size_t avail2;
    ASSERT_EQ(mx_object_get_info(process, MX_INFO_PROCESS_VMOS,
                                 vmos2, bufsize2,
                                 &actual2, &avail2),
              MX_OK);
    EXPECT_LT(actual2, avail2);
    // mini-process is very simple, and won't have modified its own set of VMOs
    // since the previous dump.
    EXPECT_EQ(avail, avail2);
    LTRACEF("\n");
    EXPECT_GT(actual2, 3u); // Make sure we're looking at something.
    for (size_t i = 0; i < actual2; i++) {
        mx_info_vmo_t* e1 = vmos + i;
        mx_info_vmo_t* e2 = vmos2 + i;
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "[%2zd] koid:%" PRIu64 "/%" PRIu64 " name:'%s'/'%s' "
                 "size:%" PRIu64 "/%" PRIu64 " flags:0x%" PRIx32 "/0x%" PRIx32,
                 i, e1->koid, e2->koid, e1->name, e2->name,
                 e1->size_bytes, e2->size_bytes, e1->flags, e2->flags);
        LTRACEF("%s\n", msg);
        EXPECT_EQ(e1->koid, e2->koid, msg);
        EXPECT_EQ(e1->size_bytes, e2->size_bytes, msg);
        EXPECT_EQ(e1->flags, e2->flags, msg);
        if (e1->flags == e2->flags && e2->flags & MX_INFO_VMO_VIA_HANDLE) {
            EXPECT_EQ(e1->handle_rights, e2->handle_rights, msg);
        }
    }

    free(vmos);
    free(vmos2);
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
const size_t kTestJobChildProcs = 3;
const size_t kTestJobChildJobs = 2;
mx_handle_t get_test_job() {
    static mx_handle_t test_job = MX_HANDLE_INVALID;

    if (test_job == MX_HANDLE_INVALID) {
        char msg[64];
        mx_handle_t root;
        mx_status_t s = mx_job_create(mx_job_default(), 0, &root);
        if (s != MX_OK) {
            EXPECT_EQ(s, MX_OK, "mx_job_create"); // Poison the test.
            return MX_HANDLE_INVALID;
        }
        for (size_t i = 0; i < kTestJobChildProcs; i++) {
            mx_handle_t proc;
            mx_handle_t vmar;
            s = mx_process_create(root, "child", 6, 0, &proc, &vmar);
            if (s != MX_OK) {
                snprintf(msg, sizeof(msg), "mx_process_create(child %zu)", i);
                goto fail;
            }
        }
        for (size_t i = 0; i < kTestJobChildJobs; i++) {
            mx_handle_t job;
            s = mx_job_create(root, 0, &job);
            if (s != MX_OK) {
                snprintf(msg, sizeof(msg), "mx_job_create(child %zu)", i);
                goto fail;
            }
            mx_handle_t proc;
            mx_handle_t vmar;
            s = mx_process_create(job, "grandchild", 6, 0, &proc, &vmar);
            if (s != MX_OK) {
                snprintf(msg, sizeof(msg), "mx_process_create(grandchild)");
                goto fail;
            }
            mx_handle_t subjob;
            s = mx_job_create(job, 0, &subjob);
            if (s != MX_OK) {
                snprintf(msg, sizeof(msg), "mx_job_create(grandchild)");
                goto fail;
            }
        }

        if (false) {
        fail:
            EXPECT_EQ(s, MX_OK, msg); // Poison the test
            mx_task_kill(root); // Clean up all tasks; leaks handles
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
              MX_OK);
    EXPECT_EQ(expected_count, actual);
    EXPECT_EQ(expected_count, avail);

    // All returned koids should produce a valid handle when passed to
    // mx_object_get_child.
    for (size_t i = 0; i < actual; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "koid %zu", koids[i]);
        mx_handle_t h = MX_HANDLE_INVALID;
        EXPECT_EQ(mx_object_get_child(get_test_job(), koids[i],
                                      MX_RIGHT_SAME_RIGHTS, &h),
                  MX_OK, msg);
        mx_handle_close(h);
    }
    END_TEST;
}

bool job_processes_smoke() {
    return jobch_helper_smoke(MX_INFO_JOB_PROCESSES, kTestJobChildProcs);
}

bool job_children_smoke() {
    return jobch_helper_smoke(MX_INFO_JOB_CHILDREN, kTestJobChildJobs);
}

} // namespace

// Tests that should pass for any topic. Use the wrappers below instead of
// calling this directly.
#define _RUN_COMMON_TESTS(topic, entry_type, get_handle)                   \
    RUN_TEST((invalid_handle_fails<topic, entry_type>));                   \
    RUN_TEST((null_avail_actual_succeeds<topic, entry_type, get_handle>)); \
    RUN_TEST((bad_buffer_fails<topic, entry_type, get_handle>));           \
    RUN_TEST((bad_actual_fails<topic, entry_type, get_handle>));           \
    RUN_TEST((bad_avail_fails<topic, entry_type, get_handle>))

// Tests that should pass for any topic that expects a single entry in its
// in/out buffer.
#define RUN_SINGLE_ENTRY_TESTS(topic, entry_type, get_handle) \
    _RUN_COMMON_TESTS(topic, entry_type, get_handle);         \
    RUN_TEST((single_zero_buffer_fails<topic, entry_type, get_handle>))

// Tests that should pass for any topic that can handle multiple entries in its
// in/out buffer.
#define RUN_MULTI_ENTRY_TESTS(topic, entry_type, get_handle)          \
    _RUN_COMMON_TESTS(topic, entry_type, get_handle);                 \
    RUN_TEST((multi_zero_buffer_succeeds<topic, get_handle>));        \
    RUN_TEST((short_buffer_succeeds<topic, entry_type, get_handle>)); \
    RUN_TEST((partially_unmapped_buffer_fails<topic, entry_type, get_handle>))

BEGIN_TEST_CASE(object_info_tests)

// MX_INFO_HANDLE_VALID is an oddball that doesn't care about its buffer,
// so we can't use the normal topic test suites.
RUN_TEST(handle_valid_on_valid_handle_succeeds);
RUN_TEST(handle_valid_on_closed_handle_fails);
RUN_TEST((invalid_handle_fails<MX_INFO_HANDLE_VALID, void*>));

RUN_TEST(task_stats_smoke);
RUN_SINGLE_ENTRY_TESTS(MX_INFO_TASK_STATS, mx_info_task_stats_t, mx_process_self);
RUN_TEST((wrong_handle_type_fails<MX_INFO_TASK_STATS, mx_info_task_stats_t, get_test_job>));
RUN_TEST((wrong_handle_type_fails<MX_INFO_TASK_STATS, mx_info_task_stats_t, mx_thread_self>));

RUN_TEST(process_maps_smoke);
RUN_MULTI_ENTRY_TESTS(MX_INFO_PROCESS_MAPS, mx_info_maps_t, get_test_process);
RUN_TEST((self_fails<MX_INFO_PROCESS_MAPS, mx_info_maps_t>))
RUN_TEST((wrong_handle_type_fails<MX_INFO_PROCESS_MAPS, mx_info_maps_t, get_test_job>));
RUN_TEST((wrong_handle_type_fails<MX_INFO_PROCESS_MAPS, mx_info_maps_t, mx_thread_self>));
RUN_TEST((missing_rights_fails<MX_INFO_PROCESS_MAPS, mx_info_maps_t, get_test_process,
                               MX_RIGHT_READ>));

RUN_TEST(process_vmos_smoke);
RUN_MULTI_ENTRY_TESTS(MX_INFO_PROCESS_VMOS, mx_info_vmo_t, get_test_process);
RUN_TEST((self_fails<MX_INFO_PROCESS_VMOS, mx_info_vmo_t>))
RUN_TEST((wrong_handle_type_fails<MX_INFO_PROCESS_VMOS, mx_info_vmo_t, get_test_job>));
RUN_TEST((wrong_handle_type_fails<MX_INFO_PROCESS_VMOS, mx_info_vmo_t, mx_thread_self>));
RUN_TEST((missing_rights_fails<MX_INFO_PROCESS_VMOS, mx_info_vmo_t, get_test_process,
                               MX_RIGHT_READ>));

RUN_TEST(job_processes_smoke);
RUN_MULTI_ENTRY_TESTS(MX_INFO_JOB_PROCESSES, mx_koid_t, get_test_job);
RUN_TEST((wrong_handle_type_fails<MX_INFO_JOB_PROCESSES, mx_koid_t, get_test_process>));
RUN_TEST((wrong_handle_type_fails<MX_INFO_JOB_PROCESSES, mx_koid_t, mx_thread_self>));
RUN_TEST((missing_rights_fails<MX_INFO_JOB_PROCESSES, mx_koid_t, get_test_job,
                               MX_RIGHT_ENUMERATE>));

RUN_TEST(job_children_smoke);
RUN_MULTI_ENTRY_TESTS(MX_INFO_JOB_CHILDREN, mx_koid_t, get_test_job);
RUN_TEST((wrong_handle_type_fails<MX_INFO_JOB_CHILDREN, mx_koid_t, get_test_process>));
RUN_TEST((wrong_handle_type_fails<MX_INFO_JOB_CHILDREN, mx_koid_t, mx_thread_self>));
RUN_TEST((missing_rights_fails<MX_INFO_JOB_CHILDREN, mx_koid_t, get_test_job,
                               MX_RIGHT_ENUMERATE>));

// Basic tests for all other topics.

RUN_SINGLE_ENTRY_TESTS(MX_INFO_HANDLE_BASIC, mx_info_handle_basic_t, get_test_job);
RUN_SINGLE_ENTRY_TESTS(MX_INFO_HANDLE_BASIC, mx_info_handle_basic_t, get_test_process);
RUN_SINGLE_ENTRY_TESTS(MX_INFO_HANDLE_BASIC, mx_info_handle_basic_t, mx_thread_self);
RUN_SINGLE_ENTRY_TESTS(MX_INFO_HANDLE_BASIC, mx_info_handle_basic_t, mx_vmar_root_self);

RUN_SINGLE_ENTRY_TESTS(MX_INFO_PROCESS, mx_info_process_t, get_test_process);
RUN_TEST((wrong_handle_type_fails<MX_INFO_PROCESS, mx_info_process_t, get_test_job>));
RUN_TEST((wrong_handle_type_fails<MX_INFO_PROCESS, mx_info_process_t, mx_thread_self>));

RUN_SINGLE_ENTRY_TESTS(MX_INFO_VMAR, mx_info_vmar_t, mx_vmar_root_self);
RUN_TEST((wrong_handle_type_fails<MX_INFO_VMAR, mx_info_vmar_t, get_test_job>));
RUN_TEST((wrong_handle_type_fails<MX_INFO_VMAR, mx_info_vmar_t, get_test_process>));
RUN_TEST((wrong_handle_type_fails<MX_INFO_VMAR, mx_info_vmar_t, mx_thread_self>));

RUN_SINGLE_ENTRY_TESTS(MX_INFO_THREAD, mx_info_thread_t, mx_thread_self);
RUN_TEST((wrong_handle_type_fails<MX_INFO_THREAD, mx_info_thread_t, get_test_job>));
RUN_TEST((wrong_handle_type_fails<MX_INFO_THREAD, mx_info_thread_t, get_test_process>));

RUN_SINGLE_ENTRY_TESTS(MX_INFO_THREAD_STATS, mx_info_thread_stats_t, mx_thread_self);
RUN_TEST((wrong_handle_type_fails<MX_INFO_THREAD_STATS, mx_info_thread_t, get_test_job>));
RUN_TEST((wrong_handle_type_fails<MX_INFO_THREAD_STATS, mx_info_thread_t, get_test_process>));

// MX_INFO_PROCESS_THREADS tests.
// TODO(dbort): Use RUN_MULTI_ENTRY_TESTS instead. |short_buffer_succeeds| and
// |partially_unmapped_buffer_fails| currently fail because those tests expect
// avail > 1, but the test process only has one thread and it's not trivial to
// add more.
RUN_TEST((invalid_handle_fails<MX_INFO_PROCESS_THREADS, mx_koid_t>));
RUN_TEST((null_avail_actual_succeeds<MX_INFO_PROCESS_THREADS, mx_koid_t, get_test_process>));
RUN_TEST((bad_buffer_fails<MX_INFO_PROCESS_THREADS, mx_koid_t, get_test_process>));
RUN_TEST((bad_actual_fails<MX_INFO_PROCESS_THREADS, mx_koid_t, get_test_process>));
RUN_TEST((bad_avail_fails<MX_INFO_PROCESS_THREADS, mx_koid_t, get_test_process>))
RUN_TEST((multi_zero_buffer_succeeds<MX_INFO_PROCESS_THREADS, get_test_process>));

// Skip most tests for MX_INFO_THREAD_EXCEPTION_REPORT, which is tested
// elsewhere and requires the target thread to be in a certain state.
RUN_TEST((invalid_handle_fails<MX_INFO_THREAD_EXCEPTION_REPORT, mx_exception_report_t>));

// TODO(dbort): Test resource topics
// RUN_MULTI_ENTRY_TESTS(MX_INFO_RESOURCE_CHILDREN, mx_rrec_t, get_root_resource);
// RUN_MULTI_ENTRY_TESTS(MX_INFO_RESOURCE_RECORDS, mx_rrec_t, get_root_resource);
// RUN_MULTI_ENTRY_TESTS(MX_INFO_CPU_STATS, mx_info_cpu_stats_t, get_root_resource);
// RUN_SINGLE_ENTRY_TESTS(MX_INFO_KMEM_STATS, mx_info_kmem_stats_t, get_root_resource);

END_TEST_CASE(object_info_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
