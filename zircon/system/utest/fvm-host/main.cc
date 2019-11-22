// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <memory>
#include <utility>

#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fvm-host/container.h>
#include <fvm/sparse-reader.h>
#include <minfs/host.h>
#include <unittest/unittest.h>

#define DEFAULT_SLICE_SIZE (8lu * (1 << 20))  // 8 mb
#define PARTITION_SIZE (1lu * (1 << 28))      // 128 mb
#define CONTAINER_SIZE (2lu * (1 << 30))      // 2 gb

#define MAX_PARTITIONS 6

static char test_dir[PATH_MAX];
static char sparse_path[PATH_MAX];
static char sparse_lz4_path[PATH_MAX];
static char fvm_path[PATH_MAX];

static constexpr char kEmptyString[] = "";

#ifndef __APPLE__
// TODO(FLK-259): Re-enable tests once the cause of timeout has been determined.
// Ifdef these out to remove a build warning on mac.
static constexpr uint32_t kDefaultNumDirs = 10;
static constexpr uint32_t kDefaultNumFiles = 10;
static constexpr uint32_t kDefaultMaxSize = (1 << 20);
#endif

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
  SPARSE,          // Sparse container
  SPARSE_LZ4,      // Sparse container compressed with LZ4
  SPARSE_ZXCRYPT,  // Sparse container to be stored on a zxcrypt volume
  FVM,             // Explicitly created FVM container
  FVM_NEW,         // FVM container created on FvmContainer::Create
  FVM_OFFSET,      // FVM container created at an offset within a file
} container_t;

typedef struct {
  fs_type_t fs_type;
  guid_type_t guid_type;
  char path[PATH_MAX];
  bool created = false;
  FvmReservation reserve;
  zx_status_t status;

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

  void GeneratePath(char* dir) { sprintf(path, "%s%s_%s.bin", dir, FsTypeName(), GuidTypeName()); }
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
  ASSERT_EQ(blobfs::GetBlockCount(r, &block_count), ZX_OK, "Cannot find end of underlying device");
  ASSERT_EQ(blobfs::Mkfs(r, block_count), ZX_OK, "Failed to make blobfs partition");
  ASSERT_EQ(close(r), 0, "Unable to close disk\n");
  END_HELPER;
}

// Adds all create partitions to |container|. If enable_data is false, the DATA partition is
// skipped. This is to avoid discrepancies in disk size calculation due to zxcrypt not being
// implemented on host.
// Stores success or failure of each AddPartition in part->status.
// TODO(planders): Once we are able to create zxcrypt'd FVM images on host, remove enable_data flag.
void AddPartitionsReserve(Container* container, bool enable_data) {
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

    if (!enable_data && !strcmp(part->GuidTypeName(), kDataTypeName)) {
      unittest_printf("Skipping addition of partition %s\n", part->path);
      continue;
    }

    if (part->created) {
      unittest_printf("Adding partition to container: %s\n", part->path);
      part->status = container->AddPartition(part->path, part->GuidTypeName(), &part->reserve);
    }
  }
}

// Adds all created partitions to |container|. Asserts on failures.
bool AddPartitions(Container* container, bool enable_data, bool should_pass) {
  BEGIN_HELPER;
  AddPartitionsReserve(container, enable_data);
  for (unsigned i = 0; i < partition_count; i++) {
    partition_t* part = &partitions[i];
    if (part->created) {
      bool added = part->status == ZX_OK;
      bool reserved = part->reserve.Approved();
      if ((added && reserved) != should_pass) {
        fprintf(stderr, "Failed to add partition %d\n", added);
        part->reserve.Dump(stderr);
      }
      ASSERT_EQ(added && reserved, should_pass);
    }
  }

  END_HELPER;
}

// Creates a sparse container and adds partitions to it. When should_pass is false,
// the function surfaces the error in adding partition to caller without asserting.
bool CreateSparse(uint32_t flags, size_t slice_size, bool should_pass, bool enable_data = true,
                  uint64_t max_disk_size = 0) {
  BEGIN_HELPER;

  const char* path = ((flags & fvm::kSparseFlagLz4) != 0) ? sparse_lz4_path : sparse_path;
  unittest_printf("Creating sparse container: %s\n", path);
  std::unique_ptr<SparseContainer> sparseContainer;
  ASSERT_EQ(SparseContainer::CreateNew(path, slice_size, flags, max_disk_size, &sparseContainer),
            ZX_OK, "Failed to initialize sparse container");
  ASSERT_TRUE(AddPartitions(sparseContainer.get(), enable_data, should_pass));
  if (should_pass) {
    ASSERT_EQ(sparseContainer->Commit(), ZX_OK, "Failed to write to sparse file");
    if (max_disk_size > 0) {
      ASSERT_EQ(sparseContainer->MaximumDiskSize(), max_disk_size);
    }
    uint64_t data_size = 0, inode_count = 0, used_size = 0;
    if ((flags & fvm::kSparseFlagLz4) == 0) {
      ASSERT_EQ(sparseContainer->UsedSize(&used_size), ZX_OK);
      ASSERT_NE(used_size, 0);
      ASSERT_EQ(sparseContainer->UsedDataSize(&data_size), ZX_OK);
      ASSERT_NE(data_size, 0);
      ASSERT_GT(used_size, data_size);
      ASSERT_EQ(sparseContainer->UsedInodes(&inode_count), ZX_OK);
      ASSERT_NE(inode_count, 0);
    } else {
      ASSERT_NE(sparseContainer->UsedSize(&used_size), ZX_OK);
      ASSERT_NE(sparseContainer->UsedDataSize(&data_size), ZX_OK);
      ASSERT_NE(sparseContainer->UsedInodes(&inode_count), ZX_OK);
    }
  }
  END_HELPER;
}

bool CreateSparseEnsure(uint32_t flags, size_t slice_size, bool enable_data = true) {
  BEGIN_HELPER;
  ASSERT_TRUE(CreateSparse(flags, slice_size, true, enable_data));
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
  std::unique_ptr<Container> container;
  ASSERT_EQ(Container::Create(path, offset, 0, &container), ZX_OK,
            "Failed to initialize container");
  ASSERT_EQ(container->Verify(), ZX_OK, "File check failed\n");
  END_HELPER;
}

bool ReportSparse(uint32_t flags) {
  BEGIN_HELPER;
  if ((flags & fvm::kSparseFlagLz4) != 0) {
    unittest_printf("Decompressing sparse file\n");
    std::unique_ptr<SparseContainer> compressedContainer;
    ASSERT_EQ(SparseContainer::CreateExisting(sparse_lz4_path, &compressedContainer), ZX_OK);
    ASSERT_EQ(compressedContainer->Decompress(sparse_path), ZX_OK);
  }

  ASSERT_TRUE(ReportContainer(sparse_path, 0));

  // Check that the calculated disk size passes inspection, but any size lower doesn't.
  std::unique_ptr<SparseContainer> container;
  ASSERT_EQ(SparseContainer::CreateExisting(sparse_path, &container), ZX_OK);

  size_t expected_size = container->CalculateDiskSize();
  ASSERT_EQ(container->CheckDiskSize(expected_size), ZX_OK);
  ASSERT_NE(container->CheckDiskSize(expected_size - 1), ZX_OK);
  END_HELPER;
}

// Creates a fvm container and adds partitions to it. When should succeed is false,
// the function surfaces the error in adding partition to caller without asserting.
bool CreateFvm(bool create_before, off_t offset, size_t slice_size, bool should_pass,
               bool enable_data = true) {
  BEGIN_HELPER;
  unittest_printf("Creating fvm container: %s\n", fvm_path);

  off_t length = 0;
  if (create_before) {
    ASSERT_TRUE(CreateFile(fvm_path, CONTAINER_SIZE));
    ASSERT_TRUE(StatFile(fvm_path, &length));
  }

  std::unique_ptr<FvmContainer> fvmContainer;
  ASSERT_EQ(FvmContainer::CreateNew(fvm_path, slice_size, offset, length - offset, &fvmContainer),
            ZX_OK, "Failed to initialize fvm container");
  ASSERT_TRUE(AddPartitions(fvmContainer.get(), enable_data, should_pass));
  if (should_pass) {
    ASSERT_EQ(fvmContainer->Commit(), ZX_OK, "Failed to write to fvm file");
  }
  END_HELPER;
}

bool CreateFvmEnsure(bool create_before, off_t offset, size_t slice_size, bool enable_data = true) {
  BEGIN_HELPER;
  ASSERT_TRUE(CreateFvm(create_before, offset, slice_size, true, enable_data));
  END_HELPER;
}

bool ExtendFvm(off_t length) {
  BEGIN_HELPER;
  std::unique_ptr<Container> container;
  ASSERT_EQ(ZX_OK, Container::Create(fvm_path, 0, 0, &container),
            "Failed to initialize fvm container");
  ASSERT_EQ(static_cast<FvmContainer*>(container.get())->Extend(length), ZX_OK,
            "Failed to write to fvm file");
  off_t current_length;
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
  char filename[len + 1];

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

bool GenerateData(size_t len, std::unique_ptr<uint8_t[]>* out) {
  BEGIN_HELPER;
  // Fill a test buffer with data
  fbl::AllocChecker ac;
  std::unique_ptr<uint8_t[]> data(new (&ac) uint8_t[len]);
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
  std::unique_ptr<uint8_t[]> data;
  ASSERT_TRUE(GenerateData(size, &data));
  ssize_t result = emu_write(fd, data.get(), size);
  ASSERT_GE(result, 0, "Failed to write data to file");
  ASSERT_EQ(static_cast<size_t>(result), size, "Failed to write data to file");
  ASSERT_EQ(emu_close(fd), 0);
  END_HELPER;
}

bool PopulateMinfs(const char* path, size_t ndirs, size_t nfiles, size_t max_size) {
  BEGIN_HELPER;
  ASSERT_EQ(emu_mount(path), 0, "Unable to run mount");
  fbl::Vector<fbl::String> paths;
  paths.push_back(fbl::String("::"));
  size_t total_size = 0;

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
    total_size += size;
    char new_file[PATH_MAX];
    GenerateFilename(base_dir, 10, new_file);
    AddFileMinfs(new_file, size);
  }
  uint64_t used_data, used_inodes, used_size;
  ASSERT_EQ(emu_get_used_resources(path, &used_data, &used_inodes, &used_size), 0);

  // Used data should be greater than or equal to total size of the data we added
  ASSERT_GE(used_data, total_size);

  // Some fs use inodes for internal structures (including root directory).
  // So used_nodes should be gt total files+directories created.
  ASSERT_GE(used_inodes, nfiles + ndirs);

  // Used size should be always greater than used data.
  ASSERT_GT(used_size, used_data);

  END_HELPER;
}

bool AddFileBlobfs(blobfs::Blobfs* bs, size_t size) {
  BEGIN_HELPER;
  char new_file[PATH_MAX];
  GenerateFilename(test_dir, 10, new_file);

  fbl::unique_fd datafd(open(new_file, O_RDWR | O_CREAT | O_EXCL, 0755));
  ASSERT_TRUE(datafd, "Unable to create new file");
  std::unique_ptr<uint8_t[]> data;
  ASSERT_TRUE(GenerateData(size, &data));
  ssize_t result = write(datafd.get(), data.get(), size);
  ASSERT_GE(result, 0, "Failed to write data to file");
  ASSERT_EQ(static_cast<size_t>(result), size, "Failed to write data to file");
  ASSERT_EQ(blobfs::blobfs_add_blob(bs, nullptr, datafd.get()), ZX_OK, "Failed to add blob");
  ASSERT_EQ(unlink(new_file), 0);
  END_HELPER;
}

bool PopulateBlobfs(const char* path, size_t nfiles, size_t max_size) {
  BEGIN_HELPER;
  fbl::unique_fd blobfd(open(path, O_RDWR, 0755));
  ASSERT_TRUE(blobfd, "Unable to open blobfs path");
  std::unique_ptr<blobfs::Blobfs> bs;
  ASSERT_EQ(blobfs::blobfs_create(&bs, blobfd.duplicate()), ZX_OK, "Failed to create blobfs");
  size_t total_size = 0;
  for (unsigned i = 0; i < nfiles; i++) {
    size_t size = 1 + (rand() % max_size);
    ASSERT_TRUE(AddFileBlobfs(bs.get(), size));
    total_size += size;
  }
  uint64_t used_data, used_inodes, used_size;

  // Used data should be greater than or equal to total size of the data we added
  ASSERT_EQ(blobfs::UsedDataSize(blobfd, &used_data), ZX_OK);
  ASSERT_GE(used_data, total_size);

  // Blobfs uses inodes for internal structures (including file extents).
  // So used_nodes should be greater than or equal to total files+directories created.
  ASSERT_EQ(blobfs::UsedInodes(blobfd, &used_inodes), ZX_OK);
  ASSERT_GE(used_inodes, nfiles);

  // Used size should be always greater than used data.
  ASSERT_EQ(blobfs::UsedSize(blobfd, &used_size), ZX_OK);
  ASSERT_GE(used_size, used_data);
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
      // Reset reservations for next iteration of the test.
      part->reserve = FvmReservation({}, {}, {});
    }
  }

  END_HELPER;
}

// Creates all partitions defined in Setup().
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

bool CreateReportDestroy(container_t type, size_t slice_size, bool test_success = true,
                         std::optional<uint64_t> data_size = {},
                         std::optional<uint64_t> inodes_count = {},
                         std::optional<uint64_t> limit = {}) {
  BEGIN_HELPER;
  for (unsigned i = 0; i < partition_count; i++) {
    partition_t* part = &partitions[i];
    part->reserve = FvmReservation(inodes_count, data_size, limit);
  }
  switch (type) {
    case SPARSE:
      __FALLTHROUGH;
    case SPARSE_LZ4:
      __FALLTHROUGH;
    case SPARSE_ZXCRYPT: {
      uint32_t flags;
      char* path;
      ASSERT_TRUE(GetSparseInfo(type, &flags, &path));
      ASSERT_TRUE(CreateSparse(flags, slice_size, test_success));
      if (test_success) {
        ASSERT_TRUE(ReportSparse(flags));
      }
      ASSERT_TRUE(DestroySparse(flags));
      break;
    }
    case FVM: {
      ASSERT_TRUE(CreateFvm(true, 0, slice_size, test_success));
      if (test_success) {
        ASSERT_TRUE(ReportFvm());
        ASSERT_TRUE(ExtendFvm(CONTAINER_SIZE * 2));
        ASSERT_TRUE(ReportFvm());
      }
      ASSERT_TRUE(DestroyFvm());
      break;
    }
    case FVM_NEW: {
      ASSERT_TRUE(CreateFvm(false, 0, slice_size, test_success));
      if (test_success) {
        ASSERT_TRUE(ReportFvm());
        ASSERT_TRUE(ExtendFvm(CONTAINER_SIZE * 2));
        ASSERT_TRUE(ReportFvm());
      }
      ASSERT_TRUE(DestroyFvm());
      break;
    }
    case FVM_OFFSET: {
      ASSERT_TRUE(CreateFvm(true, DEFAULT_SLICE_SIZE, slice_size, test_success));
      if (test_success) {
        ASSERT_TRUE(ReportFvm(DEFAULT_SLICE_SIZE));
      }
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
bool TestPartitions() {
  BEGIN_TEST;
  ASSERT_TRUE(CreateReportDestroy(ContainerType, SliceSize));
  END_TEST;
}

template <container_t ContainerType, size_t SliceSize, bool test_success, uint64_t data,
          uint64_t inodes, uint64_t size_limit>
bool TestPartitionsFailures() {
  BEGIN_TEST;
  std::optional<uint64_t> odata = data == 0 ? std::optional<uint64_t>{} : data;
  std::optional<uint64_t> osize_limit = size_limit == 0 ? std::optional<uint64_t>{} : size_limit;
  std::optional<uint64_t> oinodes = inodes == 0 ? std::optional<uint64_t>{} : inodes;

  ASSERT_TRUE(
      CreateReportDestroy(ContainerType, SliceSize, test_success, odata, oinodes, osize_limit));
  END_TEST;
}

bool VerifyFvmSize(size_t expected_size) {
  BEGIN_HELPER;
  std::unique_ptr<FvmContainer> fvmContainer;
  ASSERT_EQ(FvmContainer::CreateExisting(fvm_path, 0, &fvmContainer), ZX_OK);
  size_t calculated_size = fvmContainer->CalculateDiskSize();
  size_t actual_size = fvmContainer->GetDiskSize();

  ASSERT_EQ(calculated_size, actual_size);
  ASSERT_EQ(actual_size, expected_size);
  END_HELPER;
}

template <container_t ContainerType, size_t SliceSize>
bool TestDiskSizeCalculation() {
  BEGIN_TEST;
  uint32_t flags;
  char* path;
  ASSERT_TRUE(GetSparseInfo(ContainerType, &flags, &path));
  ASSERT_TRUE(CreateSparseEnsure(flags, SliceSize, false /* enable_data */));
  ASSERT_TRUE(ReportSparse(flags));

  std::unique_ptr<SparseContainer> sparseContainer;
  ASSERT_EQ(SparseContainer::CreateExisting(path, &sparseContainer), ZX_OK);

  size_t expected_size = sparseContainer->CalculateDiskSize();
  ASSERT_EQ(sparseContainer->CheckDiskSize(expected_size), ZX_OK);
  ASSERT_NE(sparseContainer->CheckDiskSize(expected_size - 1), ZX_OK);

  // Create an FVM using the same partitions and verify its size matches expected.
  ASSERT_TRUE(CreateFvmEnsure(false, 0, SliceSize, false /* enable_data */));
  ASSERT_TRUE(VerifyFvmSize(expected_size));
  ASSERT_TRUE(DestroyFvm());

  // Create an FVM by paving the sparse file and verify its size matches expected.
  std::unique_ptr<fvm::host::UniqueFdWrapper> wrapper;
  ASSERT_EQ(fvm::host::UniqueFdWrapper::Open(fvm_path, O_RDWR | O_CREAT | O_EXCL, 0644, &wrapper),
            ZX_OK);
  ASSERT_EQ(sparseContainer->Pave(std::move(wrapper), 0, 0), ZX_OK);
  ASSERT_TRUE(VerifyFvmSize(expected_size));
  ASSERT_TRUE(DestroyFvm());

  ASSERT_TRUE(DestroySparse(flags));
  END_TEST;
}

// Test to ensure that compression will fail if the buffer is too small.
bool TestCompressorBufferTooSmall() {
  BEGIN_TEST;
  auto result = CompressionContext::Create();
  ASSERT_TRUE(result.is_ok());
  CompressionContext compression = std::move(result.take_ok_result().value);
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

  // Clean up if possible but don't expect that this can necessarily
  // succeed after a failed Compress call.
  compression.Finish();

  END_TEST;
}

enum class PaveSizeType {
  kSmall,  // Allocate disk space for paving smaller than what is required.
  kExact,  // Allocate exactly as much disk space as is required for a pave.
  kLarge,  // Allocate additional disk space beyond what is needed for pave.
};

enum class PaveCreateType {
  kBefore,  // Create FVM file before paving.
  kOffset,  // Create FVM at an offset within the file.
};

// Creates a file at |fvm_path| to which an FVM is intended to be paved from an existing sparse
// file. The size of the file will depend on the |expected_size|, as well as the |create_type| and
// |size_type| options.
// The intended offset and allocated size for the paved FVM will be returned as |out_pave_offset|
// and |out_pave_size| respectively.
bool CreatePaveFile(PaveCreateType create_type, PaveSizeType size_type, size_t expected_size,
                    size_t* out_pave_offset, size_t* out_pave_size) {
  BEGIN_HELPER;
  *out_pave_offset = 0;
  *out_pave_size = 0;

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

  END_HELPER;
}

template <PaveCreateType CreateType, PaveSizeType SizeType, container_t ContainerType,
          size_t SliceSize>
bool TestPave() {
  BEGIN_TEST;

  uint32_t sparse_flags;
  char* src_path;
  ASSERT_TRUE(GetSparseInfo(ContainerType, &sparse_flags, &src_path));
  ASSERT_TRUE(CreateSparseEnsure(sparse_flags, SliceSize, false /* enable_data */));

  size_t pave_offset = 0;
  size_t pave_size = 0;

  std::unique_ptr<SparseContainer> sparseContainer;
  ASSERT_EQ(SparseContainer::CreateExisting(src_path, &sparseContainer), ZX_OK);
  size_t expected_size = sparseContainer->CalculateDiskSize();
  ASSERT_TRUE(CreatePaveFile(CreateType, SizeType, expected_size, &pave_offset, &pave_size));

  std::unique_ptr<fvm::host::UniqueFdWrapper> wrapper;
  ASSERT_EQ(fvm::host::UniqueFdWrapper::Open(fvm_path, O_RDWR | O_CREAT, 0644, &wrapper), ZX_OK);

  if (SizeType == PaveSizeType::kSmall) {
    ASSERT_NE(sparseContainer->Pave(std::move(wrapper), pave_offset, pave_size), ZX_OK);
  } else {
    ASSERT_EQ(sparseContainer->Pave(std::move(wrapper), pave_offset, pave_size), ZX_OK);
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
  ASSERT_TRUE(CreateSparseEnsure(0, DEFAULT_SLICE_SIZE));
  std::unique_ptr<SparseContainer> sparseContainer;
  ASSERT_EQ(SparseContainer::CreateExisting(sparse_path, &sparseContainer), ZX_OK);

  std::unique_ptr<fvm::host::UniqueFdWrapper> wrapper;
  ASSERT_EQ(fvm::host::UniqueFdWrapper::Open(fvm_path, O_RDWR | O_CREAT, 0644, &wrapper), ZX_OK);
  ASSERT_NE(sparseContainer->Pave(std::move(wrapper), 0, 0), ZX_OK);
  ASSERT_TRUE(DestroySparse(0));
  ASSERT_EQ(unlink(fvm_path), 0);
  END_TEST;
}

constexpr size_t CalculateExtendedContainerSize(const size_t initial_container_size,
                                                const size_t extended_container_size) {
  const size_t initial_metadata_size =
      fvm::MetadataSize(initial_container_size, DEFAULT_SLICE_SIZE);
  const size_t extended_metadata_size =
      fvm::MetadataSize(extended_container_size, DEFAULT_SLICE_SIZE);

  if (extended_metadata_size == initial_metadata_size) {
    return CalculateExtendedContainerSize(initial_container_size, extended_container_size * 2);
  }

  return extended_container_size;
}

// Test extend with values that ensure the FVM metadata size will increase.
bool TestExtendChangesMetadataSize() {
  BEGIN_TEST;
  ASSERT_TRUE(CreateFvm(true, 0, DEFAULT_SLICE_SIZE, true /* should_pass */));
  size_t extended_container_size = CalculateExtendedContainerSize(CONTAINER_SIZE, CONTAINER_SIZE);
  ASSERT_GT(fvm::MetadataSize(extended_container_size, DEFAULT_SLICE_SIZE),
            fvm::MetadataSize(CONTAINER_SIZE, DEFAULT_SLICE_SIZE));
  ASSERT_TRUE(ExtendFvm(extended_container_size));
  ASSERT_TRUE(ReportFvm());
  ASSERT_TRUE(DestroyFvm());
  END_TEST;
}

// Attempts to create a SparseContainer from an existing sparse image when one does not exist.
bool CreateExistingSparseFails() {
  BEGIN_TEST;
  std::unique_ptr<SparseContainer> sparseContainer;
  ASSERT_NE(SparseContainer::CreateExisting(sparse_path, &sparseContainer), ZX_OK);
  END_TEST;
}

bool CreateExistingFvmFails() {
  BEGIN_TEST;
  std::unique_ptr<FvmContainer> fvmContainer;
  ASSERT_NE(FvmContainer::CreateExisting(fvm_path, 0, &fvmContainer), ZX_OK);
  END_TEST;
}

// Attempts to re-create a sparse image at the same path with a different slice size, verifying
// that the slice size is updated.
bool RecreateSparseWithDifferentSliceSize() {
  BEGIN_TEST;
  std::unique_ptr<SparseContainer> sparseContainer;

  ASSERT_TRUE(CreateSparse(0, 8192, true));
  ASSERT_EQ(SparseContainer::CreateExisting(sparse_path, &sparseContainer), ZX_OK);
  ASSERT_EQ(sparseContainer->SliceSize(), 8192);

  ASSERT_TRUE(CreateSparse(0, DEFAULT_SLICE_SIZE, true));
  ASSERT_EQ(SparseContainer::CreateExisting(sparse_path, &sparseContainer), ZX_OK);
  ASSERT_EQ(sparseContainer->SliceSize(), DEFAULT_SLICE_SIZE);

  ASSERT_TRUE(DestroySparse(0));
  END_TEST;
}

bool RecreateFvmWithDifferentSliceSize() {
  BEGIN_TEST;
  std::unique_ptr<FvmContainer> fvmContainer;

  // Create FVM with the larger slice size first, since this will result in a larger container
  // size. Newly created FVM's will use the current container size if it already exists, so
  // creation of this container will fail if a smaller one already exists.
  // This is not an issue with the sparse test since the container is created from scratch every
  // time.
  ASSERT_TRUE(CreateFvm(false, 0, DEFAULT_SLICE_SIZE, true));
  ASSERT_EQ(FvmContainer::CreateExisting(fvm_path, 0, &fvmContainer), ZX_OK);
  ASSERT_EQ(fvmContainer->SliceSize(), DEFAULT_SLICE_SIZE);

  ASSERT_TRUE(CreateFvm(false, 0, 8192, true));
  ASSERT_EQ(FvmContainer::CreateExisting(fvm_path, 0, &fvmContainer), ZX_OK);
  ASSERT_EQ(fvmContainer->SliceSize(), 8192);

  ASSERT_TRUE(DestroyFvm());
  END_TEST;
}

bool TestCreatePreallocatedSparseImage() {
  BEGIN_TEST;
  constexpr uint64_t kMaxSize = 35ull << 30;
  ASSERT_TRUE(CreateSparse(0, DEFAULT_SLICE_SIZE, true, true, kMaxSize));
  std::unique_ptr<SparseContainer> sparse_container;
  ASSERT_EQ(SparseContainer::CreateExisting(sparse_path, &sparse_container), ZX_OK);

  std::unique_ptr<fvm::host::UniqueFdWrapper> wrapper;
  ASSERT_EQ(fvm::host::UniqueFdWrapper::Open(sparse_path, O_RDWR | O_CREAT, 0644, &wrapper), ZX_OK);
  ASSERT_NE(sparse_container->Pave(std::move(wrapper), 0, 0), ZX_OK);
  ASSERT_EQ(sparse_container->MaximumDiskSize(), kMaxSize);
  ASSERT_TRUE(DestroySparse(0));
  END_TEST;
}

bool TestCreatePreallocatedSparseImageExceedMaxSize() {
  BEGIN_TEST;
  constexpr uint64_t kMaxSize = sizeof(fvm::Header);
  ASSERT_FALSE(CreateSparse(0, DEFAULT_SLICE_SIZE, true, true, kMaxSize));
  ASSERT_TRUE(DestroySparse(0));
  END_TEST;
}

bool TestPavePreallocatedSparseImage() {
  BEGIN_TEST;
  constexpr uint64_t kMaxSize = 2ull << 30;
  ASSERT_TRUE(CreateSparse(0, DEFAULT_SLICE_SIZE, true /* should_pass */, false /* enable_data */,
                           kMaxSize));
  std::unique_ptr<SparseContainer> sparse_container;
  ASSERT_EQ(SparseContainer::CreateExisting(sparse_path, &sparse_container), ZX_OK);

  std::unique_ptr<fvm::host::UniqueFdWrapper> pave_wrapper;
  ASSERT_EQ(fvm::host::UniqueFdWrapper::Open(fvm_path, O_RDWR | O_CREAT, 0644, &pave_wrapper),
            ZX_OK);
  pave_wrapper->Truncate(kMaxSize);

  ASSERT_EQ(sparse_container->Pave(std::move(pave_wrapper), 0, 0), ZX_OK);
  ASSERT_EQ(sparse_container->MaximumDiskSize(), kMaxSize);
  ASSERT_TRUE(DestroySparse(0));

  std::unique_ptr<FvmContainer> fvmContainer;
  ASSERT_EQ(FvmContainer::CreateExisting(fvm_path, 0, &fvmContainer), ZX_OK);

  // The amount of space needed by the FVM should be smaller than its max disk size.
  // kMaxSize == actual disk size > minimum disk size
  ASSERT_EQ(fvmContainer->GetDiskSize(), kMaxSize);
  ASSERT_GT(fvmContainer->GetDiskSize(), fvmContainer->CalculateDiskSize());

  ASSERT_TRUE(DestroyFvm());
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

bool Setup(uint32_t num_dirs, uint32_t num_files, uint32_t max_size) {
  BEGIN_HELPER;
  // Generate test directory
  srand(static_cast<unsigned int>(time(0)));
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

  // Create and populate partitions
  ASSERT_TRUE(CreatePartitions());
  ASSERT_TRUE(PopulatePartitions(num_dirs, num_files, max_size));
  END_HELPER;
}

bool Cleanup() {
  BEGIN_HELPER;
  ASSERT_TRUE(DestroyPartitions());

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

#define RUN_FOR_ALL_TYPES(slice_size)                            \
  RUN_TEST_MEDIUM((TestPartitions<SPARSE, slice_size>))          \
  RUN_TEST_MEDIUM((TestPartitions<SPARSE_LZ4, slice_size>))      \
  RUN_TEST_MEDIUM((TestPartitions<FVM, slice_size>))             \
  RUN_TEST_MEDIUM((TestPartitions<FVM_NEW, slice_size>))         \
  RUN_TEST_MEDIUM((TestPartitions<FVM_OFFSET, slice_size>))      \
  RUN_TEST_MEDIUM((TestDiskSizeCalculation<SPARSE, slice_size>)) \
  RUN_TEST_MEDIUM((TestDiskSizeCalculation<SPARSE_LZ4, slice_size>))

#define RUN_RESERVATION_TEST_FOR_ALL_TYPES(slice_size, should_pass, data, inodes, limit)           \
  RUN_TEST_MEDIUM((TestPartitionsFailures<SPARSE, slice_size, should_pass, data, inodes, limit>))  \
  RUN_TEST_MEDIUM(                                                                                 \
      (TestPartitionsFailures<SPARSE_LZ4, slice_size, should_pass, data, inodes, limit>))          \
  RUN_TEST_MEDIUM((TestPartitionsFailures<FVM, slice_size, should_pass, data, inodes, limit>))     \
  RUN_TEST_MEDIUM((TestPartitionsFailures<FVM_NEW, slice_size, should_pass, data, inodes, limit>)) \
  RUN_TEST_MEDIUM(                                                                                 \
      (TestPartitionsFailures<FVM_OFFSET, slice_size, should_pass, data, inodes, limit>))

#define RUN_ALL_SPARSE(create_type, size_type, slice_size)                \
  RUN_TEST_MEDIUM((TestPave<create_type, size_type, SPARSE, slice_size>)) \
  RUN_TEST_MEDIUM((TestPave<create_type, size_type, SPARSE_LZ4, slice_size>))

#define RUN_ALL_PAVE(slice_size)                                            \
  RUN_ALL_SPARSE(PaveCreateType::kBefore, PaveSizeType::kSmall, slice_size) \
  RUN_ALL_SPARSE(PaveCreateType::kBefore, PaveSizeType::kExact, slice_size) \
  RUN_ALL_SPARSE(PaveCreateType::kBefore, PaveSizeType::kLarge, slice_size) \
  RUN_ALL_SPARSE(PaveCreateType::kOffset, PaveSizeType::kSmall, slice_size) \
  RUN_ALL_SPARSE(PaveCreateType::kOffset, PaveSizeType::kExact, slice_size) \
  RUN_ALL_SPARSE(PaveCreateType::kOffset, PaveSizeType::kLarge, slice_size)

// TODO(planders): add tests for FVM on GPT (with offset)
BEGIN_TEST_CASE(fvm_host_tests)
RUN_FOR_ALL_TYPES(8192)
RUN_FOR_ALL_TYPES(DEFAULT_SLICE_SIZE)
RUN_TEST_MEDIUM(TestCompressorBufferTooSmall)
RUN_ALL_PAVE(8192)
RUN_ALL_PAVE(DEFAULT_SLICE_SIZE)
RUN_TEST_MEDIUM(TestPaveZxcryptFail)
RUN_TEST_MEDIUM(TestExtendChangesMetadataSize)
RUN_TEST_MEDIUM(CreateExistingSparseFails)
RUN_TEST_MEDIUM(CreateExistingFvmFails)
RUN_TEST_MEDIUM(RecreateSparseWithDifferentSliceSize);
RUN_TEST_MEDIUM(RecreateFvmWithDifferentSliceSize);

// Too small total limit for inodes. Expect failure
RUN_RESERVATION_TEST_FOR_ALL_TYPES(8192, false, 1, 0, 10)

// Too small total limit for 100 bytes of data
RUN_RESERVATION_TEST_FOR_ALL_TYPES(8192, false, 0, 1000, 999)

// Too small limit for data + inodes
RUN_RESERVATION_TEST_FOR_ALL_TYPES(DEFAULT_SLICE_SIZE, false, 200, 10, 1000)

// Limitless capacity for 10 inodes and 100 bytes
RUN_RESERVATION_TEST_FOR_ALL_TYPES(8192, true, 10, 100, 0)

// Creating large total_bytes partition leads to increased test run time.
// Keep the total_bytes within certain limit.
RUN_RESERVATION_TEST_FOR_ALL_TYPES(8192, true, 100, 10, 300 * 1024 * 1024)

// Limitless capacity for 10k inodes and 10k bytes of data
RUN_RESERVATION_TEST_FOR_ALL_TYPES(DEFAULT_SLICE_SIZE, true, 10000, 1024 * 10, 0)

RUN_TEST_MEDIUM(TestCreatePreallocatedSparseImage)
RUN_TEST_MEDIUM(TestCreatePreallocatedSparseImageExceedMaxSize)
RUN_TEST_MEDIUM(TestPavePreallocatedSparseImage)

END_TEST_CASE(fvm_host_tests)

int main(int argc, char** argv) {
#ifdef __APPLE__
  // TODO(FLK-259): Re-enable tests once the cause of timeout has been determined.
  printf("Skipping tests\n");
  return 0;
#else
  // TODO(planders): Allow file settings to be passed in via command line.
  if (!Setup(kDefaultNumDirs, kDefaultNumFiles, kDefaultMaxSize)) {
    return -1;
  }
  int result = unittest_run_all_tests(argc, argv) ? 0 : -1;
  if (!Cleanup()) {
    return -1;
  }
  return result;
#endif
}
