// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

TEST(TransportTests, GoodChannelTransportWithChannelTransportEnd) {
  TestLibrary library;
  library.AddFile("good/fi-0167.test.fidl");

  ASSERT_COMPILED(library);
}

TEST(TransportTests, GoodDriverTransportWithDriverTransportEnd) {
  TestLibrary library(R"FIDL(
library example;

@transport("Driver")
protocol P {
  M(resource struct{
     c client_end:P;
  }) -> (resource struct{
     s server_end:P;
  });
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TransportTests, GoodDriverTransportWithChannelTransportEnd) {
  TestLibrary library(R"FIDL(
library example;

protocol ChannelProtocol {};

@transport("Driver")
protocol P {
  M(resource struct{
     c client_end:ChannelProtocol;
  }) -> (resource struct{
     s server_end:ChannelProtocol;
  });
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TransportTests, GoodDriverTransportWithZirconHandle) {
  TestLibrary library(R"FIDL(
library example;

using zx;

@transport("Driver")
protocol P {
  M() -> (resource struct{
     h zx.handle;
  });
};
)FIDL");
  library.UseLibraryZx();
  ASSERT_COMPILED(library);
}

TEST(TransportTests, GoodSyscallTransportWithZirconHandle) {
  TestLibrary library(R"FIDL(
library example;

using zx;

@transport("Syscall")
protocol P {
  M() -> (resource struct{
     h zx.handle;
  });
};
)FIDL");
  library.UseLibraryZx();
  ASSERT_COMPILED(library);
}

TEST(TransportTests, GoodBanjoTransportWithZirconHandle) {
  TestLibrary library(R"FIDL(
library example;

using zx;

@transport("Banjo")
protocol P {
  M() -> (resource struct{
     h zx.handle;
  });
};
)FIDL");
  library.UseLibraryZx();
  ASSERT_COMPILED(library);
}

TEST(TransportTests, GoodDriverTransportWithDriverHandle) {
  TestLibrary library(R"FIDL(
library example;

using fdf;

@transport("Driver")
protocol P {
  M() -> (resource struct{
     h fdf.handle;
  });
};
)FIDL");
  library.UseLibraryFdf();
  ASSERT_COMPILED(library);
}

TEST(TransportTests, BadChannelTransportWithDriverHandle) {
  TestLibrary library(R"FIDL(
library example;

using fdf;

protocol P {
  M() -> (resource struct{
     h fdf.handle;
  });
};
)FIDL");
  library.UseLibraryFdf();
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrHandleUsedInIncompatibleTransport);
}

TEST(TransportTests, BadChannelTransportWithDriverClientEndRequest) {
  TestLibrary library(R"FIDL(
library example;

@transport("Driver")
protocol DriverProtocol {};

protocol P {
  M(resource struct{
     c array<vector<box<resource struct{s client_end:DriverProtocol;}>>, 3>;
  });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTransportEndUsedInIncompatibleTransport);
}

TEST(TransportTests, BadChannelTransportWithDriverServerEndResponse) {
  TestLibrary library(R"FIDL(
library example;

@transport("Driver")
protocol DriverProtocol {};

protocol P {
  M() -> (resource table{
     1: s resource union{
       1: s server_end:DriverProtocol;
     };
  });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTransportEndUsedInIncompatibleTransport);
}

TEST(TransportTests, BadBanjoTransportWithDriverClientEndRequest) {
  TestLibrary library(R"FIDL(
library example;

@transport("Driver")
protocol DriverProtocol {};

@transport("Banjo")
protocol P {
  M(resource struct{
     s client_end:DriverProtocol;
  });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTransportEndUsedInIncompatibleTransport);
}

TEST(TransportTests, BadDriverTransportWithBanjoClientEndRequest) {
  TestLibrary library(R"FIDL(
library example;

@transport("Banjo")
protocol BanjoProtocol {};

@transport("Driver")
protocol P {
  M(resource struct{
     s client_end:BanjoProtocol;
  });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTransportEndUsedInIncompatibleTransport);
}

TEST(TransportTests, BadSyscallTransportWithDriverClientEndRequest) {
  TestLibrary library(R"FIDL(
library example;

@transport("Driver")
protocol DriverProtocol {};

@transport("Syscall")
protocol P {
  M(resource struct{
     s client_end:DriverProtocol;
  });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTransportEndUsedInIncompatibleTransport);
}

TEST(TransportTests, BadSyscallTransportWithSyscallClientEndRequest) {
  TestLibrary library(R"FIDL(
library example;

@transport("Syscall")
protocol SyscallProtocol {};

@transport("Syscall")
protocol P {
  M(resource struct{
     s client_end:SyscallProtocol;
  });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTransportEndUsedInIncompatibleTransport);
}

TEST(TransportTests, BadCustomHandleInZirconChannel) {
  TestLibrary library(R"FIDL(
library example;

type obj_type = strict enum : uint32 {
  NONE = 0;
};
type rights = strict enum : uint32 {
  SAME_RIGHTS = 0;
};

resource_definition handle : uint32 {
    properties {
        subtype obj_type;
        rights rights;
    };
};

protocol P {
  M(resource struct{
     h handle;
  });
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrHandleUsedInIncompatibleTransport);
}

TEST(TransportTests, BadCannotReassignTransport) {
  TestLibrary library;
  library.AddFile("bad/fi-0167.test.fidl");

  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrCannotConstrainTwice,
                                      fidl::ErrCannotConstrainTwice);
}

}  // namespace
