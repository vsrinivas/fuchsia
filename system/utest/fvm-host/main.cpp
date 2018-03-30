// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fvm/container.h>
#include <minfs/host.h>
#include <unittest/unittest.h>

#include <fvm/fvm-lz4.h>

#define DEFAULT_SLICE_SIZE (64lu * (1 << 20)) // 64 mb
#define PARTITION_SIZE     (1lu * (1 << 29))  // 512 mb
#define CONTAINER_SIZE     (4lu * (1 << 30))  // 4 gb

#define MAX_PARTITIONS 5

static char test_dir[PATH_MAX];
static char sparse_path[PATH_MAX];
static char sparse_lz4_path[PATH_MAX];
static char fvm_path[PATH_MAX];

static constexpr char kEmptyString[] = "";

typedef enum {
    MINFS,
    BLOBFS,
} fs_type_t;

typedef enum {
    DATA,
    SYSTEM,
    BLOBSTORE,
    DEFAULT,
} guid_type_t;

typedef enum {
    SPARSE,     // Sparse container
    SPARSE_LZ4, // Sparse container compressed with LZ4
    FVM,        // Explicitly created FVM container
    FVM_NEW,    // FVM container created on FvmContainer::Create
    FVM_OFFSET, // FVM container created at an offset within a file
} container_t;

typedef struct {
    fs_type_t fs_type;
    guid_type_t guid_type;
    char path[PATH_MAX];
    bool created = false;

    const char* FsTypeName() {
        switch (fs_type) {
        case MINFS:
            return kMinfsName;
        case BLOBFS:
            return kBlobfsName;
        default:
            return kEmptyString;
        }
    }

    const char* GuidTypeName() {
        switch (guid_type) {
        case DATA:
            return kDataTypeName;
        case SYSTEM:
            return kSystemTypeName;
        case BLOBSTORE:
            return kBlobTypeName;
        case DEFAULT:
            return kDefaultTypeName;
        default:
            return kEmptyString;
        }
    }

    void GeneratePath(char* dir) {
        sprintf(path, "%s%s_%s.bin", dir, FsTypeName(), GuidTypeName());
    }
} partition_t;

static partition_t partitions[MAX_PARTITIONS];
static uint32_t partition_count;

bool CreateFile(const char* path, size_t size) {
    BEGIN_HELPER;
    int r = open(path, O_RDWR | O_CREAT | O_EXCL, 0755);
    ASSERT_GE(r, 0, "Unable to create path");
    ASSERT_EQ(ftruncate(r, size), 0, "Unable to truncate disk");
    ASSERT_EQ(close(r), 0, "Unable to close disk");
    END_HELPER;
}

bool CreateMinfs(const char* path) {
    BEGIN_HELPER;
    unittest_printf("Creating Minfs partition: %s\n", path);
    ASSERT_TRUE(CreateFile(path, PARTITION_SIZE));
    ASSERT_EQ(emu_mkfs(path), 0, "Unable to run mkfs");
    END_HELPER;
}


bool CreateBlobfs(const char* path) {
    BEGIN_HELPER;
    unittest_printf("Creating Blobfs partition: %s\n", path);
    int r = open(path, O_RDWR | O_CREAT | O_EXCL, 0755);
    ASSERT_GE(r, 0, "Unable to create path");
    ASSERT_EQ(ftruncate(r, PARTITION_SIZE), 0, "Unable to truncate disk");
    uint64_t block_count;
    ASSERT_EQ(blobfs::blobfs_get_blockcount(r, &block_count), ZX_OK,
              "Cannot find end of underlying device");
    ASSERT_EQ(blobfs::blobfs_mkfs(r, block_count), ZX_OK,
              "Failed to make blobfs partition");
    ASSERT_EQ(close(r), 0, "Unable to close disk\n");
    END_HELPER;
}

bool AddPartitions(Container* container) {
    BEGIN_HELPER;

    // Randomize order in which partitions are added to container.
    uint32_t order[partition_count];

    for (unsigned i = 0; i < partition_count; i++) {
        order[i] = i;
    }

    uint32_t remaining = partition_count;
    while (remaining) {
        unsigned index = rand() % remaining;

        if (index != remaining - 1) {
            unsigned temp = order[remaining - 1];
            order[remaining - 1] = order[index];
            order[index] = temp;
        }

        remaining--;
    }

    for (unsigned i = 0; i < partition_count; i++) {
        partition_t* part = &partitions[order[i]];
        if (part->created) {
            unittest_printf("Adding partition to container: %s\n", part->path);
            ASSERT_EQ(container->AddPartition(part->path, part->GuidTypeName()), ZX_OK,
                      "Failed to add partition");
        }
    }

    END_HELPER;
}

bool CreateSparse(compress_type_t compress, size_t slice_size) {
    BEGIN_HELPER;
    const char* path = compress ? sparse_lz4_path : sparse_path;
    unittest_printf("Creating sparse container: %s\n", path);
    fbl::unique_ptr<SparseContainer> sparseContainer;
    ASSERT_EQ(SparseContainer::Create(path, slice_size, compress, &sparseContainer), ZX_OK,
              "Failed to initialize sparse container");
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
        unittest_printf("Decompressing sparse file\n");
        if (fvm::decompress_sparse(sparse_lz4_path, sparse_path) != ZX_OK) {
            return false;
        }
    }
    return ReportContainer(sparse_path, 0);
}

bool CreateFvm(bool create_before, off_t offset, size_t slice_size) {
    BEGIN_HELPER;
    unittest_printf("Creating fvm container: %s\n", fvm_path);

    off_t length = 0;
    if (create_before) {
        ASSERT_TRUE(CreateFile(fvm_path, CONTAINER_SIZE));
        ASSERT_TRUE(StatFile(fvm_path, &length));
    }

    fbl::unique_ptr<FvmContainer> fvmContainer;
    ASSERT_EQ(FvmContainer::Create(fvm_path, slice_size, offset, length - offset, &fvmContainer),
              ZX_OK, "Failed to initialize fvm container");
    ASSERT_TRUE(AddPartitions(fvmContainer.get()));
    ASSERT_EQ(fvmContainer->Commit(), ZX_OK, "Failed to write to fvm file");

    END_HELPER;
}

bool ExtendFvm(off_t length) {
    BEGIN_HELPER;
    off_t current_length;
    ASSERT_TRUE(StatFile(fvm_path, &current_length));
    fbl::unique_ptr<FvmContainer> fvmContainer;
    ASSERT_EQ(FvmContainer::Create(fvm_path, DEFAULT_SLICE_SIZE, 0, current_length, &fvmContainer),
              ZX_OK, "Failed to initialize fvm container");
    ASSERT_EQ(fvmContainer->Extend(length), ZX_OK, "Failed to write to fvm file");
    ASSERT_TRUE(StatFile(fvm_path, &current_length));
    ASSERT_EQ(current_length, length);
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

bool AddDirectoryMinfs(const char* path) {
    BEGIN_HELPER;
    ASSERT_EQ(emu_mkdir(path, 0755), 0);
    END_HELPER;
}

bool AddFileMinfs(const char* path, size_t size) {
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

bool AddFileBlobfs(blobfs::Blobfs* bs, size_t size) {
    BEGIN_HELPER;
    char new_file[PATH_MAX];
    GenerateFilename(test_dir, 10, new_file);;
    fbl::unique_fd datafd(open(new_file, O_RDWR | O_CREAT | O_EXCL, 0755));
    ASSERT_TRUE(datafd, "Unable to create new file");
    fbl::unique_ptr<uint8_t[]> data;
    ASSERT_TRUE(GenerateData(size, &data));
    ASSERT_EQ(write(datafd.get(), data.get(), size), size, "Failed to write data to file");
    ASSERT_EQ(blobfs::blobfs_add_blob(bs, datafd.get()), ZX_OK, "Failed to add blob");
    ASSERT_EQ(unlink(new_file), 0);
    END_HELPER;
}

bool PopulateBlobfs(const char* path, size_t nfiles, size_t max_size) {
    BEGIN_HELPER;
    fbl::unique_fd blobfd(open(path, O_RDWR, 0755));
    ASSERT_TRUE(blobfd, "Unable to open blobfs path");
    fbl::RefPtr<blobfs::Blobfs> bs;
    ASSERT_EQ(blobfs::blobfs_create(&bs, fbl::move(blobfd)), ZX_OK,
              "Failed to create blobfs");
    for (unsigned i = 0; i < nfiles; i++) {
        size_t size = 1 + (rand() % max_size);
        ASSERT_TRUE(AddFileBlobfs(bs.get(), size));
    }
    END_HELPER;
}

bool PopulatePartitions(size_t ndirs, size_t nfiles, size_t max_size) {
    BEGIN_HELPER;

    for (unsigned i = 0; i < partition_count; i++) {
        partition_t* part = &partitions[i];
        unittest_printf("Populating partition: %s\n", part->path);

        if (!part->created) {
            continue;
        }

        switch (part->fs_type) {
        case MINFS:
            ASSERT_TRUE(PopulateMinfs(part->path, ndirs, nfiles, max_size));
            break;
        case BLOBFS:
            ASSERT_TRUE(PopulateBlobfs(part->path, nfiles, max_size));
            break;
        default:
            fprintf(stderr, "Unknown file system type");
            ASSERT_TRUE(false);
        }
    }

    END_HELPER;
}

bool DestroySparse(compress_type_t compress) {
    BEGIN_HELPER;
    switch (compress) {
    case LZ4:
        unittest_printf("Destroying compressed sparse container: %s\n", sparse_lz4_path);
        ASSERT_EQ(unlink(sparse_lz4_path), 0, "Failed to unlink path");
    case NONE:
    default:
        unittest_printf("Destroying sparse container: %s\n", sparse_path);
        ASSERT_EQ(unlink(sparse_path), 0, "Failed to unlink path");
    }
    END_HELPER;
}

bool DestroyFvm() {
    BEGIN_HELPER;
    unittest_printf("Destroying fvm container: %s\n", fvm_path);
    ASSERT_EQ(unlink(fvm_path), 0, "Failed to unlink path");
    END_HELPER;
}

bool DestroyPartitions() {
    BEGIN_HELPER;

    for (unsigned i = 0; i < partition_count; i++) {
        partition_t* part = &partitions[i];
        if (part->created) {
            unittest_printf("Destroying partition: %s\n", part->path);
            ASSERT_EQ(unlink(part->path), 0, "Failed to unlink path");
            part->created = false;
        }
    }

    END_HELPER;
}

bool CreatePartitions() {
    BEGIN_HELPER;

    for (unsigned i = 0; i < partition_count; i++) {
        partition_t* part = &partitions[i];
        unittest_printf("Creating partition %s\n", part->path);

        switch (part->fs_type) {
        case MINFS:
            ASSERT_TRUE(CreateMinfs(part->path));
            break;
        case BLOBFS:
            ASSERT_TRUE(CreateBlobfs(part->path));
            break;
        default:
            fprintf(stderr, "Unknown file system type\n");
            ASSERT_TRUE(false);
        }

        part->created = true;
    }

    END_HELPER;
}

bool CreateReportDestroy(container_t type, size_t slice_size) {
    BEGIN_HELPER;
    switch (type) {
    case SPARSE: {
        ASSERT_TRUE(CreateSparse(NONE, slice_size));
        ASSERT_TRUE(ReportSparse(NONE));
        ASSERT_TRUE(DestroySparse(NONE));
        break;
    }
    case SPARSE_LZ4: {
        ASSERT_TRUE(CreateSparse(LZ4, slice_size));
        ASSERT_TRUE(ReportSparse(LZ4));
        ASSERT_TRUE(DestroySparse(LZ4));
        break;
    }
    case FVM: {
        ASSERT_TRUE(CreateFvm(true, 0, slice_size));
        ASSERT_TRUE(ReportFvm(0));
        ASSERT_TRUE(ExtendFvm(CONTAINER_SIZE * 2));
        ASSERT_TRUE(ReportFvm(0));
        ASSERT_TRUE(DestroyFvm());
        break;
    }
    case FVM_NEW: {
        ASSERT_TRUE(CreateFvm(false, 0, slice_size));
        ASSERT_TRUE(ReportFvm(0));
        ASSERT_TRUE(ExtendFvm(CONTAINER_SIZE * 2));
        ASSERT_TRUE(ReportFvm(0));
        ASSERT_TRUE(DestroyFvm());
        break;
    }
    case FVM_OFFSET: {
        ASSERT_TRUE(CreateFvm(true, DEFAULT_SLICE_SIZE, slice_size));
        ASSERT_TRUE(ReportFvm(DEFAULT_SLICE_SIZE));
        ASSERT_TRUE(DestroyFvm());
        break;
    }
    default: {
        ASSERT_TRUE(false);
    }
    }
    END_HELPER;
}

template <container_t ContainerType, size_t SliceSize>
bool TestEmptyPartitions() {
    BEGIN_TEST;
    ASSERT_TRUE(CreatePartitions());
    ASSERT_TRUE(CreateReportDestroy(ContainerType, SliceSize));
    ASSERT_TRUE(DestroyPartitions());
    END_TEST;
}

template <container_t ContainerType, size_t NumDirs, size_t NumFiles, size_t MaxSize,
          size_t SliceSize>
bool TestPartitions() {
    BEGIN_TEST;
    ASSERT_TRUE(CreatePartitions());
    ASSERT_TRUE(PopulatePartitions(NumDirs, NumFiles, MaxSize));
    ASSERT_TRUE(CreateReportDestroy(ContainerType, SliceSize));
    ASSERT_TRUE(DestroyPartitions());
    END_TEST;
}

bool GeneratePartitionPath(fs_type_t fs_type, guid_type_t guid_type) {
    BEGIN_HELPER;
    // Make sure we have not already created a partition with the same fs/guid type combo.
    for (unsigned i = 0; i < partition_count; i++) {
        partition_t* part = &partitions[i];
        if (part->fs_type == fs_type && part->guid_type == guid_type) {
            fprintf(stderr, "Partition %s already exists!\n", part->path);
            ASSERT_TRUE(false);
        }
    }

    partition_t* part = &partitions[partition_count++];
    part->fs_type = fs_type;
    part->guid_type = guid_type;
    part->GeneratePath(test_dir);
    unittest_printf("Generated partition path %s\n", part->path);
    END_HELPER;
}

bool Setup() {
    BEGIN_HELPER;
    // Generate test directory
    srand(time(0));
    GenerateDirectory("/tmp/", 20, test_dir);
    ASSERT_EQ(mkdir(test_dir, 0755), 0, "Failed to create test path");
    unittest_printf("Created test path %s\n", test_dir);

    // Generate partition paths
    partition_count = 0;
    ASSERT_TRUE(GeneratePartitionPath(MINFS, DATA));
    ASSERT_TRUE(GeneratePartitionPath(MINFS, SYSTEM));
    ASSERT_TRUE(GeneratePartitionPath(MINFS, DEFAULT));
    ASSERT_TRUE(GeneratePartitionPath(BLOBFS, BLOBSTORE));
    ASSERT_TRUE(GeneratePartitionPath(BLOBFS, DEFAULT));

    // Generate container paths
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

        unittest_printf("Destroying leftover file %s\n", de->d_name);
        ASSERT_EQ(unlinkat(dirfd(dir), de->d_name, 0), 0);
    }

    closedir(dir);
    unittest_printf("Destroying test path: %s\n", test_dir);
    ASSERT_EQ(rmdir(test_dir), 0, "Failed to remove test path");
    END_HELPER;
}

#define RUN_FOR_ALL_TYPES_EMPTY(slice_size) \
    RUN_TEST_MEDIUM((TestEmptyPartitions<SPARSE, slice_size>)) \
    RUN_TEST_MEDIUM((TestEmptyPartitions<SPARSE_LZ4, slice_size>)) \
    RUN_TEST_MEDIUM((TestEmptyPartitions<FVM, slice_size>)) \
    RUN_TEST_MEDIUM((TestEmptyPartitions<FVM_NEW, slice_size>)) \
    RUN_TEST_MEDIUM((TestEmptyPartitions<FVM_OFFSET, slice_size>))

#define RUN_FOR_ALL_TYPES(num_dirs, num_files, max_size, slice_size) \
    RUN_TEST_MEDIUM((TestPartitions<SPARSE, num_dirs, num_files, max_size, slice_size>)) \
    RUN_TEST_MEDIUM((TestPartitions<SPARSE_LZ4, num_dirs, num_files, max_size, slice_size>)) \
    RUN_TEST_MEDIUM((TestPartitions<FVM, num_dirs, num_files, max_size, slice_size>)) \
    RUN_TEST_MEDIUM((TestPartitions<FVM_NEW, num_dirs, num_files, max_size, slice_size>)) \
    RUN_TEST_MEDIUM((TestPartitions<FVM_OFFSET, num_dirs, num_files, max_size, slice_size>))

//TODO(planders): add tests for FVM on GPT (with offset)
BEGIN_TEST_CASE(fvm_host_tests)
RUN_FOR_ALL_TYPES_EMPTY(8192)
RUN_FOR_ALL_TYPES_EMPTY(32768)
RUN_FOR_ALL_TYPES_EMPTY(DEFAULT_SLICE_SIZE)
RUN_FOR_ALL_TYPES(10, 100, (1 << 20), 8192)
RUN_FOR_ALL_TYPES(10, 100, (1 << 20), 32768)
RUN_FOR_ALL_TYPES(10, 100, (1 << 20), DEFAULT_SLICE_SIZE)
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
