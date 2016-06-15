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

// setup networking
int netifc_open(void);

// process inbound packet(s)
void netifc_poll(void);

// return nonzero if interface exists
int netifc_active(void);

// shut down networking
void netifc_close(void);

// set a timer to expire after ms milliseconds
void netifc_set_timer(uint32_t ms);

// returns true once the timer has expired
int netifc_timer_expired(void);
