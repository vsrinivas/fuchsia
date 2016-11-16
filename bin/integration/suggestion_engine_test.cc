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
  NPublisher(const maxwell::context::ContextEnginePtr& context_engine) {
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
  Proposinator(
      const maxwell::suggestion::SuggestionEnginePtr& suggestion_engine,
      const fidl::String& url = "Proposinator") {
    suggestion_engine->RegisterSuggestionAgent("Proposinator", GetProxy(&out_));
  }

  virtual ~Proposinator() = default;

  void Propose(const std::string& id) {
    auto p = maxwell::suggestion::Proposal::New();
    p->id = id;
    p->on_selected = fidl::Array<maxwell::suggestion::ActionPtr>::New(0);
    auto d = maxwell::suggestion::Display::New();

    d->headline = id;
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

 private:
  maxwell::suggestion::SuggestionAgentClientPtr out_;
};

// maintains the number of proposals specified by the context field "n"
class NProposals : public Proposinator,
                   public maxwell::context::SubscriberLink {
 public:
  NProposals(const maxwell::context::ContextEnginePtr& context_engine,
             const maxwell::suggestion::SuggestionEnginePtr& suggestion_engine)
      : Proposinator(suggestion_engine, "NProposals"), link_binding_(this) {
    context_engine->RegisterSuggestionAgent("NProposals",
                                            GetProxy(&context_engine_));

    fidl::InterfaceHandle<maxwell::context::SubscriberLink> link_handle;
    link_binding_.Bind(&link_handle);
    context_engine_->Subscribe("n", "int", std::move(link_handle));
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
  maxwell::context::SuggestionAgentClientPtr context_engine_;
  fidl::Binding<maxwell::context::SubscriberLink> link_binding_;

  int n_ = 0;
};

class SuggestionEngineTest : public ContextEngineTestBase {
 public:
  SuggestionEngineTest() : listener_binding_(&listener_) {
    modular::ServiceProviderPtr suggestion_services =
        StartServiceProvider("file:///system/apps/suggestion_engine");
    suggestion_engine_ =
        modular::ConnectToService<maxwell::suggestion::SuggestionEngine>(
            suggestion_services.get());
    suggestion_provider_ =
        modular::ConnectToService<maxwell::suggestion::SuggestionProvider>(
            suggestion_services.get());

    fidl::InterfaceHandle<maxwell::suggestion::Listener> listener_handle;
    listener_binding_.Bind(GetProxy(&listener_handle));
    suggestion_provider_->SubscribeToNext(std::move(listener_handle),
                                          GetProxy(&ctl_));
  }

 protected:
  void SetResultCount(int count) { ctl_->SetResultCount(count); }

  int suggestion_count() const { return listener_.suggestion_count(); }
  const maxwell::suggestion::Suggestion* GetOnlySuggestion() const {
    return listener_.GetOnlySuggestion();
  }

  void StartSuggestionAgent(const std::string& url) {
    auto agent_host =
        std::make_unique<maxwell::ApplicationEnvironmentHostImpl>();
    agent_host->AddService<maxwell::context::SuggestionAgentClient>([this, url](
        fidl::InterfaceRequest<maxwell::context::SuggestionAgentClient>
            request) {
      context_engine_->RegisterSuggestionAgent(url, std::move(request));
    });
    agent_host->AddService<maxwell::suggestion::SuggestionAgentClient>(
        [this,
         url](fidl::InterfaceRequest<maxwell::suggestion::SuggestionAgentClient>
                  request) {
          suggestion_engine_->RegisterSuggestionAgent(url, std::move(request));
        });
    StartAgent(url, std::move(agent_host));
  }

  maxwell::suggestion::SuggestionEnginePtr suggestion_engine_;

 private:
  maxwell::suggestion::SuggestionProviderPtr suggestion_provider_;
  TestSuggestionListener listener_;
  fidl::Binding<maxwell::suggestion::Listener> listener_binding_;
  maxwell::suggestion::NextControllerPtr ctl_;
};

class ResultCountTest : public SuggestionEngineTest {
 public:
  ResultCountTest()
      : pub_(new NPublisher(context_engine_)),
        sub_(new NProposals(context_engine_, suggestion_engine_)) {}

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
TEST_F(SuggestionEngineTest, Dedup) {
  maxwell::acquirers::MockGps gps(context_engine_);
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
TEST_F(SuggestionEngineTest, NamespacingPerAgent) {
  maxwell::acquirers::MockGps gps(context_engine_);
  StartContextAgent("file:///system/apps/agents/carmen_sandiego");
  StartSuggestionAgent("file:///system/apps/agents/ideas");
  Proposinator conflictinator(suggestion_engine_);

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
TEST_F(SuggestionEngineTest, Fifo) {
  Proposinator fifo(suggestion_engine_);

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
TEST_F(SuggestionEngineTest, CappedFifo) {
  Proposinator fifo(suggestion_engine_);

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

TEST_F(SuggestionEngineTest, RemoveBeforeSubscribe) {
  Proposinator zombinator(suggestion_engine_);

  zombinator.Propose("brains");
  zombinator.Remove("brains");
  Sleep();

  SetResultCount(10);
  CHECK_RESULT_COUNT(0);
}
