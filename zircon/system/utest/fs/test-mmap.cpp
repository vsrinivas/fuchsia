// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <unittest/unittest.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>

#include "filesystems.h"

namespace {

// Certain filesystems delay creation of internal structures
// until the file is initially accessed. Test that we can
// actually mmap properly before the file has otherwise been
// accessed.
bool TestMmapEmpty(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    constexpr char kFilename[] = "::mmap_empty";
    fbl::unique_fd fd(open(kFilename, O_RDWR | O_CREAT | O_EXCL));
    ASSERT_TRUE(fd);

    char tmp[] = "this is a temporary buffer";
    void* addr = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED);
    ASSERT_EQ(write(fd.get(), tmp, sizeof(tmp)), sizeof(tmp));
    ASSERT_EQ(memcmp(addr, tmp, sizeof(tmp)), 0);

    ASSERT_EQ(munmap(addr, PAGE_SIZE), 0, "munmap failed");
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(unlink(kFilename), 0);
    END_TEST;
}

// Test that a file's writes are properly propagated to
// a read-only buffer.
bool TestMmapReadable(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    constexpr char kFilename[] = "::mmap_readable";
    fbl::unique_fd fd(open(kFilename, O_RDWR | O_CREAT | O_EXCL));
    ASSERT_TRUE(fd);

    char tmp1[] = "this is a temporary buffer";
    char tmp2[] = "and this is a secondary buffer";
    ASSERT_EQ(write(fd.get(), tmp1, sizeof(tmp1)), sizeof(tmp1));

    // Demonstrate that a simple buffer can be mapped
    void* addr = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED);
    ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);

    // Show that if we keep writing to the file, the mapping
    // is also updated
    ASSERT_EQ(write(fd.get(), tmp2, sizeof(tmp2)), sizeof(tmp2));
    void* addr2 = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + sizeof(tmp1));
    ASSERT_EQ(memcmp(addr2, tmp2, sizeof(tmp2)), 0);

    // But the original part of the mapping is unchanged
    ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);

    ASSERT_EQ(munmap(addr, PAGE_SIZE), 0, "munmap failed");
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(unlink(kFilename), 0);
    END_TEST;
}

// Test that a mapped buffer's writes are properly propagated
// to the file.
bool TestMmapWritable(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    constexpr char kFilename[] = "::mmap_writable";
    fbl::unique_fd fd(open(kFilename, O_RDWR | O_CREAT | O_EXCL));
    ASSERT_TRUE(fd);

    char tmp1[] = "this is a temporary buffer";
    char tmp2[] = "and this is a secondary buffer";
    ASSERT_EQ(write(fd.get(), tmp1, sizeof(tmp1)), sizeof(tmp1));

    // Demonstrate that a simple buffer can be mapped
    void* addr = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED);
    ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);

    // Extend the file length up to the necessary size
    ASSERT_EQ(ftruncate(fd.get(), sizeof(tmp1) + sizeof(tmp2)), 0);

    // Write to the file in the mapping
    void* addr2 = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + sizeof(tmp1));
    memcpy(addr2, tmp2, sizeof(tmp2));

    // Verify the write by reading from the file
    char buf[sizeof(tmp2)];
    ASSERT_EQ(read(fd.get(), buf, sizeof(buf)), sizeof(buf));
    ASSERT_EQ(memcmp(buf, tmp2, sizeof(tmp2)), 0);
    // But the original part of the mapping is unchanged
    ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);

    // Extending the file beyond the mapping should still leave the first page
    // accessible
    ASSERT_EQ(ftruncate(fd.get(), PAGE_SIZE * 2), 0);
    ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);
    ASSERT_EQ(memcmp(addr2, tmp2, sizeof(tmp2)), 0);
    for (size_t i = sizeof(tmp1) + sizeof(tmp2); i < PAGE_SIZE; i++) {
        auto caddr = reinterpret_cast<char*>(addr);
        ASSERT_EQ(caddr[i], 0);
    }

    ASSERT_EQ(munmap(addr, PAGE_SIZE), 0, "munmap failed");
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(unlink(kFilename), 0);

    END_TEST;
}

// Test that the mapping of a file remains usable even after
// the file has been closed / unlinked / renamed.
bool TestMmapUnlinked(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    constexpr char kFilename[] = "::mmap_unlinked";
    fbl::unique_fd fd(open(kFilename, O_RDWR | O_CREAT | O_EXCL));
    ASSERT_TRUE(fd);

    char tmp[] = "this is a temporary buffer";
    ASSERT_EQ(write(fd.get(), tmp, sizeof(tmp)), sizeof(tmp));

    // Demonstrate that a simple buffer can be mapped
    void* addr = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED);
    ASSERT_EQ(memcmp(addr, tmp, sizeof(tmp)), 0);

    // If we close the file, we can still access the mapping
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(memcmp(addr, tmp, sizeof(tmp)), 0);

    // If we rename the file, we can still access the mapping
    ASSERT_EQ(rename(kFilename, "::otherfile"), 0);
    ASSERT_EQ(memcmp(addr, tmp, sizeof(tmp)), 0);

    // If we unlink the file, we can still access the mapping
    ASSERT_EQ(unlink("::otherfile"), 0);
    ASSERT_EQ(memcmp(addr, tmp, sizeof(tmp)), 0);

    ASSERT_EQ(munmap(addr, PAGE_SIZE), 0, "munmap failed");
    END_TEST;
}

// Test that MAP_SHARED propagates updates to the file
bool TestMmapShared(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    constexpr char kFilename[] = "::mmap_shared";
    fbl::unique_fd fd(open(kFilename, O_RDWR | O_CREAT | O_EXCL));
    ASSERT_TRUE(fd);

    char tmp[] = "this is a temporary buffer";
    ASSERT_EQ(write(fd.get(), tmp, sizeof(tmp)), sizeof(tmp));

    // Demonstrate that a simple buffer can be mapped
    void* addr1 = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
    ASSERT_NE(addr1, MAP_FAILED);
    ASSERT_EQ(memcmp(addr1, tmp, sizeof(tmp)), 0);

    fbl::unique_fd fd2(open(kFilename, O_RDWR));
    ASSERT_TRUE(fd2);

    // Demonstrate that the buffer can be mapped multiple times
    void* addr2 = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd2.get(), 0);
    ASSERT_NE(addr2, MAP_FAILED);
    ASSERT_EQ(memcmp(addr2, tmp, sizeof(tmp)), 0);

    // Demonstrate that updates to the file are shared between mappings
    char tmp2[] = "buffer which will update through fd";
    ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
    ASSERT_EQ(write(fd.get(), tmp2, sizeof(tmp2)), sizeof(tmp2));
    ASSERT_EQ(memcmp(addr1, tmp2, sizeof(tmp2)), 0);
    ASSERT_EQ(memcmp(addr2, tmp2, sizeof(tmp2)), 0);

    // Demonstrate that updates to the mappings are shared too
    char tmp3[] = "final buffer, which updates via mapping";
    memcpy(addr1, tmp3, sizeof(tmp3));
    ASSERT_EQ(memcmp(addr1, tmp3, sizeof(tmp3)), 0);
    ASSERT_EQ(memcmp(addr2, tmp3, sizeof(tmp3)), 0);
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(close(fd2.release()), 0);
    ASSERT_EQ(munmap(addr2, PAGE_SIZE), 0, "munmap failed");

    // Demonstrate that we can map a read-only file as shared + readable
    fd.reset(open(kFilename, O_RDONLY));
    ASSERT_TRUE(fd);
    addr2 = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 0);
    ASSERT_NE(addr2, MAP_FAILED);
    ASSERT_EQ(memcmp(addr1, tmp3, sizeof(tmp3)), 0);
    ASSERT_EQ(memcmp(addr2, tmp3, sizeof(tmp3)), 0);
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(munmap(addr2, PAGE_SIZE), 0, "munmap failed");

    ASSERT_EQ(munmap(addr1, PAGE_SIZE), 0, "munmap failed");
    ASSERT_EQ(unlink(kFilename), 0);

    END_TEST;
}

// Test that MAP_PRIVATE keeps all copies of the buffer
// separate
bool TestMmapPrivate(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    constexpr char kFilename[] = "::mmap_private";
    fbl::unique_fd fd(open(kFilename, O_RDWR | O_CREAT | O_EXCL));
    ASSERT_TRUE(fd);

    char buf[64];
    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), sizeof(buf));

    // Demonstrate that a simple buffer can be mapped
    void* addr1 = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr1, MAP_FAILED);
    ASSERT_EQ(memcmp(addr1, buf, sizeof(buf)), 0);
    // ... multiple times
    void* addr2 = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr2, MAP_FAILED);
    ASSERT_EQ(memcmp(addr2, buf, sizeof(buf)), 0);

    // File: 'a'
    // addr1 private copy: 'b'
    // addr2 private copy: 'c'
    memset(buf, 'b', sizeof(buf));
    memcpy(addr1, buf, sizeof(buf));
    memset(buf, 'c', sizeof(buf));
    memcpy(addr2, buf, sizeof(buf));

    // Verify the file and two buffers all have independent contents
    memset(buf, 'a', sizeof(buf));
    char tmp[sizeof(buf)];
    ASSERT_EQ(lseek(fd.get(), SEEK_SET, 0), 0);
    ASSERT_EQ(read(fd.get(), tmp, sizeof(tmp)), sizeof(tmp));
    ASSERT_EQ(memcmp(tmp, buf, sizeof(tmp)), 0);
    memset(buf, 'b', sizeof(buf));
    ASSERT_EQ(memcmp(addr1, buf, sizeof(buf)), 0);
    memset(buf, 'c', sizeof(buf));
    ASSERT_EQ(memcmp(addr2, buf, sizeof(buf)), 0);

    ASSERT_EQ(munmap(addr1, PAGE_SIZE), 0, "munmap failed");
    ASSERT_EQ(munmap(addr2, PAGE_SIZE), 0, "munmap failed");
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(unlink(kFilename), 0);

    END_TEST;
}

// Test that mmap fails with appropriate error codes when
// we expect.
bool TestMmapEvil(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    // Try (and fail) to mmap a directory
    ASSERT_EQ(mkdir("::mydir", 0666), 0);
    fbl::unique_fd fd(open("::mydir", O_RDONLY | O_DIRECTORY));
    ASSERT_TRUE(fd);
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(close(fd.release()), 0);
    ASSERT_EQ(rmdir("::mydir"), 0);

    fd.reset(open("::myfile", O_RDWR | O_CREAT | O_EXCL));
    ASSERT_TRUE(fd);

    // Mmap without MAP_PRIVATE or MAP_SHARED
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ, 0, fd.get(), 0), MAP_FAILED);
    ASSERT_EQ(errno, EINVAL);
    errno = 0;
    // Mmap with both MAP_PRIVATE and MAP_SHARED
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED | MAP_PRIVATE, fd.get(), 0), MAP_FAILED);
    ASSERT_EQ(errno, EINVAL);
    errno = 0;
    // Mmap with unaligned offset
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 1), MAP_FAILED);
    ASSERT_EQ(errno, EINVAL);
    errno = 0;
    // Mmap with a length of zero
    ASSERT_EQ(mmap(NULL, 0, PROT_READ, MAP_SHARED, fd.get(), 0), MAP_FAILED);
    ASSERT_EQ(errno, EINVAL);
    errno = 0;
    ASSERT_EQ(close(fd.release()), 0);
    // Test all cases of MAP_PRIVATE and MAP_SHARED which require
    // a readable file.
    fd.reset(open("::myfile", O_WRONLY));
    ASSERT_TRUE(fd);
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ, MAP_PRIVATE, fd.get(), 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_WRITE, MAP_PRIVATE, fd.get(), 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd.get(), 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_WRITE, MAP_SHARED, fd.get(), 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(close(fd.release()), 0);
    // Test all cases of MAP_PRIVATE and MAP_SHARED which require a
    // writable file (notably, MAP_PRIVATE never requires a writable
    // file, since it makes a copy).
    fd.reset(open("::myfile", O_RDONLY));
    ASSERT_TRUE(fd);
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_WRITE, MAP_SHARED, fd.get(), 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(close(fd.release()), 0);
    // PROT_WRITE requires that the file is NOT append-only
    fd.reset(open("::myfile", O_RDONLY | O_APPEND));
    ASSERT_TRUE(fd);
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_WRITE, MAP_SHARED, fd.get(), 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(close(fd.release()), 0);

    ASSERT_EQ(unlink("::myfile"), 0);
    END_TEST;
}

bool TestMmapTruncateAccess(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    fbl::unique_fd fd(open("::mmap_truncate", O_CREAT | O_RDWR));
    ASSERT_TRUE(fd);

    constexpr size_t kPageCount = 5;
    char buf[PAGE_SIZE * kPageCount];
    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), sizeof(buf));

    // Map all pages and validate their contents.
    void* addr = mmap(NULL, sizeof(buf), PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED);
    ASSERT_EQ(memcmp(addr, buf, sizeof(buf)), 0);

    constexpr size_t kHalfPage = PAGE_SIZE / 2;
    for (size_t i = (kPageCount * 2) - 1; i > 0; i--) {
        // Shrink the underlying file.
        size_t new_size = kHalfPage * i;
        ASSERT_EQ(ftruncate(fd.get(), new_size), 0);
        ASSERT_EQ(memcmp(addr, buf, new_size), 0);

        // Accessing beyond the end of the file, but within the mapping, is
        // undefined behavior on other platforms. However, on Fuchsia, this
        // behavior is explicitly memory-safe.
        char buf_beyond[PAGE_SIZE * kPageCount - new_size];
        memset(buf_beyond, 'b', sizeof(buf_beyond));
        void* beyond = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + new_size);
        memset(beyond, 'b', sizeof(buf_beyond));
        ASSERT_EQ(memcmp(buf_beyond, beyond, sizeof(buf_beyond)), 0);
    }

    ASSERT_EQ(munmap(addr, sizeof(buf)), 0);
    ASSERT_EQ(unlink("::mmap_truncate"), 0);

    END_TEST;
}

bool TestMmapTruncateExtend(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    fbl::unique_fd fd(open("::mmap_truncate_extend", O_CREAT | O_RDWR));
    ASSERT_TRUE(fd);

    constexpr size_t kPageCount = 5;
    char buf[PAGE_SIZE * kPageCount];
    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), sizeof(buf));

    // Map all pages and validate their contents.
    void* addr = mmap(NULL, sizeof(buf), PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED);
    ASSERT_EQ(memcmp(addr, buf, sizeof(buf)), 0);

    constexpr size_t kHalfPage = PAGE_SIZE / 2;

    ASSERT_EQ(ftruncate(fd.get(), 0), 0);
    memset(buf, 0, sizeof(buf));

    // Even though we trample over the "out-of-bounds" part of the mapping,
    // ensure it is filled with zeroes as we truncate-extend it.
    for (size_t i = 1; i < kPageCount * 2; i++) {
        size_t new_size = kHalfPage * i;

        // Fill "out-of-bounds" with invalid data.
        char buf_beyond[PAGE_SIZE * kPageCount - new_size];
        memset(buf_beyond, 'b', sizeof(buf_beyond));
        void* beyond = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + new_size);
        memset(beyond, 'b', sizeof(buf_beyond));
        ASSERT_EQ(memcmp(buf_beyond, beyond, sizeof(buf_beyond)), 0);

        // Observe that the truncate extension fills the file with zeroes.
        ASSERT_EQ(ftruncate(fd.get(), new_size), 0);
        ASSERT_EQ(memcmp(buf, addr, new_size), 0);
    }

    ASSERT_EQ(munmap(addr, sizeof(buf)), 0);
    ASSERT_EQ(unlink("::mmap_truncate_extend"), 0);

    END_TEST;
}

bool TestMmapTruncateWriteExtend(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    fbl::unique_fd fd(open("::mmap_write_extend", O_CREAT | O_RDWR));
    ASSERT_TRUE(fd);

    constexpr size_t kPageCount = 5;
    char buf[PAGE_SIZE * kPageCount];
    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), sizeof(buf));

    // Map all pages and validate their contents.
    void* addr = mmap(NULL, sizeof(buf), PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED);
    ASSERT_EQ(memcmp(addr, buf, sizeof(buf)), 0);

    constexpr size_t kHalfPage = PAGE_SIZE / 2;

    ASSERT_EQ(ftruncate(fd.get(), 0), 0);
    memset(buf, 0, sizeof(buf));

    // Even though we trample over the "out-of-bounds" part of the mapping,
    // ensure it is filled with zeroes as we truncate-extend it.
    for (size_t i = 1; i < kPageCount * 2; i++) {
        size_t new_size = kHalfPage * i;

        // Fill "out-of-bounds" with invalid data.
        char buf_beyond[PAGE_SIZE * kPageCount - new_size];
        memset(buf_beyond, 'b', sizeof(buf_beyond));
        void* beyond = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + new_size);
        memset(beyond, 'b', sizeof(buf_beyond));
        ASSERT_EQ(memcmp(buf_beyond, beyond, sizeof(buf_beyond)), 0);

        // Observe that write extension fills the file with zeroes.
        off_t offset = static_cast<off_t>(new_size - 1);
        ASSERT_EQ(lseek(fd.get(), offset, SEEK_SET), offset);
        char zero = 0;
        ASSERT_EQ(write(fd.get(), &zero, 1), 1);
        ASSERT_EQ(memcmp(buf, addr, new_size), 0);
    }

    ASSERT_EQ(munmap(addr, sizeof(buf)), 0);
    ASSERT_EQ(unlink("::mmap_write_extend"), 0);

    END_TEST;
}

enum RW {
    Read,
    Write,
    ReadAfterUnmap,
    WriteAfterUnmap,
};

bool mmap_crash(int prot, int flags, RW rw) {
    BEGIN_HELPER;
    fbl::unique_fd fd(open("::inaccessible", O_RDWR));
    ASSERT_TRUE(fd);
    void* addr = mmap(NULL, PAGE_SIZE, prot, flags, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED);
    ASSERT_EQ(close(fd.release()), 0);

    if (rw == RW::Read || rw == RW::ReadAfterUnmap) {
        // Read
        if (rw == RW::ReadAfterUnmap) {
            ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
        }

        ASSERT_DEATH([](void* addr) -> void {
            (void)*static_cast<volatile int*>(addr);
        },
                     addr, "");

        if (rw == RW::Read) {
            ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
        }
    } else {
        // Write
        if (rw == RW::WriteAfterUnmap) {
            ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
        }

        ASSERT_DEATH([](void* addr) {
            *static_cast<int*>(addr) = 5;
        },
                     addr, "");

        if (rw == RW::Write) {
            ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
        }
    }
    END_HELPER;
}

bool TestMmapDeath(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    fbl::unique_fd fd(open("::inaccessible", O_RDWR | O_CREAT));
    ASSERT_TRUE(fd);
    char tmp[] = "this is a temporary buffer";
    ASSERT_EQ(write(fd.get(), tmp, sizeof(tmp)), sizeof(tmp));
    ASSERT_EQ(close(fd.release()), 0);

    // Crashes while mapped
    ASSERT_TRUE(mmap_crash(PROT_READ, MAP_PRIVATE, Write));
    ASSERT_TRUE(mmap_crash(PROT_READ, MAP_SHARED, Write));
    // Write-only is not possible
    ASSERT_TRUE(mmap_crash(PROT_NONE, MAP_SHARED, Read));
    ASSERT_TRUE(mmap_crash(PROT_NONE, MAP_SHARED, Write));

    // Crashes after unmapped
    ASSERT_TRUE(mmap_crash(PROT_READ, MAP_PRIVATE, ReadAfterUnmap));
    ASSERT_TRUE(mmap_crash(PROT_READ, MAP_SHARED, ReadAfterUnmap));
    ASSERT_TRUE(mmap_crash(PROT_WRITE | PROT_READ, MAP_PRIVATE, WriteAfterUnmap));
    ASSERT_TRUE(mmap_crash(PROT_WRITE | PROT_READ, MAP_SHARED, WriteAfterUnmap));
    ASSERT_TRUE(mmap_crash(PROT_NONE, MAP_SHARED, WriteAfterUnmap));

    ASSERT_EQ(unlink("::inaccessible"), 0);
    END_TEST;
}

} // namespace

RUN_FOR_ALL_FILESYSTEMS(fs_mmap_tests,
    RUN_TEST_MEDIUM(TestMmapEmpty)
    RUN_TEST_MEDIUM(TestMmapReadable)
    RUN_TEST_MEDIUM(TestMmapWritable)
    RUN_TEST_MEDIUM(TestMmapUnlinked)
    RUN_TEST_MEDIUM(TestMmapShared)
    RUN_TEST_MEDIUM(TestMmapPrivate)
    RUN_TEST_MEDIUM(TestMmapEvil)
    RUN_TEST_MEDIUM(TestMmapTruncateAccess)
    RUN_TEST_MEDIUM(TestMmapTruncateExtend)
    RUN_TEST_MEDIUM(TestMmapTruncateWriteExtend)
    RUN_TEST_ENABLE_CRASH_HANDLER(TestMmapDeath)
)
