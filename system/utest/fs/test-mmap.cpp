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

#include <magenta/compiler.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>

#include "filesystems.h"

// Certain filesystems delay creation of internal structures
// until the file is initially accessed. Test that we can
// actually mmap properly before the file has otherwise been
// accessed.
bool test_mmap_empty(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    constexpr char kFilename[] = "::mmap_empty";
    int fd = open(kFilename, O_RDWR | O_CREAT | O_EXCL);
    ASSERT_GT(fd, 0);

    char tmp[] = "this is a temporary buffer";
    void* addr = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    ASSERT_NE(addr, MAP_FAILED);
    ASSERT_EQ(write(fd, tmp, sizeof(tmp)), sizeof(tmp));
    ASSERT_EQ(memcmp(addr, tmp, sizeof(tmp)), 0);

    ASSERT_EQ(munmap(addr, PAGE_SIZE), 0, "munmap failed");
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(unlink(kFilename), 0);
    END_TEST;
}

// Test that a file's writes are properly propagated to
// a read-only buffer.
bool test_mmap_readable(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    constexpr char kFilename[] = "::mmap_readable";
    int fd = open(kFilename, O_RDWR | O_CREAT | O_EXCL);
    ASSERT_GT(fd, 0);

    char tmp1[] = "this is a temporary buffer";
    char tmp2[] = "and this is a secondary buffer";
    ASSERT_EQ(write(fd, tmp1, sizeof(tmp1)), sizeof(tmp1));

    // Demonstrate that a simple buffer can be mapped
    void* addr = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    ASSERT_NE(addr, MAP_FAILED);
    ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);

    // Show that if we keep writing to the file, the mapping
    // is also updated
    ASSERT_EQ(write(fd, tmp2, sizeof(tmp2)), sizeof(tmp2));
    void* addr2 = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + sizeof(tmp1));
    ASSERT_EQ(memcmp(addr2, tmp2, sizeof(tmp2)), 0);

    // But the original part of the mapping is unchanged
    ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);

    ASSERT_EQ(munmap(addr, PAGE_SIZE), 0, "munmap failed");
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(unlink(kFilename), 0);
    END_TEST;
}

// Test that a mapped buffer's writes are properly propagated
// to the file.
bool test_mmap_writable(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    constexpr char kFilename[] = "::mmap_writable";
    int fd = open(kFilename, O_RDWR | O_CREAT | O_EXCL);
    ASSERT_GT(fd, 0);

    char tmp1[] = "this is a temporary buffer";
    char tmp2[] = "and this is a secondary buffer";
    ASSERT_EQ(write(fd, tmp1, sizeof(tmp1)), sizeof(tmp1));

    // Demonstrate that a simple buffer can be mapped
    void* addr = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(addr, MAP_FAILED);
    ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);

    // Extend the file length up to the necessary size
    ASSERT_EQ(ftruncate(fd, sizeof(tmp1) + sizeof(tmp2)), 0);

    // Write to the file in the mapping
    void* addr2 = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + sizeof(tmp1));
    memcpy(addr2, tmp2, sizeof(tmp2));

    // Verify the write by reading from the file
    char buf[sizeof(tmp2)];
    ASSERT_EQ(read(fd, buf, sizeof(buf)), sizeof(buf));
    ASSERT_EQ(memcmp(buf, tmp2, sizeof(tmp2)), 0);
    // But the original part of the mapping is unchanged
    ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);

    // Extending the file beyond the mapping should still leave the first page
    // accessible
    ASSERT_EQ(ftruncate(fd, PAGE_SIZE * 2), 0);
    ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);
    ASSERT_EQ(memcmp(addr2, tmp2, sizeof(tmp2)), 0);
    for (size_t i = sizeof(tmp1) + sizeof(tmp2); i < PAGE_SIZE; i++) {
        auto caddr = reinterpret_cast<char*>(addr);
        ASSERT_EQ(caddr[i], 0);
    }

    ASSERT_EQ(munmap(addr, PAGE_SIZE), 0, "munmap failed");
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(unlink(kFilename), 0);

    END_TEST;
}

// Test that the mapping of a file remains usable even after
// the file has been closed / unlinked / renamed.
bool test_mmap_unlinked(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    constexpr char kFilename[] = "::mmap_unlinked";
    int fd = open(kFilename, O_RDWR | O_CREAT | O_EXCL);
    ASSERT_GT(fd, 0);

    char tmp[] = "this is a temporary buffer";
    ASSERT_EQ(write(fd, tmp, sizeof(tmp)), sizeof(tmp));

    // Demonstrate that a simple buffer can be mapped
    void* addr = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    ASSERT_NE(addr, MAP_FAILED);
    ASSERT_EQ(memcmp(addr, tmp, sizeof(tmp)), 0);

    // If we close the file, we can still access the mapping
    ASSERT_EQ(close(fd), 0);
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
bool test_mmap_shared(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    constexpr char kFilename[] = "::mmap_shared";
    int fd = open(kFilename, O_RDWR | O_CREAT | O_EXCL);
    ASSERT_GT(fd, 0);

    char tmp[] = "this is a temporary buffer";
    ASSERT_EQ(write(fd, tmp, sizeof(tmp)), sizeof(tmp));

    // Demonstrate that a simple buffer can be mapped
    void* addr1 = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(addr1, MAP_FAILED);
    ASSERT_EQ(memcmp(addr1, tmp, sizeof(tmp)), 0);

    int fd2 = open(kFilename, O_RDWR);
    ASSERT_GT(fd2, 0);

    // Demonstrate that the buffer can be mapped multiple times
    void* addr2 = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    ASSERT_NE(addr2, MAP_FAILED);
    ASSERT_EQ(memcmp(addr2, tmp, sizeof(tmp)), 0);

    // Demonstrate that updates to the file are shared between mappings
    char tmp2[] = "buffer which will update through fd";
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(write(fd, tmp2, sizeof(tmp2)), sizeof(tmp2));
    ASSERT_EQ(memcmp(addr1, tmp2, sizeof(tmp2)), 0);
    ASSERT_EQ(memcmp(addr2, tmp2, sizeof(tmp2)), 0);

    // Demonstrate that updates to the mappings are shared too
    char tmp3[] = "final buffer, which updates via mapping";
    memcpy(addr1, tmp3, sizeof(tmp3));
    ASSERT_EQ(memcmp(addr1, tmp3, sizeof(tmp3)), 0);
    ASSERT_EQ(memcmp(addr2, tmp3, sizeof(tmp3)), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(close(fd2), 0);
    ASSERT_EQ(munmap(addr2, PAGE_SIZE), 0, "munmap failed");

    // Demonstrate that we can map a read-only file as shared + readable
    fd = open(kFilename, O_RDONLY);
    ASSERT_GT(fd, 0);
    addr2 = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    ASSERT_NE(addr2, MAP_FAILED);
    ASSERT_EQ(memcmp(addr1, tmp3, sizeof(tmp3)), 0);
    ASSERT_EQ(memcmp(addr2, tmp3, sizeof(tmp3)), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(munmap(addr2, PAGE_SIZE), 0, "munmap failed");

    ASSERT_EQ(munmap(addr1, PAGE_SIZE), 0, "munmap failed");
    ASSERT_EQ(unlink(kFilename), 0);

    END_TEST;
}

// Test that MAP_PRIVATE keeps all copies of the buffer
// separate
bool test_mmap_private(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    constexpr char kFilename[] = "::mmap_private";
    int fd = open(kFilename, O_RDWR | O_CREAT | O_EXCL);
    ASSERT_GT(fd, 0);

    char buf[64];
    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(write(fd, buf, sizeof(buf)), sizeof(buf));

    // Demonstrate that a simple buffer can be mapped
    void* addr1 = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    ASSERT_NE(addr1, MAP_FAILED);
    ASSERT_EQ(memcmp(addr1, buf, sizeof(buf)), 0);
    // ... multiple times
    void* addr2 = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
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
    ASSERT_EQ(lseek(fd, SEEK_SET, 0), 0);
    ASSERT_EQ(read(fd, tmp, sizeof(tmp)), sizeof(tmp));
    ASSERT_EQ(memcmp(tmp, buf, sizeof(tmp)), 0);
    memset(buf, 'b', sizeof(buf));
    ASSERT_EQ(memcmp(addr1, buf, sizeof(buf)), 0);
    memset(buf, 'c', sizeof(buf));
    ASSERT_EQ(memcmp(addr2, buf, sizeof(buf)), 0);

    ASSERT_EQ(munmap(addr1, PAGE_SIZE), 0, "munmap failed");
    ASSERT_EQ(munmap(addr2, PAGE_SIZE), 0, "munmap failed");
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(unlink(kFilename), 0);

    END_TEST;
}

// Test that mmap fails with appropriate error codes when
// we expect.
bool test_mmap_evil(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    // Try (and fail) to mmap a directory
    ASSERT_EQ(mkdir("::mydir", 0666), 0);
    int fd = open("::mydir", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(rmdir("::mydir"), 0);

    fd = open("::myfile", O_RDWR | O_CREAT | O_EXCL);
    ASSERT_GT(fd, 0);

    // Mmap without MAP_PRIVATE or MAP_SHARED
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ, 0, fd, 0), MAP_FAILED);
    ASSERT_EQ(errno, EINVAL);
    errno = 0;
    // Mmap with both MAP_PRIVATE and MAP_SHARED
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED | MAP_PRIVATE, fd, 0), MAP_FAILED);
    ASSERT_EQ(errno, EINVAL);
    errno = 0;
    // Mmap with unaligned offset
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 1), MAP_FAILED);
    ASSERT_EQ(errno, EINVAL);
    errno = 0;
    // Mmap with a length of zero
    ASSERT_EQ(mmap(NULL, 0, PROT_READ, MAP_SHARED, fd, 0), MAP_FAILED);
    ASSERT_EQ(errno, EINVAL);
    errno = 0;
    ASSERT_EQ(close(fd), 0);
    // Test all cases of MAP_PRIVATE and MAP_SHARED which require
    // a readable file.
    fd = open("::myfile", O_WRONLY);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ, MAP_PRIVATE, fd, 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_WRITE, MAP_PRIVATE, fd, 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_WRITE, MAP_SHARED, fd, 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(close(fd), 0);
    // Test all cases of MAP_PRIVATE and MAP_SHARED which require a
    // writable file (notably, MAP_PRIVATE never requires a writable
    // file, since it makes a copy).
    fd = open("::myfile", O_RDONLY);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_WRITE, MAP_SHARED, fd, 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(close(fd), 0);
    // PROT_WRITE requires that the file is NOT append-only
    fd = open("::myfile", O_RDONLY | O_APPEND);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(mmap(NULL, PAGE_SIZE, PROT_WRITE, MAP_SHARED, fd, 0), MAP_FAILED);
    ASSERT_EQ(errno, EACCES);
    errno = 0;
    ASSERT_EQ(close(fd), 0);

    ASSERT_EQ(unlink("::myfile"), 0);
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
    int fd = open("::inaccessible", O_RDWR);
    ASSERT_GT(fd, 0);
    void* addr = mmap(NULL, PAGE_SIZE, prot, flags, fd, 0);
    ASSERT_NE(addr, MAP_FAILED);
    ASSERT_EQ(close(fd), 0);

    if (rw == RW::Read || rw == RW::ReadAfterUnmap) {
        // Read
        if (rw == RW::ReadAfterUnmap) {
            ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
        }

        ASSERT_DEATH([](void* addr) {
            __UNUSED volatile int i = *static_cast<int*>(addr);
        }, addr, "");

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
        }, addr, "");

        if (rw == RW::Write) {
            ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
        }
    }
    END_HELPER;
}

bool test_mmap_death(void) {
    BEGIN_TEST;
    if (!test_info->supports_mmap) {
        return true;
    }

    int fd = open("::inaccessible", O_RDWR | O_CREAT);
    ASSERT_GT(fd, 0);
    char tmp[] = "this is a temporary buffer";
    ASSERT_EQ(write(fd, tmp, sizeof(tmp)), sizeof(tmp));
    ASSERT_EQ(close(fd), 0);

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

RUN_FOR_ALL_FILESYSTEMS(fs_mmap_tests,
    RUN_TEST_MEDIUM(test_mmap_empty)
    RUN_TEST_MEDIUM(test_mmap_readable)
    RUN_TEST_MEDIUM(test_mmap_writable)
    RUN_TEST_MEDIUM(test_mmap_unlinked)
    RUN_TEST_MEDIUM(test_mmap_shared)
    RUN_TEST_MEDIUM(test_mmap_private)
    RUN_TEST_MEDIUM(test_mmap_evil)
    RUN_TEST_ENABLE_CRASH_HANDLER(test_mmap_death)
)
