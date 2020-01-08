// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/setting_store.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/setting_schema.h"

namespace zxdb {

namespace {

constexpr int kDefaultInt = 10;
const char kDefaultString[] = "string default";

std::vector<std::string> DefaultList() { return {kDefaultString, "list"}; }

fxl::RefPtr<SettingSchema> GetSchema() {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  schema->AddBool("bool", "bool_option", true);
  schema->AddInt("int", "int_option", kDefaultInt);
  schema->AddString("string", "string_option", kDefaultString);
  if (!schema->AddList("list", "list_option", DefaultList())) {
    FXL_NOTREACHED() << "Schema should be valid!";
    return nullptr;
  }
  if (!schema->AddList("list_with_options", "list_with_options", {}, DefaultList())) {
    FXL_NOTREACHED() << "Schema should be valid!";
    return nullptr;
  }
  return schema;
}

class SettingObserver : public SettingStoreObserver {
 public:
  // Keep track of who called.
  struct SettingNotificationRecord {
    const SettingStore* store;
    std::string name;
    SettingValue value;
  };

  void OnSettingChanged(const SettingStore& store, const std::string& setting_name) override {
    SettingNotificationRecord record = {};
    record.store = &store;
    record.name = setting_name;
    record.value = store.GetValue(setting_name);
    notifications_.push_back(std::move(record));
  }

  const std::vector<SettingNotificationRecord>& notifications() const { return notifications_; }

 private:
  std::vector<SettingNotificationRecord> notifications_;
};

}  // namespace

TEST(SettingStore, Defaults) {
  SettingStore store(GetSchema(), nullptr);

  auto value = store.GetValue("bool");
  ASSERT_TRUE(value.is_bool());
  EXPECT_TRUE(value.get_bool());

  value = store.GetValue("int");
  ASSERT_TRUE(value.is_int());
  EXPECT_EQ(value.get_int(), kDefaultInt);

  value = store.GetValue("string");
  ASSERT_TRUE(value.is_string());
  EXPECT_EQ(value.get_string(), kDefaultString);

  value = store.GetValue("list");
  ASSERT_TRUE(value.is_list());
  EXPECT_EQ(value.get_list(), DefaultList());

  // Not found.
  EXPECT_TRUE(store.GetValue("unexistent").is_null());
}

TEST(SettingStore, Overrides) {
  SettingStore store(GetSchema(), nullptr);

  Err err;

  // Wrong key.
  err = store.SetInt("wrong", 10);
  EXPECT_TRUE(err.has_error());

  // Wrong type.
  err = store.SetInt("bool", false);
  EXPECT_TRUE(err.has_error());

  constexpr int kNewInt = 15;
  err = store.SetInt("int", kNewInt);
  ASSERT_FALSE(err.has_error());
  EXPECT_EQ(store.GetInt("int"), kNewInt);
}

TEST(SettingStore, ListOptions) {
  Err err;
  SettingStore store(GetSchema(), nullptr);

  // Attemp to add a valid item to the list with options.
  err = store.SetList("list_with_options", {kDefaultString});
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Add an option that doesn't exist.
  err = store.SetList("list_with_options", {"some_weird_option"});
  EXPECT_TRUE(err.has_error());
}

TEST(SettingStore, Fallback) {
  SettingStore fallback2(GetSchema(), nullptr);
  std::vector<std::string> new_list = {"new", "list"};
  fallback2.SetList("list", new_list);

  SettingStore fallback(GetSchema(), &fallback2);
  std::string new_string = "new string";
  fallback.SetString("string", new_string);

  SettingStore store(GetSchema(), &fallback);
  store.SetBool("bool", false);

  // Should get default for not overridden.
  auto value = store.GetValue("int");
  ASSERT_TRUE(value.is_int());
  EXPECT_EQ(value.get_int(), kDefaultInt);

  // Should get local level.
  value = store.GetValue("bool");
  ASSERT_TRUE(value.is_bool());
  EXPECT_FALSE(value.get_bool());

  // Should get one override hop.
  value = store.GetValue("string");
  ASSERT_TRUE(value.is_string());
  EXPECT_EQ(value.get_string(), new_string);

  // Should fallback through the chain.
  value = store.GetValue("list");
  ASSERT_TRUE(value.is_list());
  EXPECT_EQ(value.get_list(), new_list);
}

TEST(SettingStore, Notifications) {
  SettingStore store(GetSchema(), nullptr);

  SettingObserver observer;
  store.AddObserver("int", &observer);
  store.AddObserver("list", &observer);

  // Getting values should not notify.
  store.GetBool("bool");
  store.GetInt("int");
  store.GetString("string");
  store.GetList("list");
  EXPECT_TRUE(observer.notifications().empty());

  Err err;

  // Setting another value should not notify.
  err = store.SetBool("bool", false);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_TRUE(observer.notifications().empty());

  // Setting the int should call.
  constexpr int kNewInt = 15;
  err = store.SetInt("int", kNewInt);
  EXPECT_FALSE(err.has_error()) << err.msg();

  ASSERT_EQ(observer.notifications().size(), 1u);
  auto record = observer.notifications().back();
  EXPECT_EQ(record.store, &store);
  EXPECT_EQ(record.name, "int");
  ASSERT_TRUE(record.value.is_int());
  EXPECT_EQ(record.value.get_int(), kNewInt);

  // List should also call.
  std::vector<std::string> new_list = {"new", "list"};
  err = store.SetList("list", new_list);
  EXPECT_FALSE(err.has_error()) << err.msg();

  ASSERT_EQ(observer.notifications().size(), 2u);
  record = observer.notifications().back();
  EXPECT_EQ(record.store, &store);
  EXPECT_EQ(record.name, "list");
  ASSERT_TRUE(record.value.is_list());
  EXPECT_EQ(record.value.get_list(), new_list);

  // Removing an observer should not make to stop notifying.
  store.RemoveObserver("int", &observer);
  err = store.SetInt("int", 55);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(observer.notifications().size(), 2u);

  // But not for the other one.
  new_list.push_back("another value");
  err = store.SetList("list", new_list);
  EXPECT_FALSE(err.has_error()) << err.msg();

  ASSERT_EQ(observer.notifications().size(), 3u);
  record = observer.notifications().back();
  EXPECT_EQ(record.store, &store);
  EXPECT_EQ(record.name, "list");
  ASSERT_TRUE(record.value.is_list());
  EXPECT_EQ(record.value.get_list(), new_list);

  // Adding another observer should notify twice.
  SettingObserver observer2;
  store.AddObserver("list", &observer2);
  new_list.push_back("yet another value");
  err = store.SetList("list", new_list);
  EXPECT_FALSE(err.has_error()) << err.msg();

  ASSERT_EQ(observer.notifications().size(), 4u);
  record = observer.notifications().back();
  EXPECT_EQ(record.store, &store);
  EXPECT_EQ(record.name, "list");
  ASSERT_TRUE(record.value.is_list());
  EXPECT_EQ(record.value.get_list(), new_list);

  ASSERT_EQ(observer2.notifications().size(), 1u);
  record = observer2.notifications().back();
  EXPECT_EQ(record.store, &store);
  EXPECT_EQ(record.name, "list");
  ASSERT_TRUE(record.value.is_list());
  EXPECT_EQ(record.value.get_list(), new_list);

  // Removing the first one should still notify the second.
  store.RemoveObserver("list", &observer);
  new_list.push_back("even another value?");
  err = store.SetList("list", new_list);
  EXPECT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(observer.notifications().size(), 4u);

  ASSERT_EQ(observer2.notifications().size(), 2u);
  record = observer2.notifications().back();
  EXPECT_EQ(record.store, &store);
  EXPECT_EQ(record.name, "list");
  ASSERT_TRUE(record.value.is_list());
  EXPECT_EQ(record.value.get_list(), new_list);

  // Removing all observers should not notify.
  store.RemoveObserver("list", &observer2);

  err = store.SetBool("bool", true);
  EXPECT_FALSE(err.has_error()) << err.msg();
  err = store.SetInt("int", 22);
  EXPECT_FALSE(err.has_error()) << err.msg();
  err = store.SetString("string", "blah");
  EXPECT_FALSE(err.has_error()) << err.msg();
  err = store.SetList("list", {"meh"});
  EXPECT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(observer.notifications().size(), 4u);
  EXPECT_EQ(observer2.notifications().size(), 2u);
}

}  // namespace zxdb
