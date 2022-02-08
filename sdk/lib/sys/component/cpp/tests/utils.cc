// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/component/cpp/tests/utils.h>

#include <string>

namespace component {
namespace tests {

namespace fctest = fuchsia::component::test;
namespace fcdecl = fuchsia::component::decl;
namespace fio = fuchsia::io;

std::shared_ptr<fctest::ChildOptions> CreateFidlChildOptions(fcdecl::StartupMode startup_mode,
                                                             std::string environment) {
  fctest::ChildOptions options;
  options.set_environment(std::move(environment));
  options.set_startup(startup_mode);
  return std::make_shared<fctest::ChildOptions>(std::move(options));
}

std::shared_ptr<fcdecl::Ref> CreateFidlChildRef(std::string name) {
  fcdecl::ChildRef ref;
  ref.name = std::move(name);
  return std::make_shared<fcdecl::Ref>(fcdecl::Ref::WithChild(std::move(ref)));
}

std::shared_ptr<fctest::Capability2> CreateFidlProtocolCapability(std::string name, std::string as,
                                                                  fcdecl::DependencyType type,
                                                                  std::string path) {
  fctest::Protocol capability;
  capability.set_name(std::move(name));
  capability.set_as(std::move(as));
  capability.set_type(type);
  capability.set_path(std::move(path));
  return std::make_shared<fctest::Capability2>(
      fctest::Capability2::WithProtocol(std::move(capability)));
}

std::shared_ptr<fctest::Capability2> CreateFidlProtocolCapability(std::string name) {
  fctest::Protocol capability;
  capability.set_name(std::move(name));
  return std::make_shared<fctest::Capability2>(
      fctest::Capability2::WithProtocol(std::move(capability)));
}

std::shared_ptr<fctest::Capability2> CreateFidlServiceCapability(std::string name, std::string as,
                                                                 std::string path) {
  fctest::Service capability;
  capability.set_name(std::move(name));
  capability.set_as(std::move(as));
  capability.set_path(std::move(path));
  return std::make_shared<fctest::Capability2>(
      fctest::Capability2::WithService(std::move(capability)));
}

std::shared_ptr<fctest::Capability2> CreateFidlServiceCapability(std::string name) {
  fctest::Service capability;
  capability.set_name(std::move(name));
  return std::make_shared<fctest::Capability2>(
      fctest::Capability2::WithService(std::move(capability)));
}

std::shared_ptr<fctest::Capability2> CreateFidlDirectoryCapability(std::string name, std::string as,
                                                                   fcdecl::DependencyType type,
                                                                   std::string subdir,
                                                                   fio::Operations rights,
                                                                   std::string path) {
  fctest::Directory capability;
  capability.set_name(std::move(name));
  capability.set_as(std::move(as));
  capability.set_type(type);
  capability.set_subdir(std::move(subdir));
  capability.set_rights(rights);
  capability.set_path(std::move(path));
  return std::make_shared<fctest::Capability2>(
      fctest::Capability2::WithDirectory(std::move(capability)));
}

std::shared_ptr<fctest::Capability2> CreateFidlDirectoryCapability(std::string name) {
  fctest::Directory capability;
  capability.set_name(std::move(name));
  return std::make_shared<fctest::Capability2>(
      fctest::Capability2::WithDirectory(std::move(capability)));
}

}  // namespace tests
}  // namespace component
