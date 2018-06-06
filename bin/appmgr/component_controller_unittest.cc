// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/component_controller_impl.h"

#include <fs/pseudo-dir.h>
#include <fs/remote-dir.h>
#include <lib/fdio/spawn.h>

#include "garnet/bin/appmgr/realm.h"
#include "gtest/gtest.h"
#include "lib/fsl/handles/object_info.h"
#include "lib/gtest/test_with_message_loop.h"

namespace fuchsia {
namespace sys {

class RealmFriendForTests {
 public:
  static size_t ComponentCount(const Realm* realm) {
    return realm->applications_.size();
  }

  static void AddComponent(Realm* realm,
                           std::unique_ptr<ComponentControllerImpl> component) {
    // update hub
    realm->hub_.AddComponent(component->HubInfo());
    auto key = component.get();
    realm->applications_.emplace(key, std::move(component));
  }
};
namespace {

class ComponentControllerTest : public gtest::TestWithMessageLoop {};

std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> tokens;
  auto i = 0;
  auto pos = s.find(delim);
  while (pos != std::string::npos) {
    tokens.push_back(s.substr(i, pos - i));
    i = ++pos;
    pos = s.find(delim, pos);
  }
  if (pos == std::string::npos)
    tokens.push_back(s.substr(i, s.length()));
  return tokens;
}

fbl::String get_value(const fbl::RefPtr<fs::PseudoDir>& hub_dir,
                      std::string path) {
  auto tokens = split(path, '/');
  auto ntokens = tokens.size();
  fbl::RefPtr<fs::Vnode> node = hub_dir;
  fbl::RefPtr<fs::Vnode> pdir;
  for (size_t i = 0; i < ntokens; i++) {
    auto token = tokens[i];
    pdir = node;
    if (pdir->Lookup(&node, token) != ZX_OK) {
      EXPECT_FALSE(true) << token << " not found";
      return "";
    }
  }
  fbl::RefPtr<fs::Vnode> file;
  if (node->Open(ZX_FS_RIGHT_READABLE, &file) != ZX_OK) {
    EXPECT_FALSE(true) << "cannot open: " << path;
    return "";
  }
  char buf[1024];
  size_t read_len;
  file->Read(buf, sizeof(buf), 0, &read_len);
  return fbl::String(buf, read_len);
}

bool path_exists(const fbl::RefPtr<fs::PseudoDir>& hub_dir, std::string path) {
  auto tokens = split(path, '/');
  auto ntokens = tokens.size();
  fbl::RefPtr<fs::Vnode> dir = hub_dir;
  fbl::RefPtr<fs::Vnode> pdir;
  for (size_t i = 0; i < ntokens; i++) {
    auto token = tokens[i];
    pdir = dir;
    if (pdir->Lookup(&dir, token) != ZX_OK) {
      return false;
    }
  }
  return true;
}

zx::process create_process() {
  zx::process process;
  const char* argv[] = {"sh", NULL};
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t status = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL,
                                      "/boot/bin/sh", argv, NULL, 0, NULL,
                                      process.reset_and_get_address(), err_msg);
  EXPECT_EQ(status, ZX_OK) << err_msg;
  return process;
}

TEST_F(ComponentControllerTest, CreateAndKill) {
  RealmArgs args{nullptr, zx::channel(), "test", false};
  Realm realm(std::move(args));
  zx::process process = create_process();
  ASSERT_TRUE(process);
  auto koid = std::to_string(fsl::GetKoid(process.get()));

  ComponentControllerPtr component_ptr;
  auto component = std::make_unique<ComponentControllerImpl>(
      component_ptr.NewRequest(), &realm, nullptr, std::move(process),
      "test-url", "test-arg", "test-label", nullptr,
      ExportedDirType::kLegacyFlatLayout, zx::channel(), zx::channel());
  ASSERT_EQ(RealmFriendForTests::ComponentCount(&realm), 0u);
  RealmFriendForTests::AddComponent(&realm, std::move(component));

  ASSERT_EQ(RealmFriendForTests::ComponentCount(&realm), 1u);
  auto hub_path = "c/test-label/" + koid;
  EXPECT_TRUE(path_exists(realm.hub_dir(), hub_path));

  bool wait = false;
  component_ptr->Wait([&wait](int errcode) { wait = true; });
  component_ptr->Kill();
  EXPECT_TRUE(RunLoopUntilWithTimeout([&wait] { return wait; },
                                      fxl::TimeDelta::FromSeconds(5)));

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(RealmFriendForTests::ComponentCount(&realm), 0u);
  EXPECT_FALSE(path_exists(realm.hub_dir(), hub_path));
}

TEST_F(ComponentControllerTest, ControllerScope) {
  RealmArgs args{nullptr, zx::channel(), "test", false};
  Realm realm(std::move(args));
  zx::process process = create_process();
  ASSERT_TRUE(process);
  auto koid = std::to_string(fsl::GetKoid(process.get()));
  bool wait = false;
  auto hub_path = "c/test-label/" + koid;
  {
    ComponentControllerPtr component_ptr;
    auto component = std::make_unique<ComponentControllerImpl>(
        component_ptr.NewRequest(), &realm, nullptr, std::move(process),
        "test-url", "test-arg", "test-label", nullptr,
        ExportedDirType::kLegacyFlatLayout, zx::channel(), zx::channel());
    component->Wait([&wait](int errcode) { wait = true; });
    RealmFriendForTests::AddComponent(&realm, std::move(component));

    ASSERT_EQ(RealmFriendForTests::ComponentCount(&realm), 1u);
    EXPECT_TRUE(path_exists(realm.hub_dir(), hub_path));
  }
  EXPECT_TRUE(RunLoopUntilWithTimeout([&wait] { return wait; },
                                      fxl::TimeDelta::FromSeconds(5)));

  // make sure all messages are processed after wait was called
  RunLoopUntilIdle();
  EXPECT_EQ(RealmFriendForTests::ComponentCount(&realm), 0u);
  EXPECT_FALSE(path_exists(realm.hub_dir(), hub_path));
}

TEST_F(ComponentControllerTest, DetachController) {
  RealmArgs args{nullptr, zx::channel(), "test", false};
  Realm realm(std::move(args));
  zx::process process = create_process();
  ASSERT_TRUE(process);
  bool wait = false;
  {
    ComponentControllerPtr component_ptr;
    auto component = std::make_unique<ComponentControllerImpl>(
        component_ptr.NewRequest(), &realm, nullptr, std::move(process),
        "test-url", "test-arg", "test-label", nullptr,
        ExportedDirType::kLegacyFlatLayout, zx::channel(), zx::channel());
    component->Wait([&wait](int errcode) { wait = true; });
    RealmFriendForTests::AddComponent(&realm, std::move(component));

    ASSERT_EQ(RealmFriendForTests::ComponentCount(&realm), 1u);

    // detach controller before it goes out of scope and then test that our
    // component did not die.
    component_ptr->Detach();
    RunLoopUntilIdle();
  }

  // make sure all messages are processed if Kill was called.
  RunLoopUntilIdle();
  ASSERT_FALSE(wait);
  EXPECT_EQ(RealmFriendForTests::ComponentCount(&realm), 1u);
}

TEST_F(ComponentControllerTest, Hub) {
  RealmArgs args{nullptr, zx::channel(), "test", false};
  Realm realm(std::move(args));
  zx::channel export_dir, export_dir_req;
  ASSERT_EQ(zx::channel::create(0, &export_dir, &export_dir_req), ZX_OK);

  zx::process process = create_process();
  auto koid = std::to_string(fsl::GetKoid(process.get()));
  ASSERT_TRUE(process);
  ComponentControllerPtr component_ptr;

  auto component = std::make_unique<ComponentControllerImpl>(
      component_ptr.NewRequest(), &realm, nullptr, std::move(process),
      "test-url", "test-arg", "test-label", nullptr,
      ExportedDirType::kPublicDebugCtrlLayout, std::move(export_dir_req),
      zx::channel());

  EXPECT_STREQ(get_value(component->hub_dir(), "name").c_str(), "test-label");
  EXPECT_STREQ(get_value(component->hub_dir(), "args").c_str(), "test-arg");
  EXPECT_STREQ(get_value(component->hub_dir(), "job-id").c_str(),
               realm.koid().c_str());
  EXPECT_STREQ(get_value(component->hub_dir(), "url").c_str(), "test-url");
  EXPECT_STREQ(get_value(component->hub_dir(), "process-id").c_str(),
               koid.c_str());
  EXPECT_TRUE(path_exists(component->hub_dir(), "out"));
}

}  // namespace
}  // namespace sys
}  // namespace fuchsia
