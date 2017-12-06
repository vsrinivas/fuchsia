// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fvm/container.h>
#include <minfs/host.h>
#include <unittest/unittest.h>

#include <fvm/fvm-lz4.h>

#define PARTITION_SIZE (1lu << 30)  // 1 gb
#define CONTAINER_SIZE (4lu << 30)  // 4 gb
#define SLICE_SIZE (64lu * (1 << 20)) // 64 mb
#define FILE_SIZE (5lu * (1 << 20)) // 5 mb

static char test_dir[PATH_MAX];
static char data_path[PATH_MAX];
static char system_path[PATH_MAX];
static char blobfs_path[PATH_MAX];
static char sparse_path[PATH_MAX];
static char sparse_lz4_path[PATH_MAX];
static char fvm_path[PATH_MAX];

constexpr uint32_t kData      = 1;
constexpr uint32_t kSystem    = 2;
constexpr uint32_t kBlobfs    = 4;
constexpr uint32_t kSparse    = 8;
constexpr uint32_t kSparseLz4 = 16;
constexpr uint32_t kFvm       = 32;

// gFileFlags indicates which of the above files has been successfully created.
// Keeping track of these across each individual test allows us to unlink only files that actually
// exist at the end of the test. It also gives the test information about which partitions should
// be added to the container, so this doesn't have to be specified separately.
// Keeping track of these globally allows us to know with reasonable certainty whether files from
// any tests have been left over, for example in the case of a test failure. In this case we can
// optionally recover from the previous state by removing these files and continue with the next
// test.
static uint32_t gFileFlags = 0;

typedef enum {
    SPARSE,
    SPARSE_LZ4,
    FVM,
    FVM_NEW,
    FVM_OFFSET,
} container_t;

bool CreateFile(const char* path, size_t size, uint32_t type) {
    BEGIN_HELPER;
    int r = open(path, O_RDWR | O_CREAT | O_EXCL, 0755);
    ASSERT_GE(r, 0, "Unable to create path");
    gFileFlags |= type;
    ASSERT_EQ(ftruncate(r, size), 0, "Unable to truncate disk");
    ASSERT_EQ(close(r), 0, "Unable to close disk");
    END_HELPER;
}

bool CreateMinfs(const char* path, uint32_t type) {
    BEGIN_HELPER;
    printf("Creating Minfs partition: %s\n", path);
    ASSERT_TRUE(CreateFile(path, PARTITION_SIZE, type));
    ASSERT_EQ(emu_mkfs(path), 0, "Unable to run mkfs");
    END_HELPER;
}

bool CreateData() {
    BEGIN_HELPER;
    ASSERT_TRUE(CreateMinfs(data_path, kData));
    END_HELPER;
}

bool CreateSystem() {
    BEGIN_HELPER;
    ASSERT_TRUE(CreateMinfs(system_path, kSystem));
    END_HELPER;
}

bool CreateBlobstore() {
    BEGIN_HELPER;
    printf("Creating Blobstore partition: %s\n", blobfs_path);
    int r = open(blobfs_path, O_RDWR | O_CREAT | O_EXCL, 0755);
    ASSERT_GE(r, 0, "Unable to create path");
    gFileFlags |= kBlobfs;
    ASSERT_EQ(ftruncate(r, PARTITION_SIZE), 0, "Unable to truncate disk");
    uint64_t block_count;
    ASSERT_EQ(blobstore::blobstore_get_blockcount(r, &block_count), ZX_OK,
                 "Cannot find end of underlying device");
    ASSERT_EQ(blobstore::blobstore_mkfs(r, block_count), ZX_OK,
              "Failed to make blobstore partition");
    ASSERT_EQ(close(r), 0, "Unable to close disk\n");
    END_HELPER;
}

bool AddPartitions(Container* container) {
    BEGIN_HELPER;
    if (gFileFlags & kData) {
        printf("Adding data partition to container\n");
        ASSERT_EQ(container->AddPartition(data_path, "data"), ZX_OK,
                  "Failed to add data partition");
    }

    if (gFileFlags & kSystem) {
        printf("Adding system partition to container\n");
        ASSERT_EQ(container->AddPartition(system_path, "system"), ZX_OK,
                  "Failed to add system partition");
    }

    if (gFileFlags & kBlobfs) {
        printf("Adding blobstore partition to container\n");
        ASSERT_EQ(container->AddPartition(blobfs_path, "blobstore"), ZX_OK,
                  "Failed to add blobstore partition");

    }
    END_HELPER;
}


bool CreateSparse(compress_type_t compress) {
    BEGIN_HELPER;
    char* path = compress ? sparse_lz4_path : sparse_path;
    printf("Creating sparse container: %s\n", path);
    fbl::unique_ptr<SparseContainer> sparseContainer;
    ASSERT_EQ(SparseContainer::Create(path, SLICE_SIZE, compress, &sparseContainer), ZX_OK,
              "Failed to initialize sparse container");
    gFileFlags |= compress ? kSparseLz4 : kSparse;
    ASSERT_TRUE(AddPartitions(sparseContainer.get()));
    ASSERT_EQ(sparseContainer->Commit(), ZX_OK, "Failed to write to sparse file");
    END_HELPER;
}

bool StatFile(const char* path, off_t* length) {
    BEGIN_HELPER;
    fbl::unique_fd fd(open(path, O_RDWR, 0755));
    ASSERT_TRUE(fd, "Unable to open file");
    struct stat s;
    ASSERT_EQ(fstat(fd.get(), &s), 0, "Unable to stat file");
    *length = s.st_size;
    END_HELPER;
}

bool ReportContainer(const char* path, off_t offset) {
    fbl::unique_ptr<Container> container;
    off_t length;
    ASSERT_TRUE(StatFile(path, &length));
    ASSERT_EQ(Container::Create(path, offset, length - offset, &container), ZX_OK,
              "Failed to initialize container");
    ASSERT_EQ(container->Verify(), ZX_OK, "File check failed\n");
    return true;
}

bool ReportSparse(bool compress) {
    if (compress) {
        printf("Decompressing sparse file\n");
        if (fvm::decompress_sparse(sparse_lz4_path, sparse_path) != ZX_OK) {
            return false;
        }
        gFileFlags |= kSparse;
    }
    return ReportContainer(sparse_path, 0);
}

bool CreateFvm(bool create_before, off_t offset) {
    BEGIN_HELPER;
    printf("Creating fvm container: %s\n", fvm_path);

    off_t length = 0;
    if (create_before) {
        ASSERT_TRUE(CreateFile(fvm_path, CONTAINER_SIZE, kFvm));
        ASSERT_TRUE(StatFile(fvm_path, &length));
    }

    fbl::unique_ptr<FvmContainer> fvmContainer;
    ASSERT_EQ(FvmContainer::Create(fvm_path, SLICE_SIZE, offset, length - offset, &fvmContainer),
              ZX_OK, "Failed to initialize fvm container");
    if (!create_before) {
        gFileFlags |= kFvm;
    }
    ASSERT_TRUE(AddPartitions(fvmContainer.get()));
    ASSERT_EQ(fvmContainer->Commit(), ZX_OK, "Failed to write to fvm file");

    END_HELPER;
}

bool ReportFvm(off_t offset) {
    return ReportContainer(fvm_path, offset);
}

void GenerateFilename(const char* dir, size_t len, char* out) {
    char filename[len+1];

    for (unsigned i = 0; i < len; i++) {
        filename[i] = 'a' + rand() % 26;
    }

    filename[len] = 0;
    strcpy(out, dir);
    strcat(out, filename);
}

void GenerateDirectory(const char* dir, size_t len, char* out) {
    GenerateFilename(dir, len, out);
    strcat(out, "/");
}

bool GenerateData(size_t len, fbl::unique_ptr<uint8_t[]>* out) {
    BEGIN_HELPER;
    // Fill a test buffer with data
    fbl::AllocChecker ac;
    fbl::unique_ptr<uint8_t[]> data(new (&ac) uint8_t[len]);
    ASSERT_TRUE(ac.check());

    for (unsigned n = 0; n < len; n++) {
        data[n] = static_cast<uint8_t>(rand());
    }

    *out = fbl::move(data);
    END_HELPER;
}

bool AddDirectoryMinfs(char* path) {
    BEGIN_HELPER;
    ASSERT_EQ(emu_mkdir(path, 0755), 0);
    END_HELPER;
}

bool AddFileMinfs(char* path, size_t size) {
    BEGIN_HELPER;
    int fd = emu_open(path, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);
    fbl::unique_ptr<uint8_t[]> data;
    ASSERT_TRUE(GenerateData(size, &data));
    ASSERT_EQ(emu_write(fd, data.get(), size), size, "Failed to write data to file");
    ASSERT_EQ(emu_close(fd), 0);
    END_HELPER;
}

bool PopulateMinfs(const char* path, size_t ndirs, size_t nfiles, size_t max_size) {
    BEGIN_HELPER;
    ASSERT_EQ(emu_mount(path), 0, "Unable to run mount");
    fbl::Vector<fbl::String> paths;
    paths.push_back(fbl::String("::"));

    for (unsigned i = 0; i < ndirs; i++) {
        const char* base_dir = paths[rand() % paths.size()].data();
        char new_dir[PATH_MAX];
        GenerateDirectory(base_dir, 10, new_dir);
        AddDirectoryMinfs(new_dir);
        paths.push_back(fbl::String(new_dir));
    }

    for (unsigned i = 0; i < nfiles; i++) {
        const char* base_dir = paths[rand() % paths.size()].data();
        size_t size = 1 + (rand() % max_size);
        char new_file[PATH_MAX];
        GenerateFilename(base_dir, 10, new_file);
        AddFileMinfs(new_file, size);
    }
    END_HELPER;
}

bool PopulateData(size_t ndirs, size_t nfiles, size_t max_size) {
    printf("Populating data partition\n");
    return PopulateMinfs(data_path, ndirs, nfiles, max_size);
}

bool PopulateSystem(size_t ndirs, size_t nfiles, size_t max_size) {
    printf("Populating system partition\n");
    return PopulateMinfs(system_path, ndirs, nfiles, max_size);
}

bool AddFileBlobstore(blobstore::Blobstore* bs, size_t size) {
    BEGIN_HELPER;
    char new_file[PATH_MAX];
    GenerateFilename(test_dir, 10, new_file);;
    fbl::unique_fd datafd(open(new_file, O_RDWR | O_CREAT | O_EXCL, 0755));
    ASSERT_TRUE(datafd, "Unable to create new file");
    fbl::unique_ptr<uint8_t[]> data;
    ASSERT_TRUE(GenerateData(size, &data));
    ASSERT_EQ(write(datafd.get(), data.get(), size), size, "Failed to write data to file");
    ASSERT_EQ(blobstore::blobstore_add_blob(bs, datafd.get()), ZX_OK, "Failed to add blob");
    ASSERT_EQ(unlink(new_file), 0);
    END_HELPER;
}

bool PopulateBlobstore(size_t nfiles, size_t max_size) {
    BEGIN_HELPER;
    printf("Populating blobstore partition\n");
    fbl::unique_fd blobfd(open(blobfs_path, O_RDWR, 0755));
    ASSERT_TRUE(blobfd, "Unable to open blobstore path");
    fbl::RefPtr<blobstore::Blobstore> bs;
    ASSERT_EQ(blobstore::blobstore_create(&bs, fbl::move(blobfd)), ZX_OK,
              "Failed to create blobstore");
    for (unsigned i = 0; i < nfiles; i++) {
        size_t size = 1 + (rand() % max_size);
        ASSERT_TRUE(AddFileBlobstore(bs.get(), size));
    }
    END_HELPER;
}

bool PopulatePartitions(size_t ndirs, size_t nfiles, size_t max_size) {
    BEGIN_HELPER;
    printf("Populating blobstore partition\n");
    ASSERT_TRUE(PopulateData(ndirs, nfiles, max_size));
    ASSERT_TRUE(PopulateSystem(ndirs, nfiles, max_size));
    ASSERT_TRUE(PopulateBlobstore(nfiles, max_size));
    END_HELPER;
}

bool Destroy(const char* path, uint32_t type) {
    BEGIN_HELPER;
    printf("Destroying partition: %s\n", path);
    ASSERT_EQ(unlink(path), 0, "Failed to unlink path");
    gFileFlags &= ~type;
    END_HELPER;
}

bool DestroyAll() {
    BEGIN_HELPER;
    if (gFileFlags & kData) {
        ASSERT_TRUE(Destroy(data_path, kData));
    }

    if (gFileFlags & kSystem) {
        ASSERT_TRUE(Destroy(system_path, kSystem));
    }

    if (gFileFlags & kBlobfs) {
        ASSERT_TRUE(Destroy(blobfs_path, kBlobfs));
    }

    if (gFileFlags & kSparse) {
        ASSERT_TRUE(Destroy(sparse_path, kSparse));
    }

    if (gFileFlags & kSparseLz4) {
        ASSERT_TRUE(Destroy(sparse_lz4_path, kSparseLz4));
    }

    if (gFileFlags & kFvm) {
        ASSERT_TRUE(Destroy(fvm_path, kFvm));
    }

    ASSERT_FALSE(gFileFlags, "Failed to delete all partition files");
    END_HELPER;
}

bool CreatePartitions() {
    BEGIN_HELPER;
    ASSERT_TRUE(CreateData());
    ASSERT_TRUE(CreateSystem());
    ASSERT_TRUE(CreateBlobstore());
    END_HELPER;
}

bool CreateAndReport(container_t type) {
    BEGIN_HELPER;
    switch (type) {
        case SPARSE: {
            ASSERT_TRUE(CreateSparse(NONE));
            ASSERT_TRUE(ReportSparse(NONE));
            break;
        }
        case SPARSE_LZ4: {
            ASSERT_TRUE(CreateSparse(LZ4));
            ASSERT_TRUE(ReportSparse(LZ4));
            break;
        }
        case FVM: {
            ASSERT_TRUE(CreateFvm(true, 0));
            ASSERT_TRUE(ReportFvm(0));
            break;
        }
        case FVM_NEW: {
            ASSERT_TRUE(CreateFvm(false, 0));
            ASSERT_TRUE(ReportFvm(0));
            break;
        }
        case FVM_OFFSET: {
            ASSERT_TRUE(CreateFvm(true, SLICE_SIZE));
            ASSERT_TRUE(ReportFvm(SLICE_SIZE));
            break;
        }
        default: {
            ASSERT_TRUE(false);
        }
    }
    END_HELPER;
}

template <container_t ContainerType>
bool TestEmptyPartitions() {
    BEGIN_TEST;
    ASSERT_TRUE(CreatePartitions());
    ASSERT_TRUE(CreateAndReport(ContainerType));
    ASSERT_TRUE(DestroyAll());
    END_TEST;
}

template <container_t ContainerType, size_t NumDirs, size_t NumFiles, size_t MaxSize>
bool TestPartitions() {
    BEGIN_TEST;
    ASSERT_TRUE(CreatePartitions());
    ASSERT_TRUE(PopulatePartitions(NumDirs, NumFiles, MaxSize));
    ASSERT_TRUE(CreateAndReport(ContainerType));
    ASSERT_TRUE(DestroyAll());
    END_TEST;
}

bool Setup() {
    BEGIN_HELPER;
    srand(time(0));
    GenerateDirectory("/tmp/", 20, test_dir);
    ASSERT_EQ(mkdir(test_dir, 0755), 0, "Failed to create test path");
    printf("Created test path %s\n", test_dir);
    sprintf(data_path, "%sdata.bin", test_dir);
    sprintf(system_path, "%ssystem.bin", test_dir);
    sprintf(blobfs_path, "%sblobfs.bin", test_dir);
    sprintf(sparse_path, "%ssparse.bin", test_dir);
    sprintf(sparse_lz4_path, "%ssparse.bin.lz4", test_dir);
    sprintf(fvm_path, "%sfvm.bin", test_dir);
    END_HELPER;
}

bool Cleanup() {
    BEGIN_HELPER;
    DIR* dir = opendir(test_dir);
    if (!dir) {
        fprintf(stderr, "Couldn't open test directory\n");
        return -1;
    }

    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }

        printf("Destroying leftover file %s\n", de->d_name);
        ASSERT_EQ(unlinkat(dirfd(dir), de->d_name, 0), 0);
    }

    closedir(dir);
    printf("Destroying test path: %s\n", test_dir);
    ASSERT_EQ(rmdir(test_dir), 0, "Failed to remove test path");
    END_HELPER;
}

//TODO(planders): add tests for FVM on GPT (with offset)
BEGIN_TEST_CASE(fvm_host_tests)
RUN_TEST_MEDIUM(TestEmptyPartitions<SPARSE>)
RUN_TEST_MEDIUM(TestEmptyPartitions<SPARSE_LZ4>)
RUN_TEST_MEDIUM(TestEmptyPartitions<FVM>)
RUN_TEST_MEDIUM(TestEmptyPartitions<FVM_NEW>)
RUN_TEST_MEDIUM(TestEmptyPartitions<FVM_OFFSET>)
RUN_TEST_MEDIUM((TestPartitions<SPARSE, 10, 100, (1 << 20)>))
RUN_TEST_MEDIUM((TestPartitions<SPARSE_LZ4, 10, 100, (1 << 20)>))
RUN_TEST_MEDIUM((TestPartitions<FVM, 10, 100, (1 << 20)>))
RUN_TEST_MEDIUM((TestPartitions<FVM_NEW, 10, 100, (1 << 20)>))
RUN_TEST_MEDIUM((TestPartitions<FVM_OFFSET, 10, 100, (1 << 20)>))
END_TEST_CASE(fvm_host_tests)

int main(int argc, char** argv) {
    if (!Setup()) {
        return -1;
    }
    int result = unittest_run_all_tests(argc, argv) ? 0 : -1;
    if (!Cleanup()) {
        return -1;
    }
    return result;
}
