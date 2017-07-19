// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/resources/resource_linker.h"
#include "apps/mozart/lib/scene/session_helpers.h"
#include "apps/mozart/src/scene_manager/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene_manager/tests/session_test.h"
#include "gtest/gtest.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/synchronization/waitable_event.h"
#include "lib/mtl/handles/object_info.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/thread.h"
#include "magenta/system/ulib/mx/include/mx/eventpair.h"

namespace scene_manager {
namespace test {

using ResourceLinkerTest = SessionTest;

TEST_F(ResourceLinkerTest, AllowsExport) {
  ResourceLinker linker;

  mx::eventpair source, destination;
  ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));

  auto resource =
      ftl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);

  ASSERT_TRUE(linker.ExportResource(resource, std::move(source)));

  ASSERT_EQ(1u, linker.UnresolvedExports());
}

TEST_F(ResourceLinkerTest, AllowsImport) {
  ResourceLinker linker;

  mx::eventpair source, destination;
  ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));

  auto exported =
      ftl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);

  ASSERT_TRUE(linker.ExportResource(exported, std::move(source)));

  ASSERT_EQ(1u, linker.UnresolvedExports());

  bool did_resolve = false;
  ResourceLinker::OnImportResolvedCallback resolution_handler =
      [exported, &did_resolve](ResourcePtr resource,
                               ResourceLinker::ResolutionResult cause) -> void {
    did_resolve = true;
    ASSERT_TRUE(resource);
    ASSERT_EQ(exported, resource);
    ASSERT_NE(0u, resource->type_flags() & kEntityNode);
    ASSERT_EQ(ResourceLinker::ResolutionResult::kSuccess, cause);
  };

  linker.ImportResource(mozart2::ImportSpec::NODE,  // import spec
                        std::move(destination),     // import handle
                        resolution_handler          // import resolution handler
                        );

  // Make sure the closure and its assertions are not skipped.
  ASSERT_TRUE(did_resolve);
  ASSERT_EQ(1u, linker.UnresolvedExports());
  ASSERT_EQ(0u, linker.UnresolvedImports());
}

TEST_F(ResourceLinkerTest, CannotExportWithDeadSourceHandle) {
  ResourceLinker linker;

  mx::eventpair destination;
  mx::eventpair source_out;
  {
    mx::eventpair source;
    ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));
    source_out = mx::eventpair{source.get()};
    // source dies now.
  }

  auto resource =
      ftl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);
  ASSERT_FALSE(linker.ExportResource(resource, std::move(source_out)));
  ASSERT_EQ(0u, linker.UnresolvedExports());
}

// TODO(chinmaygarde): Figure out how the related koid if valid when we have
// killed the destination.
TEST_F(ResourceLinkerTest, DISABLED_CannotExportWithDeadDestinationHandle) {
  ResourceLinker linker;

  mx::eventpair source;
  {
    mx::eventpair destination;
    ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));
    // destination dies now.
  }

  auto resource =
      ftl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);
  ASSERT_FALSE(linker.ExportResource(resource, std::move(source)));
  ASSERT_EQ(0u, linker.UnresolvedExports());
}

TEST_F(ResourceLinkerTest,
       DestinationHandleDeathAutomaticallyCleansUpResource) {
  mx::eventpair source, destination;
  ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));

  mtl::Thread thread;
  thread.Run();

  ftl::AutoResetWaitableEvent latch;
  ResourceLinker linker;

  thread.TaskRunner()->PostTask(ftl::MakeCopyable([
    this, &linker, &latch, source = std::move(source), &destination
  ]() mutable {
    // Register the resource.
    auto resource =
        ftl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);
    ASSERT_TRUE(linker.ExportResource(resource, std::move(source)));
    ASSERT_EQ(1u, linker.UnresolvedExports());

    // Set an expiry callback that checks the resource expired for the right
    // reason and signal the latch.
    linker.SetOnExpiredCallback([&linker, &latch](
                                    ResourcePtr resource,
                                    ResourceLinker::ExpirationCause cause) {
      ASSERT_EQ(ResourceLinker::ExpirationCause::kImportHandleClosed, cause);
      ASSERT_EQ(0u, linker.UnresolvedExports());
      latch.Signal();
    });

    // Release the destination handle.
    destination.reset();
  }));

  latch.Wait();

  thread.TaskRunner()->PostTask(
      []() { mtl::MessageLoop::GetCurrent()->QuitNow(); });

  thread.Join();
}

TEST_F(ResourceLinkerTest, ImportsBeforeExportsAreServiced) {
  ResourceLinker linker;

  mx::eventpair source, destination;
  ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));

  auto exported =
      ftl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);

  // Import.
  bool did_resolve = false;
  ResourceLinker::OnImportResolvedCallback resolution_handler =
      [exported, &did_resolve](ResourcePtr resource,
                               ResourceLinker::ResolutionResult cause) -> void {
    did_resolve = true;
    ASSERT_TRUE(resource);
    ASSERT_EQ(exported, resource);
    ASSERT_NE(0u, resource->type_flags() & kEntityNode);
    ASSERT_EQ(ResourceLinker::ResolutionResult::kSuccess, cause);
  };
  linker.ImportResource(mozart2::ImportSpec::NODE,  // import spec
                        std::move(destination),     // import handle
                        resolution_handler          // import resolution handler
                        );
  ASSERT_FALSE(did_resolve);
  ASSERT_EQ(0u, linker.UnresolvedExports());
  ASSERT_EQ(1u, linker.UnresolvedImports());

  // Export.
  ASSERT_TRUE(linker.ExportResource(exported, std::move(source)));
  ASSERT_EQ(1u, linker.UnresolvedExports());  // Since we already have the
                                              // destination handle in scope.
  ASSERT_EQ(0u, linker.UnresolvedImports());
  ASSERT_TRUE(did_resolve);
}

TEST_F(ResourceLinkerTest, DuplicatedDestinationHandlesAllowMultipleImports) {
  ResourceLinker linker;

  mx::eventpair source, destination;
  ASSERT_EQ(MX_OK, mx::eventpair::create(0, &source, &destination));

  auto exported =
      ftl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);

  // Import multiple times.
  size_t resolution_count = 0;
  ResourceLinker::OnImportResolvedCallback resolution_handler =
      [exported, &resolution_count](
          ResourcePtr resource,
          ResourceLinker::ResolutionResult cause) -> void {
    ASSERT_EQ(ResourceLinker::ResolutionResult::kSuccess, cause);
    resolution_count++;
    ASSERT_TRUE(resource);
    ASSERT_EQ(exported, resource);
    ASSERT_NE(0u, resource->type_flags() & kEntityNode);
  };

  static const size_t kImportCount = 100;

  for (size_t i = 1; i <= kImportCount; ++i) {
    mx::eventpair duplicate_destination;
    ASSERT_EQ(
        destination.duplicate(MX_RIGHT_SAME_RIGHTS, &duplicate_destination),
        MX_OK);
    linker.ImportResource(mozart2::ImportSpec::NODE,         // import spec
                          std::move(duplicate_destination),  // import handle
                          resolution_handler  // import resolution handler
                          );
    ASSERT_EQ(0u, resolution_count);
    ASSERT_EQ(0u, linker.UnresolvedExports());
    ASSERT_EQ(i, linker.UnresolvedImports());
  }

  // Export.
  ASSERT_TRUE(linker.ExportResource(exported, std::move(source)));
  ASSERT_EQ(1u, linker.UnresolvedExports());  // Since we already have the
                                              // destination handle in scope.
  ASSERT_EQ(0u, linker.UnresolvedImports());
  ASSERT_EQ(kImportCount, resolution_count);
}

}  // namespace test
}  // namespace scene_manager
