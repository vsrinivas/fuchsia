// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/inspect/cpp/hierarchy.h>

#include <algorithm>
#include <cctype>
#include <stack>
#include <vector>

namespace inspect {

namespace {
// Helper to sort an array of T by the value of T::name().
// If names are numeric unsigned integers, this function will sort numerically
// rather than lexicographically. Note that this will not handle negative or
// decimal numbers.
template <typename T>
void SortByName(std::vector<T>* values) {
  if (std::all_of(values->begin(), values->end(), [](const T& value) {
        for (char c : value.name()) {
          if (!std::isdigit(c)) {
            return false;
          }
        }
        return !value.name().empty();
      })) {
    std::sort(values->begin(), values->end(), [](const T& a, const T& b) {
      uint64_t a_val = atoll(a.name().c_str());
      uint64_t b_val = atoll(b.name().c_str());
      return a_val < b_val;
    });
  } else {
    std::sort(values->begin(), values->end(),
              [](const T& a, const T& b) { return a.name() < b.name(); });
  }
}
}  // namespace

NodeValue::NodeValue(std::string name) : name_(std::move(name)) {}

NodeValue::NodeValue(std::string name, std::vector<PropertyValue> properties)
    : name_(std::move(name)), properties_(std::move(properties)) {}

void NodeValue::Sort() { SortByName(&properties_); }

Hierarchy::Hierarchy(NodeValue node, std::vector<Hierarchy> children)
    : node_(std::move(node)), children_(std::move(children)) {}

const Hierarchy* Hierarchy::GetByPath(const std::vector<std::string>& path) const {
  const Hierarchy* current = this;
  auto path_it = path.begin();

  while (current && path_it != path.end()) {
    const Hierarchy* next = nullptr;
    for (const auto& obj : current->children_) {
      if (obj.node().name() == *path_it) {
        next = &obj;
        break;
      }
    }
    current = next;
    ++path_it;
  }
  return current;
}

void Hierarchy::Visit(fit::function<bool(const std::vector<std::string>&, Hierarchy*)> callback) {
  std::vector<std::string> path;
  // Keep a stack of hierarchies and the offset into the children vector for the hierarchy.
  // A work entry is complete once the offset == children().size().
  std::stack<std::pair<Hierarchy*, size_t>> work_stack;
  work_stack.push({this, 0});
  path.push_back(name());
  if (!callback(path, this)) {
    return;
  }

  while (!work_stack.empty()) {
    Hierarchy* cur;
    size_t child_index;

    std::tie(cur, child_index) = work_stack.top();

    if (child_index >= cur->children_.size()) {
      path.pop_back();
      work_stack.pop();
    } else {
      std::get<1>(work_stack.top())++;
      work_stack.push({&cur->children_[child_index], 0});
      auto* child = std::get<0>(work_stack.top());
      path.push_back(child->name());
      if (!callback(path, child)) {
        return;
      }
    }
  }
}

void Hierarchy::Visit(
    fit::function<bool(const std::vector<std::string>&, const Hierarchy*)> callback) const {
  const_cast<Hierarchy*>(this)->Visit(
      [&](const std::vector<std::string>& path, Hierarchy* hierarchy) {
        return callback(path, hierarchy);
      });
}

void Hierarchy::Sort() {
  node_.Sort();
  SortByName(&children_);
  for (auto& child : children_) {
    child.Sort();
  }
}

}  // namespace inspect
