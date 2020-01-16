// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/annotation_xdr.h"

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/base64url/base64url.h"

namespace modular {

// Serialization and deserialization of fuchsia::modular::Annotation
// to and from JSON.

namespace {

std::vector<uint8_t> BytesFromBase64(const std::string& base64) {
  std::string decoded;

  if (!base64url::Base64UrlDecode(base64, &decoded)) {
    FXL_LOG(ERROR) << "Unable to decode from Base64";
    return std::vector<uint8_t>{};
  }

  auto decoded_data = reinterpret_cast<const uint8_t*>(decoded.data());
  return std::vector<uint8_t>(decoded_data, decoded_data + decoded.length());
}

std::string BytesToBase64(const std::vector<uint8_t>& bytes) {
  return base64url::Base64UrlEncode({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

void XdrAnnotationValue_v0(XdrContext* const xdr, fuchsia::modular::AnnotationValue* const data) {
  static constexpr char kTag[] = "@tag";
  static constexpr char kValue[] = "@value";
  static constexpr char kTextTag[] = "text";
  static constexpr char kBytesTag[] = "bytes";
  static constexpr char kBufferTag[] = "buffer";

  switch (xdr->op()) {
    case XdrOp::FROM_JSON: {
      std::string tag;
      xdr->Field(kTag, &tag);

      if (tag == kTextTag) {
        std::string text;
        xdr->Field(kValue, &text);
        data->set_text(std::move(text));
      } else if (tag == kBytesTag) {
        std::string bytes_base64;
        xdr->Field(kValue, &bytes_base64);
        data->set_bytes(BytesFromBase64(bytes_base64));
      } else if (tag == kBufferTag) {
        std::string buffer_base64;
        xdr->Field(kValue, &buffer_base64);

        std::string decoded;
        if (!base64url::Base64UrlDecode(buffer_base64, &decoded)) {
          FXL_LOG(ERROR) << "Unable to decode buffer value from Base64";
        }

        fuchsia::mem::Buffer buffer{};
        if (!fsl::VmoFromString(decoded, &buffer)) {
          FXL_LOG(ERROR)
              << "Unable to convert buffer VMO to string; annotation value will be empty";
        }

        data->set_buffer(std::move(buffer));
      } else {
        FXL_LOG(ERROR) << "XdrAnnotationValue_v0 FROM_JSON unknown tag: " << tag;
      }
      break;
    }

    case XdrOp::TO_JSON: {
      std::string tag;

      switch (data->Which()) {
        case fuchsia::modular::AnnotationValue::Tag::kText: {
          tag = kTextTag;
          std::string text = data->text();
          xdr->Field(kValue, &text);
          break;
        }
        case fuchsia::modular::AnnotationValue::Tag::kBytes: {
          tag = kBytesTag;
          std::string bytes_base64 = BytesToBase64(data->bytes());
          xdr->Field(kValue, &bytes_base64);
          break;
        }
        case fuchsia::modular::AnnotationValue::Tag::kBuffer: {
          tag = kBufferTag;
          std::string buffer;
          if (!fsl::StringFromVmo(data->buffer(), &buffer)) {
            FXL_LOG(ERROR)
                << "Unable to convert buffer VMO to string; annotation value will be empty";
          }
          auto buffer_base64 = base64url::Base64UrlEncode(buffer);
          xdr->Field(kValue, &buffer_base64);
          break;
        }
        default:
        case fuchsia::modular::AnnotationValue::Tag::kUnknown: {
          FXL_LOG(ERROR) << "XdrAnnotation_v0 TO_JSON unknown tag: "
                         << static_cast<int>(data->Which());
          break;
        }
      }

      xdr->Field(kTag, &tag);
      break;
    }
  }
}

void XdrAnnotation_v0(XdrContext* const xdr, fuchsia::modular::Annotation* const data) {
  xdr->Field("key", &data->key);
  xdr->Field("value", &data->value, XdrAnnotationValue_v0);
}

}  // namespace

void XdrAnnotation(XdrContext* const xdr, fuchsia::modular::Annotation* const data) {
  XdrAnnotation_v0(xdr, data);
}

}  // namespace modular
