#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "fs_test.h"

namespace fs_test {
namespace {

TEST(FileSystemTest, ReadFileAfterWritingFileSucceeds) {
  auto fs = TestFileSystem::Create(TestFileSystem::Options()).value();
  std::string file = fs.mount_path() + "/123";
  auto fd = fbl::unique_fd(open(file.c_str(), O_RDWR | O_CREAT, 0666));
  ASSERT_TRUE(fd);
  EXPECT_EQ(write(fd.get(), "hello", 5), 5);
  char buf[5];
  EXPECT_EQ(pread(fd.get(), buf, 5, 0), 5);
  EXPECT_TRUE(!memcmp(buf, "hello", 5));
}

}  // namespace
}  // namespace fs_test
