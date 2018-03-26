// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <driver/usb.h>
#include <stdlib.h>
#include <stdio.h>

#include "usb-audio.h"

uint32_t* usb_audio_parse_sample_rates(usb_audio_ac_format_type_i_desc* format_desc,
                                       int* out_count) {
    *out_count = 0;

    // sanity check the descriptor
    int count = format_desc->bSamFreqType;
    if (count == 0 || format_desc->bLength < sizeof(*format_desc) +
                                             (sizeof(format_desc->tSamFreq[0]) * count)) {
        printf("malformed format_desc in usb_audio_parse_sample_rates\n");
        return NULL;
    }
    uint32_t* result = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (!result) return NULL;

    usb_audio_ac_samp_freq* ptr = format_desc->tSamFreq;
    for (int i = 0; i < count; i++) {
        uint32_t freq = ptr->freq[0] | (ptr->freq[1] << 8) | (ptr->freq[2] << 16);
        result[i] = freq;
        ptr++;
    }
    *out_count = count;
    return result;
}

zx_status_t usb_audio_set_sample_rate(usb_protocol_t* usb, uint8_t ep_addr, uint32_t sample_rate) {
    uint8_t buffer[3];
    buffer[0] = sample_rate;
    buffer[1] = sample_rate >> 8;
    buffer[2] = sample_rate >> 16;
    zx_status_t result = usb_control(usb, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_ENDPOINT,
                                     USB_AUDIO_SET_CUR,
                                     USB_AUDIO_SAMPLING_FREQ_CONTROL << 8,
                                     ep_addr, &buffer, sizeof(buffer), ZX_TIME_INFINITE, NULL);
    if (result == ZX_ERR_IO_REFUSED || result == ZX_ERR_IO_INVALID) {
        // clear the stall/error
        usb_reset_endpoint(usb, 0);
    }
    return result;
}

zx_status_t get_feature_unit_ch_count(const usb_audio_ac_feature_unit_desc* fu_desc,
                                      uint8_t* ch_count_out) {
    if (!fu_desc->bControlSize) {
        printf("[USB_AUD] Invalid ControlSize (%u) while computing feature unit channel count\n",
               fu_desc->bControlSize);
        return ZX_ERR_INVALID_ARGS;
    }

    // In addition to the fields listed in the feature_unit_desc struct, there
    // is and additional single byte field (iFeature) which comes after the
    // variable length control bitmaps field.  Account for this when we sanity
    // check our length.
    const uint8_t overhead = sizeof(*fu_desc) + 1;

    if (((fu_desc->bLength < (overhead + fu_desc->bControlSize)) ||
        ((fu_desc->bLength - overhead) % fu_desc->bControlSize))) {
        printf("[USB_AUD] Invalid Length (%u) while computing feature unit channel count\n",
               fu_desc->bLength);
        return ZX_ERR_INVALID_ARGS;
    }

    *ch_count_out = (fu_desc->bLength - overhead) / fu_desc->bControlSize;
    return ZX_OK;
}

void usb_audio_dump_feature_unit_caps(usb_protocol_t* usb,
                                      uint8_t interface_number,
                                      const usb_audio_ac_feature_unit_desc* fu_desc) {
    printf("Feature unit dump for interface number %u\n", interface_number);
    printf("Length    : 0x%02x (%u)\n", fu_desc->bLength, fu_desc->bLength);
    printf("DType     : 0x%02x (%u)\n", fu_desc->bDescriptorType, fu_desc->bDescriptorType);
    printf("DSubtype  : 0x%02x (%u)\n", fu_desc->bDescriptorSubtype, fu_desc->bDescriptorSubtype);
    printf("UnitID    : 0x%02x (%u)\n", fu_desc->bUnitID, fu_desc->bUnitID);
    printf("SrcID     : 0x%02x (%u)\n", fu_desc->bSourceID, fu_desc->bSourceID);
    printf("CtrlSz    : 0x%02x (%u)\n", fu_desc->bControlSize, fu_desc->bControlSize);

    uint8_t ch_count;
    if (get_feature_unit_ch_count(fu_desc, &ch_count) != ZX_OK) {
        return;
    }

    const uint8_t* bma = fu_desc->bmaControls;
    for (uint8_t i = 0; i < ch_count; ++i) {
        printf("CBma[%3u] : 0x", i);
        for (uint8_t j = 0; j < fu_desc->bControlSize; ++j) {
            printf("%02x", bma[fu_desc->bControlSize - j - 1]);
        }
        printf("\n");
        bma += fu_desc->bControlSize;
    }
}

// volume is in 0 - 100 range
zx_status_t usb_audio_set_volume(usb_protocol_t* usb,
                                 uint8_t interface_number,
                                 const usb_audio_ac_feature_unit_desc* fu_desc,
                                 int volume) {
    zx_status_t status;
    size_t out_length;
    uint8_t ch_count;

    if ((volume < 0) || (volume > 100)) {
        printf("[USB_AUD] Bad volume (%d)\n", volume);
        status = ZX_ERR_INVALID_ARGS;
        goto out;
    }

    status = get_feature_unit_ch_count(fu_desc, &ch_count);
    if (status != ZX_OK) {
        printf("[USB_AUD] Failed to parse feature unit descriptor\n");
        goto out;
    }

    const uint16_t unit_addr = (((uint16_t)fu_desc->bUnitID) << 8) | interface_number;
    for (uint8_t ch = 0; ch < ch_count; ++ch) {
        uint8_t caps_bma = fu_desc->bmaControls[ch * fu_desc->bControlSize];

        if (caps_bma & USB_AUDIO_FU_BMA_MUTE) {
            uint8_t val = (volume == 0) ? 1 : 0;
            const uint16_t ctrl_addr = (USB_AUDIO_MUTE_CONTROL << 8) | ch;
            status = usb_control(usb, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                                 USB_AUDIO_SET_CUR, ctrl_addr, unit_addr,
                                 &val, sizeof(val), ZX_TIME_INFINITE,
                                 &out_length);
            if ((status != ZX_OK) || (out_length != sizeof(val))) {
                printf("[USB_AUD] Failed to set mute; IID %u FeatUnitID %u Ch %u\n",
                        interface_number, fu_desc->bUnitID, ch);
                goto out;
            }
        }

        if (caps_bma & USB_AUDIO_FU_BMA_VOLUME) {
            int16_t min, max, val;
            const uint16_t ctrl_addr = (USB_AUDIO_VOLUME_CONTROL << 8) | ch;

            status = usb_control(usb, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                                 USB_AUDIO_GET_MIN, ctrl_addr, unit_addr,
                                 &min, sizeof(min), ZX_TIME_INFINITE,
                                 &out_length);

            if ((status != ZX_OK) || (out_length != sizeof(val))) {
                printf("[USB_AUD] Failed to to fetch min vol; IID %u FeatUnitID %u Ch %u\n",
                        interface_number, fu_desc->bUnitID, ch);
                goto out;
            }

            status = usb_control(usb, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                                 USB_AUDIO_GET_MAX, ctrl_addr, unit_addr,
                                 &max, sizeof(max), ZX_TIME_INFINITE,
                                 &out_length);

            if ((status != ZX_OK) || (out_length != sizeof(val))) {
                printf("[USB_AUD] Failed to to fetch max vol; IID %u FeatUnitID %u Ch %u\n",
                        interface_number, fu_desc->bUnitID, ch);
                goto out;
            }

            val = (int16_t)(((((int32_t)(max - min)) * volume) / 100) + min);

            status = usb_control(usb, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                                 USB_AUDIO_SET_CUR, ctrl_addr, unit_addr,
                                 &val, sizeof(val), ZX_TIME_INFINITE,
                                 &out_length);
            if ((status != ZX_OK) || (out_length != sizeof(val))) {
                printf("[USB_AUD] Failed to to set vol; IID %u FeatUnitID %u Ch %u\n",
                        interface_number, fu_desc->bUnitID, ch);
                goto out;
            }
        }
    }

out:
    if (status == ZX_ERR_IO_REFUSED || status == ZX_ERR_IO_INVALID) {
        // clear the stall/error
        usb_reset_endpoint(usb, 0);
    }
    return status;
}
