// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <err.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_address_region.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_object_paged.h>
#include <kernel/vm/vm_object_physical.h>
#include <mxalloc/new.h>
#include <mxtl/array.h>
#include <unittest.h>

static const uint kArchRwFlags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

// Allocates a single page, translates it to a vm_page_t and frees it.
static bool pmm_smoke_test(void* context) {
    BEGIN_TEST;
    paddr_t pa;

    vm_page_t* page = pmm_alloc_page(0, &pa);
    EXPECT_NEQ(nullptr, page, "pmm_alloc single page");
    EXPECT_NEQ(0u, pa, "pmm_alloc single page");

    vm_page_t* page2 = paddr_to_vm_page(pa);
    EXPECT_EQ(page2, page, "paddr_to_vm_page on single page");

    auto ret = pmm_free_page(page);
    EXPECT_EQ(1u, ret, "pmm_free_page on single page");
    END_TEST;
}

// Allocates a bunch of pages then frees them.
static bool pmm_large_alloc_test(void* context) {
    BEGIN_TEST;
    list_node list = LIST_INITIAL_VALUE(list);

    static const size_t alloc_count = 1024;

    auto count = pmm_alloc_pages(alloc_count, 0, &list);
    EXPECT_EQ(alloc_count, count, "pmm_alloc_pages a bunch of pages count");
    EXPECT_EQ(alloc_count, list_length(&list),
              "pmm_alloc_pages a bunch of pages list count");

    auto ret = pmm_free(&list);
    EXPECT_EQ(alloc_count, ret, "pmm_free_page on a list of pages");
    END_TEST;
}

// Allocates too many pages and makes sure it fails nicely.
static bool pmm_oversized_alloc_test(void* context) {
    BEGIN_TEST;
    list_node list = LIST_INITIAL_VALUE(list);

    static const size_t alloc_count =
        (128 * 1024 * 1024 * 1024ULL) / PAGE_SIZE; // 128GB

    auto count = pmm_alloc_pages(alloc_count, 0, &list);
    EXPECT_NEQ(alloc_count, 0, "pmm_alloc_pages too many pages count > 0");
    EXPECT_NEQ(alloc_count, count, "pmm_alloc_pages too many pages count");
    EXPECT_EQ(count, list_length(&list),
              "pmm_alloc_pages too many pages list count");

    auto ret = pmm_free(&list);
    EXPECT_EQ(count, ret, "pmm_free_page on a list of pages");
    END_TEST;
}

static uint32_t test_rand(uint32_t seed) {
    return (seed = seed * 1664525 + 1013904223);
}

// fill a region of memory with a pattern based on the address of the region
static void fill_region(uintptr_t seed, void* _ptr, size_t len) {
    uint32_t* ptr = (uint32_t*)_ptr;

    ASSERT(IS_ALIGNED((uintptr_t)ptr, 4));

    uint32_t val = (uint32_t)seed;
#if UINTPTR_MAX > UINT32_MAX
    val ^= (uint32_t)(seed >> 32);
#endif
    for (size_t i = 0; i < len / 4; i++) {
        ptr[i] = val;

        val = test_rand(val);
    }
}

// test a region of memory against a known pattern
static bool test_region(uintptr_t seed, void* _ptr, size_t len) {
    uint32_t* ptr = (uint32_t*)_ptr;

    ASSERT(IS_ALIGNED((uintptr_t)ptr, 4));

    uint32_t val = (uint32_t)seed;
#if UINTPTR_MAX > UINT32_MAX
    val ^= (uint32_t)(seed >> 32);
#endif
    for (size_t i = 0; i < len / 4; i++) {
        if (ptr[i] != val) {
            unittest_printf("value at %p (%zu) is incorrect: 0x%x vs 0x%x\n", &ptr[i], i, ptr[i],
                            val);
            return false;
        }

        val = test_rand(val);
    }

    return true;
}

static bool fill_and_test(void* ptr, size_t len) {
    BEGIN_TEST;

    // fill it with a pattern
    fill_region((uintptr_t)ptr, ptr, len);

    // test that the pattern is read back properly
    auto result = test_region((uintptr_t)ptr, ptr, len);
    EXPECT_TRUE(result, "testing region for corruption");

    END_TEST;
}

// Allocates a region in kernel space, reads/writes it, then destroys it.
static bool vmm_alloc_smoke_test(void* context) {
    BEGIN_TEST;
    static const size_t alloc_size = 256 * 1024;

    // allocate a region of memory
    void* ptr;
    auto kaspace = VmAspace::kernel_aspace();
    auto err = kaspace->Alloc(
            "test", alloc_size, &ptr, 0, 0, kArchRwFlags);
    EXPECT_EQ(0, err, "VmAspace::Alloc region of memory");
    EXPECT_NEQ(nullptr, ptr, "VmAspace::Alloc region of memory");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    // free the region
    err = kaspace->FreeRegion(reinterpret_cast<vaddr_t>(ptr));
    EXPECT_EQ(0, err, "VmAspace::FreeRegion region of memory");
    END_TEST;
}

// Allocates a contiguous region in kernel space, reads/writes it,
// then destroys it.
static bool vmm_alloc_contiguous_smoke_test(void* context) {
    BEGIN_TEST;
    static const size_t alloc_size = 256 * 1024;

    // allocate a region of memory
    void* ptr;
    auto kaspace = VmAspace::kernel_aspace();
    auto err = kaspace->AllocContiguous("test",
                                        alloc_size, &ptr, 0,
                                        VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(0, err, "VmAspace::AllocContiguous region of memory");
    EXPECT_NEQ(nullptr, ptr, "VmAspace::AllocContiguous region of memory");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    // test that it is indeed contiguous
    unittest_printf("testing that region is contiguous\n");
    paddr_t last_pa = 0;
    for (size_t i = 0; i < alloc_size / PAGE_SIZE; i++) {
        paddr_t pa = vaddr_to_paddr((uint8_t*)ptr + i * PAGE_SIZE);
        if (last_pa != 0) {
            EXPECT_EQ(pa, last_pa + PAGE_SIZE, "region is contiguous");
        }

        last_pa = pa;
    }

    // free the region
    err = kaspace->FreeRegion(reinterpret_cast<vaddr_t>(ptr));
    EXPECT_EQ(0, err, "VmAspace::FreeRegion region of memory");
    END_TEST;
}

// Allocates a new address space and creates a few regions in it,
// then destroys it.
static bool multiple_regions_test(void* context) {
    BEGIN_TEST;
    void* ptr;
    static const size_t alloc_size = 16 * 1024;

    mxtl::RefPtr<VmAspace> aspace = VmAspace::Create(0, "test aspace");
    EXPECT_NEQ(nullptr, aspace, "VmAspace::Create pointer");

    vmm_aspace_t* old_aspace = get_current_thread()->aspace;
    vmm_set_active_aspace(reinterpret_cast<vmm_aspace_t*>(aspace.get()));

    // allocate region 0
    status_t err = aspace->Alloc("test0", alloc_size, &ptr, 0, 0, kArchRwFlags);
    EXPECT_EQ(0, err, "VmAspace::Alloc region of memory");
    EXPECT_NEQ(nullptr, ptr, "VmAspace::Alloc region of memory");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    // allocate region 1
    err = aspace->Alloc("test1", 16384, &ptr, 0, 0, kArchRwFlags);
    EXPECT_EQ(0, err, "VmAspace::Alloc region of memory");
    EXPECT_NEQ(nullptr, ptr, "VmAspace::Alloc region of memory");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    // allocate region 2
    err = aspace->Alloc("test2", 16384, &ptr, 0, 0, kArchRwFlags);
    EXPECT_EQ(0, err, "VmAspace::Alloc region of memory");
    EXPECT_NEQ(nullptr, ptr, "VmAspace::Alloc region of memory");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    vmm_set_active_aspace(old_aspace);

    // free the address space all at once
    err = aspace->Destroy();
    EXPECT_EQ(0, err, "VmAspace::Destroy");
    END_TEST;
}

static bool vmm_alloc_zero_size_fails(void* context) {
    BEGIN_TEST;
    const size_t zero_size = 0;
    void* ptr;
    status_t err = VmAspace::kernel_aspace()->Alloc(
            "test", zero_size, &ptr, 0, 0, kArchRwFlags);
    EXPECT_EQ(ERR_INVALID_ARGS, err, "");
    END_TEST;
}

static bool vmm_alloc_bad_specific_pointer_fails(void* context) {
    BEGIN_TEST;
    // bad specific pointer
    void* ptr = (void*)1;
    status_t err = VmAspace::kernel_aspace()->Alloc(
            "test", 16384, &ptr, 0,
            VMM_FLAG_VALLOC_SPECIFIC | VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(ERR_INVALID_ARGS, err, "");
    END_TEST;
}

static bool vmm_alloc_contiguous_missing_flag_commit_fails(void* context) {
    BEGIN_TEST;
    // should have VMM_FLAG_COMMIT
    const uint zero_vmm_flags = 0;
    void* ptr;
    status_t err = VmAspace::kernel_aspace()->AllocContiguous(
            "test", 4096, &ptr, 0, zero_vmm_flags, kArchRwFlags);
    EXPECT_EQ(ERR_INVALID_ARGS, err, "");
    END_TEST;
}

static bool vmm_alloc_contiguous_zero_size_fails(void* context) {
    BEGIN_TEST;
    const size_t zero_size = 0;
    void* ptr;
    status_t err = VmAspace::kernel_aspace()->AllocContiguous(
            "test", zero_size, &ptr, 0, VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(ERR_INVALID_ARGS, err, "");
    END_TEST;
}

// Allocates a vm address space object directly, allows it to go out of scope.
static bool vmaspace_create_smoke_test(void* context) {
    BEGIN_TEST;
    auto aspace = VmAspace::Create(0, "test aspace");
    aspace->Destroy();
    END_TEST;
}

// Allocates a vm address space object directly, maps something on it,
// allows it to go out of scope.
static bool vmaspace_alloc_smoke_test(void* context) {
    BEGIN_TEST;
    auto aspace = VmAspace::Create(0, "test aspace2");

    void* ptr;
    auto err = aspace->Alloc("test", PAGE_SIZE, &ptr, 0, 0, kArchRwFlags);
    EXPECT_EQ(NO_ERROR, err, "allocating region\n");

    // destroy the aspace, which should drop all the internal refs to it
    aspace->Destroy();

    // drop the ref held by this pointer
    aspace.reset();
    END_TEST;
}

// Doesn't do anything, just prints all aspaces.
// Should be run after all other tests so that people can manually comb
// through the output for leaked test aspaces.
static bool dump_all_aspaces(void* context) {
    BEGIN_TEST;
    unittest_printf("verify there are no test aspaces left around\n");
    DumpAllAspaces(/*verbose*/ true);
    END_TEST;
}

// Creates a vm object.
static bool vmo_create_test(void* context) {
    BEGIN_TEST;
    auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, PAGE_SIZE);
    EXPECT_TRUE(vmo, "");
    END_TEST;
}

// Creates a vm object, commits memory.
static bool vmo_commit_test(void* context) {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
    REQUIRE_NONNULL(vmo, "vmobject creation\n");

    uint64_t committed;
    auto ret = vmo->CommitRange(0, alloc_size, &committed);
    EXPECT_EQ(0, ret, "committing vm object\n");
    EXPECT_EQ(ROUNDUP_PAGE_SIZE(alloc_size), committed,
              "committing vm object\n");
    END_TEST;
}

// Creates a vm object, commits odd sized memory.
static bool vmo_odd_size_commit_test(void* context) {
    BEGIN_TEST;
    static const size_t alloc_size = 15;
    auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
    REQUIRE_NONNULL(vmo, "vmobject creation\n");

    uint64_t committed;
    auto ret = vmo->CommitRange(0, alloc_size, &committed);
    EXPECT_EQ(0, ret, "committing vm object\n");
    EXPECT_EQ(ROUNDUP_PAGE_SIZE(alloc_size), committed,
              "committing vm object\n");
    END_TEST;
}

// Creates a vm object, commits contiguous memory.
static bool vmo_contiguous_commit_test(void* context) {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
    REQUIRE_NONNULL(vmo, "vmobject creation\n");

    uint64_t committed;
    auto ret = vmo->CommitRangeContiguous(0, alloc_size, &committed, 0);
    EXPECT_EQ(0, ret, "committing vm object contig\n");
    EXPECT_EQ(ROUNDUP_PAGE_SIZE(alloc_size), committed,
              "committing vm object contig\n");
    END_TEST;
}

// Creats a vm object, maps it, precommitted.
static bool vmo_precommitted_map_test(void* context) {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
    REQUIRE_NONNULL(vmo, "vmobject creation\n");

    auto ka = VmAspace::kernel_aspace();
    void* ptr;
    auto ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr,
                             0, VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(NO_ERROR, ret, "mapping object");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    auto err = ka->FreeRegion((vaddr_t)ptr);
    EXPECT_EQ(NO_ERROR, err, "unmapping object");
    END_TEST;
}

// Creates a vm object, maps it, demand paged.
static bool vmo_demand_paged_map_test(void* context) {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
    REQUIRE_NONNULL(vmo, "vmobject creation\n");

    auto ka = VmAspace::kernel_aspace();
    void* ptr;
    auto ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr,
                             0, 0, kArchRwFlags);
    EXPECT_EQ(ret, NO_ERROR, "mapping object");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    auto err = ka->FreeRegion((vaddr_t)ptr);
    EXPECT_EQ(NO_ERROR, err, "unmapping object");
    END_TEST;
}

// Creates a vm object, maps it, drops ref before unmapping.
static bool vmo_dropped_ref_test(void* context) {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
    REQUIRE_NONNULL(vmo, "vmobject creation\n");

    auto ka = VmAspace::kernel_aspace();
    void* ptr;
    auto ret = ka->MapObjectInternal(mxtl::move(vmo), "test", 0, alloc_size, &ptr,
                             0, VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(ret, NO_ERROR, "mapping object");

    EXPECT_NULL(vmo, "dropped ref to object");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    auto err = ka->FreeRegion((vaddr_t)ptr);
    EXPECT_EQ(NO_ERROR, err, "unmapping object");
    END_TEST;
}

// Creates a vm object, maps it, fills it with data, unmaps,
// maps again somewhere else.
static bool vmo_remap_test(void* context) {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
    REQUIRE_NONNULL(vmo, "vmobject creation\n");

    auto ka = VmAspace::kernel_aspace();
    void* ptr;
    auto ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr,
                             0, VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(NO_ERROR, ret, "mapping object");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    auto err = ka->FreeRegion((vaddr_t)ptr);
    EXPECT_EQ(NO_ERROR, err, "unmapping object");

    // map it again
    ret = ka->MapObjectInternal(vmo, "test", 0, alloc_size, &ptr,
                        0, VMM_FLAG_COMMIT, kArchRwFlags);
    EXPECT_EQ(ret, NO_ERROR, "mapping object");

    // test that the pattern is still valid
    bool result = test_region((uintptr_t)ptr, ptr, alloc_size);
    EXPECT_TRUE(result, "testing region for corruption");

    err = ka->FreeRegion((vaddr_t)ptr);
    EXPECT_EQ(NO_ERROR, err, "unmapping object");
    END_TEST;
}

// Creates a vm object, maps it, fills it with data, maps it a second time and
// third time somwehere else.
static bool vmo_double_remap_test(void* context) {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;
    auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
    REQUIRE_NONNULL(vmo, "vmobject creation\n");

    auto ka = VmAspace::kernel_aspace();
    void* ptr;
    auto ret = ka->MapObjectInternal(vmo, "test0", 0, alloc_size, &ptr,
                             0, 0, kArchRwFlags);
    EXPECT_EQ(NO_ERROR, ret, "mapping object");

    // fill with known pattern and test
    if (!fill_and_test(ptr, alloc_size))
        all_ok = false;

    // map it again
    void* ptr2;
    ret = ka->MapObjectInternal(vmo, "test1", 0, alloc_size, &ptr2,
                        0, 0, kArchRwFlags);
    EXPECT_EQ(ret, NO_ERROR, "mapping object second time");
    EXPECT_NEQ(ptr, ptr2, "second mapping is different");

    // test that the pattern is still valid
    bool result = test_region((uintptr_t)ptr, ptr2, alloc_size);
    EXPECT_TRUE(result, "testing region for corruption");

    // map it a third time with an offset
    void* ptr3;
    static const size_t alloc_offset = PAGE_SIZE;
    ret = ka->MapObjectInternal(vmo, "test2", alloc_offset, alloc_size - alloc_offset,
                        &ptr3, 0, 0, kArchRwFlags);
    EXPECT_EQ(ret, NO_ERROR, "mapping object third time");
    EXPECT_NEQ(ptr3, ptr2, "third mapping is different");
    EXPECT_NEQ(ptr3, ptr, "third mapping is different");

    // test that the pattern is still valid
    int mc =
        memcmp((uint8_t*)ptr + alloc_offset, ptr3, alloc_size - alloc_offset);
    EXPECT_EQ(0, mc, "testing region for corruption");

    ret = ka->FreeRegion((vaddr_t)ptr3);
    EXPECT_EQ(NO_ERROR, ret, "unmapping object third time");

    ret = ka->FreeRegion((vaddr_t)ptr2);
    EXPECT_EQ(NO_ERROR, ret, "unmapping object second time");

    ret = ka->FreeRegion((vaddr_t)ptr);
    EXPECT_EQ(NO_ERROR, ret, "unmapping object");
    END_TEST;
}

static bool vmo_read_write_smoke_test(void* context) {
    BEGIN_TEST;
    static const size_t alloc_size = PAGE_SIZE * 16;

    // create object
    auto vmo = VmObjectPaged::Create(0, alloc_size);
    REQUIRE_NONNULL(vmo, "vmobject creation\n");

    // create test buffer
    AllocChecker ac;
    mxtl::Array<uint8_t> a(new (&ac) uint8_t[alloc_size], alloc_size);
    EXPECT_TRUE(ac.check(), "");
    fill_region(99, a.get(), alloc_size);

    // write to it, make sure it seems to work with valid args
    size_t bytes_written = -1;
    status_t err = vmo->Write(a.get(), 0, 0, &bytes_written);
    EXPECT_EQ(NO_ERROR, err, "writing to object");
    EXPECT_EQ(0u, bytes_written, "writing to object");

    bytes_written = -1;
    err = vmo->Write(a.get(), 0, 37, &bytes_written);
    EXPECT_EQ(NO_ERROR, err, "writing to object");
    EXPECT_EQ(37u, bytes_written, "writing to object");

    bytes_written = -1;
    err = vmo->Write(a.get(), 99, 37, &bytes_written);
    EXPECT_EQ(NO_ERROR, err, "writing to object");
    EXPECT_EQ(37u, bytes_written, "writing to object");

    // should trim the returned size
    bytes_written = -1;
    err = vmo->Write(a.get(), 0, alloc_size + 47, &bytes_written);
    EXPECT_EQ(NO_ERROR, err, "writing to object");
    EXPECT_EQ(alloc_size - 0, bytes_written, "writing to object");

    bytes_written = -1;
    err = vmo->Write(a.get(), 31, alloc_size + 47, &bytes_written);
    EXPECT_EQ(NO_ERROR, err, "writing to object");
    EXPECT_EQ(alloc_size - 31, bytes_written, "writing to object");

    // should return an error because out of range
    bytes_written = -1;
    err = vmo->Write(a.get(), alloc_size + 99, 42, &bytes_written);
    EXPECT_EQ(ERR_OUT_OF_RANGE, err, "writing to object");
    EXPECT_EQ(0u, bytes_written, "writing to object");

    // map the object
    auto ka = VmAspace::kernel_aspace();
    uint8_t* ptr;
    err = ka->MapObjectInternal(vmo, "test", 0, alloc_size, (void**)&ptr,
                        0, 0, kArchRwFlags);
    EXPECT_EQ(NO_ERROR, err, "mapping object");

    // write to it at odd offsets
    bytes_written = -1;
    err = vmo->Write(a.get(), 31, 4197, &bytes_written);
    EXPECT_EQ(NO_ERROR, err, "writing to object");
    EXPECT_EQ(4197u, bytes_written, "writing to object");
    int cmpres = memcmp(ptr + 31, a.get(), 4197);
    EXPECT_EQ(0, cmpres, "reading from object");

    // write to it, filling the object completely
    err = vmo->Write(a.get(), 0, alloc_size, &bytes_written);
    EXPECT_EQ(NO_ERROR, err, "writing to object");
    EXPECT_EQ(alloc_size, bytes_written, "writing to object");

    // test that the data was actually written to it
    bool result = test_region(99, ptr, alloc_size);
    EXPECT_TRUE(result, "writing to object");

    // unmap it
    ka->FreeRegion((vaddr_t)ptr);

    // test that we can read from it
    mxtl::Array<uint8_t> b(new (&ac) uint8_t[alloc_size], alloc_size);
    EXPECT_TRUE(ac.check(), "can't allocate buffer");

    size_t bytes_read = -1;
    err = vmo->Read(b.get(), 0, alloc_size, &bytes_read);
    EXPECT_EQ(NO_ERROR, err, "reading from object");
    EXPECT_EQ(alloc_size, bytes_read, "reading from object");

    // validate the buffer is valid
    cmpres = memcmp(b.get(), a.get(), alloc_size);
    EXPECT_EQ(0, cmpres, "reading from object");

    // read from it at an offset
    bytes_read = -1;
    err = vmo->Read(b.get(), 31, 4197, &bytes_read);
    EXPECT_EQ(NO_ERROR, err, "reading from object");
    EXPECT_EQ(4197u, bytes_read, "reading from object");
    cmpres = memcmp(b.get(), a.get() + 31, 4197);
    EXPECT_EQ(0, cmpres, "reading from object");
    END_TEST;
}

bool vmo_cache_test(void* context) {
    BEGIN_TEST;

    paddr_t pa;
    vm_page_t* vm_page = pmm_alloc_page(0, &pa);
    auto ka = VmAspace::kernel_aspace();
    uint32_t cache_policy = ARCH_MMU_FLAG_UNCACHED_DEVICE;
    uint32_t cache_policy_get;
    void* ptr;

    EXPECT_TRUE(vm_page, "");
    // Test that the flags set/get properly
    {
        auto vmo = VmObjectPhysical::Create(pa, PAGE_SIZE);
        EXPECT_TRUE(vmo, "");
        EXPECT_EQ(NO_ERROR, vmo->GetMappingCachePolicy(&cache_policy_get), "try get");
        EXPECT_NEQ(cache_policy, cache_policy_get, "check initial cache policy");
        EXPECT_EQ(NO_ERROR, vmo->SetMappingCachePolicy(cache_policy), "try set");
        EXPECT_EQ(NO_ERROR, vmo->GetMappingCachePolicy(&cache_policy_get), "try get");
        EXPECT_EQ(cache_policy, cache_policy_get, "compare flags");
    }

    // Test valid flags
    for (uint32_t i = 0; i <= ARCH_MMU_FLAG_CACHE_MASK; i++) {
        auto vmo = VmObjectPhysical::Create(pa, PAGE_SIZE);
        EXPECT_TRUE(vmo, "");
        EXPECT_EQ(NO_ERROR, vmo->SetMappingCachePolicy(cache_policy), "try setting valid flags");
    }

    // Test invalid flags
    for (uint32_t i = ARCH_MMU_FLAG_CACHE_MASK + 1; i < 32; i++) {
        auto vmo = VmObjectPhysical::Create(pa, PAGE_SIZE);
        EXPECT_TRUE(vmo, "");
        EXPECT_EQ(ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(i), "try set with invalid flags");
    }

    // Test valid flags with invalid flags
    {
        auto vmo = VmObjectPhysical::Create(pa, PAGE_SIZE);
        EXPECT_EQ(ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0x5), "bad 0x5");
        EXPECT_EQ(ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0xA), "bad 0xA");
        EXPECT_EQ(ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0x55), "bad 0x55");
        EXPECT_EQ(ERR_INVALID_ARGS, vmo->SetMappingCachePolicy(cache_policy | 0xAA), "bad 0xAA");
    }

    // Test that changing policy while mapped is blocked
    {
        auto vmo = VmObjectPhysical::Create(pa, PAGE_SIZE);
        EXPECT_TRUE(vmo, "");
        EXPECT_EQ(NO_ERROR, ka->MapObjectInternal(vmo, "test", 0, PAGE_SIZE, (void**)&ptr, 0, 0,
                  kArchRwFlags), "map vmo");
        EXPECT_EQ(ERR_BAD_STATE, vmo->SetMappingCachePolicy(cache_policy),
                  "set flags while mapped");
        EXPECT_EQ(NO_ERROR, ka->FreeRegion((vaddr_t)ptr), "unmap vmo");
        EXPECT_EQ(NO_ERROR, vmo->SetMappingCachePolicy(cache_policy), "set flags after unmapping");
        EXPECT_EQ(NO_ERROR, ka->MapObjectInternal(vmo, "test", 0, PAGE_SIZE, (void**)&ptr, 0, 0,
                  kArchRwFlags), "map vmo again");
        EXPECT_EQ(NO_ERROR, ka->FreeRegion((vaddr_t)ptr), "unmap vmo");
    }

    pmm_free_page(vm_page);
    END_TEST;
}

// Use the function name as the test name
#define VM_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(vm_tests)
VM_UNITTEST(pmm_smoke_test)
VM_UNITTEST(pmm_large_alloc_test)
VM_UNITTEST(pmm_oversized_alloc_test)
VM_UNITTEST(vmm_alloc_smoke_test)
VM_UNITTEST(vmm_alloc_contiguous_smoke_test)
VM_UNITTEST(multiple_regions_test)
VM_UNITTEST(vmm_alloc_zero_size_fails)
VM_UNITTEST(vmm_alloc_bad_specific_pointer_fails)
VM_UNITTEST(vmm_alloc_contiguous_missing_flag_commit_fails)
VM_UNITTEST(vmm_alloc_contiguous_zero_size_fails)
VM_UNITTEST(vmaspace_create_smoke_test)
VM_UNITTEST(vmaspace_alloc_smoke_test)
VM_UNITTEST(vmo_create_test)
VM_UNITTEST(vmo_commit_test)
VM_UNITTEST(vmo_odd_size_commit_test)
VM_UNITTEST(vmo_contiguous_commit_test)
VM_UNITTEST(vmo_precommitted_map_test)
VM_UNITTEST(vmo_demand_paged_map_test)
VM_UNITTEST(vmo_dropped_ref_test)
VM_UNITTEST(vmo_remap_test)
VM_UNITTEST(vmo_double_remap_test)
VM_UNITTEST(vmo_read_write_smoke_test)
VM_UNITTEST(vmo_cache_test)
// Uncomment for debugging
// VM_UNITTEST(dump_all_aspaces)  // Run last
UNITTEST_END_TESTCASE(vm_tests, "vmtests", "Virtual memory tests", nullptr, nullptr);
