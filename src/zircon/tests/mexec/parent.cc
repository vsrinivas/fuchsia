// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>

#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/zx/channel.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include <libzbi/zbi-cpp.h>
#include <libzbi/zbi-zx.h>

#include <zxtest/zxtest.h>

namespace {

constexpr char kChildZbiFilePath[] = "/boot/testdata/zbi-bootfs/zbi-child-image.zbi";

// We reserve 4 pages because this should hopefully be enough buffer for the
// extra mexec data.
constexpr size_t kMexecPayloadSize = ZX_PAGE_SIZE * 4;
static uint8_t extra_buffer[kMexecPayloadSize];

TEST(MexecTest, ChainLoadChild) {
    zx::resource root_resource{zx_take_startup_handle(PA_HND(PA_RESOURCE, 0))};
    ASSERT_TRUE(root_resource.is_valid());
    ASSERT_OK(zx_system_mexec_payload_get(root_resource.get(), extra_buffer, kMexecPayloadSize));

    // Open the file containing the child ZBI.
    int fd = open(kChildZbiFilePath, O_RDONLY);
    ASSERT_GT(fd, 0);

    zx::vmo vmo;
    ASSERT_OK(fdio_get_vmo_clone(fd, vmo.reset_and_get_address()));

    // We need to make this VMO bigger to accomodate the ZBI extra payload but
    // it's R/O so we make a child and grow it slightly instead.
    size_t vmo_size;
    ASSERT_OK(vmo.get_size(&vmo_size));

    // Create the larger child vmo.
    zx::vmo zbi_vmo;
    const size_t zbi_size = vmo_size + kMexecPayloadSize;
    ASSERT_OK(vmo.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, zbi_size, &zbi_vmo));

    zx::vmar root_vmar(zx_vmar_root_self());

    uintptr_t zbi_base;
    ASSERT_OK(root_vmar.map(0, zbi_vmo, 0, zbi_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &zbi_base));

    zbi::Zbi zbi(reinterpret_cast<uint8_t*>(zbi_base), zbi_size);
    ASSERT_EQ(zbi.Check(nullptr), ZBI_RESULT_OK);

    zbi::Zbi extra(extra_buffer, kMexecPayloadSize);
    ASSERT_EQ(extra.Check(nullptr), ZBI_RESULT_OK);

    ASSERT_EQ(zbi.Extend(extra), ZBI_RESULT_OK);

    zbi::ZbiVMO kernel;
    zbi::ZbiVMO bootdata;
    zbi::ZbiVMO splitter;

    ASSERT_OK(splitter.Init(std::move(zbi_vmo)));

    ASSERT_EQ(splitter.SplitComplete(&kernel, &bootdata), ZBI_RESULT_OK);

    ASSERT_OK(zx_system_mexec(root_resource.get(), kernel.Release().get(), bootdata.Release().get()));
}

} // namespace
