#![feature(test)]
extern crate test;

use approx::*;
use stb_truetype::*;

/// index map format 12
static DEJA_VU_MONO: &[u8] = include_bytes!("../fonts/DejaVuSansMono.ttf");
static ROBOTO: &[u8] = include_bytes!("../fonts/Roboto-Regular.ttf");
/// index map format 4
static GUDEA: &[u8] = include_bytes!("../fonts/Gudea-Regular.ttf");

const ALPHABET_SIZE: usize = 62;
const ALPHABET: &[char; ALPHABET_SIZE] = &[
    'A', 'a', 'B', 'b', 'C', 'c', 'D', 'd', 'E', 'e', 'F', 'f', 'G', 'g', 'H', 'h', 'I', 'i', 'J',
    'j', 'K', 'k', 'L', 'l', 'M', 'm', 'N', 'n', 'O', 'o', 'P', 'p', 'Q', 'q', 'R', 'r', 'S', 's',
    'T', 't', 'U', 'u', 'V', 'v', 'W', 'w', 'X', 'x', 'W', 'w', 'Z', 'z', '0', '1', '2', '3', '4',
    '5', '6', '7', '8', '9',
];

#[bench]
fn find_glyph_index_deja_vu_mono(b: &mut test::Bencher) {
    let font = FontInfo::new(&*DEJA_VU_MONO, 0).unwrap();

    let mut indices = [0_u32; ALPHABET_SIZE];
    b.iter(|| {
        for (i, c) in ALPHABET.iter().enumerate() {
            indices[i] = font.find_glyph_index(*c as u32);
        }
    });

    assert_eq!(
        indices.to_vec(),
        vec![
            36, 68, 37, 69, 38, 70, 39, 71, 40, 72, 41, 73, 42, 74, 43, 75, 44, 76, 45, 77, 46, 78,
            47, 79, 48, 80, 49, 81, 50, 82, 51, 83, 52, 84, 53, 85, 54, 86, 55, 87, 56, 88, 57, 89,
            58, 90, 59, 91, 58, 90, 61, 93, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28
        ]
    );
}

#[bench]
fn find_glyph_index_gudea(b: &mut test::Bencher) {
    let font = FontInfo::new(&*GUDEA, 0).unwrap();

    let mut indices = [0_u32; ALPHABET_SIZE];
    b.iter(|| {
        for (i, c) in ALPHABET.iter().enumerate() {
            indices[i] = font.find_glyph_index(*c as u32);
        }
    });

    assert_eq!(
        indices.to_vec(),
        vec![
            37, 69, 38, 70, 39, 71, 40, 72, 41, 73, 42, 74, 43, 75, 44, 76, 45, 77, 46, 78, 47, 79,
            48, 80, 49, 81, 50, 82, 51, 83, 52, 84, 53, 85, 54, 86, 55, 87, 56, 88, 57, 89, 58, 90,
            59, 91, 60, 92, 59, 91, 62, 94, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29
        ]
    );
}

/// (x0, x1, y0, y1)
fn rect_to_tuple<T>(r: Rect<T>) -> (T, T, T, T) {
    (r.x0, r.x1, r.y0, r.y1)
}

#[bench]
fn get_glyph_box_deja_vu_mono(b: &mut test::Bencher) {
    let font = FontInfo::new(&*DEJA_VU_MONO, 0).unwrap();

    let indices: Vec<_> = ALPHABET
        .iter()
        .map(|c| font.find_glyph_index(*c as u32))
        .collect();

    let mut boxes = [None; ALPHABET_SIZE];
    b.iter(|| {
        for (i, glyph_index) in indices.iter().enumerate() {
            boxes[i] = font.get_glyph_box(*glyph_index);
        }
    });

    assert_eq!(rect_to_tuple(boxes[11].unwrap()), (195, 1063, 0, 1556));
    assert_eq!(rect_to_tuple(boxes[34].unwrap()), (143, 1233, 0, 1493));
    assert_eq!(rect_to_tuple(boxes[57].unwrap()), (143, 1069, -29, 1493));
}

#[bench]
fn get_glyph_box_gudea(b: &mut test::Bencher) {
    let font = FontInfo::new(&*GUDEA, 0).unwrap();

    let indices: Vec<_> = ALPHABET
        .iter()
        .map(|c| font.find_glyph_index(*c as u32))
        .collect();

    let mut boxes = [None; ALPHABET_SIZE];
    b.iter(|| {
        for (i, glyph_index) in indices.iter().enumerate() {
            boxes[i] = font.get_glyph_box(*glyph_index);
        }
    });

    assert_eq!(rect_to_tuple(boxes[11].unwrap()), (35, 316, 0, 710));
    assert_eq!(rect_to_tuple(boxes[34].unwrap()), (91, 571, 1, 701));
    assert_eq!(rect_to_tuple(boxes[57].unwrap()), (63, 504, -6, 700));
}

#[bench]
fn get_glyph_bitmap_box_subpixel_deja_vu_mono(b: &mut test::Bencher) {
    let font = FontInfo::new(&*DEJA_VU_MONO, 0).unwrap();

    let indices: Vec<_> = ALPHABET
        .iter()
        .map(|c| font.find_glyph_index(*c as u32))
        .collect();

    let (s_x, s_y) = (12.0, 14.5);
    let scale_y = font.scale_for_pixel_height(s_y);
    let scale_x = scale_y * s_x / s_y;

    let mut boxes = [None; ALPHABET_SIZE];
    b.iter(|| {
        for (i, glyph_index) in indices.iter().enumerate() {
            boxes[i] =
                font.get_glyph_bitmap_box_subpixel(*glyph_index, scale_x, scale_y, 656.0, 1034.0);
        }
    });

    assert_eq!(rect_to_tuple(boxes[11].unwrap()), (656, 662, 1024, 1034));
    assert_eq!(rect_to_tuple(boxes[34].unwrap()), (656, 663, 1024, 1034));
    assert_eq!(rect_to_tuple(boxes[57].unwrap()), (656, 662, 1024, 1035));
}

#[bench]
fn get_glyph_bitmap_box_subpixel_gudea(b: &mut test::Bencher) {
    let font = FontInfo::new(&*GUDEA, 0).unwrap();

    let indices: Vec<_> = ALPHABET
        .iter()
        .map(|c| font.find_glyph_index(*c as u32))
        .collect();

    let (s_x, s_y) = (12.0, 14.5);
    let scale_y = font.scale_for_pixel_height(s_y);
    let scale_x = scale_y * s_x / s_y;

    let mut boxes = [None; ALPHABET_SIZE];
    b.iter(|| {
        for (i, glyph_index) in indices.iter().enumerate() {
            boxes[i] =
                font.get_glyph_bitmap_box_subpixel(*glyph_index, scale_x, scale_y, 656.0, 1034.0);
        }
    });

    assert_eq!(rect_to_tuple(boxes[11].unwrap()), (656, 660, 1025, 1034));
    assert_eq!(rect_to_tuple(boxes[34].unwrap()), (656, 662, 1025, 1034));
    assert_eq!(rect_to_tuple(boxes[57].unwrap()), (656, 661, 1025, 1035));
}

/// (x, y, cx, cy, type)
fn vertex_to_tuple(vert: Vertex) -> (i16, i16, i16, i16, u8) {
    (vert.x, vert.y, vert.cx, vert.cy, vert.vertex_type() as u8)
}

#[bench]
fn get_glyph_shape_deja_vu_mono(b: &mut test::Bencher) {
    let font = FontInfo::new(&*DEJA_VU_MONO, 0).unwrap();

    let indices: Vec<_> = ALPHABET
        .iter()
        .map(|c| font.find_glyph_index(*c as u32))
        .collect();

    let mut shapes = vec![None; ALPHABET_SIZE];
    b.iter(|| {
        for (i, glyph_index) in indices.iter().enumerate() {
            shapes[i] = font.get_glyph_shape(*glyph_index);
        }
    });

    let shapes_11: Vec<_> = shapes[11]
        .as_ref()
        .unwrap()
        .iter()
        .map(|v| vertex_to_tuple(*v))
        .collect();
    assert_eq!(
        shapes_11,
        vec![
            (1063, 1556, 0, 0, 1),
            (1063, 1403, 0, 0, 2),
            (854, 1403, 0, 0, 2),
            (716, 1362, 755, 1403, 3),
            (678, 1219, 678, 1322, 3),
            (678, 1120, 0, 0, 2),
            (1063, 1120, 0, 0, 2),
            (1063, 977, 0, 0, 2),
            (678, 977, 0, 0, 2),
            (678, 0, 0, 0, 2),
            (494, 0, 0, 0, 2),
            (494, 977, 0, 0, 2),
            (195, 977, 0, 0, 2),
            (195, 1120, 0, 0, 2),
            (494, 1120, 0, 0, 2),
            (494, 1198, 0, 0, 2),
            (578, 1469, 494, 1382, 3),
            (842, 1556, 663, 1556, 3),
            (1063, 1556, 0, 0, 2)
        ]
    );

    let shapes_47: Vec<_> = shapes[47]
        .as_ref()
        .unwrap()
        .iter()
        .map(|v| vertex_to_tuple(*v))
        .collect();
    assert_eq!(
        shapes_47,
        vec![
            (1118, 1120, 0, 0, 1),
            (717, 584, 0, 0, 2),
            (1157, 0, 0, 0, 2),
            (944, 0, 0, 0, 2),
            (616, 449, 0, 0, 2),
            (289, 0, 0, 0, 2),
            (76, 0, 0, 0, 2),
            (516, 584, 0, 0, 2),
            (115, 1120, 0, 0, 2),
            (319, 1120, 0, 0, 2),
            (616, 715, 0, 0, 2),
            (911, 1120, 0, 0, 2),
            (1118, 1120, 0, 0, 2)
        ]
    );
}

#[bench]
fn get_glyph_shape_gudea(b: &mut test::Bencher) {
    let font = FontInfo::new(&*GUDEA, 0).unwrap();

    let indices: Vec<_> = ALPHABET
        .iter()
        .map(|c| font.find_glyph_index(*c as u32))
        .collect();

    let mut shapes = vec![None; ALPHABET_SIZE];
    b.iter(|| {
        for (i, glyph_index) in indices.iter().enumerate() {
            shapes[i] = font.get_glyph_shape(*glyph_index);
        }
    });

    let shapes_11: Vec<_> = shapes[11]
        .as_ref()
        .unwrap()
        .iter()
        .map(|v| vertex_to_tuple(*v))
        .collect();
    assert_eq!(
        shapes_11,
        vec![
            (113, 500, 0, 0, 1),
            (113, 596, 0, 0, 2),
            (150, 683, 113, 657, 3),
            (231, 710, 188, 710, 3),
            (316, 697, 275, 710, 3),
            (305, 643, 0, 0, 2),
            (256, 649, 272, 649, 3),
            (187, 582, 187, 649, 3),
            (187, 500, 0, 0, 2),
            (289, 500, 0, 0, 2),
            (289, 440, 0, 0, 2),
            (187, 440, 0, 0, 2),
            (187, 0, 0, 0, 2),
            (112, 0, 0, 0, 2),
            (112, 440, 0, 0, 2),
            (35, 440, 0, 0, 2),
            (35, 500, 0, 0, 2),
            (113, 500, 0, 0, 2)
        ]
    );

    let shapes_47: Vec<_> = shapes[47]
        .as_ref()
        .unwrap()
        .iter()
        .map(|v| vertex_to_tuple(*v))
        .collect();
    assert_eq!(
        shapes_47,
        vec![
            (113, 501, 0, 0, 1),
            (218, 324, 0, 0, 2),
            (323, 501, 0, 0, 2),
            (406, 501, 0, 0, 2),
            (259, 255, 0, 0, 2),
            (412, 0, 0, 0, 2),
            (329, 0, 0, 0, 2),
            (218, 198, 0, 0, 2),
            (107, 0, 0, 0, 2),
            (24, 0, 0, 0, 2),
            (177, 255, 0, 0, 2),
            (30, 501, 0, 0, 2),
            (113, 501, 0, 0, 2)
        ]
    );
}

#[bench]
fn get_glyph_shape_compound_glyph_roboto_colon(b: &mut test::Bencher) {
    let font = FontInfo::new(&*ROBOTO, 0).unwrap();

    let colon_index = font.find_glyph_index(':' as u32);

    let mut shape = None;
    b.iter(|| {
        shape = font.get_glyph_shape(colon_index);
    });

    let shape: Vec<_> = shape.unwrap().iter().map(|v| vertex_to_tuple(*v)).collect();

    assert_eq!(
        shape,
        vec![
            (134, 97, -10, 0, 1),
            (162, 177, 134, 145, 3),
            (248, 209, 191, 209, 3),
            (334, 177, 305, 209, 3),
            (364, 97, 364, 145, 3),
            (334, 20, 364, 51, 3),
            (248, -11, 305, -11, 3),
            (162, 20, 191, -11, 3),
            (134, 97, 134, 51, 3),
            (135, 980, -9, 883, 1),
            (163, 1060, 135, 1028, 3),
            (249, 1092, 192, 1092, 3),
            (335, 1060, 306, 1092, 3),
            (365, 980, 365, 1028, 3),
            (335, 903, 365, 934, 3),
            (249, 872, 306, 872, 3),
            (163, 903, 192, 872, 3),
            (135, 980, 135, 934, 3)
        ]
    );
}

/// (advance_width, left_side_bearing)
fn h_metrics_to_tuple(h: HMetrics) -> (i32, i32) {
    (h.advance_width, h.left_side_bearing)
}

#[bench]
fn get_glyph_h_metrics_deja_vu_mono(b: &mut test::Bencher) {
    let font = FontInfo::new(&*DEJA_VU_MONO, 0).unwrap();

    let indices: Vec<_> = ALPHABET
        .iter()
        .map(|c| font.find_glyph_index(*c as u32))
        .collect();

    let mut h_metrics = [HMetrics {
        advance_width: 0,
        left_side_bearing: 0,
    }; ALPHABET_SIZE];
    b.iter(|| {
        for (i, glyph_index) in indices.iter().enumerate() {
            h_metrics[i] = font.get_glyph_h_metrics(*glyph_index);
        }
    });

    assert_eq!(h_metrics_to_tuple(h_metrics[11]), (1233, 195));
    assert_eq!(h_metrics_to_tuple(h_metrics[25]), (1233, 109));
    assert_eq!(h_metrics_to_tuple(h_metrics[49]), (1233, 0));
}

#[bench]
fn get_glyph_h_metrics_gudea(b: &mut test::Bencher) {
    let font = FontInfo::new(&*GUDEA, 0).unwrap();

    let indices: Vec<_> = ALPHABET
        .iter()
        .map(|c| font.find_glyph_index(*c as u32))
        .collect();

    let mut h_metrics = [HMetrics {
        advance_width: 0,
        left_side_bearing: 0,
    }; ALPHABET_SIZE];
    b.iter(|| {
        for (i, glyph_index) in indices.iter().enumerate() {
            h_metrics[i] = font.get_glyph_h_metrics(*glyph_index);
        }
    });

    assert_eq!(h_metrics_to_tuple(h_metrics[11]), (291, 35));
    assert_eq!(h_metrics_to_tuple(h_metrics[25]), (850, 91));
    assert_eq!(h_metrics_to_tuple(h_metrics[49]), (679, 11));
}

#[bench]
fn scale_for_pixel_height_deja_vu_mono(b: &mut test::Bencher) {
    let font = FontInfo::new(&*DEJA_VU_MONO, 0).unwrap();

    let pixel_heights: [f32; 8] = [1.0, 12.0, 14.5, 25.5, 50.0, 112.2, 500.5, 657.5];

    let mut scales = [0.0; 8];
    b.iter(|| {
        // repeat so its a similar number of calls to the other benchmarks
        for _ in 0..test::black_box(8) {
            for (i, pixel_height) in pixel_heights.iter().enumerate() {
                scales[i] = font.scale_for_pixel_height(*pixel_height);
            }
        }
    });

    assert_relative_eq!(scales[0], 0.000_419_463_1);
    assert_relative_eq!(scales[1], 0.005_033_557);
    assert_relative_eq!(scales[2], 0.006_082_215);
    assert_relative_eq!(scales[3], 0.010_696_309);
    assert_relative_eq!(scales[4], 0.020_973_155);
    assert_relative_eq!(scales[5], 0.047_063_757);
    assert_relative_eq!(scales[6], 0.209_941_27);
    assert_relative_eq!(scales[7], 0.275_796_98);
}

#[bench]
fn scale_for_pixel_height_gudea(b: &mut test::Bencher) {
    let font = FontInfo::new(&*GUDEA, 0).unwrap();

    let pixel_heights: [f32; 8] = [1.0, 12.0, 14.5, 25.5, 50.0, 112.2, 500.5, 657.5];

    let mut scales = [0.0; 8];
    b.iter(|| {
        // repeat so its a similar number of calls to the other benchmarks
        for _ in 0..test::black_box(8) {
            for (i, pixel_height) in pixel_heights.iter().enumerate() {
                scales[i] = font.scale_for_pixel_height(*pixel_height);
            }
        }
    });

    assert_relative_eq!(scales[0], 0.000_809_061_5);
    assert_relative_eq!(scales[1], 0.009_708_738);
    assert_relative_eq!(scales[2], 0.011_731_392);
    assert_relative_eq!(scales[3], 0.020_631_067);
    assert_relative_eq!(scales[4], 0.040_453_073);
    assert_relative_eq!(scales[5], 0.090_776_7);
    assert_relative_eq!(scales[6], 0.404_935_27);
    assert_relative_eq!(scales[7], 0.531_957_9);
}

#[bench]
fn get_v_metrics_deja_vu_mono(b: &mut test::Bencher) {
    let font = FontInfo::new(&*DEJA_VU_MONO, 0).unwrap();

    let mut v_metrics = VMetrics {
        ascent: 1,
        descent: 2,
        line_gap: 3,
    };
    b.iter(|| {
        // repeat so its a similar number of calls to the other benchmarks
        for _ in 0..test::black_box(ALPHABET_SIZE) {
            v_metrics = font.get_v_metrics();
        }
    });

    let VMetrics {
        ascent,
        descent,
        line_gap,
    } = v_metrics;
    assert_eq!(ascent, 1901);
    assert_eq!(descent, -483);
    assert_eq!(line_gap, 0);
}

#[bench]
fn get_v_metrics_gudea(b: &mut test::Bencher) {
    let font = FontInfo::new(&*GUDEA, 0).unwrap();

    let mut v_metrics = VMetrics {
        ascent: 1,
        descent: 2,
        line_gap: 3,
    };
    b.iter(|| {
        // repeat so its a similar number of calls to the other benchmarks
        for _ in 0..test::black_box(ALPHABET_SIZE) {
            v_metrics = font.get_v_metrics();
        }
    });

    let VMetrics {
        ascent,
        descent,
        line_gap,
    } = v_metrics;
    assert_eq!(ascent, 972);
    assert_eq!(descent, -264);
    assert_eq!(line_gap, 0);
}
