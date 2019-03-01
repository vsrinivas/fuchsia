// :: Proxies ::
//
// ddk::{protocol_name}ProtocolClient is a simple wrapper around
// {protocol_name_snake}_protocol_t. It does not own the pointers passed to it.
//
// :: Mixins ::
//
// ddk::{protocol_name}Protocol is a mixin class that simplifies writing DDK drivers
// that implement the {protocol_name_lisp} protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_{protocol_name_uppercase} device.
// class {protocol_name}Device;
// using {protocol_name}DeviceType = ddk::Device<{protocol_name}Device, /* ddk mixins */>;
//
// class {protocol_name}Device : public {protocol_name}DeviceType,
//                      public ddk::{protocol_name}Protocol<{protocol_name}Device> {{
//   public:
//     {protocol_name}Device(zx_device_t* parent)
//         : {protocol_name}DeviceType(parent) {{}}
//
{example_decls}
//
//     ...
// }};
