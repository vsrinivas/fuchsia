// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "paver.h"

#include <algorithm>

#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/paver/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/channel.h>
#include <zircon/boot/netboot.h>
#include <zxtest/zxtest.h>

namespace {
constexpr char kFakeData[] = "lalala";
}

TEST(PaverTest, Constructor) {
    netsvc::Paver paver(zx::channel);
}

TEST(PaverTest, GetSingleton) {
    ASSERT_NOT_NULL(netsvc::Paver::Get());
}

TEST(PaverTest, InitialInProgressFalse) {
    zx::channel chan;
    netsvc::Paver paver_(std::move(chan));
    ASSERT_FALSE(paver_.InProgress());
}

TEST(PaverTest, InitialExitCodeValid) {
    zx::channel chan;
    netsvc::Paver paver_(std::move(chan));
    ASSERT_OK(paver_.exit_code());
}

namespace {

enum class Command {
    kUnknown,
    kQueryActiveConfiguration,
    kSetActiveConfiguration,
    kMarkActiveConfigurationSuccessful,
    kForceRecoveryConfiguration,
    kWriteAsset,
    kWriteVolumes,
    kWriteBootloader,
    kWriteDataFile,
    kWipeVolumes,
};

class FakePaver {
public:
    zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
        return fidl_bind(dispatcher, request.release(),
                         reinterpret_cast<fidl_dispatch_t*>(fuchsia_paver_Paver_dispatch),
                         this, &ops_);
    }

    zx_status_t QueryActiveConfiguration(fidl_txn_t* txn) {
        last_command_ = Command::kQueryActiveConfiguration;
        return fuchsia_paver_PaverQueryActiveConfiguration_reply(txn, ZX_ERR_NOT_SUPPORTED,
                                                                 nullptr);
    }

    zx_status_t SetActiveConfiguration(fuchsia_paver_Configuration configuration,
                                       fidl_txn_t* txn) {
        last_command_ = Command::kSetActiveConfiguration;
        return fuchsia_paver_PaverSetActiveConfiguration_reply(txn, ZX_ERR_NOT_SUPPORTED);
    }

    zx_status_t MarkActiveConfigurationSuccessful(fidl_txn_t* txn) {
        last_command_ = Command::kMarkActiveConfigurationSuccessful;
        return fuchsia_paver_PaverMarkActiveConfigurationSuccessful_reply(txn,
                                                                          ZX_ERR_NOT_SUPPORTED);
    }

    zx_status_t ForceRecoveryConfiguration(fidl_txn_t* txn) {
        last_command_ = Command::kForceRecoveryConfiguration;
        return fuchsia_paver_PaverForceRecoveryConfiguration_reply(txn, ZX_ERR_NOT_SUPPORTED);
    }

    zx_status_t WriteAsset(fuchsia_paver_Configuration configuration,
                           fuchsia_paver_Asset asset, const fuchsia_mem_Buffer* payload,
                           fidl_txn_t* txn) {
        last_command_ = Command::kWriteAsset;
        zx_handle_close(payload->vmo);
        auto status = payload->size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
        return fuchsia_paver_PaverWriteAsset_reply(txn, status);
    }

    zx_status_t WriteVolumes(zx_handle_t payload_stream, fidl_txn_t* txn) {
        last_command_ = Command::kWriteVolumes;
        zx::channel stream(payload_stream);
        // Register VMO.
        zx::vmo vmo;
        auto status = zx::vmo::create(1024, 0, &vmo);
        if (status != ZX_OK) {
            return fuchsia_paver_PaverWriteVolumes_reply(txn, status);
        }
        auto io_status = fuchsia_paver_PayloadStreamRegisterVmo(stream.get(), vmo.release(),
                                                                &status);
        status = io_status == ZX_OK ? status : io_status;
        if (status != ZX_OK) {
            return fuchsia_paver_PaverWriteVolumes_reply(txn, status);
        }
        // Stream until EOF.
        status = [&]() {
            size_t data_transferred = 0;
            for (;;) {
                fuchsia_paver_ReadResult result;
                auto io_status = fuchsia_paver_PayloadStreamReadData(stream.get(), &result);
                if (io_status != ZX_OK) {
                    return io_status;
                }
                switch (result.tag) {
                case fuchsia_paver_ReadResultTag_err:
                    return result.err;
                case fuchsia_paver_ReadResultTag_eof:
                    return data_transferred == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
                case fuchsia_paver_ReadResultTag_info:
                    data_transferred += result.info.size;
                    continue;
                default:
                    return ZX_ERR_INTERNAL;
                }
            }
        }();

        return fuchsia_paver_PaverWriteVolumes_reply(txn, status);
    }

    zx_status_t WriteBootloader(const fuchsia_mem_Buffer* payload, fidl_txn_t* txn) {
        last_command_ = Command::kWriteBootloader;
        zx_handle_close(payload->vmo);
        auto status = payload->size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
        return fuchsia_paver_PaverWriteBootloader_reply(txn, status);
    }

    zx_status_t WriteDataFile(const char* filename, size_t filename_len,
                              const fuchsia_mem_Buffer* payload, fidl_txn_t* txn) {
        last_command_ = Command::kWriteDataFile;
        zx_handle_close(payload->vmo);
        auto status = payload->size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
        return fuchsia_paver_PaverWriteDataFile_reply(txn, status);
    }

    zx_status_t WipeVolumes(fidl_txn_t* txn) {
        last_command_ = Command::kWipeVolumes;
        auto status = ZX_OK;
        return fuchsia_paver_PaverWipeVolumes_reply(txn, status);
    }

    Command last_command() { return last_command_; }
    void set_expected_payload_size(size_t size) { expected_payload_size_ = size; }

private:
    using Binder = fidl::Binder<FakePaver>;

    Command last_command_ = Command::kUnknown;
    size_t expected_payload_size_ = 0;

    static constexpr fuchsia_paver_Paver_ops_t ops_ = {
        .QueryActiveConfiguration = Binder::BindMember<&FakePaver::QueryActiveConfiguration>,
        .SetActiveConfiguration = Binder::BindMember<&FakePaver::SetActiveConfiguration>,
        .MarkActiveConfigurationSuccessful =
            Binder::BindMember<&FakePaver::MarkActiveConfigurationSuccessful>,
        .ForceRecoveryConfiguration = Binder::BindMember<&FakePaver::ForceRecoveryConfiguration>,
        .WriteAsset = Binder::BindMember<&FakePaver::WriteAsset>,
        .WriteVolumes = Binder::BindMember<&FakePaver::WriteVolumes>,
        .WriteBootloader = Binder::BindMember<&FakePaver::WriteBootloader>,
        .WriteDataFile = Binder::BindMember<&FakePaver::WriteDataFile>,
        .WipeVolumes = Binder::BindMember<&FakePaver::WipeVolumes>,
    };
};

class FakeSvc {
public:
    explicit FakeSvc(async_dispatcher_t* dispatcher)
        : dispatcher_(dispatcher), vfs_(dispatcher) {
        auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
        root_dir->AddEntry(fuchsia_paver_Paver_Name,
                           fbl::MakeRefCounted<fs::Service>([this](zx::channel request) {
                               return fake_paver_.Connect(dispatcher_, std::move(request));
                           }));

        zx::channel svc_remote;
        ASSERT_OK(zx::channel::create(0, &svc_local_, &svc_remote));

        vfs_.ServeDirectory(root_dir, std::move(svc_remote));
    }

    FakePaver& fake_paver() { return fake_paver_; }
    zx::channel& svc_chan() { return svc_local_; }

private:
    async_dispatcher_t* dispatcher_;
    fs::SynchronousVfs vfs_;
    FakePaver fake_paver_;
    zx::channel svc_local_;
};

} // namespace

class PaverTest : public zxtest::Test {
protected:
    PaverTest()
        : loop_(&kAsyncLoopConfigNoAttachToThread),
          fake_svc_(loop_.dispatcher()),
          paver_(std::move(fake_svc_.svc_chan())) {

        paver_.set_timeout(zx::msec(500));
        loop_.StartThread("paver-test-loop");
    }

    ~PaverTest() {
        // Need to make sure paver thread exits.
        Wait();
        loop_.Shutdown();
    }

    void Wait() {
        while (paver_.InProgress())
            continue;
    }

    async::Loop loop_;
    FakeSvc fake_svc_;
    netsvc::Paver paver_;
};

TEST_F(PaverTest, OpenWriteInvalidFile) {
    char invalid_file_name[32] = {};
    ASSERT_NE(paver_.OpenWrite(invalid_file_name, 0), TFTP_NO_ERROR);
    paver_.Close();
}

TEST_F(PaverTest, OpenWriteInvalidSize) {
    ASSERT_NE(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 0), TFTP_NO_ERROR);
}

TEST_F(PaverTest, OpenWriteValidFile) {
    ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
    paver_.Close();
}

TEST_F(PaverTest, OpenTwice) {
    ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
    ASSERT_NE(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
    paver_.Close();
}

TEST_F(PaverTest, WriteWithoutOpen) {
    size_t size = sizeof(kFakeData);
    ASSERT_NE(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
}

TEST_F(PaverTest, WriteAfterClose) {
    size_t size = sizeof(kFakeData);
    ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
    paver_.Close();
    // TODO(surajmalhotra): Should we ensure this fails?
    ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
    ASSERT_EQ(size, sizeof(kFakeData));
}

TEST_F(PaverTest, TimeoutNoWrites) {
    ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
    paver_.Close();
    Wait();
    ASSERT_NE(paver_.exit_code(), ZX_OK);
}

TEST_F(PaverTest, TimeoutPartialWrite) {
    size_t size = sizeof(kFakeData);
    ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
    ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
    ASSERT_EQ(size, sizeof(kFakeData));
    paver_.Close();
    Wait();
    ASSERT_NE(paver_.exit_code(), ZX_OK);
}

TEST_F(PaverTest, WriteCompleteSingle) {
    size_t size = sizeof(kFakeData);
    fake_svc_.fake_paver().set_expected_payload_size(size);
    ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, size), TFTP_NO_ERROR);
    ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
    ASSERT_EQ(size, sizeof(kFakeData));
    paver_.Close();
    Wait();
    ASSERT_EQ(paver_.exit_code(), ZX_OK);
    ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteBootloader);
}

TEST_F(PaverTest, WriteCompleteManySmallWrites) {
    size_t size = sizeof(kFakeData);
    fake_svc_.fake_paver().set_expected_payload_size(1024);
    ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 1024), TFTP_NO_ERROR);
    for (size_t offset = 0; offset < 1024; offset += sizeof(kFakeData)) {
        size = std::min(sizeof(kFakeData), 1024 - offset);
        ASSERT_EQ(paver_.Write(kFakeData, &size, offset), TFTP_NO_ERROR);
        ASSERT_EQ(size, std::min(sizeof(kFakeData), 1024 - offset));
    }
    paver_.Close();
    Wait();
    ASSERT_OK(paver_.exit_code());
}

TEST_F(PaverTest, Overwrite) {
    size_t size = sizeof(kFakeData);
    ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 2), TFTP_NO_ERROR);
    ASSERT_NE(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
    paver_.Close();
    Wait();
    ASSERT_NE(paver_.exit_code(), ZX_OK);
}

TEST_F(PaverTest, CloseChannelBetweenWrites) {
    size_t size = sizeof(kFakeData);
    fake_svc_.fake_paver().set_expected_payload_size(2 * size);
    ASSERT_EQ(paver_.OpenWrite(NB_BOOTLOADER_FILENAME, 2 * size), TFTP_NO_ERROR);
    ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
    ASSERT_EQ(size, sizeof(kFakeData));
    loop_.Shutdown();
    ASSERT_EQ(paver_.Write(kFakeData, &size, size), TFTP_NO_ERROR);
    ASSERT_EQ(size, sizeof(kFakeData));
    paver_.Close();
    Wait();
    ASSERT_EQ(paver_.exit_code(), ZX_ERR_PEER_CLOSED);
}

TEST_F(PaverTest, WriteZirconA) {
    size_t size = sizeof(kFakeData);
    fake_svc_.fake_paver().set_expected_payload_size(size);
    ASSERT_EQ(paver_.OpenWrite(NB_ZIRCONA_FILENAME, size), TFTP_NO_ERROR);
    ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
    ASSERT_EQ(size, sizeof(kFakeData));
    paver_.Close();
    Wait();
    ASSERT_EQ(paver_.exit_code(), ZX_OK);
    ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteAsset);
}

TEST_F(PaverTest, WriteVbMetaA) {
    size_t size = sizeof(kFakeData);
    fake_svc_.fake_paver().set_expected_payload_size(size);
    ASSERT_EQ(paver_.OpenWrite(NB_VBMETAA_FILENAME, size), TFTP_NO_ERROR);
    ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
    ASSERT_EQ(size, sizeof(kFakeData));
    paver_.Close();
    Wait();
    ASSERT_EQ(paver_.exit_code(), ZX_OK);
    ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteAsset);
}

TEST_F(PaverTest, WriteSshAuth) {
    size_t size = sizeof(kFakeData);
    fake_svc_.fake_paver().set_expected_payload_size(size);
    ASSERT_EQ(paver_.OpenWrite(NB_SSHAUTH_FILENAME, size), TFTP_NO_ERROR);
    ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
    ASSERT_EQ(size, sizeof(kFakeData));
    paver_.Close();
    Wait();
    ASSERT_EQ(paver_.exit_code(), ZX_OK);
    ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteDataFile);
}

TEST_F(PaverTest, WriteFvm) {
    size_t size = sizeof(kFakeData);
    fake_svc_.fake_paver().set_expected_payload_size(size);
    ASSERT_EQ(paver_.OpenWrite(NB_FVM_FILENAME, size), TFTP_NO_ERROR);
    ASSERT_EQ(paver_.Write(kFakeData, &size, 0), TFTP_NO_ERROR);
    ASSERT_EQ(size, sizeof(kFakeData));
    paver_.Close();
    Wait();
    ASSERT_EQ(paver_.exit_code(), ZX_OK);
    ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteVolumes);
}

TEST_F(PaverTest, WriteFvmManySmallWrites) {
    size_t size = sizeof(kFakeData);
    fake_svc_.fake_paver().set_expected_payload_size(1024);
    ASSERT_EQ(paver_.OpenWrite(NB_FVM_FILENAME, 1024), TFTP_NO_ERROR);
    for (size_t offset = 0; offset < 1024; offset += sizeof(kFakeData)) {
        size = std::min(sizeof(kFakeData), 1024 - offset);
        ASSERT_EQ(paver_.Write(kFakeData, &size, offset), TFTP_NO_ERROR);
        ASSERT_EQ(size, std::min(sizeof(kFakeData), 1024 - offset));
    }
    paver_.Close();
    Wait();
    ASSERT_OK(paver_.exit_code());
    ASSERT_EQ(fake_svc_.fake_paver().last_command(), Command::kWriteVolumes);
}
