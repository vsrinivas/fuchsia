// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/scene/session_helpers.h"
#include "apps/mozart/src/scene/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene/tests/session_test.h"
#include "gtest/gtest.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/synchronization/waitable_event.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/thread.h"
#include "magenta/system/ulib/mx/include/mx/eventpair.h"

namespace mozart {
namespace scene {
namespace test {

using ImportTest = SessionTest;
using ImportThreadedTest = SessionThreadedTest;

TEST_F(ImportTest, ExportsResourceViaOp) {
  // Create the event pair.
  mx::eventpair source, destination;
  ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));

  // Setup the resource to export.
  ResourceId resource_id = 1;

  // Create an entity node.
  ASSERT_TRUE(Apply(NewCreateEntityNodeOp(resource_id)));

  // Assert that the entity node was correctly mapped in.
  ASSERT_EQ(1u, session_->GetMappedResourceCount());

  // Apply the export op.
  ASSERT_TRUE(Apply(NewExportResourceOp(resource_id, std::move(source))));
}

TEST_F(ImportTest, ImportsUnlinkedImportViaOp) {
  // Create the event pair.
  mx::eventpair source, destination;
  ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));

  // Apply the import op.
  ASSERT_TRUE(Apply(NewImportResourceOp(1 /* import resource ID */,
                                        mozart2::ImportSpec::NODE, /* spec */
                                        std::move(destination)) /* endpoint */
                    ));

  // Assert that the import node was correctly mapped in. It has not been linked
  // yet.
  ASSERT_EQ(1u, session_->GetMappedResourceCount());

  // Assert that the import node was setup with the correct properties.
  auto import_node = FindResource<Import>(1);

  ASSERT_TRUE(import_node);

  // No one has exported a resource so there should be no binding.
  ASSERT_EQ(nullptr, import_node->imported_resource());

  // Import specs should match.
  ASSERT_EQ(mozart2::ImportSpec::NODE, import_node->import_spec());
}

TEST_F(ImportTest, PerformsFullLinking) {
  // Create the event pair.
  mx::eventpair source, destination;
  ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));

  // Perform the import
  {
    // Apply the import op.
    ASSERT_TRUE(Apply(NewImportResourceOp(1 /* import resource ID */,
                                          mozart2::ImportSpec::NODE, /* spec */
                                          std::move(destination)) /* endpoint */
                      ));

    // Assert that the import node was correctly mapped in. It has not been
    // linked yet.
    ASSERT_EQ(1u, session_->GetMappedResourceCount());
  }

  // Bindings not yet resolved.
  {
    // Assert that the import node was setup with the correct properties.
    auto import_node = FindResource<Import>(1);

    ASSERT_TRUE(import_node);

    // No one has exported a resource so there should be no binding.
    ASSERT_EQ(nullptr, import_node->imported_resource());

    // Import specs should match.
    ASSERT_EQ(mozart2::ImportSpec::NODE, import_node->import_spec());
  }

  // Perform the export
  {
    // Create an entity node.
    ASSERT_TRUE(Apply(NewCreateEntityNodeOp(2)));

    // Assert that the entity node was correctly mapped in.
    ASSERT_EQ(2u, session_->GetMappedResourceCount());

    // Apply the export op.
    ASSERT_TRUE(Apply(NewExportResourceOp(2, std::move(source))));
  }

  // Bindings should have been resolved.
  {
    // Assert that the import node was setup with the correct properties.
    auto import_node = FindResource<Import>(1);

    ASSERT_TRUE(import_node);

    // Bindings should be resolved by now.
    ASSERT_NE(nullptr, import_node->imported_resource());

    // Import specs should match.
    ASSERT_EQ(mozart2::ImportSpec::NODE, import_node->import_spec());

    // Check that it was bound to the right object.
    ASSERT_NE(nullptr, import_node->imported_resource());
    auto entity = FindResource<EntityNode>(2);
    ASSERT_TRUE(entity);
    ASSERT_EQ(import_node->imported_resource(), entity.get());
    ASSERT_TRUE(import_node->delegate());
    ASSERT_EQ(import_node->delegate()->type_info().flags,
              entity->type_info().flags);
    ASSERT_EQ(1u, entity->imports().size());
    ASSERT_EQ(import_node.get(), *(entity->imports().begin()));
  }
}

// TODO(chinmaygarde): This test will be fixed when the resource linker can
// detect the death of the import token. Even this, the test is not complete
// because we need notification of the death of the import token from the
// resource linker. Currently, the notification is only for the expiry of the
// export token on peer death. So it likely that the expiry API will be modified
// slightly.
TEST_F(ImportThreadedTest,
       DISABLED_KillingImportedResourceEvictsFromResourceLinker) {
  // Setup a latch on the resource expiring in the linker.
  ftl::AutoResetWaitableEvent import_expired_latch;
  session_context_->GetResourceLinker().SetOnExpiredCallback(
      [this, &import_expired_latch](ResourcePtr,
                                    ResourceLinker::ExpirationCause cause) {
        ASSERT_EQ(ResourceLinker::ExpirationCause::kImportHandleClosed, cause);
        import_expired_latch.Signal();
      });

  mx::eventpair source;

  PostTaskSync([this, &source]() {
    // Create the event pair.
    mx::eventpair destination;
    ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));

    // Apply the import op.
    ASSERT_TRUE(Apply(NewImportResourceOp(1 /* import resource ID */,
                                          mozart2::ImportSpec::NODE, /* spec */
                                          std::move(destination)) /* endpoint */
                      ));

    // Assert that the import node was correctly mapped in. It has not been
    // linked yet.
    ASSERT_EQ(1u, session_->GetMappedResourceCount());

    // Assert that the resource linker is ready to potentially link the
    // resource.
    ASSERT_EQ(1u, session_context_->GetResourceLinker().UnresolvedImports());

    // Assert that the import node was setup with the correct properties.
    auto import_node = FindResource<Import>(1);

    ASSERT_TRUE(import_node);

    // No one has exported a resource so there should be no binding.
    ASSERT_EQ(nullptr, import_node->imported_resource());

    // Import specs should match.
    ASSERT_EQ(mozart2::ImportSpec::NODE, import_node->import_spec());

    // Release the import resource.
    ASSERT_TRUE(Apply(NewReleaseResourceOp(1 /* import resource ID */)));
  });

  // Make sure the expiry handle tells us that the resource has expired.
  import_expired_latch.Wait();

  // Assert that the resource linker has removed the unresolved import
  // registration. We have already asserted that the unresolved import was
  // registered in the initial post task.
  ASSERT_EQ(session_context_->GetResourceLinker().UnresolvedImports(), 0u);
}

TEST_F(ImportTest,
       ProxiesCanBeFoundByTheirContainerOrTheirUnderlyingEntityType) {
  // Create an unlinked import resource.
  mx::eventpair source, destination;

  ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));

  // Apply the import op.
  ASSERT_TRUE(Apply(NewImportResourceOp(1 /* import resource ID */,
                                        mozart2::ImportSpec::NODE, /* spec */
                                        std::move(destination)) /* endpoint */
                    ));

  // Assert that the import node was correctly mapped in. It has not been
  // linked yet.
  ASSERT_EQ(1u, session_->GetMappedResourceCount());

  // Resolve by the import container.

  {
    // Assert that the import node was setup with the correct properties.
    auto import_node = FindResource<Import>(1);

    ASSERT_TRUE(import_node);

    // No one has exported a resource so there should be no binding.
    ASSERT_EQ(nullptr, import_node->imported_resource());

    // Import specs should match.
    ASSERT_EQ(mozart2::ImportSpec::NODE, import_node->import_spec());
  }

  // Resolve by the resource owned by the import container.
  {
    // Assert that the import node contains a node with the correct properties.
    auto import_node_backing = FindResource<EntityNode>(1);

    ASSERT_TRUE(import_node_backing);

    // Since the entity node is not owned by the resource map, its ID is 0.
    ASSERT_EQ(0u, import_node_backing->id());
  }
}

TEST_F(ImportTest, UnlinkedImportedResourceCanAcceptOps) {
  // Create an unlinked import resource.
  mx::eventpair source, destination;
  {
    ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));

    // Apply the import op.
    ASSERT_TRUE(Apply(NewImportResourceOp(1 /* import resource ID */,
                                          mozart2::ImportSpec::NODE, /* spec */
                                          std::move(destination)) /* endpoint */
                      ));

    // Assert that the import node was correctly mapped in. It has not been
    // linked yet.
    ASSERT_EQ(1u, session_->GetMappedResourceCount());

    // Assert that the import node was setup with the correct properties.
    auto import_node = FindResource<Import>(1);

    ASSERT_TRUE(import_node);

    // No one has exported a resource so there should be no binding.
    ASSERT_EQ(import_node->imported_resource(), nullptr);

    // Import specs should match.
    ASSERT_EQ(mozart2::ImportSpec::NODE, import_node->import_spec());
  }

  // Attempt to add an entity node as a child to an unlinked resource.
  {
    // Create the entity node.
    ASSERT_TRUE(Apply(NewCreateEntityNodeOp(2 /* child resource id */)));

    // Add the entity node to the import.
    ASSERT_TRUE(Apply(NewAddChildOp(1 /* unlinked import resource */,
                                    2 /* child resource */)));
  }
}

TEST_F(ImportTest, LinkedResourceShouldBeAbleToAcceptOps) {
  // Create the event pair.
  mx::eventpair source, destination;
  ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));

  // Perform the import
  {
    // Apply the import op.
    ASSERT_TRUE(Apply(NewImportResourceOp(1 /* import resource ID */,
                                          mozart2::ImportSpec::NODE, /* spec */
                                          std::move(destination)) /* endpoint */
                      ));

    // Assert that the import node was correctly mapped in. It has not been
    // linked yet.
    ASSERT_EQ(1u, session_->GetMappedResourceCount());
  }

  // Bindings not yet resolved.
  {
    // Assert that the import node was setup with the correct properties.
    auto import_node = FindResource<Import>(1);

    ASSERT_TRUE(import_node);

    // No one has exported a resource so there should be no binding.
    ASSERT_EQ(nullptr, import_node->imported_resource());

    // Import specs should match.
    ASSERT_EQ(mozart2::ImportSpec::NODE, import_node->import_spec());
  }

  // Perform the export
  {
    // Create an entity node.
    ASSERT_TRUE(Apply(NewCreateEntityNodeOp(2)));

    // Assert that the entity node was correctly mapped in.
    ASSERT_EQ(2u, session_->GetMappedResourceCount());

    // Apply the export op.
    ASSERT_TRUE(Apply(NewExportResourceOp(2, std::move(source))));
  }

  // Bindings should have been resolved.
  {
    // Assert that the import node was setup with the correct properties.
    auto import_node = FindResource<Import>(1);

    ASSERT_TRUE(import_node);

    // Bindings should be resolved by now.
    ASSERT_NE(import_node->imported_resource(), nullptr);

    // Import specs should match.
    ASSERT_EQ(import_node->import_spec(), mozart2::ImportSpec::NODE);
  }

  // Attempt to add an entity node as a child to an linked resource.
  {
    // Create the entity node.
    ASSERT_TRUE(Apply(NewCreateEntityNodeOp(3 /* child resource id */)));

    // Add the entity node to the import.
    ASSERT_TRUE(Apply(NewAddChildOp(1 /* unlinked import resource */,
                                    3 /* child resource */)));
  }
}

TEST_F(ImportTest, EmbedderCanEmbedNodesFromElsewhere) {
  // Create the token pain.
  mx::eventpair import_token, export_token;
  ASSERT_EQ(MX_OK, mx::eventpair::create(0, &import_token, &export_token));

  // Effective node hierarchy must be:
  //
  //    +----+
  //    | 1  |
  //    +----+
  //       |
  //       +----------+ Import
  //       |          |
  //       v          v
  //    +----+     +----+
  //    | 2  |     |1001|
  //    +----+     +----+
  //       |          |
  //       |          |
  //       |          |
  //       v          v
  //    +----+     +----+
  //    | 3  |     |1002|
  //    +----+     +----+
  //                  |
  //                  |
  //                  v
  //               +----+
  //               |1003|
  //               +----+

  // Embedder.
  {
    ASSERT_TRUE(Apply(NewCreateSceneOp(1)));
    ASSERT_TRUE(Apply(NewCreateEntityNodeOp(2)));
    ASSERT_TRUE(Apply(NewCreateEntityNodeOp(3)));
    ASSERT_TRUE(Apply(NewAddChildOp(1, 2)));
    ASSERT_TRUE(Apply(NewAddChildOp(2, 3)));

    // Export.
    ASSERT_TRUE(Apply(NewExportResourceOp(1, std::move(export_token))));
    ASSERT_EQ(1u, session_context_->GetResourceLinker().UnresolvedExports());
  }

  // Embeddee.
  {
    ASSERT_TRUE(Apply(NewCreateEntityNodeOp(1001)));
    ASSERT_TRUE(Apply(NewCreateEntityNodeOp(1002)));
    ASSERT_TRUE(Apply(NewCreateEntityNodeOp(1003)));
    ASSERT_TRUE(Apply(NewAddChildOp(1001, 1002)));
    ASSERT_TRUE(Apply(NewAddChildOp(1002, 1003)));

    // Import.
    ASSERT_TRUE(Apply(NewImportResourceOp(500, mozart2::ImportSpec::NODE,
                                          std::move(import_token))));
    ASSERT_TRUE(Apply(NewAddChildOp(500, 1001)));
  }

  // Check that the scene has an item in its imports. That is how the visitor
  // will visit the imported node.
  {
    auto scene = FindResource<Scene>(1);
    ASSERT_TRUE(scene);
    ASSERT_EQ(1u, scene->imports().size());
  }
}

}  // namespace test
}  // namespace scene
}  // namespace mozart
