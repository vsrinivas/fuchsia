// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_buffer.h"
#include "codec_client.h"
#include "codec_output.h"
#include "util.h"

#include <fbl/auto_call.h>
#include <lib/async-loop/cpp/loop.h>

#include "garnet/lib/media/wav_writer/wav_writer.h"

#include <thread>

// Re. this example and threading:
//
// This example shows the handling needed to run a Codec using mulitiple client
// threads correctly.  Any new codec client author should consider whether the
// benefits of using multiple threads are really worthwhile in the client's
// particular use case, or if goals can be met just fine with a single FIDL
// thread doing everything.  Which is "best" depends on many factors, both
// technical and otherwise.  The main downsides of doing everything on one FIDL
// thread from a codec point of view is potential lower responsiveness or timing
// glitches/hiccups especially if the single FIDL thread is used for additional
// non-FIDL things including _any_ time-consuming thing.  If everything on the
// single FIDL thread is as async as possible and doesn't block the thread
// (including during things like buffer allocation via some other service), then
// it can work well - but fully achieving that of course brings its own fun.
// Consider that buffer allocation can _potentially_ take some duration that's
// not entirely under the client's control, so blocking a FIDL thread during
// that wouldn't be good for responsiveness of the FIDL thread.
//
// TODO(dustingreen): Write another simpler example that shows how to use the
// codec with only a single FIDL thread, single stream_lifetime_ordinal assumed,
// minimal output config change handling, maybe output config handling directly
// on the FIDL thread (despite potential for some duration not under the
// client's control there), not reserving any packets for the client, etc.

// The VLOGF() and LOGF() macros are here because we want the calls sites to
// look like FX_VLOGF and FX_LOGF, but without hard-wiring to those.  For now,
// printf() seems to work fine.

namespace {

// This example only has one stream_lifetime_ordinal which is 1.
//
// TODO(dustingreen): actually re-use the Codec instance for at least one more
// stream, even if it's just to decode the same data again.
constexpr uint64_t kStreamLifetimeOrdinal = 1;

// Whether we actaully output a wav depends on whether there are any args or
// not.
constexpr bool kWavWriterEnabled = true;

std::unique_ptr<uint8_t[]> make_AudioSpecificConfig_from_ADTS_header(
    std::unique_ptr<uint8_t[]>* input_bytes) {
  std::unique_ptr<uint8_t[]> asc = std::make_unique<uint8_t[]>(2);

  // TODO(dustingreen): Switch from ADTS to .mp4 and fix AAC decoder to not
  // require "AudioSpecificConfig()" when fed ADTS.  In other words, move the
  // stuff here into a shim around the AAC OMX decoder, just next to (above or
  // below) the OmxCodecRunner in the codec_runner_sw_omx isolate, probably.

  // For SoftAAC2.cpp, for no particularly good reason, a CODECCONFIG buffer is
  // expected, even when running in ADTS mode, despite all the relevant data
  // being available from the ADTS header.  The CODECCONFIG buffer has an
  // AudioSpecificConfig in it.  The AudioSpecificConfig has to be created based
  // on corresponding fields of the ADTS header - not that requiring this of
  // the codec client makes any sense whatsoever...
  //
  // TODO(dustingreen): maybe add a per-codec compensation layer to un-crazy the
  // quirks of each codec.  For example, when decoding ADTS, all the needed info
  // is there in the ADTS stream directly.  No reason to hassle the codec client
  // for a pointless translated form of the same info.  In contrast, when it's
  // an mp4 file (or mkv, or whatever modern container format), the codec config
  // info is relevant.  But we should only force a client to provide it if
  // it's really needed.

  // First, parse the stuff that's needed from the first ADTS header.
  uint8_t* adts_header = static_cast<uint8_t*>(input_bytes->get());
  uint8_t profile_ObjectType;        // name in AAC spec in adts_fixed_header
  uint8_t sampling_frequency_index;  // name in AAC spec in adts_fixed_header
  uint8_t channel_configuration;     // name in AAC spec in adts_fixed_header
  profile_ObjectType = (adts_header[2] >> 6) & 0x3;
  sampling_frequency_index = (adts_header[2] >> 2) & 0xf;
  if (sampling_frequency_index >= 11) {
    Exit("sampling frequency index too large: %d - exiting\n",
         sampling_frequency_index);
  }
  channel_configuration = (adts_header[2] & 0x1) << 2 | (adts_header[3] >> 6);

  // Now let's convert these to the forms needed by AudioSpecificConfig.
  uint8_t audioObjectType =
      profile_ObjectType + 1;  // see near Table 1.A.11, for AAC not MPEG-2
  uint8_t samplingFrequencyIndex =
      sampling_frequency_index;                          // no conversion needed
  uint8_t channelConfiguration = channel_configuration;  // no conversion needed
  uint8_t frameLengthFlag = 0;
  uint8_t dependsOnCoreCoder = 0;
  uint8_t extensionFlag = 0;

  // Now we are ready to build a two-byte AudioSpecificConfig.  Not an
  // AudioSpecificInfo as stated in avc_utils.cpp (AOSP) mind you, but an
  // AudioSpecificConfig.
  uint8_t* asc_header = static_cast<uint8_t*>(asc.get());
  asc_header[0] = (audioObjectType << 3) | (samplingFrequencyIndex >> 1);
  asc_header[1] = (samplingFrequencyIndex << 7) | (channelConfiguration << 3) |
                  (frameLengthFlag << 2) | (dependsOnCoreCoder << 1) |
                  (extensionFlag << 0);

  return asc;
}

}  // namespace

// On success, out_md contains the sha256 digest of the output audio data.  This
// is intended as a golden-file value when this function is used as part of a
// test. This sha256 accounts for all the output WAV data and also the audio
// output format parameters.  When the same input file is decoded we expect the
// sha256 to be the same.
//
// codec_factory - codec_factory to take ownership of, use, and close by the
//     time the function returns.  This InterfacePtr would typically be obtained
//     by calling
//     startup_context->ConnectToEnvironmentService<fuchsia::mediacodec::CodecFactory>().
// input_adts_file - This must be set and must be the filename of an input .adts
//     file (input file extension not checked / doesn't matter).
// output_wav_file - If nullptr, don't write the audio data to a wav file.  If
//     non-nullptr, output audio data to the specified wav file.  When used as
//     an example, this will tend to be set.  When used as a test, this will not
//     be set.
// out_md - SHA256_DIGEST_LENGTH bytes long
void use_aac_decoder(fuchsia::mediacodec::CodecFactoryPtr codec_factory,
                     const std::string& input_adts_file,
                     const std::string& output_wav_file, uint8_t* out_md) {
  memset(out_md, 0, SHA256_DIGEST_LENGTH);

  // In this example code, we're using this async::Loop for everything
  // FIDL-related in this function.  We explicitly specify which loop for all
  // activity initiated by this function.  We post to the loop and want to make
  // sure it's the same loop.  We rely on it being the same loop for serializing
  // sending of messages via CodecPtr.  We need to serialize sending messages
  // since ProxyController isn't thread safe for sending messages at least in
  // the case where sent requests require responses.  We could use something
  // like a send lock, but that would require locking around every send, even
  // those sends which are already on the loop thread, vs. what we're doing
  // which only needs anything extra for sends we queue from threads that aren't
  // the loop thread.
  async::Loop loop;

  // This example will give the loop it's own thread, so that the main thread
  // can be used to sequence overall control of the Codec instance using a
  // thread instead of chaining together a bunch of async activity (which would
  // be more complicated to understand and serve little purpose in an example
  // program like this).
  loop.StartThread("use_aac_decoder_loop");
  // From this point forward, because the loop is already running, this example
  // needs to be careful to be ready for all potential FIDL channel messages and
  // errors before attaching the channel to the loop.  The loop will continue
  // running until after we've deleted all the stuff using the loop.

  // This example has these threads:
  //  * main thread - used for setup and to drive overall sequence progression
  //    without resorting to a bunch of chained async actions, so that the
  //    example can remain reasonably clear in terms of what overall big-picture
  //    steps are taken in what overall order.  Note that any calls to FIDL
  //    interfaces are posted to the loop thread.
  //  * loop thread - this thread pumps all the FIDL interfaces in this example,
  //    using a separate thread from the main thread.  This does require us to
  //    be a bit more careful to fully configure an interface to be ready to
  //    recieve messages before binding a server endpoint locally, or fully
  //    ready to have error handler (or similar) called on the client end before
  //    sending the sever end somewhere else.
  //  * input thread - feeds in compressed input data - but note that the actual
  //    calls to any FIDL interface are posted to the loop thread.
  //  * output thread - accepts output data - but note that any actual calls to
  //    any FIDL interface are posted to the loop thread.

  // Current FIDL-related threading aspects:
  //   * Safe to call client-side proxy methods from multiple threads?  If there
  //     is a response: No.  If there is not a response: Maybe.  While a
  //     no-response send might currently be safe for one-way sends without a
  //     response_handler, we don't rely on that in this example.  Certainly any
  //     calls with a response need to be started from a single thread at a
  //     time.  Posting to the loop thread for all client-side sends covers this
  //     bullet.
  //   * Safe to call a server-side response_handler's operator() from an
  //     arbitrary server-side thread?  Yes, and seems likely to remain yes.
  //   * TODO(dustingreen): FIDL events will deserve their own bullet here.

  VLOGF("reading adts file...\n");
  size_t input_size;
  std::unique_ptr<uint8_t[]> input_bytes =
      read_whole_file(input_adts_file.c_str(), &input_size);
  VLOGF("done reading adts file.\n");

  codec_factory.set_error_handler([] {
    // TODO(dustingreen): get and print CodecFactory channel epitaph once that's
    // possible.
    LOGF("codec_factory failed - unexpected\n");
  });

  VLOGF("before make_AudioSpecificConfig_from_ADTS_header()...\n");
  VLOGF("input_bytes->get(): %p\n", input_bytes.get());
  std::unique_ptr<uint8_t[]> asc =
      make_AudioSpecificConfig_from_ADTS_header(&input_bytes);
  VLOGF("after make_AudioSpecificConfig_from_ADTS_header()\n");
  std::vector<uint8_t> asc_vector(2);
  for (int i = 0; i < 2; i++) {
    asc_vector[i] = asc[i];
  }

  // Set all fields to 0 / default.
  fuchsia::mediacodec::CreateDecoder_Params params = {};
  // TODO(dustingreen): Remove need for ADTS to specify any codec config since
  // it's in-band, and maybe switch this program over to using .mp4 with
  // AudioSpecificConfig() from the .mp4 file.
  params.input_details.format_details_version_ordinal = 0;
  params.input_details.mime_type = "audio/aac-adts";
  params.input_details.codec_oob_bytes.reset(std::move(asc_vector));

  // We're using CodecPtr here rather than CodecSyncPtr partly to have this
  // example program be slightly more realistic (with respect to client programs
  // that choose to use the async interface), and partly to avoid having to
  // separately check the error return code of every call, since the sync proxy
  // doesn't have any way to get an async error callback (that I've found).
  //
  // We let the CodecClient handle the creation of the CodecPtr, because the
  // loop is already running, and we want the error handler to be set up by
  // CodecClient in advance of the channel potentially being closed.
  VLOGF("before CodecClient::CodecClient()...\n");
  CodecClient codec_client(&loop);
  VLOGF("before codec_factory->CreateAudioDecoder().\n");
  codec_factory->CreateDecoder(std::move(params),
                               codec_client.GetTheRequestOnce());
  VLOGF("before codec_client.Start()...\n");
  codec_client.Start();

  // We don't need the CodecFactory any more, and at this point any Codec
  // creation errors have had a chance to arrive via the
  // codec_factory.set_error_handler() lambda.
  codec_factory.Unbind();

  // We use a seprarate thread to provide input data, separate thread for output
  // data, and a separate FIDL thread (started above).  This is to avoid the
  // example being too simplistic to be of any use as a reference to authors of
  // codec clients that use multiple threads, and to some degree, to keep the
  // input handling and output handling code clear.

  // The captures here require the main thread to call in_thread->join() before
  // the caputures go out of scope.
  VLOGF("before starting in_thread...\n");
  std::unique_ptr<std::thread> in_thread = std::make_unique<std::thread>(
      [&codec_client, &input_bytes, input_size]() {
        // "syncword" bits for ADTS are, starting at byte alignment: 0xFF 0xF.
        // That's 12 1 bits, with the first 1 bit starting at a byte aligned
        // boundary.
        //
        // Unfortunately, the "syncword" can show up in the middle of an aac
        // frame, which means the syncword is more of a heuristic than a real
        // sync.  In this case the test file is clean, so by parsing the aac
        // frame length we can skip forward and avoid getting fooled by the fake
        // syncword(s).
        auto queue_access_unit = [&codec_client, &input_bytes](
                                     uint8_t* bytes, size_t byte_count) {
          size_t bytes_so_far = 0;
          // printf("queuing offset: %ld byte_count: %zu\n", bytes -
          // input_bytes.get(), byte_count);
          while (bytes_so_far != byte_count) {
            std::unique_ptr<fuchsia::mediacodec::CodecPacket> packet =
                codec_client.BlockingGetFreeInputPacket();
            const CodecBuffer& buffer =
                codec_client.GetInputBufferByIndex(packet->header.packet_index);
            size_t bytes_to_copy =
                std::min(byte_count - bytes_so_far, buffer.size_bytes());
            packet->stream_lifetime_ordinal = kStreamLifetimeOrdinal;
            packet->start_offset = 0;
            packet->valid_length_bytes = bytes_to_copy;
            packet->timestamp_ish = 0;
            packet->start_access_unit = true;
            packet->known_end_access_unit = true;
            memcpy(buffer.base(), bytes + bytes_so_far, bytes_to_copy);
            codec_client.QueueInputPacket(std::move(packet));
            bytes_so_far += bytes_to_copy;
          }
        };
        int input_byte_count = input_size;
        for (int i = 0; i < input_byte_count - 1;) {
          if (!(input_bytes[i] == 0xFF &&
                ((input_bytes[i + 1] & 0xF0) == 0xF0))) {
            printf("s");
            i++;
            continue;
          }
          uint32_t bytes_left = input_byte_count - i;
          uint8_t* adts_header = &input_bytes[i];
          bool protection_absent = (adts_header[1] & 1);
          uint32_t adts_header_size = protection_absent ? 7 : 9;
          if (bytes_left < adts_header_size) {
            Exit(
                "input data corrupt (maybe truncated) - vs header length - "
                "bytes_left: %d adts_header_size: %d",
                bytes_left, adts_header_size);
          }
          uint32_t aac_frame_length = ((adts_header[3] & 3) << 11) |
                                      (adts_header[4] << 3) |
                                      (adts_header[5] >> 5);
          if (bytes_left < aac_frame_length) {
            Exit(
                "input data corrupt (maybe truncated) - vs frame length - "
                "bytes_left: %d aac_frame_length: %d",
                bytes_left, aac_frame_length);
          }
          queue_access_unit(&input_bytes[i], aac_frame_length);
          i += aac_frame_length;
        }

        // Send through QueueInputEndOfStream().
        codec_client.QueueInputEndOfStream(kStreamLifetimeOrdinal);
        // input thread done
      });

  // Separate thread to process the output.
  //
  // codec_client outlives the thread.
  std::unique_ptr<std::thread> out_thread = std::make_unique<
      std::thread>([&codec_client, output_wav_file, out_md]() {
    // The codec_client lock_ is not held for long durations in here, which is
    // good since we're using this thread to do things like write to a WAV
    // file.
    media::audio::WavWriter<kWavWriterEnabled> wav_writer;
    bool is_wav_initialized = false;
    SHA256_CTX sha256_ctx;
    SHA256_Init(&sha256_ctx);
    // We allow the server to send multiple output format updates if it wants;
    // see implementation of BlockingGetEmittedOutput() which will hide
    // multiple configs before the first packet from this code.
    //
    // In this example, we only deal with one output format once we start seeing
    // stream data show up, since WAV only supports a single format per file.
    std::shared_ptr<const fuchsia::mediacodec::CodecOutputConfig> stream_config;
    while (true) {
      std::unique_ptr<CodecOutput> output =
          codec_client.BlockingGetEmittedOutput();
      if (output->stream_lifetime_ordinal() != kStreamLifetimeOrdinal) {
        Exit(
            "server emitted a stream_lifetime_ordinal that client didn't set "
            "on any input");
      }
      if (output->end_of_stream()) {
        VLOGF("output end_of_stream() - done with output\n");
        // Just "break;" would be more fragile under code modification.
        goto end_of_output;
      }

      const fuchsia::mediacodec::CodecPacket& packet = output->packet();
      // "packet" will live long enough because ~cleanup runs before ~output.
      auto cleanup = fbl::MakeAutoCall([&codec_client, &packet] {
        // Using an auto call for this helps avoid losing track of the
        // output_buffer.
        //
        // If the omx_state_ or omx_state_desired_ isn't correct,
        // UseOutputBuffer() will fail.  The only way that can happen here is
        // if the OMX codec transitioned states unilaterally without any set
        // state command, so if that occurs, exit.
        codec_client.RecycleOutputPacket(packet.header);
      });
      std::shared_ptr<const fuchsia::mediacodec::CodecOutputConfig> config =
          output->config();
      // This will remain live long enough because this thread is the only
      // thread that re-allocates output buffers.
      const CodecBuffer& buffer =
          codec_client.GetOutputBufferByIndex(packet.header.packet_index);

      if (stream_config &&
          (config->format_details.format_details_version_ordinal !=
           stream_config->format_details.format_details_version_ordinal)) {
        Exit(
            "codec server unexpectedly changed output format mid-stream - "
            "unexpected for this stream");
      }

      if (packet.valid_length_bytes == 0) {
        // The server should not generate any empty packets.
        Exit("broken server sent empty packet");
      }

      // We have a non-empty packet of the stream.

      if (!stream_config) {
        // Every output has a config.  This happens exactly once.
        stream_config = config;
        const fuchsia::mediacodec::CodecFormatDetails& format =
            stream_config->format_details;
        if (!format.domain->is_audio()) {
          Exit("!format.domain.is_audio() - unexpected");
        }
        const fuchsia::mediacodec::AudioFormat& audio = format.domain->audio();
        if (!audio.is_uncompressed()) {
          Exit("!audio.is_uncompressed() - unexpected");
        }
        const fuchsia::mediacodec::AudioUncompressedFormat& uncompressed =
            audio.uncompressed();
        if (!uncompressed.is_pcm()) {
          Exit("!uncompressed.is_pcm() - unexpected");
        }
        // For now, bail out if it's not audio PCM 16 bit 2 channel, if only
        // because that's what we expect from the one test file so far, so if
        // it's different it'll be good to know that immediately, for now.
        //
        // TODO(dustingreen): Try to figure out WAV channel ordering for > 2
        // channels so we can deal with > 2 channels correctly.  Tolerate sample
        // rates other than 44100.  Tolerate bits per sample other than 16.  Set
        // up test input files for those cases.
        //
        // TODO(dustingreen): Once we have a video decoder, update this example
        // to handle at least one video format, or create a separate example for
        // video with some of the code in this file moved to a source_set.
        const fuchsia::mediacodec::PcmFormat& pcm = uncompressed.pcm();
        if (pcm.channel_map->size() < 1 || pcm.channel_map->size() > 2) {
          Exit(
              "pcm.channel_map->size() outside range [1, 2] - unexpected - "
              "actual: %zu\n",
              pcm.channel_map->size());
        }
        if (static_cast<fuchsia::mediacodec::AudioChannelId>(
                (*pcm.channel_map)[0]) !=
            fuchsia::mediacodec::AudioChannelId::LF) {
          Exit(
              "pcm.channel_map[0] is unexpected given the input data used in "
              "this example");
        }
        if (pcm.channel_map->size() >= 2 &&
            static_cast<fuchsia::mediacodec::AudioChannelId>(
                (*pcm.channel_map)[1]) !=
                fuchsia::mediacodec::AudioChannelId::RF) {
          Exit(
              "pcm.channel_map[1] is unexpected given the input data used in "
              "this example");
        }
        if (pcm.bits_per_sample != 16) {
          Exit("pcm.bits_per_sample != 16 - unexpected - actual: %d",
               pcm.bits_per_sample);
        }
        if (pcm.frames_per_second != 44100) {
          Exit("pcm.frames_per_second != 44100 - unexpected - actual: %d",
               pcm.frames_per_second);
        }
        if (!output_wav_file.empty()) {
          if (!wav_writer.Initialize(
                  output_wav_file.c_str(),
                  fuchsia::media::AudioSampleFormat::SIGNED_16,
                  pcm.channel_map->size(), pcm.frames_per_second,
                  pcm.bits_per_sample)) {
            Exit("wav_writer.Initialize() failed");
          }
          is_wav_initialized = true;
        }
        SHA256_Update_AudioParameters(&sha256_ctx, pcm);
      }

      // We have a non-empty buffer (EOS or not), so write the audio data to the
      // WAV file.
      if (is_wav_initialized) {
        if (!wav_writer.Write(buffer.base() + packet.start_offset,
                              packet.valid_length_bytes)) {
          Exit("wav_writer.Write() failed");
        }
      }
      int16_t* int16_base =
          reinterpret_cast<int16_t*>(buffer.base() + packet.start_offset);
      for (size_t iter = 0; iter < packet.valid_length_bytes / sizeof(int16_t);
           iter++) {
        int16_t data_le = htole16(int16_base[iter]);
        SHA256_Update(&sha256_ctx, &data_le, sizeof(data_le));
      }
    }
  end_of_output:;
    if (is_wav_initialized) {
      wav_writer.Close();
    }
    if (!SHA256_Final(out_md, &sha256_ctx)) {
      assert(false);
    }
    // output thread done
  });

  // decode some audio for a bit...  in_thread, loop, out_thread, and the codec
  // itself are taking care of it.

  // First wait for the input thread to be done feeding input data.  Before the
  // in_thread terminates, it'll have sent in a last empty EOS input buffer.
  VLOGF("before in_thread->join()...\n");
  in_thread->join();
  VLOGF("after in_thread->join()\n");

  // The EOS queued as an input buffer should cause the codec to output an EOS
  // output buffer, at which point out_thread should terminate, after it has
  // finalized the output WAV file.
  VLOGF("before out_thread->join()...\n");
  out_thread->join();
  VLOGF("after out_thread->join()\n");

  // Because CodecClient posted work to the loop which captured the CodecClient
  // as "this", it's important that we ensure that all such work is done trying
  // to run before we delete CodecClient.  We need to know that the work posted
  // using PostSerial() won't be trying to touch the channel or pointers that
  // are owned by CodecClient before we close the channel or destruct
  // CodecClient (which happens before ~loop).
  //
  // We call loop.Quit();loop.JoinThreads(); before codec_client.Stop() because
  // there can be at least a RecycleOutputPacket() still working its way toward
  // the Codec (via the loop) at this point, so doing
  // loop.Quit();loop.JoinThreads(); first avoids potential FIDL message send
  // errors.  We're done decoding so we don't care whether any remaining queued
  // messages toward the codec actually reach the codec.
  //
  // We use loop.Quit();loop.JoinThreads(); instead of loop.Shutdown() because
  // we don't want the Shutdown() side-effect of failing the channel bindings.
  // The Shutdown() will happen later.
  //
  // By ensuring that the loop is done running code before closing the channel
  // (or loop.Shutdown()), we can close the channel cleanly and avoid mitigation
  // of expected normal channel closure (or loop.Shutdown()) in any code that
  // runs on the loop.  This way, unexpected channel failure is the only case to
  // worry about.
  VLOGF("before loop.Quit()\n");
  loop.Quit();
  VLOGF("before loop.JoinThreads()...\n");
  loop.JoinThreads();
  VLOGF("after loop.JoinThreads()\n");

  // Close the channel explicitly (just so we can more easily print messages
  // before and after vs. ~codec_client).
  VLOGF("before codec_client stop...\n");
  codec_client.Stop();
  VLOGF("after codec_client stop.\n");

  // loop.Shutdown() the rest of the way explicitly (just so we can more easily
  // print messages before and after vs. ~loop).  If we did this before
  // codec_client.Stop() it would cause the channel bindings to fail because
  // async waits are failed as cancelled during Shutdown().
  VLOGF("before loop.Shutdown()...\n");
  loop.Shutdown();
  VLOGF("after loop.Shutdown()\n");

  // The FIDL loop isn't running any more and the channels are closed.  There
  // are no other threads left that were started by this function.  We can just
  // delete stuff now.

  // success
  // ~codec_client
  // ~loop
  // ~codec_factory
  return;
}
