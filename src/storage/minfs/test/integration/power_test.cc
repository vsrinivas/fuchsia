// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>

#include <functional>
#include <string>
#include <vector>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace minfs {
namespace {

class PowerTest : public fs_test::BaseFilesystemTest {
 public:
  PowerTest() : BaseFilesystemTest(fs_test::TestFilesystemOptions::DefaultMinfs()) {}
};

TEST_F(PowerTest, Basics) {
  RunSimulatedPowerCutTest(
      fs_test::PowerCutOptions{
          .stride = 5,  // Chosen so test does not run for too long.
      },
      [&] {
        ASSERT_EQ(mkdir(GetPath("alpha").c_str(), 0755), 0);
        ASSERT_EQ(mkdir(GetPath("alpha/bravo").c_str(), 0755), 0);
        ASSERT_EQ(mkdir(GetPath("alpha/bravo/charlie").c_str(), 0755), 0);
        ASSERT_EQ(mkdir(GetPath("alpha/bravo/charlie/delta").c_str(), 0755), 0);
        ASSERT_EQ(mkdir(GetPath("alpha/bravo/charlie/delta/echo").c_str(), 0755), 0);
        std::string file = GetPath("alpha/bravo/charlie/delta/echo/foxtrot");
        fbl::unique_fd fd1(open(file.c_str(), O_CREAT | O_RDWR, 0666));
        ASSERT_TRUE(fd1);
        fbl::unique_fd fd2(open(file.c_str(), O_RDWR));
        ASSERT_TRUE(fd2);
        ASSERT_EQ(write(fd1.get(), "Hello, World!\n", 14), 14);
        fd1.reset();
        fd2.reset();

        file = GetPath("file.txt");
        fd1 = fbl::unique_fd(open(file.c_str(), O_CREAT | O_RDWR, 0666));
        ASSERT_TRUE(fd1);

        ASSERT_EQ(unlink(file.c_str()), 0);

        std::string dir = GetPath("emptydir");
        ASSERT_EQ(mkdir(dir.c_str(), 0755), 0);
        fd1 = fbl::unique_fd(open(dir.c_str(), O_RDONLY));
        ASSERT_TRUE(fd1);

        ASSERT_EQ(rmdir(dir.c_str()), 0);
      });
}

constexpr int kDataSize = 16 * 1024;
void* GetData() {
  static void* s_data_block = nullptr;
  if (!s_data_block) {
    s_data_block = new char[kDataSize];
    memset(s_data_block, 'y', kDataSize);
  }
  return s_data_block;
}

TEST_F(PowerTest, DeleteAndWrite) {
  RunSimulatedPowerCutTest(
      fs_test::PowerCutOptions{
          .stride = 43,  // Chosen so test does not run for too long.
      },
      [&] {
        ASSERT_EQ(mkdir(GetPath("alpha").c_str(), 0755), 0);

        std::vector<std::string> names = {
            GetPath("alpha/bravo"), GetPath("alpha/charlie"), GetPath("alpha/delta"),
            GetPath("alpha/echo"),  GetPath("alpha/foxtrot"),
        };
        std::vector<fbl::unique_fd> files;

        for (size_t i = 0; i < 10; i++) {
          fbl::unique_fd file(open(names[i % names.size()].c_str(), O_CREAT | O_RDWR, 0666));
          ASSERT_TRUE(file);
          ASSERT_EQ(write(file.get(), GetData(), kDataSize), static_cast<ssize_t>(kDataSize));
          files.push_back(std::move(file));
          if (i % 2) {
            // Create a directory over a file.
            ASSERT_EQ(unlink(names[i % names.size()].c_str()), 0);
            ASSERT_EQ(mkdir(names[i % names.size()].c_str(), 0755), 0);
          }

          if (i < 2) {
            continue;
          }

          // May or may nor succeed.
          write(files[i - 1].get(), GetData(), kDataSize);
          files[i - 2].reset();
          unlink(names[(i - 2) % names.size()].c_str());
        }
      });
}

TEST_F(PowerTest, LargeWrite) {
  RunSimulatedPowerCutTest(fs_test::PowerCutOptions({
                               .stride = 92771,  // Chosen so test does not run for too long.
                           }),
                           [&] {
                             std::string name = GetPath("the name");
                             for (int n = 0; n < 10; n++) {
                               fbl::unique_fd file(open(name.c_str(), O_CREAT | O_RDWR, 0666));
                               ASSERT_TRUE(file);
                               const int kBufferSize = 1024 * 1024 * 4;
                               std::vector<char> buffer(kBufferSize, 'p');
                               ASSERT_EQ(write(file.get(), buffer.data(), kBufferSize),
                                         static_cast<ssize_t>(kBufferSize));
                               ASSERT_EQ(unlink(name.c_str()), 0);
                             }
                           });
}

}  // namespace
}  // namespace minfs
