// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_engine_impl.h"

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/fsl/vmo/strings.h>

#include "gtest/gtest.h"
#include "peridot/bin/suggestion_engine/proposal_publisher_impl.h"
#include "peridot/bin/user_runner/puppet_master/puppet_master_impl.h"
#include "peridot/lib/testing/test_story_command_executor.h"
#include "peridot/lib/testing/test_with_session_storage.h"

namespace modular {
namespace {

class TestNextListener : public fuchsia::modular::NextListener {
 public:
  void OnNextResults(
      fidl::VectorPtr<fuchsia::modular::Suggestion> suggestions) override {
    last_suggestions_ = std::move(suggestions);
  }

  void Reset() { last_suggestions_->clear(); }

  void OnProcessingChange(bool processing) override{};

  fidl::VectorPtr<fuchsia::modular::Suggestion>& last_suggestions() {
    return last_suggestions_;
  }

 private:
  fidl::VectorPtr<fuchsia::modular::Suggestion> last_suggestions_;
};

class TestInterruptionListener : public fuchsia::modular::InterruptionListener {
 public:
  void OnInterrupt(fuchsia::modular::Suggestion suggestion) override {
    last_suggestion_ = std::move(suggestion);
  }

  fuchsia::modular::Suggestion& last_suggestion() { return last_suggestion_; }

 private:
  fuchsia::modular::Suggestion last_suggestion_;
};

class TestNavigationListener : public fuchsia::modular::NavigationListener {
 public:
  void OnNavigation(fuchsia::modular::NavigationAction navigation) override {
    last_navigation_action_ = std::move(navigation);
  }

  fuchsia::modular::NavigationAction last_navigation_action() {
    return last_navigation_action_;
  }

 private:
  fuchsia::modular::NavigationAction last_navigation_action_;
};

class TestContextReaderImpl : public fuchsia::modular::ContextReader {
 public:
  TestContextReaderImpl(
      fidl::InterfaceRequest<fuchsia::modular::ContextReader> request)
      : binding_(this, std::move(request)) {}

 private:
  // |fuchsia::modular::ContextReader|
  void Subscribe(fuchsia::modular::ContextQuery query,
                 fidl::InterfaceHandle<fuchsia::modular::ContextListener>
                     listener) override {}

  // |fuchsia::modular::ContextReader|
  void Get(fuchsia::modular::ContextQuery query,
           fuchsia::modular::ContextReader::GetCallback callback) override {}

  fidl::Binding<fuchsia::modular::ContextReader> binding_;
};

class SuggestionEngineTest : public testing::TestWithSessionStorage {
 public:
  SuggestionEngineTest()
      : next_listener_binding_(&next_listener_),
        interruption_listener_binding_(&interruption_listener_),
        navigation_listener_binding_(&navigation_listener_) {}

  void SetUp() override {
    TestWithSessionStorage::SetUp();

    suggestion_engine_impl_ = std::make_unique<SuggestionEngineImpl>();
    suggestion_engine_impl_->Connect(engine_ptr_.NewRequest());
    suggestion_engine_impl_->Connect(provider_ptr_.NewRequest());
    suggestion_engine_impl_->Connect(debug_ptr_.NewRequest());

    // Get an unbound handles for Initialize(). We won't make use of these
    // interfaces during the test.
    fidl::InterfaceHandle<fuchsia::modular::ContextWriter>
        context_writer_handle;
    context_writer_handle.NewRequest();
    fidl::InterfaceHandle<fuchsia::modular::ContextReader>
        context_reader_handle;
    context_reader_impl_ = std::make_unique<TestContextReaderImpl>(
        context_reader_handle.NewRequest());

    session_storage_ = MakeSessionStorage("page");
    puppet_master_impl_ = std::make_unique<PuppetMasterImpl>(
        session_storage_.get(), &test_executor_);
    fidl::InterfaceHandle<fuchsia::modular::PuppetMaster> puppet_master;
    puppet_master_impl_->Connect(puppet_master.NewRequest());

    suggestion_engine_impl_->Initialize(std::move(context_writer_handle),
                                        std::move(context_reader_handle),
                                        std::move(puppet_master));

    proposal_publisher_ = std::make_unique<ProposalPublisherImpl>(
        suggestion_engine_impl_.get(), "Proposinator");
  }

 protected:
  void StartListeningForNext(int max_suggestions) {
    suggestion_engine_impl_->SubscribeToNext(
        next_listener_binding_.NewBinding(), max_suggestions);
    next_listener_.Reset();
  }

  void StartListeningForInterruptions() {
    suggestion_engine_impl_->SubscribeToInterruptions(
        interruption_listener_binding_.NewBinding());
  }

  void StartListeningForNavigation() {
    suggestion_engine_impl_->SubscribeToNavigation(
        navigation_listener_binding_.NewBinding());
  }

  fuchsia::modular::Proposal MakeProposal(const std::string& id,
                                          const std::string& headline) {
    fuchsia::modular::SuggestionDisplay display;
    display.headline = headline;
    fuchsia::modular::Proposal proposal;
    proposal.id = id;
    proposal.display = std::move(display);
    return proposal;
  }

  fuchsia::modular::Proposal MakeInterruptionProposal(
      const std::string id, const std::string& headline,
      fuchsia::modular::AnnoyanceType annoyance =
          fuchsia::modular::AnnoyanceType::INTERRUPT) {
    auto proposal = MakeProposal(id, headline);
    proposal.display.annoyance = annoyance;
    return proposal;
  }

  fuchsia::modular::Proposal MakeRichProposal(const std::string id,
                                              const std::string& headline) {
    auto proposal = MakeProposal(id, headline);
    proposal.wants_rich_suggestion = true;
    return proposal;
  }

  void AddAddModuleAction(fuchsia::modular::Proposal* proposal,
                          const std::string& mod_name,
                          const std::string& mod_url,
                          const std::string& parent_mod = "",
                          fuchsia::modular::SurfaceArrangement arrangement =
                              fuchsia::modular::SurfaceArrangement::NONE) {
    fuchsia::modular::Intent intent;
    intent.handler = mod_url;
    fuchsia::modular::AddModule add_module;
    add_module.module_name = mod_name;
    add_module.intent = std::move(intent);
    if (parent_mod.empty()) {
      add_module.surface_parent_module_path.resize(0);
    } else {
      add_module.surface_parent_module_path.push_back(parent_mod);
    }
    add_module.surface_relation.arrangement = arrangement;

    fuchsia::modular::Action action;
    action.set_add_module(std::move(add_module));
    proposal->on_selected.push_back(std::move(action));
  }

  void AddFocusStoryAction(fuchsia::modular::Proposal* proposal) {
    fuchsia::modular::FocusStory focus_story;
    fuchsia::modular::Action action;
    action.set_focus_story(std::move(focus_story));
    proposal->on_selected.push_back(std::move(action));
  }

  void AddFocusModuleAction(fuchsia::modular::Proposal* proposal,
                            const std::string& mod_name) {
    fuchsia::modular::FocusModule focus_module;
    focus_module.module_path.push_back(mod_name);
    fuchsia::modular::Action action;
    action.set_focus_module(std::move(focus_module));
    proposal->on_selected.push_back(std::move(action));
  }

  void AddUpdateModuleAction(fuchsia::modular::Proposal* proposal,
                             const std::string& mod_name,
                             const std::string& json_param_name,
                             const std::string& json_param_value) {
    fuchsia::modular::IntentParameter parameter;
    parameter.name = json_param_name;
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(json_param_value, &vmo));
    parameter.data.set_json(std::move(vmo).ToTransport());
    fuchsia::modular::UpdateModule update_module;
    update_module.module_name.push_back(mod_name);
    update_module.parameters.push_back(std::move(parameter));

    fuchsia::modular::Action action;
    action.set_update_module(std::move(update_module));
    proposal->on_selected.push_back(std::move(action));
  }

  void AddSetLinkValueAction(fuchsia::modular::Proposal* proposal,
                             const std::string& mod_name,
                             const std::string& link_name,
                             const std::string& link_value) {
    fuchsia::modular::LinkPath link_path;
    link_path.module_path.push_back(mod_name);
    link_path.link_name = link_name;
    fuchsia::modular::SetLinkValueAction set_link_value;
    set_link_value.link_path = std::move(link_path);
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(link_value, &vmo));
    set_link_value.value =
        std::make_unique<fuchsia::mem::Buffer>(std::move(vmo).ToTransport());

    fuchsia::modular::Action action;
    action.set_set_link_value_action(std::move(set_link_value));
    proposal->on_selected.push_back(std::move(action));
  }

  std::unique_ptr<ProposalPublisherImpl> proposal_publisher_;
  std::unique_ptr<SessionStorage> session_storage_;
  std::unique_ptr<PuppetMasterImpl> puppet_master_impl_;
  std::unique_ptr<SuggestionEngineImpl> suggestion_engine_impl_;
  std::unique_ptr<TestContextReaderImpl> context_reader_impl_;
  fuchsia::modular::SuggestionEnginePtr engine_ptr_;
  fuchsia::modular::SuggestionProviderPtr provider_ptr_;
  fuchsia::modular::SuggestionDebugPtr debug_ptr_;
  testing::TestStoryCommandExecutor test_executor_;

  TestNextListener next_listener_;
  fidl::Binding<fuchsia::modular::NextListener> next_listener_binding_;

  TestInterruptionListener interruption_listener_;
  fidl::Binding<fuchsia::modular::InterruptionListener>
      interruption_listener_binding_;

  TestNavigationListener navigation_listener_;
  fidl::Binding<fuchsia::modular::NavigationListener>
      navigation_listener_binding_;
};

TEST_F(SuggestionEngineTest, AddNextProposal) {
  StartListeningForNext(10);

  // Add proposal.
  auto proposal = MakeProposal("1", "test_proposal");
  proposal_publisher_->Propose(std::move(proposal));

  RunLoopUntilIdle();

  // We should see proposal in listener.
  auto& results = next_listener_.last_suggestions();
  ASSERT_EQ(1u, results->size());
  EXPECT_EQ("test_proposal", results->at(0).display.headline);
}

TEST_F(SuggestionEngineTest, OnlyGetsMaxProposals) {
  StartListeningForNext(2);

  // Add three proposals.
  proposal_publisher_->Propose(MakeProposal("1", "foo"));
  proposal_publisher_->Propose(MakeProposal("2", "bar"));
  proposal_publisher_->Propose(MakeProposal("3", "baz"));

  RunLoopUntilIdle();

  // We should see 2 proposals in listener.
  auto& results = next_listener_.last_suggestions();
  ASSERT_EQ(2u, results->size());
  EXPECT_EQ("foo", results->at(0).display.headline);
  EXPECT_EQ("bar", results->at(1).display.headline);
}

TEST_F(SuggestionEngineTest, AddNextProposalInterruption) {
  StartListeningForNext(10);
  StartListeningForInterruptions();

  // Add interruptive proposal.
  proposal_publisher_->Propose(MakeInterruptionProposal("1", "foo"));

  RunLoopUntilIdle();

  // Ensure notification.
  auto& last_interruption = interruption_listener_.last_suggestion();
  EXPECT_EQ("foo", last_interruption.display.headline);

  // Suggestion shouldn't be in NEXT yet since it's interrupting.
  auto& results = next_listener_.last_suggestions();
  EXPECT_TRUE(results->empty());
}

TEST_F(SuggestionEngineTest, AddNextProposalRichNotAllowed) {
  StartListeningForNext(10);

  // Register publisher that can't submit rich proposals (see the url) and add
  // proposal.
  auto publisher = std::make_unique<ProposalPublisherImpl>(
      suggestion_engine_impl_.get(), "foo");
  publisher->Propose(MakeRichProposal("1", "foo"));

  RunLoopUntilIdle();

  // Suggestion shouldn't be rich: it has no preloaded story_id.
  auto& results = next_listener_.last_suggestions();
  ASSERT_EQ(1u, results->size());
  EXPECT_EQ("foo", results->at(0).display.headline);
  EXPECT_TRUE(results->at(0).preloaded_story_id->empty());
}

TEST_F(SuggestionEngineTest, AddNextProposalRich) {
  StartListeningForNext(10);

  // Add proposal.
  auto proposal = MakeRichProposal("1", "foo_rich");
  AddAddModuleAction(&proposal, "mod_name", "mod_url", "parent_mod",
                     fuchsia::modular::SurfaceArrangement::ONTOP);
  proposal_publisher_->Propose(std::move(proposal));

  RunLoopUntil([&] { return next_listener_.last_suggestions()->size() == 1; });

  // Suggestion should be rich: it has a preloaded story_id.
  auto& results = next_listener_.last_suggestions();
  EXPECT_EQ("foo_rich", results->at(0).display.headline);
  EXPECT_FALSE(results->at(0).preloaded_story_id->empty());
  auto story_name = results->at(0).preloaded_story_id;

  // The executor should have been called with a command to add a mod and
  // created a story.
  EXPECT_EQ(1, test_executor_.execute_count());
  EXPECT_FALSE(test_executor_.last_story_id()->empty());
  auto& commands = test_executor_.last_commands();
  ASSERT_EQ(1u, commands.size());
  ASSERT_TRUE(commands.at(0).is_add_mod());

  auto& command = commands.at(0).add_mod();
  ASSERT_EQ(1u, command.mod_name->size());
  EXPECT_EQ("mod_name", command.mod_name->at(0));
  EXPECT_EQ("mod_url", command.intent.handler);
  EXPECT_EQ(fuchsia::modular::SurfaceArrangement::ONTOP,
            command.surface_relation.arrangement);
  ASSERT_EQ(1u, command.surface_parent_mod_name->size());
  EXPECT_EQ("parent_mod", command.surface_parent_mod_name->at(0));

  // Ensure the story was created as kind-of-proto story.
  bool done{};
  session_storage_->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr story_data) {
        ASSERT_NE(nullptr, story_data);
        EXPECT_TRUE(story_data->story_options.kind_of_proto_story);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

TEST_F(SuggestionEngineTest, AddNextProposalRichReusesStory) {
  StartListeningForNext(10);
  auto story_name = "rich_story";

  // Add proposal.
  {
    auto proposal = MakeRichProposal("1", "foo_rich");
    proposal.story_name = story_name;
    AddAddModuleAction(&proposal, "mod_name", "mod_url", "parent_mod",
                       fuchsia::modular::SurfaceArrangement::ONTOP);
    proposal_publisher_->Propose(std::move(proposal));
  }

  RunLoopUntil([&] { return next_listener_.last_suggestions()->size() == 1; });

  // Up to here we expect the same as in the previous test (AddNextProposalRich)
  // Submitting a new proposal with the same story_name should result on its
  // story being directly updated and no notifications of new suggestions.
  next_listener_.Reset();
  test_executor_.Reset();
  {
    auto proposal = MakeRichProposal("1", "foo_rich");
    proposal.story_name = story_name;
    AddAddModuleAction(&proposal, "mod_name", "mod_url", "parent_mod",
                       fuchsia::modular::SurfaceArrangement::COPRESENT);
    proposal_publisher_->Propose(std::move(proposal));
  }

  RunLoopUntil([&] { return test_executor_.execute_count() == 1; });
  EXPECT_TRUE(next_listener_.last_suggestions()->empty());

  // The executor should have been called with a command to add a mod and
  // created a story.
  EXPECT_EQ(1, test_executor_.execute_count());
  EXPECT_FALSE(test_executor_.last_story_id()->empty());
  auto& commands = test_executor_.last_commands();
  ASSERT_EQ(1u, commands.size());
  ASSERT_TRUE(commands.at(0).is_add_mod());

  auto& command = commands.at(0).add_mod();
  ASSERT_EQ(1u, command.mod_name->size());
  EXPECT_EQ("mod_name", command.mod_name->at(0));
  EXPECT_EQ("mod_url", command.intent.handler);
  EXPECT_EQ(fuchsia::modular::SurfaceArrangement::COPRESENT,
            command.surface_relation.arrangement);
  ASSERT_EQ(1u, command.surface_parent_mod_name->size());
  EXPECT_EQ("parent_mod", command.surface_parent_mod_name->at(0));

  // Ensure the story is there.
  bool done{};
  session_storage_->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr story_data) {
        ASSERT_NE(nullptr, story_data);
        EXPECT_TRUE(story_data->story_options.kind_of_proto_story);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

TEST_F(SuggestionEngineTest, AddNextProposalRichRespectsStoryName) {
  StartListeningForNext(10);

  // Add proposal.
  auto proposal = MakeRichProposal("1", "foo_rich");
  proposal.story_name = "foo_story";
  AddAddModuleAction(&proposal, "mod_name", "mod_url", "parent_mod",
                     fuchsia::modular::SurfaceArrangement::ONTOP);
  proposal_publisher_->Propose(std::move(proposal));

  RunLoopUntil([&] { return next_listener_.last_suggestions()->size() == 1; });

  // Suggestion should be rich: it has a preloaded story_id.
  auto& results = next_listener_.last_suggestions();
  EXPECT_EQ("foo_story", results->at(0).preloaded_story_id);

  // The executor should have been called with a command to add a mod and
  // created a story.
  EXPECT_EQ(1, test_executor_.execute_count());
  EXPECT_EQ("foo_story", test_executor_.last_story_id());

  // Ensure the story was created as kind-of-proto story.
  bool done{};
  session_storage_->GetStoryData("foo_story")
      ->Then([&](fuchsia::modular::internal::StoryDataPtr story_data) {
        ASSERT_NE(nullptr, story_data);
        EXPECT_TRUE(story_data->story_options.kind_of_proto_story);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

TEST_F(SuggestionEngineTest, RemoveNextProposal) {
  StartListeningForNext(10);

  // Add proposal
  proposal_publisher_->Propose(MakeProposal("1", "foo"));

  // Remove proposal
  proposal_publisher_->Remove("1");

  RunLoopUntilIdle();

  auto& results = next_listener_.last_suggestions();
  EXPECT_TRUE(results->empty());
}

TEST_F(SuggestionEngineTest, RemoveNextProposalRich) {
  StartListeningForNext(10);

  // Add proposal.
  auto proposal = MakeRichProposal("1", "foo_rich");
  proposal.story_name = "foo_story";
  proposal_publisher_->Propose(std::move(proposal));

  // TODO(miguelfrde): add an operation queue in the suggestion engine and
  // remove this wait.
  RunLoopUntil([&] { return next_listener_.last_suggestions()->size() == 1; });

  // Remove proposal.
  proposal_publisher_->Remove("1");

  RunLoopUntil([&] { return next_listener_.last_suggestions()->empty(); });

  // The story that at some point was created when adding the rich suggestion
  // (not tested since other tests already cover it) should have been deleted.
  bool done{};
  session_storage_->GetStoryData("foo_story")
      ->Then([&](fuchsia::modular::internal::StoryDataPtr story_data) {
        EXPECT_EQ(nullptr, story_data);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

TEST_F(SuggestionEngineTest, NotifyInteractionSelected) {
  StartListeningForNext(10);

  // Add proposal. One action of each action we support that translates to
  // StoryCommand is added. This set of actions doesn't really make sense in an
  // actual use case.
  auto proposal = MakeProposal("1", "foo");
  AddAddModuleAction(&proposal, "mod_name", "mod_url");
  AddFocusStoryAction(&proposal);
  AddFocusModuleAction(&proposal, "mod_name");
  AddUpdateModuleAction(&proposal, "mod_name", "json_param", "1");
  AddSetLinkValueAction(&proposal, "mod_name", "foo_link_name", "foo_value");
  proposal_publisher_->Propose(std::move(proposal));

  RunLoopUntilIdle();

  // Get id of the resulting suggestion.
  auto& results = next_listener_.last_suggestions();
  ASSERT_EQ(1u, results->size());
  auto suggestion_id = results->at(0).uuid;

  fuchsia::modular::Interaction interaction;
  interaction.type = fuchsia::modular::InteractionType::SELECTED;
  suggestion_engine_impl_->NotifyInteraction(suggestion_id,
                                             std::move(interaction));

  RunLoopUntil([&] { return test_executor_.execute_count() == 1; });

  // The executor should have been called with the right commands.
  auto story_id = test_executor_.last_story_id();

  auto& commands = test_executor_.last_commands();
  ASSERT_EQ(5u, commands.size());
  EXPECT_TRUE(commands.at(0).is_add_mod());
  EXPECT_TRUE(commands.at(1).is_set_focus_state());
  EXPECT_TRUE(commands.at(2).is_focus_mod());
  EXPECT_TRUE(commands.at(3).is_update_mod());
  EXPECT_TRUE(commands.at(4).is_set_link_value());

  auto& add_mod = commands.at(0).add_mod();
  ASSERT_EQ(1u, add_mod.mod_name->size());
  EXPECT_EQ("mod_name", add_mod.mod_name->at(0));
  EXPECT_EQ("mod_url", add_mod.intent.handler);

  auto& set_focus_state = commands.at(1).set_focus_state();
  EXPECT_TRUE(set_focus_state.focused);

  auto& focus_mod = commands.at(2).focus_mod();
  ASSERT_EQ(1u, focus_mod.mod_name->size());
  EXPECT_EQ("mod_name", focus_mod.mod_name->at(0));

  auto& update_mod = commands.at(3).update_mod();
  ASSERT_EQ(1u, update_mod.mod_name->size());
  EXPECT_EQ("mod_name", update_mod.mod_name->at(0));
  EXPECT_EQ("json_param", update_mod.parameters->at(0).name);
  std::string json_value;
  FXL_CHECK(fsl::StringFromVmo(update_mod.parameters->at(0).data.json(),
                               &json_value));
  EXPECT_EQ("1", json_value);

  auto& set_link_value = commands.at(4).set_link_value();
  ASSERT_EQ(1u, set_link_value.path.module_path->size());
  EXPECT_EQ("mod_name", set_link_value.path.module_path->at(0));
  EXPECT_EQ("foo_link_name", set_link_value.path.link_name);
  std::string link_value;
  FXL_CHECK(fsl::StringFromVmo(*set_link_value.value, &link_value));
  EXPECT_EQ("foo_value", link_value);

  // Ensure a regular story was created when we executed the proposal.
  bool done{};
  session_storage_->GetStoryData(story_id)->Then(
      [&](fuchsia::modular::internal::StoryDataPtr story_data) {
        EXPECT_NE(nullptr, story_data);
        EXPECT_FALSE(story_data->story_options.kind_of_proto_story);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // We should have been notified with no suggestions after selecting this
  // suggestion.
  auto& listener_results = next_listener_.last_suggestions();
  EXPECT_TRUE(listener_results->empty());
}

TEST_F(SuggestionEngineTest, NotifyInteractionSelectedWithStoryName) {
  StartListeningForNext(10);

  // Add proposal.
  auto proposal = MakeProposal("1", "foo");
  proposal.story_name = "foo_story";
  AddFocusModuleAction(&proposal, "mod_name");
  proposal_publisher_->Propose(std::move(proposal));

  RunLoopUntilIdle();

  // Get id of the resulting suggestion.
  auto& results = next_listener_.last_suggestions();
  ASSERT_EQ(1u, results->size());
  auto suggestion_id = results->at(0).uuid;

  // Select suggestion.
  fuchsia::modular::Interaction interaction;
  interaction.type = fuchsia::modular::InteractionType::SELECTED;
  suggestion_engine_impl_->NotifyInteraction(suggestion_id,
                                             std::move(interaction));

  RunLoopUntil([&] { return test_executor_.execute_count() == 1; });

  // The executor should have been called with the command associated to the
  // action added above.
  EXPECT_EQ("foo_story", test_executor_.last_story_id());

  auto& commands = test_executor_.last_commands();
  ASSERT_EQ(1u, commands.size());
  EXPECT_TRUE(commands.at(0).is_focus_mod());
  auto& focus_mod = commands.at(0).focus_mod();
  ASSERT_EQ(1u, focus_mod.mod_name->size());
  EXPECT_EQ("mod_name", focus_mod.mod_name->at(0));

  // Ensure a regular story was created when we executed the proposal.
  bool done{};
  session_storage_->GetStoryData("foo_story")
      ->Then([&](fuchsia::modular::internal::StoryDataPtr story_data) {
        EXPECT_NE(nullptr, story_data);
        EXPECT_FALSE(story_data->story_options.kind_of_proto_story);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // We should have been notified with no suggestions after selecting this
  // suggestion.
  auto& listener_results = next_listener_.last_suggestions();
  EXPECT_TRUE(listener_results->empty());
}

TEST_F(SuggestionEngineTest, NotifyInteractionDismissed) {
  StartListeningForNext(10);

  // Add proposal.
  auto proposal = MakeProposal("1", "foo");
  AddFocusModuleAction(&proposal, "mod_name");
  proposal_publisher_->Propose(std::move(proposal));

  RunLoopUntilIdle();

  // Get id of the resulting suggestion.
  auto& results = next_listener_.last_suggestions();
  ASSERT_EQ(1u, results->size());
  auto suggestion_id = results->at(0).uuid;

  fuchsia::modular::Interaction interaction;
  interaction.type = fuchsia::modular::InteractionType::DISMISSED;
  suggestion_engine_impl_->NotifyInteraction(suggestion_id,
                                             std::move(interaction));

  RunLoopUntilIdle();

  // The executor shouldn't have been called.
  EXPECT_EQ(0, test_executor_.execute_count());

  // We should have been notified with no suggestions after dismissing this
  // suggestion.
  auto& listener_results = next_listener_.last_suggestions();
  EXPECT_TRUE(listener_results->empty());
}

TEST_F(SuggestionEngineTest, NotifyInteractionDismissedWithStoryName) {
  StartListeningForNext(10);

  // Add proposal.
  auto proposal = MakeProposal("1", "foo");
  proposal.story_name = "foo_story";
  AddFocusModuleAction(&proposal, "mod_name");
  proposal_publisher_->Propose(std::move(proposal));

  RunLoopUntilIdle();

  // Get id of the resulting suggestion.
  auto& results = next_listener_.last_suggestions();
  ASSERT_EQ(1u, results->size());
  auto suggestion_id = results->at(0).uuid;

  fuchsia::modular::Interaction interaction;
  interaction.type = fuchsia::modular::InteractionType::DISMISSED;
  suggestion_engine_impl_->NotifyInteraction(suggestion_id,
                                             std::move(interaction));

  RunLoopUntilIdle();

  // The executor shouldn't have been called.
  EXPECT_EQ(0, test_executor_.execute_count());

  // We should have been notified with no suggestions after dismissing this
  // suggestion.
  auto& listener_results = next_listener_.last_suggestions();
  EXPECT_TRUE(listener_results->empty());

  // Ensure no story was created when we executed the proposal.
  bool done{};
  session_storage_->GetStoryData("foo_story")
      ->Then([&](fuchsia::modular::internal::StoryDataPtr story_data) {
        EXPECT_EQ(nullptr, story_data);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

TEST_F(SuggestionEngineTest, NotifyInteractionSelectedRich) {
  StartListeningForNext(10);

  // Add proposal.
  auto proposal = MakeRichProposal("1", "foo_rich");
  AddFocusModuleAction(&proposal, "mod_name");
  proposal_publisher_->Propose(std::move(proposal));

  RunLoopUntil([&] { return next_listener_.last_suggestions()->size() == 1; });

  // Get id of the resulting suggestion.
  auto& results = next_listener_.last_suggestions();
  auto suggestion_id = results->at(0).uuid;
  EXPECT_FALSE(results->at(0).preloaded_story_id->empty());
  auto story_name = results->at(0).preloaded_story_id;

  test_executor_.Reset();

  fuchsia::modular::Interaction interaction;
  interaction.type = fuchsia::modular::InteractionType::SELECTED;
  suggestion_engine_impl_->NotifyInteraction(suggestion_id,
                                             std::move(interaction));

  RunLoopUntilIdle();

  // The executor should have been called for a second time with a command to
  // promote the story that the adding of the the proposal created.
  EXPECT_EQ(test_executor_.execute_count(), 0);

  // Ensure the story that was created when we adedd the rich proposal still
  // exists.
  bool done{};
  session_storage_->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr story_data) {
        EXPECT_NE(nullptr, story_data);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

TEST_F(SuggestionEngineTest, NotifyInteractionDismissedRich) {
  StartListeningForNext(10);

  // Add proposal.
  auto proposal = MakeRichProposal("1", "foo_rich");
  AddFocusModuleAction(&proposal, "mod_name");
  proposal_publisher_->Propose(std::move(proposal));

  RunLoopUntil([&] { return next_listener_.last_suggestions()->size() == 1; });

  // Get id and story of the resulting suggestion.
  auto& results = next_listener_.last_suggestions();
  EXPECT_EQ(1, test_executor_.execute_count());
  auto suggestion_id = results->at(0).uuid;

  EXPECT_FALSE(results->at(0).preloaded_story_id->empty());
  auto story_name = results->at(0).preloaded_story_id;

  test_executor_.Reset();

  fuchsia::modular::Interaction interaction;
  interaction.type = fuchsia::modular::InteractionType::DISMISSED;
  suggestion_engine_impl_->NotifyInteraction(suggestion_id,
                                             std::move(interaction));

  RunLoopUntil([&] { return next_listener_.last_suggestions()->empty(); });

  // The executor shouldn't have been called again.
  EXPECT_EQ(0, test_executor_.execute_count());

  // Ensure the story that was created when we added the rich proposal is gone.
  bool done{};
  session_storage_->GetStoryData(story_name)
      ->Then([&](fuchsia::modular::internal::StoryDataPtr story_data) {
        EXPECT_EQ(nullptr, story_data);
        done = true;
      });
  RunLoopUntil([&] { return done; });
}

TEST_F(SuggestionEngineTest, NotifyInteractionSnoozedInterruption) {
  StartListeningForInterruptions();
  StartListeningForNext(10);

  // Add interruptive proposal.
  proposal_publisher_->Propose(MakeInterruptionProposal("1", "foo"));

  RunLoopUntilIdle();

  // Get id of the resulting suggestion.
  auto& suggestion = interruption_listener_.last_suggestion();
  EXPECT_FALSE(suggestion.uuid->empty());
  auto suggestion_id = suggestion.uuid;

  EXPECT_TRUE(next_listener_.last_suggestions()->empty());

  fuchsia::modular::Interaction interaction;
  interaction.type = fuchsia::modular::InteractionType::SNOOZED;
  suggestion_engine_impl_->NotifyInteraction(suggestion_id,
                                             std::move(interaction));

  RunLoopUntil([&] { return next_listener_.last_suggestions()->size() == 1; });

  // The suggestion should still be there after being notified.
  auto& listener_results = next_listener_.last_suggestions();
  EXPECT_EQ(suggestion_id, listener_results->at(0).uuid);
}

TEST_F(SuggestionEngineTest, NotifyInteractionExpiredInterruption) {
  StartListeningForInterruptions();
  StartListeningForNext(10);

  // Add interruptive proposal.
  proposal_publisher_->Propose(MakeInterruptionProposal("1", "foo"));

  RunLoopUntilIdle();

  // Get id of the resulting suggestion.
  auto& suggestion = interruption_listener_.last_suggestion();
  EXPECT_FALSE(suggestion.uuid->empty());
  auto suggestion_id = suggestion.uuid;

  EXPECT_TRUE(next_listener_.last_suggestions()->empty());

  fuchsia::modular::Interaction interaction;
  interaction.type = fuchsia::modular::InteractionType::EXPIRED;
  suggestion_engine_impl_->NotifyInteraction(suggestion_id,
                                             std::move(interaction));

  RunLoopUntilIdle();

  // The suggestion should still be there after being notified.
  auto& listener_results = next_listener_.last_suggestions();
  EXPECT_EQ(1u, listener_results->size());
  EXPECT_EQ(suggestion_id, listener_results->at(0).uuid);
}

TEST_F(SuggestionEngineTest, NotifyInteractionSelectedInterruption) {
  StartListeningForInterruptions();
  StartListeningForNext(10);

  // Add interruptive proposal.
  auto proposal = MakeInterruptionProposal("1", "foo");
  AddFocusModuleAction(&proposal, "mod_name");
  proposal_publisher_->Propose(std::move(proposal));

  RunLoopUntilIdle();

  auto& suggestion = interruption_listener_.last_suggestion();
  EXPECT_FALSE(suggestion.uuid->empty());
  auto suggestion_id = suggestion.uuid;

  fuchsia::modular::Interaction interaction;
  interaction.type = fuchsia::modular::InteractionType::SELECTED;
  suggestion_engine_impl_->NotifyInteraction(suggestion_id,
                                             std::move(interaction));

  RunLoopUntil([&] { return test_executor_.execute_count() == 1; });

  // The executor should have been called with a command to add a mod and
  // created a story.
  auto story_id = test_executor_.last_story_id();
  auto& commands = test_executor_.last_commands();
  ASSERT_EQ(1u, commands.size());
  auto& focus_mod = commands.at(0).focus_mod();
  ASSERT_EQ(1u, focus_mod.mod_name->size());
  EXPECT_EQ("mod_name", focus_mod.mod_name->at(0));

  // Ensure a regular story was created when we executed the proposal.
  bool done{};
  session_storage_->GetStoryData(story_id)->Then(
      [&](fuchsia::modular::internal::StoryDataPtr story_data) {
        EXPECT_NE(nullptr, story_data);
        EXPECT_FALSE(story_data->story_options.kind_of_proto_story);
        done = true;
      });
  RunLoopUntil([&] { return done; });

  // The suggestion shouldn't be there anymore.
  EXPECT_TRUE(next_listener_.last_suggestions()->empty());
}

TEST_F(SuggestionEngineTest, NotifyInteractionDismissedInterruption) {
  StartListeningForInterruptions();
  StartListeningForNext(10);

  // Add interruptive proposal.
  auto proposal = MakeInterruptionProposal("1", "foo");
  AddFocusModuleAction(&proposal, "mod_name");
  proposal_publisher_->Propose(std::move(proposal));

  RunLoopUntilIdle();

  auto& suggestion = interruption_listener_.last_suggestion();
  EXPECT_FALSE(suggestion.uuid->empty());
  auto suggestion_id = suggestion.uuid;

  fuchsia::modular::Interaction interaction;
  interaction.type = fuchsia::modular::InteractionType::DISMISSED;
  suggestion_engine_impl_->NotifyInteraction(suggestion_id,
                                             std::move(interaction));

  RunLoopUntilIdle();

  // The executor shouldn't have been called.
  EXPECT_EQ(0, test_executor_.execute_count());

  // The suggestion shouldn't be there anymore.
  EXPECT_TRUE(next_listener_.last_suggestions()->empty());
}

TEST_F(SuggestionEngineTest, ProposeNavigation) {
  StartListeningForNavigation();

  proposal_publisher_->ProposeNavigation(
      fuchsia::modular::NavigationAction::HOME);
  RunLoopUntilIdle();

  EXPECT_EQ(fuchsia::modular::NavigationAction::HOME,
            navigation_listener_.last_navigation_action());
}

}  // namespace
}  // namespace modular
