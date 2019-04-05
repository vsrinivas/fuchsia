/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_DHC_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_DHC_H_
#include "mac.h"
#include "scan.h"

#define DHC_TABLE_MASK_POS (28)

/**
 * enum iwl_dhc_table_id - DHC table operations index
 * @DHC_TABLE_TOOLS: select the tools table
 * @DHC_TABLE_AUTOMATION: select the automation table
 * @DHC_TABLE_INTEGRATION: select the integration table
 * @DHC_TABLE_DEVELOPMENT: select the development table
 * @DHC_TABLE_UT: select the UT table
 * @DHC_TABLE_MAX: maximal id value
 */
enum iwl_dhc_table_id {
    DHC_TABLE_TOOLS = 0,
    DHC_TABLE_AUTOMATION = 1 << DHC_TABLE_MASK_POS,
    DHC_TABLE_INTEGRATION = 2 << DHC_TABLE_MASK_POS,
    DHC_TABLE_DEVELOPMENT = 3 << DHC_TABLE_MASK_POS,
    DHC_TABLE_UT = 4 << DHC_TABLE_MASK_POS,
    DHC_TABLE_MAX = DHC_TABLE_UT,
};

/**
 * enum iwl_dhc_lmac_tools_table - tools operations
 * @DHC_TOOLS_LMAC_TXF_FIFO_DISABLE: disable tx fifo interrupts
 *  1 DW param - bitmask of tx fifos to disable interrupts for
 */
enum iwl_dhc_lmac_tools_table {
    DHC_TOOLS_LMAC_TXF_FIFO_DISABLE = 6,
};

/**
 * enum iwl_dhc_lmac_automation_table - automation operations
 * @DHC_AUTO_LMAC_PHY_GET_STAT: get rx/tx statistics
 * @DHC_AUTO_LMAC_CONFIG_DEBUG_EBS: configure debug data on/off for EBS
 * @DHC_AUTO_LMAC_PHY_ENABLE_CRC_CHECK: enable CRC check
 * @DHC_AUTO_LMAC_SAD_RETURN_PREF_ANTS: get preferred antenna for each
 *  context where SAD is enabled
 * @DHC_AUTO_LMAC_PYFI_TIMING: override PyFi timing
 * @DHC_AUTO_LMAC_REPORT_POWER_STATISTICS: get power statistics report
 * @DHC_MAX_AUTO_LMAC_REQUEST: the size of the Automation table in lmac
 */
enum iwl_dhc_lmac_automation_table {
    DHC_AUTO_LMAC_PHY_GET_STAT = 0,
    DHC_AUTO_LMAC_CONFIG_DEBUG_EBS = 1,
    DHC_AUTO_LMAC_PHY_ENABLE_CRC_CHECK = 2,
    DHC_AUTO_LMAC_SAD_RETURN_PREF_ANTS = 3,
    DHC_AUTO_LMAC_PYFI_TIMING = 4,
    DHC_AUTO_LMAC_REPORT_POWER_STATISTICS = 5,
    DHC_MAX_AUTO_LMAC_REQUEST = 6,
};

/**
 * enum iwl_dhc_umac_automation_table - automation operations
 * @DHC_AUTO_UMAC_SET_PROFILING_REPORT_CONF: configure the profiling
 *  metrics collection
 * @DHC_AUTO_UMAC_REPORT_PROFILING: get profiling report
 * @DHC_AUTO_UMAC_SCAN_CHANNEL_DWELL_ENABLE_REPORT: enable scan channel dwell
 *  reports (received as payload in DHN)
 * @DHC_AUTO_UMAC_ADAPTIVE_DWELL_SCAN_FINE_TUNE_ENABLE_REPORT: enable adaptive
 *  dwell scan fine tune report (received as payload in DHN)
 * @DHC_AUTO_UMAC_CONFIGURE_POWER_FLAGS: configure power flags DW
 * @DHC_AUTO_UMAC_REPORT_POWER_STATISTICS: get power statistics report
 * @DHC_MAX_AUTO_UMAC_REQUEST: the size of the Automation table in umac
 */
enum iwl_dhc_umac_automation_table {
    DHC_AUTO_UMAC_SET_PROFILING_REPORT_CONF = 0,
    DHC_AUTO_UMAC_REPORT_PROFILING = 1,
    DHC_AUTO_UMAC_SCAN_CHANNEL_DWELL_ENABLE_REPORT = 2,
    DHC_AUTO_UMAC_ADAPTIVE_DWELL_SCAN_FINE_TUNE_ENABLE_REPORT = 3,
    DHC_AUTO_UMAC_CONFIGURE_POWER_FLAGS = 4,
    DHC_AUTO_UMAC_REPORT_POWER_STATISTICS = 5,
    DHC_MAX_AUTO_UMAC_REQUEST = 6,
};

/**
 * enum iwl_dhc_umac_integration_table - integration operations
 * @DHC_INTEGRATION_POWER_FLAGS: Power flags
 * @DHC_INTEGRATION_TLC_DEBUG_CONFIG: TLC debug
 * @DHC_INTEGRATION_QUOTA_ENFORCE: Enforce maximum quota
 * @DHC_INT_UMAC_BT_COEX_USER_OVERRIDES: Override BT Coex user
 * @DHC_INT_UMAC_TWT_OPERATION: trigger a TWT operation
 * @DHC_INTEGRATION_MAX: Maximum UMAC integration table entries
 */
enum iwl_dhc_umac_integration_table {
    DHC_INTEGRATION_POWER_FLAGS,
    DHC_INTEGRATION_TLC_DEBUG_CONFIG,
    DHC_INTEGRATION_QUOTA_ENFORCE,
    DHC_INT_UMAC_BT_COEX_USER_OVERRIDES,
    DHC_INT_UMAC_TWT_OPERATION,
    DHC_INTEGRATION_MAX
};

#define DHC_TARGET_UMAC BIT(27)
#define DHC_ADWELL_SCAN_CHANNEL_DWELL_INDEX 2
#define DHC_ADWELL_SCAN_FINE_TUNE_INDEX 3

/**
 * struct iwl_dhc_cmd - debug host command
 * @length: length in DWs of the data structure that is concatenated to the end
 *  of this struct
 * @index_and_mask: bit 31 is 1 for data set operation else it's 0
 *  bits 28-30 is the index of the table of the operation -
 *  &enum iwl_dhc_table_id *
 *  bit 27 is 0 if the cmd targeted to LMAC and 1 if targeted to UMAC,
 *  (LMAC is 0 for backward compatibility)
 *  bit 26 is 0 if the cmd targeted to LMAC0 and 1 if targeted to LMAC1,
 *  relevant only if bit 27 set to 0
 *  bits 0-25 is a specific entry index in the table specified in bits 28-30
 *
 * @data: the concatenated data.
 */
struct iwl_dhc_cmd {
    __le32 length;
    __le32 index_and_mask;
    __le32 data[0];
} __packed; /* DHC_CMD_API_S */

/**
 * struct iwl_dhc_cmd_resp - debug host command response
 * @status: status of the command
 * @data: the response data
 */
struct iwl_dhc_cmd_resp {
    __le32 status;
    __le32 data[0];
} __packed;

/**
 * struct iwl_dhc_profile_cmd - profiling command.
 * should be concatenated to &struct iwl_dhc_cmd
 * @period: the time period, in seconds, of the report.
 *  if it's set to 0 then report will be sent only when requested
 * @reset: set to 1 for resetting the metrics data
 *  set to 0 for not restting.
 * @enabled_metrics: bitmask of the metrics to enable
 *  bit 0 - cpu_usage_metric
 *  bit 1 - paging_metric
 *  bit 2 - flow_timing_metric
 *  bit 3 - critical_section_metric
 *  bit 4 - memory_pool_metric
 *  bit 5 - fifos_metric
 */
struct iwl_dhc_profile_cmd {
    __le32 period;
    __le32 reset;
    __le32 enabled_metrics;
} __packed;

enum iwl_profiling_context_id {
    PROFILING_CONTEXT_PS_THREAD,
    PROFILING_CONTEXT_FMAC_THREAD,
    PROFILING_CONTEXT_MAIN_THREAD,
    PROFILING_CONTEXT_AIRTIME_THREAD,
    PROFILING_CONTEXT_MPAPD_THREAD,
    PROFILING_CONTEXT_TIMER_IRQ,
    PROFILING_CONTEXT_RXF2_IRQ,
    PROFILING_CONTEXT_CMD_IRQ,
    PROFILING_CONTEXT_MAX_NUM
}; /* PROFILING_CONTEXT_ID_API_E */

enum iwl_profiling_tasks_id {
    PROFILING_MAIN_INIT_TASK,
    PROFILING_FMAC_INIT_TASK,
    PROFILING_ELOOP_TASK,
    PROFILING_UMAC_TO_FMAC_EVENT_TASK,
    PROFILING_LMAC_RXF_TASK,
    PROFILING_MPAPD_TASK,
    PROFILING_TASKS_MAX_NUM
}; /* PROFILING_TASKS_ID_API_E */

enum iwl_profiling_flow_id {
    PROFILING_HANDLING_PRB_RQST_UMAC_FLOW,
    PROFILING_UMAC_BCN_HANDLING_FLOW,
    PROFILING_UMAC_NON_TKIP_HANDLING_FLOW,
    PROFILING_UMAC_TKIP_HANDLING_FLOW,
    PROFILING_UMAC_LMAC_NOTIFICATION_THREAD_HANDLING,
    PROFILING_UMAC_RXF2_DROPPABLE_FRAME_ISR_HANDLING,
    PROFILING_UMAC_OTHER_FRAMES_HANDLING_FLOW,
    PROFILING_AIRTIME_CONTEXT_GET_FLOW,
    PROFILING_AIRTIME_CONTEXT_LOSE_FLOW,
    PROFILING_MAC_CONTEXT_LOSE_FLOW,
    PROFILING_AUX_CONTEXT_GET_FLOW,
    PROFILING_AUX_CONTEXT_CLEAR_FLOW,
    PROFILING_AUX_CONTEXT_SET_FLOW,
    PROFILING_AIRTIME_SCHEDULER_SESSION_CALC_FLOW,
    PROFILING_TLC_STATISTICS_HANDLING_FLOW,
    PROFILING_CHANNEL_SWITCH_FLOW,
    PROFILING_THREAD_CONTEXT_SWITCH_FLOW,
    PROFILING_SYSTEM_POWER_DOWN_FLOW,
    PROFILING_FLOW_MAX_NUM
}; /* PROFILING_FLOW_ID_API_E */

enum iwl_profiling_fifo_id {
    PROFILING_FIFO_UMAC_TO_LMAC1,
    PROFILING_FIFO_UMAC_TO_LMAC2,
    PROFILING_FIFO_LMAC1_TO_UMAC,
    PROFILING_FIFO_LMAC2_TO_UMAC,
    PROFILING_FIFO_RXF2,
    PROFILING_FIFO_MAX_NUM
}; /* PROFILING_FIFO_ID_API_E */

enum iwl_profiling_pool_id {
    PROFILING_POOL_MGMT_FRAME,
    PROFILING_POOL_MPDU_FRWK_1,
    PROFILING_POOL_MPDU_FRWK_2,
    PROFILING_POOL_MSG_QUEUE_AIRTIME,
    PROFILING_POOL_MSG_QUEUE_MAIN,
    PROFILING_POOL_MSG_QUEUE_BACKGROUND,
    PROFILING_POOL_MSG_QUEUE_MPAPD,
    PROFILING_POOL_MSG_QUEUE_FMAC,
    PROFILING_POOL_BLOCK_MSG_QUEUE_AIRTIME_BIG,
    PROFILING_POOL_BLOCK_MSG_QUEUE_AIRTIME_SMALL,
    PROFILING_POOL_BLOCK_MSG_QUEUE_MAIN_BIG,
    PROFILING_POOL_BLOCK_MSG_QUEUE_MAIN_SMALL,
    PROFILING_POOL_BLOCK_MSG_QUEUE_FMAC_BIG,
    PROFILING_POOL_BLOCK_MSG_QUEUE_FMAC_SMALL,
    PROFILING_POOL_INTERNAL_TX,
    PROFILING_POOL_CYCLIC_LMAC_RX,
    PROFILING_POOL_CYCLIC_UMAC_2_FMAC,
    PROFILING_POOL_BYTE_UMAC_TX,
    PROFILING_POOL_BYTE_UMAC_OS,
    PROFILING_POOL_MAX_NUM
}; /* PROFILING_POOL_ID_API_E */

/**
 * struct iwl_profiling_configuration - profiling collection configuration
 * @time_since_last_metrics_reset: Time elapsed since last FW metrics reset in
 *  usec
 * @current_system_time: Time at which this report was generated
 * @enabled_metrics: Enabled metrics bitmap
 */
struct iwl_profiling_configuration {
    __le32 time_since_last_metrics_reset;
    __le32 current_system_time;
    __le32 enabled_metrics;
} __packed; /* PROFILING_CONFIGURATION_API_S */

/**
 * struct iwl_profiling_umac_cpu_usage - profiling data on CPU usage
 * @context_id: ID of the execution context for which the following information
 *  is provided
 * @run_time: Total run time (since last metrics reset) for this context in
 *  usec
 * @enabled_metrics: enabled metrics bitmap
 * @max_processing_time: Maximal amount of time (since last metrics reset) that
 *  the context ran to completion
 * @num_of_page_faults_dl: Number of Page Fault downloads only for this context
 *  since last metrics reset. Not relevant for IRQ contexts
 * @num_of_page_faults_dl_up: Number of Page Fault uploads and downloads for
 *  this context since last metrics reset. Not relevant for IRQ contexts
 * @max_processing_time_task: ID of task which caused the longest processing
 *  time for this context
 * @max_block_time: Maximal amount of time (since last metrics reset)
 *  thread was blocked due to higher priority context
 * @max_pf_handle_time_dl: Maximal Time between PF exception to return from
 *  completion handling ISR  Download only
 * @max_pf_handle_time_dl_up: Upload + Download - Including cache write back
 *  and invalidation
 * @min_pf_handle_time_dl: Minimal Time between PF exception to return from
 *  completion handling ISR Download only
 * @min_pf_handle_time_dl_up: Upload + Download - Including cache write back
 *  and invalidation
 * @sum_pf_handle_time_dl: Accumulated Time between PF exception to return from
 *  completion handling ISR
 * @sum_pf_handle_time_dl_up: Upload + Download - Including cache write back
 *  and invalidation
 * @p_fHandle_time_bucket1:  Time for Handling Page Faults Histogram
 *  (any PF - DL or UL+DL) Number of page faults that were handled within
 *  31 microseconds. Buckets are emptied every metrics reset
 * @p_fHandle_time_bucket2:  Time for Handling Page Faults Histogram
 *  (any PF - DL or UL+DL) Number of page faults that were handled within
 *  32-63us. Buckets are emptied every metrics reset
 * @p_fHandle_time_bucket3:  Time for Handling Page Faults Histogram
 *  (any PF - DL or UL+DL) Number of page faults that were handled within
 *  64-127us. Buckets are emptied every metrics reset
 * @p_fHandle_time_bucket4:  Time for Handling Page Faults Histogram
 *  (any PF - DL or UL+DL) Number of page faults that were handled within
 *  128-255us. Buckets are emptied every metrics reset
 * @p_fHandle_time_bucket5:  Time for Handling Page Faults Histogram
 *  (any PF - DL or UL+DL) Number of page faults that were handled within
 *  256+us. Buckets are emptied every metrics reset
 * @stack_size:  Total stack size for this context
 * @stack_max_usage:  Max used stack since last reset (stack is repainted on
 *  each metrics reset)
 * @stack_max_usage_task: Task ID that used the max stack space
 */
struct iwl_profiling_umac_cpu_usage {
    __le32 context_id;
    __le32 run_time;
    __le32 enabled_metrics;
    __le32 max_processing_time;
    __le32 num_of_page_faults_dl;
    __le32 num_of_page_faults_dl_up;
    __le32 max_processing_time_task;
    __le32 max_block_time;
    __le16 max_pf_handle_time_dl;
    __le16 max_pf_handle_time_dl_up;
    __le16 min_pf_handle_time_dl;
    __le16 min_pf_handle_time_dl_up;
    __le32 sum_pf_handle_time_dl;
    __le32 sum_pf_handle_time_dl_up;
    __le16 p_fHandle_time_bucket1;
    __le16 p_fHandle_time_bucket2;
    __le16 p_fHandle_time_bucket3;
    __le16 p_fHandle_time_bucket4;
    __le16 p_fHandle_time_bucket5;
    __le16 stack_size;
    __le16 stack_max_usage;
    __le32 stack_max_usage_task;

} __packed; /* PROFILING_UMAC_CPU_USAGE_API_S */

/**
 * struct iwl_profiling_umac_general_paging
 * @num_of_page_faults:  Total number of PF since last metrics reset
 * @inter_page_fault_time_bucket1:  Time Between Page Faults Histogram Number of
 *  page faults that occurred 0-5 microseconds from the end of the previous
 *  page fault. Buckets are emptied every metrics reset
 * @inter_page_fault_time_bucket2:  Time Between Page Faults Histogram Number of
 *  page faults that occurred 6-10us from the end of the previous page
 *  fault. Buckets are emptied every metrics reset
 * @inter_page_fault_time_bucket3:  Time Between Page Faults Histogram Number of
 *  page faults that occurred 11-1000us from the end of the previous page
 *  fault. Buckets are emptied every metrics reset
 * @inter_page_fault_time_bucket4: Time Between Page Faults Histogram Number of
 *  page faults that occurred 1ms-100ms from the end of the previous page
 *  fault. Buckets are emptied every metrics reset
 * @inter_page_fault_time_bucket5: Time Between Page Faults Histogram Number of
 *  page faults that occurred 100ms+ from the end of the previous page
 *  fault. Buckets are emptied every metrics reset
 * @max_page_fault_wait_time: The maximal number of microseconds that a PF was
 *  waiting in line to be handled (due to other PFs that were handled
 *  previously)
 * @max_num_of_pending_pfs: The maximal number of pending page faults that was
 *  encountered (measured each time a PF is queued to be handled).
 */
struct iwl_profiling_umac_general_paging {
    __le32 num_of_page_faults;
    __le32 inter_page_fault_time_bucket1;
    __le32 inter_page_fault_time_bucket2;
    __le32 inter_page_fault_time_bucket3;
    __le32 inter_page_fault_time_bucket4;
    __le32 inter_page_fault_time_bucket5;
    __le16 max_page_fault_wait_time;
    __le16 max_num_of_pending_pfs;
} __packed; /* PROFILING_UMAC_GENERAL_PAGING_API_S */

/**
 * struct iwl_profiling_umac_flow_timing
 * @flow_id: Identifier of the flow for which the following information is
 *  provided should be in &enum iwl_profiling_flow_id
 * @num_of_runs: Number of times this flow occurred
 * @total_run_time: Total time this flow ran
 * @max_run_time: Maximal time this flow was timed running
 * @min_run_time: Minimal time this flow was timed running
 */
struct iwl_profiling_umac_flow_timing {
    __le32 flow_id;
    __le32 num_of_runs;
    __le32 total_run_time;
    __le32 max_run_time;
    __le32 min_run_time;
} __packed; /* PROFILING_UMAC_FLOW_TIMING_API_S */

/**
 * struct iwl_profiling_umac_critical_section
 * @max_critical_section_time: Maximal length of time of all critical sections
 */
struct iwl_profiling_umac_critical_section {
    __le32 max_critical_section_time;
} __packed; /* PROFILING_UMAC_CRITICAL_SECTION_API_S */

/**
 * struct iwl_profiling_umac_memory_pools {
 * @pool_id: Identifier of the memory pool for which the following information
 *  is provided should be in &enum iwl_profiling_pool_id
 * @min_free_space: Minimum number of blocks or bytes (depending on the pool
 *  type) that were available since last metrics reset
 * @largest_allocated_size: For byte pools, gives an indication of level of
 *  fragmentation
 */
struct iwl_profiling_umac_memory_pools {
    __le32 pool_id;
    __le32 min_free_space;
    __le32 largest_allocated_size;
} __packed; /* PROFILING_UMAC_MEMORY_POOLS_API_S */

/**
 * struct iwl_profiling_umac_fifos
 * @fifo_id: Identifier of the FIFO for which the following information is
 *  provided should be in &enum iwl_profiling_fifo_id
 * @min_free_bytes: Min number of bytes that were available since last metrics
 *  reset
 */
struct iwl_profiling_umac_fifos {
    __le32 fifo_id;
    __le32 min_free_bytes;
} __packed; /* PROFILING_UMAC_FIFOS_API_S */

/**
 * struct iwl_profiling_umac_metrics_report
 * @configuration: configuration of the following
 *  metric report
 * @umac_cpu_usage_ctx: UMAC CPU Usage per context
 * @umac_general_paging: UMAC General Paging  (not context specific)
 * @umac_flows_timing: Flows Timing (provided for each enumerated flow)
 * @umac_critical_section: UMAC critical sections
 * @umac_memory_pools: UMAC Memory Pools (provided for each enumerated
 *  memory pool)
 * @umac_fifos_arr: UMAC FIFOs (provided for each enumerated FIFO)
 */
struct iwl_profiling_umac_metrics_report {
    struct iwl_profiling_configuration configuration;
    struct iwl_profiling_umac_cpu_usage umac_cpu_usage_ctx[PROFILING_CONTEXT_MAX_NUM];
    struct iwl_profiling_umac_general_paging umac_general_paging;
    struct iwl_profiling_umac_flow_timing umac_flows_timing[PROFILING_FLOW_MAX_NUM];
    struct iwl_profiling_umac_critical_section umac_critical_section;
    struct iwl_profiling_umac_memory_pools umac_memory_pools[PROFILING_POOL_MAX_NUM];
    struct iwl_profiling_umac_fifos umac_fifos_arr[PROFILING_FIFO_MAX_NUM];
} __packed; /* PROFILING_UMAC_METRICS_REPORT_API_S */

/**
 * struct iwl_ps_report {
 * @sleep_allowed_count: number of times NIC actually went to PD - accumulator
 * @sleep_time: total sleep time in usec - accumulator
 * @max_sleep_time: max sleep time in usec
 * @missed_beacon_count: number of missed beacons - accumulator
 * @missed_3_consecutive_beacon_count: number of missed-3-consecutive-beacons
 *  events - accumulator
 * @ps_flags: flag bits, divided to 4 bytes - misbehaving AP indication, is
 *  beacon abort mechanism enabled, is LPRX enabled, is uAPSD enabled
 * @max_active_duration: Max time device was active and power save didn't
 *      apply.
 */
struct iwl_ps_report {
    __le32 sleep_allowed_count;
    __le32 sleep_time;
    __le32 max_sleep_time;
    __le32 missed_beacon_count;
    __le32 missed_3_consecutive_beacon_count;
    __le32 ps_flags;
    __le32 max_active_duration;
} __packed; /* PS_REPORT_API_S */

/**
 * struct iwl_dhn_hdr - the header of the Debug Host Notification (DHN)
 * @length: length in DWs of the data structure that is concatenated to the end
 *  of this struct
 * @index_and_mask: bit 31 is 1 for data set operation else it's 0
 *  bits 28-30 is the index of the table of the operation -
 *  &enum iwl_dhc_table_id
 *  bit 27 is 0 if the cmd targeted to LMAC and 1 if targeted to UMAC,
 *  (LMAC is 0 for backward compatibility)
 *  bit 26 is 0 if the cmd targeted to LMAC0 and 1 if targeted to LMAC1,
 *  relevant only if bit 27 set to 0
 *  bits 0-25 is a specific entry index in the table specified in bits 28-30
 */
struct iwl_dhn_hdr {
    __le32 length;
    __le32 index_and_mask;
} __packed; /* DHC_NOTIFICATION_API_S */

/**
 * struct iwl_profiling_notification - the notification of the profiling report
 * @header: DHN header
 * @profiling_metrics: the profiling metrics
 */
struct iwl_profiling_notification {
    struct iwl_dhn_hdr header;
    struct iwl_profiling_umac_metrics_report profiling_metrics;
} __packed; /* DHC_NOTIFICATION_API_S */

/**
 * struct iwl_channel_dwell_report - channel dwell time report
 *
 * This DHN (Debug Host Notification) is raised for each channel during
 * the scan, at the beginning of the dwell time,
 * and includes the following data:
 *
 * @header: DHN header
 * @channel_num: the current channel
 * @dwell_tsf: start dwell tsf
 * @dwell_time: requested dwell time
 */
struct iwl_channel_dwell_report {
    struct iwl_dhn_hdr header;
    __le32 channel_num;
    __le32 dwell_tsf;
    __le32 dwell_time;
} __packed; /* SCAN_CHANNEL_DWELL_REPORT_API_S */

/**
 * struct iwl_adwell_fine_tune_metrics_report
 *
 * This DHN (Debug Host Notification) raised at the end of the scan
 * and contains the following table:
 *
 * @header: DHN header
 * @index: indices array of the channels numbers
 * @scan_counter: fine tune scans number
 * @success_counter: fine tune success counter
 */
struct iwl_adwell_fine_tune_metrics_report {
    struct iwl_dhn_hdr header;
    int8_t index[IWL_SCAN_MAX_NUM_OF_CHANNELS];
    uint8_t scan_counter[IWL_SCAN_MAX_NUM_OF_CHANNELS];
    uint8_t success_counter[IWL_SCAN_MAX_NUM_OF_CHANNELS];
} __packed; /* ADAPTIVE_DWELL_SCAN_FINE_TUNE_METRICS_REPORT_API_S */

/**
 * enum iwl_dhc_quota_enforce_type
 *
 * @QUOTA_ENFORCE_TYPE_RESERVATION: Enforce minimum quota.
 * @QUOTA_ENFORCE_TYPE_LIMITATION: Enforce maximum quota.
 */
enum iwl_dhc_quota_enforce_type {
    QUOTA_ENFORCE_TYPE_RESERVATION,
    QUOTA_ENFORCE_TYPE_LIMITATION,
}; /* DHC_QUOTA_ENFORCE_TYPE_API_E */

/**
 * struct iwl_quota_enforce - Enforce quota percent
 *
 * @macs: bitmask of MAC IDs relevant here
 * @quota_enforce_type: &enum iwl_dhc_quota_enforce_type
 * @reserved: reserved for alignment
 * @quota_percentage: quota to enforce as percentage [0 - 100]
 */
struct iwl_dhc_quota_enforce {
    uint8_t macs;
    uint8_t quota_enforce_type;
    __le16 reserved;
    __le32 quota_percentage[MAC_INDEX_AUX];
} __packed; /* DHC_QUOTA_ENFORCE_API_S */

/**
 * enum iwl_dhc_twt_operation_type - describes the TWT operation type
 *
 * @DHC_TWT_REQUEST: Send a Request TWT command
 * @DHC_TWT_SUGGEST: Send a Suggest TWT command
 * @DHC_TWT_DEMAND: Send a Demand TWT command
 * @DHC_TWT_GROUPING: Send a Grouping TWT command
 * @DHC_TWT_ACCEPT: Send a Accept TWT command
 * @DHC_TWT_ALTERNATE: Send a Alternate TWT command
 * @DHC_TWT_DICTATE: Send a Dictate TWT command
 * @DHC_TWT_REJECT: Send a Reject TWT command
 * @DHC_TWT_TEARDOWN: Send a TearDown TWT command
 */
enum iwl_dhc_twt_operation_type {
    DHC_TWT_REQUEST,
    DHC_TWT_SUGGEST,
    DHC_TWT_DEMAND,
    DHC_TWT_GROUPING,
    DHC_TWT_ACCEPT,
    DHC_TWT_ALTERNATE,
    DHC_TWT_DICTATE,
    DHC_TWT_REJECT,
    DHC_TWT_TEARDOWN,
}; /* DHC_TWT_OPERATION_TYPE_E */

/**
 * struct iwl_dhc_twt_operation - trigger a TWT operation
 *
 * @mac_id: the mac Id on which to trigger TWT operation
 * @twt_operation: see &enum iwl_dhc_twt_operation_type
 * @target_wake_time: when should we be on channel
 * @interval_exp: the exponent for the interval
 * @interval_mantissa: the mantissa for the interval
 * @min_wake_duration: the minimum duration for the wake period
 * @trigger: is the TWT triggered or not
 * @flow_type: is the TWT announced or not
 * @flow_id: the TWT flow identifier from 0 to 7
 * @protection: is the TWT protected
 * @ndo_paging_indicator: is ndo_paging_indicator set
 * @responder_pm_mode: is responder_pm_mode set
 * @negotiation_type: if the responder wants to doze outside the TWT SP
 * @twt_request: 1 for TWT request, 0 otherwise
 * @implicit: is TWT implicit
 * @twt_group_assignment: the TWT group assignment
 * @twt_channel: the TWT channel
 * @reserved: reserved
 */
struct iwl_dhc_twt_operation {
    __le32 mac_id;
    __le32 twt_operation;
    __le64 target_wake_time;
    __le32 interval_exp;
    __le32 interval_mantissa;
    __le32 min_wake_duration;
    uint8_t trigger;
    uint8_t flow_type;
    uint8_t flow_id;
    uint8_t protection;
    uint8_t ndo_paging_indicator;
    uint8_t responder_pm_mode;
    uint8_t negotiation_type;
    uint8_t twt_request;
    uint8_t implicit;
    uint8_t twt_group_assignment;
    uint8_t twt_channel;
    uint8_t reserved;
}; /* DHC_TWT_OPERATION_API_S */

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_DHC_H_
