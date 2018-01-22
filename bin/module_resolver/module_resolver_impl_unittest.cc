// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/module_resolver/module_resolver_impl.h"
#include "gtest/gtest.h"
#include "lib/entity/cpp/json.h"
#include "lib/fxl/files/file.h"
#include "lib/module_resolver/cpp/formatting.h"
#include "peridot/lib/gtest/test_with_message_loop.h"
#include "peridot/lib/testing/entity_resolver_fake.h"

namespace maxwell {
namespace {

class QueryBuilder {
 public:
  QueryBuilder() : query(modular::ResolverQuery::New()) {
    query->noun_constraints.mark_non_null();
  }
  QueryBuilder(std::string verb) : query(modular::ResolverQuery::New()) {
    query->noun_constraints.mark_non_null();
    SetVerb(verb);
  }

  modular::ResolverQueryPtr build() { return std::move(query); }

  QueryBuilder& SetVerb(std::string verb) {
    query->verb = verb;
    return *this;
  }

  // Creates a noun that's just Entity types.
  QueryBuilder& AddNounTypes(std::string name, std::vector<std::string> types) {
    auto noun = modular::ResolverNounConstraint::New();
    auto types_array = fidl::Array<fidl::String>::From(types);
    noun->set_entity_type(std::move(types_array));
    query->noun_constraints.insert(name, std::move(noun));
    return *this;
  }

  QueryBuilder& AddEntityNoun(std::string name, std::string entity_reference) {
    auto noun = modular::ResolverNounConstraint::New();
    noun->set_entity_reference(entity_reference);
    query->noun_constraints.insert(name, std::move(noun));
    return *this;
  }

  // Creates a noun that's made of JSON content.
  QueryBuilder& AddJsonNoun(std::string name, std::string json) {
    auto noun = modular::ResolverNounConstraint::New();
    noun->set_json(json);
    query->noun_constraints.insert(name, std::move(noun));
    return *this;
  }

  // |path_parts| is a pair of { module path array, link name }.
  QueryBuilder& AddLinkInfoNounWithContent(
      std::string name,
      std::pair<std::vector<std::string>, std::string> path_parts,
      std::string entity_reference) {
    auto link_path = modular::LinkPath::New();
    link_path->module_path = fidl::Array<fidl::String>::From(path_parts.first);
    link_path->link_name = path_parts.second;

    auto link_info = modular::ResolverLinkInfo::New();
    link_info->path = std::move(link_path);
    link_info->content_snapshot =
        modular::EntityReferenceToJson(entity_reference);

    auto noun = modular::ResolverNounConstraint::New();
    noun->set_link_info(std::move(link_info));
    query->noun_constraints.insert(name, std::move(noun));
    return *this;
  }

  // |path_parts| is a pair of { module path array, link name }.
  QueryBuilder& AddLinkInfoNounWithTypeConstraints(
      std::string name,
      std::pair<std::vector<std::string>, std::string> path_parts,
      std::vector<std::string> allowed_types) {
    auto link_path = modular::LinkPath::New();
    link_path->module_path = fidl::Array<fidl::String>::From(path_parts.first);
    link_path->link_name = path_parts.second;

    auto link_info = modular::ResolverLinkInfo::New();
    link_info->path = std::move(link_path);
    link_info->allowed_types = modular::LinkAllowedTypes::New();
    link_info->allowed_types->allowed_entity_types =
        fidl::Array<fidl::String>::From(allowed_types);

    auto noun = modular::ResolverNounConstraint::New();
    noun->set_link_info(std::move(link_info));
    query->noun_constraints.insert(name, std::move(noun));
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

  fidl::String AddEntity(std::map<std::string, std::string> entity_data) {
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
    ASSERT_TRUE(RunLoopUntil([&got_response] { return got_response; }));
  }

  const fidl::Array<modular::ModuleResolverResultPtr>& results() const {
    return result_->modules;
  }

  std::unique_ptr<ModuleResolverImpl> impl_;
  std::unique_ptr<modular::EntityResolverFake> entity_resolver_;

  std::map<std::string, modular::ModuleManifestSource*> test_sources_;
  modular::ModuleResolverPtr resolver_;

  modular::FindModulesResultPtr result_;
};

#define ASSERT_DEFAULT_RESULT(results) \
  ASSERT_EQ(1lu, results.size());      \
  EXPECT_EQ("resolution_failed", results[0]->module_id);

TEST_F(ModuleResolverImplTest, Null) {
  auto source = AddSource("test");
  ResetResolver();

  modular::ModuleManifestSource::Entry entry;
  entry.binary = "id1";
  entry.verb = "verb wont match";
  source->add("1", entry);
  source->idle();

  auto query = QueryBuilder("no matchy!").build();

  FindModules(std::move(query));

  // The Resolver currently always returns a fallback Module.
  ASSERT_DEFAULT_RESULT(results());
}

TEST_F(ModuleResolverImplTest, SimpleVerb) {
  // Also add Modules from multiple different sources.
  auto source1 = AddSource("test1");
  auto source2 = AddSource("test2");
  ResetResolver();

  {
    modular::ModuleManifestSource::Entry entry;
    entry.binary = "module1";
    entry.verb = "com.google.fuchsia.navigate.v1";
    source1->add("1", entry);
  }
  {
    modular::ModuleManifestSource::Entry entry;
    entry.binary = "module2";
    entry.verb = "com.google.fuchsia.navigate.v1";
    source2->add("1", entry);
  }
  {
    modular::ModuleManifestSource::Entry entry;
    entry.binary = "module3";
    entry.verb = "com.google.fuchsia.exist.vinfinity";
    source1->add("2", entry);
  }

  source1->idle();

  auto query = QueryBuilder("com.google.fuchsia.navigate.v1").build();
  // This is mostly the contents of the FindModules() convenience function
  // above.  It's copied here so that we can call source2->idle() before
  // RunLoopUntil() for this case only.
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
  ASSERT_TRUE(RunLoopUntil([&got_response] { return got_response; }));

  ASSERT_EQ(2lu, results().size());
  EXPECT_EQ("module1", results()[0]->module_id);
  EXPECT_EQ("module2", results()[1]->module_id);

  // Remove the entries and we should see no more results. Our
  // TestManifestSource implementation above doesn't send its tasks to the
  // task_runner so we don't have to wait.
  source1->remove("1");
  source2->remove("1");

  FindModules(QueryBuilder("com.google.fuchsia.navigate.v1").build());
  ASSERT_DEFAULT_RESULT(results());
}

TEST_F(ModuleResolverImplTest, SimpleNounTypes) {
  auto source = AddSource("test");
  ResetResolver();

  {
    modular::ModuleManifestSource::Entry entry;
    entry.binary = "module1";
    entry.verb = "com.google.fuchsia.navigate.v1";
    entry.noun_constraints = {{"start", {"foo", "bar"}},
                              {"destination", {"baz"}}};
    source->add("1", entry);
  }
  {
    modular::ModuleManifestSource::Entry entry;
    entry.binary = "module2";
    entry.verb = "com.google.fuchsia.navigate.v1";
    entry.noun_constraints = {{"start", {"frob"}},
                              {"destination", {"froozle"}}};
    source->add("2", entry);
  }
  {
    modular::ModuleManifestSource::Entry entry;
    entry.binary = "module3";
    entry.verb = "com.google.fuchsia.exist.vinfinity";
    entry.noun_constraints = {{"with", {"compantionCube"}}};
    source->add("3", entry);
  }
  source->idle();

  // Either 'foo' or 'tangoTown' would be acceptible types. Only 'foo' will
  // actually match.
  auto query = QueryBuilder("com.google.fuchsia.navigate.v1")
                   .AddNounTypes("start", {"foo", "tangoTown"})
                   .build();
  FindModules(std::move(query));
  ASSERT_EQ(1lu, results().size());
  EXPECT_EQ("module1", results()[0]->module_id);

  // This one will match one of the two noun constraints on module1, but not
  // both, so no match at all is expected.
  query = QueryBuilder("com.google.fuchsia.navigate.v1")
              .AddNounTypes("start", {"foo", "tangoTown"})
              .AddNounTypes("destination", {"notbaz"})
              .build();
  FindModules(std::move(query));
  ASSERT_DEFAULT_RESULT(results());

  // Given an entity of type "frob", find a module with verb
  // com.google.fuchsia.navigate.v1.
  fidl::String location_entity = AddEntity({{"frob", ""}});
  ASSERT_TRUE(!location_entity.empty());

  query = QueryBuilder("com.google.fuchsia.navigate.v1")
              .AddEntityNoun("start", location_entity)
              .build();
  FindModules(std::move(query));
  ASSERT_EQ(1u, results().size());
  EXPECT_EQ("module2", results()[0]->module_id);
}

TEST_F(ModuleResolverImplTest, SimpleJsonNouns) {
  auto source = AddSource("test");
  ResetResolver();

  {
    modular::ModuleManifestSource::Entry entry;
    entry.binary = "module1";
    entry.verb = "com.google.fuchsia.navigate.v1";
    entry.noun_constraints = {{"start", {"foo", "bar"}},
                              {"destination", {"baz"}}};
    source->add("1", entry);
  }
  {
    modular::ModuleManifestSource::Entry entry;
    entry.binary = "module2";
    entry.verb = "com.google.fuchsia.navigate.v1";
    entry.noun_constraints = {{"start", {"frob"}},
                              {"destination", {"froozle"}}};
    source->add("2", entry);
  }
  {
    modular::ModuleManifestSource::Entry entry;
    entry.binary = "module3";
    entry.verb = "com.google.fuchsia.exist.vinfinity";
    entry.noun_constraints = {{"with", {"compantionCube"}}};
    source->add("3", entry);
  }
  source->idle();

  // Same thing as above, but we'll use JSON with embedded type information and
  // should see the same exactly results.
  auto query = QueryBuilder("com.google.fuchsia.navigate.v1")
                   .AddJsonNoun("start", R"({
                      "@type": [ "foo", "tangoTown" ],
                      "thecake": "is a lie"
                    })")
                   .AddJsonNoun("destination", R"({
                      "@type": "baz",
                      "really": "it is"
                    })")
                   .build();
  FindModules(std::move(query));
  ASSERT_EQ(1lu, results().size());
  EXPECT_EQ("module1", results()[0]->module_id);
  // TODO(thatguy): Validate that the initial_nouns content is correct.
}

TEST_F(ModuleResolverImplTest, LinkInfoNounType) {
  auto source = AddSource("test");
  ResetResolver();

  {
    modular::ModuleManifestSource::Entry entry;
    entry.binary = "module1";
    entry.verb = "com.google.fuchsia.navigate.v1";
    entry.noun_constraints = {{"start", {"foo"}}, {"destination", {"baz"}}};
    source->add("1", entry);
  }
  source->idle();

  // First try matching "module1" for a query that describes a Link that
  // already allows "foo" in the Link.
  auto query = QueryBuilder("com.google.fuchsia.navigate.v1")
                   .AddLinkInfoNounWithTypeConstraints("start",
                       { {"a", "b"}, "c"}, {"foo"})
                   .build();
  FindModules(std::move(query));
  ASSERT_EQ(1lu, results().size());
  EXPECT_EQ("module1", results()[0]->module_id);

  // Same thing should happen if there aren't any allowed types, but the Link's
  // content encodes an Entity reference.
  fidl::String entity_reference = AddEntity({{"foo", ""}});
  query = QueryBuilder("com.google.fuchsia.navigate.v1")
                   .AddLinkInfoNounWithContent("start",
                       { {"a", "b"}, "c"}, entity_reference)
                   .build();
  FindModules(std::move(query));
  ASSERT_EQ(1lu, results().size());
  EXPECT_EQ("module1", results()[0]->module_id);
}

TEST_F(ModuleResolverImplTest, ReAddExistingEntries) {
  // Add the same entry twice, to simulate what could happen during a network
  // reconnect, and show that the Module is still available.
  auto source = AddSource("test1");
  ResetResolver();

  modular::ModuleManifestSource::Entry entry;
  entry.binary = "id1";
  entry.verb = "verb1";

  source->add("1", entry);
  source->idle();
  FindModules(QueryBuilder("verb1").build());
  ASSERT_EQ(1lu, results().size());
  EXPECT_EQ("id1", results()[0]->module_id);

  source->add("1", entry);
  FindModules(QueryBuilder("verb1").build());
  ASSERT_EQ(1lu, results().size());
  EXPECT_EQ("id1", results()[0]->module_id);
}

}  // namespace
}  // namespace maxwell
