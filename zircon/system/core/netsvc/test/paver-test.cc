// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "paver.h"

#include <algorithm>

#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl-async/cpp/bind.h>
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

class FakePaver : public ::llcpp::fuchsia::paver::Paver::Interface {
public:
    zx_status_t Connect(async_dispatcher_t* dispatcher, zx::channel request) {
        return fidl::Bind(dispatcher, std::move(request), this);
    }

    void QueryActiveConfiguration(QueryActiveConfigurationCompleter::Sync completer) {
        last_command_ = Command::kQueryActiveConfiguration;
        ::llcpp::fuchsia::paver::Paver_QueryActiveConfiguration_Result result;
        result.set_err(ZX_ERR_NOT_SUPPORTED);
        completer.Reply(std::move(result));
    }

    void SetActiveConfiguration(::llcpp::fuchsia::paver::Configuration configuration,
                                SetActiveConfigurationCompleter::Sync completer) {
        last_command_ = Command::kSetActiveConfiguration;
        completer.Reply(ZX_ERR_NOT_SUPPORTED);
    }

    void MarkActiveConfigurationSuccessful(
        MarkActiveConfigurationSuccessfulCompleter::Sync completer) {
        last_command_ = Command::kMarkActiveConfigurationSuccessful;
        completer.Reply(ZX_ERR_NOT_SUPPORTED);
    }

    void ForceRecoveryConfiguration(ForceRecoveryConfigurationCompleter::Sync completer) {
        last_command_ = Command::kForceRecoveryConfiguration;
        completer.Reply(ZX_ERR_NOT_SUPPORTED);
    }

    void WriteAsset(::llcpp::fuchsia::paver::Configuration configuration,
                    ::llcpp::fuchsia::paver::Asset asset,
                    ::llcpp::fuchsia::mem::Buffer payload, WriteAssetCompleter::Sync completer) {
        last_command_ = Command::kWriteAsset;
        auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
        completer.Reply(status);
    }

    void WriteVolumes(zx::channel payload_stream, WriteVolumesCompleter::Sync completer) {
        last_command_ = Command::kWriteVolumes;
        // Register VMO.
        zx::vmo vmo;
        auto status = zx::vmo::create(1024, 0, &vmo);
        if (status != ZX_OK) {
            completer.Reply(status);
            return;
        }
        ::llcpp::fuchsia::paver::PayloadStream::SyncClient stream(std::move(payload_stream));
        auto io_status = stream.RegisterVmo(std::move(vmo), &status);
        status = io_status == ZX_OK ? status : io_status;
        if (status != ZX_OK) {
            completer.Reply(status);
            return;
        }
        // Stream until EOF.
        status = [&]() {
            size_t data_transferred = 0;
            for (;;) {
                ::llcpp::fuchsia::paver::ReadResult result;
                auto io_status = stream.ReadData(&result);
                if (io_status != ZX_OK) {
                    return io_status;
                }
                switch (result.which()) {
                case ::llcpp::fuchsia::paver::ReadResult::Tag::kErr:
                    return result.err();
                case ::llcpp::fuchsia::paver::ReadResult::Tag::kEof:
                    return data_transferred == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
                case ::llcpp::fuchsia::paver::ReadResult::Tag::kInfo:
                    data_transferred += result.info().size;
                    continue;
                default:
                    return ZX_ERR_INTERNAL;
                }
            }
        }();

        completer.Reply(status);
    }

    void WriteBootloader(::llcpp::fuchsia::mem::Buffer payload,
                         WriteBootloaderCompleter::Sync completer) {
        last_command_ = Command::kWriteBootloader;
        auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
        completer.Reply(status);
    }

    void WriteDataFile(fidl::StringView filename, ::llcpp::fuchsia::mem::Buffer payload,
                       WriteDataFileCompleter::Sync completer) {
        last_command_ = Command::kWriteDataFile;
        auto status = payload.size == expected_payload_size_ ? ZX_OK : ZX_ERR_INVALID_ARGS;
        completer.Reply(status);
    }

    void WipeVolumes(WipeVolumesCompleter::Sync completer) {
        last_command_ = Command::kWipeVolumes;
        auto status = ZX_OK;
        completer.Reply(status);
    }

    Command last_command() { return last_command_; }
    void set_expected_payload_size(size_t size) { expected_payload_size_ = size; }

private:
    Command last_command_ = Command::kUnknown;
    size_t expected_payload_size_ = 0;
};

class FakeSvc {
public:
    explicit FakeSvc(async_dispatcher_t* dispatcher)
        : dispatcher_(dispatcher), vfs_(dispatcher) {
        auto root_dir = fbl::MakeRefCounted<fs::PseudoDir>();
        root_dir->AddEntry(::llcpp::fuchsia::paver::Paver::Name_,
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
