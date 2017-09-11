// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/threading/thread.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/synchronization/waitable_event.h"
#include "zircon/system/ulib/zx/include/zx/eventpair.h"

#include "garnet/bin/ui/scene_manager/resources/nodes/entity_node.h"
#include "garnet/bin/ui/scene_manager/tests/session_test.h"
#include "garnet/bin/ui/scene_manager/tests/util.h"
#include "lib/ui/scenic/fidl_helpers.h"
#include "lib/ui/tests/test_with_message_loop.h"

namespace scene_manager {
namespace test {

using ImportTest = SessionTest;
using ImportThreadedTest = SessionThreadedTest;

TEST_F(ImportTest, ExportsResourceViaOp) {
  // Create the event pair.
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Setup the resource to export.
  scenic::ResourceId resource_id = 1;

  // Create an entity node.
  ASSERT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(resource_id)));

  // Assert that the entity node was correctly mapped in.
  ASSERT_EQ(1u, session_->GetMappedResourceCount());

  // Apply the export op.
  ASSERT_TRUE(
      Apply(scenic_lib::NewExportResourceOp(resource_id, std::move(source))));
}

TEST_F(ImportTest, ImportsUnlinkedImportViaOp) {
  // Create the event pair.
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Apply the import op.
  ASSERT_TRUE(Apply(scenic_lib::NewImportResourceOp(
      1 /* import resource ID */, scenic::ImportSpec::NODE, /* spec */
      std::move(destination))                               /* endpoint */
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
  ASSERT_EQ(scenic::ImportSpec::NODE, import_node->import_spec());
}

TEST_F(ImportTest, PerformsFullLinking) {
  // Create the event pair.
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Perform the import
  {
    // Apply the import op.
    ASSERT_TRUE(Apply(scenic_lib::NewImportResourceOp(
        1 /* import resource ID */, scenic::ImportSpec::NODE, /* spec */
        std::move(destination))                               /* endpoint */
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
    ASSERT_EQ(scenic::ImportSpec::NODE, import_node->import_spec());
  }

  // Perform the export
  {
    // Create an entity node.
    ASSERT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(2)));

    // Assert that the entity node was correctly mapped in.
    ASSERT_EQ(2u, session_->GetMappedResourceCount());

    // Apply the export op.
    ASSERT_TRUE(Apply(scenic_lib::NewExportResourceOp(2, std::move(source))));
  }

  // Bindings should have been resolved.
  {
    // Assert that the import node was setup with the correct properties.
    auto import_node = FindResource<Import>(1);

    ASSERT_TRUE(import_node);

    // Bindings should be resolved by now.
    ASSERT_NE(nullptr, import_node->imported_resource());

    // Import specs should match.
    ASSERT_EQ(scenic::ImportSpec::NODE, import_node->import_spec());

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
  ASSERT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(1)));
  EXPECT_FALSE(Apply(scenic_lib::NewExportResourceOp(1 /* resource id */,
                                                     std::move(source_out))));
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
  ASSERT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(1)));
  EXPECT_FALSE(Apply(scenic_lib::NewImportResourceOp(
      1 /* resource id */, scenic::ImportSpec::NODE,
      std::move(destination_out))));
}

TEST_F(ImportTest, DestroyingExportedResourceSendsEvent) {
  zx::eventpair source;
  zx::eventpair destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Export an entity node.
  scenic::ResourceId node_id = 1;
  scenic::ResourceId import_node = 2;
  ASSERT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(node_id)));
  EXPECT_TRUE(
      Apply(scenic_lib::NewExportResourceOp(node_id, std::move(source))));
  EXPECT_TRUE(Apply(scenic_lib::NewImportResourceOp(
      import_node, scenic::ImportSpec::NODE, std::move(destination))));

  // Release the entity node.
  EXPECT_TRUE(Apply(scenic_lib::NewReleaseResourceOp(node_id)));

  // Run the message loop until we get an event.
  RUN_MESSAGE_LOOP_UNTIL(events_.size() > 0);

  // Verify that we got an ImportUnboundEvent.
  EXPECT_EQ(1u, events_.size());
  scenic::EventPtr event = std::move(events_[0]);
  EXPECT_EQ(scenic::Event::Tag::IMPORT_UNBOUND, event->which());
  ASSERT_EQ(import_node, event->get_import_unbound()->resource_id);
}

TEST_F(ImportTest, ImportingNodeAfterDestroyingExportedResourceSendsEvent) {
  zx::eventpair source;
  zx::eventpair destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Export an entity node.
  scenic::ResourceId node_id = 1;
  scenic::ResourceId import_node = 2;
  ASSERT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(node_id)));
  EXPECT_TRUE(
      Apply(scenic_lib::NewExportResourceOp(node_id, std::move(source))));

  // Release the entity node.
  EXPECT_TRUE(Apply(scenic_lib::NewReleaseResourceOp(node_id)));

  // Try to import after the entity node has been released.
  EXPECT_TRUE(Apply(scenic_lib::NewImportResourceOp(
      import_node, scenic::ImportSpec::NODE, std::move(destination))));

  // Run the message loop until we get an event.
  RUN_MESSAGE_LOOP_UNTIL(events_.size() > 0);

  // Verify that we got an ImportUnboundEvent.
  EXPECT_EQ(1u, events_.size());
  scenic::EventPtr event = std::move(events_[0]);
  EXPECT_EQ(scenic::Event::Tag::IMPORT_UNBOUND, event->which());
  ASSERT_EQ(import_node, event->get_import_unbound()->resource_id);
}

TEST_F(ImportThreadedTest, KillingImportedResourceEvictsFromResourceLinker) {
  // Setup a latch on the resource expiring in the linker.
  fxl::AutoResetWaitableEvent import_expired_latch;
  engine_->resource_linker()->SetOnExpiredCallback(
      [this, &import_expired_latch](Resource*,
                                    ResourceLinker::ExpirationCause cause) {
        ASSERT_EQ(ResourceLinker::ExpirationCause::kResourceDestroyed, cause);
        import_expired_latch.Signal();
      });

  zx::eventpair source;

  PostTaskSync([this, &source]() {
    // Create the event pair.
    zx::eventpair destination;
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

    // Apply the import op.
    ASSERT_TRUE(Apply(scenic_lib::NewImportResourceOp(
        1 /* import resource ID */, scenic::ImportSpec::NODE, /* spec */
        std::move(destination))                               /* endpoint */
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
    ASSERT_EQ(scenic::ImportSpec::NODE, import_node->import_spec());

    // Release the import resource.
    ASSERT_TRUE(
        Apply(scenic_lib::NewReleaseResourceOp(1 /* import resource ID */)));
  });

  // Make sure the expiry handle tells us that the resource has expired.
  import_expired_latch.Wait();

  // Assert that the resource linker has removed the unresolved import
  // registration. We have already asserted that the unresolved import was
  // registered in the initial post task.
  ASSERT_EQ(engine_->resource_linker()->NumUnresolvedImports(), 0u);
}

// For a given resource, export it and bind a node to it. Additionally, keep
// an import handle open. Then, verify that the resource is not unexported until
// both the import node and the import handle are released.
TEST_F(ImportThreadedTest, ResourceUnexportedAfterImportsAndImportHandlesDie1) {
  scenic::ResourceId exported_node_id = 1;
  scenic::ResourceId import_node_id = 2;

  bool destination_handle_released = false;
  bool import_node_released = false;

  // Setup a latch on the resource becoming unexported in the linker.
  fxl::AutoResetWaitableEvent export_expired_latch;
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

        export_expired_latch.Signal();
      });

  // Create the event pair.
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  fsl::Thread thread;
  thread.Run();

  thread.TaskRunner()->PostTask([&]() {

    // Create the resource being exported.
    Apply(scenic_lib::NewCreateEntityNodeOp(exported_node_id));
    auto exported_node = FindResource<EntityNode>(exported_node_id);
    ASSERT_TRUE(exported_node);
    ASSERT_EQ(false, exported_node->is_exported());

    // Apply the export op.
    ASSERT_TRUE(Apply(
        scenic_lib::NewExportResourceOp(exported_node_id, std::move(source))));
    ASSERT_EQ(true, exported_node->is_exported());

    // Apply the import op.
    ASSERT_TRUE(Apply(scenic_lib::NewImportResourceOp(
        import_node_id, scenic::ImportSpec::NODE, /* spec */
        CopyEventPair(destination))               /* endpoint */
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

    // Post two tasks in the future. We assume the export will be released
    // after the second one. Post tasks with a slight delay so we can identify
    // the stage accurately.
    thread.TaskRunner()->PostTask([&]() {
      // Release the only import bound to the exported node.
      import_node_released = true;
      EXPECT_TRUE(Apply(scenic_lib::NewReleaseResourceOp(import_node_id)));

      thread.TaskRunner()->PostDelayedTask(
          [&]() {
            // Exported node should still be marked as exported.
            auto exported_node = FindResource<EntityNode>(exported_node_id);
            ASSERT_TRUE(exported_node);
            ASSERT_EQ(true, exported_node->is_exported());
            // List of imports should be empty.
            ASSERT_EQ(0u, exported_node->imports().size());

            // Reset the only import handle.
            destination_handle_released = true;
            destination.reset();
          },
          kPumpMessageLoopDuration);
    });
  });

  // Make sure the expiry handle tells us that the resource has expired.
  export_expired_latch.Wait();

  thread.TaskRunner()->PostTask(
      []() { fsl::MessageLoop::GetCurrent()->QuitNow(); });

  thread.Join();
}

// For a given resource, export it and bind a node to it. Additionally, keep
// an import handle open. Then, verify that the resource is not unexported until
// both the import node and the import handle are released.
// This test is identical to the previous one except the order in which the
// import node and import handle are released is switched.
TEST_F(ImportThreadedTest, ResourceUnexportedAfterImportsAndImportHandlesDie2) {
  scenic::ResourceId exported_node_id = 1;
  scenic::ResourceId import_node_id = 2;

  bool destination_handle_released = false;
  bool import_node_released = false;

  // Setup a latch on the resource becoming unexported in the linker.
  fxl::AutoResetWaitableEvent export_expired_latch;
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

        export_expired_latch.Signal();
      });

  // Create the event pair.
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  fsl::Thread thread;
  thread.Run();

  thread.TaskRunner()->PostTask([&]() {

    // Create the resource being exported.
    Apply(scenic_lib::NewCreateEntityNodeOp(exported_node_id));
    auto exported_node = FindResource<EntityNode>(exported_node_id);
    ASSERT_TRUE(exported_node);
    ASSERT_EQ(false, exported_node->is_exported());

    // Apply the export op.
    ASSERT_TRUE(Apply(
        scenic_lib::NewExportResourceOp(exported_node_id, std::move(source))));
    ASSERT_EQ(true, exported_node->is_exported());

    // Apply the import op.
    ASSERT_TRUE(Apply(scenic_lib::NewImportResourceOp(
        import_node_id, scenic::ImportSpec::NODE, /* spec */
        CopyEventPair(destination))               /* endpoint */
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

    // Post two tasks in the future. We assume the export will be released
    // after the second one. Post tasks with a slight delay so we can identify
    // the stage accurately.
    thread.TaskRunner()->PostTask([&]() {

      // Reset the only import handle.
      destination_handle_released = true;
      destination.reset();
      thread.TaskRunner()->PostDelayedTask(
          [&]() {
            // Exported node should still be marked as exported.
            auto exported_node = FindResource<EntityNode>(exported_node_id);
            ASSERT_TRUE(exported_node);
            ASSERT_EQ(true, exported_node->is_exported());
            ASSERT_EQ(1u, exported_node->imports().size());

            // Release the only import bound to the exported node.
            import_node_released = true;
            EXPECT_TRUE(
                Apply(scenic_lib::NewReleaseResourceOp(import_node_id)));
          },
          kPumpMessageLoopDuration);
    });
  });

  // Make sure the expiry handle tells us that the resource has expired.
  export_expired_latch.Wait();

  thread.TaskRunner()->PostTask(
      []() { fsl::MessageLoop::GetCurrent()->QuitNow(); });

  thread.Join();
}

// For a given resource, export it and bind a node to it. Additionally, keep
// two import handles open. Then, verify that the resource is not unexported
// until both the import node and all the import handles are released. This test
// is identical to the previous one except there is an additional import handle
// that must be destroyed.
TEST_F(ImportThreadedTest, ResourceUnexportedAfterImportsAndImportHandlesDie3) {
  scenic::ResourceId exported_node_id = 1;
  scenic::ResourceId import_node_id = 2;

  bool destination_handle1_released = false;
  bool destination_handle2_released = false;
  bool import_node_released = false;

  // Setup a latch on the resource becoming unexported in the linker.
  fxl::AutoResetWaitableEvent export_expired_latch;
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

        export_expired_latch.Signal();
      });

  // Create the event pair.
  zx::eventpair source, destination1;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination1));
  zx::eventpair destination2 = CopyEventPair(destination1);

  fsl::Thread thread;
  thread.Run();

  thread.TaskRunner()->PostTask([&]() {

    // Create the resource being exported.
    Apply(scenic_lib::NewCreateEntityNodeOp(exported_node_id));
    auto exported_node = FindResource<EntityNode>(exported_node_id);
    ASSERT_TRUE(exported_node);
    ASSERT_EQ(false, exported_node->is_exported());

    // Apply the export op.
    ASSERT_TRUE(Apply(
        scenic_lib::NewExportResourceOp(exported_node_id, std::move(source))));
    ASSERT_EQ(true, exported_node->is_exported());

    // Apply the import op.
    ASSERT_TRUE(Apply(scenic_lib::NewImportResourceOp(
        import_node_id, scenic::ImportSpec::NODE, /* spec */
        CopyEventPair(destination1))              /* endpoint */
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
    thread.TaskRunner()->PostTask([&]() {

      // Reset the first import handle.
      destination_handle1_released = true;
      destination1.reset();

      thread.TaskRunner()->PostDelayedTask(
          [&]() {
            // Exported node should still be marked as exported.
            auto exported_node = FindResource<EntityNode>(exported_node_id);
            ASSERT_TRUE(exported_node);
            ASSERT_EQ(true, exported_node->is_exported());

            // Release the only import bound to the exported node.
            import_node_released = true;
            EXPECT_TRUE(
                Apply(scenic_lib::NewReleaseResourceOp(import_node_id)));

            thread.TaskRunner()->PostDelayedTask(
                [&]() {
                  // Exported node should still be marked as exported.
                  auto exported_node =
                      FindResource<EntityNode>(exported_node_id);
                  ASSERT_TRUE(exported_node);
                  ASSERT_EQ(true, exported_node->is_exported());
                  // List of imports should be empty.
                  ASSERT_EQ(0u, exported_node->imports().size());

                  // Reset the second import handle.
                  destination_handle2_released = true;
                  destination2.reset();
                },
                kPumpMessageLoopDuration);
          },
          kPumpMessageLoopDuration);
    });
  });

  // Make sure the expiry handle tells us that the resource has expired.
  export_expired_latch.Wait();

  thread.TaskRunner()->PostTask(
      []() { fsl::MessageLoop::GetCurrent()->QuitNow(); });

  thread.Join();
}

// For a given resource, export it and bind two nodes to it. Additionally, keep
// two import handles open. Then, verify that the resource is not unexported
// until both the import nodes and all the import handles are released. This
// test is identical to the previous one except there is an additional import
// node that must be released.
TEST_F(ImportThreadedTest, ResourceUnexportedAfterImportsAndImportHandlesDie4) {
  scenic::ResourceId exported_node_id = 1;
  scenic::ResourceId import_node_id1 = 2;
  scenic::ResourceId import_node_id2 = 3;

  bool destination_handle1_released = false;
  bool destination_handle2_released = false;
  bool import_node1_released = false;
  bool import_node2_released = false;

  // Setup a latch on the resource becoming unexported in the linker.
  fxl::AutoResetWaitableEvent export_expired_latch;
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

        export_expired_latch.Signal();
      });

  // Create the event pair.
  zx::eventpair source, destination1;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination1));
  zx::eventpair destination2 = CopyEventPair(destination1);

  fsl::Thread thread;
  thread.Run();

  thread.TaskRunner()->PostTask([&]() {

    // Create the resource being exported.
    Apply(scenic_lib::NewCreateEntityNodeOp(exported_node_id));
    auto exported_node = FindResource<EntityNode>(exported_node_id);
    ASSERT_TRUE(exported_node);
    ASSERT_EQ(false, exported_node->is_exported());

    // Apply the export op.
    ASSERT_TRUE(Apply(
        scenic_lib::NewExportResourceOp(exported_node_id, std::move(source))));
    ASSERT_EQ(true, exported_node->is_exported());

    // Apply the import ops.
    ASSERT_TRUE(Apply(scenic_lib::NewImportResourceOp(
        import_node_id1, scenic::ImportSpec::NODE, /* spec */
        CopyEventPair(destination1))               /* endpoint */
    ));
    ASSERT_TRUE(Apply(scenic_lib::NewImportResourceOp(
        import_node_id2, scenic::ImportSpec::NODE, /* spec */
        CopyEventPair(destination1))               /* endpoint */
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
    thread.TaskRunner()->PostTask([&]() {

      // Reset the first import handle.
      destination_handle1_released = true;
      destination1.reset();

      thread.TaskRunner()->PostDelayedTask(
          [&]() {
            // Exported node should still be marked as exported.
            auto exported_node = FindResource<EntityNode>(exported_node_id);
            ASSERT_TRUE(exported_node);
            ASSERT_EQ(true, exported_node->is_exported());

            // Release the only import bound to the exported node.
            import_node1_released = true;
            EXPECT_TRUE(
                Apply(scenic_lib::NewReleaseResourceOp(import_node_id1)));

            thread.TaskRunner()->PostDelayedTask(
                [&]() {
                  // Exported node should still be marked as exported.
                  auto exported_node =
                      FindResource<EntityNode>(exported_node_id);
                  ASSERT_TRUE(exported_node);
                  ASSERT_EQ(true, exported_node->is_exported());

                  // One import should remain bound.
                  ASSERT_EQ(1u, exported_node->imports().size());

                  // Reset the second import handle.
                  destination_handle2_released = true;
                  destination2.reset();

                  thread.TaskRunner()->PostDelayedTask(
                      [&]() {
                        // Exported node should still be marked as exported.
                        auto exported_node =
                            FindResource<EntityNode>(exported_node_id);
                        ASSERT_TRUE(exported_node);
                        ASSERT_EQ(true, exported_node->is_exported());

                        import_node2_released = true;
                        EXPECT_TRUE(Apply(
                            scenic_lib::NewReleaseResourceOp(import_node_id2)));
                      },
                      kPumpMessageLoopDuration);
                },
                kPumpMessageLoopDuration);
          },
          kPumpMessageLoopDuration);
    });
  });

  // Make sure the expiry handle tells us that the resource has expired.
  export_expired_latch.Wait();

  thread.TaskRunner()->PostTask(
      []() { fsl::MessageLoop::GetCurrent()->QuitNow(); });

  thread.Join();
}

TEST_F(ImportTest,
       ProxiesCanBeFoundByTheirContainerOrTheirUnderlyingEntityType) {
  // Create an unlinked import resource.
  zx::eventpair source, destination;

  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Apply the import op.
  ASSERT_TRUE(Apply(scenic_lib::NewImportResourceOp(
      1 /* import resource ID */, scenic::ImportSpec::NODE, /* spec */
      std::move(destination))                               /* endpoint */
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
    ASSERT_EQ(scenic::ImportSpec::NODE, import_node->import_spec());
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

TEST_F(ImportTest, UnlinkedImportedResourceCanAcceptOps) {
  // Create an unlinked import resource.
  zx::eventpair source, destination;
  {
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

    // Apply the import op.
    ASSERT_TRUE(Apply(scenic_lib::NewImportResourceOp(
        1 /* import resource ID */, scenic::ImportSpec::NODE, /* spec */
        std::move(destination))                               /* endpoint */
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
    ASSERT_EQ(scenic::ImportSpec::NODE, import_node->import_spec());
  }

  // Attempt to add an entity node as a child to an unlinked resource.
  {
    // Create the entity node.
    ASSERT_TRUE(
        Apply(scenic_lib::NewCreateEntityNodeOp(2 /* child resource id */)));

    // Add the entity node to the import.
    ASSERT_TRUE(Apply(scenic_lib::NewAddChildOp(
        1 /* unlinked import resource */, 2 /* child resource */)));
  }
}

TEST_F(ImportTest, LinkedResourceShouldBeAbleToAcceptOps) {
  // Create the event pair.
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  // Perform the import
  {
    // Apply the import op.
    ASSERT_TRUE(Apply(scenic_lib::NewImportResourceOp(
        1 /* import resource ID */, scenic::ImportSpec::NODE, /* spec */
        std::move(destination))                               /* endpoint */
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
    ASSERT_EQ(scenic::ImportSpec::NODE, import_node->import_spec());
  }

  // Perform the export
  {
    // Create an entity node.
    ASSERT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(2)));

    // Assert that the entity node was correctly mapped in.
    ASSERT_EQ(2u, session_->GetMappedResourceCount());

    // Apply the export op.
    ASSERT_TRUE(Apply(scenic_lib::NewExportResourceOp(2, std::move(source))));
  }

  // Bindings should have been resolved.
  {
    // Assert that the import node was setup with the correct properties.
    auto import_node = FindResource<Import>(1);

    ASSERT_TRUE(import_node);

    // Bindings should be resolved by now.
    ASSERT_NE(import_node->imported_resource(), nullptr);

    // Import specs should match.
    ASSERT_EQ(import_node->import_spec(), scenic::ImportSpec::NODE);
  }

  // Attempt to add an entity node as a child to an linked resource.
  {
    // Create the entity node.
    ASSERT_TRUE(
        Apply(scenic_lib::NewCreateEntityNodeOp(3 /* child resource id */)));

    // Add the entity node to the import.
    ASSERT_TRUE(Apply(scenic_lib::NewAddChildOp(
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
    ASSERT_TRUE(Apply(scenic_lib::NewCreateSceneOp(1)));
    ASSERT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(2)));
    ASSERT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(3)));
    ASSERT_TRUE(Apply(scenic_lib::NewAddChildOp(1, 2)));
    ASSERT_TRUE(Apply(scenic_lib::NewAddChildOp(2, 3)));

    // Export.
    ASSERT_TRUE(
        Apply(scenic_lib::NewExportResourceOp(1, std::move(export_token))));
    ASSERT_EQ(1u, engine_->resource_linker()->NumExports());
  }

  // Embeddee.
  {
    ASSERT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(1001)));
    ASSERT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(1002)));
    ASSERT_TRUE(Apply(scenic_lib::NewCreateEntityNodeOp(1003)));
    ASSERT_TRUE(Apply(scenic_lib::NewAddChildOp(1001, 1002)));
    ASSERT_TRUE(Apply(scenic_lib::NewAddChildOp(1002, 1003)));

    // Import.
    ASSERT_TRUE(Apply(scenic_lib::NewImportResourceOp(
        500, scenic::ImportSpec::NODE, std::move(import_token))));
    ASSERT_TRUE(Apply(scenic_lib::NewAddChildOp(500, 1001)));
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
}  // namespace scene_manager
