// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use log::info;

#[fuchsia::component]
fn main() {
    let fav_animal = std::env::var("FAVORITE_ANIMAL").unwrap_or("None".to_owned());
    info!("Favorite Animal: {}", fav_animal);
}
