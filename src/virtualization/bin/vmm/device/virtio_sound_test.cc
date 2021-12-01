// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include <virtio/sound.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/bin/vmm/device/test_with_device.h"
#include "src/virtualization/bin/vmm/device/virtio_queue_fake.h"

static constexpr char kVirtioSoundUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_sound#meta/virtio_sound.cmx";

namespace {

class FakeAudio : public fuchsia::media::Audio {
 public:
  FakeAudio(fidl::InterfaceRequest<fuchsia::media::Audio> request, async_dispatcher_t* dispatcher)
      : binding_(this, std::move(request), dispatcher) {}

  void CreateAudioRenderer(
      fidl::InterfaceRequest<fuchsia::media::AudioRenderer> audio_renderer_request) final {
    // not implemented yet
  }
  void CreateAudioCapturer(
      fidl::InterfaceRequest<fuchsia::media::AudioCapturer> audio_capturer_request,
      bool loopback) final {
    // not implemented yet
  }

 private:
  fidl::Binding<fuchsia::media::Audio> binding_;
};

static inline uint64_t bit(uint64_t n) { return 1ul << n; }

enum QueueId {
  CONTROLQ = 0,
  EVENTQ = 1,
  TXQ = 2,
  RXQ = 3,
};

static const uint32_t kNumJacks = 1;
static const uint32_t kNumStreams = 2;
static const uint32_t kNumChmaps = 3;

struct QueueConfig {
  uint16_t descriptors;
  size_t data_bytes;
};

static constexpr QueueConfig kQueueConfigs[4] = {
    {16, 16 * 128},  // all req+resp messages are < 128 bytes
    {16, 16 * 64},   // all messages are < 64 bytes
    {16, PAGE_SIZE},
    {16, PAGE_SIZE},
};

class VirtioSoundTest : public TestWithDevice {
 protected:
  VirtioSoundTest() {
    zx_gpaddr_t addr = 0;
    for (int k = 0; k < 4; k++) {
      queue_data_addrs_[k] = addr;
      addr += kQueueConfigs[k].data_bytes;
      queues_[k] = std::make_unique<VirtioQueueFake>(phys_mem_, addr, kQueueConfigs[k].descriptors);
      addr = queues_[k]->end();
    }
    phys_mem_size_ = addr;
  }

  void SetUp() override {
    // Launch fake audio service.
    fuchsia::media::AudioPtr audio;
    audio_service_ = std::make_unique<FakeAudio>(audio.NewRequest(), dispatcher());

    // Launch device process.
    fuchsia::virtualization::hardware::StartInfo start_info;
    zx_status_t status = LaunchDevice(kVirtioSoundUrl, phys_mem_size_, &start_info);
    ASSERT_EQ(ZX_OK, status);

    // Start device execution.
    services_->Connect(sound_.NewRequest());
    RunLoopUntilIdle();

    uint32_t features, jacks, streams, chmaps;
    status = sound_->Start(std::move(start_info), std::move(audio), &features, &jacks, &streams,
                           &chmaps);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(features, 0u);
    ASSERT_EQ(jacks, kNumJacks);
    ASSERT_EQ(streams, kNumStreams);
    ASSERT_EQ(chmaps, kNumChmaps);

    // Configure device queues.
    for (uint16_t k = 0; k < queues_.size(); k++) {
      auto& q = *queues_[k];
      q.Configure(queue_data_addrs_[k], kQueueConfigs[k].data_bytes);
      status = sound_->ConfigureQueue(k, q.size(), q.desc(), q.avail(), q.used());
      ASSERT_EQ(ZX_OK, status) << "failed to configure queue " << k;
    }

    // Finish negotiating features.
    status = sound_->Ready(0);
    ASSERT_EQ(ZX_OK, status);
  }

  VirtioQueueFake& controlq() { return *queues_[CONTROLQ]; }
  VirtioQueueFake& eventq() { return *queues_[EVENTQ]; }
  VirtioQueueFake& txq() { return *queues_[TXQ]; }
  VirtioQueueFake& rxq() { return *queues_[RXQ]; }

  zx_status_t NotifyQueue(QueueId id) { return sound_->NotifyQueue(id); }

 private:
  // Using a SyncPtr can risk deadlock if a method call on this SyncPtr needs to wait for
  // an audio_service_ method to return. However, this should never happen: we only call
  // this (a) during SetUp, and (b) to NotifyQueue, which doesn't depend on audio_service_.
  fuchsia::virtualization::hardware::VirtioSoundSyncPtr sound_;
  std::array<std::unique_ptr<VirtioQueueFake>, 4> queues_;
  zx_gpaddr_t queue_data_addrs_[4];
  size_t phys_mem_size_;
  std::unique_ptr<FakeAudio> audio_service_;
};

}  // namespace

TEST_F(VirtioSoundTest, BadRequestNoReadableDescriptors) {
  virtio_snd_hdr* resphdr;
  virtio_snd_jack_info_t* resp;
  zx_status_t status = DescriptorChainBuilder(controlq())
                           .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                           .AppendWritableDescriptor(&resp, kNumJacks * sizeof(*resp))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = NotifyQueue(CONTROLQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_BAD_MSG);
}

TEST_F(VirtioSoundTest, BadRequestHeaderTooSmall) {
  char query = 0;
  static_assert(1 < sizeof(virtio_snd_hdr));

  virtio_snd_hdr* resphdr;
  virtio_snd_jack_info_t* resp;
  zx_status_t status = DescriptorChainBuilder(controlq())
                           .AppendReadableDescriptor(&query, 1)
                           .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                           .AppendWritableDescriptor(&resp, kNumJacks * sizeof(*resp))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = NotifyQueue(CONTROLQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_BAD_MSG);
}

TEST_F(VirtioSoundTest, GetJackInfos) {
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_JACK_INFO},
      .start_id = 0,
      .count = kNumJacks,
      .size = sizeof(virtio_snd_query_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_jack_info_t* resp;
  zx_status_t status = DescriptorChainBuilder(controlq())
                           .AppendReadableDescriptor(&query, sizeof(query))
                           .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                           .AppendWritableDescriptor(&resp, kNumJacks * sizeof(*resp))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = NotifyQueue(CONTROLQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  static_assert(kNumJacks == 1);

  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_OK);
  EXPECT_EQ(resp->hdr.hda_fn_nid, 0u);
  EXPECT_EQ(resp->features, 0u);
  EXPECT_EQ(resp->hda_reg_defconf, 0x90100010u);
  EXPECT_EQ(resp->hda_reg_caps, 0x30u);
  EXPECT_EQ(resp->connected, 1u);
}

TEST_F(VirtioSoundTest, GetPcmInfos) {
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_PCM_INFO},
      .start_id = 0,
      .count = kNumStreams,
      .size = sizeof(virtio_snd_query_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_pcm_info_t* resp;
  zx_status_t status = DescriptorChainBuilder(controlq())
                           .AppendReadableDescriptor(&query, sizeof(query))
                           .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                           .AppendWritableDescriptor(&resp, kNumStreams * sizeof(*resp))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = NotifyQueue(CONTROLQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_OK);

  for (size_t k = 0; k < kNumStreams; k++) {
    SCOPED_TRACE(fxl::StringPrintf("stream %lu", k));
    uint64_t supported_formats = bit(VIRTIO_SND_PCM_FMT_U8) | bit(VIRTIO_SND_PCM_FMT_S16) |
                                 bit(VIRTIO_SND_PCM_FMT_S24) | bit(VIRTIO_SND_PCM_FMT_FLOAT);
    uint64_t supported_rates = bit(VIRTIO_SND_PCM_RATE_8000) | bit(VIRTIO_SND_PCM_RATE_11025) |
                               bit(VIRTIO_SND_PCM_RATE_16000) | bit(VIRTIO_SND_PCM_RATE_22050) |
                               bit(VIRTIO_SND_PCM_RATE_32000) | bit(VIRTIO_SND_PCM_RATE_44100) |
                               bit(VIRTIO_SND_PCM_RATE_48000) | bit(VIRTIO_SND_PCM_RATE_64000) |
                               bit(VIRTIO_SND_PCM_RATE_88200) | bit(VIRTIO_SND_PCM_RATE_96000) |
                               bit(VIRTIO_SND_PCM_RATE_176400) | bit(VIRTIO_SND_PCM_RATE_192000);

    EXPECT_EQ(resp[k].hdr.hda_fn_nid, 0u);
    EXPECT_EQ(resp[k].features, 0u);
    EXPECT_EQ(resp[k].formats, supported_formats);
    EXPECT_EQ(resp[k].rates, supported_rates);
    EXPECT_EQ(resp[k].direction, (k == 0) ? VIRTIO_SND_D_OUTPUT : VIRTIO_SND_D_INPUT);
    EXPECT_EQ(resp[k].channels_min, 1u);
    EXPECT_EQ(resp[k].channels_max, (k == 0) ? 2u : 1u);
  }
}

TEST_F(VirtioSoundTest, GetChmapInfos) {
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_CHMAP_INFO},
      .start_id = 0,
      .count = kNumChmaps,
      .size = sizeof(virtio_snd_query_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_chmap_info_t* resp;
  zx_status_t status = DescriptorChainBuilder(controlq())
                           .AppendReadableDescriptor(&query, sizeof(query))
                           .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                           .AppendWritableDescriptor(&resp, kNumChmaps * sizeof(*resp))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = NotifyQueue(CONTROLQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_OK);

  for (size_t k = 0; k < kNumChmaps; k++) {
    SCOPED_TRACE(fxl::StringPrintf("chmap %lu", k));

    EXPECT_EQ(resp[k].hdr.hda_fn_nid, 0u);
    EXPECT_EQ(resp[k].direction, (k < 2) ? VIRTIO_SND_D_OUTPUT : VIRTIO_SND_D_INPUT);
    if (k % 2 == 0) {
      // mono
      EXPECT_EQ(resp[k].channels, 1u);
      EXPECT_EQ(resp[k].positions[0], VIRTIO_SND_CHMAP_MONO);
    } else {
      // stereo
      EXPECT_EQ(resp[k].channels, 2u);
      EXPECT_EQ(resp[k].positions[0], VIRTIO_SND_CHMAP_FL);
      EXPECT_EQ(resp[k].positions[1], VIRTIO_SND_CHMAP_FR);
    }
  }
}

TEST_F(VirtioSoundTest, GetChmapInfosJustOne) {
  static_assert(kNumChmaps > 1);

  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_CHMAP_INFO},
      .start_id = 0,
      .count = 1,
      .size = sizeof(virtio_snd_query_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_chmap_info_t* resp;
  zx_status_t status = DescriptorChainBuilder(controlq())
                           .AppendReadableDescriptor(&query, sizeof(query))
                           .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                           .AppendWritableDescriptor(&resp, 1 * sizeof(*resp))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = NotifyQueue(CONTROLQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_OK);

  // chmaps[0] is OUTPUT, MONO
  EXPECT_EQ(resp[0].direction, VIRTIO_SND_D_OUTPUT);
  EXPECT_EQ(resp[0].channels, 1u);
}

TEST_F(VirtioSoundTest, GetChmapInfosSubset) {
  static_assert(kNumChmaps >= 3);

  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_CHMAP_INFO},
      .start_id = 1,
      .count = 2,
      .size = sizeof(virtio_snd_query_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_chmap_info_t* resp;
  zx_status_t status = DescriptorChainBuilder(controlq())
                           .AppendReadableDescriptor(&query, sizeof(query))
                           .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                           .AppendWritableDescriptor(&resp, 2 * sizeof(*resp))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = NotifyQueue(CONTROLQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_OK);

  // chmaps[0] is OUTPUT, MONO
  // chmaps[1] is OUTPUT, STEREO
  EXPECT_EQ(resp[0].direction, VIRTIO_SND_D_OUTPUT);
  EXPECT_EQ(resp[0].channels, 2u);
  // chmaps[2] is INPUT, MONO
  EXPECT_EQ(resp[1].direction, VIRTIO_SND_D_INPUT);
  EXPECT_EQ(resp[1].channels, 1u);
}

TEST_F(VirtioSoundTest, BadGetChmapInfosRequestTooSmall) {
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_CHMAP_INFO},
      .start_id = 0,
      .count = kNumChmaps,
      .size = sizeof(virtio_snd_query_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_chmap_info_t* resp;
  zx_status_t status = DescriptorChainBuilder(controlq())
                           .AppendReadableDescriptor(&query, sizeof(query) - 1)  // too small
                           .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                           .AppendWritableDescriptor(&resp, kNumChmaps * sizeof(*resp))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = NotifyQueue(CONTROLQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_BAD_MSG);
}

TEST_F(VirtioSoundTest, BadGetChmapInfosRequestTooManyInfos) {
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_CHMAP_INFO},
      .start_id = 0,
      .count = kNumChmaps + 1,  // too many
      .size = sizeof(virtio_snd_query_info_t),
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_chmap_info_t* resp;
  zx_status_t status = DescriptorChainBuilder(controlq())
                           .AppendReadableDescriptor(&query, sizeof(query))
                           .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                           .AppendWritableDescriptor(&resp, (kNumChmaps + 1) * sizeof(*resp))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = NotifyQueue(CONTROLQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_BAD_MSG);
}

TEST_F(VirtioSoundTest, BadGetChmapInfosRequestBadSize) {
  virtio_snd_query_info_t query = {
      .hdr = {.code = VIRTIO_SND_R_CHMAP_INFO},
      .start_id = 0,
      .count = kNumChmaps,
      .size = sizeof(virtio_snd_query_info_t) - 1,  // bad size
  };
  virtio_snd_hdr* resphdr;
  virtio_snd_chmap_info_t* resp;
  zx_status_t status = DescriptorChainBuilder(controlq())
                           .AppendReadableDescriptor(&query, sizeof(query))
                           .AppendWritableDescriptor(&resphdr, sizeof(*resphdr))
                           .AppendWritableDescriptor(&resp, kNumChmaps * sizeof(*resp))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = NotifyQueue(CONTROLQ);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(resphdr->code, VIRTIO_SND_S_BAD_MSG);
}
