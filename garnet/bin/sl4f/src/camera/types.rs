// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_camera2::{DeviceInfo, DeviceType};
use fidl_fuchsia_images::{AlphaFormat, ColorSpace, ImageInfo, PixelFormat, Tiling, Transform};
use fidl_fuchsia_mem::Buffer;
use fuchsia_zircon::Vmo;
use serde::{Deserialize, Deserializer, Serialize, Serializer};
use thiserror::Error;

// FIDL request/response definitions.

#[derive(Serialize, Deserialize, Debug)]
pub struct DetectResult {
    pub camera_id: i32,

    #[serde(with = "DeviceInfoDef")]
    pub camera_info: DeviceInfo,
}

#[cfg(test)]
impl PartialEq for DetectResult {
    fn eq(&self, other: &Self) -> bool {
        self.camera_id == other.camera_id && self.camera_info == other.camera_info
    }
}

#[derive(Deserialize, Debug)]
pub struct SetCfgRequest {
    pub mode: u32,
    pub integration_time: i32,
    pub analog_gain: i32,
    pub digital_gain: i32,
}

#[derive(Serialize, Debug)]
pub struct CaptureResult {
    #[serde(with = "buffer_wrapper")]
    pub image_data: Buffer,
    #[serde(with = "ImageInfoDef")]
    pub image_info: ImageInfo,
}

// TODO(52737): Revise this once data transfer method is decided on.
#[derive(Serialize, Deserialize, Debug)]
pub struct WriteCalibrationDataRequest {
    #[serde(with = "buffer_wrapper")]
    pub calibration_data: Buffer,
    pub file_path: String,
}

#[derive(Serialize, Deserialize, Debug, Error)]
pub enum Sl4fCameraFactoryError {
    #[error("SL4F Error: No camera.")]
    NoCamera = 1,
    #[error("SL4F Error: Streaming not started.")]
    Streaming = 2,
    #[error("SL4F Error: Unable to de/serialize.")]
    Serialization = 3,
}

// |fidl_fuchsia_camera2| Helper definitions for Serde de/serialization.

#[derive(Serialize, Deserialize)]
#[serde(remote = "DeviceType")]
enum DeviceTypeDef {
    Builtin = 1,
    Virtual = 2,
}

// TODO(fxbug.dev/59274): Do not use Serde remote for FIDL tables.
#[derive(Serialize, Deserialize)]
#[serde(remote = "DeviceInfo")]
struct DeviceInfoDef {
    vendor_id: Option<u16>,
    vendor_name: Option<String>,
    product_id: Option<u16>,
    product_name: Option<String>,

    #[serde(default, with = "device_type_wrapper")]
    type_: Option<DeviceType>,
}

impl PartialEq for DeviceInfoDef {
    fn eq(&self, other: &Self) -> bool {
        self.vendor_id == other.vendor_id
            && self.vendor_name == other.vendor_name
            && self.product_id == other.product_id
            && self.product_name == other.product_name
    }
}

mod device_type_wrapper {
    use super::{Deserialize, Deserializer, DeviceType, DeviceTypeDef, Serialize, Serializer};

    pub fn serialize<S: Serializer>(
        value: &Option<DeviceType>,
        serializer: S,
    ) -> Result<S::Ok, S::Error> {
        #[derive(Serialize)]
        struct Helper<'a>(#[serde(with = "DeviceTypeDef")] &'a DeviceType);

        value.as_ref().map(Helper).serialize(serializer)
    }

    pub fn deserialize<'de, D: Deserializer<'de>>(
        deserializer: D,
    ) -> Result<Option<DeviceType>, D::Error> {
        #[derive(Deserialize)]
        struct Helper(#[serde(with = "DeviceTypeDef")] DeviceType);

        let helper = Option::deserialize(deserializer)?;
        Ok(helper.map(|Helper(external)| external))
    }
}

// |fidl_fuchsia_images| Helper definitions for Serde de/serialization.

#[derive(Serialize, Deserialize)]
#[serde(remote = "Transform")]
enum TransformDef {
    Normal = 0,
    FlipHorizontal = 1,
    FlipVertical = 2,
    FlipVerticalAndHorizontal = 3,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "PixelFormat")]
enum PixelFormatDef {
    Bgra8 = 0,
    Yuy2 = 1,
    Nv12 = 2,
    Yv12 = 3,
    R8G8B8A8 = 4,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "ColorSpace")]
enum ColorSpaceDef {
    Srgb = 0,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "Tiling")]
enum TilingDef {
    Linear = 0,
    GpuOptimal = 1,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "AlphaFormat")]
enum AlphaFormatDef {
    Opaque = 0,
    Premultiplied = 1,
    NonPremultiplied = 2,
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "ImageInfo")]
struct ImageInfoDef {
    #[serde(with = "TransformDef")]
    transform: Transform,

    width: u32,
    height: u32,
    stride: u32,

    #[serde(with = "PixelFormatDef")]
    pixel_format: PixelFormat,

    #[serde(with = "ColorSpaceDef")]
    color_space: ColorSpace,

    #[serde(with = "TilingDef")]
    tiling: Tiling,

    #[serde(with = "AlphaFormatDef")]
    alpha_format: AlphaFormat,
}

// |fidl_fuchsia_mem| Helper definitions for Serde de/serialization.

mod buffer_wrapper {
    use super::{Buffer, Deserialize, Deserializer, Serializer, Vmo};
    use base64;

    pub fn serialize<S: Serializer>(value: &Buffer, serializer: S) -> Result<S::Ok, S::Error> {
        let mut data = vec![0; value.size as usize];
        value.vmo.read(&mut data, 0).map_err(serde::ser::Error::custom)?;
        serializer.serialize_str(&base64::encode(&data))
    }

    pub fn deserialize<'de, D: Deserializer<'de>>(deserializer: D) -> Result<Buffer, D::Error> {
        let s = String::deserialize(deserializer)?;
        let len = s.len();
        let buf =
            Buffer { size: len as u64, vmo: Vmo::create(len as u64).expect("VMO creation failed") };
        match base64::decode(&s) {
            Ok(v) => {
                buf.vmo.write(&v, 0).map_err(serde::de::Error::custom)?;
            }
            Err(_) => {}
        }
        Ok(buf)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use base64;
    use serde_json::{from_value, json, to_value};

    struct Setup {
        test_int: i32,
        test_str: String,
        empty_buf: Buffer,
        full_buf: Buffer,
        device_info: DeviceInfo,
        image_info: ImageInfo,
    }

    impl Setup {
        fn new() -> Self {
            const BUFFER_SIZE: u64 = 5;
            const TEST_INT32: i32 = 256;
            const TEST_UINT32: u32 = 256;
            const TEST_UINT16: u16 = 256;
            const TEST_STR: &str = "test_string";

            let one_vec = vec![1; BUFFER_SIZE as usize];
            let full_vmo = Vmo::create(BUFFER_SIZE).unwrap();
            full_vmo.write(&one_vec, 0).unwrap();

            Self {
                test_int: TEST_INT32,
                test_str: TEST_STR.to_string(),

                empty_buf: Buffer { size: 0, vmo: Vmo::create(0).unwrap() },
                full_buf: Buffer { size: BUFFER_SIZE, vmo: full_vmo },

                device_info: DeviceInfo {
                    vendor_id: Some(TEST_UINT16),
                    vendor_name: Some(TEST_STR.to_string()),
                    product_id: Some(TEST_UINT16),
                    product_name: Some(TEST_STR.to_string()),
                    type_: Some(DeviceType::Virtual),
                },

                image_info: ImageInfo {
                    transform: Transform::Normal,
                    width: TEST_UINT32,
                    height: TEST_UINT32,
                    stride: TEST_UINT32,
                    pixel_format: PixelFormat::Bgra8,
                    color_space: ColorSpace::Srgb,
                    tiling: Tiling::Linear,
                    alpha_format: AlphaFormat::Opaque,
                },
            }
        }
    }

    #[test]
    fn serialize_detect_result() {
        let setup = Setup::new();
        let serialized_val =
            to_value(DetectResult { camera_id: setup.test_int, camera_info: setup.device_info })
                .unwrap();
        let expected_val = json!({
            "camera_id": 256,
            "camera_info": {
                "vendor_id": 256,
                "vendor_name": "test_string",
                "product_id": 256,
                "product_name": "test_string",
                "type_": "Virtual",
            }
        });
        assert_eq!(serialized_val, expected_val);
    }

    #[test]
    fn serialize_capture_image_result() {
        let setup = Setup::new();

        let serialized_val =
            to_value(CaptureResult { image_data: setup.full_buf, image_info: setup.image_info })
                .unwrap();
        let expected_val = json!({
            "image_data": base64::encode(&vec![1; 5]),
            "image_info": {
                "transform": "Normal",
                "width": 256,
                "height": 256,
                "stride": 256,
                "pixel_format": "Bgra8",
                "color_space": "Srgb",
                "tiling": "Linear",
                "alpha_format": "Opaque",
            }
        });
        assert_eq!(serialized_val, expected_val);
    }

    #[test]
    fn serialize_capture_image_result_empty_buf() {
        let setup = Setup::new();

        let serialized_val =
            to_value(CaptureResult { image_data: setup.empty_buf, image_info: setup.image_info })
                .unwrap();
        let expected_val = json!({
            "image_data": "",
            "image_info": {
                "transform": "Normal",
                "width": 256,
                "height": 256,
                "stride": 256,
                "pixel_format": "Bgra8",
                "color_space": "Srgb",
                "tiling": "Linear",
                "alpha_format": "Opaque",
            }
        });
        assert_eq!(serialized_val, expected_val);
    }

    #[test]
    fn deserialize_write_calibration_data_request() {
        let setup = Setup::new();

        let serialized_req: WriteCalibrationDataRequest = from_value(json!({
            "calibration_data": base64::encode(&vec![1; 5]),
            "file_path": "test_string",
        }))
        .unwrap();
        let expected_req = WriteCalibrationDataRequest {
            calibration_data: setup.full_buf,
            file_path: setup.test_str,
        };

        let serialized_len = serialized_req.calibration_data.vmo.get_size().unwrap();
        let expected_len = expected_req.calibration_data.vmo.get_size().unwrap();
        assert_eq!(serialized_len, expected_len);

        let mut serialized_val = vec![0; 5];
        serialized_req.calibration_data.vmo.read(&mut serialized_val, 0).unwrap();
        let mut expected_val = vec![0; 5];
        expected_req.calibration_data.vmo.read(&mut expected_val, 0).unwrap();
        assert_eq!(serialized_val, expected_val);
    }
}
