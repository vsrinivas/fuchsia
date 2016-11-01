// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app/tests.h>
#include <assert.h>
#include <err.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_region.h>
#include <mxtl/array.h>
#include <new.h>
#include <unittest.h>

static bool pmm_tests(void* context) {
    BEGIN_TEST;
    // allocate a single page, translate it to a vm_page_t and free it
    unittest_printf("allocating single page, then freeing it\n");
    {
        paddr_t pa;

        vm_page_t* page = pmm_alloc_page(0, &pa);
        EXPECT_NEQ(nullptr, page, "pmm_alloc single page");
        EXPECT_NEQ(0u, pa, "pmm_alloc single page");

        vm_page_t* page2 = paddr_to_vm_page(pa);
        EXPECT_EQ(page2, page, "paddr_to_vm_page on single page");

        auto ret = pmm_free_page(page);
        EXPECT_EQ(1u, ret, "pmm_free_page on single page");
    }

    // allocate a bunch of pages then free them
    unittest_printf("allocating a lot of pages, then freeing them\n");
    {
        list_node list = LIST_INITIAL_VALUE(list);

        static const size_t alloc_count = 1024;

        auto count = pmm_alloc_pages(alloc_count, 0, &list);
        EXPECT_EQ(alloc_count, count, "pmm_alloc_pages a bunch of pages count");
        EXPECT_EQ(alloc_count, list_length(&list), "pmm_alloc_pages a bunch of pages list count");

        auto ret = pmm_free(&list);
        EXPECT_EQ(alloc_count, ret, "pmm_free_page on a list of pages");
    }

    // allocate too many pages and make sure it fails nicely
    unittest_printf("allocating too many pages, then freeing them\n");
    {
        list_node list = LIST_INITIAL_VALUE(list);

        static const size_t alloc_count = (128 * 1024 * 1024 * 1024ULL) / PAGE_SIZE; // 128GB

        auto count = pmm_alloc_pages(alloc_count, 0, &list);
        EXPECT_NEQ(alloc_count, 0, "pmm_alloc_pages too many pages count > 0");
        EXPECT_NEQ(alloc_count, count, "pmm_alloc_pages too many pages count");
        EXPECT_EQ(count, list_length(&list), "pmm_alloc_pages too many pages list count");

        auto ret = pmm_free(&list);
        EXPECT_EQ(count, ret, "pmm_free_page on a list of pages");
    }

    unittest_printf("done with pmm tests\n");
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
            unittest_printf("value at %p (%zu) is incorrect: 0x%x vs 0x%x\n", &ptr[i], i, ptr[i], val);
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

static bool vmm_tests(void* context) {
    BEGIN_TEST;
    unittest_printf("allocating a region in kernel space, read/write it, then destroy it\n");
    {
        static const size_t alloc_size = 256 * 1024;
        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

        // allocate a region of memory
        void* ptr;
        auto err = vmm_alloc(vmm_get_kernel_aspace(), "test", alloc_size, &ptr, 0, 0, 0, arch_rw_flags);
        EXPECT_EQ(0, err, "vmm_allocate region of memory");
        EXPECT_NEQ(nullptr, ptr, "vmm_allocate region of memory");

        // fill with known pattern and test
        if (!fill_and_test(ptr, alloc_size))
            all_ok = false;

        // free the region
        err = vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)ptr);
        EXPECT_EQ(0, err, "vmm_free_region region of memory");
    }

    unittest_printf("allocating a contiguous region in kernel space, read/write it, then destroy it\n");
    {
        static const size_t alloc_size = 256 * 1024;

        // allocate a region of memory
        void* ptr;
        auto err = vmm_alloc_contiguous(vmm_get_kernel_aspace(), "test", alloc_size, &ptr, 0, 0,
                                        VMM_FLAG_COMMIT,
                                        ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
        EXPECT_EQ(0, err, "vmm_allocate_contiguous region of memory");
        EXPECT_NEQ(nullptr, ptr, "vmm_allocate_contiguous region of memory");

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
        err = vmm_free_region(vmm_get_kernel_aspace(), (vaddr_t)ptr);
        EXPECT_EQ(0, err, "vmm_free_region region of memory");
    }

    unittest_printf("allocating a new address space and creating a few regions in it, then destroy it\n");
    {
        void* ptr;
        vmm_aspace_t* aspace;
        static const size_t alloc_size = 16 * 1024;
        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

        auto err = vmm_create_aspace(&aspace, "test aspace", 0);
        EXPECT_EQ(0, err, "vmm_allocate_aspace error code");
        EXPECT_NEQ(nullptr, aspace, "vmm_allocate_aspace pointer");

        vmm_aspace_t* old_aspace = get_current_thread()->aspace;
        vmm_set_active_aspace(aspace);

        // allocate region 0
        err = vmm_alloc(aspace, "test0", alloc_size, &ptr, 0, 0, 0, arch_rw_flags);
        EXPECT_EQ(0, err, "vmm_allocate region of memory");
        EXPECT_NEQ(nullptr, ptr, "vmm_allocate region of memory");

        // fill with known pattern and test
        if (!fill_and_test(ptr, alloc_size))
            all_ok = false;

        // allocate region 1
        err = vmm_alloc(aspace, "test1", 16384, &ptr, 0, 0, 0, arch_rw_flags);
        EXPECT_EQ(0, err, "vmm_allocate region of memory");
        EXPECT_NEQ(nullptr, ptr, "vmm_allocate region of memory");

        // fill with known pattern and test
        if (!fill_and_test(ptr, alloc_size))
            all_ok = false;

        // allocate region 2
        err = vmm_alloc(aspace, "test2", 16384, &ptr, 0, 0, 0, arch_rw_flags);
        EXPECT_EQ(0, err, "vmm_allocate region of memory");
        EXPECT_NEQ(nullptr, ptr, "vmm_allocate region of memory");

        // fill with known pattern and test
        if (!fill_and_test(ptr, alloc_size))
            all_ok = false;

        vmm_set_active_aspace(old_aspace);

        // free the address space all at once
        err = vmm_free_aspace(aspace);
        EXPECT_EQ(0, err, "vmm_free_aspace");
    }

    unittest_printf("allocating a new address space and creating a few gap regions in it, then destroy it\n");
    {
        void* ptr[3];
        vmm_aspace_t* aspace;
        static const size_t alloc_size = 4 * PAGE_SIZE;
        static const size_t gap_size = PAGE_SIZE;
        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

        auto err = vmm_create_aspace(&aspace, "test aspace", 0);
        EXPECT_EQ(0, err, "vmm_allocate_aspace error code");
        EXPECT_NEQ(nullptr, aspace, "vmm_allocate_aspace pointer");

        vmm_aspace_t* old_aspace = get_current_thread()->aspace;
        vmm_set_active_aspace(aspace);

        // allocate regions
        for (unsigned int i = 0; i < countof(ptr); ++i) {
            err = vmm_alloc(aspace, "test0", alloc_size, &ptr[i], 0, gap_size, 0, arch_rw_flags);
            EXPECT_EQ(0, err, "vmm_allocate region of memory");
            EXPECT_NEQ(nullptr, ptr[i], "vmm_allocate region of memory");

            // fill with known pattern and test
            if (!fill_and_test(ptr[i], alloc_size))
                all_ok = false;
        }

        for (void* p : ptr) {
            paddr_t paddr;
            vaddr_t vaddr = reinterpret_cast<vaddr_t>(p);
            arch_aspace_t* aaspace = vmm_get_arch_aspace(aspace);

            vaddr_t addresses[] = {
                vaddr - 1, vaddr - gap_size + 1,
                vaddr + alloc_size, vaddr + alloc_size + gap_size - 1,
            };

            for (vaddr_t address : addresses) {
                if (vaddr_to_aspace(reinterpret_cast<void*>(address)) == aspace) {
                    err = arch_mmu_query(aaspace, address, &paddr, NULL);
                    EXPECT_EQ(ERR_NOT_FOUND, err, "gap occupied");
                }
            }
        }

        vmm_set_active_aspace(old_aspace);

        // free the address space all at once
        err = vmm_free_aspace(aspace);
        EXPECT_EQ(0, err, "vmm_free_aspace");
    }

    unittest_printf("test for some invalid arguments\n");
    {
        void* ptr;
        status_t err;
        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

        // zero size
        err = vmm_alloc(vmm_get_kernel_aspace(), "test", 0, &ptr, 0, 0, 0, arch_rw_flags);
        EXPECT_EQ(ERR_INVALID_ARGS, err, "invalid args to vmm_alloc");

        // bad pointer
        ptr = (void*)1;
        err = vmm_alloc(vmm_get_kernel_aspace(), "test", 16384, &ptr, 0, 0,
                        VMM_FLAG_VALLOC_SPECIFIC | VMM_FLAG_COMMIT, arch_rw_flags);
        EXPECT_EQ(ERR_INVALID_ARGS, err, "invalid args to vmm_alloc");

        // should have VMM_FLAG_COMMIT
        err = vmm_alloc_contiguous(vmm_get_kernel_aspace(), "test", 4096, &ptr, 0, 0, 0,
                                   arch_rw_flags);
        EXPECT_EQ(ERR_INVALID_ARGS, err, "invalid args to vmm_alloc_contiguous");

        // zero size
        err = vmm_alloc_contiguous(vmm_get_kernel_aspace(), "test", 0, &ptr, 0, 0, VMM_FLAG_COMMIT,
                                   arch_rw_flags);
        EXPECT_EQ(ERR_INVALID_ARGS, err, "invalid args to vmm_alloc_contiguous");
    }

    unittest_printf("allocating a vm address space object directly, allowing it to go out of scope\n");
    {
        auto aspace = VmAspace::Create(0, "test aspace");
    }

    unittest_printf("allocating a vm address space object directly, mapping somethign on it, allowing it to go out of scope\n");
    {
        auto aspace = VmAspace::Create(0, "test aspace2");
        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

        void* ptr;
        auto err = aspace->Alloc("test", PAGE_SIZE, &ptr, 0, 0, 0, arch_rw_flags);
        EXPECT_EQ(NO_ERROR, err, "allocating region\n");

        // destroy the aspace, which should drop all the internal refs to it
        aspace->Destroy();

        // drop the ref held by this pointer
        aspace.reset();
    }

    unittest_printf("verify there are no test aspaces left around\n");
    DumpAllAspaces();

    unittest_printf("done with vmm tests\n");
    END_TEST;
}

static bool vmm_object_tests(void* context) {
    BEGIN_TEST;
    unittest_printf("creating vm object\n");
    {
        auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, PAGE_SIZE);
        EXPECT_TRUE(vmo, "vmobject creation\n");
    }

    unittest_printf("creating vm object, committing memory\n");
    {
        static const size_t alloc_size = PAGE_SIZE * 16;
        auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
        EXPECT_TRUE(vmo, "vmobject creation\n");

        auto ret = vmo->CommitRange(0, alloc_size);
        EXPECT_EQ((ssize_t)alloc_size, ret, "committing vm object\n");
    }

    unittest_printf("creating vm object, committing odd sized memory\n");
    {
        static const size_t alloc_size = 15;
        auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
        EXPECT_TRUE(vmo, "vmobject creation\n");

        auto ret = vmo->CommitRange(0, alloc_size);
        EXPECT_EQ((ssize_t)alloc_size, ret, "committing vm object\n");
    }

    unittest_printf("creating vm object, committing contiguous memory\n");
    {
        static const size_t alloc_size = PAGE_SIZE * 16;
        auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
        EXPECT_TRUE(vmo, "vmobject creation\n");

        auto ret = vmo->CommitRangeContiguous(0, alloc_size, 0);
        EXPECT_EQ((ssize_t)alloc_size, ret, "committing vm object contiguously\n");
    }

    unittest_printf("creating vm object, mapping it, precommitted\n");
    {
        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
        static const size_t alloc_size = PAGE_SIZE * 16;
        auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
        EXPECT_TRUE(vmo, "vmobject creation\n");

        auto ka = VmAspace::kernel_aspace();
        void* ptr;
        auto ret =
            ka->MapObject(vmo, "test", 0, alloc_size, &ptr, 0, 0, VMM_FLAG_COMMIT, arch_rw_flags);
        EXPECT_EQ(NO_ERROR, ret, "mapping object");

        // fill with known pattern and test
        if (!fill_and_test(ptr, alloc_size))
            all_ok = false;

        auto err = ka->FreeRegion((vaddr_t)ptr);
        EXPECT_EQ(NO_ERROR, err, "unmapping object");
    }

    unittest_printf("creating vm object, mapping it, demand paged\n");
    {
        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
        static const size_t alloc_size = PAGE_SIZE * 16;
        auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
        EXPECT_TRUE(vmo, "vmobject creation\n");

        auto ka = VmAspace::kernel_aspace();
        void* ptr;
        auto ret = ka->MapObject(vmo, "test", 0, alloc_size, &ptr, 0, 0, 0, arch_rw_flags);
        EXPECT_EQ(ret, NO_ERROR, "mapping object");

        // fill with known pattern and test
        if (!fill_and_test(ptr, alloc_size))
            all_ok = false;

        auto err = ka->FreeRegion((vaddr_t)ptr);
        EXPECT_EQ(NO_ERROR, err, "unmapping object");
    }

    unittest_printf("creating vm object, mapping it, dropping ref before unmapping\n");
    {
        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
        static const size_t alloc_size = PAGE_SIZE * 16;
        auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
        EXPECT_TRUE(vmo, "vmobject creation\n");

        auto ka = VmAspace::kernel_aspace();
        void* ptr;
        auto ret = ka->MapObject(mxtl::move(vmo), "test", 0, alloc_size, &ptr, 0, 0, VMM_FLAG_COMMIT,
                                 arch_rw_flags);
        EXPECT_EQ(ret, NO_ERROR, "mapping object");

        EXPECT_FALSE(vmo, "dropped ref to object");

        // fill with known pattern and test
        if (!fill_and_test(ptr, alloc_size))
            all_ok = false;

        auto err = ka->FreeRegion((vaddr_t)ptr);
        EXPECT_EQ(NO_ERROR, err, "unmapping object");
    }

    unittest_printf(
        "creating vm object, mapping it, filling it with data, unmapping, mapping again somewhere "
        "else\n");
    {
        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
        static const size_t alloc_size = PAGE_SIZE * 16;
        auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
        EXPECT_TRUE(vmo, "vmobject creation\n");

        auto ka = VmAspace::kernel_aspace();
        void* ptr;
        auto ret =
            ka->MapObject(vmo, "test", 0, alloc_size, &ptr, 0, 0, VMM_FLAG_COMMIT, arch_rw_flags);
        EXPECT_EQ(NO_ERROR, ret, "mapping object");

        // fill with known pattern and test
        if (!fill_and_test(ptr, alloc_size))
            all_ok = false;

        auto err = ka->FreeRegion((vaddr_t)ptr);
        EXPECT_EQ(NO_ERROR, err, "unmapping object");

        // map it again
        ret =
            ka->MapObject(vmo, "test", 0, alloc_size, &ptr, 0, 0, VMM_FLAG_COMMIT, arch_rw_flags);
        EXPECT_EQ(ret, NO_ERROR, "mapping object");

        // test that the pattern is still valid
        bool result = test_region((uintptr_t)ptr, ptr, alloc_size);
        EXPECT_TRUE(result, "testing region for corruption");

        err = ka->FreeRegion((vaddr_t)ptr);
        EXPECT_EQ(NO_ERROR, err, "unmapping object");
    }

    unittest_printf(
        "creating vm object, mapping it, filling it with data, mapping it a second time and third "
        "time somwehere else\n");
    {
        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
        static const size_t alloc_size = PAGE_SIZE * 16;
        auto vmo = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, alloc_size);
        EXPECT_TRUE(vmo, "vmobject creation\n");

        auto ka = VmAspace::kernel_aspace();
        void* ptr;
        auto ret = ka->MapObject(vmo, "test0", 0, alloc_size, &ptr, 0, 0, 0, arch_rw_flags);
        EXPECT_EQ(NO_ERROR, ret, "mapping object");

        // fill with known pattern and test
        if (!fill_and_test(ptr, alloc_size))
            all_ok = false;

        // map it again
        void* ptr2;
        ret = ka->MapObject(vmo, "test1", 0, alloc_size, &ptr2, 0, 0, 0, arch_rw_flags);
        EXPECT_EQ(ret, NO_ERROR, "mapping object second time");
        EXPECT_NEQ(ptr, ptr2, "second mapping is different");

        // test that the pattern is still valid
        bool result = test_region((uintptr_t)ptr, ptr2, alloc_size);
        EXPECT_TRUE(result, "testing region for corruption");

        // map it a third time with an offset
        void* ptr3;
        static const size_t alloc_offset = PAGE_SIZE;
        ret = ka->MapObject(vmo, "test2", alloc_offset, alloc_size - alloc_offset, &ptr3, 0, 0, 0,
                            arch_rw_flags);
        EXPECT_EQ(ret, NO_ERROR, "mapping object third time");
        EXPECT_NEQ(ptr3, ptr2, "third mapping is different");
        EXPECT_NEQ(ptr3, ptr, "third mapping is different");

        // test that the pattern is still valid
        int mc = memcmp((uint8_t*)ptr + alloc_offset, ptr3, alloc_size - alloc_offset);
        EXPECT_EQ(0, mc, "testing region for corruption");

        ret = ka->FreeRegion((vaddr_t)ptr3);
        EXPECT_EQ(NO_ERROR, ret, "unmapping object third time");

        ret = ka->FreeRegion((vaddr_t)ptr2);
        EXPECT_EQ(NO_ERROR, ret, "unmapping object second time");

        ret = ka->FreeRegion((vaddr_t)ptr);
        EXPECT_EQ(NO_ERROR, ret, "unmapping object");
    }

    unittest_printf("creating vm object, writing to it\n");
    {
        const uint arch_rw_flags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
        static const size_t alloc_size = PAGE_SIZE * 16;

        // create object
        auto vmo = VmObjectPaged::Create(0, alloc_size);
        EXPECT_TRUE(vmo, "vmobject creation\n");

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
        err = ka->MapObject(vmo, "test", 0, alloc_size, (void**)&ptr, 0, 0, 0, arch_rw_flags);
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
        unittest_printf("reading back from vm object\n");
        mxtl::Array<uint8_t> b(new (&ac) uint8_t[alloc_size], alloc_size);
        EXPECT_TRUE(ac.check(), "");

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
    }

    unittest_printf("done with vmm object based tests\n");
    END_TEST;
}

UNITTEST_START_TESTCASE(vm_tests)
UNITTEST("pmm tests", pmm_tests)
UNITTEST("vmm tests", vmm_tests)
UNITTEST("vm object based test", vmm_object_tests)
UNITTEST_END_TESTCASE(vm_tests, "vmtests", "Virtual memory tests", NULL, NULL);
