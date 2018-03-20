// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/vmar_manager.h>
#include <fbl/vmo_mapper.h>
#include <unittest/unittest.h>
#include <zircon/rights.h>

namespace {

static constexpr size_t kSubVmarTestSize = 16 << 20;    // 16MB
static constexpr size_t kVmoTestSize = 512 << 10;       // 512KB

static volatile uint32_t g_access_check_var;
template <typename T>
using RefPtr = fbl::RefPtr<T>;
using VmarManager = fbl::VmarManager;
using VmoMapper = fbl::VmoMapper;

enum class AccessType { Rd, Wr };

bool probe_access(void* addr, AccessType access_type, bool expect_can_access) {
    BEGIN_TEST;

    switch (access_type) {
    case AccessType::Rd: {
        auto rd_fn = [](void* addr) { g_access_check_var = reinterpret_cast<uint32_t*>(addr)[0]; };
        if (expect_can_access) {
            ASSERT_NO_DEATH(rd_fn, addr, "Read probe failed when it should have succeeded.");
        } else {
            ASSERT_DEATH(rd_fn, addr, "Read probe succeeded when it should have failed.");
        }
    }
    break;

    case AccessType::Wr: {
        auto wr_fn = [](void* addr) { reinterpret_cast<uint32_t*>(addr)[0] = g_access_check_var; };
        if (expect_can_access) {
            ASSERT_NO_DEATH(wr_fn, addr, "Write probe failed when it should have succeeded.");
        } else {
            ASSERT_DEATH(wr_fn, addr, "Write probe succeeded when it should have failed.");
        }
    }
    break;
    }

    END_TEST;
}

template <typename T, typename U>
bool contained_in(const T& contained, const U& container) {
    uintptr_t contained_start = reinterpret_cast<uintptr_t>(contained.start());
    uintptr_t contained_end = contained_start + contained.size();
    uintptr_t container_start = reinterpret_cast<uintptr_t>(container.start());
    uintptr_t container_end = container_start + container.size();

    return (contained_start <= contained_end) &&
           (contained_start >= container_start) &&
           (contained_end <= container_end);
}

bool probe_verify_region(void* start, size_t size, uint32_t access) {
    BEGIN_TEST;

    auto uint_base = reinterpret_cast<uintptr_t>(start);
    void* probe_points[] = {
        reinterpret_cast<void*>(uint_base),
        reinterpret_cast<void*>(uint_base + (size / 2)),
        reinterpret_cast<void*>(uint_base + size - sizeof(uint32_t)),
    };

    for (void* probe_point : probe_points) {
        ASSERT_TRUE(probe_access(probe_point, AccessType::Rd, access & ZX_VM_FLAG_PERM_READ));
        ASSERT_TRUE(probe_access(probe_point, AccessType::Wr, access & ZX_VM_FLAG_PERM_WRITE));
    }

    END_TEST;
}

bool vmar_vmo_core_test(uint32_t vmar_levels, bool test_create) {
    BEGIN_TEST;

    RefPtr<VmarManager> managers[2];
    RefPtr<VmarManager> target_vmar;

    ASSERT_LE(vmar_levels, countof(managers));
    size_t vmar_size = kSubVmarTestSize;
    for (uint32_t i = 0; i < vmar_levels; ++i) {
        managers[i] = VmarManager::Create(vmar_size, i ? managers[i - 1] : nullptr);
        ASSERT_NONNULL(managers[i], "Failed to create VMAR manager");

        if (i) {
            ASSERT_TRUE(contained_in(*managers[i], *managers[i - 1]),
                        "Sub-VMO is not contained within in its parent!");
        }

        vmar_size >>= 1u;
    }

    if (vmar_levels) {
        target_vmar = managers[vmar_levels - 1];
    }

    struct {
        uint32_t access_flags;
        zx_rights_t vmo_rights;
        size_t test_offset;
        size_t test_size;
        void* start;
    } kVmoTests[] = {
        { .access_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
          .vmo_rights = ZX_RIGHT_SAME_RIGHTS,
          .test_offset = 0,
          .test_size = kVmoTestSize >> 1,
          .start = nullptr,
        },
        { .access_flags = ZX_VM_FLAG_PERM_READ,
          .vmo_rights = ZX_RIGHT_READ | ZX_RIGHT_MAP,
          .test_offset = 0,
          .test_size = kVmoTestSize,
          .start = nullptr,
        },
        // TODO(johngro): We are not allowed to map pages as write-only.  Need
        // to determine if this is WAI or not.
#if 0
        { .access_flags = ZX_VM_FLAG_PERM_WRITE,
          .vmo_rights = ZX_RIGHT_WRITE | ZX_RIGHT_MAP,
          .test_offset = 0,
          .test_size = 0,
          .start = nullptr,
        },
#endif
        { .access_flags = 0,
          .vmo_rights = 0,
          .test_offset = 0,
          .test_size = 0,
          .start = nullptr,
        },
        { .access_flags = 0,
          .vmo_rights = 0,
          .test_offset = kVmoTestSize >> 1,
          .test_size = 0,
          .start = nullptr,
        },
    };

    for (uint32_t pass = 0; pass < 2; ++pass) {
        {
            VmoMapper mappers[countof(kVmoTests)];
            zx::vmo vmo_handles[countof(kVmoTests)];
            zx_status_t res;

            for (size_t i = 0; i < countof(kVmoTests); ++i) {
                auto& t = kVmoTests[i];

                for (uint32_t create_map_pass = 0; create_map_pass < 2; ++create_map_pass) {
                    // If this is the first create/map pass, the create/map operation should
                    // succeed.  If this is the second pass, it should fail with BAD_STATE (since we
                    // should have already created/mapped already)
                    zx_status_t expected_cm_res = create_map_pass ? ZX_ERR_BAD_STATE : ZX_OK;

                    if (test_create) {
                        // If we are testing CreateAndMap, call it with the mapping
                        // rights and the proper rights reduction for the VMO it hands
                        // back to us.  Hold onto the returned handle in vmo_handles.
                        res = mappers[i].CreateAndMap(kVmoTestSize,
                                                      t.access_flags,
                                                      target_vmar,
                                                      &vmo_handles[i],
                                                      t.vmo_rights);
                        t.test_size = kVmoTestSize;

                        ASSERT_EQ(res, expected_cm_res);
                        ASSERT_TRUE(vmo_handles[i].is_valid());
                    } else {
                        // If we are testing Map, and this is the first pass, create the VMOs we
                        // will pass to map, then do so.
                        if (create_map_pass == 0) {
                            res = zx::vmo::create(kVmoTestSize, 0, &vmo_handles[i]);
                            ASSERT_EQ(res, ZX_OK);
                            ASSERT_TRUE(vmo_handles[i].is_valid());
                        }

                        res = mappers[i].Map(vmo_handles[i],
                                             t.test_offset,
                                             t.test_size,
                                             t.access_flags,
                                             target_vmar);
                        ASSERT_EQ(res, expected_cm_res);

                        // If this was the first VMO we have mapped during this test
                        // run, and we requested only a partial map, and it was mapped
                        // in a sub-vmar, and the end of the VMO is not aligned with the
                        // end of the VMAR, then check to make sure that we read or
                        // write past the end of the partial mapping.
                        //
                        // TODO(johngro): It would be nice to always do these checks,
                        // but we do not have a lot of control of whether or not
                        // something else may have been mapped adjacent to our mapping,
                        // hence all of the restrictions described above.
                        if (!i && !create_map_pass && target_vmar &&
                            t.test_size && (t.test_size < kVmoTestSize)) {
                            uintptr_t vmo_end = reinterpret_cast<uintptr_t>(mappers[i].start());
                            uintptr_t vmar_end = reinterpret_cast<uintptr_t>(target_vmar->start());

                            vmo_end += mappers[i].size();
                            vmar_end += target_vmar->size();
                            if (vmo_end < vmar_end) {
                                void* probe_tgt = reinterpret_cast<void*>(vmo_end);
                                ASSERT_TRUE(probe_access(probe_tgt, AccessType::Rd, false));
                                ASSERT_TRUE(probe_access(probe_tgt, AccessType::Wr, false));
                            }
                        }
                    }
                }

                // Stash the address of the mapped VMOs in the test state
                t.start = mappers[i].start();

                // If we mapped inside of a sub-vmar, then the mapping should be contained within
                // the VMAR.
                if (target_vmar != nullptr) {
                    ASSERT_TRUE(contained_in(mappers[i], *target_vmar));
                }

                if (test_create) {
                    // If we created this VMO, make sure that its rights were reduced correctly.
                    zx_rights_t expected_rights = t.vmo_rights != ZX_RIGHT_SAME_RIGHTS
                                                ? t.vmo_rights
                                                : ZX_DEFAULT_VMO_RIGHTS;
                    zx_info_handle_basic_t info;
                    res = vmo_handles[i].get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                                                  nullptr, nullptr);

                    ASSERT_EQ(res, ZX_OK, "Failed to get basic object info");
                    ASSERT_EQ(info.rights, expected_rights, "Rights reduction failure");
                } else {
                    // If we mapped this VMO, and we passed zero for the map size, the Mapper should
                    // have mapped the entire VMO after the offset and its size should reflect that.
                    if (!t.test_size) {
                        ASSERT_EQ(mappers[i].size() + t.test_offset, kVmoTestSize);
                        t.test_size = kVmoTestSize - t.test_offset;
                    }
                }
            }

            // Now that everything has been created and mapped, make sure that
            // everything checks out by probing and looking for seg-faults
            // if/when we violate permissions.
            for (const auto& t : kVmoTests) {
                ASSERT_TRUE(probe_verify_region(t.start, t.test_size, t.access_flags));
            }

            // Release all of our VMO handles, then verify again.  Releasing
            // these handles should not cause our mapping to go away.
            for (auto& h : vmo_handles) {
                h.reset();
            }

            for (const auto& t : kVmoTests) {
                ASSERT_TRUE(probe_verify_region(t.start, t.test_size, t.access_flags));
            }

            // If this is the first pass, manually unmap all of the VmoMappers
            // and verify that we can no longer access any of the previously
            // mapped region.
            if (!pass) {
                for (auto& m : mappers) {
                    m.Unmap();
                }

                for (const auto& t : kVmoTests) {
                    ASSERT_TRUE(probe_verify_region(t.start, t.test_size, 0));
                }
            }
        }

        // If this is the second pass, then we didn't manually call unmap, we
        // just let the mappers go out of scope.  Make sure that everything
        // auto-unmapped as it should.
        if (pass) {
            for (const auto& t : kVmoTests) {
                ASSERT_TRUE(probe_verify_region(t.start, t.test_size, 0));
            }
        }
    }

    // TODO(johngro) : release all of our VMAR references and then make certain
    // that they were destroyed as they should have been.  Right now this is
    // rather difficult as we cannot fetch mapping/vmar info for our current
    // process, so we are skipping the check.

    END_TEST;
}

bool vmo_create_and_map_root_test() {
    BEGIN_TEST;
    ASSERT_TRUE(vmar_vmo_core_test(0, true));
    END_TEST;
}

bool vmo_create_and_map_sub_vmar_test() {
    BEGIN_TEST;
    ASSERT_TRUE(vmar_vmo_core_test(1, true));
    END_TEST;
}

bool vmo_create_and_map_sub_sub_vmar_test() {
    BEGIN_TEST;
    ASSERT_TRUE(vmar_vmo_core_test(2, true));
    END_TEST;
}

bool vmo_map_root_test() {
    BEGIN_TEST;
    ASSERT_TRUE(vmar_vmo_core_test(0, false));
    END_TEST;
}

bool vmo_map_sub_vmar_test() {
    BEGIN_TEST;
    ASSERT_TRUE(vmar_vmo_core_test(1, false));
    END_TEST;
}

bool vmo_map_sub_sub_vmar_test() {
    BEGIN_TEST;
    ASSERT_TRUE(vmar_vmo_core_test(2, false));
    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(vmo_mapper_vmar_manager_tests)
RUN_NAMED_TEST("vmo_create_and_map_root", vmo_create_and_map_root_test)
RUN_NAMED_TEST("vmo_create_and_map_sub_vmar", vmo_create_and_map_sub_vmar_test)
RUN_NAMED_TEST("vmo_create_and_map_sub_sub_vmar", vmo_create_and_map_sub_sub_vmar_test)
RUN_NAMED_TEST("vmo_map_root", vmo_map_root_test)
RUN_NAMED_TEST("vmo_map_sub_vmar", vmo_map_sub_vmar_test)
RUN_NAMED_TEST("vmo_map_sub_sub_vmar", vmo_map_sub_sub_vmar_test)
END_TEST_CASE(vmo_mapper_vmar_manager_tests)
