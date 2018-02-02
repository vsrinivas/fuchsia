/*
 * Copyright (c) 2012 Neratec Solutions AG
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef DFS_PATTERN_DETECTOR_H
#define DFS_PATTERN_DETECTOR_H

#include <stdint.h>

#include <linux/list.h>
#include <linux/nl80211.h>

/* tolerated deviation of radar time stamp in usecs on both sides
 * TODO: this might need to be HW-dependent
 */
#define PRI_TOLERANCE   16

/**
 * struct ath_dfs_pool_stats - DFS Statistics for global pools
 */
struct ath_dfs_pool_stats {
    uint32_t pool_reference;
    uint32_t pulse_allocated;
    uint32_t pulse_alloc_error;
    uint32_t pulse_used;
    uint32_t pseq_allocated;
    uint32_t pseq_alloc_error;
    uint32_t pseq_used;
};

/**
 * struct pulse_event - describing pulses reported by PHY
 * @ts: pulse time stamp in us
 * @freq: channel frequency in MHz
 * @width: pulse duration in us
 * @rssi: rssi of radar event
 * @chirp: chirp detected in pulse
 */
struct pulse_event {
    uint64_t ts;
    uint16_t freq;
    uint8_t width;
    uint8_t rssi;
    bool chirp;
};

/**
 * struct radar_detector_specs - detector specs for a radar pattern type
 * @type_id: pattern type, as defined by regulatory
 * @width_min: minimum radar pulse width in [us]
 * @width_max: maximum radar pulse width in [us]
 * @pri_min: minimum pulse repetition interval in [us] (including tolerance)
 * @pri_max: minimum pri in [us] (including tolerance)
 * @num_pri: maximum number of different pri for this type
 * @ppb: pulses per bursts for this type
 * @ppb_thresh: number of pulses required to trigger detection
 * @max_pri_tolerance: pulse time stamp tolerance on both sides [us]
 * @chirp: chirp required for the radar pattern
 */
struct radar_detector_specs {
    uint8_t type_id;
    uint8_t width_min;
    uint8_t width_max;
    uint16_t pri_min;
    uint16_t pri_max;
    uint8_t num_pri;
    uint8_t ppb;
    uint8_t ppb_thresh;
    uint8_t max_pri_tolerance;
    bool chirp;
};

/**
 * struct dfs_pattern_detector - DFS pattern detector
 * @exit(): destructor
 * @set_dfs_domain(): set DFS domain, resets detector lines upon domain changes
 * @add_pulse(): add radar pulse to detector, returns true on detection
 * @region: active DFS region, NL80211_DFS_UNSET until set
 * @num_radar_types: number of different radar types
 * @last_pulse_ts: time stamp of last valid pulse in usecs
 * @radar_detector_specs: array of radar detection specs
 * @channel_detectors: list connecting channel_detector elements
 */
struct dfs_pattern_detector {
    void (*exit)(struct dfs_pattern_detector* dpd);
    bool (*set_dfs_domain)(struct dfs_pattern_detector* dpd,
                           enum nl80211_dfs_regions region);
    bool (*add_pulse)(struct dfs_pattern_detector* dpd,
                      struct pulse_event* pe);

    struct ath_dfs_pool_stats (*get_stats)(struct dfs_pattern_detector* dpd);
    enum nl80211_dfs_regions region;
    uint8_t num_radar_types;
    uint64_t last_pulse_ts;
    /* needed for ath_dbg() */
    struct ath_common* common;

    const struct radar_detector_specs* radar_spec;
    struct list_head channel_detectors;
};

/**
 * dfs_pattern_detector_init() - constructor for pattern detector class
 * @param region: DFS domain to be used, can be NL80211_DFS_UNSET at creation
 * @return instance pointer on success, NULL otherwise
 */
extern struct dfs_pattern_detector*
dfs_pattern_detector_init(struct ath_common* common,
                          enum nl80211_dfs_regions region);
#endif /* DFS_PATTERN_DETECTOR_H */
