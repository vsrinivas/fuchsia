//----------------------------------------------------------------------------
//   The confidential and proprietary information contained in this file may
//   only be used by a person authorised under and to the extent permitted
//   by a subsisting licensing agreement from ARM Limited or its affiliates.
//
//          (C) COPYRIGHT [2018] ARM Limited or its affiliates.
//              ALL RIGHTS RESERVED
//
//   This entire notice must be reproduced on all copies of this file
//   and copies of this file may only be made by a person if such person is
//   permitted to do so under the terms of a subsisting license agreement
//   from ARM Limited or its affiliates.
//----------------------------------------------------------------------------

#include "acamera_firmware_config.h"
#include "acamera_math.h"
#include "acamera_logger.h"
#include "gamma_standard_api.h"
#include "gamma_acamera_core.h"


#ifdef LOG_MODULE
#undef LOG_MODULE
#define LOG_MODULE LOG_MODULE_GAMMA_ACAMERA
#endif

typedef struct _gamma_acamera_core_obj_ {
    uint32_t cumu_hist[ISP_FULL_HISTOGRAM_SIZE];

    uint16_t gain_target;
    uint32_t gain_avg;

    uint32_t gamma_gain;
    uint32_t gamma_offset;
} gamma_acamera_core_obj_t;

static gamma_acamera_core_obj_t gamma_core_objs[FIRMWARE_CONTEXT_NUMBER];


static void gamma_acamera_core_calc( gamma_acamera_core_obj_t *p_gamma_core_obj, gamma_stats_data_t *stats, gamma_acamera_input_t *acamera_input )
{
    uint32_t i;

    //tuning params
    int32_t black_percentage = acamera_input->cali_data.auto_level_ctrl[0];
    int32_t white_percentage = acamera_input->cali_data.auto_level_ctrl[1];
    int32_t auto_black_min = acamera_input->cali_data.auto_level_ctrl[2];
    int32_t auto_black_max = acamera_input->cali_data.auto_level_ctrl[3];
    int32_t auto_white_prc_target = acamera_input->cali_data.auto_level_ctrl[4];
    int32_t avg_coeff = acamera_input->cali_data.auto_level_ctrl[5];
    int32_t enable_auto_level = acamera_input->cali_data.auto_level_ctrl[6];

    uint32_t auto_white_percentage = stats->fullhist_size * white_percentage / 100; // white_percentage% percentile
    uint32_t auto_black_percentage = stats->fullhist_size * black_percentage / 100; // black_percentage% percentile
    uint32_t shift_number = acamera_log2_fixed_to_fixed( stats->fullhist_size, 0, 0 );

    uint32_t auto_level_offset = 0;
    uint32_t auto_level_gain = 0;
    uint32_t apply_gain;

    if ( enable_auto_level ) {
        uint64_t pixel_count = 0;
        uint32_t thr = 0;
        uint32_t auto_white_index = 0;
        uint32_t auto_black_index = 0;
        uint32_t white_gain = 256;

        p_gamma_core_obj->cumu_hist[0] = stats->fullhist[0];
        for ( i = 1; i < stats->fullhist_size; i++ ) {
            p_gamma_core_obj->cumu_hist[i] = p_gamma_core_obj->cumu_hist[i - 1] + stats->fullhist[i];
        }

        pixel_count = p_gamma_core_obj->cumu_hist[stats->fullhist_size - 1];
        thr = ( auto_white_percentage * pixel_count ) >> shift_number;
        i = stats->fullhist_size - 1;
        while ( p_gamma_core_obj->cumu_hist[i] >= thr && i > 1 ) {
            i--;
        }
        auto_white_index = i;
        auto_white_index = ( auto_white_index <= stats->fullhist_size / 2 ) ? stats->fullhist_size / 2 : auto_white_index;
        white_gain = ( ( stats->fullhist_size * auto_white_prc_target / 100 ) << 8 ) / auto_white_index; //U24.8

        int32_t max_gain_clipping;
        // hard coded to 99% of the histogram as we want to avoide clipping
        max_gain_clipping = ( ( stats->fullhist_size * 99 / 100 ) << 8 ) / auto_white_index; //U24.8

        // Calculate black I cut
        thr = ( auto_black_percentage * pixel_count ) >> shift_number;
        i = auto_white_index;
        while ( p_gamma_core_obj->cumu_hist[i] >= thr && i > 1 ) {
            i--;
        }
        auto_black_index = i;

        int32_t contrast = auto_white_index / ( auto_black_index ? auto_black_index : 1 );

        auto_black_index = auto_black_index < auto_black_min ? auto_black_min : auto_black_index;
        auto_black_index = auto_black_index > auto_black_max ? auto_black_max : auto_black_index;
        auto_level_offset = auto_black_index;

        // This will prevent applyign gain in LDR scenes
        uint32_t max_gain_contrast = stats->fullhist_size * 256 / ( ( stats->fullhist_size - auto_level_offset ) ? ( stats->fullhist_size - auto_level_offset ) : 1 );
        int32_t m, cx1 = 30, cx2 = 50, cy1 = 256, cy2 = 0, alpha = 0;
        m = ( ( cy1 - cy2 ) * 256 ) / ( cx1 - cx2 );
        alpha = ( ( m * ( contrast - cx1 ) >> 8 ) + cy1 ); //U32.0
        alpha = alpha < 0 ? 0 : alpha;
        alpha = alpha > 256 ? 256 : alpha;
        max_gain_clipping = ( ( alpha * max_gain_contrast ) + ( ( 256 - alpha ) * max_gain_clipping ) ) >> 8;

        auto_level_gain = stats->fullhist_size * 256 / ( ( stats->fullhist_size - auto_level_offset ) ? ( stats->fullhist_size - auto_level_offset ) : 1 );
        auto_level_gain = auto_level_gain < white_gain ? white_gain : auto_level_gain;               //max(auto_level_gain,white_gain)
        auto_level_gain = auto_level_gain > max_gain_clipping ? max_gain_clipping : auto_level_gain; //min(auto_level_gain,max_gain_clipping)
        auto_level_gain = auto_level_gain > 4095 ? 4095 : auto_level_gain;                           //u4.8 register
        LOG( LOG_DEBUG, " white_gain %d max_gain_clipping %d auto_level_gain %d black %d auto_white_index %d contrast %d al %d m %d %d", white_gain, max_gain_clipping, auto_level_gain, auto_level_offset, auto_white_index, contrast, alpha, m, max_gain_contrast );

        LOG( LOG_INFO, "auto_white_index: %u, auto_black_index: %u, auto_level_offset: %u, auto_level_gain: %u, apply_gain: %u.",
             auto_white_index, auto_black_index, auto_level_offset, auto_level_gain, apply_gain );
    } else {
        auto_level_gain = 256;
        auto_level_offset = 0;
    }

    //time average gain
    apply_gain = p_gamma_core_obj->gain_target = auto_level_gain;
    if ( avg_coeff > 1 ) {
        p_gamma_core_obj->gain_avg += p_gamma_core_obj->gain_target - p_gamma_core_obj->gain_avg / avg_coeff; // division by zero is checked
        apply_gain = ( uint16_t )( p_gamma_core_obj->gain_avg / avg_coeff );                                  // division by zero is checked
    }

    p_gamma_core_obj->gamma_gain = apply_gain;
    p_gamma_core_obj->gamma_offset = auto_level_offset;
}


void *gamma_acamera_core_init( uint32_t ctx_id )
{
    gamma_acamera_core_obj_t *p_gamma_core_obj = NULL;

    if ( ctx_id >= FIRMWARE_CONTEXT_NUMBER ) {
        LOG( LOG_CRIT, "Invalid ctx_id: %d, greater than max: %d.", ctx_id, FIRMWARE_CONTEXT_NUMBER - 1 );
        return NULL;
    }

    p_gamma_core_obj = &gamma_core_objs[ctx_id];
    memset( p_gamma_core_obj, 0, sizeof( *p_gamma_core_obj ) );

    p_gamma_core_obj->gain_target = 256;
    p_gamma_core_obj->gain_avg = 256;
    p_gamma_core_obj->gamma_gain = 256;
    p_gamma_core_obj->gamma_offset = 0;

    return p_gamma_core_obj;
}

int32_t gamma_acamera_core_deinit( void *gamma_ctx )
{
    return 0;
}

int32_t gamma_acamera_core_proc( void *gamma_ctx, gamma_stats_data_t *stats, gamma_input_data_t *input, gamma_output_data_t *output )
{
    if ( !gamma_ctx || !stats || !input || !input->acamera_input || !output || !output->acamera_output ) {
        LOG( LOG_ERR, "Invalid parameter: %p-%p-%p-%p-%p-%p.", gamma_ctx, stats, input, input ? input->acamera_input : NULL, output, output ? output->acamera_output : NULL );
        return -1;
    }

    if ( stats->fullhist_size != ISP_FULL_HISTOGRAM_SIZE ) {
        LOG( LOG_ERR, "Not supported gamma size, current size: %d, max: %d.", stats->fullhist_size, ISP_FULL_HISTOGRAM_SIZE );
        return -2;
    }

    gamma_acamera_core_obj_t *p_gamma_core_obj = (gamma_acamera_core_obj_t *)gamma_ctx;
    gamma_acamera_input_t *acamera_input = input->acamera_input;
    gamma_acamera_output_t *p_gamma_acamera_output = (gamma_acamera_output_t *)output->acamera_output;

    gamma_acamera_core_calc( p_gamma_core_obj, stats, acamera_input );

    p_gamma_acamera_output->gamma_gain = p_gamma_core_obj->gamma_gain;
    p_gamma_acamera_output->gamma_offset = p_gamma_core_obj->gamma_offset;

    return 0;
}
