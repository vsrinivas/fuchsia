// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Copyright 2016 Joe Wilm, The Alacritty Project Contributors
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

#[macro_use]
extern crate bitflags;
#[macro_use]
extern crate log;

extern crate base64;
extern crate unicode_width;
extern crate vte;

pub mod ansi;
pub mod cell;
pub mod color;
pub mod config;
pub mod font;
pub mod grid;
pub mod index;
pub mod rgb;
pub mod selection;
pub mod term;
