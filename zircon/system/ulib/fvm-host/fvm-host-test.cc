// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <memory>
#include <utility>

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/unique_fd.h>
#include <fvm-host/container.h>
#include <fvm/sparse-reader.h>
#include <zxtest/zxtest.h>

#include "src/storage/minfs/host.h"

// NOTES ABOUT DISABLED TESTS
//
// This test used to create very large container files (2GB). This combined with so many variants
// resulted in it taking more than 20 minutes to run (bug 37779) and it was disabled. It bitrotted
// severely and and most of the tests now fail.
//
// To get better coverage, the passing tests are enabled with a much smaller container, partition,
// slice, and file sizes than originally. But most of the tests still fail and are commented out.
//
// TODO(bug 38188) fix the disabled tests.

namespace {

constexpr uint64_t kBytesPerMB = 1ull << 20;

constexpr uint64_t kDefaultSliceSize = kBytesPerMB / 2;
constexpr uint64_t kPartitionSize = 8 * kBytesPerMB;
constexpr uint64_t kContainerSize = 128 * kBytesPerMB;

constexpr size_t kMaxPartitions = 6;

static constexpr char kEmptyString[] = "";

static constexpr uint32_t kDefaultNumDirs = 10;
static constexpr uint32_t kDefaultNumFiles = 10;
static constexpr uint32_t kDefaultMaxSize = 16385;

enum class FsType {
  kMinFs,
  kBlobFs,
};

enum class GuidType {
  kData,
  kDataUnsafe,
  kSystem,
  kBlobStore,
  kDefault,
};

enum class ContainerType {
  kSparse,         // Sparse container.
  kSparseLZ4,      // Sparse container compressed with LZ4.
  kSparseZxCrypt,  // Sparse container to be stored on a zxcrypt volume.
  kFvm,            // Explicitly created FVM container.
  kFvmNew,         // FVM container created on FvmContainer::Create.
  kFvmOffset,      // FVM container created at an offset within a file.
};

struct Partition {
  FsType fs_type;
  GuidType guid_type;
  char path[PATH_MAX];
  bool created = false;
  FvmReservation reserve;
  zx_status_t status;

  const char* FsTypeName() {
    switch (fs_type) {
      case FsType::kMinFs:
        return kMinfsName;
      case FsType::kBlobFs:
        return kBlobfsName;
      default:
        return kEmptyString;
    }
  }

  const char* GuidTypeName() {
    switch (guid_type) {
      case GuidType::kData:
        return kDataTypeName;
      case GuidType::kDataUnsafe:
        return kDataUnsafeTypeName;
      case GuidType::kSystem:
        return kSystemTypeName;
      case GuidType::kBlobStore:
        return kBlobTypeName;
      case GuidType::kDefault:
        return kDefaultTypeName;
      default:
        return kEmptyString;
    }
  }

  void GeneratePath(char* dir) { sprintf(path, "%s%s_%s.bin", dir, FsTypeName(), GuidTypeName()); }
};

size_t ComputeRequiredDataSize(const std::unique_ptr<FvmContainer>& container) {
  // Make use of the CalculateDiskSize() method to compute the required data size.
  // The required data size is one that does not include the header size and extended part.
  size_t minimal_disk_size = container->CalculateDiskSize();
  fvm::Header header =
      fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, minimal_disk_size, kDefaultSliceSize);
  return minimal_disk_size - header->GetDataStartOffset();
}

class FvmHostTest : public zxtest::Test {
 public:
  void SetUp() override {
    // Generate test directory.
    srand(static_cast<unsigned int>(time(0)));
    GenerateDirectory("/tmp/", 20, test_dir);
    ASSERT_EQ(0, mkdir(test_dir, 0755));

    // Generate partition paths
    partition_count = 0;
    GeneratePartitionPath(FsType::kMinFs, GuidType::kData);
    GeneratePartitionPath(FsType::kMinFs, GuidType::kDataUnsafe);
    GeneratePartitionPath(FsType::kMinFs, GuidType::kSystem);
    GeneratePartitionPath(FsType::kMinFs, GuidType::kDefault);
    GeneratePartitionPath(FsType::kBlobFs, GuidType::kBlobStore);
    GeneratePartitionPath(FsType::kBlobFs, GuidType::kDefault);
    ASSERT_EQ(partition_count, kMaxPartitions);

    // Generate container paths
    sprintf(sparse_path, "%ssparse.bin", test_dir);
    sprintf(sparse_lz4_path, "%ssparse.bin.lz4", test_dir);
    sprintf(fvm_path, "%sfvm.bin", test_dir);

    // Create and populate partitions
    CreatePartitions();
    PopulatePartitions(kDefaultNumDirs, kDefaultNumFiles, kDefaultMaxSize);
  }

  void TearDown() override {
    DestroyPartitions();

    DIR* dir = opendir(test_dir);
    ASSERT_TRUE(dir);

    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
      if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
        continue;

      ASSERT_EQ(0, unlinkat(dirfd(dir), de->d_name, 0));
    }

    EXPECT_EQ(0, closedir(dir));
    ASSERT_EQ(0, rmdir(test_dir));
  }

 protected:
  void CreatePartitions() {
    for (unsigned i = 0; i < partition_count; i++) {
      Partition* part = &partitions[i];
      switch (part->fs_type) {
        case FsType::kMinFs:
          CreateMinfs(part->path);
          break;
        case FsType::kBlobFs:
          CreateBlobfs(part->path);
          break;
        default:
          ASSERT_TRUE(false);  // Unknown file system type.
      }

      part->created = true;
    }
  }

  // Adds all created partitions to |container|. Asserts on failures.
  void AddPartitions(Container* container, bool enable_data, bool should_pass) {
    AddPartitionsReserve(container, enable_data);
    for (unsigned i = 0; i < partition_count; i++) {
      Partition* part = &partitions[i];
      if (part->created) {
        bool added = part->status == ZX_OK;
        bool reserved = part->reserve.Approved();
        if ((added && reserved) != should_pass) {
          part->reserve.Dump(stderr);
        }
        ASSERT_EQ(added && reserved, should_pass);
      }
    }
  }

  void DestroyPartitions() {
    for (unsigned i = 0; i < partition_count; i++) {
      Partition* part = &partitions[i];
      if (part->created) {
        EXPECT_EQ(0, unlink(part->path));
        part->created = false;
        // Reset reservations for next iteration of the test.
        part->reserve = FvmReservation({}, {}, {});
      }
    }
  }

  void GeneratePartitionPath(FsType fs_type, GuidType guid_type) {
    ASSERT_LT(partition_count, kMaxPartitions);

    // Make sure we have not already created a partition with the same fs/guid type combo.
    for (unsigned i = 0; i < partition_count; i++) {
      Partition* part = &partitions[i];
      ASSERT_FALSE(part->fs_type == fs_type && part->guid_type == guid_type);
    }

    Partition* part = &partitions[partition_count++];
    part->fs_type = fs_type;
    part->guid_type = guid_type;
    part->GeneratePath(test_dir);
  }

  void GenerateData(size_t len, std::unique_ptr<uint8_t[]>* out) {
    // Fill a test buffer with data
    fbl::AllocChecker ac;
    std::unique_ptr<uint8_t[]> data(new (&ac) uint8_t[len]);
    ASSERT_TRUE(ac.check());

    for (unsigned n = 0; n < len; n++) {
      data[n] = static_cast<uint8_t>(rand());
    }

    *out = std::move(data);
  }

  void AddDirectoryMinfs(const char* path) { ASSERT_EQ(0, emu_mkdir(path, 0755)); }

  void AddFileMinfs(const char* path, size_t size) {
    int fd = emu_open(path, O_RDWR | O_CREAT, 0644);
    ASSERT_GT(fd, 0);
    std::unique_ptr<uint8_t[]> data;
    GenerateData(size, &data);

    ssize_t result = emu_write(fd, data.get(), size);
    ASSERT_GE(result, 0);
    ASSERT_EQ(size, static_cast<size_t>(result));
    ASSERT_EQ(0, emu_close(fd));
  }

  void PopulateMinfs(const char* path, size_t ndirs, size_t nfiles, size_t max_size) {
    ASSERT_EQ(0, emu_mount(path));
    fbl::Vector<fbl::String> paths;
    paths.push_back(fbl::String("::"));
    size_t total_size = 0;

    for (size_t i = 0; i < ndirs; i++) {
      const char* base_dir = paths[rand() % paths.size()].data();
      char new_dir[PATH_MAX];
      GenerateDirectory(base_dir, 10, new_dir);
      AddDirectoryMinfs(new_dir);
      paths.push_back(fbl::String(new_dir));
    }

    for (size_t i = 0; i < nfiles; i++) {
      const char* base_dir = paths[rand() % paths.size()].data();
      size_t size = 1 + (rand() % max_size);
      total_size += size;
      char new_file[PATH_MAX];
      GenerateFilename(base_dir, 10, new_file);
      AddFileMinfs(new_file, size);
    }

    uint64_t used_data, used_inodes, used_size;
    ASSERT_EQ(0, emu_get_used_resources(path, &used_data, &used_inodes, &used_size));

    // Used data should be greater than or equal to total size of the data we added
    ASSERT_GE(used_data, total_size);

    // Some fs use inodes for internal structures (including root directory).
    // So used_nodes should be gt total files+directories created.
    ASSERT_GE(used_inodes, nfiles + ndirs);

    // Used size should be always greater than used data.
    ASSERT_GT(used_size, used_data);
  }

  void AddFileBlobfs(blobfs::Blobfs* bs, size_t size) {
    char new_file[PATH_MAX];
    GenerateFilename(test_dir, 10, new_file);

    fbl::unique_fd datafd(open(new_file, O_RDWR | O_CREAT | O_EXCL, 0755));
    ASSERT_TRUE(datafd);

    std::unique_ptr<uint8_t[]> data;
    GenerateData(size, &data);

    ssize_t result = write(datafd.get(), data.get(), size);
    ASSERT_GE(result, 0);
    ASSERT_EQ(static_cast<size_t>(result), size);
    ASSERT_OK(blobfs::blobfs_add_blob(bs, nullptr, datafd.get()));
    ASSERT_EQ(0, unlink(new_file));
  }

  void PopulateBlobfs(const char* path, size_t nfiles, size_t max_size) {
    fbl::unique_fd blobfd(open(path, O_RDWR, 0755));
    ASSERT_TRUE(blobfd);

    std::unique_ptr<blobfs::Blobfs> bs;
    ASSERT_OK(blobfs::blobfs_create(&bs, blobfd.duplicate()));

    size_t total_size = 0;
    for (unsigned i = 0; i < nfiles; i++) {
      size_t size = 1 + (rand() % max_size);
      AddFileBlobfs(bs.get(), size);
      total_size += size;
    }
    uint64_t used_data, used_inodes, used_size;

    // Used data should be greater than or equal to total size of the data we added
    ASSERT_OK(blobfs::UsedDataSize(blobfd, &used_data));
    ASSERT_GE(used_data, total_size);

    // Blobfs uses inodes for internal structures (including file extents).
    // So used_nodes should be greater than or equal to total files+directories created.
    ASSERT_OK(blobfs::UsedInodes(blobfd, &used_inodes));
    ASSERT_GE(used_inodes, nfiles);

    // Used size should be always greater than used data.
    ASSERT_OK(blobfs::UsedSize(blobfd, &used_size));
    ASSERT_GE(used_size, used_data);
  }

  void PopulatePartitions(size_t ndirs, size_t nfiles, size_t max_size) {
    for (unsigned i = 0; i < partition_count; i++) {
      Partition* part = &partitions[i];
      if (!part->created)
        continue;

      switch (part->fs_type) {
        case FsType::kMinFs:
          PopulateMinfs(part->path, ndirs, nfiles, max_size);
          break;
        case FsType::kBlobFs:
          PopulateBlobfs(part->path, nfiles, max_size);
          break;
        default:
          ASSERT_TRUE(false);
      }
    }
  }

  void DestroySparse(uint32_t flags) {
    if ((flags & fvm::kSparseFlagLz4) != 0) {
      ASSERT_EQ(0, unlink(sparse_lz4_path));
    } else {
      ASSERT_EQ(0, unlink(sparse_path));
    }
  }

  void DestroyFvm() { ASSERT_EQ(0, unlink(fvm_path)); }

  void GetSparseInfo(ContainerType type, uint32_t* out_flags, char** out_path) {
    switch (type) {
      case ContainerType::kSparse: {
        *out_flags = 0;
        *out_path = sparse_path;
        break;
      }
      case ContainerType::kSparseLZ4: {
        *out_flags = fvm::kSparseFlagLz4;
        *out_path = sparse_lz4_path;
        break;
      }
      case ContainerType::kSparseZxCrypt: {
        *out_flags = fvm::kSparseFlagZxcrypt;
        *out_path = sparse_path;
        break;
      }
      default:
        ASSERT_TRUE(false);
    }
  }

  void CreateReportDestroy(ContainerType type, size_t slice_size, bool test_success = true,
                           std::optional<uint64_t> data_size = {},
                           std::optional<uint64_t> inodes_count = {},
                           std::optional<uint64_t> limit = {}) {
    for (unsigned i = 0; i < partition_count; i++) {
      Partition* part = &partitions[i];
      part->reserve = FvmReservation(inodes_count, data_size, limit);
    }
    switch (type) {
      case ContainerType::kSparse:
        __FALLTHROUGH;
      case ContainerType::kSparseLZ4:
        __FALLTHROUGH;
      case ContainerType::kSparseZxCrypt: {
        uint32_t flags;
        char* path;
        GetSparseInfo(type, &flags, &path);
        CreateSparse(flags, slice_size, test_success);
        if (test_success) {
          ReportSparse(flags);
        }
        DestroySparse(flags);
        break;
      }
      case ContainerType::kFvm: {
        CreateFvm(true, 0, slice_size, test_success);
        if (test_success) {
          ReportFvm();
          ExtendFvm(kContainerSize * 2);
          ReportFvm();
        }
        DestroyFvm();
        break;
      }
      case ContainerType::kFvmNew: {
        CreateFvm(false, 0, slice_size, test_success);
        if (test_success) {
          ReportFvm();
          ExtendFvm(kContainerSize * 2);
          ReportFvm();
        }
        DestroyFvm();
        break;
      }
      case ContainerType::kFvmOffset: {
        CreateFvm(true, kDefaultSliceSize, slice_size, test_success);
        if (test_success) {
          ReportFvm(kDefaultSliceSize);
        }
        DestroyFvm();
        break;
      }
      default: {
        ASSERT_TRUE(false);
      }
    }
  }

  void CreateFile(const char* path, size_t size) {
    int r = open(path, O_RDWR | O_CREAT | O_EXCL, 0755);
    ASSERT_GE(r, 0);
    ASSERT_EQ(0, ftruncate(r, size));
    ASSERT_EQ(0, close(r));
  }

  void CreateMinfs(const char* path) {
    CreateFile(path, kPartitionSize);
    ASSERT_EQ(0, emu_mkfs(path));
  }

  void CreateBlobfs(const char* path) {
    int r = open(path, O_RDWR | O_CREAT | O_EXCL, 0755);
    ASSERT_GE(r, 0);
    ASSERT_EQ(0, ftruncate(r, kPartitionSize));

    uint64_t block_count;
    ASSERT_OK(blobfs::GetBlockCount(r, &block_count));
    ASSERT_OK(blobfs::Mkfs(r, block_count));
    ASSERT_EQ(0, close(r));
  }

  // Adds all create partitions to |container|. If enable_data is false, the DATA partition is
  // skipped. This is to avoid discrepancies in disk size calculation due to zxcrypt not being
  // implemented on host.
  // Stores success or failure of each AddPartition in part->status.
  // TODO(planders): Once we are able to create zxcrypt'd FVM images on host, remove enable_data
  // flag.
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
      Partition* part = &partitions[order[i]];

      if (!enable_data && !strcmp(part->GuidTypeName(), kDataTypeName)) {
        continue;
      }

      if (part->created) {
        part->status = container->AddPartition(part->path, part->GuidTypeName(), &part->reserve);
      }
    }
  }

  // Creates a sparse container and adds partitions to it. When should_pass is false,
  // the function surfaces the error in adding partition to caller without asserting.
  void CreateSparse(uint32_t flags, size_t slice_size, bool should_pass, bool enable_data = true,
                    uint64_t max_disk_size = 0) {
    const char* path = ((flags & fvm::kSparseFlagLz4) != 0) ? sparse_lz4_path : sparse_path;

    std::unique_ptr<SparseContainer> sparseContainer;
    ASSERT_OK(SparseContainer::CreateNew(path, slice_size, flags, max_disk_size, &sparseContainer));
    AddPartitions(sparseContainer.get(), enable_data, should_pass);
    if (should_pass) {
      ASSERT_OK(sparseContainer->Commit());
      if (max_disk_size > 0) {
        ASSERT_EQ(sparseContainer->MaximumDiskSize(), max_disk_size);
      }

      uint64_t data_size = 0, inode_count = 0, used_size = 0;
      if ((flags & fvm::kSparseFlagLz4) == 0) {
        ASSERT_OK(sparseContainer->UsedSize(&used_size));
        ASSERT_NE(used_size, 0);
        ASSERT_OK(sparseContainer->UsedDataSize(&data_size));
        ASSERT_NE(data_size, 0);
        ASSERT_GT(used_size, data_size);
        ASSERT_OK(sparseContainer->UsedInodes(&inode_count));
        ASSERT_NE(inode_count, 0);
      } else {
        ASSERT_NOT_OK(sparseContainer->UsedSize(&used_size));
        ASSERT_NOT_OK(sparseContainer->UsedDataSize(&data_size));
        ASSERT_NOT_OK(sparseContainer->UsedInodes(&inode_count));
      }
    }
  }

  void CreateSparseEnsure(uint32_t flags, size_t slice_size, bool enable_data = true) {
    CreateSparse(flags, slice_size, true, enable_data);
  }

  void StatFile(const char* path, off_t* length) {
    fbl::unique_fd fd(open(path, O_RDWR, 0755));
    ASSERT_TRUE(fd);

    struct stat s;
    ASSERT_EQ(0, fstat(fd.get(), &s));
    *length = s.st_size;
  }

  void ReportContainer(const char* path, off_t offset) {
    std::unique_ptr<Container> container;
    ASSERT_OK(Container::Create(path, offset, 0, &container));
    ASSERT_OK(container->Verify());
  }

  void ReportSparse(uint32_t flags) {
    if ((flags & fvm::kSparseFlagLz4) != 0) {
      std::unique_ptr<SparseContainer> compressedContainer;
      ASSERT_OK(SparseContainer::CreateExisting(sparse_lz4_path, &compressedContainer));
      ASSERT_OK(compressedContainer->Decompress(sparse_path));
    }

    ReportContainer(sparse_path, 0);

    // Check that the calculated disk size passes inspection, but any size lower doesn't.
    std::unique_ptr<SparseContainer> container;
    ASSERT_OK(SparseContainer::CreateExisting(sparse_path, &container));

    size_t expected_size = container->CalculateDiskSize();
    ASSERT_OK(container->CheckDiskSize(expected_size));
    ASSERT_NOT_OK(container->CheckDiskSize(expected_size - 1));
  }

  // Creates a fvm container and adds partitions to it. When should succeed is false,
  // the function surfaces the error in adding partition to caller without asserting.
  void CreateFvm(bool create_before, off_t offset, size_t slice_size, bool should_pass,
                 bool enable_data = true, std::unique_ptr<FvmContainer>* out = nullptr) {
    off_t length = 0;
    if (create_before) {
      CreateFile(fvm_path, kContainerSize);
      StatFile(fvm_path, &length);
    }

    std::unique_ptr<FvmContainer> fvmContainer;
    ASSERT_OK(
        FvmContainer::CreateNew(fvm_path, slice_size, offset, length - offset, &fvmContainer));
    AddPartitions(fvmContainer.get(), enable_data, should_pass);
    if (should_pass) {
      ASSERT_OK(fvmContainer->Commit());
    }
    if (out) {
      *out = std::move(fvmContainer);
    }
  }

  void CreateFvmEnsure(bool create_before, off_t offset, size_t slice_size,
                       bool enable_data = true) {
    CreateFvm(create_before, offset, slice_size, true, enable_data);
  }

  void ExtendFvm(off_t length) {
    std::unique_ptr<Container> container;
    ASSERT_OK(Container::Create(fvm_path, 0, 0, &container));
    ASSERT_OK(static_cast<FvmContainer*>(container.get())->Extend(length));
    off_t current_length;
    StatFile(fvm_path, &current_length);
    ASSERT_EQ(current_length, length);
  }

  void ReportFvm(off_t offset = 0) { ReportContainer(fvm_path, offset); }

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

  void TestPartitionsFailures(ContainerType container_type, size_t slice_size, bool test_success,
                              uint64_t data, uint64_t inodes, uint64_t size_limit) {
    std::optional<uint64_t> odata = data == 0 ? std::optional<uint64_t>{} : data;
    std::optional<uint64_t> osize_limit = size_limit == 0 ? std::optional<uint64_t>{} : size_limit;
    std::optional<uint64_t> oinodes = inodes == 0 ? std::optional<uint64_t>{} : inodes;

    CreateReportDestroy(container_type, slice_size, test_success, odata, oinodes, osize_limit);
  }

  void RunReservationTestForAllTypes(size_t slice_size, bool test_success, uint64_t data,
                                     uint64_t inodes, uint64_t limit) {
    TestPartitionsFailures(ContainerType::kSparse, slice_size, test_success, data, inodes, limit);
    TestPartitionsFailures(ContainerType::kSparseLZ4, slice_size, test_success, data, inodes,
                           limit);
    TestPartitionsFailures(ContainerType::kFvm, slice_size, test_success, data, inodes, limit);
    TestPartitionsFailures(ContainerType::kFvmNew, slice_size, test_success, data, inodes, limit);
    TestPartitionsFailures(ContainerType::kFvmOffset, slice_size, test_success, data, inodes,
                           limit);
  }

  char test_dir[PATH_MAX];
  char sparse_path[PATH_MAX];
  char sparse_lz4_path[PATH_MAX];
  char fvm_path[PATH_MAX];

  Partition partitions[kMaxPartitions] = {};
  uint32_t partition_count = 0;
};

#if 0  // TODO(bug 38188)
void VerifyFvmSize(size_t expected_size) {
  std::unique_ptr<FvmContainer> fvm_container;
  ASSERT_EQ(FvmContainer::CreateExisting(fvm_path, 0, &fvm_container), ZX_OK);
  size_t calculated_size = fvm_container->CalculateDiskSize();
  size_t actual_size = fvm_container->GetDiskSize();

  ASSERT_EQ(calculated_size, actual_size);
  ASSERT_EQ(actual_size, expected_size);
}

void TestDiskSizeCalculation(ContainerType container_type, size_t slice_size) {
  uint32_t flags;
  char* path;
  GetSparseInfo(container_type, &flags, &path);
  ASSERT_TRUE(CreateSparseEnsure(flags, slice_size, false /* enable_data */));
  ReportSparse(flags);

  std::unique_ptr<SparseContainer> sparseContainer;
  ASSERT_EQ(SparseContainer::CreateExisting(path, &sparseContainer), ZX_OK);

  size_t expected_size = sparseContainer->CalculateDiskSize();
  ASSERT_EQ(sparseContainer->CheckDiskSize(expected_size), ZX_OK);
  ASSERT_NE(sparseContainer->CheckDiskSize(expected_size - 1), ZX_OK);

  // Create an FVM using the same partitions and verify its size matches expected.
  ASSERT_TRUE(CreateFvmEnsure(false, 0, slice_size, false /* enable_data */));
  VerifyFvmSize(expected_size);
  DestroyFvm();

  // Create an FVM by paving the sparse file and verify its size matches expected.
  std::unique_ptr<fvm::host::UniqueFdWrapper> wrapper;
  ASSERT_EQ(fvm::host::UniqueFdWrapper::Open(fvm_path, O_RDWR | O_CREAT | O_EXCL, 0644, &wrapper),
            ZX_OK);
  ASSERT_EQ(sparseContainer->Pave(std::move(wrapper), 0, 0), ZX_OK);
  VerifyFvmSize(expected_size);
  DestroyFvm();

  DestroySparse(flags);
}
#endif

// Test to ensure that compression will fail if the buffer is too small.
#if 0  // TODO(bug 38188)
TEST_F(FvmHostTest, TestCompressorBufferTooSmall) {
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
}
#endif

enum class PaveSizeType {
  kSmall,  // Allocate disk space for paving smaller than what is required.
  kExact,  // Allocate exactly as much disk space as is required for a pave.
  kLarge,  // Allocate additional disk space beyond what is needed for pave.
};

enum class PaveCreateType {
  kBefore,  // Create FVM file before paving.
  kOffset,  // Create FVM at an offset within the file.
};

// Paving an FVM with a data partition will fail since we zxcrypt is not currently implemented on
// host.
// TODO(planders): Once we are able to create zxcrypt'd FVM images on host, remove this test.
TEST_F(FvmHostTest, TestPaveZxcryptFail) {
  CreateSparseEnsure(0, kDefaultSliceSize);
  std::unique_ptr<SparseContainer> sparseContainer;
  ASSERT_OK(SparseContainer::CreateExisting(sparse_path, &sparseContainer));

  std::unique_ptr<fvm::host::UniqueFdWrapper> wrapper;
  ASSERT_OK(fvm::host::UniqueFdWrapper::Open(fvm_path, O_RDWR | O_CREAT, 0644, &wrapper));
  ASSERT_NOT_OK(sparseContainer->Pave(std::move(wrapper), 0, 0));
  DestroySparse(0);
  ASSERT_EQ(unlink(fvm_path), 0);
}

TEST_F(FvmHostTest, TestFvmVerifyOK) {
  CreateFvm(true, 0, kDefaultSliceSize, true /* should_pass */, true);
  chmod(fvm_path, S_IRUSR);
  ASSERT_OK(FvmContainer::Verify(fvm_path, 0));
}

TEST_F(FvmHostTest, TestFvmVerifyFail) {
  CreateSparseEnsure(0, kDefaultSliceSize);
  ASSERT_NOT_OK(FvmContainer::Verify(sparse_path, 0));
}

TEST_F(FvmHostTest, TestCreateWithResizeImageFileToFit) {
  size_t offset = 4096;
  std::unique_ptr<FvmContainer> out;
  CreateFvm(true, offset, kDefaultSliceSize, true /* should_pass */, true, &out);
  ASSERT_OK(out->ResizeImageFileToFit());

  std::unique_ptr<FvmContainer> container;
  ASSERT_OK(FvmContainer::CreateExisting(fvm_path, offset, &container));
  auto required_data_size = ComputeRequiredDataSize(container);
  size_t expected_size = offset + required_data_size +
                         2 * fvm::MetadataSizeForDiskSize(kContainerSize, kDefaultSliceSize);
  off_t current_size;
  StatFile(fvm_path, &current_size);
  ASSERT_EQ(static_cast<size_t>(current_size), expected_size);
  DestroyFvm();
}

TEST_F(FvmHostTest, TestResizeImageFileToFitAfterExtend) {
  CreateFvm(true, 0, kDefaultSliceSize, true /* should_pass */, true);

  std::unique_ptr<FvmContainer> container;
  ASSERT_OK(FvmContainer::CreateExisting(fvm_path, 0, &container));
  ASSERT_OK(container->Extend(kContainerSize * 2));
  ASSERT_OK(container->ResizeImageFileToFit());
  auto required_data_size = ComputeRequiredDataSize(container);
  size_t expected_size =
      required_data_size + 2 * fvm::MetadataSizeForDiskSize(2 * kContainerSize, kDefaultSliceSize);

  off_t current_size;
  StatFile(fvm_path, &current_size);
  ASSERT_EQ(static_cast<size_t>(current_size), expected_size);
  DestroyFvm();
}

TEST_F(FvmHostTest, ExtendToSmallerThanCurrentSizeSucceedWithLowerBoundLength) {
  CreateFvm(true, 0, kDefaultSliceSize, true /* should_pass */, true);
  std::unique_ptr<FvmContainer> container;
  ASSERT_OK(FvmContainer::CreateExisting(fvm_path, 0, &container),
            "Failed to initialize fvm container");
  container->SetExtendLengthType(FvmContainer::ExtendLengthType::LOWER_BOUND);
  ASSERT_OK(container->Extend(kContainerSize - 1));
  DestroyFvm();
}

TEST_F(FvmHostTest, ExtendToSmallerThanCurrentSizeResizeImageFileSizeToDiskSize) {
  CreateFvm(true, 0, kDefaultSliceSize, true /* should_pass */, true);
  std::unique_ptr<FvmContainer> container;
  ASSERT_OK(FvmContainer::CreateExisting(fvm_path, 0, &container),
            "Failed to initialize fvm container");
  ASSERT_OK(container->Extend(kContainerSize * 2));
  ASSERT_OK(container->ResizeImageFileToFit());
  container->SetExtendLengthType(FvmContainer::ExtendLengthType::LOWER_BOUND);
  ASSERT_OK(container->Extend(2 * kContainerSize - 1));
  // Validate that extend will reset image file size to be equal to the disk size.
  off_t current_size;
  StatFile(fvm_path, &current_size);
  ASSERT_EQ(kContainerSize * 2, current_size);
  DestroyFvm();
}

namespace {
constexpr size_t kAndroidSparseBlockSize = 4096;

union ChunkData {
  uint32_t fill_val;
  const uint8_t* raw_data;
};

void ValidateAndroidSparseChunk(const fbl::unique_fd& fd, uint16_t chunk_type, uint32_t chunk_size,
                                const ChunkData& expected) {
  AndroidSparseChunkHeader chunk_header;
  ASSERT_EQ(read(fd.get(), &chunk_header, sizeof(chunk_header)), sizeof(chunk_header));
  ASSERT_EQ(chunk_header.chunk_type, chunk_type);
  ASSERT_EQ(chunk_header.chunk_blocks, chunk_size);
  uint32_t fill_val = 0;
  switch (chunk_type) {
    case kChunkTypeDontCare:
      ASSERT_EQ(chunk_header.total_size, sizeof(chunk_header));
      break;
    case kChunkTypeFill:
      ASSERT_EQ(chunk_header.total_size, sizeof(chunk_header) + sizeof(uint32_t));
      ASSERT_EQ(read(fd.get(), &fill_val, sizeof(fill_val)), sizeof(fill_val));
      ASSERT_EQ(fill_val, expected.fill_val);
      break;
    case kChunkTypeRaw:
      size_t data_size = chunk_header.chunk_blocks * kAndroidSparseBlockSize;
      ASSERT_EQ(chunk_header.total_size, sizeof(chunk_header) + data_size);
      std::vector<uint8_t> validate(data_size);
      ASSERT_EQ(read(fd.get(), validate.data(), data_size), data_size);
      ASSERT_BYTES_EQ(validate.data(), expected.raw_data, data_size);
      break;
  }
}
}  // namespace

TEST_F(FvmHostTest, ConverToAndroidSparseFormat) {
  uint8_t block_data[kAndroidSparseBlockSize];

  std::unique_ptr<FvmContainer> out;
  CreateFvm(true, 0, kDefaultSliceSize, true /* should_pass */, true, &out);
  size_t disk_size = out->GetDiskSize();
  size_t roundup_disk_size = fbl::round_up(disk_size, kAndroidSparseBlockSize);
  size_t superblock_size = 2 * fvm::MetadataSizeForDiskSize(disk_size, out->SliceSize());
  out.reset();
  // Modify the created fvm by writing custom data to test sparse image conversion logic.
  fbl::unique_fd fd(open(fvm_path, O_RDWR, 0644));
  ASSERT_TRUE(fd);
  // Avoid modifying the superblock. Otherwise it cannot be loaded.
  size_t roundup_super_block_size = fbl::round_up(superblock_size, kAndroidSparseBlockSize);
  // Make sure the fvm size is block aligned.
  ASSERT_EQ(ftruncate(fd.get(), roundup_disk_size), 0);
  // Write some new content
  ASSERT_EQ(lseek(fd.get(), roundup_super_block_size, SEEK_SET), roundup_super_block_size);
  // Write two fill blocks of fill value 0xab.
  memset(block_data, 0xab, kAndroidSparseBlockSize);
  ASSERT_EQ(write(fd.get(), block_data, kAndroidSparseBlockSize), kAndroidSparseBlockSize);
  ASSERT_EQ(write(fd.get(), block_data, kAndroidSparseBlockSize), kAndroidSparseBlockSize);
  // Write a fill block of fill value 0xcd.
  memset(block_data, 0xcd, kAndroidSparseBlockSize);
  ASSERT_EQ(write(fd.get(), block_data, kAndroidSparseBlockSize), kAndroidSparseBlockSize);
  // Write two raw blocks
  for (size_t i = 0; i < kAndroidSparseBlockSize; i++) {
    block_data[i] = i % 0xff;
  }
  ASSERT_EQ(write(fd.get(), block_data, kAndroidSparseBlockSize), kAndroidSparseBlockSize);
  ASSERT_EQ(write(fd.get(), block_data, kAndroidSparseBlockSize), kAndroidSparseBlockSize);
  fd.reset();
  roundup_disk_size =
      std::max(roundup_disk_size, roundup_super_block_size + 5 * kAndroidSparseBlockSize);
  return;
  // Create an FVM from it and covert to android sparse image.
  std::unique_ptr<FvmContainer> container;
  ASSERT_OK(FvmContainer::CreateExisting(fvm_path, 0, &container),
            "Failed to initialize fvm container");
  // Add non-empty segments info. Superblock is skipped to simplify the test, so that
  // we don't have to deal with the complicated data in it.
  container->AddNonEmptySegment(roundup_super_block_size,
                                roundup_super_block_size + 5 * kAndroidSparseBlockSize);
  ASSERT_OK(container->ConvertToAndroidSparseImage());
  container.reset();

  // Validate the image;
  fd.reset(open(fvm_path, O_RDWR, 0644));
  ASSERT_TRUE(fd);
  // Validate the header
  AndroidSparseHeader sparse_header;
  ASSERT_EQ(read(fd.get(), &sparse_header, sizeof(sparse_header)), sizeof(sparse_header));
  ASSERT_EQ(sparse_header.kMagic, kAndroidSparseHeaderMagic);
  ASSERT_EQ(sparse_header.kMajorVersion, 1);
  ASSERT_EQ(sparse_header.kMinorVersion, 0);
  ASSERT_EQ(sparse_header.file_header_size, sizeof(AndroidSparseHeader));
  ASSERT_EQ(sparse_header.chunk_header_size, sizeof(AndroidSparseChunkHeader));
  ASSERT_EQ(sparse_header.block_size, static_cast<uint32_t>(kAndroidSparseBlockSize));
  ASSERT_EQ(sparse_header.total_blocks, roundup_disk_size / kAndroidSparseBlockSize);
  // dont-care chunk, fill chunk 0xab, fill chunk 0xcd, raw chunk, remaining dont-care chunk.
  ASSERT_EQ(sparse_header.total_chunks, 5);
  ASSERT_EQ(sparse_header.image_checksum, 0);

  // Validate chunks
  ChunkData expected;
  // dont-care superblock chunk
  ASSERT_NO_FAILURES(ValidateAndroidSparseChunk(
      fd, kChunkTypeDontCare, roundup_super_block_size / kAndroidSparseBlockSize, expected));

  // Fill chunk 0xab
  expected.fill_val = 0xabababab;
  ASSERT_NO_FAILURES(ValidateAndroidSparseChunk(fd, kChunkTypeFill, 2, expected));

  // Fill chunk 0xcd
  expected.fill_val = 0xcdcdcdcd;
  ASSERT_NO_FAILURES(ValidateAndroidSparseChunk(fd, kChunkTypeFill, 1, expected));

  // Raw chunk
  std::vector<uint8_t> expected_raw(2 * kAndroidSparseBlockSize);
  for (size_t i = 0; i < kAndroidSparseBlockSize; i++) {
    expected_raw[i] = i % 0xff;
    expected_raw[i + kAndroidSparseBlockSize] = expected_raw[i];
  }
  expected.raw_data = expected_raw.data();
  ASSERT_NO_FAILURES(ValidateAndroidSparseChunk(fd, kChunkTypeRaw, 2, expected));

  // The rest (if there is any) is dont-care chunk
  size_t remaining = roundup_disk_size - roundup_super_block_size - 5 * kAndroidSparseBlockSize;
  if (remaining) {
    ASSERT_NO_FAILURES(ValidateAndroidSparseChunk(fd, kChunkTypeDontCare,
                                                  remaining / kAndroidSparseBlockSize, expected));
  }
}

TEST_F(FvmHostTest, CompressWithLZ4) {
  std::unique_ptr<FvmContainer> out;
  CreateFvm(true, 0, kDefaultSliceSize, true /* should_pass */, true, &out);
  ASSERT_OK(out->CompressWithLZ4());
  // Validate magic value in the lz4 frame header.
  fbl::unique_fd fd(open(fvm_path, O_RDONLY, 0644));
  ASSERT_TRUE(fd);
  uint32_t magic;
  ASSERT_EQ(read(fd.get(), &magic, sizeof(magic)), sizeof(magic));
  ASSERT_EQ(magic, 0x184D2204);
}

TEST_F(FvmHostTest, DecompressLZ4) {
  std::unique_ptr<FvmContainer> out;
  CreateFvm(true, 0, kDefaultSliceSize, true /* should_pass */, true, &out);
  ASSERT_OK(out->CompressWithLZ4());
  out.reset();
  // Decompress to a file using path |sparse_path|.
  ASSERT_OK(fvm::SparseReader::DecompressLZ4File(fvm_path, sparse_path));
  // Load the fvm from the decompressed file
  std::unique_ptr<FvmContainer> fvm_container;
  ASSERT_OK(FvmContainer::CreateExisting(sparse_path, 0, &fvm_container));

  // Compare that the decompressed image is the same as the original image.
  // |fvm_path| is now a compressed image, need to recreate it.
  DestroyFvm();
  CreateFvm(true, 0, kDefaultSliceSize, true /* should_pass */, true, &out);

  off_t original, decompressed;
  StatFile(fvm_path, &original);
  StatFile(sparse_path, &decompressed);
  ASSERT_EQ(original, decompressed);

  fbl::unique_fd fd_original(open(fvm_path, O_RDONLY, 0644));
  ASSERT_TRUE(fd_original);
  fbl::unique_fd fd_decompressed(open(fvm_path, O_RDONLY, 0644));
  ASSERT_TRUE(fd_decompressed);

  std::vector<uint8_t> original_data(original), decompressed_data(decompressed);
  ASSERT_EQ(read(fd_original.get(), original_data.data(), original), original);
  ASSERT_EQ(read(fd_decompressed.get(), decompressed_data.data(), decompressed), decompressed);
  ASSERT_BYTES_EQ(original_data.data(), decompressed_data.data(), original);

  DestroyFvm();
}

// Test extend with values that ensure the FVM metadata size will increase.
#if 0  // TODO(bug 38188)
constexpr size_t CalculateExtendedContainerSize(const size_t initial_container_size,
                                                const size_t extended_container_size) {
  const size_t initial_metadata_size = fvm::MetadataSizeForDiskSize(initial_container_size, kDefaultSliceSize);
  const size_t extended_metadata_size =
      fvm::MetadataSizeForDiskSize(extended_container_size, kDefaultSliceSize);

  if (extended_metadata_size == initial_metadata_size) {
    return CalculateExtendedContainerSize(initial_container_size, extended_container_size * 2);
  }

  return extended_container_size;
}

TEST_F(FvmHostTest, TestExtendChangesMetadataSize) {
  CreateFvm(true, 0, kDefaultSliceSize, true /* should_pass */);
  size_t extended_container_size = CalculateExtendedContainerSize(kContainerSize, kContainerSize);
  ASSERT_GT(fvm::MetadataSizeForDiskSize(extended_container_size, kDefaultSliceSize),
            fvm::MetadataSizeForDiskSize(kContainerSize, kDefaultSliceSize));
  ExtendFvm(extended_container_size);
  ReportFvm();
  DestroyFvm();
}
#endif

// Attempts to create a SparseContainer from an existing sparse image when one does not exist.
TEST_F(FvmHostTest, CreateExistingSparseFails) {
  std::unique_ptr<SparseContainer> sparseContainer;
  ASSERT_NOT_OK(SparseContainer::CreateExisting(sparse_path, &sparseContainer));
}

TEST_F(FvmHostTest, CreateExistingFvmFails) {
  std::unique_ptr<FvmContainer> fvmContainer;
  ASSERT_NOT_OK(FvmContainer::CreateExisting(fvm_path, 0, &fvmContainer));
}

// Attempts to re-create a sparse image at the same path with a different slice size, verifying
// that the slice size is updated.
TEST_F(FvmHostTest, RecreateSparseWithDifferentSliceSize) {
  std::unique_ptr<SparseContainer> sparseContainer;

  CreateSparse(0, 8192, true);
  ASSERT_OK(SparseContainer::CreateExisting(sparse_path, &sparseContainer));
  ASSERT_EQ(sparseContainer->SliceSize(), 8192);

  CreateSparse(0, kDefaultSliceSize, true);
  ASSERT_OK(SparseContainer::CreateExisting(sparse_path, &sparseContainer));
  ASSERT_EQ(sparseContainer->SliceSize(), kDefaultSliceSize);

  DestroySparse(0);
}

TEST_F(FvmHostTest, RecreateFvmWithDifferentSliceSize) {
  std::unique_ptr<FvmContainer> fvmContainer;

  // Create FVM with the larger slice size first, since this will result in a larger container
  // size. Newly created FVM's will use the current container size if it already exists, so
  // creation of this container will fail if a smaller one already exists.
  // This is not an issue with the sparse test since the container is created from scratch every
  // time.
  CreateFvm(false, 0, kDefaultSliceSize, true);
  ASSERT_OK(FvmContainer::CreateExisting(fvm_path, 0, &fvmContainer));
  ASSERT_EQ(fvmContainer->SliceSize(), kDefaultSliceSize);

  CreateFvm(false, 0, 8192, true);
  ASSERT_OK(FvmContainer::CreateExisting(fvm_path, 0, &fvmContainer));
  ASSERT_EQ(fvmContainer->SliceSize(), 8192);

  DestroyFvm();
}

#if 0  // TODO(bug 38188)
TEST_F(FvmHostTest, TestCreatePreallocatedSparseImage) {
  constexpr uint64_t kMaxSize = 35ull << 30;
  CreateSparse(0, kDefaultSliceSize, true, true, kMaxSize);
  std::unique_ptr<SparseContainer> sparse_container;
  ASSERT_OK(SparseContainer::CreateExisting(sparse_path, &sparse_container));

  std::unique_ptr<fvm::host::UniqueFdWrapper> wrapper;
  ASSERT_OK(fvm::host::UniqueFdWrapper::Open(sparse_path, O_RDWR | O_CREAT, 0644, &wrapper));
  ASSERT_OK(sparse_container->Pave(std::move(wrapper), 0, 0));
  ASSERT_EQ(sparse_container->MaximumDiskSize(), kMaxSize);
  DestroySparse(0);
}
#endif

#if 0  // TODO(bug 38188)
TEST_F(FvmHostTest, TestCreatePreallocatedSparseImageExceedMaxSize) {
  constexpr uint64_t kMaxSize = sizeof(fvm::Header);
  CreateSparse(0, kDefaultSliceSize, true, true, kMaxSize);
  DestroySparse(0);
}
#endif

#if 0  // TODO(bug 38188)
TEST_F(FvmHostTest, TestPavePreallocatedSparseImage) {
  constexpr uint64_t kMaxSize = kContainerSize;
  CreateSparse(0, kDefaultSliceSize, true /* should_pass */, false /* enable_data */, kMaxSize);
  std::unique_ptr<SparseContainer> sparse_container;
  ASSERT_OK(SparseContainer::CreateExisting(sparse_path, &sparse_container));

  std::unique_ptr<fvm::host::UniqueFdWrapper> pave_wrapper;
  ASSERT_OK(fvm::host::UniqueFdWrapper::Open(fvm_path, O_RDWR | O_CREAT, 0644, &pave_wrapper));
  pave_wrapper->Truncate(kMaxSize);

  ASSERT_OK(sparse_container->Pave(std::move(pave_wrapper), 0, 0));
  ASSERT_EQ(sparse_container->MaximumDiskSize(), kMaxSize);
  DestroySparse(0);

  std::unique_ptr<FvmContainer> fvmContainer;
  ASSERT_OK(FvmContainer::CreateExisting(fvm_path, 0, &fvmContainer));

  // The amount of space needed by the FVM should be smaller than its max disk size.
  // kMaxSize == actual disk size > minimum disk size
  ASSERT_EQ(fvmContainer->GetDiskSize(), kMaxSize);
  ASSERT_GT(fvmContainer->GetDiskSize(), fvmContainer->CalculateDiskSize());

  DestroyFvm();
}
#endif

#if 0  // TODO(bug 38188)
void TestPartitions(ContainerType container_type, size_t slice_size) {
  CreateReportDestroy(container_type, slice_size);
}

TEST_F(FvmHostTest, Partitions) {
  // When this is fixed re-enabled, we don't need all these combinations of formats and sizes and
  // having this many tests slows things down. Evaluate some reasonable combinations of parameters
  // that give reasonable coverate.
  TestPartitions(ContainerType::kSparse, 8192);
  TestPartitions(ContainerType::kSparseLZ4, 8192);
  TestPartitions(FVM, 8192);
  TestPartitions(ContainerType::kFvmNew, 8192);
  TestPartitions(ContainerType::kFvmOffset, 8192);
  TestDiskSizeCalculation(ContainerType::kSparse, 8192);
  TestDiskSizeCalculation(ContainerType::kSparseLZ4, 8192);

  TestPartitions(ContainerType::kSparse, kDefaultSliceSize);
  TestPartitions(ContainerType::kSparseLZ4, kDefaultSliceSize);
  TestPartitions(FVM, kDefaultSliceSize);
  TestPartitions(ContainerType::kFvmNew, kDefaultSliceSize);
  TestPartitions(ContainerType::kFvmOffset, kDefaultSliceSize);
  TestDiskSizeCalculation(ContainerType::kSparse, kDefaultSliceSize);
  TestDiskSizeCalculation(ContainerType::kSparseLZ4, kDefaultSliceSize);
}
#endif

#if 0  // TODO(bug 38188)
// Creates a file at |fvm_path| to which an FVM is intended to be paved from an existing sparse
// file. The size of the file will depend on the |expected_size|, as well as the |create_type| and
// |size_type| options.
//
// The intended offset and allocated size for the paved FVM will be returned as |out_pave_offset|
// and |out_pave_size| respectively.
void CreatePaveFile(PaveCreateType create_type, PaveSizeType size_type, size_t expected_size,
                    size_t* out_pave_offset, size_t* out_pave_size) {
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
}

void TestPave(PaveCreateType CreateType, PaveSizeType SizeType, ContainerType container_type,
              size_t slice_size) {
  uint32_t sparse_flags;
  char* src_path;
  GetSparseInfo(container_type, &sparse_flags, &src_path);
  CreateSparseEnsure(sparse_flags, slice_size, false /* enable_data */);

  size_t pave_offset = 0;
  size_t pave_size = 0;

  std::unique_ptr<SparseContainer> sparseContainer;
  ASSERT_EQ(SparseContainer::CreateExisting(src_path, &sparseContainer), ZX_OK);
  size_t expected_size = sparseContainer->CalculateDiskSize();
  CreatePaveFile(CreateType, SizeType, expected_size, &pave_offset, &pave_size);

  std::unique_ptr<fvm::host::UniqueFdWrapper> wrapper;
  ASSERT_EQ(fvm::host::UniqueFdWrapper::Open(fvm_path, O_RDWR | O_CREAT, 0644, &wrapper), ZX_OK);

  if (SizeType == PaveSizeType::kSmall) {
    ASSERT_NE(sparseContainer->Pave(std::move(wrapper), pave_offset, pave_size), ZX_OK);
  } else {
    ASSERT_EQ(sparseContainer->Pave(std::move(wrapper), pave_offset, pave_size), ZX_OK);
    ReportFvm(pave_offset);
  }

  DestroyFvm();
  DestroySparse(sparse_flags);
}

TEST_F(FvmHostTest, Pave) {
  // When this is fixed re-enabled, we don't need all these combinations of formats and sizes and
  // having this many tests slows things down. Find some reasonable combinations of parameters
  // (maybe ~4 different ones?) that give reasonable coverate
  TestPave(PaveCreateType::kBefore, PaveSizeType::kSmall, ContainerType::kSparse, 8192);
  TestPave(PaveCreateType::kBefore, PaveSizeType::kSmall, ContainerType::kSparseLZ4, 8192);
  TestPave(PaveCreateType::kBefore, PaveSizeType::kExact, ContainerType::kSparse, 8192);
  TestPave(PaveCreateType::kBefore, PaveSizeType::kExact, ContainerType::kSparseLZ4, 8192);
  TestPave(PaveCreateType::kBefore, PaveSizeType::kLarge, ContainerType::kSparse, 8192);
  TestPave(PaveCreateType::kBefore, PaveSizeType::kLarge, ContainerType::kSparseLZ4, 8192);

  TestPave(PaveCreateType::kOffset, PaveSizeType::kSmall, ContainerType::kSparse, 8192);
  TestPave(PaveCreateType::kOffset, PaveSizeType::kSmall, ContainerType::kSparseLZ4, 8192);
  TestPave(PaveCreateType::kOffset, PaveSizeType::kExact, ContainerType::kSparse, 8192);
  TestPave(PaveCreateType::kOffset, PaveSizeType::kExact, ContainerType::kSparseLZ4, 8192);
  TestPave(PaveCreateType::kOffset, PaveSizeType::kLarge, ContainerType::kSparse, 8192);
  TestPave(PaveCreateType::kOffset, PaveSizeType::kLarge, ContainerType::kSparseLZ4, 8192);

  TestPave(PaveCreateType::kBefore, PaveSizeType::kSmall, ContainerType::kSparse,
           kDefaultSliceSize);
  TestPave(PaveCreateType::kBefore, PaveSizeType::kSmall, ContainerType::kSparseLZ4,
           kDefaultSliceSize);
  TestPave(PaveCreateType::kBefore, PaveSizeType::kExact, ContainerType::kSparse,
           kDefaultSliceSize);
  TestPave(PaveCreateType::kBefore, PaveSizeType::kExact, ContainerType::kSparseLZ4,
           kDefaultSliceSize);
  TestPave(PaveCreateType::kBefore, PaveSizeType::kLarge, ContainerType::kSparse,
           kDefaultSliceSize);
  TestPave(PaveCreateType::kBefore, PaveSizeType::kLarge, ContainerType::kSparseLZ4,
           kDefaultSliceSize);

  TestPave(PaveCreateType::kOffset, PaveSizeType::kSmall, ContainerType::kSparse,
           kDefaultSliceSize);
  TestPave(PaveCreateType::kOffset, PaveSizeType::kSmall, ContainerType::kSparseLZ4,
           kDefaultSliceSize);
  TestPave(PaveCreateType::kOffset, PaveSizeType::kExact, ContainerType::kSparse,
           kDefaultSliceSize);
  TestPave(PaveCreateType::kOffset, PaveSizeType::kExact, ContainerType::kSparseLZ4,
           kDefaultSliceSize);
  TestPave(PaveCreateType::kOffset, PaveSizeType::kLarge, ContainerType::kSparse,
           kDefaultSliceSize);
  TestPave(PaveCreateType::kOffset, PaveSizeType::kLarge, ContainerType::kSparseLZ4,
           kDefaultSliceSize);
}
#endif

// Too small total limit for inodes. Expect failure
TEST_F(FvmHostTest, TooSmallInodeLimit) { RunReservationTestForAllTypes(8192, false, 1, 0, 10); }

// Too small total limit for 100 bytes of data
TEST_F(FvmHostTest, TooSmallTotalLimit) {
  RunReservationTestForAllTypes(8192, false, 0, 1000, 999);
}

// Too small limit for data + inodes
TEST_F(FvmHostTest, TooSmallDataLimit) {
  RunReservationTestForAllTypes(kDefaultSliceSize, false, 200, 10, 1000);
}

#if 0  // TODO(bug 38188)
// Limitless capacity for 10 inodes and 100 bytes
TEST_F(FvmHostTest, LimitlessCapacity) {
  RunReservationTestForAllTypes(8192, true, 10, 100, 0);
}

// Creating large total_bytes partition leads to increased test run time.
// Keep the total_bytes within certain limit.
TEST_F(FvmHostTest, LargeSize) {
  RunReservationTestForAllTypes(8192, true, 100, 10, 300 * 1024 * 1024);
}

// Limitless capacity for 10k inodes and 10k bytes of data
TEST_F(FvmHostTest, LotsOfInodes) {
  RunReservationTestForAllTypes(kDefaultSliceSize, true, 10000, 1024 * 10, 0);
}
#endif

}  // namespace
