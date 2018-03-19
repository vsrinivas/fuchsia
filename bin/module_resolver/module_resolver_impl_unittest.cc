// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/module_resolver/module_resolver_impl.h"
#include "garnet/lib/gtest/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/entity/cpp/json.h"
#include "lib/fxl/files/file.h"
#include "lib/module_resolver/cpp/formatting.h"
#include "peridot/lib/testing/entity_resolver_fake.h"

namespace maxwell {
namespace {

// Returns pair<true, ..> if key found, else <false, nullptr>.
std::pair<bool, modular::CreateChainPropertyInfo*> GetProperty(
    modular::CreateChainInfoPtr& chain_info,
    const std::string& key) {
  for (auto& it : *chain_info->property_info) {
    if (it->key == key) {
      return std::make_pair<bool, modular::CreateChainPropertyInfo*>(
          true, it->value.get());
    }
  }

  return std::make_pair<bool, modular::CreateChainPropertyInfo*>(false,
                                                                 nullptr);
}

class QueryBuilder {
 public:
  QueryBuilder() : query(modular::ResolverQuery::New()) {}
  QueryBuilder(std::string verb) : query(modular::ResolverQuery::New()) {
    SetVerb(verb);
  }

  modular::ResolverQueryPtr build() { return std::move(query); }

  QueryBuilder& SetUrl(std::string url) {
    query->url = url;
    return *this;
  }

  QueryBuilder& SetVerb(std::string verb) {
    query->verb = verb;
    return *this;
  }

  // Creates a noun that's just Entity types.
  QueryBuilder& AddNounTypes(std::string name, std::vector<std::string> types) {
    auto noun = modular::ResolverNounConstraint::New();
    auto types_array = f1dl::VectorPtr<f1dl::StringPtr>::From(types);
    noun->set_entity_type(std::move(types_array));
    auto resolver_noun_constraint_entry =
        modular::ResolverNounConstraintEntry::New();
    resolver_noun_constraint_entry->key = name;
    resolver_noun_constraint_entry->constraint = std::move(noun);
    query->noun_constraints.push_back(
        std::move(resolver_noun_constraint_entry));
    return *this;
  }

  QueryBuilder& AddEntityNoun(std::string name, std::string entity_reference) {
    auto noun = modular::ResolverNounConstraint::New();
    noun->set_entity_reference(entity_reference);
    auto resolver_noun_constraint_entry =
        modular::ResolverNounConstraintEntry::New();
    resolver_noun_constraint_entry->key = name;
    resolver_noun_constraint_entry->constraint = std::move(noun);
    query->noun_constraints.push_back(
        std::move(resolver_noun_constraint_entry));
    return *this;
  }

  // Creates a noun that's made of JSON content.
  QueryBuilder& AddJsonNoun(std::string name, std::string json) {
    auto noun = modular::ResolverNounConstraint::New();
    noun->set_json(json);
    auto resolver_noun_constraint_entry =
        modular::ResolverNounConstraintEntry::New();
    resolver_noun_constraint_entry->key = name;
    resolver_noun_constraint_entry->constraint = std::move(noun);
    query->noun_constraints.push_back(
        std::move(resolver_noun_constraint_entry));
    return *this;
  }

  // |path_parts| is a pair of { module path array, link name }.
  QueryBuilder& AddLinkInfoNounWithContent(
      std::string name,
      std::pair<std::vector<std::string>, std::string> path_parts,
      std::string entity_reference) {
    auto link_path = modular::LinkPath::New();
    link_path->module_path = f1dl::VectorPtr<f1dl::StringPtr>::From(path_parts.first);
    link_path->link_name = path_parts.second;

    auto link_info = modular::ResolverLinkInfo::New();
    link_info->path = std::move(link_path);
    link_info->content_snapshot =
        modular::EntityReferenceToJson(entity_reference);

    auto noun = modular::ResolverNounConstraint::New();
    noun->set_link_info(std::move(link_info));
    auto resolver_noun_constraint_entry =
        modular::ResolverNounConstraintEntry::New();
    resolver_noun_constraint_entry->key = name;
    resolver_noun_constraint_entry->constraint = std::move(noun);
    query->noun_constraints.push_back(
        std::move(resolver_noun_constraint_entry));
    return *this;
  }

  // |path_parts| is a pair of { module path array, link name }.
  QueryBuilder& AddLinkInfoNounWithTypeConstraints(
      std::string name,
      std::pair<std::vector<std::string>, std::string> path_parts,
      std::vector<std::string> allowed_types) {
    auto link_path = modular::LinkPath::New();
    link_path->module_path = f1dl::VectorPtr<f1dl::StringPtr>::From(path_parts.first);
    link_path->link_name = path_parts.second;

    auto link_info = modular::ResolverLinkInfo::New();
    link_info->path = std::move(link_path);
    link_info->allowed_types = modular::LinkAllowedTypes::New();
    link_info->allowed_types->allowed_entity_types =
        f1dl::VectorPtr<f1dl::StringPtr>::From(allowed_types);

    auto noun = modular::ResolverNounConstraint::New();
    noun->set_link_info(std::move(link_info));
    auto resolver_noun_constraint_entry =
        modular::ResolverNounConstraintEntry::New();
    resolver_noun_constraint_entry->key = name;
    resolver_noun_constraint_entry->constraint = std::move(noun);
    query->noun_constraints.push_back(
        std::move(resolver_noun_constraint_entry));
    return *this;
  }

 private:
  modular::ResolverQueryPtr query;
};

class TestManifestSource : public modular::ModuleManifestSource {
 public:
  IdleFn idle;
  NewEntryFn add;
  RemovedEntryFn remove;

 private:
  void Watch(fxl::RefPtr<fxl::TaskRunner> task_runner,
             IdleFn idle_fn,
             NewEntryFn new_fn,
             RemovedEntryFn removed_fn) override {
    idle = std::move(idle_fn);
    add = std::move(new_fn);
    remove = std::move(removed_fn);
  }
};

class ModuleResolverImplTest : public gtest::TestWithMessageLoop {
 protected:
  void ResetResolver() {
    modular::EntityResolverPtr entity_resolver_ptr;
    entity_resolver_.reset(new modular::EntityResolverFake());
    entity_resolver_->Connect(entity_resolver_ptr.NewRequest());
    // TODO: |impl_| will fail to resolve any queries whose nouns are entity
    // references.
    impl_.reset(new ModuleResolverImpl(std::move(entity_resolver_ptr)));
    for (auto entry : test_sources_) {
      impl_->AddSource(
          entry.first,
          std::unique_ptr<modular::ModuleManifestSource>(entry.second));
    }
    test_sources_.clear();
    impl_->Connect(resolver_.NewRequest());
  }

  TestManifestSource* AddSource(std::string name) {
    // Ownership given to |impl_| in ResetResolver().
    auto ptr = new TestManifestSource;
    test_sources_.emplace(name, ptr);
    return ptr;
  }

  f1dl::StringPtr AddEntity(std::map<std::string, std::string> entity_data) {
    return entity_resolver_->AddEntity(std::move(entity_data));
  }

  void FindModules(modular::ResolverQueryPtr query) {
    auto scoring_info = modular::ResolverScoringInfo::New();

    bool got_response = false;
    resolver_->FindModules(
        std::move(query), nullptr /* scoring_info */,
        [this, &got_response](const modular::FindModulesResultPtr& result) {
          got_response = true;
          result_ = result.Clone();
        });
    ASSERT_TRUE(
        RunLoopUntilWithTimeout([&got_response] { return got_response; }));
  }

  const f1dl::VectorPtr<modular::ModuleResolverResultPtr>& results() const {
    return result_->modules;
  }

  std::unique_ptr<ModuleResolverImpl> impl_;
  std::unique_ptr<modular::EntityResolverFake> entity_resolver_;

  std::map<std::string, modular::ModuleManifestSource*> test_sources_;
  modular::ModuleResolverPtr resolver_;

  modular::FindModulesResultPtr result_;
};

TEST_F(ModuleResolverImplTest, Null) {
  auto source = AddSource("test");
  ResetResolver();

  auto entry = modular::ModuleManifest::New();
  entry->binary = "id1";
  entry->verb = "verb wont match";
  source->add("1", std::move(entry));
  source->idle();

  auto query = QueryBuilder("no matchy!").build();

  FindModules(std::move(query));

  // The Resolver returns an empty candidate list
  ASSERT_EQ(0lu, results()->size());
}

TEST_F(ModuleResolverImplTest, ExplicitUrl) {
  auto source = AddSource("test");
  ResetResolver();

  auto entry = modular::ModuleManifest::New();
  entry->binary = "no see this";
  entry->verb = "verb";
  source->add("1", std::move(entry));
  source->idle();

  auto query = QueryBuilder("verb").SetUrl("another URL").build();

  FindModules(std::move(query));

  // Even though the query has a verb set that matches another Module, we
  // ignore it and prefer only the one URL. It's OK that the referenced Module
  // ("another URL") doesn't have a manifest entry.
  ASSERT_EQ(1u, results()->size());
  EXPECT_EQ("another URL", results()->at(0)->module_id);
}

TEST_F(ModuleResolverImplTest, SimpleVerb) {
  // Also add Modules from multiple different sources.
  auto source1 = AddSource("test1");
  auto source2 = AddSource("test2");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    source1->add("1", std::move(entry));
  }
  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module2";
    entry->verb = "com.google.fuchsia.navigate.v1";
    source2->add("1", std::move(entry));
  }
  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module3";
    entry->verb = "com.google.fuchsia.exist.vinfinity";
    source1->add("2", std::move(entry));
  }

  source1->idle();

  auto query = QueryBuilder("com.google.fuchsia.navigate.v1").build();
  // This is mostly the contents of the FindModules() convenience function
  // above.  It's copied here so that we can call source2->idle() before
  // RunLoopUntilWithTimeout() for this case only.
  auto scoring_info = modular::ResolverScoringInfo::New();
  bool got_response = false;
  resolver_->FindModules(
      std::move(query), nullptr /* scoring_info */,
      [this, &got_response](const modular::FindModulesResultPtr& result) {
        got_response = true;
        result_ = result.Clone();
      });
  // Waiting until here to set |source2| as idle shows that FindModules() is
  // effectively delayed until all sources have indicated idle ("module2" is in
  // |source2|).
  source2->idle();
  ASSERT_TRUE(
      RunLoopUntilWithTimeout([&got_response] { return got_response; }));

  ASSERT_EQ(2lu, results()->size());
  EXPECT_EQ("module1", results()->at(0)->module_id);
  EXPECT_EQ("module2", results()->at(1)->module_id);

  // Remove the entries and we should see no more results. Our
  // TestManifestSource implementation above doesn't send its tasks to the
  // task_runner so we don't have to wait.
  source1->remove("1");
  source2->remove("1");

  FindModules(QueryBuilder("com.google.fuchsia.navigate.v1").build());
  ASSERT_EQ(0lu, results()->size());
}

TEST_F(ModuleResolverImplTest, SimpleNounTypes) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun1 = modular::NounConstraint::New();
    noun1->name = "start";
    noun1->types.reset({"foo", "bar"});
    auto noun2 = modular::NounConstraint::New();
    noun2->name = "destination";
    noun2->types.reset({"baz"});
    entry->noun_constraints.push_back(std::move(noun1));
    entry->noun_constraints.push_back(std::move(noun2));
    source->add("1", std::move(entry));
  }
  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module2";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun1 = modular::NounConstraint::New();
    noun1->name = "start";
    noun1->types.reset({"frob"});
    auto noun2 = modular::NounConstraint::New();
    noun2->name = "destination";
    noun2->types.reset({"froozle"});
    entry->noun_constraints.push_back(std::move(noun1));
    entry->noun_constraints.push_back(std::move(noun2));
    source->add("2", std::move(entry));
  }
  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module3";
    entry->verb = "com.google.fuchsia.exist.vinfinity";
    auto noun = modular::NounConstraint::New();
    noun->name = "with";
    noun->types.reset({"compantionCube"});
    entry->noun_constraints.push_back(std::move(noun));
    source->add("3", std::move(entry));
  }
  source->idle();

  // Either 'foo' or 'tangoTown' would be acceptible types. Only 'foo' will
  // actually match.
  auto query = QueryBuilder("com.google.fuchsia.navigate.v1")
                   .AddNounTypes("start", {"foo", "tangoTown"})
                   .build();
  FindModules(std::move(query));
  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("module1", results()->at(0)->module_id);

  // This one will match one of the two noun constraints on module1, but not
  // both, so no match at all is expected.
  query = QueryBuilder("com.google.fuchsia.navigate.v1")
              .AddNounTypes("start", {"foo", "tangoTown"})
              .AddNounTypes("destination", {"notbaz"})
              .build();
  FindModules(std::move(query));
  ASSERT_EQ(0lu, results()->size());

  // Given an entity of type "frob", find a module with verb
  // com.google.fuchsia.navigate.v1.
  f1dl::StringPtr location_entity = AddEntity({{"frob", ""}});
  ASSERT_TRUE(!location_entity->empty());

  query = QueryBuilder("com.google.fuchsia.navigate.v1")
              .AddEntityNoun("start", location_entity)
              .build();
  FindModules(std::move(query));
  ASSERT_EQ(1u, results()->size());
  auto& res = results()->at(0);
  EXPECT_EQ("module2", res->module_id);

  // Verify that |create_chain_info| is set up correctly.
  ASSERT_TRUE(res->create_chain_info);
  EXPECT_EQ(1lu, res->create_chain_info->property_info->size());
  ASSERT_TRUE(GetProperty(res->create_chain_info, "start").first);
  auto start = GetProperty(res->create_chain_info, "start").second;
  ASSERT_TRUE(start->is_create_link());
  EXPECT_EQ(modular::EntityReferenceToJson(location_entity),
            start->get_create_link()->initial_data);
  // TODO(thatguy): Populate |allowed_types| correctly.
  EXPECT_FALSE(start->get_create_link()->allowed_types);
}

TEST_F(ModuleResolverImplTest, SimpleJsonNouns) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun1 = modular::NounConstraint::New();
    noun1->name = "start";
    noun1->types.reset({"foo", "bar"});
    auto noun2 = modular::NounConstraint::New();
    noun2->name = "destination";
    noun2->types.reset({"baz"});
    entry->noun_constraints.push_back(std::move(noun1));
    entry->noun_constraints.push_back(std::move(noun2));
    source->add("1", std::move(entry));
  }
  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module2";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun1 = modular::NounConstraint::New();
    noun1->name = "start";
    noun1->types.reset({"frob"});
    auto noun2 = modular::NounConstraint::New();
    noun2->name = "destination";
    noun2->types.reset({"froozle"});
    entry->noun_constraints.push_back(std::move(noun1));
    entry->noun_constraints.push_back(std::move(noun2));
    source->add("2", std::move(entry));
  }
  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module3";
    entry->verb = "com.google.fuchsia.exist.vinfinity";
    auto noun = modular::NounConstraint::New();
    noun->name = "with";
    noun->types.reset({"compantionCube"});
    entry->noun_constraints.push_back(std::move(noun));
    source->add("3", std::move(entry));
  }
  source->idle();

  // Same thing as above, but we'll use JSON with embedded type information and
  // should see the same exactly results.
  const auto startJson = R"({
        "@type": [ "foo", "tangoTown" ],
        "thecake": "is a lie"
      })";
  const auto destinationJson = R"({
        "@type": "baz",
        "really": "it is"
      })";
  auto query = QueryBuilder("com.google.fuchsia.navigate.v1")
                   .AddJsonNoun("start", startJson)
                   .AddJsonNoun("destination", destinationJson)
                   .build();
  FindModules(std::move(query));
  ASSERT_EQ(1lu, results()->size());
  auto& res = results()->at(0);
  EXPECT_EQ("module1", res->module_id);

  // Verify that |create_chain_info| is set up correctly.
  ASSERT_TRUE(res->create_chain_info);
  EXPECT_EQ(2lu, res->create_chain_info->property_info->size());
  ASSERT_TRUE(GetProperty(res->create_chain_info, "start").first);
  auto start = GetProperty(res->create_chain_info, "start").second;
  ASSERT_TRUE(start->is_create_link());
  EXPECT_EQ(startJson, start->get_create_link()->initial_data);
  // TODO(thatguy): Populate |allowed_types| correctly.
  EXPECT_FALSE(start->get_create_link()->allowed_types);

  ASSERT_TRUE(GetProperty(res->create_chain_info, "destination").first);
  auto destination = GetProperty(res->create_chain_info, "destination").second;
  ASSERT_TRUE(destination->is_create_link());
  EXPECT_EQ(destinationJson, destination->get_create_link()->initial_data);
  // TODO(thatguy): Populate |allowed_types| correctly.
  EXPECT_FALSE(destination->get_create_link()->allowed_types);
}

TEST_F(ModuleResolverImplTest, LinkInfoNounType) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun1 = modular::NounConstraint::New();
    noun1->name = "start";
    noun1->types.reset({"foo"});
    auto noun2 = modular::NounConstraint::New();
    noun2->name = "destination";
    noun2->types.reset({"baz"});
    entry->noun_constraints.push_back(std::move(noun1));
    entry->noun_constraints.push_back(std::move(noun2));
    source->add("1", std::move(entry));
  }
  source->idle();

  // First try matching "module1" for a query that describes a Link that
  // already allows "foo" in the Link.
  auto query = QueryBuilder("com.google.fuchsia.navigate.v1")
                   .AddLinkInfoNounWithTypeConstraints(
                       "start", {{"a", "b"}, "c"}, {"foo"})
                   .build();
  FindModules(std::move(query));
  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("module1", results()->at(0)->module_id);

  // Same thing should happen if there aren't any allowed types, but the Link's
  // content encodes an Entity reference.
  f1dl::StringPtr entity_reference = AddEntity({{"foo", ""}});
  query = QueryBuilder("com.google.fuchsia.navigate.v1")
              .AddLinkInfoNounWithContent("start", {{"a", "b"}, "c"},
                                          entity_reference)
              .build();
  FindModules(std::move(query));
  ASSERT_EQ(1lu, results()->size());
  auto& res = results()->at(0);
  EXPECT_EQ("module1", res->module_id);

  // Verify that |create_chain_info| is set up correctly.
  ASSERT_TRUE(res->create_chain_info);
  EXPECT_EQ(1lu, res->create_chain_info->property_info->size());
  ASSERT_TRUE(GetProperty(res->create_chain_info, "start").first);
  auto start = GetProperty(res->create_chain_info, "start").second;
  ASSERT_TRUE(start->is_link_path());
  EXPECT_EQ("a", start->get_link_path()->module_path->at(0));
  EXPECT_EQ("b", start->get_link_path()->module_path->at(1));
  EXPECT_EQ("c", start->get_link_path()->link_name);
}

TEST_F(ModuleResolverImplTest, ReAddExistingEntries) {
  // Add the same entry twice, to simulate what could happen during a network
  // reconnect, and show that the Module is still available.
  auto source = AddSource("test1");
  ResetResolver();

  auto entry = modular::ModuleManifest::New();
  entry->binary = "id1";
  entry->verb = "verb1";

  source->add("1", entry.Clone());
  source->idle();
  FindModules(QueryBuilder("verb1").build());
  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("id1", results()->at(0)->module_id);

  source->add("1", entry.Clone());
  FindModules(QueryBuilder("verb1").build());
  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("id1", results()->at(0)->module_id);
}

// Tests that a query that does not contain a verb or a URL matches a noun with
// the correct types.
TEST_F(ModuleResolverImplTest, MatchingNounWithNoVerbOrUrl) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun = modular::NounConstraint::New();
    noun->name = "start";
    noun->types.reset({"foo", "missed"});
    entry->noun_constraints.push_back(std::move(noun));
    source->add("1", std::move(entry));
  }

  source->idle();

  auto query = QueryBuilder().AddNounTypes("start", {"foo", "bar"}).build();

  FindModules(std::move(query));

  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("module1", results()->at(0)->module_id);
}

// Tests that a query that does not contain a verb or a URL matches when the
// noun types do, even if the noun name does not.
TEST_F(ModuleResolverImplTest, CorrectNounTypeWithNoVerbOrUrl) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun = modular::NounConstraint::New();
    noun->name = "end";
    noun->types.reset({"foo", "baz"});
    entry->noun_constraints.push_back(std::move(noun));
    source->add("1", std::move(entry));
  }

  source->idle();

  auto query = QueryBuilder().AddNounTypes("start", {"foo", "bar"}).build();

  FindModules(std::move(query));

  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("module1", results()->at(0)->module_id);
}

// Tests that a query that does not contain a verb or a URL returns results for
// multiple matching entries.
TEST_F(ModuleResolverImplTest, CorrectNounTypeWithNoVerbOrUrlMultipleMatches) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun = modular::NounConstraint::New();
    noun->name = "end";
    noun->types.reset({"foo", "baz"});
    entry->noun_constraints.push_back(std::move(noun));
    source->add("1", std::move(entry));
  }
  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module2";
    entry->verb = "com.google.fuchsia.navigate.v2";
    auto noun = modular::NounConstraint::New();
    noun->name = "end";
    noun->types.reset({"foo", "baz"});
    entry->noun_constraints.push_back(std::move(noun));
    source->add("2", std::move(entry));
  }

  source->idle();

  auto query = QueryBuilder().AddNounTypes("start", {"foo", "bar"}).build();

  FindModules(std::move(query));

  ASSERT_EQ(2lu, results()->size());
  EXPECT_EQ("module1", results()->at(0)->module_id);
  EXPECT_EQ("module2", results()->at(1)->module_id);
}

// Tests that a query that does not contain a verb or a URL does not match when
// the noun types don't match.
TEST_F(ModuleResolverImplTest, IncorrectNounTypeWithNoVerbOrUrl) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun = modular::NounConstraint::New();
    noun->name = "start";
    noun->types.reset({"not", "correct"});
    entry->noun_constraints.push_back(std::move(noun));
    source->add("1", std::move(entry));
  }

  source->idle();

  auto query = QueryBuilder().AddNounTypes("start", {"foo", "bar"}).build();

  FindModules(std::move(query));

  EXPECT_EQ(0lu, results()->size());
}

// Tests that a query without a verb or url, that contains more nouns than the
// potential result, still returns that result.
TEST_F(ModuleResolverImplTest, QueryWithMoreNounsThanEntry) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun = modular::NounConstraint::New();
    noun->name = "start";
    noun->types.reset({"gps"});
    entry->noun_constraints.push_back(std::move(noun));
    source->add("1", std::move(entry));
  }

  source->idle();

  auto query = QueryBuilder()
                   .AddNounTypes("start", {"gps", "bar"})
                   .AddNounTypes("end", {"foo", "bar"})
                   .build();

  FindModules(std::move(query));

  EXPECT_EQ(1lu, results()->size());
}

// Tests that for a query with multiple nouns, each noun gets assigned to the
// correct module parameters.
TEST_F(ModuleResolverImplTest, QueryWithoutVerbAndMultipleNouns) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun1 = modular::NounConstraint::New();
    noun1->name = "start";
    noun1->types.reset({"gps"});
    entry->noun_constraints.push_back(std::move(noun1));
    auto noun2 = modular::NounConstraint::New();
    noun2->name = "end";
    noun2->types.reset({"not_gps"});
    entry->noun_constraints.push_back(std::move(noun2));
    source->add("1", std::move(entry));
  }

  source->idle();

  f1dl::StringPtr start_entity = AddEntity({{"gps", "gps data"}});
  ASSERT_TRUE(!start_entity->empty());

  f1dl::StringPtr end_entity = AddEntity({{"not_gps", "not gps data"}});
  ASSERT_TRUE(!end_entity->empty());

  auto query = QueryBuilder()
                   .AddEntityNoun("noun1", start_entity)
                   .AddEntityNoun("noun2", end_entity)
                   .build();

  FindModules(std::move(query));

  ASSERT_EQ(1lu, results()->size());
  auto result = results()->at(0).get();

  EXPECT_EQ(GetProperty(result->create_chain_info, "start")
                .second->get_create_link()
                ->initial_data,
            modular::EntityReferenceToJson(start_entity));
  EXPECT_EQ(GetProperty(result->create_chain_info, "end")
                .second->get_create_link()
                ->initial_data,
            modular::EntityReferenceToJson(end_entity));
}

// Tests that when there are multiple valid mappings of query noun types to
// entities, all such combinations are returned.
TEST_F(ModuleResolverImplTest, QueryWithoutVerbAndTwoNounsOfSameType) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun1 = modular::NounConstraint::New();
    noun1->name = "start";
    noun1->types.reset({"gps"});
    entry->noun_constraints.push_back(std::move(noun1));
    auto noun2 = modular::NounConstraint::New();
    noun2->name = "end";
    noun2->types.reset({"gps"});
    entry->noun_constraints.push_back(std::move(noun2));
    source->add("1", std::move(entry));
  }

  source->idle();

  f1dl::StringPtr start_entity = AddEntity({{"gps", "gps data one"}});
  ASSERT_TRUE(!start_entity->empty());

  f1dl::StringPtr end_entity = AddEntity({{"gps", "gps data two"}});
  ASSERT_TRUE(!end_entity->empty());

  auto query = QueryBuilder()
                   .AddEntityNoun("noun1", start_entity)
                   .AddEntityNoun("noun2", end_entity)
                   .build();

  FindModules(std::move(query));

  EXPECT_EQ(2lu, results()->size());

  bool found_first_mapping = false;
  bool found_second_mapping = false;

  for (const auto& result : *results()) {
    bool start_mapped_to_start =
        GetProperty(result->create_chain_info, "start")
            .second->get_create_link()
            ->initial_data == modular::EntityReferenceToJson(start_entity);
    bool start_mapped_to_end =
        GetProperty(result->create_chain_info, "start")
            .second->get_create_link()
            ->initial_data == modular::EntityReferenceToJson(end_entity);
    bool end_mapped_to_end =
        GetProperty(result->create_chain_info, "end")
            .second->get_create_link()
            ->initial_data == modular::EntityReferenceToJson(end_entity);
    bool end_mapped_to_start =
        GetProperty(result->create_chain_info, "end")
            .second->get_create_link()
            ->initial_data == modular::EntityReferenceToJson(start_entity);
    found_first_mapping |= start_mapped_to_start && end_mapped_to_end;
    found_second_mapping |= start_mapped_to_end && end_mapped_to_start;
  }

  EXPECT_TRUE(found_first_mapping);
  EXPECT_TRUE(found_second_mapping);
}

// Tests that a query with three nouns of the same type matches an entry with
// three expected nouns in 6 different ways.
TEST_F(ModuleResolverImplTest, QueryWithoutVerbAndThreeNounsOfSameType) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun1 = modular::NounConstraint::New();
    noun1->name = "start";
    noun1->types.reset({"gps"});
    entry->noun_constraints.push_back(std::move(noun1));
    auto noun2 = modular::NounConstraint::New();
    noun2->name = "end";
    noun2->types.reset({"gps"});
    entry->noun_constraints.push_back(std::move(noun2));
    auto noun3 = modular::NounConstraint::New();
    noun3->name = "middle";
    noun3->types.reset({"gps"});
    entry->noun_constraints.push_back(std::move(noun3));
    source->add("1", std::move(entry));
  }

  source->idle();

  f1dl::StringPtr start_entity = AddEntity({{"gps", "gps data one"}});
  ASSERT_TRUE(!start_entity->empty());

  f1dl::StringPtr end_entity = AddEntity({{"gps", "gps data two"}});
  ASSERT_TRUE(!end_entity->empty());

  f1dl::StringPtr middle_entity = AddEntity({{"gps", "gps data three"}});
  ASSERT_TRUE(!middle_entity->empty());

  auto query = QueryBuilder()
                   .AddEntityNoun("noun1", start_entity)
                   .AddEntityNoun("noun2", end_entity)
                   .AddEntityNoun("noun3", middle_entity)
                   .build();

  FindModules(std::move(query));

  EXPECT_EQ(6lu, results()->size());
}

// Tests that a query with three nouns of the same type matches an entry with
// two expected nouns in 6 different ways.
TEST_F(ModuleResolverImplTest,
       QueryWithoutVerbAndDifferentNumberOfNounsInQueryVsEntry) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun1 = modular::NounConstraint::New();
    noun1->name = "start";
    noun1->types.reset({"gps"});
    entry->noun_constraints.push_back(std::move(noun1));
    auto noun2 = modular::NounConstraint::New();
    noun2->name = "end";
    noun2->types.reset({"gps"});
    entry->noun_constraints.push_back(std::move(noun2));
    source->add("1", std::move(entry));
  }

  source->idle();

  f1dl::StringPtr start_entity = AddEntity({{"gps", "gps data one"}});
  ASSERT_TRUE(!start_entity->empty());

  f1dl::StringPtr end_entity = AddEntity({{"gps", "gps data two"}});
  ASSERT_TRUE(!end_entity->empty());

  f1dl::StringPtr middle_entity = AddEntity({{"gps", "gps data three"}});
  ASSERT_TRUE(!middle_entity->empty());

  auto query = QueryBuilder()
                   .AddEntityNoun("noun1", start_entity)
                   .AddEntityNoun("noun2", end_entity)
                   .AddEntityNoun("noun3", middle_entity)
                   .build();

  FindModules(std::move(query));

  EXPECT_EQ(6lu, results()->size());
}

// Tests that a query without a verb does not match a module that requires a
// proper superset of the query nouns.
TEST_F(ModuleResolverImplTest,
       QueryWithoutVerbWithEntryContainingProperSuperset) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun1 = modular::NounConstraint::New();
    noun1->name = "start";
    noun1->types.reset({"gps"});
    entry->noun_constraints.push_back(std::move(noun1));
    auto noun2 = modular::NounConstraint::New();
    noun2->name = "end";
    noun2->types.reset({"gps"});
    entry->noun_constraints.push_back(std::move(noun2));
    source->add("1", std::move(entry));
  }

  source->idle();

  f1dl::StringPtr start_entity = AddEntity({{"gps", "gps data"}});
  ASSERT_TRUE(!start_entity->empty());

  // The query only contains an Entity for "noun1", but the module manifest
  // requires two nouns of type "gps."
  auto query = QueryBuilder().AddEntityNoun("noun1", start_entity).build();

  FindModules(std::move(query));

  EXPECT_EQ(0lu, results()->size());
}

// Tests that a query without a verb does not match an entry where the noun
// types are incompatible.
TEST_F(ModuleResolverImplTest, QueryWithoutVerbIncompatibleNounTypes) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun = modular::NounConstraint::New();
    noun->name = "start";
    noun->types.reset({"gps"});
    entry->noun_constraints.push_back(std::move(noun));
    source->add("1", std::move(entry));
  }

  source->idle();

  f1dl::StringPtr start_entity = AddEntity({{"not_gps", "not gps data"}});
  ASSERT_TRUE(!start_entity->empty());

  // The query only contains an Entity for "noun1", but the module manifest
  // requires two nouns of type "gps."
  auto query = QueryBuilder().AddEntityNoun("noun1", start_entity).build();

  FindModules(std::move(query));

  EXPECT_EQ(0lu, results()->size());
}

// Tests that a query with a verb requires noun name and type to match (i.e.
// does not behave like verb-less matching where the noun names are
// disregarded).
TEST_F(ModuleResolverImplTest, QueryWithVerbMatchesBothNounNamesAndTypes) {
  auto source = AddSource("test");
  ResetResolver();

  {
    auto entry = modular::ModuleManifest::New();
    entry->binary = "module1";
    entry->verb = "com.google.fuchsia.navigate.v1";
    auto noun = modular::NounConstraint::New();
    noun->name = "end";
    noun->types.reset({"foo", "bar"});
    entry->noun_constraints.push_back(std::move(noun));
    source->add("1", std::move(entry));
  }

  source->idle();

  auto query = QueryBuilder("com.google.fuchsia.navigate.v1")
                   .AddNounTypes("start", {"foo", "baz"})
                   .build();

  FindModules(std::move(query));

  EXPECT_EQ(0lu, results()->size());
}

}  // namespace
}  // namespace maxwell
