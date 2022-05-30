// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/component/decl/cpp/fidl.h>
#include <lib/sys/component/cpp/tests/utils.h>

#include <string>

namespace component {
namespace tests {

namespace fctest = fuchsia::component::test;
namespace fcdecl = fuchsia::component::decl;
namespace fio = fuchsia::io;

std::shared_ptr<fcdecl::Ref> CreateFidlChildRef(std::string_view name) {
  fcdecl::ChildRef ref;
  ref.name = std::string(name);
  return std::make_shared<fcdecl::Ref>(fcdecl::Ref::WithChild(std::move(ref)));
}

std::shared_ptr<fcdecl::Ref> CreateFidlParentRef() {
  return std::make_shared<fcdecl::Ref>(fcdecl::Ref::WithParent(fcdecl::ParentRef{}));
}

std::shared_ptr<fcdecl::Offer> CreateFidlProtocolOfferDecl(std::string_view source_name,
                                                           std::shared_ptr<fcdecl::Ref> source,
                                                           std::string_view target_name,
                                                           std::shared_ptr<fcdecl::Ref> target) {
  fcdecl::OfferProtocol offer;
  offer.set_source(std::move(*source));
  offer.set_source_name(std::string(source_name));
  offer.set_target(std::move(*target));
  offer.set_target_name(std::string(target_name));
  offer.set_dependency_type(fcdecl::DependencyType::STRONG);
  offer.set_availability(fcdecl::Availability::REQUIRED);

  return std::make_shared<fcdecl::Offer>(fcdecl::Offer::WithProtocol(std::move(offer)));
}

std::shared_ptr<fcdecl::Offer> CreateFidlServiceOfferDecl(std::string_view source_name,
                                                          std::shared_ptr<fcdecl::Ref> source,
                                                          std::string_view target_name,
                                                          std::shared_ptr<fcdecl::Ref> target) {
  fcdecl::OfferService offer;
  offer.set_source(std::move(*source));
  offer.set_source_name(std::string(source_name));
  offer.set_target(std::move(*target));
  offer.set_target_name(std::string(target_name));
  offer.set_availability(fcdecl::Availability::REQUIRED);

  return std::make_shared<fcdecl::Offer>(fcdecl::Offer::WithService(std::move(offer)));
}

std::shared_ptr<fcdecl::Offer> CreateFidlDirectoryOfferDecl(
    std::string_view source_name, std::shared_ptr<fcdecl::Ref> source, std::string_view target_name,
    std::shared_ptr<fcdecl::Ref> target, std::string_view subdir, fio::Operations rights) {
  fcdecl::OfferDirectory offer;
  offer.set_source(std::move(*source));
  offer.set_source_name(std::string(source_name));
  offer.set_target(std::move(*target));
  offer.set_target_name(std::string(target_name));
  offer.set_subdir(std::string(subdir));
  offer.set_rights(rights);
  offer.set_dependency_type(fcdecl::DependencyType::STRONG);
  offer.set_availability(fcdecl::Availability::REQUIRED);

  return std::make_shared<fcdecl::Offer>(fcdecl::Offer::WithDirectory(std::move(offer)));
}

std::shared_ptr<fcdecl::Offer> CreateFidlStorageOfferDecl(std::string_view source_name,
                                                          std::shared_ptr<fcdecl::Ref> source,
                                                          std::string_view target_name,
                                                          std::shared_ptr<fcdecl::Ref> target) {
  fcdecl::OfferStorage offer;
  offer.set_source(std::move(*source));
  offer.set_source_name(std::string(source_name));
  offer.set_target(std::move(*target));
  offer.set_target_name(std::string(target_name));
  offer.set_availability(fcdecl::Availability::REQUIRED);

  return std::make_shared<fcdecl::Offer>(fcdecl::Offer::WithStorage(std::move(offer)));
}

std::shared_ptr<fctest::ChildOptions> CreateFidlChildOptions(fcdecl::StartupMode startup_mode,
                                                             std::string_view environment) {
  fctest::ChildOptions options;
  options.set_environment(std::string(environment));
  options.set_startup(startup_mode);
  return std::make_shared<fctest::ChildOptions>(std::move(options));
}

std::shared_ptr<fctest::Capability2> CreateFidlProtocolCapability(std::string_view name,
                                                                  std::string_view as,
                                                                  fcdecl::DependencyType type,
                                                                  std::string_view path) {
  fctest::Protocol capability;
  capability.set_name(std::string(name));
  capability.set_as(std::string(as));
  capability.set_type(type);
  capability.set_path(std::string(path));
  return std::make_shared<fctest::Capability2>(
      fctest::Capability2::WithProtocol(std::move(capability)));
}

std::shared_ptr<fctest::Capability2> CreateFidlProtocolCapability(std::string_view name) {
  fctest::Protocol capability;
  capability.set_name(std::string(name));
  return std::make_shared<fctest::Capability2>(
      fctest::Capability2::WithProtocol(std::move(capability)));
}

std::shared_ptr<fctest::Capability2> CreateFidlServiceCapability(std::string_view name,
                                                                 std::string_view as,
                                                                 std::string_view path) {
  fctest::Service capability;
  capability.set_name(std::string(name));
  capability.set_as(std::string(as));
  capability.set_path(std::string(path));
  return std::make_shared<fctest::Capability2>(
      fctest::Capability2::WithService(std::move(capability)));
}

std::shared_ptr<fctest::Capability2> CreateFidlServiceCapability(std::string_view name) {
  fctest::Service capability;
  capability.set_name(std::string(name));
  return std::make_shared<fctest::Capability2>(
      fctest::Capability2::WithService(std::move(capability)));
}

std::shared_ptr<fctest::Capability2> CreateFidlDirectoryCapability(
    std::string_view name, std::string_view as, fcdecl::DependencyType type,
    std::string_view subdir, fio::Operations rights, std::string_view path) {
  fctest::Directory capability;
  capability.set_name(std::string(name));
  capability.set_as(std::string(as));
  capability.set_type(type);
  capability.set_subdir(std::string(subdir));
  capability.set_rights(rights);
  capability.set_path(std::string(path));
  return std::make_shared<fctest::Capability2>(
      fctest::Capability2::WithDirectory(std::move(capability)));
}

std::shared_ptr<fctest::Capability2> CreateFidlDirectoryCapability(std::string_view name) {
  fctest::Directory capability;
  capability.set_name(std::string(name));
  return std::make_shared<fctest::Capability2>(
      fctest::Capability2::WithDirectory(std::move(capability)));
}

}  // namespace tests
}  // namespace component
