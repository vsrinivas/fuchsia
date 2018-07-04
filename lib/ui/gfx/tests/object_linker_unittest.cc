// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/object_linker.h"

#include <lib/fit/function.h>
#include <lib/zx/eventpair.h>
#include <zircon/types.h>

#include "garnet/lib/ui/gfx/tests/error_reporting_test.h"
#include "gtest/gtest.h"

namespace scenic {
namespace gfx {
namespace test {

#define ERROR_IF_CALLED(str) \
  std::bind(                 \
      []() { EXPECT_TRUE(false) << "Delegate called unexpectedly: " << str; })

class ObjectLinkerTest : public ErrorReportingTest {
 protected:
  struct TestExportObj;
  struct TestImportObj;
  using TestObjectLinker = ObjectLinker<TestImportObj, TestExportObj>;

  struct TestExportObj {
    fit::function<void(TestObjectLinker*, TestImportObj*)> LinkResolved;
    fit::function<void(void)> PeerDestroyed;
    fit::function<void(void)> ConnectionClosed;
  };

  struct TestImportObj {
    fit::function<void(TestExportObj*)> LinkResolved;
    fit::function<void(void)> PeerDestroyed;
    fit::function<void(void)> ConnectionClosed;
  };

  TestObjectLinker object_linker_;
};

TEST_F(ObjectLinkerTest, InitialState) {
  EXPECT_EQ(0u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
  EXPECT_EQ(0u, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, AllowsExport) {
  zx::eventpair export_token, import_token;
  TestExportObj export_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed"),
  };
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  uint64_t export_handle = object_linker_.RegisterExport(
      &export_obj, std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, export_handle);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, CannotExportInvalidToken) {
  zx::eventpair export_token{ZX_HANDLE_INVALID};
  TestExportObj export_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed"),
  };

  uint64_t export_handle = object_linker_.RegisterExport(
      &export_obj, std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(1);  // RegisterExport throws an error.
  EXPECT_EQ(0u, export_handle);
  EXPECT_EQ(0u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, CannotExportSameTokenTwice) {
  zx::eventpair export_token, export_token2, import_token;
  TestExportObj export_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved.1"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.1"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.1"),
  };
  TestExportObj export_obj2{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved.2"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.2"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.2"),
  };
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
  EXPECT_EQ(ZX_OK,
            export_token.duplicate(ZX_RIGHT_SAME_RIGHTS, &export_token2));

  uint64_t export_handle = object_linker_.RegisterExport(
      &export_obj, std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);  // First RegisterExport is fine.
  uint64_t export_handle2 = object_linker_.RegisterExport(
      &export_obj2, std::move(export_token2), error_reporter());
  EXPECT_ERROR_COUNT(1);  // Second RegisterExport throws an error.
  EXPECT_NE(0u, export_handle);
  EXPECT_EQ(0u, export_handle2);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, CannotExportWithDeadExportToken) {
  zx::eventpair export_token2;
  zx::eventpair import_token;
  TestExportObj export_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed"),
  };
  {
    zx::eventpair export_token;
    EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
    export_token2 = zx::eventpair{export_token.get()};
    // export dies now.
  }

  uint64_t export_handle = object_linker_.RegisterExport(
      &export_obj, std::move(export_token2), error_reporter());
  EXPECT_ERROR_COUNT(1);  // RegisterExport throws an error.
  EXPECT_EQ(0u, export_handle);
  EXPECT_EQ(0u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, CanExportWithDeadImportToken) {
  zx::eventpair export_token;
  zx::eventpair import_token2;
  TestExportObj export_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed"),
  };
  {
    zx::eventpair import_token;
    EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
    import_token2 = zx::eventpair{import_token.get()};
    // import dies now.
  }

  uint64_t export_handle = object_linker_.RegisterExport(
      &export_obj, std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, export_handle);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, UnregisterRemovesExport) {
  zx::eventpair export_token, import_token;
  TestExportObj export_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved.1"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.1"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.1"),
  };
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  uint64_t export_handle = object_linker_.RegisterExport(
      &export_obj, std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, export_handle);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());

  object_linker_.UnregisterExport(export_handle);
  EXPECT_ERROR_COUNT(0);
  EXPECT_EQ(0u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, AllowsImport) {
  zx::eventpair export_token, import_token;
  TestImportObj import_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed"),
  };
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  uint64_t import_handle = object_linker_.RegisterImport(
      &import_obj, std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, import_handle);
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, CannotImportInvalidToken) {
  zx::eventpair import_token{ZX_HANDLE_INVALID};
  TestImportObj import_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed"),
  };

  uint64_t import_handle = object_linker_.RegisterImport(
      &import_obj, std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(1);  // RegisterImport throws an error.
  EXPECT_EQ(0u, import_handle);
  EXPECT_EQ(0u, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, CannotImportSameTokenTwice) {
  zx::eventpair export_token, import_token, import_token2;
  TestImportObj import_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved.1"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.1"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.1"),
  };
  TestImportObj import_obj2{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved.2"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.2"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.2"),
  };
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
  EXPECT_EQ(ZX_OK,
            import_token.duplicate(ZX_RIGHT_SAME_RIGHTS, &import_token2));

  uint64_t import_handle = object_linker_.RegisterImport(
      &import_obj, std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);  // First RegisterImport is fine.
  uint64_t import_handle2 = object_linker_.RegisterImport(
      &import_obj2, std::move(import_token2), error_reporter());
  EXPECT_ERROR_COUNT(1);  // Second RegisterImport throws an error.
  EXPECT_NE(0u, import_handle);
  EXPECT_EQ(0u, import_handle2);
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, CannotImportWithDeadImportToken) {
  zx::eventpair import_token2;
  zx::eventpair export_token;
  TestImportObj import_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed"),
  };
  {
    zx::eventpair import_token;
    EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
    import_token2 = zx::eventpair{import_token.get()};
    // import dies now.
  }

  uint64_t import_handle = object_linker_.RegisterImport(
      &import_obj, std::move(import_token2), error_reporter());
  EXPECT_ERROR_COUNT(1);  // RegisterImport throws an error.
  EXPECT_EQ(0u, import_handle);
  EXPECT_EQ(0u, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, CanImportWithDeadExportToken) {
  zx::eventpair import_token;
  zx::eventpair export_token2;
  TestImportObj import_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed"),
  };
  {
    zx::eventpair export_token;
    EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
    export_token2 = zx::eventpair{export_token.get()};
    // export dies now.
  }

  uint64_t import_handle = object_linker_.RegisterImport(
      &import_obj, std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, import_handle);
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, UnregisterRemovesImport) {
  zx::eventpair export_token, import_token;
  TestImportObj import_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved.1"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.1"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.1"),
  };
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  uint64_t import_handle = object_linker_.RegisterImport(
      &import_obj, std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, import_handle);
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

  object_linker_.UnregisterImport(import_handle);
  EXPECT_ERROR_COUNT(0);
  EXPECT_EQ(0u, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, MatchingPeersAreLinked) {
  zx::eventpair export_token, import_token;
  TestExportObj* linked_export_obj = nullptr;
  TestImportObj* linked_import_obj = nullptr;
  TestExportObj export_obj{
      .LinkResolved =
          [&linked_import_obj](TestObjectLinker* linker,
                               TestImportObj* import_obj) {
            linked_import_obj = import_obj;
          },
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.Export"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.Export"),
  };
  TestImportObj import_obj{
      .LinkResolved =
          [&linked_export_obj](TestExportObj* export_obj) {
            linked_export_obj = export_obj;
          },
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.Import"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.Import"),
  };
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  uint64_t export_handle = object_linker_.RegisterExport(
      &export_obj, std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, export_handle);
  EXPECT_EQ(nullptr, linked_export_obj);
  EXPECT_EQ(nullptr, linked_import_obj);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());

  uint64_t import_handle = object_linker_.RegisterImport(
      &import_obj, std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, import_handle);
  EXPECT_EQ(&export_obj, linked_export_obj);
  EXPECT_EQ(&import_obj, linked_import_obj);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, MatchingPeersAreLinkedWithImportBeforeExport) {
  zx::eventpair export_token, import_token;
  TestExportObj* linked_export_obj = nullptr;
  TestImportObj* linked_import_obj = nullptr;
  TestExportObj export_obj{
      .LinkResolved =
          [&linked_import_obj](TestObjectLinker* linker,
                               TestImportObj* import_obj) {
            linked_import_obj = import_obj;
          },
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.Export"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.Export"),
  };
  TestImportObj import_obj{
      .LinkResolved =
          [&linked_export_obj](TestExportObj* export_obj) {
            linked_export_obj = export_obj;
          },
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.Import"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.Import"),
  };
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  uint64_t import_handle = object_linker_.RegisterImport(
      &import_obj, std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, import_handle);
  EXPECT_EQ(nullptr, linked_export_obj);
  EXPECT_EQ(nullptr, linked_import_obj);
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

  uint64_t export_handle = object_linker_.RegisterExport(
      &export_obj, std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, export_handle);
  EXPECT_EQ(&export_obj, linked_export_obj);
  EXPECT_EQ(&import_obj, linked_import_obj);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, NonMatchingPeersAreNotLinked) {
  zx::eventpair export_token, import_token;
  zx::eventpair export_token2, import_token2;
  TestExportObj* linked_export_obj = nullptr;
  TestImportObj* linked_import_obj = nullptr;
  TestExportObj export_obj{
      .LinkResolved =
          [&linked_import_obj](TestObjectLinker* linker,
                               TestImportObj* import_obj) {
            linked_import_obj = import_obj;
          },
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.Export"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.Export"),
  };
  TestImportObj import_obj{
      .LinkResolved =
          [&linked_export_obj](TestExportObj* export_obj) {
            linked_export_obj = export_obj;
          },
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.Import"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.Import"),
  };
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token2, &import_token2));

  uint64_t export_handle = object_linker_.RegisterExport(
      &export_obj, std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, export_handle);
  EXPECT_EQ(nullptr, linked_import_obj);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());
  uint64_t import_handle = object_linker_.RegisterImport(
      &import_obj, std::move(import_token2), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, export_handle);
  EXPECT_NE(0u, import_handle);
  EXPECT_EQ(nullptr, linked_export_obj);
  EXPECT_EQ(nullptr, linked_import_obj);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, ImportTokenDeathCleansUpObjectExport) {
  zx::eventpair export_token, import_token;
  bool conn_closed_fired = false;
  TestExportObj export_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed"),
      .ConnectionClosed =
          std::bind([&conn_closed_fired]() { conn_closed_fired = true; }),
  };
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  uint64_t export_handle = object_linker_.RegisterExport(
      &export_obj, std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, export_handle);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());

  // This should cause the export to die with a ConnectionClosed event on the
  // next tick of the event loop.
  import_token.reset();
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(conn_closed_fired);
  EXPECT_EQ(0u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, ExportTokenDeathCleansUpUnresolvedImports) {
  zx::eventpair export_token, import_token;
  bool conn_closed_fired = false;
  TestImportObj import_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed"),
      .ConnectionClosed =
          std::bind([&conn_closed_fired]() { conn_closed_fired = true; }),
  };
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  uint64_t import_handle = object_linker_.RegisterImport(
      &import_obj, std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, import_handle);
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

  // This should cause the import to die with a ConnectionClosed event on the
  // next tick of the event loop.
  export_token.reset();
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(conn_closed_fired);
  EXPECT_EQ(0u, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, ImportAfterUnregisteredExportFails) {
  zx::eventpair export_token, import_token;
  bool conn_closed_fired = false;
  TestExportObj export_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved.Export"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.Export"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.Export"),
  };
  TestImportObj import_obj{
      .LinkResolved = ERROR_IF_CALLED("LinkResolved.Import"),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.Import"),
      .ConnectionClosed =
          std::bind([&conn_closed_fired]() { conn_closed_fired = true; }),
  };
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  uint64_t export_handle = object_linker_.RegisterExport(
      &export_obj, std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, export_handle);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());
  object_linker_.UnregisterExport(export_handle);  // Release the export.
  EXPECT_EQ(0u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());

  // Now try to import. We should get a ConnectionClosed callback.
  uint64_t import_handle = object_linker_.RegisterImport(
      &import_obj, std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, import_handle);
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(conn_closed_fired);
  EXPECT_EQ(0u, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, DISABLED_DuplicatedImportTokensAllowMultipleImports) {
  zx::eventpair export_token, import_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  size_t import_resolution_count = 0;
  bool export_link_resolved_fired = false;
  TestExportObj export_obj{
      .LinkResolved = std::bind([&export_link_resolved_fired]() {
        export_link_resolved_fired = true;
      }),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.Export"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.Export"),
  };
  TestImportObj import_obj{
      .LinkResolved = std::bind(
          [&import_resolution_count]() { import_resolution_count++; }),
      .PeerDestroyed = ERROR_IF_CALLED("PeerDestroyed.Import"),
      .ConnectionClosed = ERROR_IF_CALLED("ConnectionClosed.Import"),
  };

  // Import multiple times.
  static const size_t kImportCount = 100;
  for (size_t i = 1; i <= kImportCount; ++i) {
    zx::eventpair dupe_import_token;
    EXPECT_EQ(ZX_OK,
              export_token.duplicate(ZX_RIGHT_SAME_RIGHTS, &dupe_import_token));
    uint64_t import_handle = object_linker_.RegisterImport(
        &import_obj, std::move(dupe_import_token), error_reporter());
    EXPECT_ERROR_COUNT(0);
    EXPECT_NE(0u, import_handle);
    EXPECT_EQ(0u, import_resolution_count);
    EXPECT_FALSE(export_link_resolved_fired);
    EXPECT_EQ(0u, object_linker_.ExportCount());
    EXPECT_EQ(i, object_linker_.UnresolvedImportCount());
  }

  // Export once, it should link to all imports.
  uint64_t export_handle = object_linker_.RegisterExport(
      &export_obj, std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_NE(0u, export_handle);
  EXPECT_TRUE(export_link_resolved_fired);
  EXPECT_EQ(kImportCount, import_resolution_count);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
  EXPECT_EQ(kImportCount, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic
