// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDKTL_INCLUDE_DDKTL_DEVICE_GROUP_H_
#define SRC_LIB_DDKTL_INCLUDE_DDKTL_DEVICE_GROUP_H_

#include <lib/ddk/device.h>

namespace ddk {

class DeviceGroupBindRule {
 public:
  static DeviceGroupBindRule CreateWithIntList(device_bind_prop_key_t key,
                                               device_bind_rule_condition condition,
                                               cpp20::span<const uint32_t> values) {
    auto bind_prop_values = std::vector<device_bind_prop_value_t>(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
      bind_prop_values[i] = device_bind_prop_int_val(values[i]);
    }

    return DeviceGroupBindRule(key, condition, std::move(bind_prop_values));
  }

  static DeviceGroupBindRule CreateWithStringList(device_bind_prop_key_t key,
                                                  device_bind_rule_condition condition,
                                                  cpp20::span<const char*> values) {
    auto bind_prop_values = std::vector<device_bind_prop_value_t>(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
      bind_prop_values[i] = device_bind_prop_str_val(values[i]);
    }

    return DeviceGroupBindRule(key, condition, std::move(bind_prop_values));
  }

  DeviceGroupBindRule(device_bind_prop_key_t key, device_bind_rule_condition condition,
                      device_bind_prop_value_t value) {
    value_data_.push_back(value);
    rule_ = device_group_bind_rule_t{
        .key = key,
        .condition = condition,
        .values = value_data_.data(),
        .values_count = std::size(value_data_),
    };
  }

  DeviceGroupBindRule(device_bind_prop_key_t key, device_bind_rule_condition condition,
                      std::vector<device_bind_prop_value_t> values)
      : value_data_(values) {
    rule_ = device_group_bind_rule_t{
        .key = key,
        .condition = condition,
        .values = value_data_.data(),
        .values_count = std::size(value_data_),
    };
  }

  DeviceGroupBindRule& operator=(const DeviceGroupBindRule& other) {
    value_data_.clear();
    for (size_t i = 0; i < other.value_data_.size(); ++i) {
      value_data_.push_back(other.value_data_[i]);
    }
    return *this;
  }

  DeviceGroupBindRule(const DeviceGroupBindRule& other) { *this = other; }

  const device_group_bind_rule_t& get() const { return rule_; }

 private:
  // Contains the data for bind property values.
  std::vector<device_bind_prop_value_t> value_data_;

  device_group_bind_rule_t rule_;
};

// Factory functions to create a DeviceGroupBindRule.
// std::string values passed in the functions must outlive the returned value.
inline DeviceGroupBindRule MakeAcceptBindRule(uint32_t key, uint32_t val) {
  return DeviceGroupBindRule(device_bind_prop_int_key(key), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                             device_bind_prop_int_val(val));
}

inline DeviceGroupBindRule MakeAcceptBindRule(const std::string& key, uint32_t val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key.c_str()),
                             DEVICE_BIND_RULE_CONDITION_ACCEPT, device_bind_prop_int_val(val));
}

inline DeviceGroupBindRule MakeAcceptBindRule(const char* key, uint32_t val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                             device_bind_prop_int_val(val));
}

inline DeviceGroupBindRule MakeAcceptBindRule(const std::string& key, bool val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key.c_str()),
                             DEVICE_BIND_RULE_CONDITION_ACCEPT, device_bind_prop_bool_val(val));
}

inline DeviceGroupBindRule MakeAcceptBindRule(const char* key, bool val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                             device_bind_prop_bool_val(val));
}

inline DeviceGroupBindRule MakeAcceptBindRule(const std::string& key, const std::string& val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key.c_str()),
                             DEVICE_BIND_RULE_CONDITION_ACCEPT,
                             device_bind_prop_str_val(val.c_str()));
}

inline DeviceGroupBindRule MakeAcceptBindRule(const char* key, const std::string& val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                             device_bind_prop_str_val(val.c_str()));
}

inline DeviceGroupBindRule MakeAcceptBindRule(const std::string& key, const char* val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key.c_str()),
                             DEVICE_BIND_RULE_CONDITION_ACCEPT, device_bind_prop_str_val(val));
}

inline DeviceGroupBindRule MakeAcceptBindRule(const char* key, const char* val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                             device_bind_prop_str_val(val));
}

inline DeviceGroupBindRule MakeRejectBindRule(uint32_t key, uint32_t val) {
  return DeviceGroupBindRule(device_bind_prop_int_key(key), DEVICE_BIND_RULE_CONDITION_REJECT,
                             device_bind_prop_int_val(val));
}

inline DeviceGroupBindRule MakeRejectBindRule(const std::string& key, uint32_t val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key.c_str()),
                             DEVICE_BIND_RULE_CONDITION_REJECT, device_bind_prop_int_val(val));
}

inline DeviceGroupBindRule MakeRejectBindRule(const char* key, uint32_t val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_REJECT,
                             device_bind_prop_int_val(val));
}

inline DeviceGroupBindRule MakeRejectBindRule(const std::string& key, bool val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key.c_str()),
                             DEVICE_BIND_RULE_CONDITION_REJECT, device_bind_prop_bool_val(val));
}

inline DeviceGroupBindRule MakeRejectBindRule(const char* key, bool val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_REJECT,
                             device_bind_prop_bool_val(val));
}

inline DeviceGroupBindRule MakeRejectBindRule(const std::string& key, const std::string& val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key.c_str()),
                             DEVICE_BIND_RULE_CONDITION_REJECT,
                             device_bind_prop_str_val(val.c_str()));
}

inline DeviceGroupBindRule MakeRejectBindRule(const char* key, const std::string& val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_REJECT,
                             device_bind_prop_str_val(val.c_str()));
}

inline DeviceGroupBindRule MakeRejectBindRule(const std::string& key, const char* val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key.c_str()),
                             DEVICE_BIND_RULE_CONDITION_REJECT, device_bind_prop_str_val(val));
}

inline DeviceGroupBindRule MakeRejectBindRule(const char* key, const char* val) {
  return DeviceGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_REJECT,
                             device_bind_prop_str_val(val));
}

inline DeviceGroupBindRule BindRuleAcceptList(uint32_t key, cpp20::span<const uint32_t> values) {
  return ddk::DeviceGroupBindRule::CreateWithIntList(device_bind_prop_int_key(key),
                                                     DEVICE_BIND_RULE_CONDITION_ACCEPT, values);
}

inline DeviceGroupBindRule BindRuleAcceptList(const std::string& key,
                                              cpp20::span<const uint32_t> values) {
  return ddk::DeviceGroupBindRule::CreateWithIntList(device_bind_prop_str_key(key.c_str()),
                                                     DEVICE_BIND_RULE_CONDITION_ACCEPT, values);
}

inline DeviceGroupBindRule BindRuleAcceptList(const char* key, cpp20::span<const uint32_t> values) {
  return ddk::DeviceGroupBindRule::CreateWithIntList(device_bind_prop_str_key(key),
                                                     DEVICE_BIND_RULE_CONDITION_ACCEPT, values);
}

inline DeviceGroupBindRule BindRuleAcceptList(const std::string& key,
                                              cpp20::span<const char*> values) {
  return ddk::DeviceGroupBindRule::CreateWithStringList(device_bind_prop_str_key(key.c_str()),
                                                        DEVICE_BIND_RULE_CONDITION_ACCEPT, values);
}

inline DeviceGroupBindRule BindRuleAcceptList(const char* key, cpp20::span<const char*> values) {
  return ddk::DeviceGroupBindRule::CreateWithStringList(device_bind_prop_str_key(key),
                                                        DEVICE_BIND_RULE_CONDITION_ACCEPT, values);
}

inline DeviceGroupBindRule BindRuleRejectList(uint32_t key, cpp20::span<const uint32_t> values) {
  return ddk::DeviceGroupBindRule::CreateWithIntList(device_bind_prop_int_key(key),
                                                     DEVICE_BIND_RULE_CONDITION_REJECT, values);
}

inline DeviceGroupBindRule BindRuleRejectList(const std::string& key,
                                              cpp20::span<const uint32_t> values) {
  return ddk::DeviceGroupBindRule::CreateWithIntList(device_bind_prop_str_key(key.c_str()),
                                                     DEVICE_BIND_RULE_CONDITION_REJECT, values);
}

inline DeviceGroupBindRule BindRuleRejectList(const char* key, cpp20::span<const uint32_t> values) {
  return ddk::DeviceGroupBindRule::CreateWithIntList(device_bind_prop_str_key(key),
                                                     DEVICE_BIND_RULE_CONDITION_REJECT, values);
}

inline DeviceGroupBindRule BindRuleRejectList(const std::string& key,
                                              cpp20::span<const char*> values) {
  return ddk::DeviceGroupBindRule::CreateWithStringList(device_bind_prop_str_key(key.c_str()),
                                                        DEVICE_BIND_RULE_CONDITION_REJECT, values);
}

inline DeviceGroupBindRule BindRuleRejectList(const char* key, cpp20::span<const char*> values) {
  return ddk::DeviceGroupBindRule::CreateWithStringList(device_bind_prop_str_key(key),
                                                        DEVICE_BIND_RULE_CONDITION_REJECT, values);
}

// Factory functions to create a device_bind_prop_t.
// std::string values passed in the functions must outlive the returned value.
inline device_bind_prop_t MakeProperty(uint32_t key, uint32_t val) {
  return {device_bind_prop_int_key(key), device_bind_prop_int_val(val)};
}

inline device_bind_prop_t MakeProperty(const std::string& key, uint32_t val) {
  return {device_bind_prop_str_key(key.c_str()), device_bind_prop_int_val(val)};
}

inline device_bind_prop_t MakeProperty(const char* key, uint32_t val) {
  return {device_bind_prop_str_key(key), device_bind_prop_int_val(val)};
}

inline device_bind_prop_t MakeProperty(const std::string& key, bool val) {
  return {device_bind_prop_str_key(key.c_str()), device_bind_prop_bool_val(val)};
}

inline device_bind_prop_t MakeProperty(const char* key, bool val) {
  return {device_bind_prop_str_key(key), device_bind_prop_bool_val(val)};
}

inline device_bind_prop_t MakeProperty(const std::string& key, const std::string& val) {
  return {device_bind_prop_str_key(key.c_str()), device_bind_prop_str_val(val.c_str())};
}

inline device_bind_prop_t MakeProperty(const char* key, const std::string& val) {
  return {device_bind_prop_str_key(key), device_bind_prop_str_val(val.c_str())};
}

inline device_bind_prop_t MakeProperty(const std::string& key, const char* val) {
  return {device_bind_prop_str_key(key.c_str()), device_bind_prop_str_val(val)};
}

inline device_bind_prop_t MakeProperty(const char* key, const char* val) {
  return {device_bind_prop_str_key(key), device_bind_prop_str_val(val)};
}

class DeviceGroupDesc {
 public:
  DeviceGroupDesc(cpp20::span<const DeviceGroupBindRule> primary_node_bind_rules,
                  cpp20::span<const device_bind_prop_t> primary_node_bind_properties) {
    AddNode(primary_node_bind_rules, primary_node_bind_properties);
    desc_.nodes = nodes_.data();
  }

  DeviceGroupDesc& operator=(const DeviceGroupDesc& other) {
    desc_ = other.desc_;

    nodes_.clear();
    bind_rules_data_.clear();
    bind_properties_data_.clear();
    for (size_t i = 0; i < other.nodes_.size(); ++i) {
      AddNode(other.nodes_[i]);
    }

    desc_.nodes = nodes_.data();
    return *this;
  }

  DeviceGroupDesc(const DeviceGroupDesc& other) { *this = other; }

  // Add a node to |nodes_| and store the property data in |prop_data_|.
  DeviceGroupDesc& AddNode(cpp20::span<const DeviceGroupBindRule> rules,
                           cpp20::span<const device_bind_prop_t> properties) {
    auto bind_rule_count = rules.size();
    auto bind_rules = std::vector<device_group_bind_rule_t>(bind_rule_count);
    for (size_t i = 0; i < bind_rule_count; i++) {
      bind_rules[i] = rules[i].get();
    }

    auto bind_property_count = properties.size();
    auto bind_properties = std::vector<device_bind_prop_t>();
    for (size_t i = 0; i < bind_property_count; i++) {
      bind_properties.push_back(properties[i]);
    }

    nodes_.push_back(device_group_node_t{
        .bind_rules = bind_rules.data(),
        .bind_rule_count = bind_rule_count,
        .bind_properties = bind_properties.data(),
        .bind_property_count = bind_property_count,
    });
    desc_.nodes_count = std::size(nodes_);

    bind_rules_data_.push_back(std::move(bind_rules));
    bind_properties_data_.push_back(std::move(bind_properties));
    return *this;
  }

  DeviceGroupDesc& set_metadata(cpp20::span<const device_metadata_t> metadata) {
    desc_.metadata_list = metadata.data();
    desc_.metadata_count = static_cast<uint32_t>(metadata.size());
    return *this;
  }

  DeviceGroupDesc& set_spawn_colocated(bool spawn_colocated) {
    desc_.spawn_colocated = spawn_colocated;
    return *this;
  }

  const device_group_desc_t& get() const { return desc_; }

 private:
  // Add a node to |nodes_| and store the property data in |prop_data_|.
  void AddNode(const device_group_node_t& node) {
    auto bind_rule_count = node.bind_rule_count;
    auto bind_rules = std::vector<device_group_bind_rule_t>(bind_rule_count);
    for (size_t i = 0; i < bind_rule_count; i++) {
      bind_rules[i] = node.bind_rules[i];
    }

    auto bind_property_count = node.bind_property_count;
    auto bind_properties = std::vector<device_bind_prop_t>();
    for (size_t i = 0; i < bind_property_count; i++) {
      bind_properties.push_back(node.bind_properties[i]);
    }

    nodes_.push_back(device_group_node_t{
        .bind_rules = bind_rules.data(),
        .bind_rule_count = bind_rule_count,
        .bind_properties = bind_properties.data(),
        .bind_property_count = bind_property_count,
    });
    desc_.nodes_count = std::size(nodes_);

    bind_rules_data_.push_back(std::move(bind_rules));
    bind_properties_data_.push_back(std::move(bind_properties));
  }

  std::vector<device_group_node_t> nodes_;

  // Stores all the bind rules data in |nodes_|.
  std::vector<std::vector<device_group_bind_rule_t>> bind_rules_data_;

  // Stores all bind_properties data in |nodes_|.
  std::vector<std::vector<device_bind_prop_t>> bind_properties_data_;

  device_group_desc_t desc_ = {};
};

}  // namespace ddk

#endif  // SRC_LIB_DDKTL_INCLUDE_DDKTL_DEVICE_GROUP_H_
