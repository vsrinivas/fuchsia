// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::HandleBased;
use fuchsia_zircon_status as zx_status;

fn reverse<T>(value: (T, T)) -> (T, T) {
    (value.1, value.0)
}

fn maybe_reverse<T>(really_do_it: bool, value: (T, T)) -> (T, T) {
    if really_do_it {
        (value.1, value.0)
    } else {
        (value.0, value.1)
    }
}

pub enum CreateHandlePurpose {
    PrimaryTestChannel,
    PayloadChannel,
}

pub trait Fixture {
    fn create_handles(&self, purpose: CreateHandlePurpose) -> (fidl::Channel, fidl::Channel);
}

#[derive(Clone, Copy)]
enum AfterSend {
    RemainOpen,
    CloseSender,
}

fn send_message(
    channels: (fidl::Channel, fidl::Channel),
    after_send: AfterSend,
    mut out: Vec<(&[u8], &mut Vec<fidl::Handle>)>,
) {
    let num_handles_in: Vec<_> = out.iter().map(|(_, handles)| handles.len()).collect();
    for (out_bytes, handles) in out.iter_mut() {
        println!(
            "#    send message from {:?} to {:?}: {:?}, {:?}",
            channels.0, channels.1, out_bytes, handles
        );
        assert_eq!(channels.0.write(out_bytes, *handles), Ok(()));
    }
    match after_send {
        AfterSend::RemainOpen => (),
        AfterSend::CloseSender => drop(channels.0),
    }
    let mut in_bytes = Vec::new();
    for ((out_bytes, handles), num_handles_in) in out.iter_mut().zip(num_handles_in) {
        loop {
            // other channel should eventually receive the message, but we allow that propagation
            // need not be instant
            match channels.1.read_split(&mut in_bytes, *handles) {
                Ok(()) => {
                    assert_eq!(in_bytes.as_slice(), *out_bytes);
                    assert_eq!(handles.len(), num_handles_in);
                    return;
                }
                Err(zx_status::Status::SHOULD_WAIT) => {
                    std::thread::sleep(std::time::Duration::from_millis(10));
                    continue;
                }
                Err(x) => panic!("Unexpected error {:?}", x),
            }
        }
    }
}

fn send_channel(
    first: (fidl::Channel, fidl::Channel),
    second: (fidl::Channel, fidl::Channel),
    after_send_first: AfterSend,
    after_send_second: AfterSend,
) {
    let mut second_b = vec![second.1.into_handle()];
    send_message(first, after_send_first, vec![(&[], &mut second_b)]);
    send_message(
        (second.0, fidl::Channel::from_handle(second_b.into_iter().next().unwrap())),
        after_send_second,
        vec![(&[100, 101, 102, 103, 104], &mut vec![])],
    );
}

pub fn run(fixture: impl Fixture) {
    println!("# send message a->b");
    send_message(
        fixture.create_handles(CreateHandlePurpose::PrimaryTestChannel),
        AfterSend::RemainOpen,
        vec![(&[1, 2, 3], &mut vec![])],
    );
    println!("# send message b->a");
    send_message(
        reverse(fixture.create_handles(CreateHandlePurpose::PrimaryTestChannel)),
        AfterSend::RemainOpen,
        vec![(&[7, 8, 9], &mut vec![])],
    );

    println!("# send message then close a->b");
    send_message(
        fixture.create_handles(CreateHandlePurpose::PrimaryTestChannel),
        AfterSend::CloseSender,
        vec![(&[1, 2, 3], &mut vec![])],
    );
    println!("# send message then close b->a");
    send_message(
        reverse(fixture.create_handles(CreateHandlePurpose::PrimaryTestChannel)),
        AfterSend::CloseSender,
        vec![(&[7, 8, 9], &mut vec![])],
    );

    for reverse_ab in [false, true].iter() {
        for reverse_cd in [false, true].iter() {
            for after_send_ab in [AfterSend::RemainOpen, AfterSend::CloseSender].iter() {
                for after_send_cd in [AfterSend::RemainOpen, AfterSend::CloseSender].iter() {
                    println!(
                        "# send channel {}{}; {}{}",
                        match after_send_ab {
                            AfterSend::RemainOpen => "",
                            AfterSend::CloseSender => "then close ",
                        },
                        match reverse_ab {
                            false => "a->b",
                            true => "b->a",
                        },
                        match after_send_cd {
                            AfterSend::RemainOpen => "",
                            AfterSend::CloseSender => "then close ",
                        },
                        match reverse_ab {
                            false => "c->d",
                            true => "d->c",
                        }
                    );
                    send_channel(
                        maybe_reverse(
                            *reverse_ab,
                            fixture.create_handles(CreateHandlePurpose::PrimaryTestChannel),
                        ),
                        maybe_reverse(
                            *reverse_cd,
                            fixture.create_handles(CreateHandlePurpose::PayloadChannel),
                        ),
                        *after_send_ab,
                        *after_send_cd,
                    );
                }
            }
        }
    }

    // Verify a very large send (just bytes to simplify the test for now)
    send_message(
        fixture.create_handles(CreateHandlePurpose::PrimaryTestChannel),
        AfterSend::CloseSender,
        vec![
            (&[0], &mut vec![]),
            (&[1], &mut vec![]),
            (&[2], &mut vec![]),
            (&[3], &mut vec![]),
            (&[4], &mut vec![]),
            (&[5], &mut vec![]),
            (&[6], &mut vec![]),
            (&[7], &mut vec![]),
            (&[8], &mut vec![]),
            (&[9], &mut vec![]),
            (&[10], &mut vec![]),
            (&[11], &mut vec![]),
            (&[12], &mut vec![]),
            (&[13], &mut vec![]),
            (&[14], &mut vec![]),
            (&[15], &mut vec![]),
            (&[16], &mut vec![]),
            (&[17], &mut vec![]),
            (&[18], &mut vec![]),
            (&[19], &mut vec![]),
            (&[20], &mut vec![]),
            (&[21], &mut vec![]),
            (&[22], &mut vec![]),
            (&[23], &mut vec![]),
            (&[24], &mut vec![]),
            (&[25], &mut vec![]),
            (&[26], &mut vec![]),
            (&[27], &mut vec![]),
            (&[28], &mut vec![]),
            (&[29], &mut vec![]),
            (&[30], &mut vec![]),
            (&[31], &mut vec![]),
            (&[32], &mut vec![]),
            (&[33], &mut vec![]),
            (&[34], &mut vec![]),
            (&[35], &mut vec![]),
            (&[36], &mut vec![]),
            (&[37], &mut vec![]),
            (&[38], &mut vec![]),
            (&[39], &mut vec![]),
            (&[40], &mut vec![]),
            (&[41], &mut vec![]),
            (&[42], &mut vec![]),
            (&[43], &mut vec![]),
            (&[44], &mut vec![]),
            (&[45], &mut vec![]),
            (&[46], &mut vec![]),
            (&[47], &mut vec![]),
            (&[48], &mut vec![]),
            (&[49], &mut vec![]),
            (&[50], &mut vec![]),
            (&[51], &mut vec![]),
            (&[52], &mut vec![]),
            (&[53], &mut vec![]),
            (&[54], &mut vec![]),
            (&[55], &mut vec![]),
            (&[56], &mut vec![]),
            (&[57], &mut vec![]),
            (&[58], &mut vec![]),
            (&[59], &mut vec![]),
            (&[60], &mut vec![]),
            (&[61], &mut vec![]),
            (&[62], &mut vec![]),
            (&[63], &mut vec![]),
            (&[64], &mut vec![]),
            (&[65], &mut vec![]),
            (&[66], &mut vec![]),
            (&[67], &mut vec![]),
            (&[68], &mut vec![]),
            (&[69], &mut vec![]),
            (&[70], &mut vec![]),
            (&[71], &mut vec![]),
            (&[72], &mut vec![]),
            (&[73], &mut vec![]),
            (&[74], &mut vec![]),
            (&[75], &mut vec![]),
            (&[76], &mut vec![]),
            (&[77], &mut vec![]),
            (&[78], &mut vec![]),
            (&[79], &mut vec![]),
            (&[80], &mut vec![]),
            (&[81], &mut vec![]),
            (&[82], &mut vec![]),
            (&[83], &mut vec![]),
            (&[84], &mut vec![]),
            (&[85], &mut vec![]),
            (&[86], &mut vec![]),
            (&[87], &mut vec![]),
            (&[88], &mut vec![]),
            (&[89], &mut vec![]),
            (&[90], &mut vec![]),
            (&[91], &mut vec![]),
            (&[92], &mut vec![]),
            (&[93], &mut vec![]),
            (&[94], &mut vec![]),
            (&[95], &mut vec![]),
            (&[96], &mut vec![]),
            (&[97], &mut vec![]),
            (&[98], &mut vec![]),
            (&[99], &mut vec![]),
            (&[100], &mut vec![]),
            (&[101], &mut vec![]),
            (&[102], &mut vec![]),
            (&[103], &mut vec![]),
            (&[104], &mut vec![]),
            (&[105], &mut vec![]),
            (&[106], &mut vec![]),
            (&[107], &mut vec![]),
            (&[108], &mut vec![]),
            (&[109], &mut vec![]),
            (&[110], &mut vec![]),
            (&[111], &mut vec![]),
            (&[112], &mut vec![]),
            (&[113], &mut vec![]),
            (&[114], &mut vec![]),
            (&[115], &mut vec![]),
            (&[116], &mut vec![]),
            (&[117], &mut vec![]),
            (&[118], &mut vec![]),
            (&[119], &mut vec![]),
            (&[120], &mut vec![]),
            (&[121], &mut vec![]),
            (&[122], &mut vec![]),
            (&[123], &mut vec![]),
            (&[124], &mut vec![]),
            (&[125], &mut vec![]),
            (&[126], &mut vec![]),
            (&[127], &mut vec![]),
            (&[128], &mut vec![]),
            (&[129], &mut vec![]),
            (&[130], &mut vec![]),
            (&[131], &mut vec![]),
            (&[132], &mut vec![]),
            (&[133], &mut vec![]),
            (&[134], &mut vec![]),
            (&[135], &mut vec![]),
            (&[136], &mut vec![]),
            (&[137], &mut vec![]),
            (&[138], &mut vec![]),
            (&[139], &mut vec![]),
            (&[140], &mut vec![]),
            (&[141], &mut vec![]),
            (&[142], &mut vec![]),
            (&[143], &mut vec![]),
            (&[144], &mut vec![]),
            (&[145], &mut vec![]),
            (&[146], &mut vec![]),
            (&[147], &mut vec![]),
            (&[148], &mut vec![]),
            (&[149], &mut vec![]),
            (&[150], &mut vec![]),
            (&[151], &mut vec![]),
            (&[152], &mut vec![]),
            (&[153], &mut vec![]),
            (&[154], &mut vec![]),
            (&[155], &mut vec![]),
            (&[156], &mut vec![]),
            (&[157], &mut vec![]),
            (&[158], &mut vec![]),
            (&[159], &mut vec![]),
            (&[160], &mut vec![]),
            (&[161], &mut vec![]),
            (&[162], &mut vec![]),
            (&[163], &mut vec![]),
            (&[164], &mut vec![]),
            (&[165], &mut vec![]),
            (&[166], &mut vec![]),
            (&[167], &mut vec![]),
            (&[168], &mut vec![]),
            (&[169], &mut vec![]),
            (&[170], &mut vec![]),
            (&[171], &mut vec![]),
            (&[172], &mut vec![]),
            (&[173], &mut vec![]),
            (&[174], &mut vec![]),
            (&[175], &mut vec![]),
            (&[176], &mut vec![]),
            (&[177], &mut vec![]),
            (&[178], &mut vec![]),
            (&[179], &mut vec![]),
            (&[180], &mut vec![]),
            (&[181], &mut vec![]),
            (&[182], &mut vec![]),
            (&[183], &mut vec![]),
            (&[184], &mut vec![]),
            (&[185], &mut vec![]),
            (&[186], &mut vec![]),
            (&[187], &mut vec![]),
            (&[188], &mut vec![]),
            (&[189], &mut vec![]),
            (&[190], &mut vec![]),
            (&[191], &mut vec![]),
            (&[192], &mut vec![]),
            (&[193], &mut vec![]),
            (&[194], &mut vec![]),
            (&[195], &mut vec![]),
            (&[196], &mut vec![]),
            (&[197], &mut vec![]),
            (&[198], &mut vec![]),
            (&[199], &mut vec![]),
            (&[200], &mut vec![]),
            (&[201], &mut vec![]),
            (&[202], &mut vec![]),
            (&[203], &mut vec![]),
            (&[204], &mut vec![]),
            (&[205], &mut vec![]),
            (&[206], &mut vec![]),
            (&[207], &mut vec![]),
            (&[208], &mut vec![]),
            (&[209], &mut vec![]),
            (&[210], &mut vec![]),
            (&[211], &mut vec![]),
            (&[212], &mut vec![]),
            (&[213], &mut vec![]),
            (&[214], &mut vec![]),
            (&[215], &mut vec![]),
            (&[216], &mut vec![]),
            (&[217], &mut vec![]),
            (&[218], &mut vec![]),
            (&[219], &mut vec![]),
            (&[220], &mut vec![]),
            (&[221], &mut vec![]),
            (&[222], &mut vec![]),
            (&[223], &mut vec![]),
            (&[224], &mut vec![]),
            (&[225], &mut vec![]),
            (&[226], &mut vec![]),
            (&[227], &mut vec![]),
            (&[228], &mut vec![]),
            (&[229], &mut vec![]),
            (&[230], &mut vec![]),
            (&[231], &mut vec![]),
            (&[232], &mut vec![]),
            (&[233], &mut vec![]),
            (&[234], &mut vec![]),
            (&[235], &mut vec![]),
            (&[236], &mut vec![]),
            (&[237], &mut vec![]),
            (&[238], &mut vec![]),
            (&[239], &mut vec![]),
            (&[240], &mut vec![]),
            (&[241], &mut vec![]),
            (&[242], &mut vec![]),
            (&[243], &mut vec![]),
            (&[244], &mut vec![]),
            (&[245], &mut vec![]),
            (&[246], &mut vec![]),
            (&[247], &mut vec![]),
            (&[248], &mut vec![]),
            (&[249], &mut vec![]),
            (&[250], &mut vec![]),
            (&[251], &mut vec![]),
            (&[252], &mut vec![]),
            (&[253], &mut vec![]),
            (&[254], &mut vec![]),
            (&[255], &mut vec![]),
        ],
    );
}

struct FidlFixture;

impl Fixture for FidlFixture {
    fn create_handles(&self, _: CreateHandlePurpose) -> (fidl::Channel, fidl::Channel) {
        fidl::Channel::create().unwrap()
    }
}

#[cfg(test)]
#[test]
fn tests() {
    run(FidlFixture)
}
