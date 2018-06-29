// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/task.h>

#include "lib/context/cpp/context_helper.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/logging.h"
#include "lib/svc/cpp/services.h"
#include "peridot/bin/acquirers/mock/mock_gps.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/story_provider_mock.h"
#include "peridot/lib/testing/wait_until_idle.h"
#include "peridot/tests/maxwell_integration/context_engine_test_base.h"
#include "peridot/tests/maxwell_integration/test_suggestion_listener.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/pointer.h"

namespace maxwell {
namespace {

// context agent that publishes an int n
class NWriter {
 public:
  NWriter(fuchsia::modular::ContextEngine* context_engine) {
    fuchsia::modular::ComponentScope scope;
    scope.set_global_scope(fuchsia::modular::GlobalScope());
    context_engine->GetWriter(std::move(scope), pub_.NewRequest());
  }

  void Publish(int n) { pub_->WriteEntityTopic("n", std::to_string(n)); }

 private:
  fuchsia::modular::ContextWriterPtr pub_;
};

fuchsia::modular::Proposal CreateProposal(
    const std::string& id, const std::string& headline,
    fidl::VectorPtr<fuchsia::modular::Action> actions,
    fuchsia::modular::AnnoyanceType annoyance) {
  fuchsia::modular::Proposal p;
  p.id = id;
  p.on_selected = std::move(actions);
  fuchsia::modular::SuggestionDisplay d;

  d.headline = headline;
  d.color = 0x00aa00aa;  // argb purple
  d.annoyance = annoyance;

  p.display = std::move(d);
  return p;
}

class Proposinator {
 public:
  Proposinator(fuchsia::modular::SuggestionEngine* suggestion_engine,
               fidl::StringPtr url = "Proposinator") {
    suggestion_engine->RegisterProposalPublisher("Proposinator",
                                                 out_.NewRequest());
  }

  virtual ~Proposinator() = default;

  void Propose(const std::string& id,
               fidl::VectorPtr<fuchsia::modular::Action> actions =
                   fidl::VectorPtr<fuchsia::modular::Action>::New(0)) {
    Propose(id, id, fuchsia::modular::AnnoyanceType::NONE, std::move(actions));
  }

  void Propose(const std::string& id, const std::string& headline,
               fuchsia::modular::AnnoyanceType annoyance =
                   fuchsia::modular::AnnoyanceType::NONE,
               fidl::VectorPtr<fuchsia::modular::Action> actions =
                   fidl::VectorPtr<fuchsia::modular::Action>::New(0)) {
    Propose(CreateProposal(id, headline, std::move(actions), annoyance));
  }

  void Propose(fuchsia::modular::Proposal proposal) {
    out_->Propose(std::move(proposal));
  }

  void Remove(const std::string& id) { out_->Remove(id); }

  void KillPublisher() { out_.Unbind(); }

 protected:
  fuchsia::modular::ProposalPublisherPtr out_;
};

class AskProposinator : public Proposinator,
                        public fuchsia::modular::QueryHandler {
 public:
  AskProposinator(fuchsia::modular::SuggestionEngine* suggestion_engine,
                  async::Loop* loop, fidl::StringPtr url = "AskProposinator")
      : Proposinator(suggestion_engine, url), loop_(loop), ask_binding_(this) {
    fidl::InterfaceHandle<fuchsia::modular::QueryHandler> query_handle;
    ask_binding_.Bind(query_handle.NewRequest());
    suggestion_engine->RegisterQueryHandler(url, std::move(query_handle));
  }

  void OnQuery(fuchsia::modular::UserInput query,
               OnQueryCallback callback) override {
    query_ = fidl::MakeOptional(query);
    query_callback_ = callback;
    query_proposals_.resize(0);

    if (waiting_for_query_) {
      waiting_for_query_ = false;
      async::PostTask(loop_->async(), [this] { loop_->Quit(); });
    }
  }

  void WaitForQuery() {
    waiting_for_query_ = true;
    loop_->Run();
    loop_->ResetQuit();
  }

  void Commit() {
    fuchsia::modular::QueryResponse response;
    response.proposals = std::move(query_proposals_);
    query_callback_(std::move(response));
  }

  fidl::StringPtr query() const { return query_ ? query_->text : nullptr; }

  void ProposeForAsk(const std::string& id) {
    auto actions = fidl::VectorPtr<fuchsia::modular::Action>::New(0);
    ProposeForAsk(id, id, fuchsia::modular::AnnoyanceType::NONE,
                  std::move(actions));
  }

  void ProposeForAsk(const std::string& id, const std::string& headline,
                     fuchsia::modular::AnnoyanceType annoyance =
                         fuchsia::modular::AnnoyanceType::NONE,
                     fidl::VectorPtr<fuchsia::modular::Action> actions =
                         fidl::VectorPtr<fuchsia::modular::Action>::New(0)) {
    query_proposals_.push_back(
        CreateProposal(id, headline, std::move(actions), annoyance));
  }

 private:
  async::Loop* const loop_;
  fidl::Binding<fuchsia::modular::QueryHandler> ask_binding_;
  fuchsia::modular::UserInputPtr query_;
  fidl::VectorPtr<fuchsia::modular::Proposal> query_proposals_;
  OnQueryCallback query_callback_;
  bool waiting_for_query_ = false;
};

// maintains the number of proposals specified by the context field "n"
class NProposals : public Proposinator,
                   public fuchsia::modular::ContextListener {
 public:
  NProposals(fuchsia::modular::ContextEngine* context_engine,
             fuchsia::modular::SuggestionEngine* suggestion_engine)
      : Proposinator(suggestion_engine, "NProposals"), listener_binding_(this) {
    fuchsia::modular::ComponentScope scope;
    scope.set_global_scope(fuchsia::modular::GlobalScope());
    context_engine->GetReader(std::move(scope), reader_.NewRequest());

    fuchsia::modular::ContextSelector selector;
    selector.type = fuchsia::modular::ContextValueType::ENTITY;
    selector.meta = fuchsia::modular::ContextMetadata::New();
    selector.meta->entity = fuchsia::modular::EntityMetadata::New();
    selector.meta->entity->topic = "n";
    fuchsia::modular::ContextQuery query;
    modular::AddToContextQuery(&query, "n", std::move(selector));
    reader_->Subscribe(std::move(query), listener_binding_.NewBinding());
  }

  void OnContextUpdate(fuchsia::modular::ContextUpdate update) override {
    auto r = modular::TakeContextValue(&update, "n");
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
  fuchsia::modular::ContextReaderPtr reader_;
  fidl::Binding<fuchsia::modular::ContextListener> listener_binding_;

  int n_ = 0;
};

class SuggestionEngineTest : public ContextEngineTestBase,
                             fuchsia::modular::ProposalListener {
 public:
  SuggestionEngineTest() : story_provider_binding_(&story_provider_) {}

  void SetUp() override {
    ContextEngineTestBase::SetUp();

    fuchsia::sys::Services suggestion_services =
        StartServices("suggestion_engine");
    suggestion_engine_ =
        suggestion_services
            .ConnectToService<fuchsia::modular::SuggestionEngine>();
    suggestion_provider_ =
        suggestion_services
            .ConnectToService<fuchsia::modular::SuggestionProvider>();
    suggestion_debug_ =
        suggestion_services
            .ConnectToService<fuchsia::modular::SuggestionDebug>();

    // Initialize the fuchsia::modular::SuggestionEngine.
    fidl::InterfaceHandle<fuchsia::modular::StoryProvider>
        story_provider_handle;
    story_provider_binding_.Bind(story_provider_handle.NewRequest());

    // Hack to get an unbound fuchsia::modular::FocusController for
    // Initialize().
    fidl::InterfaceHandle<fuchsia::modular::FocusProvider>
        focus_provider_handle;
    focus_provider_handle.NewRequest();

    fidl::InterfaceHandle<fuchsia::modular::ContextWriter>
        context_writer_handle;
    fidl::InterfaceHandle<fuchsia::modular::ContextReader>
        context_reader_handle;
    fuchsia::modular::ComponentScope scope;
    scope.set_global_scope(fuchsia::modular::GlobalScope());
    fuchsia::modular::ComponentScope scope_clone;
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
  fuchsia::modular::SuggestionEngine* suggestion_engine() {
    return suggestion_engine_.get();
  }

  fuchsia::modular::SuggestionProvider* suggestion_provider() {
    return suggestion_provider_.get();
  }

  fuchsia::modular::SuggestionDebug* suggestion_debug() {
    return suggestion_debug_.get();
  }

  modular::StoryProviderMock* story_provider() { return &story_provider_; }

  void StartSuggestionAgent(const std::string& url) {
    auto agent_bridge =
        std::make_unique<MaxwellServiceProviderBridge>(root_environment());
    agent_bridge->AddService<fuchsia::modular::ContextReader>(
        [this,
         url](fidl::InterfaceRequest<fuchsia::modular::ContextReader> request) {
          fuchsia::modular::ComponentScope scope;
          fuchsia::modular::AgentScope agent_scope;
          agent_scope.url = url;
          scope.set_agent_scope(std::move(agent_scope));
          context_engine()->GetReader(std::move(scope), std::move(request));
        });
    agent_bridge->AddService<fuchsia::modular::ProposalPublisher>(
        [this, url](fidl::InterfaceRequest<fuchsia::modular::ProposalPublisher>
                        request) {
          suggestion_engine_->RegisterProposalPublisher(url,
                                                        std::move(request));
        });
    StartAgent(url, std::move(agent_bridge));
  }

  void AcceptSuggestion(const std::string& suggestion_id) {
    Interact(suggestion_id, fuchsia::modular::InteractionType::SELECTED);
  }

  void DismissSuggestion(const std::string& suggestion_id) {
    Interact(suggestion_id, fuchsia::modular::InteractionType::DISMISSED);
  }

  void WaitUntilIdle() {
    ContextEngineTestBase::WaitUntilIdle();
    util::WaitUntilIdle(&suggestion_debug_, &loop_);
  }

  void AddProposalListenerBinding(
      fidl::InterfaceRequest<fuchsia::modular::ProposalListener> request) {
    proposal_listener_bindings_.AddBinding(this, std::move(request));
  }

  // The id of the most recently accepted proposal.
  std::string accepted_proposal_id_;

  // The amount of proposals that have been accepted, as indicated by calls to
  // |OnProposalAccepted|.
  int accepted_proposal_count_ = 0;

  // Whether or not a successful create story action has been observed.
  bool created_story_action_;

 private:
  void Interact(const std::string& suggestion_id,
                fuchsia::modular::InteractionType interaction_type) {
    fuchsia::modular::Interaction interaction;
    interaction.type = interaction_type;
    suggestion_provider_->NotifyInteraction(suggestion_id,
                                            std::move(interaction));
  }

  // |fuchsia::modular::ProposalListener|
  void OnProposalAccepted(fidl::StringPtr proposal_id,
                          fidl::StringPtr story_id) override {
    accepted_proposal_id_ = proposal_id;
    if (story_id->length() > 0) {
      created_story_action_ = true;
    }
    accepted_proposal_count_++;
  }

  fuchsia::modular::SuggestionEnginePtr suggestion_engine_;
  fuchsia::modular::SuggestionDebugPtr suggestion_debug_;
  fuchsia::modular::SuggestionProviderPtr suggestion_provider_;

  modular::StoryProviderMock story_provider_;
  fidl::Binding<fuchsia::modular::StoryProvider> story_provider_binding_;
  fidl::BindingSet<fuchsia::modular::ProposalListener>
      proposal_listener_bindings_;
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
    fuchsia::modular::UserInput input;
    input.type = fuchsia::modular::InputType::TEXT;
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
  fidl::Binding<fuchsia::modular::QueryListener> listener_binding_;
  fidl::Binding<fuchsia::modular::AskProposalListener> debug_listener_binding_;
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

  modular::TestDebugInterruptionListener* debugListener() {
    return &debug_listener_;
  }
  modular::TestSuggestionListener* listener() { return &listener_; }

 protected:
  int suggestion_count() const { return listener_.suggestion_count(); }

  void EnsureDebugMatches() {
    auto& subscriberNexts = listener_.GetSuggestions();
    auto lastInterruption = debug_listener_.get_interrupt_proposal();
    ASSERT_GE(subscriberNexts.size(), 1u);
    auto& suggestion = subscriberNexts[0];
    EXPECT_EQ(suggestion->display.headline, lastInterruption.display.headline);
    EXPECT_EQ(suggestion->display.subheadline,
              lastInterruption.display.subheadline);
    EXPECT_EQ(suggestion->display.details, lastInterruption.display.details);
  }

 private:
  modular::TestSuggestionListener listener_;
  modular::TestDebugInterruptionListener debug_listener_;

  fidl::Binding<fuchsia::modular::InterruptionListener> listener_binding_;
  fidl::Binding<fuchsia::modular::InterruptionProposalListener>
      debug_listener_binding_;
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

  const fuchsia::modular::Suggestion* GetOnlySuggestion() const {
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
      EXPECT_EQ(suggestion->display.subheadline, proposal.display.subheadline);
      EXPECT_EQ(suggestion->display.details, proposal.display.details);
    }
  }

 private:
  modular::TestSuggestionListener listener_;
  modular::TestDebugNextListener debug_listener_;

  fidl::Binding<fuchsia::modular::NextListener> listener_binding_;
  fidl::Binding<fuchsia::modular::NextProposalListener> debug_listener_binding_;
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
      << "fuchsia::modular::Proposal 2 ranked over proposal 2; test invalid; "
         "update to test "
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

  fuchsia::modular::CreateStory create_story;
  fuchsia::modular::Intent intent;
  intent.action.handler = "foo://bar";
  create_story.intent = std::move(intent);

  fuchsia::modular::Action action;
  action.set_create_story(std::move(create_story));

  fidl::VectorPtr<fuchsia::modular::Action> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());

  auto suggestion_id = GetOnlySuggestion()->uuid;
  AcceptSuggestion(suggestion_id);
  WaitUntilIdle();
  EXPECT_EQ("foo://bar",
            story_provider()->story_controller().last_added_module());
}

TEST_F(SuggestionInteractionTest, AcceptSuggestionCallback) {
  Proposinator p(suggestion_engine());
  StartListening(10);

  fuchsia::modular::CreateStory create_story;
  fuchsia::modular::Intent intent;
  intent.action.handler = "foo://bar";
  create_story.intent = std::move(intent);

  fuchsia::modular::Action action;
  action.set_create_story(std::move(create_story));
  fidl::VectorPtr<fuchsia::modular::Action> actions;
  actions.push_back(std::move(action));
  auto proposal = CreateProposal("1", "1", std::move(actions),
                                 fuchsia::modular::AnnoyanceType::NONE);
  AddProposalListenerBinding(proposal.listener.NewRequest());
  p.Propose(std::move(proposal));
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());

  auto suggestion_id = GetOnlySuggestion()->uuid;
  AcceptSuggestion(suggestion_id);
  WaitUntilIdle();

  EXPECT_EQ(accepted_proposal_id_, "1");
}

TEST_F(SuggestionInteractionTest, AcceptSuggestionToCreateStory) {
  Proposinator p(suggestion_engine());
  StartListening(10);

  fuchsia::modular::CreateStory create_story;
  fuchsia::modular::Intent intent;
  intent.action.handler = "foo://bar";
  create_story.intent = std::move(intent);

  fuchsia::modular::Action action;
  action.set_create_story(std::move(create_story));

  fidl::VectorPtr<fuchsia::modular::Action> actions;
  actions.push_back(std::move(action));
  auto proposal = CreateProposal("1", "1", std::move(actions),
                                 fuchsia::modular::AnnoyanceType::NONE);
  AddProposalListenerBinding(proposal.listener.NewRequest());
  p.Propose(std::move(proposal));
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());

  auto suggestion_id = GetOnlySuggestion()->uuid;
  AcceptSuggestion(suggestion_id);
  WaitUntilIdle();
  EXPECT_TRUE(created_story_action_);
}

// Tests that accepting a suggestion that creates multiple stories only notifies
// the proposal listener once.
TEST_F(SuggestionInteractionTest, AcceptSuggestionToCreateMultipleStories) {
  Proposinator p(suggestion_engine());
  StartListening(10);

  fuchsia::modular::CreateStory create_story;
  fuchsia::modular::Intent intent;
  intent.action.handler = "foo://bar";
  create_story.intent = std::move(intent);

  fuchsia::modular::Action action;
  action.set_create_story(std::move(create_story));
  fuchsia::modular::Action action2;
  action2.set_create_story(std::move(create_story));

  fidl::VectorPtr<fuchsia::modular::Action> actions;
  actions.push_back(std::move(action));
  actions.push_back(std::move(action2));
  auto proposal = CreateProposal("1", "1", std::move(actions),
                                 fuchsia::modular::AnnoyanceType::NONE);
  AddProposalListenerBinding(proposal.listener.NewRequest());
  p.Propose(std::move(proposal));
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());

  auto suggestion_id = GetOnlySuggestion()->uuid;
  AcceptSuggestion(suggestion_id);
  WaitUntilIdle();
  EXPECT_EQ(accepted_proposal_count_, 1);
}

TEST_F(SuggestionInteractionTest, AcceptSuggestion_CreateStoryIntent) {
  Proposinator p(suggestion_engine());
  StartListening(10);

  fuchsia::modular::Intent intent;
  intent.action.handler = "foo://bar";
  fuchsia::modular::CreateStory create_story;
  create_story.intent = std::move(intent);

  fuchsia::modular::Action action;
  action.set_create_story(std::move(create_story));
  fidl::VectorPtr<fuchsia::modular::Action> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  WaitUntilIdle();

  auto suggestion_id = GetOnlySuggestion()->uuid;
  AcceptSuggestion(suggestion_id);
  WaitUntilIdle();
  EXPECT_EQ("foo://bar",
            story_provider()->story_controller().last_added_module());
}

TEST_F(SuggestionInteractionTest, AcceptSuggestion_AddModule) {
  Proposinator p(suggestion_engine());
  StartListening(10);

  auto module_id = "foo://bar1";

  fuchsia::modular::AddModule add_module;
  add_module.story_id = "foo://bar";
  add_module.module_name = module_id;
  add_module.intent.action.handler = module_id;
  add_module.surface_parent_module_path =
      fidl::VectorPtr<fidl::StringPtr>::New(0);
  add_module.surface_relation = fuchsia::modular::SurfaceRelation();

  fuchsia::modular::Action action;
  action.set_add_module(std::move(add_module));
  fidl::VectorPtr<fuchsia::modular::Action> actions;
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

TEST_F(SuggestionInteractionTest, AcceptSugestion_QueryAction) {
  AskProposinator p(suggestion_engine(), &loop_);
  StartListening(10);

  fuchsia::modular::UserInput user_input;
  user_input.text = "test query";
  fuchsia::modular::QueryAction query_action;
  query_action.input = std::move(user_input);
  fuchsia::modular::Action action;
  action.set_query_action(std::move(query_action));
  fidl::VectorPtr<fuchsia::modular::Action> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));

  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());

  // fuchsia::modular::Suggestion is selected.
  auto suggestion_id = GetOnlySuggestion()->uuid;
  AcceptSuggestion(suggestion_id);

  // Expect query handler to be called with the query action input.
  p.WaitForQuery();
  EXPECT_EQ(p.query(), "test query");

  // Response from fuchsia::modular::QueryHandler.
  fuchsia::modular::CreateStory create_story;
  fuchsia::modular::Intent intent;
  intent.action.handler = "foo://bar";
  create_story.intent = std::move(intent);

  fuchsia::modular::Action action2;
  action2.set_create_story(std::move(create_story));
  fidl::VectorPtr<fuchsia::modular::Action> suggested_actions;
  suggested_actions.push_back(std::move(action2));
  p.ProposeForAsk("2", "suggestion", fuchsia::modular::AnnoyanceType::NONE,
                  std::move(suggested_actions));
  p.Commit();

  WaitUntilIdle();
  EXPECT_EQ("foo://bar",
            story_provider()->story_controller().last_added_module());
}

TEST_F(AskTest, DefaultAsk) {
  AskProposinator p(suggestion_engine(), &loop_);

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
  AskProposinator p(suggestion_engine(), &loop_);

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
  AskProposinator p(suggestion_engine(), &loop_);

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
  AskProposinator p(suggestion_engine(), &loop_);

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

  fuchsia::modular::CreateStory create_story;
  fuchsia::modular::Intent intent;
  intent.action.handler = "foo://bar";
  create_story.intent = std::move(intent);

  fuchsia::modular::Action action;
  action.set_create_story(std::move(create_story));
  fidl::VectorPtr<fuchsia::modular::Action> actions;
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

  // First notify watchers of the fuchsia::modular::StoryProvider that a story
  // already exists.
  fuchsia::modular::StoryInfo story_info;
  story_info.url = "foo://bazzle_dazzle";
  story_info.id = "";
  story_provider()->NotifyStoryChanged(std::move(story_info),
                                       fuchsia::modular::StoryState::STOPPED);

  fuchsia::modular::CreateStory create_story;
  fuchsia::modular::Intent intent;
  intent.action.handler = "foo://bar";
  create_story.intent = std::move(intent);

  fuchsia::modular::Action action;
  action.set_create_story(std::move(create_story));
  fidl::VectorPtr<fuchsia::modular::Action> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
}

TEST_F(InterruptionTest, SingleInterruption) {
  Proposinator p(suggestion_engine());

  p.Propose("1", "2", fuchsia::modular::AnnoyanceType::INTERRUPT);

  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
  EnsureDebugMatches();
}

TEST_F(InterruptionTest, RemovedInterruption) {
  Proposinator p(suggestion_engine());

  p.Propose("1", "2", fuchsia::modular::AnnoyanceType::INTERRUPT);

  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
  EnsureDebugMatches();

  // Removing shouldn't do anything to an interruption
  p.Remove("1");

  WaitUntilIdle();
  EXPECT_EQ(1, suggestion_count());
}

}  // namespace maxwell
