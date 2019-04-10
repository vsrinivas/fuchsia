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

fxl::RefPtr<SettingSchema> GetSchema(
    SettingSchema::Level level = SettingSchema::Level::kDefault) {
  auto schema = fxl::MakeRefCounted<SettingSchema>(level);

  SettingSchemaItem item("bool", "bool option", true);
  schema->AddSetting("bool", item);

  item = SettingSchemaItem("int", "int option", kDefaultInt);
  schema->AddSetting("int", item);

  item = SettingSchemaItem("string", "string option", kDefaultString);
  schema->AddSetting("string", item);

  item = SettingSchemaItem::StringWithOptions(
      "string_options", "string with options", kDefaultString, DefaultList());
  schema->AddSetting("string_options", item);

  item = SettingSchemaItem("list", "list option", DefaultList());
  schema->AddSetting("list", item);

  return schema;
}

class SettingObserver : public SettingStoreObserver {
 public:
  // Keep track of who called.
  struct SettingNotificationRecord {
    const SettingStore* store;
    std::string setting_name;
    SettingValue new_value;
  };

  void OnSettingChanged(const SettingStore& store,
                        const std::string& setting_name) override {
    SettingNotificationRecord record = {};
    record.store = &store;
    record.setting_name = setting_name;
    record.new_value = store.GetSetting(setting_name, false).value;
    notifications_.push_back(std::move(record));
  }

  const std::vector<SettingNotificationRecord>& notifications() const {
    return notifications_;
  }

 private:
  std::vector<SettingNotificationRecord> notifications_;
};

}  // namespace

TEST(SettingStore, Defaults) {
  SettingStore store(GetSchema(), nullptr);

  auto setting = store.GetSetting("bool");
  ASSERT_TRUE(setting.value.is_bool());
  EXPECT_TRUE(setting.value.get_bool());
  EXPECT_EQ(setting.level, SettingSchema::Level::kDefault);

  setting = store.GetSetting("int");
  ASSERT_TRUE(setting.value.is_int());
  EXPECT_EQ(setting.value.get_int(), kDefaultInt);
  EXPECT_EQ(setting.level, SettingSchema::Level::kDefault);

  setting = store.GetSetting("string");
  ASSERT_TRUE(setting.value.is_string());
  EXPECT_EQ(setting.value.get_string(), kDefaultString);
  EXPECT_EQ(setting.level, SettingSchema::Level::kDefault);

  setting = store.GetSetting("list");
  ASSERT_TRUE(setting.value.is_list());
  EXPECT_EQ(setting.value.get_list(), DefaultList());
  EXPECT_EQ(setting.level, SettingSchema::Level::kDefault);

  // Not found.
  EXPECT_TRUE(store.GetSetting("unexistent").value.is_null());
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

  // Valid options.
  err = store.SetString("string_options", "list");
  ASSERT_FALSE(err.has_error()) << err.msg();
  ASSERT_TRUE(store.GetSetting("string_options").value.is_string());
  EXPECT_EQ(store.GetString("string_options"), "list");

  // Invalid option.
  err = store.SetString("string_options", "invalid");
  EXPECT_TRUE(err.has_error());
}

TEST(SettingStore, Fallback) {
  SettingStore fallback2(GetSchema(SettingSchema::Level::kSystem), nullptr);
  std::vector<std::string> new_list = {"new", "list"};
  fallback2.SetList("list", new_list);

  SettingStore fallback(GetSchema(SettingSchema::Level::kTarget), &fallback2);
  std::string new_string = "new string";
  fallback.SetString("string", new_string);

  SettingStore store(GetSchema(SettingSchema::Level::kThread), &fallback);
  store.SetBool("bool", false);

  // Also test that the correct fallback level is communicated.

  // Should get default for not overridden.
  auto setting = store.GetSetting("int");
  ASSERT_TRUE(setting.value.is_int());
  EXPECT_EQ(setting.value.get_int(), kDefaultInt);
  EXPECT_EQ(setting.level, SettingSchema::Level::kDefault);

  // Should get local level.
  setting = store.GetSetting("bool");
  ASSERT_TRUE(setting.value.is_bool());
  EXPECT_FALSE(setting.value.get_bool());
  EXPECT_EQ(setting.level, SettingSchema::Level::kThread);

  // Should get one override hop.
  setting = store.GetSetting("string");
  ASSERT_TRUE(setting.value.is_string());
  EXPECT_EQ(setting.value.get_string(), new_string);
  EXPECT_EQ(setting.level, SettingSchema::Level::kTarget);

  // Should fallback through the chain.
  setting = store.GetSetting("list");
  ASSERT_TRUE(setting.value.is_list());
  EXPECT_EQ(setting.value.get_list(), new_list);
  EXPECT_EQ(setting.level, SettingSchema::Level::kSystem);
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
  EXPECT_EQ(record.setting_name, "int");
  ASSERT_TRUE(record.new_value.is_int());
  EXPECT_EQ(record.new_value.get_int(), kNewInt);

  // List should also call.
  std::vector<std::string> new_list = {"new", "list"};
  err = store.SetList("list", new_list);
  EXPECT_FALSE(err.has_error()) << err.msg();

  ASSERT_EQ(observer.notifications().size(), 2u);
  record = observer.notifications().back();
  EXPECT_EQ(record.store, &store);
  EXPECT_EQ(record.setting_name, "list");
  ASSERT_TRUE(record.new_value.is_list());
  EXPECT_EQ(record.new_value.get_list(), new_list);

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
  EXPECT_EQ(record.setting_name, "list");
  ASSERT_TRUE(record.new_value.is_list());
  EXPECT_EQ(record.new_value.get_list(), new_list);

  // Adding another observer should notify twice.
  SettingObserver observer2;
  store.AddObserver("list", &observer2);
  new_list.push_back("yet another value");
  err = store.SetList("list", new_list);
  EXPECT_FALSE(err.has_error()) << err.msg();

  ASSERT_EQ(observer.notifications().size(), 4u);
  record = observer.notifications().back();
  EXPECT_EQ(record.store, &store);
  EXPECT_EQ(record.setting_name, "list");
  ASSERT_TRUE(record.new_value.is_list());
  EXPECT_EQ(record.new_value.get_list(), new_list);

  ASSERT_EQ(observer2.notifications().size(), 1u);
  record = observer2.notifications().back();
  EXPECT_EQ(record.store, &store);
  EXPECT_EQ(record.setting_name, "list");
  ASSERT_TRUE(record.new_value.is_list());
  EXPECT_EQ(record.new_value.get_list(), new_list);

  // Removing the first one should still notify the second.
  store.RemoveObserver("list", &observer);
  new_list.push_back("even another value?");
  err = store.SetList("list", new_list);
  EXPECT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(observer.notifications().size(), 4u);

  ASSERT_EQ(observer2.notifications().size(), 2u);
  record = observer2.notifications().back();
  EXPECT_EQ(record.store, &store);
  EXPECT_EQ(record.setting_name, "list");
  ASSERT_TRUE(record.new_value.is_list());
  EXPECT_EQ(record.new_value.get_list(), new_list);

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
