// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/eventpair.h>

#include "garnet/lib/ui/gfx/engine/resource_linker.h"
#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "garnet/lib/ui/gfx/tests/util.h"

#include "lib/fsl/handles/object_info.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/ui/scenic/cpp/commands.h"

namespace scenic {
namespace gfx {
namespace test {

using ResourceLinkerTest = SessionTest;

TEST_F(ResourceLinkerTest, AllowsExport) {
  ResourceLinker* linker = engine_->resource_linker();
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  auto resource =
      fxl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);

  ASSERT_TRUE(linker->ExportResource(resource.get(), std::move(source)));

  ASSERT_EQ(1u, linker->NumExports());
}

TEST_F(ResourceLinkerTest, AllowsImport) {
  ResourceLinker* linker = engine_->resource_linker();

  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  auto exported =
      fxl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);
  ASSERT_EQ(1u, session_->GetTotalResourceCount());
  ASSERT_TRUE(linker->ExportResource(exported.get(), std::move(source)));

  ASSERT_EQ(1u, linker->NumExports());

  ASSERT_EQ(1u, session_->GetTotalResourceCount());

  bool did_resolve = false;

  // SetOnImportResolvedCallback should not capture reference to exported
  // node.
  linker->SetOnImportResolvedCallback(
      [exported_ptr = exported.get(), &did_resolve](
          Import*, Resource* resource, ImportResolutionResult cause) -> void {
        did_resolve = true;
        ASSERT_TRUE(resource);
        ASSERT_EQ(exported_ptr, resource);
        ASSERT_NE(0u, resource->type_flags() & kEntityNode);
        ASSERT_EQ(ImportResolutionResult::kSuccess, cause);
      });
  ImportPtr import = fxl::MakeRefCounted<Import>(
      session_.get(), 2, ::fuchsia::ui::gfx::ImportSpec::NODE);
  // Expect three resources: The exported node, import, and the import's
  // delegate.
  ASSERT_EQ(3u, session_->GetTotalResourceCount());
  linker->ImportResource(import.get(),
                         ::fuchsia::ui::gfx::ImportSpec::NODE,  // import spec
                         std::move(destination));               // import handle

  ASSERT_EQ(3u, session_->GetTotalResourceCount());

  // Make sure the closure and its assertions are not skipped.
  ASSERT_TRUE(did_resolve);
  ASSERT_EQ(1u, linker->NumExports());
  ASSERT_EQ(0u, linker->NumUnresolvedImports());
}

TEST_F(ResourceLinkerTest, CannotImportWithDeadSourceAndDestinationHandles) {
  ResourceLinker* linker = engine_->resource_linker();

  zx::eventpair destination_out;
  {
    zx::eventpair destination;
    zx::eventpair source;
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));
    destination_out = zx::eventpair{destination.get()};
    // source and destination dies now.
  }

  bool did_resolve = false;
  linker->SetOnImportResolvedCallback(
      [&did_resolve](Import* import, Resource* resource,
                     ImportResolutionResult cause) -> void {
        did_resolve = true;
      });
  ImportPtr import = fxl::MakeRefCounted<Import>(
      session_.get(), 1, ::fuchsia::ui::gfx::ImportSpec::NODE);
  ASSERT_FALSE(linker->ImportResource(
      import.get(),
      ::fuchsia::ui::gfx::ImportSpec::NODE,  // import spec
      std::move(destination_out)));          // import handle

  ASSERT_EQ(0u, linker->NumUnresolvedImports());
  ASSERT_FALSE(did_resolve);
}

TEST_F(ResourceLinkerTest, CannotImportWithDeadDestinationHandles) {
  ResourceLinker* linker = engine_->resource_linker();

  zx::eventpair destination_out;
  zx::eventpair source;
  {
    zx::eventpair destination;

    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));
    destination_out = zx::eventpair{destination.get()};
    // destination dies now.
  }

  bool did_resolve = false;
  linker->SetOnImportResolvedCallback(
      [&did_resolve](Import* import, Resource* resource,
                     ImportResolutionResult cause) -> void {
        did_resolve = true;
      });
  ImportPtr import = fxl::MakeRefCounted<Import>(
      session_.get(), 1, ::fuchsia::ui::gfx::ImportSpec::NODE);
  ASSERT_FALSE(linker->ImportResource(
      import.get(),
      ::fuchsia::ui::gfx::ImportSpec::NODE,  // import spec
      std::move(destination_out)));          // import handle

  ASSERT_EQ(0u, linker->NumUnresolvedImports());
  ASSERT_FALSE(did_resolve);
}

TEST_F(ResourceLinkerTest, CanImportWithDeadSourceHandle) {
  zx::eventpair destination;
  zx::eventpair source_out;
  {
    zx::eventpair source;

    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));
    source_out = zx::eventpair{source.get()};
    // source dies now.
  }

  ResourceLinker* linker = engine_->resource_linker();
  scenic::gfx::ResourcePtr resource;
  ImportPtr import;
  bool expiry_cb_called = false;

  async::PostTask(
      dispatcher(),
      fxl::MakeCopyable([this, &import, &resource, linker, &expiry_cb_called,
                         destination = std::move(destination)]() mutable {
        // Set an expiry callback that checks the resource expired for the right
        // reason and signal the latch.
        linker->SetOnExpiredCallback(
            [&linker, &expiry_cb_called](
                Resource*, ResourceLinker::ExpirationCause cause) {
              ASSERT_EQ(ResourceLinker::ExpirationCause::kExportTokenClosed,
                        cause);
              ASSERT_EQ(0u, linker->NumUnresolvedImports());
              ASSERT_EQ(0u, linker->NumExports());
              expiry_cb_called = true;
            });

        bool did_resolve = false;
        linker->SetOnImportResolvedCallback(
            [&did_resolve](Import* import, Resource* resource,
                           ImportResolutionResult cause) -> void {
              did_resolve = true;
            });
        import = fxl::MakeRefCounted<Import>(
            session_.get(), 1, ::fuchsia::ui::gfx::ImportSpec::NODE);
        ASSERT_TRUE(linker->ImportResource(
            import.get(),
            ::fuchsia::ui::gfx::ImportSpec::NODE,  // import spec
            std::move(destination)));              // import handle

        ASSERT_EQ(1u, linker->NumUnresolvedImports());
        ASSERT_FALSE(did_resolve);
      }));

  EXPECT_TRUE(RunLoopUntilIdle());
  ASSERT_TRUE(expiry_cb_called);
}

TEST_F(ResourceLinkerTest, CannotExportWithDeadSourceAndDestinationHandles) {
  ResourceLinker* linker = engine_->resource_linker();

  zx::eventpair source_out;
  {
    zx::eventpair destination;
    zx::eventpair source;
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));
    source_out = zx::eventpair{source.get()};
    // source and destination dies now.
  }

  auto resource =
      fxl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);
  ASSERT_FALSE(linker->ExportResource(resource.get(), std::move(source_out)));
  ASSERT_EQ(0u, linker->NumExports());
}

TEST_F(ResourceLinkerTest, CannotExportWithDeadSourceHandle) {
  ResourceLinker* linker = engine_->resource_linker();

  zx::eventpair destination;
  zx::eventpair source_out;
  {
    zx::eventpair source;
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));
    source_out = zx::eventpair{source.get()};
    // source dies now.
  }

  auto resource =
      fxl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);

  ASSERT_FALSE(linker->ExportResource(resource.get(), std::move(source_out)));
  ASSERT_EQ(0u, linker->NumExports());
}

// Related koid of the source handle is valid as long as the source handle
// itself is valid (i.e. it doesn't matter if the destination handle is dead).
TEST_F(ResourceLinkerTest, CanExportWithDeadDestinationHandle) {
  ResourceLinker* linker = engine_->resource_linker();

  zx::eventpair source;
  {
    zx::eventpair destination;
    ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));
    // destination dies now.
  }

  scenic::gfx::ResourcePtr resource;
  bool called = false;

  async::PostTask(
      dispatcher(),
      fxl::MakeCopyable([this, &resource, linker, source = std::move(source),
                         &called]() mutable {
        resource = fxl::MakeRefCounted<EntityNode>(session_.get(),
                                                   1 /* resource id */);

        ASSERT_TRUE(linker->ExportResource(resource.get(), std::move(source)));
        ASSERT_EQ(1u, linker->NumExports());

        // Set an expiry callback that checks the resource expired for the right
        // reason and signal the latch.
        linker->SetOnExpiredCallback(
            [linker, &called](Resource*,
                              ResourceLinker::ExpirationCause cause) {
              ASSERT_EQ(ResourceLinker::ExpirationCause::kNoImportsBound,
                        cause);
              ASSERT_EQ(0u, linker->NumUnresolvedImports());
              ASSERT_EQ(0u, linker->NumExports());
              called = true;
            });
      }));
  EXPECT_TRUE(RunLoopUntilIdle());
  ASSERT_TRUE(called);
}

TEST_F(ResourceLinkerTest,
       DestinationHandleDeathAutomaticallyCleansUpResourceExport) {
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  ResourceLinker* linker = engine_->resource_linker();
  scenic::gfx::ResourcePtr resource;
  bool called = false;

  async::PostTask(
      dispatcher(),
      fxl::MakeCopyable([this, &resource, linker, source = std::move(source),
                         &destination, &called]() mutable {
        // Register the resource.
        resource = fxl::MakeRefCounted<EntityNode>(session_.get(),
                                                   1 /* resource id */);

        ASSERT_TRUE(linker->ExportResource(resource.get(), std::move(source)));
        ASSERT_EQ(1u, linker->NumExports());

        // Set an expiry callback that checks the resource expired for the right
        // reason and signal the latch.
        linker->SetOnExpiredCallback(
            [linker, &called](Resource*,
                              ResourceLinker::ExpirationCause cause) {
              ASSERT_EQ(ResourceLinker::ExpirationCause::kNoImportsBound,
                        cause);
              ASSERT_EQ(0u, linker->NumExports());
              called = true;
            });

        // Release the destination handle.
        destination.reset();
      }));

  EXPECT_TRUE(RunLoopUntilIdle());
  ASSERT_TRUE(called);
}

TEST_F(ResourceLinkerTest,
       SourceHandleDeathAutomaticallyCleansUpUnresolvedImports) {
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  ResourceLinker* linker = engine_->resource_linker();
  scenic::gfx::ResourcePtr resource;
  ImportPtr import;
  bool did_resolve = false;

  async::PostTask(
      dispatcher(), fxl::MakeCopyable([this, &import, &resource, linker,
                                       source = std::move(source), &destination,
                                       &did_resolve]() mutable {
        // Register the resource.
        resource = fxl::MakeRefCounted<EntityNode>(session_.get(),
                                                   1 /* resource id */);

        // Import.
        linker->SetOnImportResolvedCallback(
            [&did_resolve, linker](Import* import, Resource* resource,
                                   ImportResolutionResult cause) -> void {
              did_resolve = true;
              ASSERT_FALSE(resource);
              ASSERT_EQ(ImportResolutionResult::kExportHandleDiedBeforeBind,
                        cause);
              ASSERT_EQ(0u, linker->NumUnresolvedImports());
            });

        import = fxl::MakeRefCounted<Import>(
            session_.get(), 2, ::fuchsia::ui::gfx::ImportSpec::NODE);
        linker->ImportResource(
            import.get(),
            ::fuchsia::ui::gfx::ImportSpec::NODE,  // import spec
            CopyEventPair(destination));           // import handle

        ASSERT_EQ(1u, linker->NumUnresolvedImports());

        // Release both destination and source handles.
        destination.reset();
        source.reset();
      }));

  EXPECT_TRUE(RunLoopUntilIdle());
  ASSERT_TRUE(did_resolve);
}

TEST_F(ResourceLinkerTest, ResourceDeathAutomaticallyCleansUpResourceExport) {
  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  ResourceLinker* linker = engine_->resource_linker();
  bool called = false;

  async::PostTask(
      dispatcher(), fxl::MakeCopyable([this, linker, source = std::move(source),
                                       &destination, &called]() mutable {
        // Register the resource.
        auto resource = fxl::MakeRefCounted<EntityNode>(session_.get(),
                                                        1 /* resource id */);
        ASSERT_TRUE(linker->ExportResource(resource.get(), std::move(source)));
        ASSERT_EQ(1u, linker->NumExports());

        // Set an expiry callback that checks the resource expired for the right
        // reason and signal the latch.
        linker->SetOnExpiredCallback(
            [linker, &called](Resource*,
                              ResourceLinker::ExpirationCause cause) {
              ASSERT_EQ(ResourceLinker::ExpirationCause::kResourceDestroyed,
                        cause);
              ASSERT_EQ(0u, linker->NumExports());
              called = true;
            });

        // |resource| gets destroyed now since its out of scope.
      }));

  EXPECT_TRUE(RunLoopUntilIdle());
  ASSERT_TRUE(called);
}

TEST_F(ResourceLinkerTest, ImportsBeforeExportsAreServiced) {
  ResourceLinker* linker = engine_->resource_linker();

  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  auto exported =
      fxl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);

  // Import.
  bool did_resolve = false;
  linker->SetOnImportResolvedCallback(
      [exported_ptr = exported.get(), &did_resolve](
          Import* import, Resource* resource,
          ImportResolutionResult cause) -> void {
        did_resolve = true;
        ASSERT_TRUE(resource);
        ASSERT_EQ(exported_ptr, resource);
        ASSERT_NE(0u, resource->type_flags() & kEntityNode);
        ASSERT_EQ(ImportResolutionResult::kSuccess, cause);
      });
  ImportPtr import = fxl::MakeRefCounted<Import>(
      session_.get(), 2, ::fuchsia::ui::gfx::ImportSpec::NODE);
  linker->ImportResource(import.get(),
                         ::fuchsia::ui::gfx::ImportSpec::NODE,  // import spec
                         std::move(destination));               // import handle

  ASSERT_FALSE(did_resolve);
  ASSERT_EQ(0u, linker->NumExports());
  ASSERT_EQ(1u, linker->NumUnresolvedImports());

  // Export.
  ASSERT_TRUE(linker->ExportResource(exported.get(), std::move(source)));
  ASSERT_EQ(1u, linker->NumExports());  // Since we already have the
                                        // destination handle in scope.
  ASSERT_EQ(0u, linker->NumUnresolvedImports());
  ASSERT_TRUE(did_resolve);
}

TEST_F(ResourceLinkerTest, ImportAfterReleasedExportedResourceFails) {
  ResourceLinker* linker = engine_->resource_linker();

  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  bool did_resolve = false;
  {
    auto exported =
        fxl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);

    // Import.
    linker->SetOnImportResolvedCallback(
        [&did_resolve](Import* import, Resource* resource,
                       ImportResolutionResult cause) -> void {
          did_resolve = true;
          ASSERT_EQ(nullptr, resource);
          ASSERT_EQ(ImportResolutionResult::kExportHandleDiedBeforeBind, cause);
        });

    // Export.
    ASSERT_TRUE(linker->ExportResource(exported.get(), std::move(source)));
    ASSERT_EQ(1u, linker->NumExports());  // Since we already have the
                                          // destination handle in scope.
    ASSERT_EQ(0u, linker->NumUnresolvedImports());

    // Release the exported resource.
  }
  ASSERT_EQ(0u, linker->NumExports());

  // Now try to import. We should get a resolution callback that it failed.
  ImportPtr import = fxl::MakeRefCounted<Import>(
      session_.get(), 2, ::fuchsia::ui::gfx::ImportSpec::NODE);
  linker->ImportResource(import.get(),
                         ::fuchsia::ui::gfx::ImportSpec::NODE,  // import spec
                         std::move(destination));               // import handle
  RunLoopUntilIdle();
  ASSERT_TRUE(did_resolve);
  ASSERT_EQ(0u, linker->NumUnresolvedImports());
}

TEST_F(ResourceLinkerTest, DuplicatedDestinationHandlesAllowMultipleImports) {
  ResourceLinker* linker = engine_->resource_linker();

  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  auto exported =
      fxl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);

  // Import multiple times.
  size_t resolution_count = 0;
  linker->SetOnImportResolvedCallback(
      [exported_ptr = exported.get(), &resolution_count](
          Import* import, Resource* resource,
          ImportResolutionResult cause) -> void {
        ASSERT_EQ(ImportResolutionResult::kSuccess, cause);
        resolution_count++;
        ASSERT_TRUE(resource);
        ASSERT_EQ(exported_ptr, resource);
        ASSERT_NE(0u, resource->type_flags() & kEntityNode);
      });

  static const size_t kImportCount = 100;

  std::vector<ImportPtr> imports;
  for (size_t i = 1; i <= kImportCount; ++i) {
    zx::eventpair duplicate_destination = CopyEventPair(destination);

    ImportPtr import = fxl::MakeRefCounted<Import>(
        session_.get(), i + 1, ::fuchsia::ui::gfx::ImportSpec::NODE);
    // Need to keep the import alive.
    imports.push_back(import);
    linker->ImportResource(import.get(),
                           ::fuchsia::ui::gfx::ImportSpec::NODE,  // import spec
                           std::move(duplicate_destination));  // import handle

    ASSERT_EQ(0u, resolution_count);
    ASSERT_EQ(0u, linker->NumExports());
    ASSERT_EQ(i, linker->NumUnresolvedImports());
  }

  // Export.
  ASSERT_TRUE(linker->ExportResource(exported.get(), std::move(source)));
  ASSERT_EQ(1u, linker->NumExports());  // Since we already have the
                                        // destination handle in scope.
  ASSERT_EQ(0u, linker->NumUnresolvedImports());
  ASSERT_EQ(kImportCount, resolution_count);
}

TEST_F(ResourceLinkerTest, UnresolvedImportIsRemovedIfDestroyed) {
  ResourceLinker* linker = engine_->resource_linker();

  zx::eventpair source, destination;
  ASSERT_EQ(ZX_OK, zx::eventpair::create(0, &source, &destination));

  auto exported =
      fxl::MakeRefCounted<EntityNode>(session_.get(), 1 /* resource id */);

  // Import multiple times.
  size_t resolution_count = 0;
  linker->SetOnImportResolvedCallback(
      [exported_ptr = exported.get(), &resolution_count](
          Import* import, Resource* resource,
          ImportResolutionResult cause) -> void {
        ASSERT_EQ(ImportResolutionResult::kImportDestroyedBeforeBind, cause);
        resolution_count++;
      });

  static const size_t kImportCount = 2;

  for (size_t i = 1; i <= kImportCount; ++i) {
    zx::eventpair duplicate_destination = CopyEventPair(destination);

    ImportPtr import = fxl::MakeRefCounted<Import>(
        session_.get(), i + 1, ::fuchsia::ui::gfx::ImportSpec::NODE);
    linker->ImportResource(import.get(),
                           ::fuchsia::ui::gfx::ImportSpec::NODE,  // import spec
                           std::move(duplicate_destination));  // import handle

    ASSERT_EQ(0u, linker->NumExports());
    ASSERT_EQ(1u, linker->NumUnresolvedImports());
  }

  ASSERT_EQ(0u, linker->NumUnresolvedImports());
  ASSERT_EQ(kImportCount, resolution_count);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic
