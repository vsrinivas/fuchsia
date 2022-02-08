// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_CPP_TESTS_UTILS_H_
#define LIB_SYS_COMPONENT_CPP_TESTS_UTILS_H_

#include <fuchsia/component/decl/cpp/fidl.h>
#include <fuchsia/component/test/cpp/fidl.h>

#include <string>

namespace component {
namespace tests {

namespace fctest = fuchsia::component::test;
namespace fcdecl = fuchsia::component::decl;
namespace fio = fuchsia::io;

std::shared_ptr<fctest::ChildOptions> CreateFidlChildOptions(fcdecl::StartupMode startup_mode,
                                                             std::string environment);
std::shared_ptr<fcdecl::Ref> CreateFidlChildRef(std::string name);
std::shared_ptr<fctest::Capability2> CreateFidlProtocolCapability(std::string name, std::string as,
                                                                  fcdecl::DependencyType type,
                                                                  std::string path);
std::shared_ptr<fctest::Capability2> CreateFidlProtocolCapability(std::string name);
std::shared_ptr<fctest::Capability2> CreateFidlServiceCapability(std::string name, std::string as,
                                                                 std::string path);
std::shared_ptr<fctest::Capability2> CreateFidlServiceCapability(std::string name);
std::shared_ptr<fctest::Capability2> CreateFidlDirectoryCapability(std::string name, std::string as,
                                                                   fcdecl::DependencyType type,
                                                                   std::string subdir,
                                                                   fio::Operations rights,
                                                                   std::string path);
std::shared_ptr<fctest::Capability2> CreateFidlDirectoryCapability(std::string name);

}  // namespace tests
}  // namespace component

#endif  // LIB_SYS_COMPONENT_CPP_TESTS_UTILS_H_
