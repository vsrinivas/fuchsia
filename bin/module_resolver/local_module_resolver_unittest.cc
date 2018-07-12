// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/module_resolver/local_module_resolver.h"

#include <lib/entity/cpp/json.h>
#include <lib/fsl/types/type_converters.h>
#include <lib/fxl/files/file.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/module_resolver/cpp/formatting.h>

#include "peridot/lib/testing/entity_resolver_fake.h"

namespace modular {
namespace {

class TestManifestSource : public ModuleManifestSource {
 public:
  IdleFn idle;
  NewEntryFn add;
  RemovedEntryFn remove;

 private:
  void Watch(async_dispatcher_t* dispatcher, IdleFn idle_fn, NewEntryFn new_fn,
             RemovedEntryFn removed_fn) override {
    idle = std::move(idle_fn);
    add = std::move(new_fn);
    remove = std::move(removed_fn);
  }
};

class FindModulesTest : public gtest::TestLoopFixture {
 protected:
  void ResetResolver() {
    // TODO: |impl_| will fail to resolve any queries whose parameters are
    // entity references.
    impl_.reset(new LocalModuleResolver());
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

  void FindModules(fuchsia::modular::FindModulesQuery query) {
    bool got_response = false;
    resolver_->FindModules(
        std::move(query),
        [this, &got_response](fuchsia::modular::FindModulesResponse response) {
          got_response = true;
          response.Clone(&response_);
        });
    RunLoopUntilIdle();
    ASSERT_TRUE(got_response);
  }

  const fidl::VectorPtr<fuchsia::modular::FindModulesResult>& results() const {
    return response_.results;
  }

  class QueryBuilder {
   public:
    QueryBuilder() = delete;

    QueryBuilder(std::string action)
        : query(fuchsia::modular::FindModulesQuery()) {
      query.action = action;
      query.parameter_constraints.resize(0);
    }

    fuchsia::modular::FindModulesQuery build() { return std::move(query); }

    QueryBuilder& AddParameter(std::string name,
                               std::vector<std::string> types) {
      fuchsia::modular::FindModulesParameterConstraint constraint;
      constraint.param_name = name;
      constraint.param_types = fxl::To<fidl::VectorPtr<fidl::StringPtr>>(types);
      query.parameter_constraints.push_back(std::move(constraint));
      return *this;
    }

   private:
    fuchsia::modular::FindModulesQuery query;
  };

  std::unique_ptr<LocalModuleResolver> impl_;

  std::map<std::string, ModuleManifestSource*> test_sources_;
  fuchsia::modular::ModuleResolverPtr resolver_;

  fuchsia::modular::FindModulesResponse response_;
};

class FindModulesByTypesTest : public gtest::TestLoopFixture {
 protected:
  void ResetResolver() {
    // TODO: |impl_| will fail to resolve any queries whose parameters are
    // entity references.
    impl_.reset(new LocalModuleResolver());
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

  void FindModulesByTypes(fuchsia::modular::FindModulesByTypesQuery query) {
    bool got_response = false;
    resolver_->FindModulesByTypes(
        std::move(query),
        [this,
         &got_response](fuchsia::modular::FindModulesByTypesResponse response) {
          got_response = true;
          response.Clone(&response_);
        });
    RunLoopUntilIdle();
    ASSERT_TRUE(got_response);
  }

  const fidl::VectorPtr<fuchsia::modular::FindModulesByTypesResult>& results()
      const {
    return response_.results;
  }

  class QueryBuilder {
   public:
    fuchsia::modular::FindModulesByTypesQuery build() {
      return std::move(query);
    }

    QueryBuilder& AddParameter(std::string name,
                               std::vector<std::string> types) {
      fuchsia::modular::FindModulesByTypesParameterConstraint constraint;
      constraint.constraint_name = name;
      constraint.param_types = fxl::To<fidl::VectorPtr<fidl::StringPtr>>(types);
      query.parameter_constraints.push_back(std::move(constraint));
      return *this;
    }

   private:
    fuchsia::modular::FindModulesByTypesQuery query;
  };

  std::string GetMappingFromQuery(
      const fidl::VectorPtr<
          fuchsia::modular::FindModulesByTypesParameterMapping>& mapping,
      std::string query_constraint_name) {
    for (auto& item : *mapping) {
      if (item.query_constraint_name == query_constraint_name) {
        return item.result_param_name;
      }
    }
    return "";
  }

  std::unique_ptr<LocalModuleResolver> impl_;

  std::map<std::string, ModuleManifestSource*> test_sources_;
  fuchsia::modular::ModuleResolverPtr resolver_;

  fuchsia::modular::FindModulesByTypesResponse response_;
};

TEST_F(FindModulesTest, Null) {
  auto source = AddSource("test");
  ResetResolver();

  fuchsia::modular::ModuleManifest entry;
  entry.binary = "id1";
  entry.action = "verb wont match";
  source->add("1", std::move(entry));
  source->idle();

  FindModules(QueryBuilder("no matchy!").build());

  // The Resolver returns an empty candidate list
  ASSERT_EQ(0lu, results()->size());
}

TEST_F(FindModulesTest, Simpleaction) {
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

  // This is mostly the contents of the FindModules() convenience function
  // above.  It's copied here so that we can call source2->idle() before
  // RunLoopUntilIdle() for this case only.
  bool got_response = false;
  resolver_->FindModules(
      QueryBuilder("com.google.fuchsia.navigate.v1").build(),
      [this, &got_response](fuchsia::modular::FindModulesResponse response) {
        got_response = true;
        response.Clone(&response_);
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

TEST_F(FindModulesTest, SimpleParameterTypes) {
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
  FindModules(QueryBuilder("com.google.fuchsia.navigate.v1")
                  .AddParameter("start", {"foo", "tangoTown"})
                  .build());
  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("module1", results()->at(0).module_id);

  // This one will match one of the two parameter constraints on module1, but
  // not both, so no match at all is expected.
  FindModules(QueryBuilder("com.google.fuchsia.navigate.v1")
                  .AddParameter("start", {"foo", "tangoTown"})
                  .AddParameter("destination", {"notbaz"})
                  .build());
  ASSERT_EQ(0lu, results()->size());

  // Given parameter of type "frob", find a module with action
  // com.google.fuchsia.navigate.v1.
  FindModules(QueryBuilder("com.google.fuchsia.navigate.v1")
                  .AddParameter("start", {"frob"})
                  .build());
  ASSERT_EQ(1u, results()->size());
  auto& res = results()->at(0);
  EXPECT_EQ("module2", res.module_id);
}

TEST_F(FindModulesTest, ReAddExistingEntries) {
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
TEST_F(FindModulesByTypesTest, MatchingParameterWithNoactionOrUrl) {
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

  FindModulesByTypes(
      QueryBuilder().AddParameter("start", {"foo", "bar"}).build());

  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("module1", results()->at(0).module_id);
}

// Tests that a query that does not contain a action or a URL matches when the
// parameter types do, even if the parameter name does not.
TEST_F(FindModulesByTypesTest, CorrectParameterTypeWithNoactionOrUrl) {
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

  FindModulesByTypes(
      QueryBuilder().AddParameter("start", {"foo", "bar"}).build());

  ASSERT_EQ(1lu, results()->size());
  EXPECT_EQ("module1", results()->at(0).module_id);
}

// Tests that a query that does not contain a action or a URL returns results
// for multiple matching entries.
TEST_F(FindModulesByTypesTest,
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

  FindModulesByTypes(
      QueryBuilder().AddParameter("start", {"foo", "bar"}).build());

  ASSERT_EQ(2lu, results()->size());
  EXPECT_EQ("module1", results()->at(0).module_id);
  EXPECT_EQ("module2", results()->at(1).module_id);
}

// Tests that a query that does not contain a action or a URL does not match
// when the parameter types don't match.
TEST_F(FindModulesByTypesTest, IncorrectParameterTypeWithNoactionOrUrl) {
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

  FindModulesByTypes(
      QueryBuilder().AddParameter("start", {"foo", "bar"}).build());

  EXPECT_EQ(0lu, results()->size());
}

// Tests that a query without a action or url, that contains more parameters
// than the potential result, still returns that result.
TEST_F(FindModulesByTypesTest, QueryWithMoreParametersThanEntry) {
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

  FindModulesByTypes(QueryBuilder()
                         .AddParameter("start", {"gps", "bar"})
                         .AddParameter("end", {"foo", "bar"})
                         .build());

  EXPECT_EQ(1lu, results()->size());
}

// Tests that for a query with multiple parameters, each parameter gets assigned
// to the correct module parameters.
TEST_F(FindModulesByTypesTest, QueryWithoutActionAndMultipleParameters) {
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

  FindModulesByTypes(QueryBuilder()
                         .AddParameter("parameter1", {"gps"})
                         .AddParameter("parameter2", {"not_gps"})
                         .build());

  ASSERT_EQ(1lu, results()->size());
  auto& result = results()->at(0);

  EXPECT_EQ("start",
            GetMappingFromQuery(result.parameter_mappings, "parameter1"));
  EXPECT_EQ("end",
            GetMappingFromQuery(result.parameter_mappings, "parameter2"));
}

// Tests that when there are multiple valid mappings of query parameter types to
// entities, all such combinations are returned.
TEST_F(FindModulesByTypesTest, FindModulesByTypeWithTwoParametersOfSameType) {
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

  FindModulesByTypes(QueryBuilder()
                         .AddParameter("parameter1", {"gps"})
                         .AddParameter("parameter2", {"gps"})
                         .build());

  EXPECT_EQ(2lu, results()->size());

  bool found_first_mapping = false;
  bool found_second_mapping = false;

  for (const auto& result : *results()) {
    bool start_mapped_to_p1 =
        GetMappingFromQuery(result.parameter_mappings, "parameter1") == "start";

    bool start_mapped_to_p2 =
        GetMappingFromQuery(result.parameter_mappings, "parameter2") == "start";

    bool end_mapped_to_p1 =
        GetMappingFromQuery(result.parameter_mappings, "parameter1") == "end";

    bool end_mapped_to_p2 =
        GetMappingFromQuery(result.parameter_mappings, "parameter2") == "end";

    found_first_mapping |= start_mapped_to_p1 && end_mapped_to_p2;
    found_second_mapping |= start_mapped_to_p2 && end_mapped_to_p1;
  }

  EXPECT_TRUE(found_first_mapping);
  EXPECT_TRUE(found_second_mapping);
}

// Tests that a query with three parameters of the same type matches an entry
// with three expected parameters in 6 different ways.
TEST_F(FindModulesByTypesTest, QueryWithoutActionAndThreeParametersOfSameType) {
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

  FindModulesByTypes(QueryBuilder()
                         .AddParameter("parameter1", {"gps"})
                         .AddParameter("parameter2", {"gps"})
                         .AddParameter("parameter3", {"gps"})
                         .build());

  EXPECT_EQ(6lu, results()->size());
}

// Tests that a query with three parameters of the same type matches an entry
// with two expected parameters in 6 different ways.
TEST_F(FindModulesByTypesTest,
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

  FindModulesByTypes(QueryBuilder()
                         .AddParameter("parameter1", {"gps"})
                         .AddParameter("parameter2", {"gps"})
                         .AddParameter("parameter3", {"gps"})
                         .build());

  EXPECT_EQ(6lu, results()->size());
}

// Tests that a query without a action does not match a module that requires a
// proper superset of the query parameters.
TEST_F(FindModulesByTypesTest,
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

  // The query only contains an fuchsia::modular::Entity for "parameter1", but
  // the module manifest requires two parameters of type "gps."
  FindModulesByTypes(
      QueryBuilder().AddParameter("parameter1", {"gps"}).build());

  EXPECT_EQ(0lu, results()->size());
}

// Tests that a query without a action does not match an entry where the
// parameter types are incompatible.
TEST_F(FindModulesByTypesTest, QueryWithoutActionIncompatibleParameterTypes) {
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

  // The query only contains an fuchsia::modular::Entity for "parameter1", but
  // the module manifest requires two parameters of type "gps."
  FindModulesByTypes(
      QueryBuilder().AddParameter("parameter1", {"not_gps"}).build());

  EXPECT_EQ(0lu, results()->size());
}

// Tests that a query with a action requires parameter name and type to match
// (i.e. does not behave like action-less matching where the parameter names are
// disregarded).
TEST_F(FindModulesTest, QueryWithActionMatchesBothParameterNamesAndTypes) {
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

  FindModules(QueryBuilder("com.google.fuchsia.navigate.v1")
                  .AddParameter("start", {"foo", "baz"})
                  .build());

  EXPECT_EQ(0lu, results()->size());
}

}  // namespace
}  // namespace modular
