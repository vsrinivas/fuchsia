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

#ifndef __VP9_COEFFICIENT_ADAPTATION_H
#define __VP9_COEFFICIENT_ADAPTATION_H

#ifdef __cplusplus
extern "C" {
#endif

struct adapt_coef_proc_cfg {
  unsigned int *pre_pr_buf;
  unsigned int *pr_buf;
  unsigned int *count_buf;
};

void adapt_coef_process(struct adapt_coef_proc_cfg *cfg, int prev_k, int cur_k, int pre_f);

#ifdef __cplusplus
}
#endif

#endif  // __VP9_COEFFICIENT_ADAPTATION_H
