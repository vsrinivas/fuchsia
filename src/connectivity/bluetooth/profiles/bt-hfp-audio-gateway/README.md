# Bluetooth Profile: Hands-Free Profile Audio Gateway (AG)

This component implements the Audio Gateway role of Hands-Free Profile (HFP) version 1.8 as
specified by the Bluetooth SIG in the
[official specification](https://www.bluetooth.org/DocMan/handlers/DownloadDoc.ashx?doc_id=489628).

This means that you can use your Fuchsia device to connect with Bluetooth to a headset with speakers
and microphone that supports the Hands-Free role. Audio data is streamed between the two devices
in real-time to support interactive voice calls.

HFP includes support for user-initiated actions on the Hands-Free device, such as answering calls,
hanging up calls, and volume control. See the specification for a complete list of functionality.

## Build Configuration

Add the following to your Fuchsia set configuration to include the profile component:

`--with //src/connectivity/bluetooth/profiles/bt-hfp-audio-gateway`

### Profile Startup

Component startup differs based on the component framework version.

#### Component Framework v2

Currently, the only way to start the v2 component is via protocol discovery. Include the appropriate
`core_shard` in your product config target. For configurations in which both A2DP and HFP AG support
is desired, include the `bt-headset-core-shard`. For configurations in which A2DP is not desired
but HFP AG is, include the `bt-hfp-audio-gateway-core-shard`.
When the `fuchsia.bluetooth.hfp.Hfp` FIDL capability is requested, the CFv2 HFP AG component will be
started.

## Running tests

HFP relies on unit tests to validate behavior. Add the following to your Fuchsia set configuration
to include the profile unit tests:

`--with //src/connectivity/bluetooth/profiles/bt-hfp-audio-gateway:bt-hfp-audio-gateway-tests`

To run the tests:

```
fx test bt-hfp-audio-gateway-tests
```

## Inspect

The `bt-hfp-audio-gateway` component includes support for
[component inspection](https://fuchsia.dev/fuchsia-src/development/diagnostics/inspect). To view
the current state of the component, use `fx iquery show core/bt-hfp-audio-gateway`.

### Hierarchy

```
root:
  hfp:
    autoconnect: (true / false)
    audio_gateway_feature_support:
      reject_incoming_voice_call
      three_way_calling
      in_band_ringtone
      echo_canceling_and_noise_reduction
      voice_recognition
      attach_phone_number_to_voice_tag
      remote_audio_volume_control
      respond_and_hold
      enhanced_call_controls
      wide_band_speech
      enhanced_voice_recognition
      enhanced_voice_recognition_with_text
    call_manager:
        manager_connection_id
        connected: (true / false)
    peers:
      peer_0:
        task:
          peer_id
          connected_peer_handler: (true / false)
          calls:
            call_0:
              call_state
              is_incoming
          network:
            service_available
            signal_strength
            roaming
          hf_battery_level
          sco_connection:
            status
            parameters:
              parameter_set
              air_coding_format
              air_frame_size
              io_bandwidth
              io_coding_format
              io_frame_size
              io_pcm_data_format
              io_pcm_sample_payload_msb_position
              path
          service_level_connection:
            connected_at
            initialized_at
            hf_supported_codecs
            selected_codec
            handsfree_feature_support:
              echo_canceling_and_noise_reduction
              three_way_calling
              cli_presentation
              voice_recognition_activation
              remote_volume_control
              enhanced_call_status
              enhanced_call_control
              codec_negotiation
              handsfree_indicators
              esco_s4
              enhanced_voice_recognition
              enhanced_voice_recognition_with_text
            extended_errors
            call_waiting_notifications
            call_line_ident_notifications
            procedures:
              procedure_0:
                name
                started_at
                completed_at
              procedure_1:
                name
                started_at

```
