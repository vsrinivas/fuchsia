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

// Conversion functions for |fuchsia.component.decl| types.
std::shared_ptr<fcdecl::Ref> CreateFidlChildRef(std::string_view name);
std::shared_ptr<fcdecl::Ref> CreateFidlParentRef();

std::shared_ptr<fcdecl::Offer> CreateFidlProtocolOfferDecl(std::string_view source_name,
                                                           std::shared_ptr<fcdecl::Ref> source,
                                                           std::string_view target_name,
                                                           std::shared_ptr<fcdecl::Ref> target);
std::shared_ptr<fcdecl::Offer> CreateFidlServiceOfferDecl(std::string_view source_name,
                                                          std::shared_ptr<fcdecl::Ref> source,
                                                          std::string_view target_name,
                                                          std::shared_ptr<fcdecl::Ref> target);
std::shared_ptr<fcdecl::Offer> CreateFidlDirectoryOfferDecl(
    std::string_view source_name, std::shared_ptr<fcdecl::Ref> source, std::string_view target_name,
    std::shared_ptr<fcdecl::Ref> target, std::string_view subdir, fio::Operations rights);

std::shared_ptr<fcdecl::Offer> CreateFidlStorageOfferDecl(std::string_view source_name,
                                                          std::shared_ptr<fcdecl::Ref> source,
                                                          std::string_view target_name,
                                                          std::shared_ptr<fcdecl::Ref> target);

// Conversion functions for |fuchsia.component.test| types.
std::shared_ptr<fctest::ChildOptions> CreateFidlChildOptions(fcdecl::StartupMode startup_mode,
                                                             std::string_view environment);

std::shared_ptr<fctest::Capability> CreateFidlProtocolCapability(std::string_view name,
                                                                 std::string_view as,
                                                                 fcdecl::DependencyType type,
                                                                 std::string_view path);

std::shared_ptr<fctest::Capability> CreateFidlProtocolCapability(std::string_view name);

std::shared_ptr<fctest::Capability> CreateFidlServiceCapability(std::string_view name,
                                                                std::string_view as,
                                                                std::string_view path);

std::shared_ptr<fctest::Capability> CreateFidlServiceCapability(std::string_view name);

std::shared_ptr<fctest::Capability> CreateFidlDirectoryCapability(
    std::string_view name, std::string_view as, fcdecl::DependencyType type,
    std::string_view subdir, fio::Operations rights, std::string_view path);

std::shared_ptr<fctest::Capability> CreateFidlDirectoryCapability(std::string_view name);

}  // namespace tests
}  // namespace component

#endif  // LIB_SYS_COMPONENT_CPP_TESTS_UTILS_H_
