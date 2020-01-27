// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_MESSAGE_GRAPH_H_
#define TOOLS_FIDLCAT_LIB_MESSAGE_GRAPH_H_

#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fidlcat {

// The MessageGraph and nodes templates are used to  store a graph representing an execution. We use
// one for the execution stored in the golden file, instantiated with GoldenBase, and one for the
// current execution, instantiated with ActualBase.
//
// An execution is encoded as nodes, containing some information depending on their type, and links
// between those nodes. For now, we have nodes for messages, handles, pids and tids, but more types
// of nodes can be introduced.
//
// Each node contains two categories of links, dependencies and reverse dependencies:
// - dependencies are unique per node, that is to say a node has at most one dependancy link
// for a given dependency type. They record for instance that a message node depends on its tid.
// Beside, they form an acyclic graph when taken other all the nodes, and can hence be stored with
// shared pointers.
// - reverse dependencies: as their name indicates, they are added from a node A to a node B with
// type t, if B depends on A with type t. They are not unique (a tid may have multiple messages that
// depend on it), and can create cycles when taken together with dependencies.
//
// Each dependency has a type and a pointer to a node. The type of a dependency is simply a pair of
// integers (type_node_a, type_link_to_node_b), for a dependency from node_a to node_b. For instance
// a dependency of type (kMessageNode, kTidNode) from a message to the tid that produced this
// message. Or for handles, a depdendency of type (kMessageNode, kHandleNode + 0) from a message to
// the first handle appearing in this message, (kMessageNode, kHandleNode + 1) to the following
// handle... We use the same type for the reverse dependency : for a reverse dependency from node a
// to node b, the type is that of a dependency from b to a, ie (type_node_b, type_link_to_node_a).
//
// We direct dependencies as follows:
// message depends on handle
// message depends on tid
// tid depends on pid

using DependencyType = std::pair<int, int>;

// Node types
static constexpr int kPidNode = 1;
static constexpr int kTidNode = 2;
static constexpr int kMessageNode = 3;
static constexpr int kMessageInputNode = 4;
static constexpr int kZxWriteMessageNode = 5;
static constexpr int kZxReadMessageNode = 6;
// following ids are reserved for the case of multiple handles in a message.
static constexpr int kHandleNode = 100;

template <class BaseNode>
class Node : public BaseNode {
  using DependencyMap = typename std::map<DependencyType, std::shared_ptr<Node<BaseNode>>>;
  using ReverseDependencyMap =
      typename std::map<DependencyType, std::vector<std::weak_ptr<Node<BaseNode>>>>;

 public:
  Node() {}
  virtual ~Node() = default;

  const ReverseDependencyMap& reverse_dependencies() const { return reverse_dependencies_; }

  const DependencyMap& dependencies() const { return dependencies_; }

  typename ReverseDependencyMap::const_iterator get_reverse_dependencies_by_type(
      DependencyType link_type) const {
    return reverse_dependencies_.find(link_type);
  }

  std::shared_ptr<Node<BaseNode>> get_dependency_by_type(DependencyType link_type) {
    auto result = dependencies_.find(link_type);
    if (result == dependencies_.end()) {
      return nullptr;
    }
    return result->second;
  }

  // Adds a new dependency to the node (note that this removes the previous dependency of the same
  // name if there was one.)
  void AddDependency(DependencyType link_type, std::shared_ptr<Node<BaseNode>> node) {
    dependencies_[link_type] = node;
  }

  // Adds a new reverse dependency to the node.
  void AddReverseDependency(DependencyType link_type, std::shared_ptr<Node<BaseNode>> node) {
    auto vector_ptr = reverse_dependencies_.find(link_type);
    if (vector_ptr == reverse_dependencies_.end()) {
      reverse_dependencies_[link_type] = {node};
    } else {
      vector_ptr->second.push_back(node);
    }
  }

  virtual void PrintNode(std::ostream& output) const = 0;

  friend std::ostream& operator<<(std::ostream& output, Node<BaseNode> const& node) {
    node.PrintNode(output);
    return output;
  }

 protected:
  DependencyMap dependencies_;
  ReverseDependencyMap reverse_dependencies_;
};

template <class BaseNode>
class PidNode : public Node<BaseNode> {
 public:
  PidNode(uint64_t pid, std::string_view process_name) : pid_(pid), process_name_(process_name) {}

  void PrintNode(std::ostream& output) const override {
    BaseNode::PrintNode(output);
    output << "pid node: " << pid_ << " ";
  }

 private:
  uint64_t pid_;
  std::string process_name_;
};

template <class BaseNode>
class TidNode : public Node<BaseNode> {
 public:
  TidNode(uint64_t tid, std::shared_ptr<PidNode<BaseNode>> pid_node) : tid_(tid) {
    Node<BaseNode>::AddDependency(DependencyType(kTidNode, kPidNode), pid_node);
  }

  void PrintNode(std::ostream& output) const override {
    BaseNode::PrintNode(output);
    output << "tid node: " << tid_ << " ";
  }

 private:
  uint64_t tid_;
};

template <class BaseNode>
class HandleNode : public Node<BaseNode> {
 public:
  HandleNode(uint32_t handle) : handle_(handle) {}

  void PrintNode(std::ostream& output) const override {
    BaseNode::PrintNode(output);
    output << "handle node: " << std::hex << handle_ << std::dec << " ";
  }

 private:
  uint32_t handle_;
};

// On top of storing the message as a string, this class stores an extra field containing the
// message type (a channel write, read, ...). This field allow to have more precise dependency
// types: (kZxReadMessageNode to kHandle instead of simply kMessageNode to kHandle).
template <class BaseNode>
class MessageNode : public Node<BaseNode> {
 public:
  MessageNode(std::string_view message, std::shared_ptr<TidNode<BaseNode>> tid_node)
      : message_txt_(message) {
    // We use .find() here as we only have access to messages as text, but this could be made nicer
    // once we get messages as a data structure.
    if (message.find("zx_channel_write") != std::string::npos) {
      message_type_ = kZxWriteMessageNode;
    } else if (message.find("zx_channel_read") != std::string::npos) {
      message_type_ = kZxReadMessageNode;
    } else {
      message_type_ = kMessageNode;
    }
    Node<BaseNode>::AddDependency(DependencyType(message_type_, kTidNode), tid_node);
  }

  const std::string& message() const { return message_txt_; }

  int message_type() const { return message_type_; }

  void PrintNode(std::ostream& output) const override {
    BaseNode::PrintNode(output);
    output << "message node: " << message_txt_ << " ";
  }

 private:
  std::string message_txt_;
  int message_type_;
};

template <class BaseNode>
class MessageGraph {
 public:
  MessageGraph() {}

  // Creates the given message node. The string message should not contain any header. If the
  // message to be inserted is an output message, input_message_node should point to the
  // corresponding input message. All necessary dependencies (to handle nodes, tid node, pid node)
  // are created, creating the nodes to depend on if they do not exist already.
  std::shared_ptr<MessageNode<BaseNode>> InsertMessage(
      std::string_view process_name, uint64_t pid, uint64_t tid, std::string_view message,
      std::shared_ptr<MessageNode<BaseNode>> input_message_node = nullptr) {
    auto tid_node = get_tid_node(tid);
    if (!tid_node) {
      tid_node = NewTidNode(tid, pid, process_name);
    }

    // All handles are replace with handle_0, handle_1, ... according to their order of appearance,
    // and depdendencies to those handle nodes are added
    std::string corrected_message = std::string(message);
    std::vector<uint32_t> handles_order_of_appearance =
        ReplacesHandlesWithTokens(&corrected_message);
    auto message_node = std::make_shared<MessageNode<BaseNode>>(corrected_message, tid_node);
    for (size_t i = 0; i < handles_order_of_appearance.size(); i++) {
      uint32_t handle_value = handles_order_of_appearance[i];
      auto handle_node = get_handle_node(handle_value);
      if (!handle_node) {
        handle_node = NewHandleNode(handle_value);
      }
      DependencyType handle_dependency_type =
          DependencyType(message_node->message_type(), i + kHandleNode);
      message_node->AddDependency(handle_dependency_type, handle_node);
      handle_node->AddReverseDependency(handle_dependency_type, message_node);
    }

    tid_node->AddReverseDependency(DependencyType(message_node->message_type(), kTidNode),
                                   message_node);

    // The message node is added to the map containing all messages
    auto message_node_vector = message_nodes_.find(message_node->message());
    if (message_node_vector == message_nodes_.end()) {
      message_nodes_[message_node->message()] = {message_node};
    } else {
      message_node_vector->second.push_back(message_node);
    }

    // We are currently inserting an output message node, and need to link it to its input
    if (input_message_node) {
      DependencyType input_dependency_type =
          DependencyType(message_node->message_type(), kMessageInputNode);
      message_node->AddDependency(input_dependency_type, input_message_node);
      input_message_node->AddReverseDependency(input_dependency_type, message_node);
    }

    return message_node;
  }

  const std::map<std::string, std::vector<std::shared_ptr<MessageNode<BaseNode>>>>& message_nodes()
      const {
    return message_nodes_;
  }

  const std::map<uint64_t, std::shared_ptr<PidNode<BaseNode>>>& pid_nodes() const {
    return pid_nodes_;
  }

  const std::map<uint32_t, std::shared_ptr<HandleNode<BaseNode>>>& handle_nodes() const {
    return handle_nodes_;
  }

  const std::map<uint64_t, std::shared_ptr<TidNode<BaseNode>>>& tid_nodes() const {
    return tid_nodes_;
  }

  // Returns the given tid node, or an empty pointer if it does not exist
  std::shared_ptr<TidNode<BaseNode>> get_tid_node(uint64_t tid) const {
    auto tid_node_in_map = tid_nodes_.find(tid);
    if (tid_node_in_map != tid_nodes_.end()) {
      return tid_node_in_map->second;
    }
    return nullptr;
  }

  // Returns the given pid node, or an empty pointer if it does not exist
  std::shared_ptr<PidNode<BaseNode>> get_pid_node(uint64_t pid) const {
    auto pid_node_in_map = pid_nodes_.find(pid);
    if (pid_node_in_map != pid_nodes_.end()) {
      return pid_node_in_map->second;
    }
    return nullptr;
  }

  // Returns the given handle node, or a null pointer if it does not exist
  std::shared_ptr<HandleNode<BaseNode>> get_handle_node(uint32_t handle) const {
    auto handle_node_in_map = handle_nodes_.find(handle);
    if (handle_node_in_map != handle_nodes_.end()) {
      return handle_node_in_map->second;
    }
    return nullptr;
  }

 protected:
  // For a given message string, replaces all handle values with handle_0, handle_1..., where
  // handle_0 corresponds to the first handle appearing, handle_1 to the second one... The vector
  // returned gives the handles id in their order of appearance. For instance if the message
  // contains the handle a1 then a2, this functions returns {a1, a2}. Note that if a handle appears
  // twice in a message, it will appear twice in this vector.
  static std::vector<uint32_t> ReplacesHandlesWithTokens(std::string* message) {
    static std::vector<std::string> handle_texts = {"handle: ", "handle = "};
    std::vector<uint32_t> handle_ids;
    uint cur_token = 0;

    for (size_t i = 0; i < handle_texts.size(); i++) {
      std::string_view handle_text = handle_texts[i];
      size_t handle_position = message->find(handle_text);
      while (handle_position != std::string::npos) {
        handle_position += handle_text.length();
        uint32_t handle_value =
            ExtractHexUint32(std::string_view(*message).substr(handle_position));
        size_t handle_end = message->find_first_not_of("abcdef1234567890", handle_position);
        handle_ids.push_back(handle_value);
        message->replace(handle_position, handle_end - handle_position, std::to_string(cur_token));
        cur_token++;
        handle_position = message->find(handle_text, handle_position);
      }
    }
    return handle_ids;
  }

 private:
  // Creates the given pid node
  std::shared_ptr<PidNode<BaseNode>> NewPidNode(uint64_t pid, std::string_view process_name) {
    auto pid_node = std::make_shared<PidNode<BaseNode>>(pid, process_name);
    pid_nodes_[pid] = pid_node;
    return pid_node;
  }

  // Creates the given tid node
  std::shared_ptr<TidNode<BaseNode>> NewTidNode(uint64_t tid, uint64_t pid,
                                                std::string_view process_name) {
    auto pid_node = get_pid_node(pid);
    if (!pid_node) {
      pid_node = NewPidNode(pid, process_name);
    }
    auto tid_node = std::make_shared<TidNode<BaseNode>>(tid, pid_node);
    tid_nodes_[tid] = tid_node;
    pid_node->AddReverseDependency(DependencyType(kTidNode, kPidNode), tid_node);
    return tid_node;
  }

  std::shared_ptr<HandleNode<BaseNode>> NewHandleNode(uint32_t handle) {
    auto handle_node = std::make_shared<HandleNode<BaseNode>>(handle);
    handle_nodes_[handle] = handle_node;
    return handle_node;
  }

  static uint32_t ExtractHexUint32(std::string_view str) {
    uint32_t result = 0;
    for (size_t i = 0; i < str.size(); ++i) {
      char value = str[i];
      if ('0' <= value && value <= '9') {
        result = 16 * result + (value - '0');
      } else if ('a' <= value && value <= 'f') {
        result = 16 * result + (value - 'a' + 10);
      } else {
        break;
      }
    }
    return result;
  }

  std::map<uint64_t, std::shared_ptr<PidNode<BaseNode>>> pid_nodes_;
  std::map<uint64_t, std::shared_ptr<TidNode<BaseNode>>> tid_nodes_;
  std::map<uint32_t, std::shared_ptr<HandleNode<BaseNode>>> handle_nodes_;
  std::map<std::string, std::vector<std::shared_ptr<MessageNode<BaseNode>>>> message_nodes_;
};

// This class describes the nodes for the execution contained in the golden
// file.
class GoldenBase {
 public:
  void PrintNode(std::ostream& output) const { output << " golden "; }
};

// This class describes the nodes for the current execution. Teh only addition to GoldenBase is
// matching_golden_node_. This field is initially set to a null pointer when we create the actual
// node. When we know for sure which golden node this node corresponds to, we set it accordingly.
class ActualBase {
 public:
  void PrintNode(std::ostream& output) const { output << " actual "; }
  std::shared_ptr<Node<GoldenBase>> matching_golden_node() const { return matching_golden_node_; };

  void set_matching_golden_node(std::shared_ptr<Node<GoldenBase>> node) {
    matching_golden_node_ = node;
  };

 protected:
  std::shared_ptr<Node<GoldenBase>> matching_golden_node_;
};

using GoldenMessageGraph = MessageGraph<GoldenBase>;
using GoldenMessageNode = MessageNode<GoldenBase>;
using ActualMessageGraph = MessageGraph<ActualBase>;
using ActualNode = Node<ActualBase>;
using ActualMessageNode = MessageNode<ActualBase>;

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_MESSAGE_GRAPH_H_
