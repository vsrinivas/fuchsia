# Copyright 2017 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_SRCS += \
    $(GET_LOCAL_DIR)/src/arena.c \
    $(GET_LOCAL_DIR)/src/base.c \
    $(GET_LOCAL_DIR)/src/bitmap.c \
    $(GET_LOCAL_DIR)/src/ckh.c \
    $(GET_LOCAL_DIR)/src/ctl.c \
    $(GET_LOCAL_DIR)/src/extent.c \
    $(GET_LOCAL_DIR)/src/extent_mmap.c \
    $(GET_LOCAL_DIR)/src/extent_dss.c \
    $(GET_LOCAL_DIR)/src/jemalloc.c \
    $(GET_LOCAL_DIR)/src/large.c \
    $(GET_LOCAL_DIR)/src/mutex.c \
    $(GET_LOCAL_DIR)/src/nstime.c \
    $(GET_LOCAL_DIR)/src/pages.c \
    $(GET_LOCAL_DIR)/src/prof.c \
    $(GET_LOCAL_DIR)/src/rtree.c \
    $(GET_LOCAL_DIR)/src/stats.c \
    $(GET_LOCAL_DIR)/src/tcache.c \
    $(GET_LOCAL_DIR)/src/tsd.c \
    $(GET_LOCAL_DIR)/src/util.c \
    $(GET_LOCAL_DIR)/src/witness.c \

LOCAL_CFLAGS += -I$(GET_LOCAL_DIR)/include
