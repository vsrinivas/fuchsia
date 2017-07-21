// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <unittest/unittest.h>

#define GOOD_SYMBOL     "mx_syscall_test_0"
#define BAD_SYMBOL      "mx_syscall_test_1"

bool vdso_open_test(void) {
    BEGIN_TEST;

    int vdso_dir_fd = open("/boot/kernel/vdso", O_RDONLY | O_DIRECTORY);
    ASSERT_GE(vdso_dir_fd, 0, "open of vdso directory failed");

    DIR* dir = fdopendir(dup(vdso_dir_fd));
    ASSERT_NONNULL(dir, "fdopendir failed");

    const struct dirent* d;
    int vdso_files_found = 0;
    while ((d = readdir(dir)) != NULL) {
        if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
            continue;

        ++vdso_files_found;
        // Test that we can open for read.
        int fd = openat(vdso_dir_fd, d->d_name, O_RDONLY);
        EXPECT_GE(fd, 0, d->d_name);
        EXPECT_EQ(close(fd), 0, "");

        // Test that we cannot open for write.
        EXPECT_EQ(openat(vdso_dir_fd, d->d_name, O_RDWR), -1,
                  "opening vDSO file for writing");
        EXPECT_EQ(errno, EACCES, "opening vDSO file for writing");
    }

    EXPECT_GT(vdso_files_found, 1, "didn't find vDSO files");

    EXPECT_EQ(closedir(dir), 0, "");
    EXPECT_EQ(close(vdso_dir_fd), 0, "");

    END_TEST;
}

bool vdso_missing_test_syscall1_test(void) {
    BEGIN_TEST;

    void* dso = dlopen("libmagenta.so", RTLD_LOCAL | RTLD_NOLOAD);
    ASSERT_NONNULL(dso, dlerror());

    EXPECT_NONNULL(dlsym(dso, GOOD_SYMBOL), dlerror());

    EXPECT_NULL(dlsym(dso, BAD_SYMBOL), BAD_SYMBOL " symbol found in vDSO");

    EXPECT_EQ(dlclose(dso), 0, "");

    END_TEST;
}

BEGIN_TEST_CASE(vdso_variant_tests)
RUN_TEST(vdso_open_test)
RUN_TEST(vdso_missing_test_syscall1_test)
END_TEST_CASE(vdso_variant_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
