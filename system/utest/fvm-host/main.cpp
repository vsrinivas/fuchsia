// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/lz4.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fvm/container.h>
#include <minfs/host.h>
#include <unittest/unittest.h>

#include <fvm/fvm-lz4.h>

#include <utility>

#define DEFAULT_SLICE_SIZE (64lu * (1 << 20)) // 64 mb
#define PARTITION_SIZE     (1lu * (1 << 29))  // 512 mb
#define CONTAINER_SIZE     (6lu * (1 << 30))  // 6 gb

#define MAX_PARTITIONS 6

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
    DATA_UNSAFE,
    SYSTEM,
    BLOBSTORE,
    DEFAULT,
} guid_type_t;

typedef enum {
    SPARSE,         // Sparse container
    SPARSE_LZ4,     // Sparse container compressed with LZ4
    SPARSE_ZXCRYPT, // Sparse container to be stored on a zxcrypt volume
    FVM,            // Explicitly created FVM container
    FVM_NEW,        // FVM container created on FvmContainer::Create
    FVM_OFFSET,     // FVM container created at an offset within a file
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
        case DATA_UNSAFE:
            return kDataUnsafeTypeName;
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
    ASSERT_EQ(blobfs::GetBlockCount(r, &block_count), ZX_OK,
              "Cannot find end of underlying device");
    ASSERT_EQ(blobfs::Mkfs(r, block_count), ZX_OK,
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

bool CreateSparse(uint32_t flags, size_t slice_size) {
    BEGIN_HELPER;
    const char* path = ((flags & fvm::kSparseFlagLz4) != 0) ? sparse_lz4_path : sparse_path;
    unittest_printf("Creating sparse container: %s\n", path);
    fbl::unique_ptr<SparseContainer> sparseContainer;
    ASSERT_EQ(SparseContainer::Create(path, slice_size, flags, &sparseContainer), ZX_OK,
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
    BEGIN_HELPER;
    fbl::unique_ptr<Container> container;
    off_t length;
    ASSERT_TRUE(StatFile(path, &length));
    ASSERT_EQ(Container::Create(path, offset, length - offset, 0, &container), ZX_OK,
              "Failed to initialize container");
    ASSERT_EQ(container->Verify(), ZX_OK, "File check failed\n");
    END_HELPER;
}

bool ReportSparse(uint32_t flags) {
    BEGIN_HELPER;
    if ((flags & fvm::kSparseFlagLz4) != 0) {
        unittest_printf("Decompressing sparse file\n");
        SparseContainer compressedContainer(sparse_lz4_path, DEFAULT_SLICE_SIZE, flags);
        ASSERT_EQ(compressedContainer.Decompress(sparse_path), ZX_OK);
    }

    ASSERT_TRUE(ReportContainer(sparse_path, 0));

    // Check that the calculated disk size passes inspection, but any size lower doesn't.
    SparseContainer container(sparse_path, 0, 0);
    size_t expected_size = container.CalculateDiskSize();
    ASSERT_EQ(container.CheckDiskSize(expected_size), ZX_OK);
    ASSERT_NE(container.CheckDiskSize(expected_size - 1), ZX_OK);
    END_HELPER;
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

bool ReportFvm(off_t offset = 0) {
    BEGIN_HELPER;
    ASSERT_TRUE(ReportContainer(fvm_path, offset));
    END_HELPER;
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

    *out = std::move(data);
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
    fbl::unique_ptr<blobfs::Blobfs> bs;
    ASSERT_EQ(blobfs::blobfs_create(&bs, std::move(blobfd)), ZX_OK,
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

bool DestroySparse(uint32_t flags) {
    BEGIN_HELPER;
    if ((flags & fvm::kSparseFlagLz4) != 0) {
        unittest_printf("Destroying compressed sparse container: %s\n", sparse_lz4_path);
        ASSERT_EQ(unlink(sparse_lz4_path), 0, "Failed to unlink path");
    } else {
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

// Creates all partitions defined in Setup(). If enable_data is false, the DATA partition is
// skipped. This is to avoid discrepancies in disk size calculation due to zxcrypt not being
// implemented on host.
// TODO(planders): Once we are able to create zxcrypt'd FVM images on host, remove enable_data flag.
bool CreatePartitions(bool enable_data = true) {
    BEGIN_HELPER;

    for (unsigned i = 0; i < partition_count; i++) {
        partition_t* part = &partitions[i];

        if (!enable_data && !strcmp(part->GuidTypeName(), kDataTypeName)) {
            unittest_printf("Skipping creation of partition %s\n", part->path);
            continue;
        }

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

bool GetSparseInfo(container_t type, uint32_t* out_flags, char** out_path) {
    BEGIN_HELPER;
    switch (type) {
    case SPARSE: {
        *out_flags = 0;
        *out_path = sparse_path;
        break;
    }
    case SPARSE_LZ4: {
        *out_flags = fvm::kSparseFlagLz4;
        *out_path = sparse_lz4_path;
        break;
    }
    case SPARSE_ZXCRYPT: {
        *out_flags = fvm::kSparseFlagZxcrypt;
        *out_path = sparse_path;
        break;
    }
    default:
        ASSERT_TRUE(false);
    }
    END_HELPER;
}

bool CreateReportDestroy(container_t type, size_t slice_size) {
    BEGIN_HELPER;
    switch (type) {
    case SPARSE:
        __FALLTHROUGH;
    case SPARSE_LZ4:
        __FALLTHROUGH;
    case SPARSE_ZXCRYPT: {
        uint32_t flags;
        char* path;
        ASSERT_TRUE(GetSparseInfo(type, &flags, &path));
        ASSERT_TRUE(CreateSparse(flags, slice_size));
        ASSERT_TRUE(ReportSparse(flags));
        ASSERT_TRUE(DestroySparse(flags));
        break;
    }
    case FVM: {
        ASSERT_TRUE(CreateFvm(true, 0, slice_size));
        ASSERT_TRUE(ReportFvm());
        ASSERT_TRUE(ExtendFvm(CONTAINER_SIZE * 2));
        ASSERT_TRUE(ReportFvm());
        ASSERT_TRUE(DestroyFvm());
        break;
    }
    case FVM_NEW: {
        ASSERT_TRUE(CreateFvm(false, 0, slice_size));
        ASSERT_TRUE(ReportFvm());
        ASSERT_TRUE(ExtendFvm(CONTAINER_SIZE * 2));
        ASSERT_TRUE(ReportFvm());
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

bool VerifyFvmSize(size_t expected_size) {
    BEGIN_HELPER;
    FvmContainer fvmContainer(fvm_path, 0, 0, 0);
    size_t calculated_size = fvmContainer.CalculateDiskSize();
    size_t actual_size = fvmContainer.GetDiskSize();

    ASSERT_EQ(calculated_size, actual_size);
    ASSERT_EQ(actual_size, expected_size);
    END_HELPER;
}

template <container_t ContainerType, size_t NumDirs, size_t NumFiles, size_t MaxSize,
          size_t SliceSize>
bool TestDiskSizeCalculation() {
    BEGIN_TEST;
    ASSERT_TRUE(CreatePartitions(false /* enable_data */));
    ASSERT_TRUE(PopulatePartitions(NumDirs, NumFiles, MaxSize));
    uint32_t flags;
    char* path;
    ASSERT_TRUE(GetSparseInfo(ContainerType, &flags, &path));
    ASSERT_TRUE(CreateSparse(flags, SliceSize));
    ASSERT_TRUE(ReportSparse(flags));
    SparseContainer sparseContainer(path, 0, 0);

    size_t expected_size = sparseContainer.CalculateDiskSize();
    ASSERT_EQ(sparseContainer.CheckDiskSize(expected_size), ZX_OK);
    ASSERT_NE(sparseContainer.CheckDiskSize(expected_size - 1), ZX_OK);

    // Create an FVM using the same partitions and verify its size matches expected.
    ASSERT_TRUE(CreateFvm(false, 0, SliceSize));
    ASSERT_TRUE(VerifyFvmSize(expected_size));
    ASSERT_TRUE(DestroyFvm());

    // Create an FVM by paving the sparse file and verify its size matches expected.
    ASSERT_EQ(sparseContainer.Pave(fvm_path, 0, 0), ZX_OK);
    ASSERT_TRUE(VerifyFvmSize(expected_size));
    ASSERT_TRUE(DestroyFvm());

    ASSERT_TRUE(DestroyPartitions());
    ASSERT_TRUE(DestroySparse(flags));
    END_TEST;
}

// Test to ensure that compression will fail if the buffer is too small.
bool TestCompressorBufferTooSmall() {
    BEGIN_TEST;

    CompressionContext compression;
    ASSERT_EQ(compression.Setup(1), ZX_OK);

    unsigned int seed = 0;
    zx_status_t status = ZX_OK;
    for (;;) {
        char data = static_cast<char>(rand_r(&seed));
        if ((status = compression.Compress(&data, 1)) != ZX_OK) {
            break;
        }
    }

    ASSERT_EQ(status, ZX_ERR_INTERNAL);
    ASSERT_EQ(compression.Finish(), ZX_OK);

    END_TEST;
}

bool TestBlobfsCompressor() {
    BEGIN_TEST;
    blobfs::Compressor compressor;

    // Pretend we're going to compress only one byte of data.
    const size_t buf_size = compressor.BufferMax(1);
    fbl::unique_ptr<char[]> buf(new char[buf_size]);
    ASSERT_EQ(compressor.Initialize(buf.get(), buf_size), ZX_OK);

    // Create data as large as possible that will fit still within this buffer.
    size_t data_size = 0;
    while (compressor.BufferMax(data_size + 1) <= buf_size) {
        ++data_size;
    }

    ASSERT_GT(data_size, 0);
    ASSERT_EQ(compressor.BufferMax(data_size), buf_size);
    ASSERT_GT(compressor.BufferMax(data_size+1), buf_size);

    unsigned int seed = 0;
    for (size_t i = 0; i < data_size; i++) {
        char data = static_cast<char>(rand_r(&seed));
        ASSERT_EQ(compressor.Update(&data, 1), ZX_OK);
    }

    ASSERT_EQ(compressor.End(), ZX_OK);
    END_TEST;
}

enum class PaveSizeType {
    kSmall, // Allocate disk space for paving smaller than what is required.
    kExact, // Allocate exactly as much disk space as is required for a pave.
    kLarge, // Allocate additional disk space beyond what is needed for pave.
};

enum class PaveCreateType {
    kBefore, // Create FVM file before paving.
    kOffset, // Create FVM at an offset within the file.
    kOnPave, // Create the file at the time of pave.
};

// Creates a file at |fvm_path| to which an FVM is intended to be paved from an existing sparse
// file. If create_type is kOnPave, no file is created.
// The size of the file will depend on the |expected_size|, as well as the |create_type| and
// |size_type| options.
// The intended offset and allocated size for the paved FVM will be returned as |out_pave_offset|
// and |out_pave_size| respectively.
bool CreatePaveFile(PaveCreateType create_type, PaveSizeType size_type, size_t expected_size,
                    size_t* out_pave_offset, size_t* out_pave_size) {
    BEGIN_HELPER;
    *out_pave_offset = 0;
    *out_pave_size = 0;

    if (create_type == PaveCreateType::kOnPave) {
        ASSERT_EQ(size_type, PaveSizeType::kExact);
    } else {
        size_t disk_size = 0;

        switch (size_type) {
        case PaveSizeType::kSmall:
            disk_size = expected_size - 1;
            break;
        case PaveSizeType::kExact:
            disk_size = expected_size;
            break;
        case PaveSizeType::kLarge:
            disk_size = expected_size * 2;
            break;
        }

        *out_pave_size = disk_size;
        *out_pave_offset = 0;

        if (create_type == PaveCreateType::kOffset) {
            disk_size = disk_size * 2;
            ASSERT_GT(disk_size, *out_pave_size);
            *out_pave_offset = disk_size - *out_pave_size;
        }

        fbl::unique_fd fd(open(fvm_path, O_CREAT | O_EXCL | O_WRONLY, 0644));
        ASSERT_TRUE(fd);
        ASSERT_EQ(ftruncate(fd.get(), disk_size), 0);
    }

    END_HELPER;
}

constexpr uint32_t kNumDirs = 10;
constexpr uint32_t kNumFiles = 10;
constexpr uint32_t kMaxSize = (1 << 20);

template <PaveCreateType CreateType, PaveSizeType SizeType, container_t ContainerType,
          size_t SliceSize>
bool TestPave() {
    BEGIN_TEST;

    uint32_t sparse_flags;
    char* src_path;
    ASSERT_TRUE(GetSparseInfo(ContainerType, &sparse_flags, &src_path));

    ASSERT_TRUE(CreatePartitions(false /* enable_data */));
    ASSERT_TRUE(PopulatePartitions(kNumDirs, kNumFiles, kMaxSize));
    ASSERT_TRUE(CreateSparse(sparse_flags, SliceSize));
    ASSERT_TRUE(DestroyPartitions());

    size_t pave_offset = 0;
    size_t pave_size = 0;
    SparseContainer sparseContainer(src_path, 0, 0);
    size_t expected_size = sparseContainer.CalculateDiskSize();
    ASSERT_TRUE(CreatePaveFile(CreateType, SizeType, expected_size, &pave_offset, &pave_size));

    if (SizeType == PaveSizeType::kSmall) {
        ASSERT_NE(sparseContainer.Pave(fvm_path, pave_offset, pave_size), ZX_OK);
    } else {
        ASSERT_EQ(sparseContainer.Pave(fvm_path, pave_offset, pave_size), ZX_OK);
        ASSERT_TRUE(ReportFvm(pave_offset));
    }

    ASSERT_TRUE(DestroyFvm());
    ASSERT_TRUE(DestroySparse(sparse_flags));

    END_TEST;
}

// Paving an FVM with a data partition will fail since we zxcrypt is not currently implemented on
// host.
// TODO(planders): Once we are able to create zxcrypt'd FVM images on host, remove this test.
bool TestPaveZxcryptFail() {
    BEGIN_TEST;
    ASSERT_TRUE(CreatePartitions());
    ASSERT_TRUE(PopulatePartitions(kNumDirs, kNumFiles, kMaxSize));
    ASSERT_TRUE(CreateSparse(0, DEFAULT_SLICE_SIZE));
    SparseContainer sparseContainer(sparse_path, 0, 0);
    ASSERT_NE(sparseContainer.Pave(fvm_path, 0, 0), ZX_OK);
    ASSERT_TRUE(DestroyPartitions());
    ASSERT_TRUE(DestroySparse(0));
    END_TEST;
}

bool GeneratePartitionPath(fs_type_t fs_type, guid_type_t guid_type) {
    BEGIN_HELPER;
    ASSERT_LT(partition_count, MAX_PARTITIONS);

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
    ASSERT_TRUE(GeneratePartitionPath(MINFS, DATA_UNSAFE));
    ASSERT_TRUE(GeneratePartitionPath(MINFS, SYSTEM));
    ASSERT_TRUE(GeneratePartitionPath(MINFS, DEFAULT));
    ASSERT_TRUE(GeneratePartitionPath(BLOBFS, BLOBSTORE));
    ASSERT_TRUE(GeneratePartitionPath(BLOBFS, DEFAULT));
    ASSERT_EQ(partition_count, MAX_PARTITIONS);

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
    RUN_TEST_MEDIUM((TestEmptyPartitions<SPARSE_ZXCRYPT, slice_size>)) \
    RUN_TEST_MEDIUM((TestEmptyPartitions<FVM, slice_size>)) \
    RUN_TEST_MEDIUM((TestEmptyPartitions<FVM_NEW, slice_size>)) \
    RUN_TEST_MEDIUM((TestEmptyPartitions<FVM_OFFSET, slice_size>)) \
    RUN_TEST_MEDIUM((TestDiskSizeCalculation<SPARSE, 0, 0, 0, slice_size>)) \
    RUN_TEST_MEDIUM((TestDiskSizeCalculation<SPARSE_LZ4, 0, 0, 0, slice_size>)) \
    RUN_TEST_MEDIUM((TestDiskSizeCalculation<SPARSE_ZXCRYPT, 0, 0, 0, slice_size>))

#define RUN_FOR_ALL_TYPES(num_dirs, num_files, max_size, slice_size) \
    RUN_TEST_MEDIUM((TestPartitions<SPARSE, num_dirs, num_files, max_size, slice_size>)) \
    RUN_TEST_MEDIUM((TestPartitions<SPARSE_LZ4, num_dirs, num_files, max_size, slice_size>)) \
    RUN_TEST_MEDIUM((TestPartitions<SPARSE_ZXCRYPT, num_dirs, num_files, max_size, slice_size>)) \
    RUN_TEST_MEDIUM((TestPartitions<FVM, num_dirs, num_files, max_size, slice_size>)) \
    RUN_TEST_MEDIUM((TestPartitions<FVM_NEW, num_dirs, num_files, max_size, slice_size>)) \
    RUN_TEST_MEDIUM((TestPartitions<FVM_OFFSET, num_dirs, num_files, max_size, slice_size>)) \
    RUN_TEST_MEDIUM((TestDiskSizeCalculation<SPARSE, num_dirs, num_files, max_size, slice_size>)) \
    RUN_TEST_MEDIUM((TestDiskSizeCalculation<SPARSE_LZ4, num_dirs, num_files, max_size, \
                                             slice_size>)) \
    RUN_TEST_MEDIUM((TestDiskSizeCalculation<SPARSE_ZXCRYPT, num_dirs, num_files, max_size, \
                                             slice_size>))

#define RUN_ALL_SPARSE(create_type, size_type, slice_size) \
    RUN_TEST_MEDIUM((TestPave<create_type, size_type, SPARSE, slice_size>)) \
    RUN_TEST_MEDIUM((TestPave<create_type, size_type, SPARSE_LZ4, slice_size>)) \
    RUN_TEST_MEDIUM((TestPave<create_type, size_type, SPARSE_ZXCRYPT, slice_size>))

#define RUN_ALL_PAVE(slice_size) \
    RUN_ALL_SPARSE(PaveCreateType::kBefore, PaveSizeType::kSmall, slice_size) \
    RUN_ALL_SPARSE(PaveCreateType::kBefore, PaveSizeType::kExact, slice_size) \
    RUN_ALL_SPARSE(PaveCreateType::kBefore, PaveSizeType::kLarge, slice_size) \
    RUN_ALL_SPARSE(PaveCreateType::kOffset, PaveSizeType::kSmall, slice_size) \
    RUN_ALL_SPARSE(PaveCreateType::kOffset, PaveSizeType::kExact, slice_size) \
    RUN_ALL_SPARSE(PaveCreateType::kOffset, PaveSizeType::kLarge, slice_size) \
    RUN_ALL_SPARSE(PaveCreateType::kOnPave, PaveSizeType::kExact, slice_size)

//TODO(planders): add tests for FVM on GPT (with offset)
BEGIN_TEST_CASE(fvm_host_tests)
RUN_FOR_ALL_TYPES_EMPTY(8192)
RUN_FOR_ALL_TYPES_EMPTY(DEFAULT_SLICE_SIZE)
RUN_FOR_ALL_TYPES(kNumDirs, kNumFiles, kMaxSize, 8192)
RUN_FOR_ALL_TYPES(kNumDirs, kNumFiles, kMaxSize, DEFAULT_SLICE_SIZE)
RUN_TEST_MEDIUM(TestCompressorBufferTooSmall)
RUN_TEST_MEDIUM(TestBlobfsCompressor)
RUN_ALL_PAVE(8192)
RUN_ALL_PAVE(DEFAULT_SLICE_SIZE)
RUN_TEST_MEDIUM(TestPaveZxcryptFail)
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
