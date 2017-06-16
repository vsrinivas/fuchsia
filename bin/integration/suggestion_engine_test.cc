// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_engine.fidl.h"
#include "apps/maxwell/src/acquirers/mock/mock_gps.h"
#include "apps/maxwell/src/agents/ideas.h"
#include "apps/maxwell/src/integration/context_engine_test_base.h"
#include "apps/maxwell/src/integration/test_suggestion_listener.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/lib/testing/story_provider_mock.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/pointer.h"

constexpr char maxwell::agents::IdeasAgent::kIdeaId[];

using modular::StoryProviderMock;

namespace maxwell {
namespace {

// context agent that publishes an int n
class NPublisher {
 public:
  NPublisher(ContextEngine* context_engine) {
    auto scope = ComponentScope::New();
    scope->set_global_scope(GlobalScope::New());
    context_engine->GetPublisher(std::move(scope), pub_.NewRequest());
  }

  void Publish(int n) { pub_->Publish("n", std::to_string(n)); }

 private:
  ContextPublisherPtr pub_;
};

ProposalPtr CreateProposal(const std::string& id,
                           const std::string& headline,
                           fidl::Array<ActionPtr> actions) {
  auto p = Proposal::New();
  p->id = id;
  p->on_selected = std::move(actions);
  auto d = SuggestionDisplay::New();

  d->headline = headline;
  d->subheadline = "";
  d->details = "";
  d->color = 0x00aa00aa;  // argb purple
  d->icon_urls = fidl::Array<fidl::String>::New(1);
  d->icon_urls[0] = "";
  d->image_url = "";
  d->image_type = SuggestionImageType::PERSON;

  p->display = std::move(d);
  return p;
}

class Proposinator {
 public:
  Proposinator(SuggestionEngine* suggestion_engine,
               const fidl::String& url = "Proposinator") {
    suggestion_engine->RegisterPublisher("Proposinator", out_.NewRequest());
  }

  virtual ~Proposinator() = default;

  void Propose(
      const std::string& id,
      fidl::Array<ActionPtr> actions = fidl::Array<ActionPtr>::New(0)) {
    Propose(id, id, std::move(actions));
  }

  void Propose(
      const std::string& id,
      const std::string& headline,
      fidl::Array<ActionPtr> actions = fidl::Array<ActionPtr>::New(0)) {
    out_->Propose(CreateProposal(id, headline, std::move(actions)));
  }

  void Remove(const std::string& id) { out_->Remove(id); }

  void KillPublisher() { out_.reset(); }

 protected:
  ProposalPublisherPtr out_;
};

// maintains the number of proposals specified by the context field "n"
class NProposals : public Proposinator, public ContextListener {
 public:
  NProposals(ContextEngine* context_engine, SuggestionEngine* suggestion_engine)
      : Proposinator(suggestion_engine, "NProposals"), listener_binding_(this) {
    auto scope = ComponentScope::New();
    scope->set_global_scope(GlobalScope::New());
    context_engine->GetProvider(std::move(scope), provider_.NewRequest());

    auto query = ContextQuery::New();
    query->topics.push_back("n");
    provider_->Subscribe(std::move(query), listener_binding_.NewBinding());
  }

  void OnUpdate(ContextUpdatePtr update) override {
    int n = std::stoi(update->values["n"]);

    for (int i = n_; i < n; i++)
      Propose(std::to_string(i));
    for (int i = n; i < n_; i++)
      Remove(std::to_string(i));

    n_ = n;
  }

 private:
  ContextProviderPtr provider_;
  fidl::Binding<ContextListener> listener_binding_;

  int n_ = 0;
};

class SuggestionEngineTest : public ContextEngineTestBase {
 public:
  SuggestionEngineTest() : story_provider_binding_(&story_provider_) {
    app::ServiceProviderPtr suggestion_services =
        StartServiceProvider("suggestion_engine");
    suggestion_engine_ =
        app::ConnectToService<SuggestionEngine>(suggestion_services.get());
    suggestion_provider_ =
        app::ConnectToService<SuggestionProvider>(suggestion_services.get());

    // Initialize the SuggestionEngine.
    fidl::InterfaceHandle<modular::StoryProvider> story_provider_handle;
    story_provider_binding_.Bind(&story_provider_handle);

    // Hack to get an unbound FocusController for Initialize().
    fidl::InterfaceHandle<modular::FocusProvider> focus_provider_handle;
    focus_provider_handle.NewRequest();

    fidl::InterfaceHandle<maxwell::ContextPublisher> context_publisher_handle;
    auto scope = ComponentScope::New();
    scope->set_global_scope(GlobalScope::New());
    context_engine()->GetPublisher(std::move(scope), context_publisher_handle.NewRequest());

    suggestion_engine()->Initialize(std::move(story_provider_handle),
                                    std::move(focus_provider_handle),
                                    std::move(context_publisher_handle));
  }

 protected:
  SuggestionEngine* suggestion_engine() { return suggestion_engine_.get(); }

  SuggestionProvider* suggestion_provider() {
    return suggestion_provider_.get();
  }

  StoryProviderMock* story_provider() { return &story_provider_; }

  void StartSuggestionAgent(const std::string& url) {
    auto agent_host =
        std::make_unique<ApplicationEnvironmentHostImpl>(root_environment);
    agent_host->AddService<ContextProvider>(
        [this, url](fidl::InterfaceRequest<ContextProvider> request) {
          auto scope = ComponentScope::New();
          auto agent_scope = AgentScope::New();
          agent_scope->url = url;
          scope->set_agent_scope(std::move(agent_scope));
          context_engine()->GetProvider(std::move(scope), std::move(request));
        });
    agent_host->AddService<ProposalPublisher>(
        [this, url](fidl::InterfaceRequest<ProposalPublisher> request) {
          suggestion_engine_->RegisterPublisher(url, std::move(request));
        });
    StartAgent(url, std::move(agent_host));
  }

  void AcceptSuggestion(const std::string& suggestion_id) {
    Interact(suggestion_id, InteractionType::SELECTED);
  }

  void DismissSuggestion(const std::string& suggestion_id) {
    Interact(suggestion_id, InteractionType::DISMISSED);
  }

 private:
  void Interact(const std::string& suggestion_id,
                InteractionType interaction_type) {
    auto interaction = Interaction::New();
    interaction->type = interaction_type;
    suggestion_provider_->NotifyInteraction(suggestion_id,
                                            std::move(interaction));
  }

  SuggestionEnginePtr suggestion_engine_;
  SuggestionProviderPtr suggestion_provider_;

  StoryProviderMock story_provider_;
  fidl::Binding<modular::StoryProvider> story_provider_binding_;
};

class NextTest : public virtual SuggestionEngineTest {
 public:
  NextTest() : listener_binding_(&listener_) {
    suggestion_provider()->SubscribeToNext(listener_binding_.NewBinding(),
                                           ctl_.NewRequest());
  }

  TestSuggestionListener* listener() { return &listener_; }

 protected:
  void SetResultCount(int count) { ctl_->SetResultCount(count); }

  int suggestion_count() const { return listener_.suggestion_count(); }
  const Suggestion* GetOnlySuggestion() const {
    return listener_.GetOnlySuggestion();
  }

  void KillController() { ctl_.reset(); }

 private:
  TestSuggestionListener listener_;
  fidl::Binding<SuggestionListener> listener_binding_;
  NextControllerPtr ctl_;
};

class ResultCountTest : public NextTest {
 public:
  ResultCountTest()
      : pub_(new NPublisher(context_engine())),
        sub_(new NProposals(context_engine(), suggestion_engine())) {}

 protected:
  // Publishes signals for n new suggestions to context.
  void PublishNewSignal(int n = 1) { pub_->Publish(n_ += n); }

 private:
  std::unique_ptr<NPublisher> pub_;
  std::unique_ptr<NProposals> sub_;
  int n_ = 0;
};

}  // namespace

// Macro rather than method to capture the expectation in the assertion message.
#define CHECK_RESULT_COUNT(expected) ASYNC_EQ(expected, suggestion_count())

TEST_F(ResultCountTest, InitiallyEmpty) {
  SetResultCount(10);
  CHECK_RESULT_COUNT(0);
}

TEST_F(ResultCountTest, OneByOne) {
  SetResultCount(10);
  PublishNewSignal();
  CHECK_RESULT_COUNT(1);

  PublishNewSignal();
  CHECK_RESULT_COUNT(2);
}

TEST_F(ResultCountTest, AddOverLimit) {
  PublishNewSignal(3);
  CHECK_RESULT_COUNT(0);

  SetResultCount(1);
  CHECK_RESULT_COUNT(1);

  SetResultCount(3);
  CHECK_RESULT_COUNT(3);

  SetResultCount(5);
  CHECK_RESULT_COUNT(3);

  PublishNewSignal(4);
  CHECK_RESULT_COUNT(5);
}

TEST_F(ResultCountTest, Clear) {
  SetResultCount(10);
  PublishNewSignal(3);
  CHECK_RESULT_COUNT(3);

  SetResultCount(0);
  CHECK_RESULT_COUNT(0);

  SetResultCount(10);
  CHECK_RESULT_COUNT(3);
}

TEST_F(ResultCountTest, MultiRemove) {
  SetResultCount(10);
  PublishNewSignal(3);
  CHECK_RESULT_COUNT(3);

  SetResultCount(1);
  CHECK_RESULT_COUNT(1);

  SetResultCount(10);
  CHECK_RESULT_COUNT(3);
}

// The ideas agent only publishes a single proposal ID, so each new idea is a
// duplicate suggestion. Test that given two such ideas (via two GPS locations),
// only the latest is kept.
TEST_F(NextTest, Dedup) {
  acquirers::MockGps gps(context_engine());
  StartContextAgent("file:///system/apps/agents/carmen_sandiego");
  StartSuggestionAgent("file:///system/apps/agents/ideas");

  SetResultCount(10);
  gps.Publish(90, 0);
  CHECK_RESULT_COUNT(1);
  const Suggestion* suggestion = GetOnlySuggestion();
  const std::string uuid1 = suggestion->uuid;
  const std::string headline1 = suggestion->display->headline;
  gps.Publish(-90, 0);
  CHECK_RESULT_COUNT(1);
  suggestion = GetOnlySuggestion();
  EXPECT_EQ(uuid1, suggestion->uuid);
  EXPECT_NE(headline1, suggestion->display->headline);
}

// Tests two different agents proposing with the same ID (expect distinct
// proposals). One agent is the agents/ideas process while the other is the test
// itself (maxwell_test).
TEST_F(NextTest, NamespacingPerAgent) {
  acquirers::MockGps gps(context_engine());
  StartContextAgent("file:///system/apps/agents/carmen_sandiego");
  StartSuggestionAgent("file:///system/apps/agents/ideas");
  Proposinator conflictinator(suggestion_engine());

  SetResultCount(10);
  gps.Publish(90, 0);
  // Spoof the idea agent's proposal ID (well, not really spoofing since they
  // are namespaced by component).
  conflictinator.Propose(agents::IdeasAgent::kIdeaId);
  CHECK_RESULT_COUNT(2);
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

  SetResultCount(10);
  fifo.Propose("1");
  CHECK_RESULT_COUNT(1);
  auto uuid_1 = GetOnlySuggestion()->uuid;

  fifo.Propose("2");
  CHECK_RESULT_COUNT(2);
  fifo.Remove("1");
  CHECK_RESULT_COUNT(1);
  auto suggestion = GetOnlySuggestion();
  EXPECT_NE(uuid_1, suggestion->uuid);
  EXPECT_EQ("2", suggestion->display->headline);
}

// Tests the removal of earlier suggestions while capped.
// TODO(rosswang): see above TODO
TEST_F(NextTest, CappedFifo) {
  Proposinator fifo(suggestion_engine());

  SetResultCount(1);
  fifo.Propose("1");
  CHECK_RESULT_COUNT(1);
  auto uuid1 = GetOnlySuggestion()->uuid;

  fifo.Propose("2");
  Sleep();
  EXPECT_EQ(uuid1, GetOnlySuggestion()->uuid)
      << "Proposal 2 ranked over proposal 2; test invalid; update to test "
         "FIFO-ranked proposals.";

  fifo.Remove("1");
  // Need the suggestion-count() == 1 because there may be a brief moment when
  // the suggestion count is 2.
  ASYNC_CHECK(suggestion_count() == 1 && GetOnlySuggestion()->uuid != uuid1);

  EXPECT_EQ("2", GetOnlySuggestion()->display->headline);
}

TEST_F(NextTest, RemoveBeforeSubscribe) {
  Proposinator zombinator(suggestion_engine());

  zombinator.Propose("brains");
  zombinator.Remove("brains");
  Sleep();

  SetResultCount(10);
  CHECK_RESULT_COUNT(0);
}

TEST_F(NextTest, SubscribeBeyondController) {
  Proposinator p(suggestion_engine());

  SetResultCount(10);
  KillController();
  Sleep();
  p.Propose("1");
  p.Propose("2");
  CHECK_RESULT_COUNT(2);
}

class SuggestionInteractionTest : public NextTest {};

TEST_F(SuggestionInteractionTest, AcceptSuggestion) {
  Proposinator p(suggestion_engine());
  SetResultCount(10);

  auto create_story = CreateStory::New();
  create_story->module_id = "foo://bar";
  auto action = Action::New();
  action->set_create_story(std::move(create_story));
  fidl::Array<ActionPtr> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  CHECK_RESULT_COUNT(1);

  auto suggestion_id = GetOnlySuggestion()->uuid;
  AcceptSuggestion(suggestion_id);
  ASYNC_EQ("foo://bar", story_provider()->last_created_story());
}

TEST_F(SuggestionInteractionTest, AcceptSuggestion_WithInitialData) {
  Proposinator p(suggestion_engine());
  SetResultCount(10);

  auto create_story = CreateStory::New();
  create_story->module_id = "foo://bar";
  auto action = Action::New();

  rapidjson::Document doc;
  rapidjson::Pointer("/foo/bar").Set(doc, "some_data");
  create_story->initial_data = modular::JsonValueToString(doc);

  action->set_create_story(std::move(create_story));
  fidl::Array<ActionPtr> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  CHECK_RESULT_COUNT(1);

  auto suggestion_id = GetOnlySuggestion()->uuid;
  AcceptSuggestion(suggestion_id);
  ASYNC_EQ("foo://bar", story_provider()->last_created_story());
}

TEST_F(SuggestionInteractionTest, AcceptSuggestion_AddModule) {
  Proposinator p(suggestion_engine());
  SetResultCount(10);

  auto module_id = "foo://bar1";

  auto add_module_to_story = AddModuleToStory::New();
  add_module_to_story->story_id = "foo://bar";
  add_module_to_story->module_name = module_id;
  add_module_to_story->module_url = module_id;
  add_module_to_story->module_path = fidl::Array<fidl::String>::New(0);
  add_module_to_story->link_name = "";

  auto action = Action::New();
  action->set_add_module_to_story(std::move(add_module_to_story));
  fidl::Array<ActionPtr> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  CHECK_RESULT_COUNT(1);

  auto suggestion_id = GetOnlySuggestion()->uuid;
  AcceptSuggestion(suggestion_id);

  ASYNC_EQ(module_id, story_provider()->story_controller().last_added_module());
}

class AskTest : public virtual SuggestionEngineTest {
 public:
  AskTest() : binding_(&listener_) {}

  void InitiateAsk() {
    suggestion_provider()->InitiateAsk(binding_.NewBinding(),
                                       ctl_.NewRequest());
  }

  void KillListener() { binding_.Close(); }

  void SetQuery(const std::string& query) {
    auto input = UserInput::New();
    input->set_text(query);
    ctl_->SetUserInput(std::move(input));
  }

  void SetResultCount(int32_t count) { ctl_->SetResultCount(count); }

  int suggestion_count() const { return listener_.suggestion_count(); }

  TestSuggestionListener* listener() { return &listener_; }

 private:
  TestSuggestionListener listener_;
  fidl::Binding<SuggestionListener> binding_;
  AskControllerPtr ctl_;
};

TEST_F(AskTest, DefaultAsk) {
  Proposinator p(suggestion_engine());

  p.Propose("1");
  Sleep();

  InitiateAsk();

  SetResultCount(10);
  CHECK_RESULT_COUNT(1);

  p.Propose("2");
  CHECK_RESULT_COUNT(2);
}

#define CHECK_ONLY_HEADLINE(h)                       \
  ASYNC_CHECK(listener()->suggestion_count() == 1 && \
              listener()->GetOnlySuggestion()->display->headline == h)

TEST_F(AskTest, AskIncludeExclude) {
  Proposinator p(suggestion_engine());

  p.Propose("Mozart's Ghost");
  p.Propose("The Hottest Band on the Internet");

  InitiateAsk();
  SetResultCount(10);
  SetQuery("The Hottest Band on the Internet");
  CHECK_ONLY_HEADLINE("The Hottest Band on the Internet");

  SetQuery("Mozart's Ghost");
  CHECK_ONLY_HEADLINE("Mozart's Ghost");

  p.Propose("Mozart's Ghost", "Gatekeeper");
  CHECK_RESULT_COUNT(0);

  p.Propose("The Hottest Band on the Internet", "Mozart's Ghost");
  CHECK_RESULT_COUNT(1);
}

TEST_F(AskTest, AskIncludeExcludeFlip) {
  Proposinator p(suggestion_engine());

  p.Propose("Mozart's Ghost");
  InitiateAsk();
  SetResultCount(10);

  CHECK_RESULT_COUNT(1);
  SetQuery("Mo");
  CHECK_RESULT_COUNT(1);
  SetQuery("Mox");
  CHECK_RESULT_COUNT(0);
  SetQuery("Mo");
  CHECK_RESULT_COUNT(1);
  SetQuery("Mox");
  CHECK_RESULT_COUNT(0);
}

TEST_F(AskTest, RemoveAskFallback) {
  Proposinator p(suggestion_engine());

  p.Propose("Esc");
  InitiateAsk();
  SetResultCount(10);
  CHECK_RESULT_COUNT(1);

  p.Remove("Esc");
  CHECK_RESULT_COUNT(0);
}

TEST_F(AskTest, ChangeFallback) {
  Proposinator p(suggestion_engine());

  p.Propose("E-mail");
  InitiateAsk();
  SetResultCount(10);
  CHECK_RESULT_COUNT(1);

  p.Propose("E-mail", "E-vite");
  CHECK_ONLY_HEADLINE("E-vite");

  // Make sure we're still alive; historical crash above
  SetQuery("X");
  CHECK_RESULT_COUNT(0);
}

TEST_F(AskTest, ChangeSameRank) {
  Proposinator p(suggestion_engine());

  p.Propose("E-mail");
  p.Propose("Music");
  InitiateAsk();
  SetResultCount(10);
  CHECK_RESULT_COUNT(2);

  SetQuery("E");
  CHECK_RESULT_COUNT(1);
  p.Propose("E-mail", "E-vite");  // E-mail and E-vite are equidistant from E
  CHECK_ONLY_HEADLINE("E-vite");

  // Make sure we're still alive; historical crash above
  SetQuery("X");
  CHECK_RESULT_COUNT(0);
}

TEST_F(AskTest, ChangeAmbiguousRank) {
  Proposinator p(suggestion_engine());

  p.Propose("E-mail");
  p.Propose("E-vite");
  p.Propose("E-card");
  p.Propose("Music");
  InitiateAsk();
  SetResultCount(10);
  CHECK_RESULT_COUNT(4);

  SetQuery("E");
  CHECK_RESULT_COUNT(3);
  p.Propose("E-vite", "E-pass");
  p.Propose("E-mail", "Comms");
  p.Propose("E-vite", "RSVP");
  CHECK_RESULT_COUNT(1);  // historical assertion failure by now
  // Note that we can't just have removed one and checked that because on
  // assertion failure, one remove will have happened (at least as of the
  // 11/29/17 codebase).
}

TEST_F(AskTest, ChangeWorseSameOrder) {
  Proposinator p(suggestion_engine());

  p.Propose("E-mail");
  p.Propose("Music");
  InitiateAsk();
  SetResultCount(10);
  CHECK_RESULT_COUNT(2);

  SetQuery("E");
  CHECK_RESULT_COUNT(1);
  p.Propose("E-mail", "Messaging");  // Messaging is a worse match than E-mail
  CHECK_ONLY_HEADLINE("Messaging");

  // Make sure we're still alive; historical crash above
  SetQuery("X");
  CHECK_RESULT_COUNT(0);
}

TEST_F(AskTest, ChangeSuboptimal) {
  Proposinator p(suggestion_engine());

  p.Propose("E-mail");
  p.Propose("Evisceration");
  p.Propose("Magic");
  InitiateAsk();
  SetResultCount(10);
  CHECK_RESULT_COUNT(3);

  SetQuery("E");
  CHECK_RESULT_COUNT(2);
  p.Propose("Evisceration", "Incarceration");  // both are worse than E-mail
  ASYNC_CHECK(suggestion_count() == 2 &&
              (*listener())[1]->display->headline == "Incarceration");

  // Make sure we're still alive; historical crash above
  SetQuery("X");
  CHECK_RESULT_COUNT(0);
}

#define HEADLINE_EQ(expected, index) \
  EXPECT_EQ(expected, (*listener())[index]->display->headline)

TEST_F(AskTest, AskRanking) {
  Proposinator p(suggestion_engine());

  p.Propose("View E-mail");
  p.Propose("Compose E-mail");
  p.Propose("Reply to E-mail");
  p.Propose("Send E-vites");
  p.Propose("E-mail Guests");

  InitiateAsk();
  SetResultCount(10);

  CHECK_RESULT_COUNT(5);
  HEADLINE_EQ("View E-mail", 0);
  HEADLINE_EQ("Compose E-mail", 1);
  HEADLINE_EQ("Reply to E-mail", 2);
  HEADLINE_EQ("Send E-vites", 3);
  HEADLINE_EQ("E-mail Guests", 4);

  SetQuery("e-mail");
  CHECK_RESULT_COUNT(4);
  HEADLINE_EQ("View E-mail", 0);
  HEADLINE_EQ("E-mail Guests", 1);
  HEADLINE_EQ("Compose E-mail", 2);
  HEADLINE_EQ("Reply to E-mail", 3);

  SetResultCount(2);
  CHECK_RESULT_COUNT(2);
  HEADLINE_EQ("View E-mail", 0);
  HEADLINE_EQ("E-mail Guests", 1);

  SetQuery("Compose");
  CHECK_RESULT_COUNT(1);
  HEADLINE_EQ("Compose E-mail", 0);
}

class AskProposinator : public Proposinator, public AskHandler {
 public:
  AskProposinator(SuggestionEngine* suggestion_engine,
                  const fidl::String& url = "AskProposinator")
      : Proposinator(suggestion_engine, url), ask_binding_(this) {
    fidl::InterfaceHandle<AskHandler> ask_handle;
    ask_binding_.Bind(&ask_handle);
    out_->RegisterAskHandler(std::move(ask_handle));
  }

  void Ask(UserInputPtr query, const AskCallback& callback) override {
    query_ = std::move(query);
    ask_callback_ = callback;
    ask_proposals_.resize(0);
  }

  void Commit() { ask_callback_(std::move(ask_proposals_)); }

  fidl::String query() const { return query_ ? query_->get_text() : NULL; }

  void ProposeForAsk(
      const std::string& id,
      fidl::Array<ActionPtr> actions = fidl::Array<ActionPtr>::New(0)) {
    ProposeForAsk(id, id, std::move(actions));
  }

  void ProposeForAsk(
      const std::string& id,
      const std::string& headline,
      fidl::Array<ActionPtr> actions = fidl::Array<ActionPtr>::New(0)) {
    ask_proposals_.push_back(CreateProposal(id, headline, std::move(actions)));
  }

 private:
  fidl::Binding<AskHandler> ask_binding_;
  UserInputPtr query_;
  fidl::Array<ProposalPtr> ask_proposals_;
  AskCallback ask_callback_;
};

// Ensure that proposals made while handling an Ask query:
// * are not textwise filtered by the query (unlike Next).
// * fully replace any proposals made while handling a previous Ask query.
TEST_F(AskTest, ReactiveAsk) {
  AskProposinator p(suggestion_engine());

  InitiateAsk();
  SetResultCount(10);
  SetQuery("Hello");

  ASYNC_EQ("Hello", p.query());
  p.ProposeForAsk("Hi, how can I help?");
  p.ProposeForAsk("What can you do?");
  p.Commit();

  CHECK_RESULT_COUNT(2);

  SetQuery("Stuff happens.");
  ASYNC_EQ("Stuff happens.", p.query());
  p.ProposeForAsk("What can you do?");
  p.Commit();

  CHECK_RESULT_COUNT(1);
}

// Ensure that Ask continues to work even if the Next publisher has
// disconnected.
TEST_F(AskTest, AskWithoutPublisher) {
  AskProposinator p(suggestion_engine());
  p.KillPublisher();

  InitiateAsk();
  SetResultCount(10);
  SetQuery("I have a pen. I have an apple.");

  ASYNC_EQ("I have a pen. I have an apple.", p.query());
  p.ProposeForAsk("Apple pen!");
  p.Commit();

  CHECK_RESULT_COUNT(1);
}

class MultiChannelTest : public AskTest, public NextTest {};

TEST_F(MultiChannelTest, PublishAfterAskEnd) {
  Proposinator p(suggestion_engine());

  p.Propose("E-mail");
  InitiateAsk();
  NextTest::SetResultCount(10);
  AskTest::SetResultCount(10);
  ASYNC_EQ(1, NextTest::suggestion_count());
  ASYNC_EQ(1, AskTest::suggestion_count());

  AskTest::KillListener();
  Sleep();

  p.Propose("E-mail", "E-vite");

  // Historical failure mode: Prior to implementing ranked-suggestion cleanup on
  // AskChannel destruction, this would seg fault due to trying to add the
  // proposal to the destroyed channel.

  ASYNC_CHECK(NextTest::listener()->suggestion_count() == 1 &&
              NextTest::listener()->GetOnlySuggestion()->display->headline ==
                  "E-vite");
}

class SuggestionFilteringTest : public NextTest {};

TEST_F(SuggestionFilteringTest, Baseline) {
  Sleep();  // TEMPORARY; wait for init

  // Show that without any existing Stories, we see Proposals to launch
  // any story.
  Proposinator p(suggestion_engine());
  SetResultCount(10);

  auto create_story = CreateStory::New();
  create_story->module_id = "foo://bar";
  auto action = Action::New();
  action->set_create_story(std::move(create_story));
  fidl::Array<ActionPtr> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  CHECK_RESULT_COUNT(1);
}

TEST_F(SuggestionFilteringTest, Baseline_FilterDoesntMatch) {
  Sleep();  // TEMPORARY; wait for init

  // Show that with an existing Story for a URL, we see Proposals to launch
  // other URLs.
  Proposinator p(suggestion_engine());
  SetResultCount(10);

  // First notify watchers of the StoryProvider that a story
  // already exists.
  auto story_info = modular::StoryInfo::New();
  story_info->url = "foo://bazzle_dazzle";
  story_info->id = "";
  story_info->extra.mark_non_null();
  story_provider()->NotifyStoryChanged(std::move(story_info),
                                       modular::StoryState::INITIAL);

  auto create_story = CreateStory::New();
  create_story->module_id = "foo://bar";
  auto action = Action::New();
  action->set_create_story(std::move(create_story));
  fidl::Array<ActionPtr> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  CHECK_RESULT_COUNT(1);
}

TEST_F(SuggestionFilteringTest, FilterOnPropose) {
  Sleep();  // TEMPORARY; wait for init

  // If a Story already exists, then Proposals that want to create
  // that same story are filtered when they are proposed.
  Proposinator p(suggestion_engine());
  SetResultCount(10);

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
  fidl::Array<ActionPtr> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  p.Propose("2");
  CHECK_RESULT_COUNT(1);
}

TEST_F(SuggestionFilteringTest, ChangeFiltered) {
  Sleep();  // TEMPORARY; wait for init

  Proposinator p(suggestion_engine());
  SetResultCount(10);

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
    fidl::Array<ActionPtr> actions;
    actions.push_back(std::move(action));

    p.Propose("1", std::move(actions));
  }

  // historically crashed by now
  p.Propose("2");

  CHECK_RESULT_COUNT(1);
}

}  // namespace maxwell
