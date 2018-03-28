// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/modular.h>

#include "gtest/gtest.h"
#include "lib/context/cpp/context_helper.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/svc/cpp/services.h"
#include "peridot/bin/acquirers/mock/mock_gps.h"
#include "peridot/bin/agents/ideas.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/story_provider_mock.h"
#include "peridot/lib/util/wait_until_idle.h"
#include "peridot/tests/maxwell_integration/context_engine_test_base.h"
#include "peridot/tests/maxwell_integration/test_suggestion_listener.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/pointer.h"

constexpr char maxwell::agents::IdeasAgent::kIdeaId[];

using modular::StoryProviderMock;

namespace maxwell {
namespace {

// context agent that publishes an int n
class NWriter {
 public:
  NWriter(modular::ContextEngine* context_engine) {
    modular::ComponentScope scope;
    scope.set_global_scope(modular::GlobalScope());
    context_engine->GetWriter(std::move(scope), pub_.NewRequest());
  }

  void Publish(int n) { pub_->WriteEntityTopic("n", std::to_string(n)); }

 private:
  modular::ContextWriterPtr pub_;
};

modular::Proposal CreateProposal(const std::string& id,
                        const std::string& headline,
                        fidl::VectorPtr<modular::Action> actions,
                        modular::AnnoyanceType annoyance) {
  modular::Proposal p;
  p.id = id;
  p.on_selected = std::move(actions);
  modular::SuggestionDisplay d;

  d.headline = headline;
  d.color = 0x00aa00aa;  // argb purple
  d.annoyance = annoyance;

  p.display = std::move(d);
  return p;
}

class Proposinator {
 public:
  Proposinator(modular::SuggestionEngine* suggestion_engine,
               fidl::StringPtr url = "Proposinator") {
    suggestion_engine->RegisterProposalPublisher("Proposinator",
                                                 out_.NewRequest());
  }

  virtual ~Proposinator() = default;

  void Propose(
      const std::string& id,
      fidl::VectorPtr<modular::Action> actions = fidl::VectorPtr<modular::Action>::New(0)) {
    Propose(id, id, modular::AnnoyanceType::NONE, std::move(actions));
  }

  void Propose(
      const std::string& id,
      const std::string& headline,
      modular::AnnoyanceType annoyance = modular::AnnoyanceType::NONE,
      fidl::VectorPtr<modular::Action> actions = fidl::VectorPtr<modular::Action>::New(0)) {
    out_->Propose(CreateProposal(id, headline, std::move(actions), annoyance));
  }

  void Remove(const std::string& id) { out_->Remove(id); }

  void KillPublisher() { out_.Unbind(); }

 protected:
  modular::ProposalPublisherPtr out_;
};

class AskProposinator : public Proposinator, public modular::QueryHandler {
 public:
  AskProposinator(modular::SuggestionEngine* suggestion_engine,
                  fidl::StringPtr url = "AskProposinator")
      : Proposinator(suggestion_engine, url), ask_binding_(this) {
    fidl::InterfaceHandle<QueryHandler> query_handle;
    ask_binding_.Bind(query_handle.NewRequest());
    suggestion_engine->RegisterQueryHandler(url, std::move(query_handle));
  }

  void OnQuery(modular::UserInput query,
               OnQueryCallback callback) override {
    query_ = fidl::MakeOptional(query);
    query_callback_ = callback;
    query_proposals_.resize(0);

    if (waiting_for_query_) {
      waiting_for_query_ = false;
      fsl::MessageLoop::GetCurrent()->PostQuitTask();
    }
  }

  void WaitForQuery() {
    waiting_for_query_ = true;
    fsl::MessageLoop::GetCurrent()->Run();
  }

  void Commit() {
    modular::QueryResponse response;
    response.proposals = std::move(query_proposals_);
    query_callback_(std::move(response));
  }

  fidl::StringPtr query() const { return query_ ? query_->text : nullptr; }

  void ProposeForAsk(const std::string& id) {
    auto actions = fidl::VectorPtr<modular::Action>::New(0);
    ProposeForAsk(id, id, modular::AnnoyanceType::NONE, std::move(actions));
  }

  void ProposeForAsk(
      const std::string& id,
      const std::string& headline,
      modular::AnnoyanceType annoyance = modular::AnnoyanceType::NONE,
      fidl::VectorPtr<modular::Action> actions = fidl::VectorPtr<modular::Action>::New(0)) {
    query_proposals_.push_back(
        CreateProposal(id, headline, std::move(actions), annoyance));
  }

 private:
  fidl::Binding<QueryHandler> ask_binding_;
  modular::UserInputPtr query_;
  fidl::VectorPtr<modular::Proposal> query_proposals_;
  OnQueryCallback query_callback_;
  bool waiting_for_query_ = false;
};

// maintains the number of proposals specified by the context field "n"
class NProposals : public Proposinator, public modular::ContextListener {
 public:
  NProposals(modular::ContextEngine* context_engine, modular::SuggestionEngine* suggestion_engine)
      : Proposinator(suggestion_engine, "NProposals"), listener_binding_(this) {
    modular::ComponentScope scope;
    scope.set_global_scope(modular::GlobalScope());
    context_engine->GetReader(std::move(scope), reader_.NewRequest());

    modular::ContextSelector selector;
    selector.type = modular::ContextValueType::ENTITY;
    selector.meta = modular::ContextMetadata::New();
    selector.meta->entity = modular::EntityMetadata::New();
    selector.meta->entity->topic = "n";
    modular::ContextQuery query;
    AddToContextQuery(&query, "n", std::move(selector));
    reader_->Subscribe(std::move(query), listener_binding_.NewBinding());
  }

  void OnContextUpdate(modular::ContextUpdate update) override {
    auto r = TakeContextValue(&update, "n");
    ASSERT_TRUE(r.first) << "Expect an update key for every query key.";
    if (r.second->empty())
      return;
    int n = std::stoi(r.second->at(0).content);

    for (int i = n_; i < n; i++)
      Propose(std::to_string(i));
    for (int i = n; i < n_; i++)
      Remove(std::to_string(i));

    n_ = n;
  }

 private:
  modular::ContextReaderPtr reader_;
  fidl::Binding<ContextListener> listener_binding_;

  int n_ = 0;
};

class SuggestionEngineTest : public ContextEngineTestBase {
 public:
  SuggestionEngineTest() : story_provider_binding_(&story_provider_) {}

  void SetUp() override {
    ContextEngineTestBase::SetUp();

    component::Services suggestion_services =
        StartServices("suggestion_engine");
    suggestion_engine_ =
        suggestion_services.ConnectToService<modular::SuggestionEngine>();
    suggestion_provider_ =
        suggestion_services.ConnectToService<modular::SuggestionProvider>();
    suggestion_debug_ = suggestion_services.ConnectToService<modular::SuggestionDebug>();

    // Initialize the SuggestionEngine.
    fidl::InterfaceHandle<modular::StoryProvider> story_provider_handle;
    story_provider_binding_.Bind(story_provider_handle.NewRequest());

    // Hack to get an unbound FocusController for Initialize().
    fidl::InterfaceHandle<modular::FocusProvider> focus_provider_handle;
    focus_provider_handle.NewRequest();

    fidl::InterfaceHandle<modular::ContextWriter> context_writer_handle;
    fidl::InterfaceHandle<modular::ContextReader> context_reader_handle;
    modular::ComponentScope scope;
    scope.set_global_scope(modular::GlobalScope());
    modular::ComponentScope scope_clone;
    fidl::Clone(scope, &scope_clone);
    context_engine()->GetWriter(std::move(scope_clone),
                                context_writer_handle.NewRequest());
    context_engine()->GetReader(std::move(scope),
                                context_reader_handle.NewRequest());

    suggestion_engine()->Initialize(
        std::move(story_provider_handle), std::move(focus_provider_handle),
        std::move(context_writer_handle), std::move(context_reader_handle));
  }

 protected:
  modular::SuggestionEngine* suggestion_engine() { return suggestion_engine_.get(); }

  modular::SuggestionProvider* suggestion_provider() {
    return suggestion_provider_.get();
  }

  modular::SuggestionDebug* suggestion_debug() { return suggestion_debug_.get(); }

  StoryProviderMock* story_provider() { return &story_provider_; }

  void StartSuggestionAgent(const std::string& url) {
    auto agent_bridge =
        std::make_unique<MaxwellServiceProviderBridge>(root_environment());
    agent_bridge->AddService<modular::ContextReader>(
        [this, url](fidl::InterfaceRequest<modular::ContextReader> request) {
          modular::ComponentScope scope;
          modular::AgentScope agent_scope;
          agent_scope.url = url;
          scope.set_agent_scope(std::move(agent_scope));
          context_engine()->GetReader(std::move(scope), std::move(request));
        });
    agent_bridge->AddService<modular::ProposalPublisher>(
        [this, url](fidl::InterfaceRequest<modular::ProposalPublisher> request) {
          suggestion_engine_->RegisterProposalPublisher(url,
                                                        std::move(request));
        });
    StartAgent(url, std::move(agent_bridge));
  }

  void AcceptSuggestion(const std::string& suggestion_id) {
    Interact(suggestion_id, modular::InteractionType::SELECTED);
  }

  void DismissSuggestion(const std::string& suggestion_id) {
    Interact(suggestion_id, modular::InteractionType::DISMISSED);
  }

  void WaitUntilIdle() {
    ContextEngineTestBase::WaitUntilIdle();
    util::WaitUntilIdle(suggestion_debug_.get());
  }

 private:
  void Interact(const std::string& suggestion_id,
                modular::InteractionType interaction_type) {
    modular::Interaction interaction;
    interaction.type = interaction_type;
    suggestion_provider_->NotifyInteraction(suggestion_id,
                                            std::move(interaction));
  }

  modular::SuggestionEnginePtr suggestion_engine_;
  modular::SuggestionDebugPtr suggestion_debug_;
  modular::SuggestionProviderPtr suggestion_provider_;

  StoryProviderMock story_provider_;
  fidl::Binding<modular::StoryProvider> story_provider_binding_;
};

class AskTest : public virtual SuggestionEngineTest {
 public:
  AskTest()
      : listener_binding_(&listener_),
        debug_listener_binding_(&debug_listener_) {}

  void SetUp() override {
    SuggestionEngineTest::SetUp();

    suggestion_debug()->WatchAskProposals(debug_listener_binding_.NewBinding());
  }

  void CloseAndResetListener() {
    if (listener_binding_.is_bound()) {
      listener_binding_.Unbind();
      listener_.ClearSuggestions();
    }
  }

  void Query(const std::string& query, int count = 10) {
    CloseAndResetListener();
    modular::UserInput input;
    input.type = modular::InputType::TEXT;
    input.text = query;
    suggestion_provider()->Query(listener_binding_.NewBinding(),
                                 std::move(input), count);
  }

  int suggestion_count() const { return listener_.suggestion_count(); }

  modular::TestSuggestionListener* listener() { return &listener_; }

 protected:
  void EnsureDebugMatches() {
    auto& subscriberAsks = listener_.GetSuggestions();
    auto& debugAsks = debug_listener_.GetProposals();
    EXPECT_GE(debugAsks.size(), subscriberAsks.size());
    for (size_t i = 0; i < subscriberAsks.size(); i++) {
      auto& suggestion = subscriberAsks[i];
      auto& proposal = debugAsks[i];
      EXPECT_EQ(suggestion->display.headline, proposal.display.headline);
      EXPECT_EQ(suggestion->display.subheadline, proposal.display.subheadline);
      EXPECT_EQ(suggestion->display.details, proposal.display.details);
    }
  }

 private:
  modular::TestSuggestionListener listener_;
  modular::TestDebugAskListener debug_listener_;
  fidl::Binding<modular::QueryListener> listener_binding_;
  fidl::Binding<modular::AskProposalListener> debug_listener_binding_;
};

class InterruptionTest : public virtual SuggestionEngineTest {
 public:
  InterruptionTest()
      : listener_binding_(&listener_),
        debug_listener_binding_(&debug_listener_) {}

  void SetUp() override {
    SuggestionEngineTest::SetUp();

    suggestion_provider()->SubscribeToInterruptions(
        listener_binding_.NewBinding());
    suggestion_debug()->WatchInterruptionProposals(
        debug_listener_binding_.NewBinding());

    // Make sure we're subscribed before we start the test.
    WaitUntilIdle();
  }

  modular::TestDebugInterruptionListener* debugListener() { return &debug_listener_; }
  modular::TestSuggestionListener* listener() { return &listener_; }

 protected:
  int suggestion_count() const { return listener_.suggestion_count(); }

  void EnsureDebugMatches() {
    auto& subscriberNexts = listener_.GetSuggestions();
    auto lastInterruption = debug_listener_.get_interrupt_proposal();
    ASSERT_GE(subscriberNexts.size(), 1u);
    auto& suggestion = subscriberNexts[0];
    EXPECT_EQ(suggestion->display.headline,
              lastInterruption.display.headline);
    EXPECT_EQ(suggestion->display.subheadline,
              lastInterruption.display.subheadline);
    EXPECT_EQ(suggestion->display.details, lastInterruption.display.details);
  }

 private:
  modular::TestSuggestionListener listener_;
  modular::TestDebugInterruptionListener debug_listener_;

  fidl::Binding<modular::InterruptionListener> listener_binding_;
  fidl::Binding<modular::InterruptionProposalListener> debug_listener_binding_;
};

class NextTest : public virtual SuggestionEngineTest {
 public:
  NextTest()
      : listener_binding_(&listener_),
        debug_listener_binding_(&debug_listener_) {}

  void SetUp() override {
    SuggestionEngineTest::SetUp();

    suggestion_debug()->WatchNextProposals(
        debug_listener_binding_.NewBinding());
  }

  modular::TestDebugNextListener* debugListener() { return &debug_listener_; }
  modular::TestSuggestionListener* listener() { return &listener_; }

 protected:
  void StartListening(int count) {
    suggestion_provider()->SubscribeToNext(listener_binding_.NewBinding(),
                                           count);
  }

  void CloseAndResetListener() {
    listener_binding_.Unbind();
    listener_.ClearSuggestions();
  }

  void SetResultCount(int count) {
    CloseAndResetListener();
    suggestion_provider()->SubscribeToNext(listener_binding_.NewBinding(),
                                           count);
  }

  int suggestion_count() const { return listener_.suggestion_count(); }

  const modular::Suggestion* GetOnlySuggestion() const {
    return listener_.GetOnlySuggestion();
  }

  void EnsureDebugMatches() {
    auto& subscriberNexts = listener_.GetSuggestions();
    auto& debugNexts = debug_listener_.GetProposals();
    EXPECT_GE(debugNexts.size(), subscriberNexts.size());
    for (size_t i = 0; i < subscriberNexts.size(); i++) {
      auto& suggestion = subscriberNexts[i];
      auto& proposal = debugNexts[i];
      EXPECT_EQ(suggestion->display.headline, proposal.display.headline);
      EXPECT_EQ(suggestion->display.subheadline,
                proposal.display.subheadline);
      EXPECT_EQ(suggestion->display.details, proposal.display.details);
    }
  }

 private:
  modular::TestSuggestionListener listener_;
  modular::TestDebugNextListener debug_listener_;

  fidl::Binding<modular::NextListener> listener_binding_;
  fidl::Binding<modular::NextProposalListener> debug_listener_binding_;
};

class ResultCountTest : public NextTest {
 public:
  void SetUp() override {
    NextTest::SetUp();

    pub_.reset(new NWriter(context_engine()));
    sub_.reset(new NProposals(context_engine(), suggestion_engine()));
  }

 protected:
  // Publishes signals for n new suggestions to context.
  void PublishNewSignal(int n = 1) { pub_->Publish(n_ += n); }

 private:
  std::unique_ptr<NWriter> pub_;
  std::unique_ptr<NProposals> sub_;
  int n_ = 0;
};

}  // namespace

// Macro rather than method to capture the expectation in the assertion message.
#define CHECK_RESULT_COUNT(expected) ASYNC_EQ(expected, suggestion_count())

TEST_F(ResultCountTest, InitiallyEmpty) {
  StartListening(10);
  WaitUntilIdle();
  EXPECT_EQ(0, suggestion_count());
}

TEST_F(ResultCountTest, OneByOne) {
  StartListening(10);
  PublishNewSignal();
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());

  PublishNewSignal();
  WaitUntilIdle();
  EXPECT_EQ(2, suggestion_count());
}

TEST_F(ResultCountTest, AddOverLimit) {
  StartListening(0);
  PublishNewSignal(3);
  WaitUntilIdle();
  EXPECT_EQ(0, suggestion_count());

  SetResultCount(1);
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());

  SetResultCount(3);
  WaitUntilIdle();
  EXPECT_EQ(3, suggestion_count());

  SetResultCount(5);
  WaitUntilIdle();
  EXPECT_EQ(3, suggestion_count());

  PublishNewSignal(4);
  WaitUntilIdle();
  EXPECT_EQ(5, suggestion_count());
}

TEST_F(ResultCountTest, Clear) {
  StartListening(10);
  PublishNewSignal(3);
  WaitUntilIdle();
  EXPECT_EQ(3, suggestion_count());

  SetResultCount(0);
  WaitUntilIdle();
  EXPECT_EQ(0, suggestion_count());

  SetResultCount(10);
  WaitUntilIdle();
  EXPECT_EQ(3, suggestion_count());
}

TEST_F(ResultCountTest, MultiRemove) {
  StartListening(10);
  PublishNewSignal(3);
  WaitUntilIdle();
  EXPECT_EQ(3, suggestion_count());

  SetResultCount(1);
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());

  SetResultCount(10);
  WaitUntilIdle();
  EXPECT_EQ(3, suggestion_count());
}

/* TODO(jwnichols): Re-enable these two tests when this functionality returns
   to the suggestion engine.

// The ideas agent only publishes a single proposal ID, so each new idea is a
// duplicate suggestion. Test that given two such ideas (via two GPS locations),
// only the latest is kept.
TEST_F(NextTest, Dedup) {
  acquirers::MockGps gps(context_engine());
  StartContextAgent("agents/carmen_sandiego");
  StartSuggestionAgent("agents/ideas");

  StartListening(10);
  gps.Publish(90, 0);
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
  const Suggestion* suggestion = GetOnlySuggestion();
  const std::string uuid1 = suggestion->uuid;
  const std::string headline1 = suggestion->display->headline;
  gps.Publish(-90, 0);
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
  suggestion = GetOnlySuggestion();
  EXPECT_NE(headline1, suggestion->display->headline);
  WaitUntilIdle();
  EnsureDebugMatches();
}

// Tests two different agents proposing with the same ID (expect distinct
// proposals). One agent is the agents/ideas process while the other is the test
// itself (maxwell_test).
TEST_F(NextTest, NamespacingPerAgent) {
  acquirers::MockGps gps(context_engine());
  StartContextAgent("agents/carmen_sandiego");
  StartSuggestionAgent("agents/ideas");
  Proposinator conflictinator(suggestion_engine());

  StartListening(10);
  gps.Publish(90, 0);
  // Spoof the idea agent's proposal ID (well, not really spoofing since they
  // are namespaced by component).
  conflictinator.Propose(agents::IdeasAgent::kIdeaId);
  WaitUntilIdle();
  EXPECT_EQ(2, suggestion_count());
  EnsureDebugMatches();
}
*/

// Tests the removal of earlier suggestions, ensuring that suggestion engine can
// handle the case where an agent requests the removal of suggestions in a non-
// LIFO ordering. This exercises some internal shuffling, especially when
// rankings are likewise non-LIFO (where last = lowest-priority).
//
// TODO(rosswang): Currently this test also tests removing higher-ranked
// suggestions. After we have real ranking, add a test for that.
TEST_F(NextTest, Fifo) {
  Proposinator fifo(suggestion_engine());

  StartListening(10);
  fifo.Propose("1");
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
  auto uuid_1 = GetOnlySuggestion()->uuid;

  fifo.Propose("2");
  WaitUntilIdle();
  EXPECT_EQ(2, suggestion_count());
  fifo.Remove("1");
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
  auto suggestion = GetOnlySuggestion();
  EXPECT_NE(uuid_1, suggestion->uuid);
  EXPECT_EQ("2", suggestion->display.headline);
}

// Tests the removal of earlier suggestions while capped.
// TODO(rosswang): see above TODO
TEST_F(NextTest, CappedFifo) {
  Proposinator fifo(suggestion_engine());

  StartListening(1);
  fifo.Propose("1");
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
  auto uuid1 = GetOnlySuggestion()->uuid;

  fifo.Propose("2");
  WaitUntilIdle();
  EXPECT_EQ(uuid1, GetOnlySuggestion()->uuid)
      << "Proposal 2 ranked over proposal 2; test invalid; update to test "
         "FIFO-ranked proposals.";

  fifo.Remove("1");
  WaitUntilIdle();
  ASSERT_EQ(1, suggestion_count());
  EXPECT_NE(uuid1, GetOnlySuggestion()->uuid);

  EXPECT_EQ("2", GetOnlySuggestion()->display.headline);
}

TEST_F(NextTest, RemoveBeforeSubscribe) {
  Proposinator zombinator(suggestion_engine());

  zombinator.Propose("brains");
  zombinator.Remove("brains");
  WaitUntilIdle();

  StartListening(10);
  WaitUntilIdle();
  EXPECT_EQ(0, suggestion_count());
}

TEST_F(NextTest, SubscribeBeyondController) {
  Proposinator p(suggestion_engine());

  StartListening(10);
  WaitUntilIdle();
  p.Propose("1");
  p.Propose("2");
  WaitUntilIdle();
  EXPECT_EQ(2, suggestion_count());
}

class SuggestionInteractionTest : public NextTest {};

TEST_F(SuggestionInteractionTest, AcceptSuggestion) {
  Proposinator p(suggestion_engine());
  StartListening(10);

  modular::CreateStory create_story;
  create_story.module_id = "foo://bar";
  modular::Action action;
  action.set_create_story(std::move(create_story));
  fidl::VectorPtr<modular::Action> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());

  auto suggestion_id = GetOnlySuggestion()->uuid;
  AcceptSuggestion(suggestion_id);
  WaitUntilIdle();
  EXPECT_EQ("foo://bar", story_provider()->last_created_story());
}

TEST_F(SuggestionInteractionTest, AcceptSuggestion_WithInitialData) {
  Proposinator p(suggestion_engine());
  StartListening(10);

  modular::CreateStory create_story;
  create_story.module_id = "foo://bar";
  modular::Action action;

  rapidjson::Document doc;
  rapidjson::Pointer("/foo/bar").Set(doc, "some_data");
  create_story.initial_data = modular::JsonValueToString(doc);

  action.set_create_story(std::move(create_story));
  fidl::VectorPtr<modular::Action> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());

  auto suggestion_id = GetOnlySuggestion()->uuid;
  AcceptSuggestion(suggestion_id);
  WaitUntilIdle();
  EXPECT_EQ("foo://bar", story_provider()->last_created_story());
}

TEST_F(SuggestionInteractionTest, AcceptSuggestion_AddModule) {
  Proposinator p(suggestion_engine());
  StartListening(10);

  auto module_id = "foo://bar1";

  modular::AddModuleToStory add_module_to_story;
  add_module_to_story.story_id = "foo://bar";
  add_module_to_story.module_name = module_id;
  add_module_to_story.module_url = module_id;
  add_module_to_story.module_path = fidl::VectorPtr<fidl::StringPtr>::New(0);
  add_module_to_story.link_name = "";
  add_module_to_story.surface_relation = modular::SurfaceRelation();

  modular::Action action;
  action.set_add_module_to_story(std::move(add_module_to_story));
  fidl::VectorPtr<modular::Action> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());

  auto suggestion_id = GetOnlySuggestion()->uuid;
  AcceptSuggestion(suggestion_id);

  WaitUntilIdle();
  EXPECT_EQ(module_id,
            story_provider()->story_controller().last_added_module());
}

TEST_F(AskTest, DefaultAsk) {
  AskProposinator p(suggestion_engine());

  Query("test query");
  p.WaitForQuery();
  p.ProposeForAsk("1");
  p.Commit();

  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());

  Query("test query 2");
  p.WaitForQuery();
  p.ProposeForAsk("2");
  p.Commit();

  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
  EnsureDebugMatches();
}

/* These tests assume that a string match between the proposal headline
   and the query text factors into suggestion ranking. That ranking
   feature is currently turned off and thus these tests fail, but they
   will pass with it turned on.

#define CHECK_TOP_HEADLINE(h) \
  ASYNC_CHECK(listener()->GetTopSuggestion()->display->headline == h)

TEST_F(AskTest, AskDifferentQueries) {
  AskProposinator p(suggestion_engine());

  Query("The Hottest Band on the Internet");
  p.WaitForQuery();
  p.ProposeForAsk("Mozart's Ghost");
  p.ProposeForAsk("The Hottest Band on the Internet");
  p.Commit();
  WaitUntilIdle();

  CHECK_TOP_HEADLINE("The Hottest Band on the Internet");

  Query("Mozart's Ghost");
  p.WaitForQuery();
  p.ProposeForAsk("Mozart's Ghost");
  p.ProposeForAsk("The Hottest Band on the Internet");
  p.Commit();
  WaitUntilIdle();

  CHECK_TOP_HEADLINE("Mozart's Ghost");
  EnsureDebugMatches();
}

TEST_F(AskTest, ChangeHeadlineRank) {
  AskProposinator p(suggestion_engine());

  Query("test query");
  p.WaitForQuery();
  p.ProposeForAsk("E-mail", "E-mail");
  p.ProposeForAsk("E-vite", "E-vite");
  p.ProposeForAsk("E-card", "E-card");
  p.ProposeForAsk("Music", "Music");
  p.Commit();

  WaitUntilIdle();
  EXPECT_EQ(4, suggestion_count());

  Query("Ca");
  p.WaitForQuery();
  p.ProposeForAsk("E-mail", "E-mail");
  p.ProposeForAsk("E-vite", "E-vite");
  p.ProposeForAsk("E-card", "E-card");
  p.ProposeForAsk("Music", "Music");
  p.Commit();
  WaitUntilIdle();

  // E-card has a 'ca' in the 3rd position, so should be ranked highest.
  CHECK_TOP_HEADLINE("E-card");

  Query("Ca");
  p.WaitForQuery();
  p.ProposeForAsk("E-mail", "E-mail");
  p.ProposeForAsk("E-mail", "Cam");
  p.ProposeForAsk("E-vite", "E-vite");
  p.ProposeForAsk("E-card", "E-card");
  p.ProposeForAsk("Music", "Music");
  p.Commit();
  WaitUntilIdle();

  CHECK_TOP_HEADLINE("Cam");
  EnsureDebugMatches();
  EXPECT_EQ(4, suggestion_count());
}
*/

/* These tests make an assumption that timestamp factors into ranking, which
   it no longer does.  It could be re-enabled if that factor is included again.

#define HEADLINE_EQ(expected, index) \
  EXPECT_EQ(expected, (*listener())[index]->display->headline)

TEST_F(AskTest, AskRanking) {
  AskProposinator p(suggestion_engine());

  Query("");
  p.WaitForQuery();
  p.ProposeForAsk("View E-mail");
  p.ProposeForAsk("Compose E-mail");
  p.ProposeForAsk("Reply to E-mail");
  p.ProposeForAsk("Send E-vites");
  p.ProposeForAsk("E-mail Guests");
  p.Commit();

  WaitUntilIdle();
  EXPECT_EQ(5, suggestion_count());
  // Results should be ranked by timestamp at this point.
  HEADLINE_EQ("View E-mail", 0);
  HEADLINE_EQ("Compose E-mail", 1);
  HEADLINE_EQ("Reply to E-mail", 2);
  HEADLINE_EQ("Send E-vites", 3);
  HEADLINE_EQ("E-mail Guests", 4);
  EnsureDebugMatches();

  Query("e-mail");
  p.WaitForQuery();
  p.ProposeForAsk("View E-mail");
  p.ProposeForAsk("Compose E-mail");
  p.ProposeForAsk("Reply to E-mail");
  p.ProposeForAsk("Send E-vites");
  p.ProposeForAsk("E-mail Guests");
  p.Commit();

  WaitUntilIdle();
  EXPECT_EQ(5, suggestion_count());
  HEADLINE_EQ("View E-mail", 0);
  HEADLINE_EQ("E-mail Guests", 1);
  HEADLINE_EQ("Compose E-mail", 2);
  HEADLINE_EQ("Reply to E-mail", 3);
  EnsureDebugMatches();

  Query("e-mail", 2);
  p.WaitForQuery();
  p.ProposeForAsk("View E-mail");
  p.ProposeForAsk("Compose E-mail");
  p.ProposeForAsk("Reply to E-mail");
  p.ProposeForAsk("Send E-vites");
  p.ProposeForAsk("E-mail Guests");
  p.Commit();

  WaitUntilIdle();
  EXPECT_EQ(2, suggestion_count());
  HEADLINE_EQ("View E-mail", 0);
  HEADLINE_EQ("E-mail Guests", 1);

  Query("Compose", 1);
  p.WaitForQuery();
  p.ProposeForAsk("View E-mail");
  p.ProposeForAsk("Compose E-mail");
  p.ProposeForAsk("Reply to E-mail");
  p.ProposeForAsk("Send E-vites");
  p.ProposeForAsk("E-mail Guests");
  p.Commit();

  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
  HEADLINE_EQ("Compose E-mail", 0);
  EnsureDebugMatches();
}
*/
class SuggestionFilteringTest : public NextTest {};

TEST_F(SuggestionFilteringTest, Baseline) {
  // Show that without any existing Stories, we see Proposals to launch
  // any story.
  Proposinator p(suggestion_engine());
  StartListening(10);

  modular::CreateStory create_story;
  create_story.module_id = "foo://bar";
  modular::Action action;
  action.set_create_story(std::move(create_story));
  fidl::VectorPtr<modular::Action> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
}

TEST_F(SuggestionFilteringTest, Baseline_FilterDoesntMatch) {
  // Show that with an existing Story for a URL, we see Proposals to launch
  // other URLs.
  Proposinator p(suggestion_engine());
  StartListening(10);

  // First notify watchers of the StoryProvider that a story
  // already exists.
  modular::StoryInfo story_info;
  story_info.url = "foo://bazzle_dazzle";
  story_info.id = "";
  story_provider()->NotifyStoryChanged(std::move(story_info),
                                       modular::StoryState::INITIAL);

  modular::CreateStory create_story;
  create_story.module_id = "foo://bar";
  modular::Action action;
  action.set_create_story(std::move(create_story));
  fidl::VectorPtr<modular::Action> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
}

/* TODO(jwnichols): Re-enable these two tests when this functionality returns
   to the suggestion engine.

TEST_F(SuggestionFilteringTest, FilterOnPropose) {
  // If a Story already exists, then Proposals that want to create
  // that same story are filtered when they are proposed.
  Proposinator p(suggestion_engine());
  StartListening(10);

  // First notify watchers of the StoryProvider that this story
  // already exists.
  auto story_info = modular::StoryInfo::New();
  story_info->url = "foo://bar";
  story_info->id = "";
  story_info->extra.mark_non_null();
  story_provider()->NotifyStoryChanged(std::move(story_info),
                                       modular::StoryState::INITIAL);

  auto create_story = CreateStory::New();
  create_story->module_id = "foo://bar";
  auto action = Action::New();
  action->set_create_story(std::move(create_story));
  fidl::VectorPtr<ActionPtr> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  p.Propose("2");
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
}

TEST_F(SuggestionFilteringTest, ChangeFiltered) {
  Proposinator p(suggestion_engine());
  StartListening(10);

  auto story_info = modular::StoryInfo::New();
  story_info->url = "foo://bar";
  story_info->id = "";
  story_info->extra.mark_non_null();
  story_provider()->NotifyStoryChanged(std::move(story_info),
                                       modular::StoryState::INITIAL);

  for (int i = 0; i < 2; i++) {
    auto create_story = CreateStory::New();
    create_story->module_id = "foo://bar";
    auto action = Action::New();
    action->set_create_story(std::move(create_story));
    fidl::VectorPtr<ActionPtr> actions;
    actions.push_back(std::move(action));

    p.Propose("1", std::move(actions));
  }

  // historically crashed by now
  p.Propose("2");

  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
}
*/

TEST_F(InterruptionTest, SingleInterruption) {
  Proposinator p(suggestion_engine());

  p.Propose("1", "2", modular::AnnoyanceType::INTERRUPT);

  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
  EnsureDebugMatches();
}

TEST_F(InterruptionTest, RemovedInterruption) {
  Proposinator p(suggestion_engine());

  p.Propose("1", "2", modular::AnnoyanceType::INTERRUPT);

  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
  EnsureDebugMatches();

  // Removing shouldn't do anything to an interruption
  p.Remove("1");

  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
}

}  // namespace maxwell
