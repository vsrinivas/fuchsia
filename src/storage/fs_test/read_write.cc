#include <fbl/unique_fd.h>

#include "fs_test_fixture.h"

namespace fs_test {
namespace {

TEST_P(FileSystemTest, ReadFileAfterWritingFileSucceeds) {
  std::string file = fs_.mount_path() + "/123";
  auto fd = fbl::unique_fd(open(file.c_str(), O_RDWR | O_CREAT, 0666));
  ASSERT_TRUE(fd);
  EXPECT_EQ(write(fd.get(), "hello", 5), 5);
  char buf[5];
  EXPECT_EQ(pread(fd.get(), buf, 5, 0), 5);
  EXPECT_TRUE(!memcmp(buf, "hello", 5));
}

INSTANTIATE_TEST_SUITE_P(ReadWrite, FileSystemTest, testing::ValuesIn(AllTestFileSystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
