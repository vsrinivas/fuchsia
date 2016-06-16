// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#define NB_MAGIC	0xAA774217

#define NB_SERVER_PORT	33330
#define NB_ADVERT_PORT	33331

#define NB_COMMAND		1 // arg=0, data=command
#define NB_SEND_FILE		2 // arg=0, data=filename
#define NB_DATA			3 // arg=blocknum, data=data
#define NB_BOOT			4 // arg=0

#define NB_ACK			0

#define NB_ADVERTISE		0x77777777

#define NB_ERROR		0x80000000
#define NB_ERROR_BAD_CMD	0x80000001
#define NB_ERROR_BAD_PARAM	0x80000002
#define NB_ERROR_TOO_LARGE	0x80000003

typedef struct nbmsg_t {
	uint32_t magic;
	uint32_t cookie;
	uint32_t cmd;
	uint32_t arg;
	uint8_t  data[0];
} nbmsg;

int netboot_init(void *buf, size_t len);
int netboot_poll(void);
