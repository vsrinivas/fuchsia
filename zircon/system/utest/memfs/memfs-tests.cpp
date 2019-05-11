// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>

#include <fbl/unique_ptr.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/memfs/memfs.h>
#include <unittest/unittest.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

namespace {

bool TestMemfsNull() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);
    memfs_filesystem_t* vfs;
    zx_handle_t root;

    ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
    ASSERT_EQ(zx_handle_close(root), ZX_OK);
    sync_completion_t unmounted;
    memfs_free_filesystem(vfs, &unmounted);
    ASSERT_EQ(sync_completion_wait(&unmounted, ZX_SEC(3)), ZX_OK);

    END_TEST;
}

bool TestMemfsBasic() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    // Create a memfs filesystem, acquire a file descriptor
    memfs_filesystem_t* vfs;
    zx_handle_t root;
    ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
    int fd;
    ASSERT_EQ(fdio_fd_create(root, &fd), ZX_OK);

    // Access files within the filesystem.
    DIR* d = fdopendir(fd);

    // Create a file
    const char* filename = "file-a";
    fd = openat(dirfd(d), filename, O_CREAT | O_RDWR);
    ASSERT_GE(fd, 0);
    const char* data = "hello";
    ssize_t datalen = strlen(data);
    ASSERT_EQ(write(fd, data, datalen), datalen);
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    char buf[32];
    ASSERT_EQ(read(fd, buf, sizeof(buf)), datalen);
    ASSERT_EQ(memcmp(buf, data, datalen), 0);

    // Readdir the file
    struct dirent* de;
    ASSERT_NONNULL((de = readdir(d)));
    ASSERT_EQ(strcmp(de->d_name, "."), 0);
    ASSERT_NONNULL((de = readdir(d)));
    ASSERT_EQ(strcmp(de->d_name, filename), 0);
    ASSERT_NULL(readdir(d));

    ASSERT_EQ(closedir(d), 0);
    sync_completion_t unmounted;
    memfs_free_filesystem(vfs, &unmounted);
    ASSERT_EQ(sync_completion_wait(&unmounted, ZX_SEC(3)), ZX_OK);

    END_TEST;
}

bool TestMemfsLimitPages() {
    BEGIN_TEST;

    constexpr ssize_t kPageSize = static_cast<ssize_t>(PAGE_SIZE);
    fbl::Vector<ssize_t> page_limits = {1, 2, 5, 50};
    for (const auto& page_limit : page_limits) {
        async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
        ASSERT_EQ(loop.StartThread(), ZX_OK);

        // Create a memfs filesystem, acquire a file descriptor
        memfs_filesystem_t* vfs;
        zx_handle_t root;
        ASSERT_EQ(memfs_create_filesystem_with_page_limit(loop.dispatcher(),
                                                          static_cast<size_t>(page_limit),
                                                          &vfs, &root), ZX_OK);
        int raw_root_fd = -1;
        ASSERT_EQ(fdio_fd_create(root, &raw_root_fd), ZX_OK);
        fbl::unique_fd root_fd(raw_root_fd);

        // Access files within the filesystem.
        DIR* d = fdopendir(root_fd.get());

        // Create a file
        const char* filename = "file-a";
        fbl::unique_fd fd = fbl::unique_fd(openat(dirfd(d), filename, O_CREAT | O_RDWR));
        ASSERT_GE(fd.get(), 0);

        auto data = fbl::unique_ptr<uint8_t[]>(new uint8_t[page_limit * kPageSize + 1]);
        auto data_back = fbl::unique_ptr<uint8_t[]>(new uint8_t[page_limit * kPageSize + 1]);
        for (ssize_t i = 0; i < page_limit * kPageSize + 1; i++) {
            data[i] = static_cast<uint8_t>(i % 100);
            data_back[i] = 0;
        }

        // 1. Write some data which is exactly kPageSize * page_limit bytes long
        ASSERT_EQ(write(fd.get(), &data[0], kPageSize * page_limit), kPageSize * page_limit);
        ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
        ASSERT_EQ(read(fd.get(), &data_back[0], kPageSize * page_limit), kPageSize * page_limit);
        ASSERT_EQ(memcmp(&data[0], &data_back[0], kPageSize * page_limit), 0);

        // 2. Try to write to a second file. This should fail since the first file has already
        // taken up all the available pages.
        const char* filename_another = "file-b";
        int fd_another = openat(dirfd(d), filename_another, O_CREAT | O_RDWR);
        ASSERT_GE(fd_another, 0);
        ASSERT_EQ(write(fd_another, &data[0], 1), -1);
        ASSERT_EQ(errno, ENOSPC);
        ASSERT_EQ(unlinkat(dirfd(d), filename_another, 0), 0);
        ASSERT_EQ(close(fd_another), 0);

        // 3. Overwriting the file should succeed because it does not result in extra allocations.
        ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
        ASSERT_EQ(write(fd.get(), &data[0], kPageSize * page_limit), kPageSize * page_limit);

        // 4. Write some data which is exactly (kPageSize * page_limit + 1) bytes long.
        // This should fail.
        ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
        errno = 0;
        ASSERT_EQ(write(fd.get(), &data[0], kPageSize * page_limit + 1), -1);
        ASSERT_EQ(errno, ENOSPC);

        // 5. Write kPageSize * page_limit data then unlink&close&open the file, repeat a few times.
        for (size_t i = 0; i < 3; i++) {
            ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
            errno = 0;
            ASSERT_EQ(write(fd.get(), &data[0], kPageSize * page_limit), kPageSize * page_limit);
            ASSERT_EQ(errno, 0);
            ASSERT_EQ(unlinkat(dirfd(d), "file-a", 0), 0);
            fd = fbl::unique_fd(openat(dirfd(d), filename, O_CREAT | O_RDWR));
            ASSERT_GE(fd.get(), 0);
        }

        // Teardown
        ASSERT_EQ(closedir(d), 0);
        sync_completion_t unmounted;
        memfs_free_filesystem(vfs, &unmounted);
        ASSERT_EQ(sync_completion_wait(&unmounted, ZX_SEC(3)), ZX_OK);
    }

    END_TEST;
}

bool TestMemfsInstall() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/mytmp"), ZX_OK);
    int fd = open("/mytmp", O_DIRECTORY | O_RDONLY);
    ASSERT_GE(fd, 0);

    // Access files within the filesystem.
    DIR* d = fdopendir(fd);

    // Create a file
    const char* filename = "file-a";
    fd = openat(dirfd(d), filename, O_CREAT | O_RDWR);
    ASSERT_GE(fd, 0);
    const char* data = "hello";
    ssize_t datalen = strlen(data);
    ASSERT_EQ(write(fd, data, datalen), datalen);
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    char buf[32];
    ASSERT_EQ(read(fd, buf, sizeof(buf)), datalen);
    ASSERT_EQ(memcmp(buf, data, datalen), 0);

    // Readdir the file
    struct dirent* de;
    ASSERT_NONNULL((de = readdir(d)));
    ASSERT_EQ(strcmp(de->d_name, "."), 0);
    ASSERT_NONNULL((de = readdir(d)));
    ASSERT_EQ(strcmp(de->d_name, filename), 0);
    ASSERT_NULL(readdir(d));

    ASSERT_EQ(closedir(d), 0);

    ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/mytmp"), ZX_ERR_ALREADY_EXISTS);

    loop.Shutdown();

    // No way to clean up the namespace entry. See ZX-2013 for more details.

    END_TEST;
}

bool TestMemfsCloseDuringAccess() {
    BEGIN_TEST;

    for (int i = 0; i < 100; i++) {
        async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
        ASSERT_EQ(loop.StartThread(), ZX_OK);

        // Create a memfs filesystem, acquire a file descriptor
        memfs_filesystem_t* vfs;
        zx_handle_t root;
        ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
        int fd = -1;
        ASSERT_EQ(fdio_fd_create(root, &fd), ZX_OK);

        // Access files within the filesystem.
        DIR* d = fdopendir(fd);
        ASSERT_NONNULL(d);
        thrd_t worker;

        struct thread_args {
            DIR* d;
            sync_completion_t spinning{};
        } args{
            .d = d,
        };

        ASSERT_EQ(thrd_create(&worker, [](void* arg) {
            thread_args* args = reinterpret_cast<thread_args*>(arg);
            DIR* d = args->d;
            int fd = openat(dirfd(d), "foo", O_CREAT | O_RDWR);
            while (true) {
                if (close(fd)) {
                    return errno == EPIPE ? 0 : -1;
                }

                if ((fd = openat(dirfd(d), "foo", O_RDWR)) < 0) {
                    return errno == EPIPE ? 0 : -1;
                }
                sync_completion_signal(&args->spinning);
            }
        }, &args), thrd_success);

        ASSERT_EQ(sync_completion_wait(&args.spinning, ZX_SEC(3)), ZX_OK);

        sync_completion_t unmounted;
        memfs_free_filesystem(vfs, &unmounted);
        ASSERT_EQ(sync_completion_wait(&unmounted, ZX_SEC(3)), ZX_OK);

        int result;
        ASSERT_EQ(thrd_join(worker, &result), thrd_success);
        ASSERT_EQ(result, 0);

        // Now that the filesystem has terminated, we should be
        // unable to access it.
        ASSERT_LT(openat(dirfd(d), "foo", O_CREAT | O_RDWR), 0);
        ASSERT_EQ(errno, EPIPE, "Expected connection to remote server to be closed");

        // Since the filesystem has terminated, this will
        // only close the client side of the connection.
        ASSERT_EQ(closedir(d), 0);
    }

    END_TEST;
}

bool TestMemfsOverflow() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    // Create a memfs filesystem, acquire a file descriptor
    memfs_filesystem_t* vfs;
    zx_handle_t root;
    ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
    int root_fd;
    ASSERT_EQ(fdio_fd_create(root, &root_fd), ZX_OK);

    // Access files within the filesystem.
    DIR* d = fdopendir(root_fd);
    ASSERT_NONNULL(d);

    // Issue writes to the file in an order that previously would have triggered
    // an overflow in the memfs write path.
    //
    // Values provided mimic the bug reported by syzkaller (ZX-3791).
    uint8_t buf[4096];
    memset(buf, 'a', sizeof(buf));
    fbl::unique_fd fd(openat(dirfd(d), "file", O_CREAT | O_RDWR));
    ASSERT_TRUE(fd);
    ASSERT_EQ(pwrite(fd.get(), buf, 199, 0), 199);
    ASSERT_EQ(pwrite(fd.get(), buf, 226, 0xfffffffffffff801), -1);
    ASSERT_EQ(errno, EFBIG);

    ASSERT_EQ(closedir(d), 0);
    sync_completion_t unmounted;
    memfs_free_filesystem(vfs, &unmounted);
    ASSERT_EQ(sync_completion_wait(&unmounted, ZX_SEC(3)), ZX_OK);
    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(memfs_tests)
RUN_TEST(TestMemfsNull)
RUN_TEST(TestMemfsBasic)
RUN_TEST(TestMemfsLimitPages)
RUN_TEST(TestMemfsInstall)
RUN_TEST(TestMemfsCloseDuringAccess)
RUN_TEST(TestMemfsOverflow)
END_TEST_CASE(memfs_tests)
