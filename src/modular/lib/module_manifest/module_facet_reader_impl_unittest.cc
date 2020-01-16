// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/module_manifest/module_facet_reader_impl.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <map>
#include <string>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/gtest/real_loop_fixture.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/modular/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace {

// A utility class for making a static filesystem. Use |AddFile| to populate the
// file system with (file path, file content)s.
class FilesystemForTest {
 public:
  // Returns an opened file descriptor for |dir|. |dir| must be an absolute
  // path.
  zx::channel GetChannelForDir(fxl::StringView dir) {
    std::string dir_str = ToRelativePath(dir);
    FXL_CHECK(files::IsDirectoryAt(tmpfs_.root_fd(), dir_str));

    int fd = openat(tmpfs_.root_fd(), dir_str.data(), O_DIRECTORY);
    FXL_CHECK(fd != -1);

    auto ch = fsl::CloneChannelFromFileDescriptor(fd);
    close(fd);
    return ch;
  }

  // Adds a file to the filesystem. |path| must be an absolute path representing
  // a file. |data| contains the file contents. Intermediate directories
  // required for |path| exist are created as needed.
  void AddFile(fxl::StringView path, const std::string& data) {
    std::string path_str = ToRelativePath(path);
    FXL_CHECK(files::CreateDirectoryAt(tmpfs_.root_fd(), files::GetDirectoryName(path_str)));
    FXL_CHECK(files::WriteFileAt(tmpfs_.root_fd(), path_str, data.data(), data.size()));
  }

 private:
  std::string ToRelativePath(fxl::StringView path) {
    if (path[0] == '/') {
      return path.substr(1).ToString();
    }
    return path.ToString();
  }

  // In-memory file system.
  scoped_tmpfs::ScopedTmpFS tmpfs_;
};

// A Loader used for testing. Use |AddLoadInfo()| to pre-populate answers to
// |fuchsia.sys.Loader.LoadUrl()| requests. Because directories are not
// trivially clonable, |AddLoadInfo(url,..)| is only able to serve one
// |fuchsia.sys.Loader.LoadUrl(url)|.
class SysLoaderForTest : fuchsia::sys::Loader {
 public:
  SysLoaderForTest() : binding_(this) {}

  // Returns a fuchsia::sys::LoaderPtr that returns one-shot answers that were
  // added from |AddLoadInfo|.
  fuchsia::sys::LoaderPtr NewEndpoint() {
    fuchsia::sys::LoaderPtr loader;
    binding_.Bind(loader.NewRequest());
    return loader;
  }

  // Populate a one-shot answer to |fuchsia.sys.Loader.LoadUrl|; that is,
  // LoadUrl() will not be able to answer for |url| a second time unless
  // |AddLoadInfo| is called again.
  void AddLoadInfo(fidl::StringPtr url, fuchsia::sys::PackagePtr pkg) {
    if (url->find("//") == std::string::npos) {
      url = fxl::Substitute("file://$0", url.value_or(""));
    }
    load_info_[url.value_or("")] = std::move(pkg);
  }

 private:
  // |fuchsia::sys::Loader|
  void LoadUrl(std::string url, LoadUrlCallback cb) {
    if (load_info_.find(url) != load_info_.end()) {
      auto retval = std::move(load_info_[url]);
      load_info_.erase(url);

      cb(std::move(retval));
    } else {
      cb({});
    }
  }

  std::map<std::string, fuchsia::sys::PackagePtr> load_info_;
  fidl::Binding<fuchsia::sys::Loader> binding_;
};

}  // namespace

class ModuleFacetReaderImplTest : public gtest::RealLoopFixture {
 protected:
  ModuleFacetReaderImplTest() : module_facet_reader_impl_(sys_loader_.NewEndpoint()) {}

  static constexpr char kNoFacet[] = R"({})";
  static constexpr char kBasicFacet[] = R"(
    {
      "facets": {
        "fuchsia.module":{
          "@version":2,
          "binary":"binary",
          "suggestion_headline":"suggestion_headline",
          "intent_filters":[
            {
              "action":"action",
              "parameters":[
                {
                  "name":"name",
                  "type":"type"
                }
              ]
            }
          ]
        }
      }
    }
  )";

  modular::ModuleFacetReader* module_facet_reader() { return &module_facet_reader_impl_; }

  // Populates a one-shot answer for fuchsia::sys::Loader used by
  // ModuleFacetReaderImpl::GetModuleFacet()
  void PopulateModFacetFromPkgUrl(fxl::StringView mod_pkg_name, fxl::StringView mod_cmx_data) {
    fs_.AddFile(fxl::Substitute("/$0/meta/$0.cmx", mod_pkg_name), mod_cmx_data.data());
    auto pkg = fuchsia::sys::Package::New();
    pkg->resolved_url = fxl::Substitute("fuchsia-pkg://fuchsia.com/$0#meta/$0.cmx", mod_pkg_name);
    pkg->directory = fs_.GetChannelForDir(mod_pkg_name);
    sys_loader_.AddLoadInfo(mod_pkg_name.ToString(), std::move(pkg));
  }

  // Populates a one-shot answer for fuchsia::sys::Loader used by
  // ModuleFacetReaderImpl::GetModuleFacet()
  void PopulateModFacetFromComponentUrl(fxl::StringView mod_pkg_name,
                                        fxl::StringView mod_component_name,
                                        fxl::StringView mod_cmx_data) {
    fs_.AddFile(fxl::Substitute("/$0/meta/$1.cmx", mod_pkg_name, mod_component_name),
                mod_cmx_data.data());
    auto pkg = fuchsia::sys::Package::New();
    pkg->resolved_url = fxl::Substitute("fuchsia-pkg://fuchsia.com/$0#meta/$1.cmx", mod_pkg_name,
                                        mod_component_name);
    pkg->directory = fs_.GetChannelForDir(mod_pkg_name);
    sys_loader_.AddLoadInfo(fxl::Substitute("fuchsia-pkg://fuchsia.com/$0#meta/$1.cmx",
                                            mod_pkg_name, mod_component_name),
                            std::move(pkg));
  }

 private:
  FilesystemForTest fs_;
  SysLoaderForTest sys_loader_;
  modular::ModuleFacetReaderImpl module_facet_reader_impl_;
};

TEST_F(ModuleFacetReaderImplTest, ModFacetFoundFromPkgUrl) {
  constexpr char kModName[] = "my_mod_url";
  PopulateModFacetFromPkgUrl(kModName, kBasicFacet);

  bool done = false;
  module_facet_reader()->GetModuleManifest(
      kModName, [&done](fuchsia::modular::ModuleManifestPtr manifest) {
        EXPECT_TRUE(manifest);
        EXPECT_EQ("file://my_mod_url", manifest->binary);
        EXPECT_EQ("suggestion_headline", manifest->suggestion_headline);
        EXPECT_EQ(1u, manifest->intent_filters->size());
        done = true;
      });
  RunLoopUntil([&done] { return done; });
  EXPECT_TRUE(done);
}

TEST_F(ModuleFacetReaderImplTest, ModFacetFoundFromComponentUrl) {
  constexpr char kPkgName[] = "my_pkg_name";
  constexpr char kModName[] = "my_mod_name";
  PopulateModFacetFromComponentUrl(kPkgName, kModName, kBasicFacet);

  bool done = false;
  module_facet_reader()->GetModuleManifest(
      fxl::Substitute("fuchsia-pkg://fuchsia.com/$0#meta/$1.cmx", kPkgName, kModName),
      [&done](fuchsia::modular::ModuleManifestPtr manifest) {
        EXPECT_TRUE(manifest);
        EXPECT_EQ("fuchsia-pkg://fuchsia.com/my_pkg_name#meta/my_mod_name.cmx", manifest->binary);
        EXPECT_EQ("suggestion_headline", manifest->suggestion_headline);
        EXPECT_EQ(1u, manifest->intent_filters->size());
        done = true;
      });
  RunLoopUntil([&done] { return done; });
  EXPECT_TRUE(done);
}

TEST_F(ModuleFacetReaderImplTest, ModHasNoFacet) {
  constexpr char kModName[] = "my_mod_url";
  PopulateModFacetFromPkgUrl(kModName, kNoFacet);
  bool done = false;
  module_facet_reader()->GetModuleManifest(kModName,
                                           [&done](fuchsia::modular::ModuleManifestPtr manifest) {
                                             EXPECT_FALSE(manifest);
                                             done = true;
                                           });
  RunLoopUntil([&done] { return done; });
  EXPECT_TRUE(done);
}

TEST_F(ModuleFacetReaderImplTest, ModDoesntExist) {
  bool done = false;
  module_facet_reader()->GetModuleManifest("kajsdhf",
                                           [&done](fuchsia::modular::ModuleManifestPtr manifest) {
                                             EXPECT_FALSE(manifest);
                                             done = true;
                                           });
  RunLoopUntil([&done] { return done; });
  EXPECT_TRUE(done);
}
