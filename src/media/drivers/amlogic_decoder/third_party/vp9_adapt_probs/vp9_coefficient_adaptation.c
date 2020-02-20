// Copyright 2019 Amlogic, Inc.

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdint.h>

#include "vp9_coefficient_adaptation.h"

enum adapt_node_index {
  VP9_PARTITION = 0,
  VP9_PARTITION_P,
  VP9_SKIP,
  VP9_TX_MODE,
  VP9_COEF,
  VP9_INTER_MODE,
  VP9_INTERP,
  VP9_INTRA_INTER,
  VP9_INTERP_INTRA_INTER,
  VP9_COMP_INTER,
  VP9_COMP_REF,
  VP9_SINGLE_REF,
  VP9_REF_MODE,
  VP9_IF_Y_MODE,
  VP9_IF_UV_MODE,
  VP9_MV_JOINTS,
  VP9_MV_SIGN_0,
  VP9_MV_CLASSES_0,
  VP9_MV_CLASS0_0,
  VP9_MV_BITS_0,
  VP9_MV_SIGN_1,
  VP9_MV_CLASSES_1,
  VP9_MV_CLASS0_1,
  VP9_MV_BITS_1,
  VP9_MV_CLASS0_FP_0,
  VP9_MV_CLASS0_FP_1,
  VP9_MV_CLASS0_HP_0,
  VP9_MV_CLASS0_HP_1,
  VP9_ADAPT_NODE_MAX
};

struct adapt_coef_buf_spec_s {
  enum adapt_node_index index;
  enum adapt_node_index off_index;
  unsigned int off_val;
  int start;
};

struct adapt_coef_buf_start_s {
  unsigned int pr_start;
  unsigned int count_start;
} adapt_coef_buf_start[VP9_ADAPT_NODE_MAX];

#define PARTITION_SIZE_STEP  (3 * 4)
#define PARTITION_ONE_SIZE   (4 * PARTITION_SIZE_STEP)
#define COEF_SIZE_ONE_SET    100 /* ((3 +5*6)*3 + 1 padding)*/

#define INTERP_SIZE          8
#define INTRA_INTER_SIZE     4

#define COMP_INTER_SIZE      5
#define COMP_REF_SIZE        5

#define COEF_COUNT_SIZE_ONE_SET    165 /* ((3 +5*6)*5 */
#define COEF_COUNT_SIZE            (4 * 2 * 2 * COEF_COUNT_SIZE_ONE_SET)
#define MV_CLASS0_HP_1_COUNT_SIZE  (2*2)

#define clip_1_255(p) ((p > 255) ? 255 : (p < 1) ? 1 : p)

#define ROUND_POWER_OF_TWO(value, n) (((value) + (1 << ((n) - 1))) >> (n))

#define DC_PRED_VP9    0
#define V_PRED_VP9     1
#define H_PRED_VP9     2
#define D45_PRED_VP9   3
#define D135_PRED_VP9  4
#define D117_PRED_VP9  5
#define D153_PRED_VP9  6
#define D207_PRED_VP9  7
#define D63_PRED_VP9   8
#define TM_PRED_VP9    9

#define MODE_MV_COUNT_SAT_VP9 20

static int adapt_coef_buf_spec_init_flag = 0;

static struct adapt_coef_buf_spec_s adapt_coef_prob_spec[] = {
  { VP9_PARTITION, -1, 0, -1},
  { VP9_PARTITION_P, -1, PARTITION_ONE_SIZE, -1},
  { VP9_SKIP, VP9_PARTITION, 2 * PARTITION_ONE_SIZE, -1},
  { VP9_TX_MODE, -1, 4, -1},
  { VP9_COEF, -1, 12, -1},
  { VP9_INTER_MODE, -1, 4 * 2 * 2 * COEF_SIZE_ONE_SET, -1},
  { VP9_INTERP, -1, 24, -1},
  { VP9_INTRA_INTER, -1, INTERP_SIZE, -1},
  { VP9_INTERP_INTRA_INTER, VP9_INTERP, 0, -1},
  { VP9_COMP_INTER, VP9_INTERP_INTRA_INTER, INTERP_SIZE + INTRA_INTER_SIZE, -1},
  { VP9_COMP_REF, -1, COMP_INTER_SIZE, -1},
  { VP9_SINGLE_REF, -1, COMP_REF_SIZE, -1},
  { VP9_REF_MODE, VP9_COMP_INTER, 0, -1},
  { VP9_IF_Y_MODE, -1, COMP_INTER_SIZE+COMP_REF_SIZE+10, -1},
  { VP9_IF_UV_MODE, -1, 36, -1},
  { VP9_MV_JOINTS, -1, 92, -1},
  { VP9_MV_SIGN_0, -1, 3, -1},
  { VP9_MV_CLASSES_0, -1,1, -1},
  { VP9_MV_CLASS0_0, -1 ,10, -1},
  { VP9_MV_BITS_0, -1, 1, -1},
  { VP9_MV_SIGN_1,-1,10, -1},
  { VP9_MV_CLASSES_1, -1, 1, -1},
  { VP9_MV_CLASS0_1,-1,10, -1},
  { VP9_MV_BITS_1,-1,1, -1},
  { VP9_MV_CLASS0_FP_0,-1,10, -1},
  { VP9_MV_CLASS0_FP_1,-1,9, -1},
  { VP9_MV_CLASS0_HP_0,-1,9, -1}
};

static struct adapt_coef_buf_spec_s adapt_coef_count_spec[] = {
  {VP9_COEF,-1,0, -1},
  {VP9_INTRA_INTER, -1, COEF_COUNT_SIZE, -1},
  {VP9_COMP_INTER, -1, 4*2, -1},
  {VP9_COMP_REF, -1, 5*2, -1},
  {VP9_SINGLE_REF, -1, 5*2, -1},
  {VP9_TX_MODE, -1, 10*2, -1},
  {VP9_SKIP, -1, 12*2, -1},
  {VP9_MV_SIGN_0, -1, 3*2, -1},
  {VP9_MV_SIGN_1, -1, 1*2, -1},
  {VP9_MV_BITS_0, -1, 1*2, -1},
  {VP9_MV_BITS_1, -1, 10*2, -1},
  {VP9_MV_CLASS0_HP_0, -1, 10*2, -1},
  {VP9_MV_CLASS0_HP_1, -1, 2*2, -1},
  {VP9_INTER_MODE, -1, 2*2, -1},
  {VP9_IF_Y_MODE, -1, 7*4, -1},
  {VP9_IF_UV_MODE,-1, 10*4, -1},
  {VP9_PARTITION_P, -1, 10*10, -1},
  {VP9_INTERP,-1,4*4*4, -1},
  {VP9_MV_JOINTS,-1,4*3, -1},
  {VP9_MV_CLASSES_0,-1,1*4, -1},
  {VP9_MV_CLASS0_0,-1,1*11, -1},
  {VP9_MV_CLASSES_1,-1,1*2, -1},
  {VP9_MV_CLASS0_1,-1,1*11, -1},
  {VP9_MV_CLASS0_FP_0,-1,1*2, -1},
  {VP9_MV_CLASS0_FP_1,-1,3*4, -1}
};

static int get_buf_start(struct adapt_coef_buf_spec_s *spec, int i)
{
  if (spec[i].start == -1) {
    if (i==0)
      spec[i].start = 0;
    else {
      int off_index = spec[i].off_index;

      if (off_index == -1)
        off_index = i - 1;
      spec[i].start = get_buf_start(spec, off_index) +
        spec[i].off_val;
    }
  }
  return spec[i].start;
}

static int get_coef_prob_start(enum adapt_node_index index)
{
  int size = sizeof(adapt_coef_prob_spec)
    /sizeof(adapt_coef_prob_spec[0]);
  int start = -1;
  int i;

  for (i = 0; i < size; i++) {
    if (adapt_coef_prob_spec[i].index == index)
      break;
  }
  if (i < size)
    start = get_buf_start(&adapt_coef_prob_spec[0], i);

  return start;
}

static int get_coef_count_start(enum adapt_node_index index)
{
  int size = sizeof(adapt_coef_count_spec)/sizeof(adapt_coef_count_spec[0]);
  int start = -1;
  int i;

  for (i = 0; i < size; i++) {
    if (adapt_coef_count_spec[i].index == index)
      break;
  }

  if (i < size)
    start = get_buf_start(&adapt_coef_count_spec[0], i);

  return start;
}

static void init_adapt_coef_buf_spec(void)
{
  int i;

  for (i = 0; i < VP9_ADAPT_NODE_MAX; i++) {
    adapt_coef_buf_start[i].pr_start = get_coef_prob_start(i);
    adapt_coef_buf_start[i].count_start = get_coef_count_start(i);
  }
}

static const int to_update_factor[MODE_MV_COUNT_SAT_VP9 + 1] = {
  0, 6, 12, 19, 25, 32, 38, 44, 51, 57, 64,
  70, 76, 83, 89, 96, 102, 108, 115, 121, 128
};

static void merge_probs(struct adapt_coef_proc_cfg *cfg,
  int coef_node_st, int tr_left, int tr_right) {

  int p_shift, pre_pr, new_pr, den, m_cnt, get_pr, factor;

  p_shift = (coef_node_st & 3) * 8;
  pre_pr = (cfg->pre_pr_buf[coef_node_st / 4 * 2] >> p_shift) & 0xff;

  den = tr_left + tr_right;

  if (den == 0)
    new_pr = pre_pr;
  else {
    m_cnt = (den < MODE_MV_COUNT_SAT_VP9) ?
      den : MODE_MV_COUNT_SAT_VP9;
    get_pr = clip_1_255(((int64_t)tr_left * 256 + (den >> 1)) / den);
    factor = to_update_factor[m_cnt];
    new_pr = ROUND_POWER_OF_TWO(pre_pr * (256 - factor)
        + get_pr * factor, 8);
  }
  cfg->pr_buf[coef_node_st / 4 * 2] = (cfg->pr_buf[coef_node_st / 4 * 2]
      & (~(0xff << p_shift))) | (new_pr << p_shift);

}

void adapt_coef_process(struct adapt_coef_proc_cfg *cfg,
  int prev_k, int cur_k, int pre_f)
{
  int txsize, coef_txsize_start, coef_count_txsize_start, type, coef_type_st, coef_count_type_st;
  int plane, coef_plane_st, coef_count_plane_st, band, coef_band_start, coef_count_band_st;
  int cxt_n,node, coef_node_st,cxt,coef_cxt_st;
  int tr_i, tr_left, tr_right, mvdi;
  int count_sat = 24;
  int update_factor =   cur_k ? 112 :
      prev_k ? 128 : 112;
  int pr_32, pr_res, p_shift, pre_pr, num, den;
  int get_pr, m_cnt, factor, new_pr;
  unsigned int *c;

  if (!adapt_coef_buf_spec_init_flag) {
    init_adapt_coef_buf_spec();
    adapt_coef_buf_spec_init_flag = 1;
  }

  for (txsize = 0; txsize < 4; txsize++) {
    coef_txsize_start = adapt_coef_buf_start[VP9_COEF].pr_start
      + txsize * 4 * COEF_SIZE_ONE_SET;
    coef_count_txsize_start = adapt_coef_buf_start[VP9_COEF].count_start
      + txsize * 4 * COEF_COUNT_SIZE_ONE_SET;
    coef_plane_st = coef_txsize_start;
    coef_count_plane_st = coef_count_txsize_start;

    for (plane = 0; plane < 2; plane++) {
      coef_type_st = coef_plane_st;
      coef_count_type_st = coef_count_plane_st;

      for (type = 0; type < 2; type++) {
        coef_band_start = coef_type_st;
        coef_count_band_st = coef_count_type_st;

        for (band = 0; band < 6; band++) {
          if (band == 0)
            cxt_n = 3;
          else
            cxt_n = 6;

          coef_cxt_st = coef_band_start;
          c = cfg->count_buf + coef_count_band_st;

          for (cxt = 0; cxt < cxt_n; cxt++) {
            const int n0 = *c;
            const int n1 = *(c + 1);
            const int n2 = *(c + 2);
            const int neob = *(c + 3);
            const int nneob = *(c + 4);
            const unsigned int branch_ct[3][2] = {
              { neob, nneob },
              { n0, n1 + n2 },
              { n1, n2 }};

            coef_node_st = coef_cxt_st;

            for (node = 0; node < 3; node++) {
              pr_32 = cfg->pre_pr_buf[coef_node_st  / 4 * 2];
              pr_res = coef_node_st & 3;
              p_shift = pr_res * 8;
              pre_pr = (pr_32 >> p_shift) & 0xff;

              num = branch_ct[node][0];
              den = branch_ct[node][0] + branch_ct[node][1];
              m_cnt = (den < count_sat) ? den : count_sat;
              get_pr = (den == 0) ? 128u : clip_1_255((((int64_t) num * 256  + (den >> 1)) / den));
              factor = update_factor * m_cnt / count_sat;
              new_pr = ROUND_POWER_OF_TWO(pre_pr * (256 - factor) + get_pr * factor, 8);
              cfg->pr_buf[coef_node_st/ 4 * 2] =
                (cfg->pr_buf[coef_node_st / 4 * 2] & (~(0xff << p_shift))) | (new_pr << p_shift);
              coef_node_st += 1;
            }

            coef_cxt_st = coef_cxt_st + 3;
            c += 5;
          }
          if (band == 0) {
            coef_band_start += 10;
            coef_count_band_st += 15;
          } else {
            coef_band_start += 18;
            coef_count_band_st += 30;
          }
        }

        coef_type_st += COEF_SIZE_ONE_SET;
        coef_count_type_st += COEF_COUNT_SIZE_ONE_SET;
      }

      coef_plane_st += 2 * COEF_SIZE_ONE_SET;
      coef_count_plane_st += 2 * COEF_COUNT_SIZE_ONE_SET;
    }
  }

  if (cur_k == 0) {
    int i;

    for (i = 1; adapt_coef_count_spec[i].index != VP9_MV_CLASS0_HP_1; i++) {
      unsigned int * n_c;
      int index = adapt_coef_count_spec[i].index;
      coef_node_st = adapt_coef_buf_start[index].pr_start;

      c = cfg->count_buf + adapt_coef_buf_start[index].count_start;
      if (index!=VP9_MV_CLASS0_HP_0)
        n_c = cfg->count_buf + adapt_coef_count_spec[i+1].start;
      else
        n_c = cfg->count_buf + adapt_coef_buf_start[VP9_MV_CLASS0_HP_1].count_start
            + MV_CLASS0_HP_1_COUNT_SIZE;

      for (; c < n_c; c+=2) {
        den = *c + *(c+1);

        pr_32 = cfg->pre_pr_buf[coef_node_st / 4 * 2];
        pr_res = coef_node_st & 3;
        p_shift = pr_res * 8;
        pre_pr = (pr_32 >> p_shift) & 0xff;

        if (den == 0)
          new_pr = pre_pr;
        else {
          m_cnt = (den < MODE_MV_COUNT_SAT_VP9) ?
            den : MODE_MV_COUNT_SAT_VP9;
          get_pr = clip_1_255(((int64_t)(*c) * 256 + (den >> 1)) / den);
          factor = to_update_factor[m_cnt];
          new_pr = ROUND_POWER_OF_TWO(pre_pr * (256 - factor) + get_pr * factor, 8);
        }

        cfg->pr_buf[coef_node_st / 4 * 2] = (cfg->pr_buf[coef_node_st / 4 * 2] &
                                        (~(0xff << p_shift))) | (new_pr << p_shift);
        coef_node_st = coef_node_st + 1;
      }
    }

    coef_node_st = adapt_coef_buf_start[VP9_INTER_MODE].pr_start;
    c = cfg->count_buf + adapt_coef_buf_start[VP9_INTER_MODE].count_start;

    for (tr_i = 0; tr_i < 7; tr_i++) {
      for (node = 0; node < 3; node++) {
        switch (node) {
        case 2:
          tr_left = *(c + 1);
          tr_right = *(c + 3);
          break;
        case 1:
          tr_left = *(c + 0);
          tr_right = *(c + 1) + *(c + 3);
          break;
        default:
          tr_left = *(c + 2);
          tr_right = *(c + 0) +  *(c + 1) +  *(c + 3);
          break;
        }

        merge_probs(cfg, coef_node_st, tr_left, tr_right);

        coef_node_st = coef_node_st + 1;
      }

      c += 4;
    }

    coef_node_st = adapt_coef_buf_start[VP9_IF_Y_MODE].pr_start;
    c = cfg->count_buf + adapt_coef_buf_start[VP9_IF_Y_MODE].count_start;

    for (tr_i = 0; tr_i < 14; tr_i++) {
      for (node = 0; node < 9; node++) {
        switch (node) {
        case 8:
          tr_left = *(c+D153_PRED_VP9);
          tr_right = *(c+D207_PRED_VP9);
          break;
        case 7:
          tr_left = *(c+D63_PRED_VP9);
          tr_right = *(c+D207_PRED_VP9) + *(c+D153_PRED_VP9);
          break;
        case 6:
          tr_left = *(c + D45_PRED_VP9);
          tr_right = *(c+D207_PRED_VP9) + *(c+D153_PRED_VP9) +  *(c+D63_PRED_VP9);
          break;
        case 5:
          tr_left = *(c+D135_PRED_VP9);
          tr_right = *(c+D117_PRED_VP9);
          break;
        case 4:
          tr_left = *(c+H_PRED_VP9);
          tr_right = *(c+D117_PRED_VP9) + *(c+D135_PRED_VP9);
          break;
        case 3:
          tr_left = *(c+H_PRED_VP9) + *(c+D117_PRED_VP9) + *(c+D135_PRED_VP9);
          tr_right = *(c+D45_PRED_VP9) + *(c+D207_PRED_VP9) + *(c+D153_PRED_VP9) + *(c+D63_PRED_VP9);
          break;
        case 2:
          tr_left = *(c+V_PRED_VP9);
          tr_right = *(c+H_PRED_VP9) + *(c+D117_PRED_VP9) + *(c+D135_PRED_VP9) +
            *(c+D45_PRED_VP9) + *(c+D207_PRED_VP9) + *(c+D153_PRED_VP9) + *(c+D63_PRED_VP9);
          break;
        case 1:
          tr_left = *(c+TM_PRED_VP9);
          tr_right = *(c+V_PRED_VP9) + *(c+H_PRED_VP9) + *(c+D117_PRED_VP9) + *(c+D135_PRED_VP9) +
            *(c+D45_PRED_VP9) + *(c+D207_PRED_VP9) + *(c+D153_PRED_VP9) + *(c+D63_PRED_VP9);
          break;
        default:
          tr_left = *(c+DC_PRED_VP9);
          tr_right = *(c+TM_PRED_VP9) + *(c+V_PRED_VP9) + *(c+H_PRED_VP9) + *(c+D117_PRED_VP9) + *(c+D135_PRED_VP9) +
            *(c+D45_PRED_VP9) + *(c+D207_PRED_VP9) + *(c+D153_PRED_VP9) + *(c+D63_PRED_VP9);
          break;
        }

        merge_probs(cfg, coef_node_st, tr_left, tr_right);

        coef_node_st = coef_node_st + 1;
      }
      c += 10;
    }

    coef_node_st = adapt_coef_buf_start[VP9_PARTITION_P].pr_start;
    c = cfg->count_buf + adapt_coef_buf_start[VP9_PARTITION_P].count_start;

    for (tr_i = 0; tr_i < 16; tr_i++) {
      for (node = 0; node < 3; node++) {
        switch (node) {
        case 2:
          tr_left = *(c + 2);
          tr_right = *(c + 3);
          break;
        case 1:
          tr_left = *(c + 1);
          tr_right = *(c + 2) + *(c + 3);
          break;
        default:
          tr_left = *(c + 0);
          tr_right = *(c + 1) + *(c + 2) + *(c + 3);
          break;
        }

        merge_probs(cfg, coef_node_st,tr_left, tr_right);

        coef_node_st = coef_node_st + 1;
      }

      c += 4;
    }

    coef_node_st = adapt_coef_buf_start[VP9_INTERP].pr_start;
    c = cfg->count_buf + adapt_coef_buf_start[VP9_INTERP].count_start;

    for (tr_i = 0; tr_i < 4; tr_i++) {
      for (node = 0; node < 2; node++) {
        switch (node) {
        case 1:
          tr_left = *(c + 1);
          tr_right = *(c + 2);
          break;
        default:
          tr_left = *(c + 0);
          tr_right = *(c + 1) + *(c + 2);
          break;
        }

        merge_probs(cfg, coef_node_st,tr_left, tr_right);

        coef_node_st = coef_node_st + 1;
      }

      c += 3;
    }

    coef_node_st = adapt_coef_buf_start[VP9_MV_JOINTS].pr_start;
    c = cfg->count_buf + adapt_coef_buf_start[VP9_MV_JOINTS].count_start;

    for (tr_i = 0; tr_i < 1; tr_i++) {
      for (node = 0; node < 3; node++) {
        switch (node) {
        case 2:
          tr_left = *(c + 2);
          tr_right = *(c + 3);
          break;
        case 1:
          tr_left = *(c + 1);
          tr_right = *(c + 2) + *(c + 3);
          break;
        default:
          tr_left = *(c + 0);
          tr_right = *(c + 1) + *(c + 2) + *(c + 3);
          break;
        }

        merge_probs(cfg, coef_node_st,tr_left, tr_right);

        coef_node_st = coef_node_st + 1;
      }

      c += 4;
    }

    for (mvdi = 0; mvdi < 2; mvdi++) {
      if (mvdi) {
        coef_node_st = adapt_coef_buf_start[VP9_MV_CLASSES_1].pr_start;
        c = cfg->count_buf + adapt_coef_buf_start[VP9_MV_CLASSES_1].count_start;
      } else {
        coef_node_st = adapt_coef_buf_start[VP9_MV_CLASSES_0].pr_start;
        c = cfg->count_buf + adapt_coef_buf_start[VP9_MV_CLASSES_0].count_start;
      }

      tr_i = 0;

      for (node = 0; node < 10; node++) {
        switch (node) {
        case 9:
          tr_left = *(c + 9);
          tr_right = *(c + 10);
          break;
        case 8:
          tr_left = *(c + 7);
          tr_right = *(c + 8);
          break;
        case 7:
          tr_left = *(c + 7) + *(c + 8);
          tr_right = *(c + 9) + *(c + 10);
          break;
        case 6:
          tr_left = *(c + 6);
          tr_right = *(c + 7) + *(c + 8) + *(c + 9) + *(c + 10);
          break;
        case 5:
          tr_left = *(c + 4);
          tr_right = *(c + 5);
          break;
        case 4:
          tr_left = *(c + 4) + *(c + 5);
          tr_right = *(c + 6) + *(c + 7) + *(c + 8) + *(c + 9) + *(c + 10);
          break;
        case 3:
          tr_left = *(c + 2);
          tr_right = *(c + 3);
          break;
        case 2:
          tr_left = *(c + 2) + *(c + 3);
          tr_right = *(c + 4) + *(c + 5) + *(c + 6) + *(c + 7) + *(c + 8) +
            *(c + 9) + *(c + 10);
          break;
        case 1:
          tr_left = *(c + 1);
          tr_right = *(c + 2) + *(c + 3) + *(c + 4) + *(c + 5) + *(c + 6) +
            *(c + 7) + *(c + 8) + *(c + 9) + *(c + 10);
          break;
        default:
          tr_left = *(c + 0);
          tr_right = *(c + 1) + *(c + 2) + *(c + 3) + *(c + 4) + *(c + 5) +
            *(c + 6) + *(c + 7) + *(c + 8) + *(c + 9) + *(c + 10);
          break;
        }

        merge_probs(cfg, coef_node_st, tr_left, tr_right);

        coef_node_st = coef_node_st + 1;
      }

      if (mvdi) {
        coef_node_st = adapt_coef_buf_start[VP9_MV_CLASS0_1].pr_start;
        c = cfg->count_buf + adapt_coef_buf_start[VP9_MV_CLASS0_1].count_start;
      } else {
        coef_node_st = adapt_coef_buf_start[VP9_MV_CLASS0_0].pr_start;
        c = cfg->count_buf + adapt_coef_buf_start[VP9_MV_CLASS0_0].count_start;
      }

      tr_i = 0;
      node = 0;
      tr_left = *c;
      tr_right = *(c+1);

      merge_probs(cfg, coef_node_st, tr_left, tr_right);

      if (mvdi) {
        coef_node_st = adapt_coef_buf_start[VP9_MV_CLASS0_FP_1].pr_start;
        c = cfg->count_buf + adapt_coef_buf_start[VP9_MV_CLASS0_FP_1].count_start;
      } else {
        coef_node_st = adapt_coef_buf_start[VP9_MV_CLASS0_FP_0].pr_start;
        c = cfg->count_buf + adapt_coef_buf_start[VP9_MV_CLASS0_FP_0].count_start;
      }

      for (tr_i = 0; tr_i < 3; tr_i++) {
        for (node = 0; node < 3; node++) {
          switch (node) {
          case 2:
            tr_left = *(c + 2);
            tr_right = *(c + 3);
            break;
          case 1:
            tr_left = *(c + 1);
            tr_right = *(c + 2) + *(c + 3);
            break;
          default:
            tr_left = *(c + 0);
            tr_right = *(c + 1) + *(c + 2) + *(c + 3);
            break;
          }

          merge_probs(cfg, coef_node_st, tr_left, tr_right);

          coef_node_st = coef_node_st + 1;
        }
        c += 4;
      }
    }
  }
}
