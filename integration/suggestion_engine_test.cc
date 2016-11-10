// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>

#include "apps/maxwell/services/context_engine.fidl.h"
#include "apps/maxwell/services/suggestion_engine.fidl.h"
#include "apps/maxwell/services/formatting.h"
#include "lib/fidl/cpp/bindings/binding.h"

#include "apps/maxwell/acquirers/mock/mock_gps.h"
#include "apps/maxwell/agents/ideas.h"
#include "apps/maxwell/integration/context_engine_test_base.h"

using namespace maxwell::acquirers;
using namespace maxwell::agents;
using namespace maxwell::context_engine;
using namespace maxwell::suggestion_engine;
using namespace fidl;

constexpr char IdeasAgent::kIdeaId[];

namespace {

class TestListener : public SuggestionListener {
 public:
  void OnAdd(fidl::Array<SuggestionPtr> suggestions) override {
    FTL_LOG(INFO) << "OnAdd(" << suggestions << ")";
    naive_suggestion_count_ += suggestions.size();
    for (auto& suggestion : suggestions)
      suggestions_[suggestion->uuid] = std::move(suggestion);

    EXPECT_EQ(naive_suggestion_count_, (signed)suggestions_.size());
  }

  void OnRemove(const fidl::String& uuid) override {
    FTL_LOG(INFO) << "OnRemove(" << uuid << ")";
    naive_suggestion_count_--;
    suggestions_.erase(uuid);

    EXPECT_EQ(naive_suggestion_count_, (signed)suggestions_.size());
  }

  void OnRemoveAll() override {
    FTL_LOG(INFO) << "OnRemoveAll";
    naive_suggestion_count_ = 0;
    suggestions_.clear();
  }

  int suggestion_count() const { return naive_suggestion_count_; }

  // Exposes a pointer to the only suggestion in this listener. Retains
  // ownership of the pointer.
  const Suggestion* GetOnlySuggestion() const {
    EXPECT_EQ(1, suggestion_count());
    return suggestions_.begin()->second.get();
  }

 private:
  int naive_suggestion_count_ = 0;
  std::unordered_map<std::string, SuggestionPtr> suggestions_;
};

// context agent that publishes an int n
class NPublisher {
 public:
  NPublisher(const ContextEnginePtr& context_engine) {
    ContextAcquirerClientPtr out;
    context_engine->RegisterContextAcquirer("NPublisher", GetProxy(&out));
    out->Publish("n", "int", NULL, GetProxy(&pub_));
  }

  void Publish(int n) { pub_->Update(std::to_string(n)); }

 private:
  ContextPublisherLinkPtr pub_;
};

class Proposinator {
 public:
  Proposinator(const SuggestionEnginePtr& suggestion_engine,
               const fidl::String& url = "Proposinator") {
    suggestion_engine->RegisterSuggestionAgent("Proposinator", GetProxy(&pm_));
  }

  virtual ~Proposinator() = default;

  void Propose(const std::string& id) {
    ProposalPtr p = Proposal::New();
    p->id = id;
    p->on_selected = fidl::Array<ActionPtr>::New(0);
    SuggestionDisplayPropertiesPtr d = SuggestionDisplayProperties::New();

    d->icon = "";
    d->headline = "";
    d->subheadline = "";
    d->details = "";

    p->display = std::move(d);

    pm_->Propose(std::move(p));
  }

  void Remove(const std::string& id) { pm_->Remove(id); }

 private:
  ProposalManagerPtr pm_;
};

// maintains the number of proposals specified by the context field "n"
class NProposals : public Proposinator, public ContextSubscriberLink {
 public:
  NProposals(const ContextEnginePtr& context_engine,
             const SuggestionEnginePtr& suggestion_engine)
      : Proposinator(suggestion_engine, "NProposals"), link_binding_(this) {
    context_engine->RegisterSuggestionAgent("NProposals", GetProxy(&cx_));

    InterfaceHandle<ContextSubscriberLink> link_handle;
    link_binding_.Bind(&link_handle);
    cx_->Subscribe("n", "int", std::move(link_handle));
  }

  void OnUpdate(ContextUpdatePtr update) override {
    int n = std::stoi(update->json_value);

    for (int i = n_; i < n; i++)
      Propose(std::to_string(i));
    for (int i = n; i < n_; i++)
      Remove(std::to_string(i));

    n_ = n;
  }

 private:
  SuggestionAgentClientPtr cx_;
  Binding<ContextSubscriberLink> link_binding_;

  int n_ = 0;
};

class SuggestionEngineTest : public ContextEngineTestBase {
 public:
  SuggestionEngineTest() : listener_binding_(&listener_) {
    modular::ServiceProviderPtr suggestion_engine_services =
        StartEngine("file:///system/apps/suggestion_engine");
    suggestion_engine_ = modular::ConnectToService<SuggestionEngine>(
        suggestion_engine_services.get());
    suggestion_manager_ = modular::ConnectToService<SuggestionManager>(
        suggestion_engine_services.get());

    InterfaceHandle<SuggestionListener> listener_handle;
    listener_binding_.Bind(GetProxy(&listener_handle));
    suggestion_manager_->SubscribeToNext(std::move(listener_handle),
                                         GetProxy(&ctl_));
  }

 protected:
  void SetResultCount(int count) { ctl_->SetResultCount(count); }

  int suggestion_count() const { return listener_.suggestion_count(); }
  const Suggestion* GetOnlySuggestion() const {
    return listener_.GetOnlySuggestion();
  }

  void StartSuggestionAgent(const std::string& url) {
    auto agent_host = std::make_unique<maxwell::AgentEnvironmentHost>();
    agent_host->AddService<SuggestionAgentClient>(
        [this, url](fidl::InterfaceRequest<SuggestionAgentClient> request) {
          cx_->RegisterSuggestionAgent(url, std::move(request));
        });
    agent_host->AddService<ProposalManager>(
        [this, url](fidl::InterfaceRequest<ProposalManager> request) {
          suggestion_engine_->RegisterSuggestionAgent(url, std::move(request));
        });
    StartAgent(url, std::move(agent_host));
  }

  SuggestionEnginePtr suggestion_engine_;

 private:
  SuggestionManagerPtr suggestion_manager_;
  TestListener listener_;
  Binding<SuggestionListener> listener_binding_;
  NextControllerPtr ctl_;
};

class ResultCountTest : public SuggestionEngineTest {
 public:
  ResultCountTest()
      : pub_(new NPublisher(cx_)),
        sub_(new NProposals(cx_, suggestion_engine_)) {}

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
  MockGps gps(cx_);
  StartContextAgent("file:///system/apps/agents/carmen_sandiego");
  StartSuggestionAgent("file:///system/apps/agents/ideas");

  SetResultCount(10);
  gps.Publish(90, 0);
  CHECK_RESULT_COUNT(1);
  const Suggestion* suggestion = GetOnlySuggestion();
  const std::string uuid1 = suggestion->uuid;
  const std::string headline1 = suggestion->display_properties->headline;
  gps.Publish(-90, 0);
  CHECK_RESULT_COUNT(1);
  suggestion = GetOnlySuggestion();
  EXPECT_EQ(uuid1, suggestion->uuid);
  EXPECT_NE(headline1, suggestion->display_properties->headline);
}

// Tests two different agents proposing with the same ID (expect distinct
// proposals). One agent is the agents/ideas process while the other is the test
// itself (maxwell_test).
TEST_F(SuggestionEngineTest, NamespacingPerAgent) {
  MockGps gps(cx_);
  StartContextAgent("file:///system/apps/agents/carmen_sandiego");
  StartSuggestionAgent("file:///system/apps/agents/ideas");
  Proposinator conflictinator(suggestion_engine_);

  SetResultCount(10);
  gps.Publish(90, 0);
  // Spoof the idea agent's proposal ID (well, not really spoofing since they
  // are namespaced by component).
  conflictinator.Propose(IdeasAgent::kIdeaId);
  CHECK_RESULT_COUNT(2);
}
