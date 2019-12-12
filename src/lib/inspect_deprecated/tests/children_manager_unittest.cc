// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/inspect/deprecated/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>

#include <map>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "peridot/lib/rng/test_random.h"
#include "src/lib/callback/auto_cleanable.h"
#include "src/lib/callback/capture.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/inspect_deprecated/hierarchy.h"
#include "src/lib/inspect_deprecated/inspect.h"
#include "src/lib/inspect_deprecated/reader.h"
#include "src/lib/inspect_deprecated/testing/inspect.h"

namespace {

using testing::AllOf;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::UnorderedElementsAre;

using namespace inspect_deprecated::testing;

fit::closure SetWhenCalled(bool* value) {
  *value = false;
  return [value] { *value = true; };
}

bool NextBool(rng::Random* random) {
  auto bit_generator = random->NewBitGenerator<bool>();
  return bool(std::uniform_int_distribution(0, 1)(bit_generator));
}

struct Table {
  std::map<std::string, std::unique_ptr<Table>> children;
};

// |table_description| is a set of the full names of leaf elements.
Table TableFromTableDescription(const std::set<std::vector<std::string>>& table_description) {
  Table table;
  for (const std::vector<std::string>& leaf_full_name : table_description) {
    Table* current = &table;
    for (const std::string& short_name : leaf_full_name) {
      auto emplacement = current->children.emplace(short_name, std::make_unique<Table>());
      current = emplacement.first->second.get();
    }
  }
  return table;
}

bool PresentInTable(const Table* table, const std::vector<std::string>& full_name) {
  const Table* current = table;
  for (const auto& short_name : full_name) {
    const auto& it = current->children.find(short_name);
    if (it == current->children.end()) {
      return false;
    }
    current = it->second.get();
  }
  return true;
}

// Constructs the full names of all leaf nodes of a table of depth |depth|. The
// table has a branching factor of three and all siblings are named "a", "b",
// and "c".
std::set<std::vector<std::string>> CompleteTableDescription(int depth) {
  if (depth == 1) {
    return {{"a"}, {"b"}, {"c"}};
  } else {
    std::set<std::vector<std::string>> depth_minus_one_table = CompleteTableDescription(depth - 1);
    std::set<std::vector<std::string>> table;
    for (const auto& prefix : depth_minus_one_table) {
      for (const auto& suffix : {"a", "b", "c"}) {
        std::vector<std::string> table_entry = prefix;
        table_entry.emplace_back(suffix);
        table.insert(table_entry);
      }
    }
    return table;
  }
}

::testing::Matcher<const inspect_deprecated::ObjectHierarchy&> CompleteMatcher(int depth) {
  if (depth == 0) {
    return ChildrenMatch(IsEmpty());
  } else {
    return ChildrenMatch(
        ElementsAre(AllOf(NodeMatches(NameMatches("a")), CompleteMatcher(depth - 1)),
                    AllOf(NodeMatches(NameMatches("b")), CompleteMatcher(depth - 1)),
                    AllOf(NodeMatches(NameMatches("c")), CompleteMatcher(depth - 1))));
  }
}

class Element final : public inspect_deprecated::ChildrenManager {
 public:
  Element(async::TestLoop* test_loop, rng::Random* random, Table* table,
          inspect_deprecated::Node inspect_node)
      : random_(random),
        test_loop_(test_loop),
        table_(table),
        inspect_node_(std::move(inspect_node)),
        children_manager_retainer_(inspect_node_.SetChildrenManager(this)),
        user_serving_retention_(0),
        inspect_retention_(0),
        children_(test_loop->dispatcher()),
        weak_factory_(this) {
    children_.SetOnDiscardable([this]() { CheckDiscardable(); });
  }

  ~Element() override {
    children_.SetOnDiscardable([]() {});
  };

  bool IsDiscardable() const {
    FXL_DCHECK(0 <= user_serving_retention_);
    FXL_DCHECK(0 <= inspect_retention_);
    return user_serving_retention_ == 0 && inspect_retention_ == 0 && children_.empty();
  }

  void SetOnDiscardable(fit::closure on_discardable) {
    on_discardable_ = std::move(on_discardable);
  }

  void GetNames(fit::function<void(std::set<std::string>)> callback) override {
    std::set<std::string> names;
    for (const auto& [child_name, unused_child_unique_pointer] : table_->children) {
      names.insert(child_name);
    }

    if (NextBool(random_)) {
      async::PostTask(test_loop_->dispatcher(), [callback = std::move(callback),
                                                 names = std::move(names)]() { callback(names); });
    } else {
      callback(names);
    }
  }

  void Attach(std::string name, fit::function<void(fit::closure)> callback) override {
    if (table_->children.find(name) == table_->children.end()) {
      if (NextBool(random_)) {
        async::PostTask(test_loop_->dispatcher(),
                        [callback = std::move(callback)]() { callback([]() {}); });
      } else {
        callback([]() {});
      }
      return;
    }

    auto child = ActivateChild(name);
    auto retainer = child->RetainForInspect();
    if (NextBool(random_)) {
      async::PostTask(test_loop_->dispatcher(),
                      [callback = std::move(callback), retainer = std::move(retainer)]() mutable {
                        callback(std::move(retainer));
                      });
    } else {
      callback(std::move(retainer));
    }
  }

  std::pair<Element*, fit::closure> GetChild(const std::string& child_short_name) {
    Element* child_ptr = ActivateChild(child_short_name);

    return std::make_pair(child_ptr, child_ptr->RetainToServeUser());
  }

  void ActivateDescendant(std::vector<std::string> relative_descendant_name,
                          fit::function<void(bool, fit::closure)> callback) {
    auto implementation = [this, weak_this = weak_factory_.GetWeakPtr(),
                           relative_descendant_name = std::move(relative_descendant_name),
                           callback = std::move(callback)]() mutable {
      if (!weak_this) {
        callback(false, []() {});
        return;
      }

      auto child = ActivateChild(relative_descendant_name[0]);
      if (relative_descendant_name.size() == 1) {
        callback(true, child->RetainToServeUser());
      } else {
        child->ActivateDescendant(
            {relative_descendant_name.begin() + 1, relative_descendant_name.end()},
            std::move(callback));
      }
    };
    if (NextBool(random_)) {
      async::PostTask(test_loop_->dispatcher(), std::move(implementation));
    } else {
      implementation();
    }
  }

  void DeleteDescendant(std::vector<std::string> relative_descendant_name) {
    auto it = children_.find(relative_descendant_name[0]);
    if (it != children_.end()) {
      if (relative_descendant_name.size() == 1) {
        children_.erase(it);
      } else {
        it->second.DeleteDescendant(
            {relative_descendant_name.begin() + 1, relative_descendant_name.end()});
      }
    }
  }

  Element* DebugGetDescendant(std::vector<std::string> relative_descendant_name) {
    const auto& it = children_.find(relative_descendant_name[0]);
    if (it == children_.end()) {
      return nullptr;
    } else if (relative_descendant_name.size() == 1) {
      return &it->second;
    } else {
      return it->second.DebugGetDescendant(
          {relative_descendant_name.begin() + 1, relative_descendant_name.end()});
    }
  }

  fit::closure RetainToServeUser() {
    user_serving_retention_++;
    auto weak_this = weak_factory_.GetWeakPtr();
    return [this, weak_this]() {
      if (weak_this) {
        user_serving_retention_--;
        CheckDiscardable();
      }
    };
  }

 private:
  Element* ActivateChild(const std::string& child_short_name) {
    auto it = children_.find(child_short_name);
    if (it == children_.end()) {
      inspect_deprecated::Node child_inspect_node = inspect_node_.CreateChild(child_short_name);
      auto emplacement = children_.try_emplace(
          child_short_name, test_loop_, random_,
          table_->children.find(child_short_name)->second.get(), std::move(child_inspect_node));
      return &emplacement.first->second;
    } else {
      return &it->second;
    }
  }

  fit::closure RetainForInspect() {
    inspect_retention_++;
    return [this]() {
      inspect_retention_--;
      CheckDiscardable();
    };
  }

  void CheckDiscardable() {
    if (IsDiscardable() && on_discardable_) {
      on_discardable_();
    }
  }

  rng::Random* random_;
  async::TestLoop* test_loop_;
  Table* table_;
  inspect_deprecated::Node inspect_node_;
  fit::deferred_callback children_manager_retainer_;

  fit::closure on_discardable_;
  int64_t user_serving_retention_;
  int64_t inspect_retention_;
  callback::AutoCleanableMap<std::string, Element> children_;

  // Must be the last member.
  fxl::WeakPtrFactory<Element> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Element);
};

enum class Activity {
  ABSENT,
  INACTIVE,
  ACTIVE,
};

// Inspect-using application representative of those that use and that we think
// are likely to use a ChildrenManager. The application:
//   (1) Maintains a frequently-changing-shape variable-depth tree of elements
//     that each statically maintain an inspect_deprecated::Node.
//   (2) The application is asynchronous.
class Application final {
 public:
  Application(async::TestLoop* loop, rng::Random* random,
              inspect_deprecated::Node* application_inspect_node,
              const std::set<std::vector<std::string>>& table_description)
      : loop_(loop),
        random_(random),
        application_inspect_node_(application_inspect_node),
        table_(TableFromTableDescription(table_description)),
        elements_(loop_->dispatcher()){};

  // The "user interface" of the application, this method is called by the test
  // when the test is acting as the application's user. If the element for
  // |full_name| is not resident in memory, this method "activates" it by
  // creating an in-memory Element (and thus alters the Inspect hierarchy).
  // Passed to |callback| are:
  //   (1) A boolean "success" indicator that is representative of how in real
  //     applications analogs of this method can fail or time out.
  //   (2) A closure to call when the "user" (again, the test acting as the
  //     user) no longer needs the element to remain "activated".
  void Activate(std::vector<std::string> full_name,
                fit::function<void(bool, fit::closure)> callback) {
    auto implementation = [this, full_name = std::move(full_name),
                           callback = std::move(callback)]() mutable {
      auto& first_short_name = full_name[0];
      Element* element;
      const auto& it = elements_.find(first_short_name);
      if (it == elements_.end()) {
        inspect_deprecated::Node child_inspect_node =
            application_inspect_node_->CreateChild(first_short_name);
        auto emplacement = elements_.try_emplace(
            first_short_name, loop_, random_, table_.children.find(first_short_name)->second.get(),
            std::move(child_inspect_node));
        element = &emplacement.first->second;
      } else {
        element = &it->second;
      }
      if (full_name.size() == 1) {
        callback(true, element->RetainToServeUser());
      } else {
        element->ActivateDescendant({full_name.begin() + 1, full_name.end()}, std::move(callback));
      }
    };

    if (NextBool(random_)) {
      async::PostTask(loop_->dispatcher(), std::move(implementation));
    } else {
      implementation();
    }
  }

  // The "administrator interface" of the application, this method is called
  // when the test is acting as the application's owner and decides for whatever
  // reason that some portion of the activated elements (the element at
  // |full_name| and all elements under it) must be deleted from memory.
  void Delete(const std::vector<std::string>& full_name) {
    if (full_name.empty()) {
      while (!elements_.empty()) {
        elements_.erase(elements_.begin());
      }
    } else {
      auto it = elements_.find(full_name[0]);
      if (it != elements_.end()) {
        if (full_name.size() == 1) {
          elements_.erase(it);
        } else {
          it->second.DeleteDescendant({full_name.begin() + 1, full_name.end()});
        }
      }
    }
  }

  // Called by the test acting as the test, this method describes for use in
  // assertions whether an element is active at |full_name|, is inactive at
  // |full_name|, or is not understood as either active or inactive.
  Activity DebugGetActivity(const std::vector<std::string>& full_name) {
    if (!PresentInTable(&table_, full_name)) {
      return Activity::ABSENT;
    } else {
      auto it = elements_.find(full_name[0]);
      if (it == elements_.end()) {
        return Activity::INACTIVE;
      } else if (full_name.size() == 1) {
        return Activity::ACTIVE;
      } else {
        auto descendant = it->second.DebugGetDescendant({full_name.begin() + 1, full_name.end()});
        return descendant == nullptr ? Activity::INACTIVE : Activity::ACTIVE;
      }
    }
  }

 private:
  async::TestLoop* loop_;
  rng::Random* random_;
  inspect_deprecated::Node* application_inspect_node_;

  // Representative of the application's persistent data on disk, the set of
  // names for which the application considers elements to exist (whether
  // activated or not).
  Table table_;
  callback::AutoCleanableMap<std::string, Element> elements_;
};

constexpr char kTestTopLevelNodeName[] = "top-level-of-test node";
constexpr char kElementsInspectPathComponent[] = "elements";

class ChildrenManagerTest : public gtest::TestLoopFixture {
 public:
  ChildrenManagerTest()
      : executor_(dispatcher()),
        random_(test_loop().initial_state()),
        top_level_node_(inspect_deprecated::Node(kTestTopLevelNodeName)) {
    elements_node_ = top_level_node_.CreateChild(kElementsInspectPathComponent);
  }

 protected:
  ::testing::AssertionResult OpenElementsNode(
      fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* elements);
  ::testing::AssertionResult ReadData(
      fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* node,
      fuchsia::inspect::deprecated::Object* object);
  ::testing::AssertionResult ListChildren(
      fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* node,
      std::vector<std::string>* child_names);
  ::testing::AssertionResult OpenChild(
      fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* parent,
      const std::string& child_name,
      fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* child);
  ::testing::AssertionResult Activate(Application* application, std::vector<std::string> full_name,
                                      fit::closure* retainer);
  ::testing::AssertionResult ReadWithReaderAPI(inspect_deprecated::ObjectHierarchy* hierarchy);

  async::Executor executor_;
  rng::TestRandom random_;
  inspect_deprecated::Node top_level_node_;
  inspect_deprecated::Node elements_node_;
};

::testing::AssertionResult ChildrenManagerTest::OpenElementsNode(
    fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* elements) {
  bool callback_called;
  bool success;
  top_level_node_.object_dir().object()->OpenChild(
      kElementsInspectPathComponent, elements->NewRequest(),
      callback::Capture(SetWhenCalled(&callback_called), &success));
  RunLoopUntilIdle();
  if (!callback_called) {
    return ::testing::AssertionFailure()
           << "OpenElementsNode callback passed to OpenChild not called!";
  } else if (!success) {
    return ::testing::AssertionFailure() << "OpenElementsNode unsuccessful!";
  } else {
    return ::testing::AssertionSuccess();
  }
}

::testing::AssertionResult ChildrenManagerTest::ReadData(
    fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* node,
    fuchsia::inspect::deprecated::Object* object) {
  bool callback_called;
  (*node)->ReadData(callback::Capture(SetWhenCalled(&callback_called), object));
  RunLoopUntilIdle();
  if (!callback_called) {
    return ::testing::AssertionFailure() << "Callback passed to ReadData not called!";
  } else {
    return ::testing::AssertionSuccess();
  }
}

::testing::AssertionResult ChildrenManagerTest::ListChildren(
    fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* node,
    std::vector<std::string>* child_names) {
  bool callback_called;
  (*node)->ListChildren(callback::Capture(SetWhenCalled(&callback_called), child_names));
  RunLoopUntilIdle();
  if (!callback_called) {
    return ::testing::AssertionFailure() << "Callback passed to ListChildren not called!";
  } else {
    return ::testing::AssertionSuccess();
  }
}

::testing::AssertionResult ChildrenManagerTest::OpenChild(
    fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* parent,
    const std::string& child_name,
    fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect>* child) {
  bool callback_called;
  bool success;
  (*parent)->OpenChild(child_name, child->NewRequest(),
                       callback::Capture(SetWhenCalled(&callback_called), &success));
  RunLoopUntilIdle();
  if (!callback_called) {
    return ::testing::AssertionFailure() << "Callback passed to OpenChild not called!";
  } else if (!success) {
    return ::testing::AssertionFailure() << "OpenChild unsuccessful!";
  } else {
    return ::testing::AssertionSuccess();
  }
}

::testing::AssertionResult ChildrenManagerTest::Activate(Application* application,
                                                         std::vector<std::string> full_name,
                                                         fit::closure* retainer) {
  bool callback_called;
  bool success;
  application->Activate(std::move(full_name),
                        callback::Capture(SetWhenCalled(&callback_called), &success, retainer));
  RunLoopUntilIdle();
  if (!callback_called) {
    return ::testing::AssertionFailure() << "Callback passed to Activate not called!";
  } else if (!success) {
    return ::testing::AssertionFailure() << "Activate not successful!";
  } else {
    return ::testing::AssertionSuccess();
  }
}

::testing::AssertionResult ChildrenManagerTest::ReadWithReaderAPI(
    inspect_deprecated::ObjectHierarchy* hierarchy) {
  bool callback_called;
  bool success;
  fidl::InterfaceHandle<fuchsia::inspect::deprecated::Inspect> inspect_handle;
  top_level_node_.object_dir().object()->OpenChild(
      kElementsInspectPathComponent, inspect_handle.NewRequest(),
      callback::Capture(SetWhenCalled(&callback_called), &success));
  RunLoopUntilIdle();
  if (!callback_called) {
    return ::testing::AssertionFailure() << "Callback passed to OpenChild not called!";
  } else if (!success) {
    return ::testing::AssertionFailure() << "OpenChild not successful!";
  }

  callback_called = false;
  fit::result<inspect_deprecated::ObjectHierarchy> hierarchy_result;
  auto hierarchy_promise =
      inspect_deprecated::ReadFromFidl(inspect_deprecated::ObjectReader(std::move(inspect_handle)))
          .then([&](fit::result<inspect_deprecated::ObjectHierarchy>& then_hierarchy_result) {
            callback_called = true;
            hierarchy_result = std::move(then_hierarchy_result);
          });
  executor_.schedule_task(std::move(hierarchy_promise));
  RunLoopUntilIdle();
  if (!callback_called) {
    return ::testing::AssertionFailure()
           << "Callback passed to ReadFromFidl(<...>).then not called!";
  } else if (!hierarchy_result.is_ok()) {
    return ::testing::AssertionFailure() << "Hierarchy result not okay!";
  }
  *hierarchy = hierarchy_result.take_value();
  return ::testing::AssertionSuccess();
}

// Verifies that a single inactive element is made active by an inspection and
// made inactive by the inspection's completion.
TEST_F(ChildrenManagerTest, SingleDynamicElement) {
  std::vector<std::string> dynamic_child_full_name({"a", "b"});
  auto application =
      Application(&test_loop(), &random_, &elements_node_, {dynamic_child_full_name});
  fit::closure a_retainer;
  ASSERT_TRUE(Activate(&application, {dynamic_child_full_name[0]}, &a_retainer));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> elements_ptr;
  ASSERT_TRUE(OpenElementsNode(&elements_ptr));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_ptr;
  ASSERT_TRUE(OpenChild(&elements_ptr, dynamic_child_full_name[0], &a_ptr));

  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_b_ptr;
  ASSERT_TRUE(OpenChild(&a_ptr, dynamic_child_full_name[1], &a_b_ptr));

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fuchsia::inspect::deprecated::Object object;
  ASSERT_TRUE(ReadData(&a_b_ptr, &object));
  ASSERT_EQ(dynamic_child_full_name[1], object.name);

  a_ptr.Unbind();
  RunLoopUntilIdle();
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  a_b_ptr.Unbind();
  RunLoopUntilIdle();
  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(dynamic_child_full_name));
}

// Verifies one-quarter of the "overlap" core use case: that the user can begin
// making use of an element, an inspection can start, the inspection can end,
// the user can release the element, and the element was active exactly as long
// as it should have been.
TEST_F(ChildrenManagerTest, SingleElementInspectInsideUse) {
  std::vector<std::string> dynamic_child_full_name({"a", "b"});
  auto application =
      Application(&test_loop(), &random_, &elements_node_, {dynamic_child_full_name});
  fit::closure a_retainer;
  ASSERT_TRUE(Activate(&application, {dynamic_child_full_name[0]}, &a_retainer));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> elements_ptr;
  ASSERT_TRUE(OpenElementsNode(&elements_ptr));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_ptr;
  ASSERT_TRUE(OpenChild(&elements_ptr, dynamic_child_full_name[0], &a_ptr));

  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fit::closure a_b_retainer;
  ASSERT_TRUE(Activate(&application, dynamic_child_full_name, &a_b_retainer));

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_b_ptr;
  ASSERT_TRUE(OpenChild(&a_ptr, dynamic_child_full_name[1], &a_b_ptr));

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fuchsia::inspect::deprecated::Object object;
  ASSERT_TRUE(ReadData(&a_b_ptr, &object));
  ASSERT_EQ(dynamic_child_full_name[1], object.name);

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  a_b_ptr.Unbind();
  RunLoopUntilIdle();

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  a_b_retainer();
  RunLoopUntilIdle();

  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(dynamic_child_full_name));
}

// Verifies one-quarter of the "overlap" core use case: that an inspection can
// start, the user can start making use of an element, the inspection can end,
// the user can release the element, and the element was active exactly as long
// as it should have been.
TEST_F(ChildrenManagerTest, SingleElementInspectBeforeAndIntoUse) {
  std::vector<std::string> dynamic_child_full_name({"a", "b"});
  auto application =
      Application(&test_loop(), &random_, &elements_node_, {dynamic_child_full_name});
  fit::closure a_retainer;
  ASSERT_TRUE(Activate(&application, {dynamic_child_full_name[0]}, &a_retainer));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> elements_ptr;
  ASSERT_TRUE(OpenElementsNode(&elements_ptr));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_ptr;
  ASSERT_TRUE(OpenChild(&elements_ptr, dynamic_child_full_name[0], &a_ptr));

  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_b_ptr;
  ASSERT_TRUE(OpenChild(&a_ptr, dynamic_child_full_name[1], &a_b_ptr));

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fit::closure a_b_retainer;
  ASSERT_TRUE(Activate(&application, dynamic_child_full_name, &a_b_retainer));

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fuchsia::inspect::deprecated::Object object;
  ASSERT_TRUE(ReadData(&a_b_ptr, &object));
  ASSERT_EQ(dynamic_child_full_name[1], object.name);

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  a_b_ptr.Unbind();
  RunLoopUntilIdle();

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  a_b_retainer();
  RunLoopUntilIdle();

  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(dynamic_child_full_name));
}

// Verifies one-quarter of the "overlap" core use case: that an inspection can
// start, the user can start making use of an element, the user can release the
// element, the inspection can end, and the element was active exactly as long
// as it should have been.
TEST_F(ChildrenManagerTest, SingleElementUseInsideInspect) {
  std::vector<std::string> dynamic_child_full_name({"a", "b"});
  auto application =
      Application(&test_loop(), &random_, &elements_node_, {dynamic_child_full_name});
  fit::closure a_retainer;
  ASSERT_TRUE(Activate(&application, {dynamic_child_full_name[0]}, &a_retainer));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> elements_ptr;
  ASSERT_TRUE(OpenElementsNode(&elements_ptr));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_ptr;
  ASSERT_TRUE(OpenChild(&elements_ptr, dynamic_child_full_name[0], &a_ptr));

  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_b_ptr;
  ASSERT_TRUE(OpenChild(&a_ptr, dynamic_child_full_name[1], &a_b_ptr));

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fit::closure a_b_retainer;
  ASSERT_TRUE(Activate(&application, dynamic_child_full_name, &a_b_retainer));

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  a_b_retainer();
  RunLoopUntilIdle();

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fuchsia::inspect::deprecated::Object object;
  ASSERT_TRUE(ReadData(&a_b_ptr, &object));
  ASSERT_EQ(dynamic_child_full_name[1], object.name);

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  a_b_ptr.Unbind();
  RunLoopUntilIdle();

  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(dynamic_child_full_name));
}

// Verifies one-quarter of the "overlap" core use case: that the user can begin
// making use of an element, an inspection can start, the user can release the
// element, the inspection can end, and the element was active exactly as long
// as it should have been.
TEST_F(ChildrenManagerTest, SingleElementUseBeforeAndIntoInspect) {
  std::vector<std::string> dynamic_child_full_name({"a", "b"});
  auto application =
      Application(&test_loop(), &random_, &elements_node_, {dynamic_child_full_name});
  fit::closure a_retainer;
  ASSERT_TRUE(Activate(&application, {dynamic_child_full_name[0]}, &a_retainer));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> elements_ptr;
  ASSERT_TRUE(OpenElementsNode(&elements_ptr));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_ptr;
  ASSERT_TRUE(OpenChild(&elements_ptr, dynamic_child_full_name[0], &a_ptr));

  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fit::closure a_b_retainer;
  ASSERT_TRUE(Activate(&application, dynamic_child_full_name, &a_b_retainer));

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_b_ptr;
  ASSERT_TRUE(OpenChild(&a_ptr, dynamic_child_full_name[1], &a_b_ptr));

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  fuchsia::inspect::deprecated::Object object;
  ASSERT_TRUE(ReadData(&a_b_ptr, &object));
  ASSERT_EQ(dynamic_child_full_name[1], object.name);

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  a_b_retainer();
  RunLoopUntilIdle();

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(dynamic_child_full_name));

  a_b_ptr.Unbind();
  RunLoopUntilIdle();

  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(dynamic_child_full_name));
}

// Verifies that the application does not surrender control of the lifetimes of
// its objects: the application can delete elements in the middle of an ongoing
// inspection and the inspection completes without crashing.
TEST_F(ChildrenManagerTest, ElementsDeletedDuringInspection) {
  std::vector<std::string> deepest_child_full_name({"a", "b", "c"});
  auto application =
      Application(&test_loop(), &random_, &elements_node_, {deepest_child_full_name});

  fit::closure a_retainer;
  ASSERT_TRUE(Activate(&application, {deepest_child_full_name[0]}, &a_retainer));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> elements_ptr;
  ASSERT_TRUE(OpenElementsNode(&elements_ptr));

  bool a_error_callback_called;
  zx_status_t a_error_status;
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_ptr;
  a_ptr.set_error_handler(
      callback::Capture(SetWhenCalled(&a_error_callback_called), &a_error_status));
  ASSERT_TRUE(OpenChild(&elements_ptr, deepest_child_full_name[0], &a_ptr));

  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(deepest_child_full_name));

  bool a_b_error_callback_called;
  zx_status_t a_b_error_status;
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_b_ptr;
  a_b_ptr.set_error_handler(
      callback::Capture(SetWhenCalled(&a_b_error_callback_called), &a_b_error_status));
  ASSERT_TRUE(OpenChild(&a_ptr, deepest_child_full_name[1], &a_b_ptr));

  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(deepest_child_full_name));

  bool a_b_c_error_callback_called;
  zx_status_t a_b_c_error_status;
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_b_c_ptr;
  a_b_c_ptr.set_error_handler(
      callback::Capture(SetWhenCalled(&a_b_c_error_callback_called), &a_b_c_error_status));
  ASSERT_TRUE(OpenChild(&a_b_ptr, deepest_child_full_name[2], &a_b_c_ptr));

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(deepest_child_full_name));

  fuchsia::inspect::deprecated::Object object;
  ASSERT_TRUE(ReadData(&a_b_c_ptr, &object));
  ASSERT_EQ(deepest_child_full_name[2], object.name);

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(deepest_child_full_name));

  application.Delete({deepest_child_full_name.begin(), deepest_child_full_name.begin() + 2});
  RunLoopUntilIdle();

  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(deepest_child_full_name));
  ASSERT_EQ(Activity::INACTIVE,
            application.DebugGetActivity(
                {deepest_child_full_name.begin(), deepest_child_full_name.begin() + 2}));

  // TODO(crjohns, nathaniel): We would like it to be the case that when nodes
  // are deleted in the middle of an inspection that the FIDL connections are
  // broken and the inspection cannot continue, but... nodes being deleted in
  // the middle of an inspection is enough of an edge-case that the current
  // behavior of (1) not crashing and (2) reporting stale data is acceptable for
  // the remainder of the FIDL implementation's life.
  //
  // The behavior we'd like:
  //  ASSERT_TRUE(a_b_c_error_callback_called);
  //  ASSERT_EQ(ZX_OK, a_b_c_error_status);
  //  ASSERT_TRUE(!a_b_c_ptr.is_bound());
  //  ASSERT_EQ(Activity::INACTIVE,
  //            application.DebugGetActivity(deepest_child_full_name));
  //  ASSERT_TRUE(a_b_error_callback_called);
  //  ASSERT_EQ(ZX_OK, a_b_error_status);
  //  ASSERT_TRUE(!a_b_ptr.is_bound());
  //  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity(
  //                                    {deepest_child_full_name.begin(),
  //                                     deepest_child_full_name.begin() + 2}));
  //  ASSERT_TRUE(!a_error_callback_called);
  //  ASSERT_TRUE(a_ptr.is_bound());
  //  ASSERT_EQ(Activity::ACTIVE,
  //            application.DebugGetActivity({deepest_child_full_name[0]}));
  //
  //  ASSERT_TRUE(ReadData(&a_ptr, &object));
  //  ASSERT_EQ(deepest_child_full_name[0], object.name);
  //
  //  a_ptr.Unbind();
  //  RunLoopUntilIdle();
  //
  //  ASSERT_EQ(Activity::INACTIVE,
  //            application.DebugGetActivity({deepest_child_full_name[0]}));
}

// Verifies that activation of elements can be multi-level and that active
// inspections serve to keep active only those portions of the tree of elements
// that should be kept active.
TEST_F(ChildrenManagerTest, FiveLevelsOfDynamicism) {
  std::vector<std::string> deep_child_full_name({"a", "b", "c", "d", "e"});
  std::vector<std::string> deeper_child_full_name({"a", "b", "c", "1", "2", "3"});
  auto application = Application(&test_loop(), &random_, &elements_node_,
                                 {deep_child_full_name, deeper_child_full_name});
  fit::closure a_retainer;
  ASSERT_TRUE(Activate(&application, {"a"}, &a_retainer));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> elements_ptr;
  ASSERT_TRUE(OpenElementsNode(&elements_ptr));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_ptr;
  ASSERT_TRUE(OpenChild(&elements_ptr, "a", &a_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> b_ptr;
  ASSERT_TRUE(OpenChild(&a_ptr, "b", &b_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> c_ptr;
  ASSERT_TRUE(OpenChild(&b_ptr, "c", &c_ptr));

  std::vector<std::string> c_child_names;
  ASSERT_TRUE(ListChildren(&c_ptr, &c_child_names));
  ASSERT_THAT(c_child_names, ElementsAre("1", "d"));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> d_ptr;
  ASSERT_TRUE(OpenChild(&c_ptr, "d", &d_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> e_ptr;
  ASSERT_TRUE(OpenChild(&d_ptr, "e", &e_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> one_ptr;
  ASSERT_TRUE(OpenChild(&c_ptr, "1", &one_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> two_ptr;
  ASSERT_TRUE(OpenChild(&one_ptr, "2", &two_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> three_ptr;
  ASSERT_TRUE(OpenChild(&two_ptr, "3", &three_ptr));

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(deep_child_full_name));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(deeper_child_full_name));

  // Dropping connections to intermediate nodes doesn't cause those intermediate
  // nodes to go inactive.
  a_ptr.Unbind();
  b_ptr.Unbind();
  c_ptr.Unbind();
  d_ptr.Unbind();
  one_ptr.Unbind();
  two_ptr.Unbind();
  RunLoopUntilIdle();

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c", "d"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c", "1"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c", "1", "2"}));

  // Dropping the connection to one end of the "fork" causes the nodes on that
  // "fork" to go inactive but not the nodes on the shared "stem".
  three_ptr.Unbind();
  RunLoopUntilIdle();

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c", "d"}));
  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity({"a", "b", "c", "1"}));
  ASSERT_EQ(Activity::INACTIVE, application.DebugGetActivity({"a", "b", "c", "1", "2"}));
}

// Verifies that concurrent inspections complement one another rather than
// conflict.
TEST_F(ChildrenManagerTest, ConcurrentInspections) {
  std::vector<std::string> deep_child_full_name({"a", "b", "c", "d", "e"});
  std::vector<std::string> deeper_child_full_name({"a", "b", "c", "1", "2", "3"});
  auto application = Application(&test_loop(), &random_, &elements_node_,
                                 {deep_child_full_name, deeper_child_full_name});
  fit::closure a_retainer;
  ASSERT_TRUE(Activate(&application, {"a"}, &a_retainer));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> first_elements_ptr;
  ASSERT_TRUE(OpenElementsNode(&first_elements_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> second_elements_ptr;
  ASSERT_TRUE(OpenElementsNode(&second_elements_ptr));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> first_a_ptr;
  ASSERT_TRUE(OpenChild(&first_elements_ptr, "a", &first_a_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> first_b_ptr;
  ASSERT_TRUE(OpenChild(&first_a_ptr, "b", &first_b_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> first_c_ptr;
  ASSERT_TRUE(OpenChild(&first_b_ptr, "c", &first_c_ptr));

  std::vector<std::string> c_child_names;
  ASSERT_TRUE(ListChildren(&first_c_ptr, &c_child_names));
  ASSERT_THAT(c_child_names, ElementsAre("1", "d"));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> first_d_ptr;
  ASSERT_TRUE(OpenChild(&first_c_ptr, "d", &first_d_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> first_e_ptr;
  ASSERT_TRUE(OpenChild(&first_d_ptr, "e", &first_e_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> first_one_ptr;
  ASSERT_TRUE(OpenChild(&first_c_ptr, "1", &first_one_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> first_two_ptr;
  ASSERT_TRUE(OpenChild(&first_one_ptr, "2", &first_two_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> first_three_ptr;
  ASSERT_TRUE(OpenChild(&first_two_ptr, "3", &first_three_ptr));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> second_a_ptr;
  ASSERT_TRUE(OpenChild(&second_elements_ptr, "a", &second_a_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> second_b_ptr;
  ASSERT_TRUE(OpenChild(&second_a_ptr, "b", &second_b_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> second_c_ptr;
  ASSERT_TRUE(OpenChild(&second_b_ptr, "c", &second_c_ptr));

  ASSERT_TRUE(ListChildren(&second_c_ptr, &c_child_names));
  ASSERT_THAT(c_child_names, ElementsAre("1", "d"));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> second_d_ptr;
  ASSERT_TRUE(OpenChild(&second_c_ptr, "d", &second_d_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> second_e_ptr;
  ASSERT_TRUE(OpenChild(&second_d_ptr, "e", &second_e_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> second_one_ptr;
  ASSERT_TRUE(OpenChild(&second_c_ptr, "1", &second_one_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> second_two_ptr;
  ASSERT_TRUE(OpenChild(&second_one_ptr, "2", &second_two_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> second_three_ptr;
  ASSERT_TRUE(OpenChild(&second_two_ptr, "3", &second_three_ptr));

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(deep_child_full_name));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity(deeper_child_full_name));

  // Dropping connections to intermediate nodes doesn't cause those intermediate
  // nodes to go inactive.
  first_a_ptr.Unbind();
  first_b_ptr.Unbind();
  first_c_ptr.Unbind();
  first_d_ptr.Unbind();
  first_one_ptr.Unbind();
  first_two_ptr.Unbind();
  second_a_ptr.Unbind();
  second_b_ptr.Unbind();
  second_c_ptr.Unbind();
  second_d_ptr.Unbind();
  second_one_ptr.Unbind();
  second_two_ptr.Unbind();
  RunLoopUntilIdle();

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c", "d"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c", "1"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c", "1", "2"}));

  // Dropping one but not the other the connection to each end of the "fork"
  // causes no nodes to go inactive since all nodes either are or are ancestors
  // of nodes that are still "under inspection".
  first_three_ptr.Unbind();
  second_e_ptr.Unbind();
  RunLoopUntilIdle();

  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c", "d"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c", "1"}));
  ASSERT_EQ(Activity::ACTIVE, application.DebugGetActivity({"a", "b", "c", "1", "2"}));
}

// Verifies that the Reader API reads an only-active-at-the-first-level
// application hierarchy.
TEST_F(ChildrenManagerTest, ReaderAPIMinimalActiveElements) {
  auto depth = 3;
  auto application =
      Application(&test_loop(), &random_, &elements_node_, CompleteTableDescription(depth));
  fit::closure a_retainer;
  ASSERT_TRUE(Activate(&application, {"a"}, &a_retainer));
  fit::closure b_retainer;
  ASSERT_TRUE(Activate(&application, {"b"}, &b_retainer));
  fit::closure c_retainer;
  ASSERT_TRUE(Activate(&application, {"c"}, &c_retainer));

  inspect_deprecated::ObjectHierarchy hierarchy;
  ASSERT_TRUE(ReadWithReaderAPI(&hierarchy));
  ASSERT_THAT(hierarchy, CompleteMatcher(depth));
}

// Verifies that the Reader API reads a hierarchy with scattershot activity
// throughout.
TEST_F(ChildrenManagerTest, ReaderAPISomeInactiveElements) {
  auto depth = 3;
  auto application =
      Application(&test_loop(), &random_, &elements_node_, CompleteTableDescription(depth));
  fit::closure a_a_a_retainer;
  ASSERT_TRUE(Activate(&application, {"a", "a", "a"}, &a_a_a_retainer));
  fit::closure b_b_retainer;
  ASSERT_TRUE(Activate(&application, {"b", "b"}, &b_b_retainer));
  fit::closure c_retainer;
  ASSERT_TRUE(Activate(&application, {"c"}, &c_retainer));

  inspect_deprecated::ObjectHierarchy hierarchy;
  ASSERT_TRUE(ReadWithReaderAPI(&hierarchy));
  ASSERT_THAT(hierarchy, CompleteMatcher(depth));
}

// Verifies that the Reader API reads a hierarchy in which every element is
// active.
TEST_F(ChildrenManagerTest, ReaderAPINoInactiveElements) {
  auto depth = 3;
  auto leaf_full_names = CompleteTableDescription(depth);
  auto application = Application(&test_loop(), &random_, &elements_node_, leaf_full_names);
  std::vector<fit::closure> retainers;
  for (const auto& leaf_full_name : leaf_full_names) {
    fit::closure retainer;
    ASSERT_TRUE(Activate(&application, leaf_full_name, &retainer));
    retainers.push_back(std::move(retainer));
  }

  inspect_deprecated::ObjectHierarchy hierarchy;
  ASSERT_TRUE(ReadWithReaderAPI(&hierarchy));
  ASSERT_THAT(hierarchy, CompleteMatcher(depth));
}

// Verifies that the Reader API reads a hierarchy in which another inspection
// is already progressing.
TEST_F(ChildrenManagerTest, ReaderAPIConcurrentInspection) {
  auto depth = 3;
  auto application =
      Application(&test_loop(), &random_, &elements_node_, CompleteTableDescription(depth));
  fit::closure a_retainer;
  ASSERT_TRUE(Activate(&application, {"a"}, &a_retainer));
  fit::closure b_retainer;
  ASSERT_TRUE(Activate(&application, {"b"}, &b_retainer));
  fit::closure c_retainer;
  ASSERT_TRUE(Activate(&application, {"c"}, &c_retainer));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> elements_ptr;
  ASSERT_TRUE(OpenElementsNode(&elements_ptr));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_ptr;
  ASSERT_TRUE(OpenChild(&elements_ptr, "a", &a_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_a_ptr;
  ASSERT_TRUE(OpenChild(&a_ptr, "a", &a_a_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> a_a_a_ptr;
  ASSERT_TRUE(OpenChild(&a_a_ptr, "a", &a_a_a_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> b_ptr;
  ASSERT_TRUE(OpenChild(&elements_ptr, "b", &b_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> b_b_ptr;
  ASSERT_TRUE(OpenChild(&b_ptr, "b", &b_b_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> c_ptr;
  ASSERT_TRUE(OpenChild(&elements_ptr, "c", &c_ptr));

  // And for the heck of it: keep a connection to b-a-b without keeping a
  // connection to b-a:
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> b_a_ptr;
  ASSERT_TRUE(OpenChild(&b_ptr, "a", &b_a_ptr));
  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> b_a_b_ptr;
  ASSERT_TRUE(OpenChild(&b_a_ptr, "b", &b_a_b_ptr));
  b_a_ptr.Unbind();
  RunLoopUntilIdle();

  inspect_deprecated::ObjectHierarchy hierarchy;
  ASSERT_TRUE(ReadWithReaderAPI(&hierarchy));
  ASSERT_THAT(hierarchy, CompleteMatcher(depth));
}

TEST_F(ChildrenManagerTest, AbsentChildDoesNotDeadlock) {
  // Since we're testing an edge behavior our "representative application"
  // doesn't work and we use a custom ChildrenManager.
  class ChildrenManager final : public component::ChildrenManager {
   public:
    ChildrenManager(fit::closure on_detachment) : on_detachment_(std::move(on_detachment)) {}

   private:
    void GetNames(fit::function<void(std::set<std::string>)> callback) override {}
    void Attach(std::string name, fit::function<void(fit::closure)> callback) override {
      callback(std::move(on_detachment_));
    }
    fit::closure on_detachment_;
  };

  bool on_detachment_called = false;
  ChildrenManager children_manager([this, &on_detachment_called]() {
    on_detachment_called = true;
    auto int_metric = elements_node_.CreateIntMetric("ignored_int_metric", 0);
  });
  auto children_manager_retainer = elements_node_.SetChildrenManager(&children_manager);

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> elements_ptr;
  ASSERT_TRUE(OpenElementsNode(&elements_ptr));

  fidl::InterfacePtr<fuchsia::inspect::deprecated::Inspect> no_such_child_ptr;
  ASSERT_FALSE(OpenChild(&elements_ptr, "no_such_child", &no_such_child_ptr));
  ASSERT_TRUE(on_detachment_called);
}

}  // namespace
