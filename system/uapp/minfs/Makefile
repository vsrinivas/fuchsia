# Copyright 2016 The Fuchsia Authors
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

CFLAGS := -Wall -std=c11 -g
CFLAGS += -Werror-implicit-function-declaration
CFLAGS += -Wstrict-prototypes -Wwrite-strings
CFLAGS += -I../../ulib/system/include
CFLAGS += -I../../ulib/magenta/include
CFLAGS += -I../../ulib/mxio/include
CFLAGS += -I../../../global/include

LFLAGS := -Wl,-wrap,open -Wl,-wrap,unlink -Wl,-wrap,stat -Wl,-wrap,mkdir
LFLAGS += -Wl,-wrap,close -Wl,-wrap,read -Wl,-wrap,write -Wl,-wrap,fstat

SRCS += main.c wrap.c test.c
SRCS += bitmap.c bcache.c vfs.c
SRCS += minfs.c minfs-ops.c minfs-check.c

OBJS := $(patsubst %.c,out/%.o,$(SRCS))
DEPS := $(patsubst %.c,out/%.d,$(SRCS))

all:: out/minfs

-include $(DEPS)

out/%.o: %.c
	@echo Build: $@
	@mkdir -p $(dir $@)
	@$(CC) -MMD -MP -c -o $@ $(CFLAGS) $<

out/minfs: $(OBJS)
	@echo Link: $@
	@$(CC) -o $@ $(LFLAGS) $(CFLAGS) $(OBJS)

clean::
	@echo Clean
	@rm -fr out

mkfs: out/minfs
	out/minfs create out/fs 256M

test: out/minfs
	gdb -quiet -ex=r --args out/minfs test out/fs 256M

