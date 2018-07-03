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
  struct TestExportObj {};
  struct TestImportObj {};
  using TestObjectLinker = ObjectLinker<TestExportObj, TestImportObj>;

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
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  TestObjectLinker::ExportLink export_link =
      object_linker_.CreateExport(std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(export_link.valid());
  EXPECT_FALSE(export_link.initialized());
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, CannotExportInvalidToken) {
  zx::eventpair export_token{ZX_HANDLE_INVALID};

  TestObjectLinker::ExportLink export_link =
      object_linker_.CreateExport(std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(1);  // CreateExport throws an error.
  EXPECT_FALSE(export_link.valid());
  EXPECT_FALSE(export_link.initialized());
  EXPECT_EQ(0u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, CannotExportWithDeadExportToken) {
  zx::eventpair export_token, import_token;
  {
    zx::eventpair export_token2;
    EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token2, &import_token));
    export_token = zx::eventpair{export_token2.get()};
    // |export_token2| dies now, |export_token| is an invalid copy.
  }

  TestObjectLinker::ExportLink export_link =
      object_linker_.CreateExport(std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(1);  // CreateExport throws an error.
  EXPECT_FALSE(export_link.valid());
  EXPECT_FALSE(export_link.initialized());
  EXPECT_EQ(0u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, CanExportWithDeadImportToken) {
  zx::eventpair export_token, import_token;
  {
    zx::eventpair import_token2;
    EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token2));
    import_token = zx::eventpair{import_token2.get()};
    // |import_token2| dies now, |import_token| is an invalid copy.
  }

  TestObjectLinker::ExportLink export_link =
      object_linker_.CreateExport(std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(export_link.valid());
  EXPECT_FALSE(export_link.initialized());
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, CannotExportSameTokenTwice) {
  zx::eventpair export_token, export_token2, import_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
  EXPECT_EQ(ZX_OK,
            export_token.duplicate(ZX_RIGHT_SAME_RIGHTS, &export_token2));

  TestObjectLinker::ExportLink export_link =
      object_linker_.CreateExport(std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(export_link.valid());
  EXPECT_FALSE(export_link.initialized());
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());

  TestObjectLinker::ExportLink export_link2 =
      object_linker_.CreateExport(std::move(export_token2), error_reporter());
  EXPECT_ERROR_COUNT(1);  // CreateExport throws an error.
  EXPECT_FALSE(export_link2.valid());
  EXPECT_FALSE(export_link2.initialized());
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, LinkDeathRemovesExport) {
  zx::eventpair export_token, import_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  {
    TestObjectLinker::ExportLink export_link =
        object_linker_.CreateExport(std::move(export_token), error_reporter());
    EXPECT_ERROR_COUNT(0);
    EXPECT_TRUE(export_link.valid());
    EXPECT_FALSE(export_link.initialized());
    EXPECT_EQ(1u, object_linker_.ExportCount());
    EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());

    // | export_link dies now. |
  }

  EXPECT_EQ(0u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, AllowsImport) {
  zx::eventpair export_token, import_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  TestObjectLinker::ImportLink import_link =
      object_linker_.CreateImport(std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(import_link.valid());
  EXPECT_FALSE(import_link.initialized());
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, CannotImportInvalidToken) {
  zx::eventpair import_token{ZX_HANDLE_INVALID};

  TestObjectLinker::ImportLink import_link =
      object_linker_.CreateImport(std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(1);  // CreateImport throws an error.
  EXPECT_FALSE(import_link.valid());
  EXPECT_FALSE(import_link.initialized());
  EXPECT_EQ(0u, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, CannotImportWithDeadImportToken) {
  zx::eventpair export_token, import_token;
  {
    zx::eventpair import_token2;
    EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token2));
    import_token = zx::eventpair{import_token2.get()};
    // |import_token2| dies now, |import_token| is an invalid copy.
  }

  TestObjectLinker::ImportLink import_link =
      object_linker_.CreateImport(std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(1);  // CreateImport throws an error.
  EXPECT_FALSE(import_link.valid());
  EXPECT_FALSE(import_link.initialized());
  EXPECT_EQ(0u, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, CanImportWithDeadExportToken) {
  zx::eventpair export_token, import_token;
  {
    zx::eventpair export_token2;
    EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token2, &import_token));
    export_token = zx::eventpair{export_token2.get()};
    // |export_token2| dies now, |export_token| is an invalid copy.
  }

  TestObjectLinker::ImportLink import_link =
      object_linker_.CreateImport(std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(import_link.valid());
  EXPECT_FALSE(import_link.initialized());
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, CannotImportSameTokenTwice) {
  zx::eventpair export_token, import_token, import_token2;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
  EXPECT_EQ(ZX_OK,
            import_token.duplicate(ZX_RIGHT_SAME_RIGHTS, &import_token2));

  TestObjectLinker::ImportLink import_link =
      object_linker_.CreateImport(std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(import_link.valid());
  EXPECT_FALSE(import_link.initialized());
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

  TestObjectLinker::ImportLink import_link2 =
      object_linker_.CreateImport(std::move(import_token2), error_reporter());
  EXPECT_ERROR_COUNT(1);  // CreateImport throws an error.
  EXPECT_FALSE(import_link2.valid());
  EXPECT_FALSE(import_link2.initialized());
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, LinkDeathRemovesImport) {
  zx::eventpair export_token, import_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  {
    TestObjectLinker::ImportLink import_link =
        object_linker_.CreateImport(std::move(import_token), error_reporter());
    EXPECT_ERROR_COUNT(0);
    EXPECT_TRUE(import_link.valid());
    EXPECT_FALSE(import_link.initialized());
    EXPECT_EQ(1u, object_linker_.ImportCount());
    EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

    // | import_link dies now. |
  }

  EXPECT_EQ(0u, object_linker_.ImportCount());
}

TEST_F(ObjectLinkerTest, InitializingLinkTwiceCausesDeath) {
  TestExportObj export_obj, export_obj2;
  zx::eventpair export_token, import_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));

  TestObjectLinker::ExportLink export_link =
      object_linker_.CreateExport(std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(export_link.valid());
  EXPECT_FALSE(export_link.initialized());

  export_link.Initialize(&export_obj, ERROR_IF_CALLED("export.link_resolved"),
                         ERROR_IF_CALLED("export.link_disconnected"));
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(export_link.initialized());

  // 2nd Initialize() attempt dies with a DCHECK.
  EXPECT_DEATH_IF_SUPPORTED(
      export_link.Initialize(&export_obj2,
                             ERROR_IF_CALLED("export.link_resolved"),
                             ERROR_IF_CALLED("export.link_disconnected")),
      "");
}

TEST_F(ObjectLinkerTest, InitializeLinksMatchingPeers) {
  zx::eventpair export_token, import_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
  TestExportObj export_obj;
  TestImportObj import_obj;
  bool export_linked = false, import_linked = false;
  bool export_disconnected = false, import_disconnected = false;
  bool fail_on_disconnect_called = false;

  TestObjectLinker::ExportLink export_link =
      object_linker_.CreateExport(std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(export_link.valid());
  EXPECT_FALSE(export_link.initialized());
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());

  {
    TestObjectLinker::ImportLink import_link =
        object_linker_.CreateImport(std::move(import_token), error_reporter());
    EXPECT_ERROR_COUNT(0);
    EXPECT_TRUE(import_link.valid());
    EXPECT_FALSE(import_link.initialized());
    EXPECT_EQ(1u, object_linker_.ImportCount());
    EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

    export_link.Initialize(
        &export_obj,
        [&import_obj, &import_linked](TestImportObj* obj) {
          EXPECT_EQ(obj, &import_obj);
          EXPECT_FALSE(import_linked);
          import_linked = true;
        },
        [&fail_on_disconnect_called, &import_disconnected]() {
          EXPECT_FALSE(fail_on_disconnect_called);
          EXPECT_FALSE(import_disconnected);
          import_disconnected = true;
        });
    EXPECT_ERROR_COUNT(0);
    EXPECT_TRUE(export_link.initialized());
    EXPECT_FALSE(export_linked);
    EXPECT_FALSE(import_linked);
    EXPECT_FALSE(export_disconnected);
    EXPECT_FALSE(import_disconnected);
    EXPECT_EQ(1u, object_linker_.ExportCount());
    EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());
    EXPECT_EQ(1u, object_linker_.ImportCount());
    EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

    import_link.Initialize(
        &import_obj,
        [&export_obj, &export_linked](TestExportObj* obj) {
          EXPECT_EQ(obj, &export_obj);
          EXPECT_FALSE(export_linked);
          export_linked = true;
        },
        [&fail_on_disconnect_called, &export_disconnected]() {
          EXPECT_FALSE(fail_on_disconnect_called);
          EXPECT_FALSE(export_disconnected);
          export_disconnected = true;
        });
    EXPECT_ERROR_COUNT(0);
    EXPECT_TRUE(import_link.initialized());
    EXPECT_TRUE(export_linked);
    EXPECT_TRUE(import_linked);
    EXPECT_FALSE(export_disconnected);
    EXPECT_FALSE(import_disconnected);
    EXPECT_EQ(1u, object_linker_.ExportCount());
    EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
    EXPECT_EQ(1u, object_linker_.ImportCount());
    EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());

    // |import_link| dies now.
  }

  EXPECT_TRUE(import_disconnected);
  EXPECT_FALSE(export_disconnected);

  // |export_link| dies now.  No disconnect callback should be called.
  fail_on_disconnect_called = true;
}

TEST_F(ObjectLinkerTest, InitializeLinksMatchingPeersWithImportBeforeExport) {
  zx::eventpair export_token, import_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
  TestExportObj export_obj;
  TestImportObj import_obj;
  bool export_linked = false, import_linked = false;
  bool export_disconnected = false, import_disconnected = false;
  bool fail_on_disconnect_called = false;

  TestObjectLinker::ImportLink import_link =
      object_linker_.CreateImport(std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(import_link.valid());
  EXPECT_FALSE(import_link.initialized());
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

  {
    import_link.Initialize(
        &import_obj,

        [&export_obj, &export_linked](TestExportObj* obj) {
          EXPECT_EQ(obj, &export_obj);
          EXPECT_FALSE(export_linked);
          export_linked = true;
        },
        [&fail_on_disconnect_called, &export_disconnected]() {
          EXPECT_FALSE(fail_on_disconnect_called);
          EXPECT_FALSE(export_disconnected);
          export_disconnected = true;
        });
    EXPECT_ERROR_COUNT(0);
    EXPECT_TRUE(import_link.initialized());
    EXPECT_FALSE(export_linked);
    EXPECT_FALSE(import_linked);
    EXPECT_FALSE(export_disconnected);
    EXPECT_FALSE(import_disconnected);
    EXPECT_EQ(1u, object_linker_.ImportCount());
    EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

    TestObjectLinker::ExportLink export_link =
        object_linker_.CreateExport(std::move(export_token), error_reporter());
    EXPECT_ERROR_COUNT(0);
    EXPECT_TRUE(export_link.valid());
    EXPECT_FALSE(export_link.initialized());
    EXPECT_FALSE(export_linked);
    EXPECT_FALSE(import_linked);
    EXPECT_FALSE(export_disconnected);
    EXPECT_FALSE(import_disconnected);
    EXPECT_EQ(1u, object_linker_.ExportCount());
    EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());
    EXPECT_EQ(1u, object_linker_.ImportCount());
    EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

    export_link.Initialize(
        &export_obj,
        [&import_obj, &import_linked](TestImportObj* obj) {
          EXPECT_EQ(obj, &import_obj);
          EXPECT_FALSE(import_linked);
          import_linked = true;
        },
        [&fail_on_disconnect_called, &import_disconnected]() {
          EXPECT_FALSE(fail_on_disconnect_called);
          EXPECT_FALSE(import_disconnected);
          import_disconnected = true;
        });
    EXPECT_ERROR_COUNT(0);
    EXPECT_TRUE(export_link.initialized());
    EXPECT_TRUE(export_linked);
    EXPECT_TRUE(import_linked);
    EXPECT_FALSE(export_disconnected);
    EXPECT_FALSE(import_disconnected);
    EXPECT_EQ(1u, object_linker_.ExportCount());
    EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
    EXPECT_EQ(1u, object_linker_.ImportCount());
    EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());

    // |export_link| dies now.
  }

  EXPECT_TRUE(export_disconnected);
  EXPECT_FALSE(import_disconnected);

  // |import_link| dies now.  No disconnect callback should be called.
  fail_on_disconnect_called = true;
}

TEST_F(ObjectLinkerTest, InitializeDoesNotLinkNonMatchingPeers) {
  zx::eventpair export_token, import_token;
  zx::eventpair export_token2, import_token2;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token2, &import_token2));
  TestExportObj export_obj;
  TestImportObj import_obj;

  TestObjectLinker::ImportLink import_link =
      object_linker_.CreateImport(std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(import_link.valid());
  EXPECT_FALSE(import_link.initialized());
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

  {
    TestObjectLinker::ExportLink export_link =
        object_linker_.CreateExport(std::move(export_token2), error_reporter());
    EXPECT_ERROR_COUNT(0);
    EXPECT_TRUE(export_link.valid());
    EXPECT_FALSE(export_link.initialized());
    EXPECT_EQ(1u, object_linker_.ExportCount());
    EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());

    import_link.Initialize(&import_obj, ERROR_IF_CALLED("export.link_resolved"),
                           ERROR_IF_CALLED("export.link_disconnected"));
    EXPECT_ERROR_COUNT(0);
    EXPECT_TRUE(import_link.initialized());
    EXPECT_EQ(1u, object_linker_.ExportCount());
    EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());
    EXPECT_EQ(1u, object_linker_.ImportCount());
    EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

    export_link.Initialize(&export_obj, ERROR_IF_CALLED("export.link_resolved"),
                           ERROR_IF_CALLED("export.link_disconnected"));
    EXPECT_ERROR_COUNT(0);
    EXPECT_TRUE(export_link.initialized());
    EXPECT_EQ(1u, object_linker_.ExportCount());
    EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());
    EXPECT_EQ(1u, object_linker_.ImportCount());
    EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

    // |export_link| dies now.  No disconnect callback should be called.
  }

  // |import_link| dies now.  No disconnect callback should be called.
}

TEST_F(ObjectLinkerTest, EarlyImportTokenDeathCausesExportDisconnection) {
  zx::eventpair export_token, import_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
  TestExportObj export_obj;
  bool import_disconnected = false;

  TestObjectLinker::ExportLink export_link =
      object_linker_.CreateExport(std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(export_link.valid());
  EXPECT_FALSE(export_link.initialized());
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());

  // This should cause the export to get a link_disconnected event when it is
  // initialized.
  import_token.reset();
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_FALSE(import_disconnected);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());

  export_link.Initialize(&export_obj, ERROR_IF_CALLED("export.link_resolved"),
                         [&import_disconnected]() {
                           EXPECT_FALSE(import_disconnected);
                           import_disconnected = true;
                         });
  EXPECT_TRUE(export_link.initialized());
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, ImportTokenDeathCausesExportDisconnection) {
  zx::eventpair export_token, import_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
  TestExportObj export_obj;
  bool import_disconnected = false;

  TestObjectLinker::ExportLink export_link =
      object_linker_.CreateExport(std::move(export_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(export_link.valid());
  EXPECT_FALSE(export_link.initialized());
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());

  export_link.Initialize(&export_obj, ERROR_IF_CALLED("export.link_resolved"),
                         [&import_disconnected]() {
                           EXPECT_FALSE(import_disconnected);
                           import_disconnected = true;
                         });
  EXPECT_TRUE(export_link.initialized());
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedExportCount());

  // This should cause the export to get a link_disconnected event when the
  // eventloop ticks.
  import_token.reset();
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(export_link.initialized());
  EXPECT_TRUE(import_disconnected);
  EXPECT_EQ(1u, object_linker_.ExportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedExportCount());
}

TEST_F(ObjectLinkerTest, EarlyExportTokenDeathCausesImportDisconnection) {
  zx::eventpair export_token, import_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
  TestImportObj import_obj;
  bool export_disconnected = false;

  TestObjectLinker::ImportLink import_link =
      object_linker_.CreateImport(std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(import_link.valid());
  EXPECT_FALSE(import_link.initialized());
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

  // This should cause the import to get a link_disconnected event when it is
  // initialized.
  export_token.reset();
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_FALSE(export_disconnected);
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());

  import_link.Initialize(&import_obj, ERROR_IF_CALLED("import.link_resolved"),
                         [&export_disconnected]() {
                           EXPECT_FALSE(export_disconnected);
                           export_disconnected = true;
                         });
  EXPECT_TRUE(import_link.initialized());
  EXPECT_TRUE(export_disconnected);
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());
}

TEST_F(ObjectLinkerTest, ExportTokenDeathCausesImportDisconnection) {
  zx::eventpair export_token, import_token;
  EXPECT_EQ(ZX_OK, zx::eventpair::create(0, &export_token, &import_token));
  TestImportObj import_obj;
  bool export_disconnected = false;

  TestObjectLinker::ImportLink import_link =
      object_linker_.CreateImport(std::move(import_token), error_reporter());
  EXPECT_ERROR_COUNT(0);
  EXPECT_TRUE(import_link.valid());
  EXPECT_FALSE(import_link.initialized());
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

  import_link.Initialize(&import_obj, ERROR_IF_CALLED("import.link_resolved"),
                         [&export_disconnected]() {
                           EXPECT_FALSE(export_disconnected);
                           export_disconnected = true;
                         });
  EXPECT_TRUE(import_link.initialized());
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(1u, object_linker_.UnresolvedImportCount());

  // This should cause the import to get a link_disconnected event when the
  // eventloop ticks.
  export_token.reset();
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_TRUE(import_link.initialized());
  EXPECT_TRUE(export_disconnected);
  EXPECT_EQ(1u, object_linker_.ImportCount());
  EXPECT_EQ(0u, object_linker_.UnresolvedImportCount());
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic
