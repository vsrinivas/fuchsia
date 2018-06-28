// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/zx/eventpair.h>

#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "garnet/lib/ui/gfx/tests/util.h"
#include "lib/ui/scenic/fidl_helpers.h"

namespace scenic {
namespace gfx {
namespace test {

using ImportTest = SessionTest;

TEST_F(ImportTest, ExportsResourceViaCmd) {
  // Create the event pair.
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Setup the resource to export.
  scenic::ResourceId resource_id = 1;

  // Create an entity node.
  ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(resource_id)));

  // Assert that the entity node was correctly mapped in.
  ASSERT_EQ(1u, session_->GetMappedResourceCount());

  // Apply the export command.
  ASSERT_TRUE(Apply(
      scenic::NewExportResourceCmd(resource_id, std::move(source))));
}

TEST_F(ImportTest, ImportsUnlinkedImportViaCmd) {
  // Create the event pair.
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Apply the import command.
  ASSERT_TRUE(Apply(scenic::NewImportResourceCmd(
      1 /* import resource ID */,
      ::fuchsia::ui::gfx::ImportSpec::NODE, /* spec */
      std::move(destination))               /* endpoint */
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
  ASSERT_EQ(::fuchsia::ui::gfx::ImportSpec::NODE, import_node->import_spec());
}

TEST_F(ImportTest, PerformsFullLinking) {
  // Create the event pair.
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Perform the import
  {
    // Apply the import command.
    ASSERT_TRUE(Apply(scenic::NewImportResourceCmd(
        1 /* import resource ID */,
        ::fuchsia::ui::gfx::ImportSpec::NODE, /* spec */
        std::move(destination))               /* endpoint */
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
    ASSERT_EQ(::fuchsia::ui::gfx::ImportSpec::NODE, import_node->import_spec());
  }

  // Perform the export
  {
    // Create an entity node.
    ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(2)));

    // Assert that the entity node was correctly mapped in.
    ASSERT_EQ(2u, session_->GetMappedResourceCount());

    // Apply the export command.
    ASSERT_TRUE(
        Apply(scenic::NewExportResourceCmd(2, std::move(source))));
  }

  // Bindings should have been resolved.
  {
    // Assert that the import node was setup with the correct properties.
    auto import_node = FindResource<Import>(1);

    ASSERT_TRUE(import_node);

    // Bindings should be resolved by now.
    ASSERT_NE(nullptr, import_node->imported_resource());

    // Import specs should match.
    ASSERT_EQ(::fuchsia::ui::gfx::ImportSpec::NODE, import_node->import_spec());

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

TEST_F(ImportTest, HandlesDeadSourceHandle) {
  zx::eventpair source_out;
  zx::eventpair destination;
  {
    zx::eventpair source;
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));
    source_out = zx::eventpair{source.get()};
    // source dies now.
  }

  // Export an entity node with a dead handle.
  ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(1)));
  EXPECT_FALSE(Apply(scenic::NewExportResourceCmd(
      1 /* resource id */, std::move(source_out))));
}

TEST_F(ImportTest, HandlesDeadDestinationHandle) {
  zx::eventpair source_out;
  zx::eventpair destination_out;
  {
    zx::eventpair source;
    zx::eventpair destination;
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));
    source_out = zx::eventpair{source.get()};
    destination_out = zx::eventpair{destination.get()};
    // source and destination dies now.
  }

  // Import an entity node with a dead handle.
  ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(1)));
  EXPECT_FALSE(Apply(scenic::NewImportResourceCmd(
      1 /* resource id */, ::fuchsia::ui::gfx::ImportSpec::NODE,
      std::move(destination_out))));
}

TEST_F(ImportTest, DestroyingExportedResourceSendsEvent) {
  zx::eventpair source;
  zx::eventpair destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Export an entity node.
  scenic::ResourceId node_id = 1;
  scenic::ResourceId import_node = 2;
  ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(node_id)));
  EXPECT_TRUE(
      Apply(scenic::NewExportResourceCmd(node_id, std::move(source))));
  EXPECT_TRUE(Apply(scenic::NewImportResourceCmd(
      import_node, ::fuchsia::ui::gfx::ImportSpec::NODE,
      std::move(destination))));

  // Release the entity node.
  EXPECT_TRUE(Apply(scenic::NewReleaseResourceCmd(node_id)));

  // Run the message loop until we get an event.
  RunLoopUntilIdle();

  // Verify that we got an ImportUnboundEvent.
  EXPECT_EQ(1u, events_.size());
  fuchsia::ui::scenic::Event event = std::move(events_[0]);
  EXPECT_EQ(fuchsia::ui::scenic::Event::Tag::kGfx, event.Which());
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kImportUnbound,
            event.gfx().Which());
  ASSERT_EQ(import_node, event.gfx().import_unbound().resource_id);
}

TEST_F(ImportTest, ImportingNodeAfterDestroyingExportedResourceSendsEvent) {
  zx::eventpair source;
  zx::eventpair destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Export an entity node.
  scenic::ResourceId node_id = 1;
  scenic::ResourceId import_node = 2;
  ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(node_id)));
  EXPECT_TRUE(
      Apply(scenic::NewExportResourceCmd(node_id, std::move(source))));

  // Release the entity node.
  EXPECT_TRUE(Apply(scenic::NewReleaseResourceCmd(node_id)));

  // Try to import after the entity node has been released.
  EXPECT_TRUE(Apply(scenic::NewImportResourceCmd(
      import_node, ::fuchsia::ui::gfx::ImportSpec::NODE,
      std::move(destination))));

  // Run the message loop until we get an event.
  RunLoopUntilIdle();

  // Verify that we got an ImportUnboundEvent.
  EXPECT_EQ(1u, events_.size());
  fuchsia::ui::scenic::Event event = std::move(events_[0]);
  EXPECT_EQ(fuchsia::ui::scenic::Event::Tag::kGfx, event.Which());
  EXPECT_EQ(::fuchsia::ui::gfx::Event::Tag::kImportUnbound,
            event.gfx().Which());
  ASSERT_EQ(import_node, event.gfx().import_unbound().resource_id);
}

TEST_F(ImportTest, KillingImportedResourceEvictsFromResourceLinker) {
  bool called = false;
  engine_->resource_linker()->SetOnExpiredCallback(
      [this, &called](Resource*, ResourceLinker::ExpirationCause cause) {
        ASSERT_EQ(ResourceLinker::ExpirationCause::kResourceDestroyed, cause);
        called = true;
      });

  zx::eventpair source;

  async::PostTask(dispatcher(), [this, &source]() {
    // Create the event pair.
    zx::eventpair destination;
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

    // Apply the import command.
    ASSERT_TRUE(Apply(scenic::NewImportResourceCmd(
        1 /* import resource ID */,
        ::fuchsia::ui::gfx::ImportSpec::NODE, /* spec */
        std::move(destination))               /* endpoint */
                      ));

    // Assert that the import node was correctly mapped in. It has not been
    // linked yet.
    ASSERT_EQ(1u, session_->GetMappedResourceCount());

    // Assert that the resource linker is ready to potentially link the
    // resource.
    ASSERT_EQ(1u, engine_->resource_linker()->NumUnresolvedImports());

    // Assert that the import node was setup with the correct properties.
    auto import_node = FindResource<Import>(1);

    ASSERT_TRUE(import_node);

    // No one has exported a resource so there should be no binding.
    ASSERT_EQ(nullptr, import_node->imported_resource());

    // Import specs should match.
    ASSERT_EQ(::fuchsia::ui::gfx::ImportSpec::NODE, import_node->import_spec());

    // Release the import resource.
    ASSERT_TRUE(Apply(
        scenic::NewReleaseResourceCmd(1 /* import resource ID */)));
  });

  // Make sure the expiry handle tells us that the resource has expired.
  RunLoopUntilIdle();
  ASSERT_TRUE(called);

  // Assert that the resource linker has removed the unresolved import
  // registration. We have already asserted that the unresolved import was
  // registered in the initial post task.
  ASSERT_EQ(engine_->resource_linker()->NumUnresolvedImports(), 0u);
}

// For a given resource, export it and bind a node to it. Additionally, keep
// an import handle open. Then, verify that the resource is not unexported until
// both the import node and the import handle are released.
TEST_F(ImportTest, ResourceUnexportedAfterImportsAndImportHandlesDie1) {
  scenic::ResourceId exported_node_id = 1;
  scenic::ResourceId import_node_id = 2;

  bool destination_handle_released = false;
  bool import_node_released = false;
  bool called = false;
  engine_->resource_linker()->SetOnExpiredCallback(
      [&](Resource*, ResourceLinker::ExpirationCause cause) {
        ASSERT_EQ(ResourceLinker::ExpirationCause::kNoImportsBound, cause);
        ASSERT_EQ(0u, engine_->resource_linker()->NumExports());
        ASSERT_EQ(0u, engine_->resource_linker()->NumUnresolvedImports());

        // Ensure that our export was unbound after all the necessary
        // preconditions were met.
        ASSERT_EQ(1u, session_->GetMappedResourceCount());
        ASSERT_TRUE(destination_handle_released);
        ASSERT_TRUE(import_node_released);

        // Ensure the node is no longer marked as exported.
        auto exported_node = FindResource<EntityNode>(exported_node_id);
        ASSERT_TRUE(exported_node);
        ASSERT_EQ(false, exported_node->is_exported());
        called = true;
      });

  // Create the event pair.
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  async::PostTask(dispatcher(), [&]() {
    // Create the resource being exported.
    Apply(scenic::NewCreateEntityNodeCmd(exported_node_id));
    auto exported_node = FindResource<EntityNode>(exported_node_id);
    ASSERT_TRUE(exported_node);
    ASSERT_EQ(false, exported_node->is_exported());

    // Apply the export command.
    ASSERT_TRUE(Apply(scenic::NewExportResourceCmd(exported_node_id,
                                                           std::move(source))));
    ASSERT_EQ(true, exported_node->is_exported());

    // Apply the import command.
    ASSERT_TRUE(Apply(scenic::NewImportResourceCmd(
        import_node_id, ::fuchsia::ui::gfx::ImportSpec::NODE, /* spec */
        CopyEventPair(destination))                           /* endpoint */
                      ));
    auto import_node = FindResource<Import>(import_node_id);
    ASSERT_TRUE(import_node);

    // Assert that the nodes were correctly mapped in.
    ASSERT_EQ(2u, session_->GetMappedResourceCount());

    // Nodes should be bound together.
    ASSERT_EQ(exported_node.get(), import_node->imported_resource());
    ASSERT_EQ(true, exported_node->is_exported());
    ASSERT_EQ(1u, exported_node->imports().size());
    ASSERT_EQ(1u, engine_->resource_linker()->NumExports());

    async::PostTask(dispatcher(), [&]() {
      // Release the only import bound to the exported node.
      import_node_released = true;
      EXPECT_TRUE(Apply(scenic::NewReleaseResourceCmd(import_node_id)));

      async::PostTask(dispatcher(), [&]() {
        // Exported node should still be marked as exported.
        auto exported_node = FindResource<EntityNode>(exported_node_id);
        ASSERT_TRUE(exported_node);
        ASSERT_EQ(true, exported_node->is_exported());
        // List of imports should be empty.
        ASSERT_EQ(0u, exported_node->imports().size());

        // Reset the only import handle.
        destination_handle_released = true;
        destination.reset();
      });
    });
  });

  EXPECT_TRUE(RunLoopUntilIdle());
  ASSERT_TRUE(called);
}

// For a given resource, export it and bind a node to it. Additionally, keep
// an import handle open. Then, verify that the resource is not unexported until
// both the import node and the import handle are released.
// This test is identical to the previous one except the order in which the
// import node and import handle are released is switched.
TEST_F(ImportTest, ResourceUnexportedAfterImportsAndImportHandlesDie2) {
  scenic::ResourceId exported_node_id = 1;
  scenic::ResourceId import_node_id = 2;

  bool destination_handle_released = false;
  bool import_node_released = false;
  bool called = false;
  engine_->resource_linker()->SetOnExpiredCallback(
      [&](Resource*, ResourceLinker::ExpirationCause cause) {
        ASSERT_EQ(ResourceLinker::ExpirationCause::kNoImportsBound, cause);
        ASSERT_EQ(0u, engine_->resource_linker()->NumExports());
        ASSERT_EQ(0u, engine_->resource_linker()->NumUnresolvedImports());

        // Ensure that our export was unbound after all the necessary
        // preconditions were met.
        ASSERT_EQ(1u, session_->GetMappedResourceCount());
        ASSERT_TRUE(destination_handle_released);
        ASSERT_TRUE(import_node_released);

        // Ensure the node is no longer marked as exported.
        auto exported_node = FindResource<EntityNode>(exported_node_id);
        ASSERT_TRUE(exported_node);
        ASSERT_EQ(false, exported_node->is_exported());
        ASSERT_EQ(0u, exported_node->imports().size());
        called = true;
      });

  // Create the event pair.
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  async::PostTask(dispatcher(), [&]() {
    // Create the resource being exported.
    Apply(scenic::NewCreateEntityNodeCmd(exported_node_id));
    auto exported_node = FindResource<EntityNode>(exported_node_id);
    ASSERT_TRUE(exported_node);
    ASSERT_EQ(false, exported_node->is_exported());

    // Apply the export command.
    ASSERT_TRUE(Apply(scenic::NewExportResourceCmd(exported_node_id,
                                                           std::move(source))));
    ASSERT_EQ(true, exported_node->is_exported());

    // Apply the import command.
    ASSERT_TRUE(Apply(scenic::NewImportResourceCmd(
        import_node_id, ::fuchsia::ui::gfx::ImportSpec::NODE, /* spec */
        CopyEventPair(destination))                           /* endpoint */
                      ));
    auto import_node = FindResource<Import>(import_node_id);
    ASSERT_TRUE(import_node);

    // Assert that the nodes were correctly mapped in.
    ASSERT_EQ(2u, session_->GetMappedResourceCount());

    // Nodes should be bound together.
    ASSERT_EQ(exported_node.get(), import_node->imported_resource());
    ASSERT_EQ(true, exported_node->is_exported());
    ASSERT_EQ(1u, exported_node->imports().size());
    ASSERT_EQ(1u, engine_->resource_linker()->NumExports());

    async::PostTask(dispatcher(), [&]() {
      // Reset the only import handle.
      destination_handle_released = true;
      destination.reset();
      async::PostTask(dispatcher(), [&]() {
        // Exported node should still be marked as exported.
        auto exported_node = FindResource<EntityNode>(exported_node_id);
        ASSERT_TRUE(exported_node);
        ASSERT_EQ(true, exported_node->is_exported());
        ASSERT_EQ(1u, exported_node->imports().size());

        // Release the only import bound to the exported node.
        import_node_released = true;
        EXPECT_TRUE(
            Apply(scenic::NewReleaseResourceCmd(import_node_id)));
      });
    });
  });

  EXPECT_TRUE(RunLoopUntilIdle());
  ASSERT_TRUE(called);
}

// For a given resource, export it and bind a node to it. Additionally, keep
// two import handles open. Then, verify that the resource is not unexported
// until both the import node and all the import handles are released. This test
// is identical to the previous one except there is an additional import handle
// that must be destroyed.
TEST_F(ImportTest, ResourceUnexportedAfterImportsAndImportHandlesDie3) {
  scenic::ResourceId exported_node_id = 1;
  scenic::ResourceId import_node_id = 2;

  bool destination_handle1_released = false;
  bool destination_handle2_released = false;
  bool import_node_released = false;
  bool called = false;

  engine_->resource_linker()->SetOnExpiredCallback(
      [&](Resource*, ResourceLinker::ExpirationCause cause) {
        ASSERT_EQ(ResourceLinker::ExpirationCause::kNoImportsBound, cause);
        ASSERT_EQ(0u, engine_->resource_linker()->NumExports());
        ASSERT_EQ(0u, engine_->resource_linker()->NumUnresolvedImports());

        // Ensure that our export was unbound after all the necessary
        // preconditions were met.
        ASSERT_EQ(1u, session_->GetMappedResourceCount());
        ASSERT_TRUE(destination_handle1_released);
        ASSERT_TRUE(destination_handle2_released);
        ASSERT_TRUE(import_node_released);

        // Ensure the node is no longer marked as exported.
        auto exported_node = FindResource<EntityNode>(exported_node_id);
        ASSERT_TRUE(exported_node);
        ASSERT_EQ(false, exported_node->is_exported());
        ASSERT_EQ(0u, exported_node->imports().size());
        called = true;
      });

  // Create the event pair.
  zx::eventpair source, destination1;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination1));
  zx::eventpair destination2 = CopyEventPair(destination1);

  async::PostTask(dispatcher(), [&]() {
    // Create the resource being exported.
    Apply(scenic::NewCreateEntityNodeCmd(exported_node_id));
    auto exported_node = FindResource<EntityNode>(exported_node_id);
    ASSERT_TRUE(exported_node);
    ASSERT_EQ(false, exported_node->is_exported());

    // Apply the export command.
    ASSERT_TRUE(Apply(scenic::NewExportResourceCmd(exported_node_id,
                                                           std::move(source))));
    ASSERT_EQ(true, exported_node->is_exported());

    // Apply the import command.
    ASSERT_TRUE(Apply(scenic::NewImportResourceCmd(
        import_node_id, ::fuchsia::ui::gfx::ImportSpec::NODE, /* spec */
        CopyEventPair(destination1))                          /* endpoint */
                      ));
    auto import_node = FindResource<Import>(import_node_id);
    ASSERT_TRUE(import_node);

    // Assert that the nodes were correctly mapped in.
    ASSERT_EQ(2u, session_->GetMappedResourceCount());

    // Nodes should be bound together.
    ASSERT_EQ(exported_node.get(), import_node->imported_resource());
    ASSERT_EQ(true, exported_node->is_exported());
    ASSERT_EQ(1u, exported_node->imports().size());
    ASSERT_EQ(1u, engine_->resource_linker()->NumExports());

    // Post three tasks in the future. We assume the export will be released
    // after the second one. Post tasks with a slight delay so we can identify
    // the stage accurately.
    async::PostTask(dispatcher(), [&]() {
      // Reset the first import handle.
      destination_handle1_released = true;
      destination1.reset();

      async::PostTask(dispatcher(), [&]() {
        // Exported node should still be marked as exported.
        auto exported_node = FindResource<EntityNode>(exported_node_id);
        ASSERT_TRUE(exported_node);
        ASSERT_EQ(true, exported_node->is_exported());

        // Release the only import bound to the exported node.
        import_node_released = true;
        EXPECT_TRUE(
            Apply(scenic::NewReleaseResourceCmd(import_node_id)));

        async::PostTask(dispatcher(), [&]() {
          // Exported node should still be marked as exported.
          auto exported_node = FindResource<EntityNode>(exported_node_id);
          ASSERT_TRUE(exported_node);
          ASSERT_EQ(true, exported_node->is_exported());
          // List of imports should be empty.
          ASSERT_EQ(0u, exported_node->imports().size());

          // Reset the second import handle.
          destination_handle2_released = true;
          destination2.reset();
        });
      });
    });
  });

  EXPECT_TRUE(RunLoopUntilIdle());
  ASSERT_TRUE(called);
}

// For a given resource, export it and bind two nodes to it. Additionally, keep
// two import handles open. Then, verify that the resource is not unexported
// until both the import nodes and all the import handles are released. This
// test is identical to the previous one except there is an additional import
// node that must be released.
TEST_F(ImportTest, ResourceUnexportedAfterImportsAndImportHandlesDie4) {
  scenic::ResourceId exported_node_id = 1;
  scenic::ResourceId import_node_id1 = 2;
  scenic::ResourceId import_node_id2 = 3;

  bool destination_handle1_released = false;
  bool destination_handle2_released = false;
  bool import_node1_released = false;
  bool import_node2_released = false;
  bool called = false;

  engine_->resource_linker()->SetOnExpiredCallback(
      [&](Resource*, ResourceLinker::ExpirationCause cause) {
        ASSERT_EQ(ResourceLinker::ExpirationCause::kNoImportsBound, cause);
        ASSERT_EQ(0u, engine_->resource_linker()->NumExports());
        ASSERT_EQ(0u, engine_->resource_linker()->NumUnresolvedImports());

        // Ensure that our export was unbound after all the necessary
        // preconditions were met.
        ASSERT_EQ(1u, session_->GetMappedResourceCount());
        ASSERT_TRUE(destination_handle1_released);
        ASSERT_TRUE(destination_handle2_released);
        ASSERT_TRUE(import_node1_released);
        ASSERT_TRUE(import_node2_released);

        // Ensure the node is no longer marked as exported.
        auto exported_node = FindResource<EntityNode>(exported_node_id);
        ASSERT_TRUE(exported_node);
        ASSERT_EQ(false, exported_node->is_exported());
        ASSERT_EQ(0u, exported_node->imports().size());
        called = true;
      });

  // Create the event pair.
  zx::eventpair source, destination1;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination1));
  zx::eventpair destination2 = CopyEventPair(destination1);

  async::PostTask(dispatcher(), [&]() {
    // Create the resource being exported.
    Apply(scenic::NewCreateEntityNodeCmd(exported_node_id));
    auto exported_node = FindResource<EntityNode>(exported_node_id);
    ASSERT_TRUE(exported_node);
    ASSERT_EQ(false, exported_node->is_exported());

    // Apply the export command.
    ASSERT_TRUE(Apply(scenic::NewExportResourceCmd(exported_node_id,
                                                           std::move(source))));
    ASSERT_EQ(true, exported_node->is_exported());

    // Apply the import commands.
    ASSERT_TRUE(Apply(scenic::NewImportResourceCmd(
        import_node_id1, ::fuchsia::ui::gfx::ImportSpec::NODE, /* spec */
        CopyEventPair(destination1))                           /* endpoint */
                      ));
    ASSERT_TRUE(Apply(scenic::NewImportResourceCmd(
        import_node_id2, ::fuchsia::ui::gfx::ImportSpec::NODE, /* spec */
        CopyEventPair(destination1))                           /* endpoint */
                      ));
    auto import_node1 = FindResource<Import>(import_node_id1);
    ASSERT_TRUE(import_node1);
    auto import_node2 = FindResource<Import>(import_node_id2);
    ASSERT_TRUE(import_node2);

    // Assert that the nodes were correctly mapped in.
    ASSERT_EQ(3u, session_->GetMappedResourceCount());

    // Nodes should be bound together.
    ASSERT_EQ(exported_node.get(), import_node1->imported_resource());
    ASSERT_EQ(exported_node.get(), import_node2->imported_resource());
    ASSERT_EQ(true, exported_node->is_exported());
    ASSERT_EQ(2u, exported_node->imports().size());
    ASSERT_EQ(1u, engine_->resource_linker()->NumExports());

    // Post three tasks in the future. We assume the export will be released
    // after the second one. Post tasks with a slight delay so we can identify
    // the stage accurately.
    async::PostTask(dispatcher(), [&]() {
      // Reset the first import handle.
      destination_handle1_released = true;
      destination1.reset();

      async::PostTask(dispatcher(), [&]() {
        // Exported node should still be marked as exported.
        auto exported_node = FindResource<EntityNode>(exported_node_id);
        ASSERT_TRUE(exported_node);
        ASSERT_EQ(true, exported_node->is_exported());

        // Release the only import bound to the exported node.
        import_node1_released = true;
        EXPECT_TRUE(
            Apply(scenic::NewReleaseResourceCmd(import_node_id1)));

        async::PostTask(dispatcher(), [&]() {
          // Exported node should still be marked as exported.
          auto exported_node = FindResource<EntityNode>(exported_node_id);
          ASSERT_TRUE(exported_node);
          ASSERT_EQ(true, exported_node->is_exported());

          // One import should remain bound.
          ASSERT_EQ(1u, exported_node->imports().size());

          // Reset the second import handle.
          destination_handle2_released = true;
          destination2.reset();

          async::PostTask(dispatcher(), [&]() {
            // Exported node should still be marked as exported.
            auto exported_node = FindResource<EntityNode>(exported_node_id);
            ASSERT_TRUE(exported_node);
            ASSERT_EQ(true, exported_node->is_exported());

            import_node2_released = true;
            EXPECT_TRUE(
                Apply(scenic::NewReleaseResourceCmd(import_node_id2)));
          });
        });
      });
    });
  });

  EXPECT_TRUE(RunLoopUntilIdle());
  ASSERT_TRUE(called);
}

TEST_F(ImportTest,
       ProxiesCanBeFoundByTheirContainerOrTheirUnderlyingEntityType) {
  // Create an unlinked import resource.
  zx::eventpair source, destination;

  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Apply the import command.
  ASSERT_TRUE(Apply(scenic::NewImportResourceCmd(
      1 /* import resource ID */,
      ::fuchsia::ui::gfx::ImportSpec::NODE, /* spec */
      std::move(destination))               /* endpoint */
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
    ASSERT_EQ(::fuchsia::ui::gfx::ImportSpec::NODE, import_node->import_spec());
  }

  // Resolve by the resource owned by the import container.
  {
    // Assert that the import node contains a node with the correct
    // properties.
    auto import_node_backing = FindResource<EntityNode>(1);

    ASSERT_TRUE(import_node_backing);

    // The imported node has the same id as the import resource.
    ASSERT_EQ(1u, import_node_backing->id());
  }
}

TEST_F(ImportTest, UnlinkedImportedResourceCanAcceptCommands) {
  // Create an unlinked import resource.
  zx::eventpair source, destination;
  {
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

    // Apply the import command.
    ASSERT_TRUE(Apply(scenic::NewImportResourceCmd(
        1 /* import resource ID */,
        ::fuchsia::ui::gfx::ImportSpec::NODE, /* spec */
        std::move(destination))               /* endpoint */
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
    ASSERT_EQ(::fuchsia::ui::gfx::ImportSpec::NODE, import_node->import_spec());
  }

  // Attempt to add an entity node as a child to an unlinked resource.
  {
    // Create the entity node.
    ASSERT_TRUE(Apply(
        scenic::NewCreateEntityNodeCmd(2 /* child resource id */)));

    // Add the entity node to the import.
    ASSERT_TRUE(Apply(scenic::NewAddChildCmd(
        1 /* unlinked import resource */, 2 /* child resource */)));
  }
}

TEST_F(ImportTest, LinkedResourceShouldBeAbleToAcceptCommands) {
  // Create the event pair.
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Perform the import
  {
    // Apply the import command.
    ASSERT_TRUE(Apply(scenic::NewImportResourceCmd(
        1 /* import resource ID */,
        ::fuchsia::ui::gfx::ImportSpec::NODE, /* spec */
        std::move(destination))               /* endpoint */
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
    ASSERT_EQ(::fuchsia::ui::gfx::ImportSpec::NODE, import_node->import_spec());
  }

  // Perform the export
  {
    // Create an entity node.
    ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(2)));

    // Assert that the entity node was correctly mapped in.
    ASSERT_EQ(2u, session_->GetMappedResourceCount());

    // Apply the export command.
    ASSERT_TRUE(
        Apply(scenic::NewExportResourceCmd(2, std::move(source))));
  }

  // Bindings should have been resolved.
  {
    // Assert that the import node was setup with the correct properties.
    auto import_node = FindResource<Import>(1);

    ASSERT_TRUE(import_node);

    // Bindings should be resolved by now.
    ASSERT_NE(import_node->imported_resource(), nullptr);

    // Import specs should match.
    ASSERT_EQ(import_node->import_spec(), ::fuchsia::ui::gfx::ImportSpec::NODE);
  }

  // Attempt to add an entity node as a child to an linked resource.
  {
    // Create the entity node.
    ASSERT_TRUE(Apply(
        scenic::NewCreateEntityNodeCmd(3 /* child resource id */)));

    // Add the entity node to the import.
    ASSERT_TRUE(Apply(scenic::NewAddChildCmd(
        1 /* unlinked import resource */, 3 /* child resource */)));
  }
}

TEST_F(ImportTest, EmbedderCanEmbedNodesFromElsewhere) {
  // Create the token pair.
  zx::eventpair import_token, export_token;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &import_token, &export_token));

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
    ASSERT_TRUE(Apply(scenic::NewCreateSceneCmd(1)));
    ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(2)));
    ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(3)));
    ASSERT_TRUE(Apply(scenic::NewAddChildCmd(1, 2)));
    ASSERT_TRUE(Apply(scenic::NewAddChildCmd(2, 3)));

    // Export.
    ASSERT_TRUE(Apply(
        scenic::NewExportResourceCmd(1, std::move(export_token))));
    ASSERT_EQ(1u, engine_->resource_linker()->NumExports());
  }

  // Embeddee.
  {
    ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(1001)));
    ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(1002)));
    ASSERT_TRUE(Apply(scenic::NewCreateEntityNodeCmd(1003)));
    ASSERT_TRUE(Apply(scenic::NewAddChildCmd(1001, 1002)));
    ASSERT_TRUE(Apply(scenic::NewAddChildCmd(1002, 1003)));

    // Import.
    ASSERT_TRUE(Apply(scenic::NewImportResourceCmd(
        500, ::fuchsia::ui::gfx::ImportSpec::NODE, std::move(import_token))));
    ASSERT_TRUE(Apply(scenic::NewAddChildCmd(500, 1001)));
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
}  // namespace gfx
}  // namespace scenic
