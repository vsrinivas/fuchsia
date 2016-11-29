// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>

#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_engine.fidl.h"
#include "apps/maxwell/src/acquirers/mock/mock_gps.h"
#include "apps/maxwell/src/agents/ideas.h"
#include "apps/maxwell/src/integration/context_engine_test_base.h"
#include "apps/maxwell/src/integration/test_suggestion_listener.h"
#include "lib/fidl/cpp/bindings/binding.h"

constexpr char maxwell::agents::IdeasAgent::kIdeaId[];

namespace {

// context agent that publishes an int n
class NPublisher {
 public:
  NPublisher(maxwell::context::ContextEngine* context_engine) {
    maxwell::context::ContextAcquirerClientPtr out;
    context_engine->RegisterContextAcquirer("NPublisher", GetProxy(&out));
    out->Publish("n", "int", NULL, GetProxy(&pub_));
  }

  void Publish(int n) { pub_->Update(std::to_string(n)); }

 private:
  maxwell::context::PublisherLinkPtr pub_;
};

class Proposinator {
 public:
  Proposinator(maxwell::suggestion::SuggestionEngine* suggestion_engine,
               const fidl::String& url = "Proposinator") {
    suggestion_engine->RegisterSuggestionAgent("Proposinator", GetProxy(&out_));
  }

  virtual ~Proposinator() = default;

  void Propose(const std::string& id,
               fidl::Array<maxwell::suggestion::ActionPtr> actions =
                   fidl::Array<maxwell::suggestion::ActionPtr>::New(0)) {
    Propose(id, id, std::move(actions));
  }

  void Propose(const std::string& id,
               const std::string& headline,
               fidl::Array<maxwell::suggestion::ActionPtr> actions =
                   fidl::Array<maxwell::suggestion::ActionPtr>::New(0)) {
    auto p = maxwell::suggestion::Proposal::New();
    p->id = id;
    p->on_selected = std::move(actions);
    auto d = maxwell::suggestion::Display::New();

    d->headline = headline;
    d->subheadline = "";
    d->details = "";
    d->color = 0x00aa00aa;  // argb purple
    d->icon_urls = fidl::Array<fidl::String>::New(1);
    d->icon_urls[0] = "";
    d->image_url = "";
    d->image_type = maxwell::suggestion::SuggestionImageType::PERSON;

    p->display = std::move(d);

    out_->Propose(std::move(p));
  }

  void Remove(const std::string& id) { out_->Remove(id); }

 protected:
  maxwell::suggestion::SuggestionAgentClientPtr out_;
};

// maintains the number of proposals specified by the context field "n"
class NProposals : public Proposinator,
                   public maxwell::context::SubscriberLink {
 public:
  NProposals(maxwell::context::ContextEngine* context_engine,
             maxwell::suggestion::SuggestionEngine* suggestion_engine)
      : Proposinator(suggestion_engine, "NProposals"), link_binding_(this) {
    context_engine->RegisterSuggestionAgent("NProposals",
                                            GetProxy(&context_client_));

    fidl::InterfaceHandle<maxwell::context::SubscriberLink> link_handle;
    link_binding_.Bind(&link_handle);
    context_client_->Subscribe("n", "int", std::move(link_handle));
  }

  void OnUpdate(maxwell::context::UpdatePtr update) override {
    int n = std::stoi(update->json_value);

    for (int i = n_; i < n; i++)
      Propose(std::to_string(i));
    for (int i = n; i < n_; i++)
      Remove(std::to_string(i));

    n_ = n;
  }

 private:
  maxwell::context::SuggestionAgentClientPtr context_client_;
  fidl::Binding<maxwell::context::SubscriberLink> link_binding_;

  int n_ = 0;
};

class SuggestionEngineTest : public ContextEngineTestBase {
 public:
  SuggestionEngineTest() {
    modular::ServiceProviderPtr suggestion_services =
        StartServiceProvider("file:///system/apps/suggestion_engine");
    suggestion_engine_ =
        modular::ConnectToService<maxwell::suggestion::SuggestionEngine>(
            suggestion_services.get());
    suggestion_provider_ =
        modular::ConnectToService<maxwell::suggestion::SuggestionProvider>(
            suggestion_services.get());
  }

 protected:
  maxwell::suggestion::SuggestionEngine* suggestion_engine() {
    return suggestion_engine_.get();
  }

  maxwell::suggestion::SuggestionProvider* suggestion_provider() {
    return suggestion_provider_.get();
  }

  void StartSuggestionAgent(const std::string& url) {
    auto agent_host =
        std::make_unique<maxwell::ApplicationEnvironmentHostImpl>();
    agent_host->AddService<maxwell::context::SuggestionAgentClient>([this, url](
        fidl::InterfaceRequest<maxwell::context::SuggestionAgentClient>
            request) {
      context_engine()->RegisterSuggestionAgent(url, std::move(request));
    });
    agent_host->AddService<maxwell::suggestion::SuggestionAgentClient>(
        [this,
         url](fidl::InterfaceRequest<maxwell::suggestion::SuggestionAgentClient>
                  request) {
          suggestion_engine_->RegisterSuggestionAgent(url, std::move(request));
        });
    StartAgent(url, std::move(agent_host));
  }

  void AcceptSuggestion(const std::string& suggestion_id) {
    Interact(suggestion_id, maxwell::suggestion::InteractionType::SELECTED);
  }

  void DismissSuggestion(const std::string& suggestion_id) {
    Interact(suggestion_id, maxwell::suggestion::InteractionType::DISMISSED);
  }

 private:
  void Interact(const std::string& suggestion_id,
                maxwell::suggestion::InteractionType interaction_type) {
    auto interaction = maxwell::suggestion::Interaction::New();
    interaction->type = interaction_type;
    suggestion_provider_->NotifyInteraction(suggestion_id,
                                            std::move(interaction));
  }

  maxwell::suggestion::SuggestionEnginePtr suggestion_engine_;
  maxwell::suggestion::SuggestionProviderPtr suggestion_provider_;
};

class NextTest : public SuggestionEngineTest {
 public:
  NextTest() : listener_binding_(&listener_) {
    fidl::InterfaceHandle<maxwell::suggestion::Listener> listener_handle;
    listener_binding_.Bind(&listener_handle);
    suggestion_provider()->SubscribeToNext(std::move(listener_handle),
                                           GetProxy(&ctl_));
  }

 protected:
  void SetResultCount(int count) { ctl_->SetResultCount(count); }

  int suggestion_count() const { return listener_.suggestion_count(); }
  const maxwell::suggestion::Suggestion* GetOnlySuggestion() const {
    return listener_.GetOnlySuggestion();
  }

  void KillController() { ctl_.reset(); }

 private:
  TestSuggestionListener listener_;
  fidl::Binding<maxwell::suggestion::Listener> listener_binding_;
  maxwell::suggestion::NextControllerPtr ctl_;
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
  maxwell::acquirers::MockGps gps(context_engine());
  StartContextAgent("file:///system/apps/agents/carmen_sandiego");
  StartSuggestionAgent("file:///system/apps/agents/ideas");

  SetResultCount(10);
  gps.Publish(90, 0);
  CHECK_RESULT_COUNT(1);
  const maxwell::suggestion::Suggestion* suggestion = GetOnlySuggestion();
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
  maxwell::acquirers::MockGps gps(context_engine());
  StartContextAgent("file:///system/apps/agents/carmen_sandiego");
  StartSuggestionAgent("file:///system/apps/agents/ideas");
  Proposinator conflictinator(suggestion_engine());

  SetResultCount(10);
  gps.Publish(90, 0);
  // Spoof the idea agent's proposal ID (well, not really spoofing since they
  // are namespaced by component).
  conflictinator.Propose(maxwell::agents::IdeasAgent::kIdeaId);
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

class TestStoryProvider : public modular::StoryProvider {
 public:
  void CreateStory(
      const fidl::String& url,
      fidl::InterfaceRequest<modular::StoryController> story) override {
    last_created_story_ = url;
  }

  void Watch(
      fidl::InterfaceHandle<modular::StoryProviderWatcher> watcher) override {}

  void DeleteStory(const fidl::String& story_id,
                   const DeleteStoryCallback& callback) override {}
  void GetStoryInfo(const fidl::String& story_id,
                    const GetStoryInfoCallback& callback) override {}
  void ResumeStory(
      const fidl::String& story_id,
      fidl::InterfaceRequest<modular::StoryController> story) override {}
  void PreviousStories(const PreviousStoriesCallback& callback) override {}

  std::string last_created_story() const { return last_created_story_; }

 private:
  std::string last_created_story_;
};

class SuggestionInteractionTest : public NextTest {
 public:
  SuggestionInteractionTest() : story_provider_binding_(&story_provider_) {
    fidl::InterfaceHandle<modular::StoryProvider> story_provider_handle;
    story_provider_binding_.Bind(&story_provider_handle);
    suggestion_engine()->Initialize(std::move(story_provider_handle));
  }

 protected:
  std::string last_created_story() const {
    return story_provider_.last_created_story();
  }

 private:
  TestStoryProvider story_provider_;
  fidl::Binding<modular::StoryProvider> story_provider_binding_;
};

TEST_F(SuggestionInteractionTest, AcceptSuggestion) {
  Proposinator p(suggestion_engine());
  SetResultCount(10);

  auto create_story = maxwell::suggestion::CreateStory::New();
  create_story->module_id = "foo://bar";
  auto action = maxwell::suggestion::Action::New();
  action->set_create_story(std::move(create_story));
  fidl::Array<maxwell::suggestion::ActionPtr> actions;
  actions.push_back(std::move(action));
  p.Propose("1", std::move(actions));
  CHECK_RESULT_COUNT(1);

  auto suggestion_id = GetOnlySuggestion()->uuid;
  AcceptSuggestion(suggestion_id);
  ASYNC_EQ("foo://bar", last_created_story());
}

class AskTest : public SuggestionEngineTest {
 public:
  AskTest() : binding_(&listener_) {}

  void InitiateAsk() {
    fidl::InterfaceHandle<maxwell::suggestion::Listener> handle;
    binding_.Bind(&handle);
    suggestion_provider()->InitiateAsk(std::move(handle), GetProxy(&ctl_));
  }

  void SetQuery(const std::string& query) {
    auto input = maxwell::suggestion::UserInput::New();
    input->set_text(query);
    ctl_->SetUserInput(std::move(input));
  }

  void SetResultCount(int32_t count) { ctl_->SetResultCount(count); }

  int suggestion_count() const { return listener_.suggestion_count(); }

  TestSuggestionListener* listener() { return &listener_; }

 private:
  TestSuggestionListener listener_;
  fidl::Binding<maxwell::suggestion::Listener> binding_;
  maxwell::suggestion::AskControllerPtr ctl_;
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

class AskProposinator : public Proposinator,
                        public maxwell::suggestion::AskHandler {
 public:
  AskProposinator(maxwell::suggestion::SuggestionEngine* suggestion_engine,
                  const fidl::String& url = "AskProposinator")
      : Proposinator(suggestion_engine, url), ask_binding_(this) {
    fidl::InterfaceHandle<AskHandler> ask_handle;
    ask_binding_.Bind(&ask_handle);
    out_->RegisterAskHandler(std::move(ask_handle));
  }

  void Ask(maxwell::suggestion::UserInputPtr query,
           const AskCallback& callback) override {
    query_ = std::move(query);
    ask_callback_ = callback;
    ask_proposals_.resize(0);
  }

  void Commit() { ask_callback_(std::move(ask_proposals_)); }

  fidl::String query() const { return query_ ? query_->get_text() : NULL; }

 private:
  fidl::Binding<AskHandler> ask_binding_;
  maxwell::suggestion::UserInputPtr query_;
  fidl::Array<maxwell::suggestion::ProposalPtr> ask_proposals_;
  AskCallback ask_callback_;
};

TEST_F(AskTest, ReactiveAsk) {
  AskProposinator p(suggestion_engine());

  InitiateAsk();
  SetResultCount(10);
  SetQuery("Hello");

  ASYNC_EQ("Hello", p.query());
  p.Propose("Hello, Ask?");  // TODO(rosswang): Test attributed Ask.
  p.Commit();

  CHECK_RESULT_COUNT(1);
}
