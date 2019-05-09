// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fzl/vmo-mapper.h>
#include <ramdevice-client/ramdisk.h>
#include <ramdevice-client/ramnand.h>
#include <zxtest/zxtest.h>

constexpr uint64_t kBlockSize = 0x1000;
constexpr uint64_t kBlockCount = 0x10;

constexpr uint32_t kOobSize = 8;
constexpr uint32_t kPageSize = 1024;
constexpr uint32_t kPagesPerBlock = 16;
constexpr uint32_t kSkipBlockSize = kPageSize * kPagesPerBlock;
constexpr uint32_t kNumBlocks = 20;

extern fbl::Vector<fbl::String> test_block_devices;

bool FilterRealBlockDevices(const fbl::unique_fd& fd);

class BlockDevice {
public:
    static void Create(const uint8_t* guid, fbl::unique_ptr<BlockDevice>* device);

    ~BlockDevice() {
        ramdisk_destroy(client_);
    }

private:
    BlockDevice(ramdisk_client_t* client)
        : client_(client) {}

    ramdisk_client_t* client_;
};

class SkipBlockDevice {
public:
    static void Create(const fuchsia_hardware_nand_RamNandInfo& nand_info,
                       fbl::unique_ptr<SkipBlockDevice>* device);

    fbl::unique_fd devfs_root() { return fbl::unique_fd(dup(ctl_->devfs_root().get())); }

    fzl::VmoMapper& mapper() { return mapper_; }

    ~SkipBlockDevice() = default;

private:
    SkipBlockDevice(fbl::RefPtr<ramdevice_client::RamNandCtl> ctl,
                    ramdevice_client::RamNand ram_nand, fzl::VmoMapper mapper)
        : ctl_(std::move(ctl)), ram_nand_(std::move(ram_nand)), mapper_(std::move(mapper)) {}

    fbl::RefPtr<ramdevice_client::RamNandCtl> ctl_;
    ramdevice_client::RamNand ram_nand_;
    fzl::VmoMapper mapper_;
};

class FakeSysinfo {
public:
    FakeSysinfo(async_dispatcher_t* dispatcher) {
        zx::channel remote;
        ASSERT_OK(zx::channel::create(0, &remote, &svc_chan_));
        fidl_bind(dispatcher, remote.release(),
                  reinterpret_cast<fidl_dispatch_t*>(fuchsia_sysinfo_Device_dispatch),
                  this, &ops_);
    }

    zx_status_t GetRootJob(fidl_txn_t* txn) {
        return fuchsia_sysinfo_DeviceGetRootJob_reply(txn, ZX_ERR_NOT_SUPPORTED, ZX_HANDLE_INVALID);
    }

    zx_status_t GetRootResource(fidl_txn_t* txn) {
        return fuchsia_sysinfo_DeviceGetRootResource_reply(txn, ZX_ERR_NOT_SUPPORTED,
                                                           ZX_HANDLE_INVALID);
    }

    zx_status_t GetHypervisorResource(fidl_txn_t* txn) {
        return fuchsia_sysinfo_DeviceGetHypervisorResource_reply(txn, ZX_ERR_NOT_SUPPORTED,
                                                                 ZX_HANDLE_INVALID);
    }

    zx_status_t GetBoardName(fidl_txn_t* txn) {
        char board[32] = {};
        strcpy(board, "vim2");
        return fuchsia_sysinfo_DeviceGetBoardName_reply(txn, ZX_OK, board, 32);
    }

    zx_status_t GetInterruptControllerInfo(fidl_txn_t* txn) {
        return fuchsia_sysinfo_DeviceGetInterruptControllerInfo_reply(txn, ZX_ERR_NOT_SUPPORTED,
                                                                      nullptr);
    }

    zx::channel& svc_chan() { return svc_chan_; }

private:
    using Binder = fidl::Binder<FakeSysinfo>;

    zx::channel svc_chan_;

    static constexpr fuchsia_sysinfo_Device_ops_t ops_ = {
        .GetRootJob = Binder::BindMember<&FakeSysinfo::GetRootJob>,
        .GetRootResource = Binder::BindMember<&FakeSysinfo::GetRootResource>,
        .GetHypervisorResource = Binder::BindMember<&FakeSysinfo::GetHypervisorResource>,
        .GetBoardName = Binder::BindMember<&FakeSysinfo::GetBoardName>,
        .GetInterruptControllerInfo = Binder::BindMember<&FakeSysinfo::GetInterruptControllerInfo>,
    };
};
