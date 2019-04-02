// Copyright 2017 The UNIC Project Developers.
//
// See the COPYRIGHT file at the top-level directory of this distribution.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use unic_ucd_category::GeneralCategory as GC;

#[test]
fn test_display() {
    assert_eq!(format!("{}", GC::UppercaseLetter), "Uppercase Letter");
    assert_eq!(format!("{}", GC::Unassigned), "Unassigned");
}
