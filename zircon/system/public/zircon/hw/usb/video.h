// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_HW_USB_VIDEO_H_
#define SYSROOT_ZIRCON_HW_USB_VIDEO_H_

// clang-format off

#include <zircon/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS;

// video interface subclasses
#define USB_SUBCLASS_VIDEO_CONTROL                 0x01
#define USB_SUBCLASS_VIDEO_STREAMING               0x02
#define USB_SUBCLASS_VIDEO_INTERFACE_COLLECTION    0x03

// video class specific descriptor types
#define USB_VIDEO_CS_DEVICE                        0x21
#define USB_VIDEO_CS_CONFIGURATION                 0x22
#define USB_VIDEO_CS_STRING                        0x23
#define USB_VIDEO_CS_INTERFACE                     0x24
#define USB_VIDEO_CS_ENDPOINT                      0x25

// video class specific VC interface descriptor subtypes
#define USB_VIDEO_VC_HEADER                        0x01
#define USB_VIDEO_VC_INPUT_TERMINAL                0x02
#define USB_VIDEO_VC_OUTPUT_TERMINAL               0x03
#define USB_VIDEO_VC_SELECTOR_UNIT                 0x04
#define USB_VIDEO_VC_PROCESSING_UNIT               0x05
#define USB_VIDEO_VC_EXTENSION_UNIT                0x06
#define USB_VIDEO_VC_ENCODING_UNIT                 0x07

// video class specific VS interface descriptor subtypes
#define USB_VIDEO_VS_INPUT_HEADER                  0x01
#define USB_VIDEO_VS_OUTPUT_HEADER                 0x02
#define USB_VIDEO_VS_STILL_IMAGE_FRAME             0x03
#define USB_VIDEO_VS_FORMAT_UNCOMPRESSED           0x04
#define USB_VIDEO_VS_FRAME_UNCOMPRESSED            0x05
#define USB_VIDEO_VS_FORMAT_MJPEG                  0x06
#define USB_VIDEO_VS_FRAME_MJPEG                   0x07
#define USB_VIDEO_VS_FORMAT_MPEG2TS                0x0A
#define USB_VIDEO_VS_FORMAT_DV                     0x0C
#define USB_VIDEO_VS_COLORFORMAT                   0x0D
#define USB_VIDEO_VS_FORMAT_FRAME_BASED            0x10
#define USB_VIDEO_VS_FRAME_FRAME_BASED             0x11
#define USB_VIDEO_VS_FORMAT_STREAM_BASED           0x12
#define USB_VIDEO_VS_FORMAT_H264                   0x13
#define USB_VIDEO_VS_FRAME_H264                    0x14
#define USB_VIDEO_VS_FORMAT_H264_SIMULCAST         0x15
#define USB_VIDEO_VS_FORMAT_VP8                    0x16
#define USB_VIDEO_VS_FRAME_VP8                     0x17
#define USB_VIDEO_VS_FORMAT_VP8_SIMULCAST          0x18

// video class specific endpoint descriptor subtypes
#define USB_VIDEO_EP_GENERAL                       0x01
#define USB_VIDEO_EP_ENDPOINT                      0x02
#define USB_VIDEO_EP_INTERRUPT                     0x03

// video class specific request codes
#define USB_VIDEO_SET_CUR                          0x01
#define USB_VIDEO_SET_CUR_ALL                      0x11
#define USB_VIDEO_GET_CUR                          0x81
#define USB_VIDEO_GET_MIN                          0x82
#define USB_VIDEO_GET_MAX                          0x83
#define USB_VIDEO_GET_RES                          0x84
#define USB_VIDEO_GET_LEN                          0x85
#define USB_VIDEO_GET_INFO                         0x86
#define USB_VIDEO_GET_DEF                          0x87
#define USB_VIDEO_GET_CUR_ALL                      0x91
#define USB_VIDEO_GET_MIN_ALL                      0x92
#define USB_VIDEO_GET_MAX_ALL                      0x93
#define USB_VIDEO_GET_RES_ALL                      0x94
#define USB_VIDEO_GET_DEF_ALL                      0x97

// video streaming interface control selectors
#define USB_VIDEO_VS_PROBE_CONTROL                 0x01
#define USB_VIDEO_VS_COMMIT_CONTROL                0x02
#define USB_VIDEO_VS_STILL_PROBE_CONTROL           0x03
#define USB_VIDEO_VS_STILL_COMMIT_CONTROL          0x04
#define USB_VIDEO_VS_STILL_IMAGE_TRIGGER_CONTROL   0x05
#define USB_VIDEO_VS_STREAM_ERROR_CODE_CONTROL     0x06
#define USB_VIDEO_VS_GENERATE_KEY_FRAME_CONTROL    0x07
#define USB_VIDEO_VS_UPDATE_FRAME_SEGMENT_CONTROL  0x08
#define USB_VIDEO_VS_SYNCH_DELAY_CONTROL           0x09

// header for usb_video_vc_* below
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_VIDEO_CS_INTERFACE
    uint8_t bDescriptorSubtype;
} __PACKED usb_video_vc_desc_header;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_VIDEO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_VIDEO_VC_HEADER
    uint16_t bcdUVC;
    uint16_t wTotalLength;
    uint32_t dwClockFrequency;
    uint8_t bInCollection;
    uint8_t baInterfaceNr[];
} __PACKED usb_video_vc_header_desc;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_VIDEO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_VIDEO_VC_INPUT_TERMINAL
    uint8_t bTerminalID;
    uint16_t wTerminalType;
    uint8_t bAssocTerminal;
    uint8_t iTerminal;
} __PACKED usb_video_vc_input_terminal_desc;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_VIDEO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_VIDEO_VC_OUTPUT_TERMINAL
    uint8_t bTerminalID;
    uint16_t wTerminalType;
    uint8_t bAssocTerminal;
    uint8_t bSourceID;
    uint8_t iTerminal;
} __PACKED usb_video_vc_output_terminal_desc;

// class specific VC interrupt endpoint descriptor
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_VIDEO_CS_ENDPOINT
    uint8_t bDescriptorSubtype;     // USB_ENDPOINT_INTERRUPT
    uint16_t wMaxTransferSize;
} __PACKED usb_video_vc_interrupt_endpoint_desc;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;        // USB_VIDEO_CS_INTERFACE
    uint8_t bDescriptorSubtype;     // USB_VIDEO_VS_HEADER
    uint8_t bNumFormats;
    uint16_t wTotalLength;
    uint8_t bEndpointAddress;
    uint8_t bmInfo;
    uint8_t bTerminalLink;
    uint8_t bStillCaptureMethod;
    uint8_t bTriggerSupport;
    uint8_t bTriggerUsage;
    uint8_t bControlSize;
    uint8_t bmaControls[];
} __PACKED usb_video_vs_input_header_desc;

#define GUID_LENGTH 16

// A GUID consists of a:
//  - four-byte integer
//  - two-byte integer
//  - two-byte integer
//  - eight-byte array
//
// The string representation uses big endian format, so to convert it
// to a byte array we need to reverse the byte order of the three integers.
//
// See USB Video Class revision 1.5, FAQ section 2.9
// for GUID Data Structure Layout.

#define USB_VIDEO_GUID_YUY2_STRING "32595559-0000-0010-8000-00AA00389B71"
#define USB_VIDEO_GUID_YUY2_VALUE { \
    0x59, 0x55, 0x59, 0x32, \
    0x00, 0x00, \
    0x10, 0x00, \
    0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 \
}

#define USB_VIDEO_GUID_NV12_STRING "3231564E-0000-0010-8000-00AA00389B71"
#define USB_VIDEO_GUID_NV12_VALUE { \
    0x4e, 0x56, 0x31, 0x32, \
    0x00, 0x00, \
    0x10, 0x00, \
    0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 \
}

#define USB_VIDEO_GUID_M420_STRING "3032344D-0000-0010-8000-00AA00389B71"
#define USB_VIDEO_GUID_M420_VALUE { \
    0x4d, 0x34, 0x32, 0x30, \
    0x00, 0x00, \
    0x10, 0x00, \
    0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 \
}

#define USB_VIDEO_GUID_I420_STRING "30323449-0000-0010-8000-00AA00389B71"
#define USB_VIDEO_GUID_I420_VALUE { \
    0x49, 0x34, 0x32, 0x30, \
    0x00, 0x00, \
    0x10, 0x00, \
    0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 \
}

// USB Video Payload Uncompressed
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;         // USB_VIDEO_CS_INTERFACE
    uint8_t bDescriptorSubType;      // USB_VIDEO_VS_FORMAT_UNCOMPRESSED
    uint8_t bFormatIndex;
    uint8_t bNumFrameDescriptors;
    uint8_t guidFormat[GUID_LENGTH];
    uint8_t bBitsPerPixel;
    uint8_t bDefaultFrameIndex;
    uint8_t bAspectRatioX;
    uint8_t bAspectRatioY;
    uint8_t bmInterfaceFlags;
    uint8_t bCopyProtect;
} __PACKED usb_video_vs_uncompressed_format_desc;

// USB Video Payload MJPEG
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;         // USB_VIDEO_CS_INTERFACE
    uint8_t bDescriptorSubType;      // USB_VIDEO_VS_FORMAT_MJPEG
    uint8_t bFormatIndex;
    uint8_t bNumFrameDescriptors;
    uint8_t bmFlags;
    uint8_t bDefaultFrameIndex;
    uint8_t bAspectRatioX;
    uint8_t bAspectRatioY;
    uint8_t bmInterfaceFlags;
    uint8_t bCopyProtect;
} __PACKED usb_video_vs_mjpeg_format_desc;

// Uncompressed and MJPEG formats have the same frame descriptor structure.
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;         // USB_VIDEO_CS_INTERFACE
    uint8_t bDescriptorSubType;      // USB_VIDEO_VS_FRAME_UNCOMPRESSED / USB_VIDEO_VS_FRAME_MJPEG
    uint8_t bFrameIndex;
    uint8_t bmCapabilities;
    uint16_t wWidth;
    uint16_t wHeight;
    uint32_t dwMinBitRate;
    uint32_t dwMaxBitRate;
    uint32_t dwMaxVideoFrameBufferSize;
    uint32_t dwDefaultFrameInterval;
    uint8_t bFrameIntervalType;
    uint32_t dwFrameInterval[];
} __PACKED usb_video_vs_frame_desc;

// Stream negotiation
#define USB_VIDEO_BM_HINT_FRAME_INTERVAL        (1 << 0)
#define USB_VIDEO_BM_HINT_KEY_FRAME_RATE        (1 << 1)
#define USB_VIDEO_BM_HINT_P_FRAME_RATE          (1 << 2)
#define USB_VIDEO_BM_HINT_COMP_QUALITY          (1 << 3)
#define USB_VIDEO_BM_HINT_COMP_WINDOW_SIZE      (1 << 4)

typedef struct {
   uint16_t bmHint;
   uint8_t bFormatIndex;
   uint8_t bFrameIndex;
   uint32_t dwFrameInterval;
   uint16_t wKeyFrameRate;
   uint16_t wPFrameRate;
   uint16_t wCompQuality;
   uint16_t wCompWindowSize;
   uint16_t wDelay;
   uint32_t dwMaxVideoFrameSize;
   uint32_t dwMaxPayloadTransferSize;
   // The following fields are optional.
   uint32_t dwClockFrequency;
   uint8_t bmFramingInfo;
   uint8_t bPreferedVersion;
   uint8_t bMinVersion;
   uint8_t bMaxVersion;
   uint8_t bUsage;
   uint8_t bBitDepthLuma;
   uint8_t bmSettings;
   uint8_t bMaxNumberOfRefFramesPlus1;
   uint16_t bmRateControlModes;
   uint32_t bmLayoutPerStream;
} __PACKED usb_video_vc_probe_and_commit_controls;

// For accessing payload bmHeaderInfo bitmap
#define USB_VIDEO_VS_PAYLOAD_HEADER_FID         (1 << 0)
#define USB_VIDEO_VS_PAYLOAD_HEADER_EOF         (1 << 1)
#define USB_VIDEO_VS_PAYLOAD_HEADER_PTS         (1 << 2)
#define USB_VIDEO_VS_PAYLOAD_HEADER_SCR         (1 << 3)
#define USB_VIDEO_VS_PAYLOAD_HEADER_RES         (1 << 4)
#define USB_VIDEO_VS_PAYLOAD_HEADER_STI         (1 << 5)
#define USB_VIDEO_VS_PAYLOAD_HEADER_ERR         (1 << 6)
#define USB_VIDEO_VS_PAYLOAD_HEADER_EOH         (1 << 7)

// Common header for all payloads.
typedef struct {
    uint8_t bHeaderLength;
    uint8_t bmHeaderInfo;

} __PACKED usb_video_vs_payload_header;

typedef struct {
    uint8_t bHeaderLength;
    uint8_t bmHeaderInfo;
    uint32_t dwPresentationTime;
    uint32_t scrSourceTimeClock;
    // Frame number when the source clock was sampled.
    uint16_t scrSourceClockSOFCounter;
} __PACKED usb_video_vs_uncompressed_payload_header;

__END_CDECLS;


#endif  // SYSROOT_ZIRCON_HW_USB_VIDEO_H_
