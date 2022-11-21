// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDKTL_INCLUDE_DDKTL_NODE_GROUP_H_
#define SRC_LIB_DDKTL_INCLUDE_DDKTL_NODE_GROUP_H_

#include <lib/ddk/device.h>

namespace ddk {

class NodeGroupBindRule {
 public:
  static NodeGroupBindRule CreateWithIntList(device_bind_prop_key_t key,
                                             device_bind_rule_condition condition,
                                             cpp20::span<const uint32_t> values) {
    auto bind_prop_values = std::vector<device_bind_prop_value_t>(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
      bind_prop_values[i] = device_bind_prop_int_val(values[i]);
    }

    return NodeGroupBindRule(key, condition, std::move(bind_prop_values));
  }

  static NodeGroupBindRule CreateWithStringList(device_bind_prop_key_t key,
                                                device_bind_rule_condition condition,
                                                cpp20::span<const char*> values) {
    auto bind_prop_values = std::vector<device_bind_prop_value_t>(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
      bind_prop_values[i] = device_bind_prop_str_val(values[i]);
    }

    return NodeGroupBindRule(key, condition, std::move(bind_prop_values));
  }

  NodeGroupBindRule(device_bind_prop_key_t key, device_bind_rule_condition condition,
                    device_bind_prop_value_t value) {
    value_data_.push_back(value);
    rule_ = node_group_bind_rule_t{
        .key = key,
        .condition = condition,
        .values = value_data_.data(),
        .values_count = std::size(value_data_),
    };
  }

  NodeGroupBindRule(device_bind_prop_key_t key, device_bind_rule_condition condition,
                    std::vector<device_bind_prop_value_t> values)
      : value_data_(values) {
    rule_ = node_group_bind_rule_t{
        .key = key,
        .condition = condition,
        .values = value_data_.data(),
        .values_count = std::size(value_data_),
    };
  }

  NodeGroupBindRule& operator=(const NodeGroupBindRule& other) {
    value_data_.clear();
    for (size_t i = 0; i < other.value_data_.size(); ++i) {
      value_data_.push_back(other.value_data_[i]);
    }

    rule_ = node_group_bind_rule_t{
        .key = other.rule_.key,
        .condition = other.rule_.condition,
        .values = value_data_.data(),
        .values_count = std::size(value_data_),
    };

    return *this;
  }

  NodeGroupBindRule(const NodeGroupBindRule& other) { *this = other; }

  const node_group_bind_rule_t& get() const { return rule_; }

  std::vector<device_bind_prop_value_t> value_data() const { return value_data_; }

 private:
  // Contains the data for bind property values.
  std::vector<device_bind_prop_value_t> value_data_;

  node_group_bind_rule_t rule_;
};

// Factory functions to create a NodeGroupBindRule.
// std::string values passed in the functions must outlive the returned value.
inline NodeGroupBindRule MakeAcceptBindRule(uint32_t key, uint32_t val) {
  return NodeGroupBindRule(device_bind_prop_int_key(key), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                           device_bind_prop_int_val(val));
}

inline NodeGroupBindRule MakeAcceptBindRule(const std::string& key, uint32_t val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key.c_str()), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                           device_bind_prop_int_val(val));
}

inline NodeGroupBindRule MakeAcceptBindRule(const char* key, uint32_t val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                           device_bind_prop_int_val(val));
}

inline NodeGroupBindRule MakeAcceptBindRule(const std::string& key, bool val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key.c_str()), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                           device_bind_prop_bool_val(val));
}

inline NodeGroupBindRule MakeAcceptBindRule(const char* key, bool val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                           device_bind_prop_bool_val(val));
}

inline NodeGroupBindRule MakeAcceptBindRule(const std::string& key, const std::string& val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key.c_str()), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                           device_bind_prop_str_val(val.c_str()));
}

inline NodeGroupBindRule MakeAcceptBindRule(const char* key, const std::string& val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                           device_bind_prop_str_val(val.c_str()));
}

inline NodeGroupBindRule MakeAcceptBindRule(const std::string& key, const char* val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key.c_str()), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                           device_bind_prop_str_val(val));
}

inline NodeGroupBindRule MakeAcceptBindRule(const char* key, const char* val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_ACCEPT,
                           device_bind_prop_str_val(val));
}

inline NodeGroupBindRule MakeRejectBindRule(uint32_t key, uint32_t val) {
  return NodeGroupBindRule(device_bind_prop_int_key(key), DEVICE_BIND_RULE_CONDITION_REJECT,
                           device_bind_prop_int_val(val));
}

inline NodeGroupBindRule MakeRejectBindRule(const std::string& key, uint32_t val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key.c_str()), DEVICE_BIND_RULE_CONDITION_REJECT,
                           device_bind_prop_int_val(val));
}

inline NodeGroupBindRule MakeRejectBindRule(const char* key, uint32_t val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_REJECT,
                           device_bind_prop_int_val(val));
}

inline NodeGroupBindRule MakeRejectBindRule(const std::string& key, bool val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key.c_str()), DEVICE_BIND_RULE_CONDITION_REJECT,
                           device_bind_prop_bool_val(val));
}

inline NodeGroupBindRule MakeRejectBindRule(const char* key, bool val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_REJECT,
                           device_bind_prop_bool_val(val));
}

inline NodeGroupBindRule MakeRejectBindRule(const std::string& key, const std::string& val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key.c_str()), DEVICE_BIND_RULE_CONDITION_REJECT,
                           device_bind_prop_str_val(val.c_str()));
}

inline NodeGroupBindRule MakeRejectBindRule(const char* key, const std::string& val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_REJECT,
                           device_bind_prop_str_val(val.c_str()));
}

inline NodeGroupBindRule MakeRejectBindRule(const std::string& key, const char* val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key.c_str()), DEVICE_BIND_RULE_CONDITION_REJECT,
                           device_bind_prop_str_val(val));
}

inline NodeGroupBindRule MakeRejectBindRule(const char* key, const char* val) {
  return NodeGroupBindRule(device_bind_prop_str_key(key), DEVICE_BIND_RULE_CONDITION_REJECT,
                           device_bind_prop_str_val(val));
}

inline NodeGroupBindRule MakeAcceptBindRuleList(uint32_t key, cpp20::span<const uint32_t> values) {
  return ddk::NodeGroupBindRule::CreateWithIntList(device_bind_prop_int_key(key),
                                                   DEVICE_BIND_RULE_CONDITION_ACCEPT, values);
}

inline NodeGroupBindRule MakeAcceptBindRuleList(const std::string& key,
                                                cpp20::span<const uint32_t> values) {
  return ddk::NodeGroupBindRule::CreateWithIntList(device_bind_prop_str_key(key.c_str()),
                                                   DEVICE_BIND_RULE_CONDITION_ACCEPT, values);
}

inline NodeGroupBindRule MakeAcceptBindRuleList(const char* key,
                                                cpp20::span<const uint32_t> values) {
  return ddk::NodeGroupBindRule::CreateWithIntList(device_bind_prop_str_key(key),
                                                   DEVICE_BIND_RULE_CONDITION_ACCEPT, values);
}

inline NodeGroupBindRule MakeAcceptBindRuleList(const std::string& key,
                                                cpp20::span<const char*> values) {
  return ddk::NodeGroupBindRule::CreateWithStringList(device_bind_prop_str_key(key.c_str()),
                                                      DEVICE_BIND_RULE_CONDITION_ACCEPT, values);
}

inline NodeGroupBindRule MakeAcceptBindRuleList(const char* key, cpp20::span<const char*> values) {
  return ddk::NodeGroupBindRule::CreateWithStringList(device_bind_prop_str_key(key),
                                                      DEVICE_BIND_RULE_CONDITION_ACCEPT, values);
}

inline NodeGroupBindRule MakeRejectBindRuleList(uint32_t key, cpp20::span<const uint32_t> values) {
  return ddk::NodeGroupBindRule::CreateWithIntList(device_bind_prop_int_key(key),
                                                   DEVICE_BIND_RULE_CONDITION_REJECT, values);
}

inline NodeGroupBindRule MakeRejectBindRuleList(const std::string& key,
                                                cpp20::span<const uint32_t> values) {
  return ddk::NodeGroupBindRule::CreateWithIntList(device_bind_prop_str_key(key.c_str()),
                                                   DEVICE_BIND_RULE_CONDITION_REJECT, values);
}

inline NodeGroupBindRule MakeRejectBindRuleList(const char* key,
                                                cpp20::span<const uint32_t> values) {
  return ddk::NodeGroupBindRule::CreateWithIntList(device_bind_prop_str_key(key),
                                                   DEVICE_BIND_RULE_CONDITION_REJECT, values);
}

inline NodeGroupBindRule MakeRejectBindRuleList(const std::string& key,
                                                cpp20::span<const char*> values) {
  return ddk::NodeGroupBindRule::CreateWithStringList(device_bind_prop_str_key(key.c_str()),
                                                      DEVICE_BIND_RULE_CONDITION_REJECT, values);
}

inline NodeGroupBindRule MakeRejectBindRuleList(const char* key, cpp20::span<const char*> values) {
  return ddk::NodeGroupBindRule::CreateWithStringList(device_bind_prop_str_key(key),
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

class NodeGroupDesc {
 public:
  NodeGroupDesc(cpp20::span<const NodeGroupBindRule> bind_rules,
                cpp20::span<const device_bind_prop_t> properties) {
    AddNodeRepresentation(bind_rules, properties);
    desc_.nodes = nodes_.data();
  }

  NodeGroupDesc& operator=(const NodeGroupDesc& other) {
    desc_ = other.desc_;

    nodes_.clear();
    bind_rules_data_.clear();
    bind_rules_values_data_.clear();
    bind_properties_data_.clear();
    for (size_t i = 0; i < other.nodes_.size(); ++i) {
      AddNodeRepresentation(other.nodes_[i]);
    }

    desc_.nodes = nodes_.data();
    return *this;
  }

  NodeGroupDesc(const NodeGroupDesc& other) { *this = other; }

  // Add a node to |nodes_| and store the property data in |prop_data_|.
  NodeGroupDesc& AddNodeRepresentation(cpp20::span<const NodeGroupBindRule> rules,
                                       cpp20::span<const device_bind_prop_t> properties) {
    auto bind_rule_count = rules.size();
    auto bind_rules = std::vector<node_group_bind_rule_t>(bind_rule_count);
    for (size_t i = 0; i < bind_rule_count; i++) {
      bind_rules[i] = rules[i].get();

      auto bind_rule_values = rules[i].value_data();
      bind_rules[i].values = bind_rule_values.data();
      bind_rules_values_data_.push_back(std::move(bind_rule_values));
    }

    auto bind_property_count = properties.size();
    auto bind_properties = std::vector<device_bind_prop_t>();
    for (size_t i = 0; i < bind_property_count; i++) {
      bind_properties.push_back(properties[i]);
    }

    nodes_.push_back(node_representation_t{
        .bind_rules = bind_rules.data(),
        .bind_rule_count = bind_rule_count,
        .bind_properties = bind_properties.data(),
        .bind_property_count = bind_property_count,
    });

    bind_rules_data_.push_back(std::move(bind_rules));
    bind_properties_data_.push_back(std::move(bind_properties));

    desc_.nodes = nodes_.data();
    desc_.nodes_count = std::size(nodes_);

    return *this;
  }

  NodeGroupDesc& set_metadata(cpp20::span<const device_metadata_t> metadata) {
    desc_.metadata_list = metadata.data();
    desc_.metadata_count = static_cast<uint32_t>(metadata.size());
    return *this;
  }

  NodeGroupDesc& set_spawn_colocated(bool spawn_colocated) {
    desc_.spawn_colocated = spawn_colocated;
    return *this;
  }

  const node_group_desc_t& get() const { return desc_; }

 private:
  // Add a node to |nodes_| and store the property data in |prop_data_|.
  void AddNodeRepresentation(const node_representation_t& node) {
    auto bind_rule_count = node.bind_rule_count;
    auto bind_rules = std::vector<node_group_bind_rule_t>(bind_rule_count);
    for (size_t i = 0; i < bind_rule_count; i++) {
      auto node_rules = node.bind_rules[i];
      auto bind_rule_values = std::vector<device_bind_prop_value_t>(node_rules.values_count);
      for (size_t k = 0; k < node_rules.values_count; k++) {
        bind_rule_values[k] = node_rules.values[k];
      }

      bind_rules[i] = node_rules;
      bind_rules[i].values = bind_rule_values.data();
      bind_rules_values_data_.push_back(std::move(bind_rule_values));
    }

    auto bind_property_count = node.bind_property_count;
    auto bind_properties = std::vector<device_bind_prop_t>();
    for (size_t i = 0; i < bind_property_count; i++) {
      bind_properties.push_back(node.bind_properties[i]);
    }

    nodes_.push_back(node_representation_t{
        .bind_rules = bind_rules.data(),
        .bind_rule_count = bind_rule_count,
        .bind_properties = bind_properties.data(),
        .bind_property_count = bind_property_count,
    });
    desc_.nodes = nodes_.data();
    desc_.nodes_count = std::size(nodes_);

    bind_rules_data_.push_back(std::move(bind_rules));
    bind_properties_data_.push_back(std::move(bind_properties));
  }

  std::vector<node_representation_t> nodes_;

  // Stores all the bind rules data in |nodes_|.
  std::vector<std::vector<node_group_bind_rule_t>> bind_rules_data_;

  // Store all bind rule values data in |nodes_|.
  std::vector<std::vector<device_bind_prop_value_t>> bind_rules_values_data_;

  // Stores all bind_properties data in |nodes_|.
  std::vector<std::vector<device_bind_prop_t>> bind_properties_data_;

  node_group_desc_t desc_ = {};
};

}  // namespace ddk

#endif  // SRC_LIB_DDKTL_INCLUDE_DDKTL_NODE_GROUP_H_
