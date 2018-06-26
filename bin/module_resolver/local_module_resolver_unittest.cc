// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/module_resolver/local_module_resolver.h"

#include "lib/entity/cpp/json.h"
#include "lib/fxl/files/file.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/module_resolver/cpp/formatting.h"
#include "peridot/lib/testing/entity_resolver_fake.h"

namespace modular {
namespace {

// Returns pair<true, ..> if key found, else <false, nullptr>.
std::pair<bool, const fuchsia::modular::CreateModuleParameterInfo*> GetProperty(
    const fuchsia::modular::CreateModuleParameterMapInfo& map_info,
    const std::string& key) {
  for (auto& it : *map_info.property_info) {
    if (it.key == key) {
      return std::make_pair<bool,
                            const fuchsia::modular::CreateModuleParameterInfo*>(
          true, &it.value);
    }
  }

  return std::make_pair<bool, fuchsia::modular::CreateModuleParameterInfo*>(
      false, nullptr);
}

class QueryBuilder {
 public:
  QueryBuilder() : query(fuchsia::modular::ResolverQuery()) {}
  QueryBuilder(std::string action) : query(fuchsia::modular::ResolverQuery()) {
    SetAction(action);
  }

  fuchsia::modular::ResolverQuery build() { return std::move(query); }

  QueryBuilder& SetHandler(std::string handler) {
    query.handler = handler;
    return *this;
  }

  QueryBuilder& SetAction(std::string action) {
    query.action = action;
    return *this;
  }

  // Creates a parameter that's just fuchsia::modular::Entity types.
  QueryBuilder& AddParameterTypes(std::string name,
                                  std::vector<std::string> types) {
    fuchsia::modular::ResolverParameterConstraint parameter;
    fidl::VectorPtr<fidl::StringPtr> types_array;
    types_array->reserve(types.size());
    for (auto& type : types) {
      types_array->emplace_back(std::move(type));
    }
    parameter.set_entity_type(std::move(types_array));
    fuchsia::modular::ResolverParameterConstraintEntry
        resolver_parameter_constraint_entry;
    resolver_parameter_constraint_entry.key = name;
    resolver_parameter_constraint_entry.constraint = std::move(parameter);
    query.parameter_constraints.push_back(
        std::move(resolver_parameter_constraint_entry));
    return *this;
  }

  QueryBuilder& AddEntityParameter(std::string name,
                                   std::string entity_reference) {
    fuchsia::modular::ResolverParameterConstraint parameter;
    parameter.set_entity_reference(entity_reference);
    fuchsia::modular::ResolverParameterConstraintEntry
        resolver_parameter_constraint_entry;
    resolver_parameter_constraint_entry.key = name;
    resolver_parameter_constraint_entry.constraint = std::move(parameter);
    query.parameter_constraints.push_back(
        std::move(resolver_parameter_constraint_entry));
    return *this;
  }

  // Creates a parameter that's made of JSON content.
  QueryBuilder& AddJsonParameter(std::string name, std::string json) {
    fuchsia::modular::ResolverParameterConstraint parameter;
    parameter.set_json(json);
    fuchsia::modular::ResolverParameterConstraintEntry
        resolver_parameter_constraint_entry;
    resolver_parameter_constraint_entry.key = name;
    resolver_parameter_constraint_entry.constraint = std::move(parameter);
    query.parameter_constraints.push_back(
        std::move(resolver_parameter_constraint_entry));
    return *this;
  }

  // |path_parts| is a pair of { module path array, link name }.
  QueryBuilder& AddLinkInfoParameterWithContent(
      std::string name,
      std::pair<std::vector<std::string>, std::string> path_parts,
      std::string entity_reference) {
    fuchsia::modular::LinkPath link_path;
    link_path.module_path->reserve(path_parts.first.size());
    for (auto& part : path_parts.first) {
      link_path.module_path->emplace_back(std::move(part));
    }
    link_path.link_name = path_parts.second;

    fuchsia::modular::ResolverLinkInfo link_info;
    link_info.path = std::move(link_path);
    link_info.content_snapshot = EntityReferenceToJson(entity_reference);

    fuchsia::modular::ResolverParameterConstraint parameter;
    parameter.set_link_info(std::move(link_info));
    fuchsia::modular::ResolverParameterConstraintEntry
        resolver_parameter_constraint_entry;
    resolver_parameter_constraint_entry.key = name;
    resolver_parameter_constraint_entry.constraint = std::move(parameter);
    query.parameter_constraints.push_back(
        std::move(resolver_parameter_constraint_entry));
    return *this;
  }

  // |path_parts| is a pair of { module path array, link name }.
  QueryBuilder& AddLinkInfoParameterWithTypeConstraints(
      std::string name,
      std::pair<std::vector<std::string>, std::string> path_parts,
      std::vector<std::string> allowed_types) {
    fuchsia::modular::LinkPath link_path;
    link_path.module_path->reserve(path_parts.first.size());
    for (auto& part : path_parts.first) {
      link_path.module_path->emplace_back(std::move(part));
    }
    link_path.link_name = path_parts.second;

    fuchsia::modular::ResolverLinkInfo link_info;
    link_info.path = std::move(link_path);
    link_info.allowed_types = fuchsia::modular::LinkAllowedTypes::New();
    link_info.allowed_types->allowed_entity_types->reserve(
        allowed_types.size());
    for (auto& type : allowed_types) {
      link_info.allowed_types->allowed_entity_types->emplace_back(
          std::move(type));
    }

    fuchsia::modular::ResolverParameterConstraint parameter;
    parameter.set_link_info(std::move(link_info));
    fuchsia::modular::ResolverParameterConstraintEntry
        resolver_parameter_constraint_entry;
    resolver_parameter_constraint_entry.key = name;
    resolver_parameter_constraint_entry.constraint = std::move(parameter);
    query.parameter_constraints.push_back(
        std::move(resolver_parameter_constraint_entry));
    return *this;
  }

 private:
  fuchsia::modular::ResolverQuery query;
};

class TestManifestSource : public ModuleManifestSource {
 public:
  IdleFn idle;
  NewEntryFn add;
  RemovedEntryFn remove;

 private:
  void Watch(async_t* async, IdleFn idle_fn, NewEntryFn new_fn,
             RemovedEntryFn removed_fn) override {
    idle = std::move(idle_fn);
    add = std::move(new_fn);
    remove = std::move(removed_fn);
  }
};

class LocalModuleResolverTest : public gtest::TestLoopFixture {
 protected:
  void ResetResolver() {
    fuchsia::modular::EntityResolverPtr entity_resolver_ptr;
    entity_resolver_.reset(new EntityResolverFake());
    entity_resolver_->Connect(entity_resolver_ptr.NewRequest());
    // TODO: |impl_| will fail to resolve any queries whose parameters are
    // entity references.
    impl_.reset(new LocalModuleResolver(std::move(entity_resolver_ptr)));
    for (auto entry : test_sources_) {
      impl_->AddSource(entry.first,
                       std::unique_ptr<ModuleManifestSource>(entry.second));
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

  fidl::StringPtr AddEntity(std::map<std::string, std::string> entity_data) {
    return entity_resolver_->AddEntity(std::move(entity_data));
  }

  void FindModules(fuchsia::modular::ResolverQuery query) {
    auto scoring_info = fuchsia::modular::ResolverScoringInfo::New();

    bool got_response = false;
    resolver_->FindModules(
        std::move(query), nullptr /* scoring_info */,
        [this,
         &got_response](const fuchsia::modular::FindModulesResult& result) {
          got_response = true;
          result.Clone(&result_);
        });
        RunLoopUntilIdle();
        ASSERT_TRUE(got_response);
  }

  const fidl::VectorPtr<fuchsia::modular::ModuleResolverResult>& results()
      const {
    return result_.modules;
  }

  std::unique_ptr<LocalModuleResolver> impl_;
  std::unique_ptr<EntityResolverFake> entity_resolver_;

  std::map<std::string, ModuleManifestSource*> test_sources_;
  fuchsia::modular::ModuleResolverPtr resolver_;

  fuchsia::modular::FindModulesResult result_;
};

TEST_F(LocalModuleResolverTest, Null) {
  auto source = AddSource("test");
  ResetResolver();

  fuchsia::modular::ModuleManifest entry;
  entry.binary = "id1";
  entry.action = "verb wont match";
  source->add("1", std::move(entry));
  source->idle();

  auto query = QueryBuilder("no matchy!").build();

  FindModules(std::move(query));

  // The Resolver returns an empty candidate list
  ASSERT_EQ(0lu, results()->size());
}

TEST_F(LocalModuleResolverTest, ExplicitUrl) {
  auto source = AddSource("test");
  ResetResolver();

  fuchsia::modular::ModuleManifest entry;
  entry.binary = "no see this";
  entry.action = "verb";
  source->add("1", std::move(entry));
  source->idle();

  auto query = QueryBuilder("action").SetHandler("another URL").build();

  FindModules(std::move(query));

  // Even though the query has a action set that matches another Module, we
  // ignore it and prefer only the one URL. It's OK that the referenced Module
  // ("another URL") doesn't have a manifest entry.
  ASSERT_EQ(1u, results()->size());
  EXPECT_EQ("another URL", results()->at(0).module_id);
}

TEST_F(LocalModuleResolverTest, Simpleaction) {
  // Also add Modules from multiple different sources.
  auto source1 = AddSource("test1");
  auto source2 = AddSource("test2");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    source1->add("1", std::move(entry));
  }
  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module2";
    entry.action = "com.google.fuchsia.navigate.v1";
    source2->add("1", std::move(entry));
  }
  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module3";
    entry.action = "com.google.fuchsia.exist.vinfinity";
    source1->add("2", std::move(entry));
  }

  source1->idle();

  auto query = QueryBuilder("com.google.fuchsia.navigate.v1").build();
  // This is mostly the contents of the FindModules() convenience function
  // above.  It's copied here so that we can call source2->idle() before
  // RunLoopUntilIdle() for this case only.
  auto scoring_info = fuchsia::modular::ResolverScoringInfo::New();
  bool got_response = false;
  resolver_->FindModules(
      std::move(query), nullptr /* scoring_info */,
      [this, &got_response](const fuchsia::modular::FindModulesResult& result) {
        got_response = true;
        result.Clone(&result_);
      });
  // Waiting until here to set |source2| as idle shows that FindModules() is
  // effectively delayed until all sources have indicated idle ("module2" is in
  // |source2|).
  source2->idle();
  RunLoopUntilIdle();
  ASSERT_TRUE(got_response);

  ASSERT_EQ(2lu, results()->size());
  EXPECT_EQ("module1", results()->at(0).module_id);
  EXPECT_EQ("module2", results()->at(1).module_id);

  // Remove the entries and we should see no more results. Our
  // TestManifestSource implementation above doesn't send its tasks to the
  // task_runner so we don't have to wait.
  source1->remove("1");
  source2->remove("1");

  FindModules(QueryBuilder("com.google.fuchsia.navigate.v1").build());
  ASSERT_EQ(0lu, results()->size());
}

TEST_F(LocalModuleResolverTest, SimpleParameterTypes) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter1;
    parameter1.name = "start";
    parameter1.type = "foo";
    fuchsia::modular::ParameterConstraint parameter2;
    parameter2.name = "destination";
    parameter2.type = "baz";
    entry.parameter_constraints.push_back(std::move(parameter1));
    entry.parameter_constraints.push_back(std::move(parameter2));
    source->add("1", std::move(entry));
  }
  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module2";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter1;
    parameter1.name = "start";
    parameter1.type = "frob";
    fuchsia::modular::ParameterConstraint parameter2;
    parameter2.name = "destination";
    parameter2.type = "froozle";
    entry.parameter_constraints.push_back(std::move(parameter1));
    entry.parameter_constraints.push_back(std::move(parameter2));
    source->add("2", std::move(entry));
  }
  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module3";
    entry.action = "com.google.fuchsia.exist.vinfinity";
    fuchsia::modular::ParameterConstraint parameter;
    parameter.name = "with";
    parameter.type = "compantionCube";
    entry.parameter_constraints.push_back(std::move(parameter));
    source->add("3", std::move(entry));
  }
  source->idle();

  // Either 'foo' or 'tangoTown' would be acceptible types. Only 'foo' will
  // actually match.
  auto query = QueryBuilder("com.google.fuchsia.navigate.v1")
                   .AddParameterTypes("start", {"foo", "tangoTown"})
                   .build();
  FindModules(std::move(query));
  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("module1", results()->at(0).module_id);

  // This one will match one of the two parameter constraints on module1, but
  // not both, so no match at all is expected.
  query = QueryBuilder("com.google.fuchsia.navigate.v1")
              .AddParameterTypes("start", {"foo", "tangoTown"})
              .AddParameterTypes("destination", {"notbaz"})
              .build();
  FindModules(std::move(query));
  ASSERT_EQ(0lu, results()->size());

  // Given an entity of type "frob", find a module with action
  // com.google.fuchsia.navigate.v1.
  fidl::StringPtr location_entity = AddEntity({{"frob", ""}});
  ASSERT_TRUE(!location_entity->empty());

  query = QueryBuilder("com.google.fuchsia.navigate.v1")
              .AddEntityParameter("start", location_entity)
              .build();
  FindModules(std::move(query));
  ASSERT_EQ(1u, results()->size());
  auto& res = results()->at(0);
  EXPECT_EQ("module2", res.module_id);

  // Verify that |create_parameter_map_info| is set up correctly.
  EXPECT_EQ(1lu, res.create_parameter_map_info.property_info->size());
  ASSERT_TRUE(GetProperty(res.create_parameter_map_info, "start").first);
  auto start = GetProperty(res.create_parameter_map_info, "start").second;
  ASSERT_TRUE(start->is_create_link());
  EXPECT_EQ(EntityReferenceToJson(location_entity),
            start->create_link().initial_data);
  // TODO(thatguy): Populate |allowed_types| correctly.
  EXPECT_FALSE(start->create_link().allowed_types);
}

TEST_F(LocalModuleResolverTest, SimpleJsonParameters) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter1;
    parameter1.name = "start";
    parameter1.type = "foo";
    fuchsia::modular::ParameterConstraint parameter2;
    parameter2.name = "destination";
    parameter2.type = "baz";
    entry.parameter_constraints.push_back(std::move(parameter1));
    entry.parameter_constraints.push_back(std::move(parameter2));
    source->add("1", std::move(entry));
  }
  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module2";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter1;
    parameter1.name = "start";
    parameter1.type = "frob";
    fuchsia::modular::ParameterConstraint parameter2;
    parameter2.name = "destination";
    parameter2.type = "froozle";
    entry.parameter_constraints.push_back(std::move(parameter1));
    entry.parameter_constraints.push_back(std::move(parameter2));
    source->add("2", std::move(entry));
  }
  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module3";
    entry.action = "com.google.fuchsia.exist.vinfinity";
    fuchsia::modular::ParameterConstraint parameter;
    parameter.name = "with";
    parameter.type = "compantionCube";
    entry.parameter_constraints.push_back(std::move(parameter));
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
                   .AddJsonParameter("start", startJson)
                   .AddJsonParameter("destination", destinationJson)
                   .build();
  FindModules(std::move(query));
  ASSERT_EQ(1lu, results()->size());
  auto& res = results()->at(0);
  EXPECT_EQ("module1", res.module_id);

  // Verify that |create_parameter_map_info| is set up correctly.
  EXPECT_EQ(2lu, res.create_parameter_map_info.property_info->size());
  ASSERT_TRUE(GetProperty(res.create_parameter_map_info, "start").first);
  auto start = GetProperty(res.create_parameter_map_info, "start").second;
  ASSERT_TRUE(start->is_create_link());
  EXPECT_EQ(startJson, start->create_link().initial_data);
  // TODO(thatguy): Populate |allowed_types| correctly.
  EXPECT_FALSE(start->create_link().allowed_types);

  ASSERT_TRUE(GetProperty(res.create_parameter_map_info, "destination").first);
  auto destination =
      GetProperty(res.create_parameter_map_info, "destination").second;
  ASSERT_TRUE(destination->is_create_link());
  EXPECT_EQ(destinationJson, destination->create_link().initial_data);
  // TODO(thatguy): Populate |allowed_types| correctly.
  EXPECT_FALSE(destination->create_link().allowed_types);
}

TEST_F(LocalModuleResolverTest, LinkInfoParameterType) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter1;
    parameter1.name = "start";
    parameter1.type = "foo";
    fuchsia::modular::ParameterConstraint parameter2;
    parameter2.name = "destination";
    parameter2.type = "baz";
    entry.parameter_constraints.push_back(std::move(parameter1));
    entry.parameter_constraints.push_back(std::move(parameter2));
    source->add("1", std::move(entry));
  }
  source->idle();

  // First try matching "module1" for a query that describes a
  // fuchsia::modular::Link that already allows "foo" in the
  // fuchsia::modular::Link.
  auto query = QueryBuilder("com.google.fuchsia.navigate.v1")
                   .AddLinkInfoParameterWithTypeConstraints(
                       "start", {{"a", "b"}, "c"}, {"foo"})
                   .build();
  FindModules(std::move(query));
  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("module1", results()->at(0).module_id);

  // Same thing should happen if there aren't any allowed types, but the
  // fuchsia::modular::Link's content encodes an fuchsia::modular::Entity
  // reference.
  fidl::StringPtr entity_reference = AddEntity({{"foo", ""}});
  query = QueryBuilder("com.google.fuchsia.navigate.v1")
              .AddLinkInfoParameterWithContent("start", {{"a", "b"}, "c"},
                                               entity_reference)
              .build();
  FindModules(std::move(query));
  ASSERT_EQ(1lu, results()->size());
  auto& res = results()->at(0);
  EXPECT_EQ("module1", res.module_id);

  // Verify that |create_parameter_map_info| is set up correctly.
  EXPECT_EQ(1lu, res.create_parameter_map_info.property_info->size());
  ASSERT_TRUE(GetProperty(res.create_parameter_map_info, "start").first);
  auto start = GetProperty(res.create_parameter_map_info, "start").second;
  ASSERT_TRUE(start->is_link_path());
  EXPECT_EQ("a", start->link_path().module_path->at(0));
  EXPECT_EQ("b", start->link_path().module_path->at(1));
  EXPECT_EQ("c", start->link_path().link_name);
}

TEST_F(LocalModuleResolverTest, ReAddExistingEntries) {
  // Add the same entry twice, to simulate what could happen during a network
  // reconnect, and show that the Module is still available.
  auto source = AddSource("test1");
  ResetResolver();

  fuchsia::modular::ModuleManifest entry;
  entry.binary = "id1";
  entry.action = "action1";

  fuchsia::modular::ModuleManifest entry1;
  entry.Clone(&entry1);
  source->add("1", std::move(entry1));
  source->idle();
  FindModules(QueryBuilder("action1").build());
  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("id1", results()->at(0).module_id);

  fuchsia::modular::ModuleManifest entry2;
  entry.Clone(&entry2);
  source->add("1", std::move(entry2));
  FindModules(QueryBuilder("action1").build());
  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("id1", results()->at(0).module_id);
}

// Tests that a query that does not contain a action or a URL matches a
// parameter with the correct types.
TEST_F(LocalModuleResolverTest, MatchingParameterWithNoactionOrUrl) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter;
    parameter.name = "start";
    parameter.type = "foo";
    entry.parameter_constraints.push_back(std::move(parameter));
    source->add("1", std::move(entry));
  }

  source->idle();

  auto query =
      QueryBuilder().AddParameterTypes("start", {"foo", "bar"}).build();

  FindModules(std::move(query));

  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("module1", results()->at(0).module_id);
}

// Tests that a query that does not contain a action or a URL matches when the
// parameter types do, even if the parameter name does not.
TEST_F(LocalModuleResolverTest, CorrectParameterTypeWithNoactionOrUrl) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter;
    parameter.name = "end";
    parameter.type = "foo";
    entry.parameter_constraints.push_back(std::move(parameter));
    source->add("1", std::move(entry));
  }

  source->idle();

  auto query =
      QueryBuilder().AddParameterTypes("start", {"foo", "bar"}).build();

  FindModules(std::move(query));

  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("module1", results()->at(0).module_id);
}

// Tests that a query that does not contain a action or a URL returns results
// for multiple matching entries.
TEST_F(LocalModuleResolverTest,
       CorrectParameterTypeWithNoactionOrUrlMultipleMatches) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter;
    parameter.name = "end";
    parameter.type = "foo";
    entry.parameter_constraints.push_back(std::move(parameter));
    source->add("1", std::move(entry));
  }
  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module2";
    entry.action = "com.google.fuchsia.navigate.v2";
    fuchsia::modular::ParameterConstraint parameter;
    parameter.name = "end";
    parameter.type = "foo";
    entry.parameter_constraints.push_back(std::move(parameter));
    source->add("2", std::move(entry));
  }

  source->idle();

  auto query =
      QueryBuilder().AddParameterTypes("start", {"foo", "bar"}).build();

  FindModules(std::move(query));

  ASSERT_EQ(2lu, results()->size());
  EXPECT_EQ("module1", results()->at(0).module_id);
  EXPECT_EQ("module2", results()->at(1).module_id);
}

// Tests that a query that does not contain a action or a URL does not match
// when the parameter types don't match.
TEST_F(LocalModuleResolverTest, IncorrectParameterTypeWithNoactionOrUrl) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter;
    parameter.name = "start";
    parameter.type = "not";
    entry.parameter_constraints.push_back(std::move(parameter));
    source->add("1", std::move(entry));
  }

  source->idle();

  auto query =
      QueryBuilder().AddParameterTypes("start", {"foo", "bar"}).build();

  FindModules(std::move(query));

  EXPECT_EQ(0lu, results()->size());
}

// Tests that a query without a action or url, that contains more parameters
// than the potential result, still returns that result.
TEST_F(LocalModuleResolverTest, QueryWithMoreParametersThanEntry) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter;
    parameter.name = "start";
    parameter.type = "gps";
    entry.parameter_constraints.push_back(std::move(parameter));
    source->add("1", std::move(entry));
  }

  source->idle();

  auto query = QueryBuilder()
                   .AddParameterTypes("start", {"gps", "bar"})
                   .AddParameterTypes("end", {"foo", "bar"})
                   .build();

  FindModules(std::move(query));

  EXPECT_EQ(1lu, results()->size());
}

// Tests that for a query with multiple parameters, each parameter gets assigned
// to the correct module parameters.
TEST_F(LocalModuleResolverTest, QueryWithoutActionAndMultipleParameters) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter1;
    parameter1.name = "start";
    parameter1.type = "gps";
    entry.parameter_constraints.push_back(std::move(parameter1));
    fuchsia::modular::ParameterConstraint parameter2;
    parameter2.name = "end";
    parameter2.type = "not_gps";
    entry.parameter_constraints.push_back(std::move(parameter2));
    source->add("1", std::move(entry));
  }

  source->idle();

  fidl::StringPtr start_entity = AddEntity({{"gps", "gps data"}});
  ASSERT_TRUE(!start_entity->empty());

  fidl::StringPtr end_entity = AddEntity({{"not_gps", "not gps data"}});
  ASSERT_TRUE(!end_entity->empty());

  auto query = QueryBuilder()
                   .AddEntityParameter("parameter1", start_entity)
                   .AddEntityParameter("parameter2", end_entity)
                   .build();

  FindModules(std::move(query));

  ASSERT_EQ(1lu, results()->size());
  auto& result = results()->at(0);

  EXPECT_EQ(GetProperty(result.create_parameter_map_info, "start")
                .second->create_link()
                .initial_data,
            EntityReferenceToJson(start_entity));
  EXPECT_EQ(GetProperty(result.create_parameter_map_info, "end")
                .second->create_link()
                .initial_data,
            EntityReferenceToJson(end_entity));
}

// Tests that when there are multiple valid mappings of query parameter types to
// entities, all such combinations are returned.
TEST_F(LocalModuleResolverTest, QueryWithoutActionAndTwoParametersOfSameType) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter1;
    parameter1.name = "start";
    parameter1.type = "gps";
    entry.parameter_constraints.push_back(std::move(parameter1));
    fuchsia::modular::ParameterConstraint parameter2;
    parameter2.name = "end";
    parameter2.type = "gps";
    entry.parameter_constraints.push_back(std::move(parameter2));
    source->add("1", std::move(entry));
  }

  source->idle();

  fidl::StringPtr start_entity = AddEntity({{"gps", "gps data one"}});
  ASSERT_TRUE(!start_entity->empty());

  fidl::StringPtr end_entity = AddEntity({{"gps", "gps data two"}});
  ASSERT_TRUE(!end_entity->empty());

  auto query = QueryBuilder()
                   .AddEntityParameter("parameter1", start_entity)
                   .AddEntityParameter("parameter2", end_entity)
                   .build();

  FindModules(std::move(query));

  EXPECT_EQ(2lu, results()->size());

  bool found_first_mapping = false;
  bool found_second_mapping = false;

  for (const auto& result : *results()) {
    bool start_mapped_to_start =
        GetProperty(result.create_parameter_map_info, "start")
            .second->create_link()
            .initial_data == EntityReferenceToJson(start_entity);
    bool start_mapped_to_end =
        GetProperty(result.create_parameter_map_info, "start")
            .second->create_link()
            .initial_data == EntityReferenceToJson(end_entity);
    bool end_mapped_to_end =
        GetProperty(result.create_parameter_map_info, "end")
            .second->create_link()
            .initial_data == EntityReferenceToJson(end_entity);
    bool end_mapped_to_start =
        GetProperty(result.create_parameter_map_info, "end")
            .second->create_link()
            .initial_data == EntityReferenceToJson(start_entity);
    found_first_mapping |= start_mapped_to_start && end_mapped_to_end;
    found_second_mapping |= start_mapped_to_end && end_mapped_to_start;
  }

  EXPECT_TRUE(found_first_mapping);
  EXPECT_TRUE(found_second_mapping);
}

// Tests that a query with three parameters of the same type matches an entry
// with three expected parameters in 6 different ways.
TEST_F(LocalModuleResolverTest,
       QueryWithoutActionAndThreeParametersOfSameType) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter1;
    parameter1.name = "start";
    parameter1.type = "gps";
    entry.parameter_constraints.push_back(std::move(parameter1));
    fuchsia::modular::ParameterConstraint parameter2;
    parameter2.name = "end";
    parameter2.type = "gps";
    entry.parameter_constraints.push_back(std::move(parameter2));
    fuchsia::modular::ParameterConstraint parameter3;
    parameter3.name = "middle";
    parameter3.type = "gps";
    entry.parameter_constraints.push_back(std::move(parameter3));
    source->add("1", std::move(entry));
  }

  source->idle();

  fidl::StringPtr start_entity = AddEntity({{"gps", "gps data one"}});
  ASSERT_TRUE(!start_entity->empty());

  fidl::StringPtr end_entity = AddEntity({{"gps", "gps data two"}});
  ASSERT_TRUE(!end_entity->empty());

  fidl::StringPtr middle_entity = AddEntity({{"gps", "gps data three"}});
  ASSERT_TRUE(!middle_entity->empty());

  auto query = QueryBuilder()
                   .AddEntityParameter("parameter1", start_entity)
                   .AddEntityParameter("parameter2", end_entity)
                   .AddEntityParameter("parameter3", middle_entity)
                   .build();

  FindModules(std::move(query));

  EXPECT_EQ(6lu, results()->size());
}

// Tests that a query with three parameters of the same type matches an entry
// with two expected parameters in 6 different ways.
TEST_F(LocalModuleResolverTest,
       QueryWithoutActionAndDifferentNumberOfParametersInQueryVsEntry) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter1;
    parameter1.name = "start";
    parameter1.type = "gps";
    entry.parameter_constraints.push_back(std::move(parameter1));
    fuchsia::modular::ParameterConstraint parameter2;
    parameter2.name = "end";
    parameter2.type = "gps";
    entry.parameter_constraints.push_back(std::move(parameter2));
    source->add("1", std::move(entry));
  }

  source->idle();

  fidl::StringPtr start_entity = AddEntity({{"gps", "gps data one"}});
  ASSERT_TRUE(!start_entity->empty());

  fidl::StringPtr end_entity = AddEntity({{"gps", "gps data two"}});
  ASSERT_TRUE(!end_entity->empty());

  fidl::StringPtr middle_entity = AddEntity({{"gps", "gps data three"}});
  ASSERT_TRUE(!middle_entity->empty());

  auto query = QueryBuilder()
                   .AddEntityParameter("parameter1", start_entity)
                   .AddEntityParameter("parameter2", end_entity)
                   .AddEntityParameter("parameter3", middle_entity)
                   .build();

  FindModules(std::move(query));

  EXPECT_EQ(6lu, results()->size());
}

// Tests that a query without a action does not match a module that requires a
// proper superset of the query parameters.
TEST_F(LocalModuleResolverTest,
       QueryWithoutActionWithEntryContainingProperSuperset) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter1;
    parameter1.name = "start";
    parameter1.type = "gps";
    entry.parameter_constraints.push_back(std::move(parameter1));
    fuchsia::modular::ParameterConstraint parameter2;
    parameter2.name = "end";
    parameter2.type = "gps";
    entry.parameter_constraints.push_back(std::move(parameter2));
    source->add("1", std::move(entry));
  }

  source->idle();

  fidl::StringPtr start_entity = AddEntity({{"gps", "gps data"}});
  ASSERT_TRUE(!start_entity->empty());

  // The query only contains an fuchsia::modular::Entity for "parameter1", but
  // the module manifest requires two parameters of type "gps."
  auto query =
      QueryBuilder().AddEntityParameter("parameter1", start_entity).build();

  FindModules(std::move(query));

  EXPECT_EQ(0lu, results()->size());
}

// Tests that a query without a action does not match an entry where the
// parameter types are incompatible.
TEST_F(LocalModuleResolverTest, QueryWithoutActionIncompatibleParameterTypes) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter;
    parameter.name = "start";
    parameter.type = "gps";
    entry.parameter_constraints.push_back(std::move(parameter));
    source->add("1", std::move(entry));
  }

  source->idle();

  fidl::StringPtr start_entity = AddEntity({{"not_gps", "not gps data"}});
  ASSERT_TRUE(!start_entity->empty());

  // The query only contains an fuchsia::modular::Entity for "parameter1", but
  // the module manifest requires two parameters of type "gps."
  auto query =
      QueryBuilder().AddEntityParameter("parameter1", start_entity).build();

  FindModules(std::move(query));

  EXPECT_EQ(0lu, results()->size());
}

// Tests that a query with a action requires parameter name and type to match
// (i.e. does not behave like action-less matching where the parameter names are
// disregarded).
TEST_F(LocalModuleResolverTest,
       QueryWithActionMatchesBothParameterNamesAndTypes) {
  auto source = AddSource("test");
  ResetResolver();

  {
    fuchsia::modular::ModuleManifest entry;
    entry.binary = "module1";
    entry.action = "com.google.fuchsia.navigate.v1";
    fuchsia::modular::ParameterConstraint parameter;
    parameter.name = "end";
    parameter.type = "foo";
    entry.parameter_constraints.push_back(std::move(parameter));
    source->add("1", std::move(entry));
  }

  source->idle();

  auto query = QueryBuilder("com.google.fuchsia.navigate.v1")
                   .AddParameterTypes("start", {"foo", "baz"})
                   .build();

  FindModules(std::move(query));

  EXPECT_EQ(0lu, results()->size());
}

}  // namespace
}  // namespace modular
