// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod test {
    use wayland_scanner_lib::{ArgKind, AstResult, Parser, Protocol};

    fn parse_str(s: &str) -> AstResult<Protocol> {
        Protocol::from_parse_tree(Parser::from_str(s).read_document().unwrap())
    }

    #[test]
    fn test_empty_protocol() {
        const PROTOCOL: &'static str = r#"
    <?xml version="1.0" encoding="UTF-8"?>
    <protocol name="test_protocol">
    </protocol>
    "#;
        let protocol = parse_str(PROTOCOL).unwrap();
        assert_eq!(protocol.name, "test_protocol");
        assert!(!protocol.copyright.is_some());
        assert!(!protocol.description.is_some());
        assert!(protocol.interfaces.is_empty());
    }

    #[test]
    fn parser_integration_test() {
        const PROTOCOL: &'static str = r#"
    <?xml version="1.0" encoding="UTF-8"?>
    <protocol name="my_protocol">
        <copyright>This is a copyright</copyright>
        <description summary="protocol summary">This describes my protocol</description>


        <interface name="my_interface" version="3">
            <description summary="interface summary">This describes my interface</description>

            <request name="request_with_all_arg_types" since="2">
                <description summary="request_with_all_arg_types summary">This describes request_with_all_arg_types</description>
                <arg name="int_arg" type="int"/>
                <arg name="uint_arg" type="uint"/>
                <arg name="fixed_arg" type="fixed"/>
                <arg name="object_arg" type="object"/>
                <arg name="new_id_arg" type="new_id" interface="my_interface"/>
                <arg name="string_arg" type="string"/>
                <arg name="array_arg" type="array"/>
                <arg name="fd_arg" type="fd"/>
            </request>

            <event name="event_with_all_arg_types" since="1024">
                <description summary="event_with_all_arg_types summary">This describes event_with_all_arg_types</description>
                <arg name="int_arg" type="int"/>
                <arg name="uint_arg" type="uint"/>
                <arg name="fixed_arg" type="fixed"/>
                <arg name="object_arg" type="object"/>
                <arg name="new_id_arg" type="new_id" interface="my_interface"/>
                <arg name="string_arg" type="string"/>
                <arg name="array_arg" type="array"/>
                <arg name="fd_arg" type="fd"/>
            </event>
        </interface>
    </protocol>
    "#;
        let protocol = parse_str(PROTOCOL).unwrap();
        assert_eq!(protocol.name, "my_protocol");
        let copyright = protocol.copyright.expect("Missing protocol copyright");
        assert_eq!(copyright, "This is a copyright");
        let description = protocol.description.expect("Missing protocol description");
        assert_eq!(description.summary, "protocol summary");
        assert_eq!(description.description, "This describes my protocol");

        let mut interfaces = protocol.interfaces;
        assert_eq!(1, interfaces.len());
        let interface = interfaces.remove(0);
        assert_eq!(interface.name, "my_interface");
        assert_eq!(interface.version, 3);
        let description = interface.description.expect("Missing interface description");
        assert_eq!(description.summary, "interface summary");
        assert_eq!(description.description, "This describes my interface");

        let mut requests = interface.requests;
        assert_eq!(1, requests.len());
        let request = requests.remove(0);
        assert_eq!(request.name, "request_with_all_arg_types");
        assert_eq!(request.since, 2);
        assert_eq!(request.request_type, None);
        let description = request.description.expect("Missing request description");
        assert_eq!(description.summary, "request_with_all_arg_types summary");
        assert_eq!(description.description, "This describes request_with_all_arg_types");
        let mut args = request.args;
        assert_eq!(8, args.len());

        let arg = args.remove(0);
        assert_eq!(arg.name, "int_arg");
        assert_eq!(arg.kind, ArgKind::Int);
        let arg = args.remove(0);
        assert_eq!(arg.name, "uint_arg");
        assert_eq!(arg.kind, ArgKind::Uint);
        let arg = args.remove(0);
        assert_eq!(arg.name, "fixed_arg");
        assert_eq!(arg.kind, ArgKind::Fixed);
        let arg = args.remove(0);
        assert_eq!(arg.name, "object_arg");
        assert_eq!(arg.kind, ArgKind::Object);
        let arg = args.remove(0);
        assert_eq!(arg.name, "new_id_arg");
        assert_eq!(arg.kind, ArgKind::NewId);
        let arg = args.remove(0);
        assert_eq!(arg.name, "string_arg");
        assert_eq!(arg.kind, ArgKind::String);
        let arg = args.remove(0);
        assert_eq!(arg.name, "array_arg");
        assert_eq!(arg.kind, ArgKind::Array);
        let arg = args.remove(0);
        assert_eq!(arg.name, "fd_arg");
        assert_eq!(arg.kind, ArgKind::Fd);

        let mut events = interface.events;
        assert_eq!(1, events.len());
        let event = events.remove(0);
        assert_eq!(event.name, "event_with_all_arg_types");
        assert_eq!(event.since, 1024);
        let description = event.description.expect("Missing event description");
        assert_eq!(description.summary, "event_with_all_arg_types summary");
        assert_eq!(description.description, "This describes event_with_all_arg_types");
        let mut args = event.args;
        assert_eq!(8, args.len());

        let arg = args.remove(0);
        assert_eq!(arg.name, "int_arg");
        assert_eq!(arg.kind, ArgKind::Int);
        let arg = args.remove(0);
        assert_eq!(arg.name, "uint_arg");
        assert_eq!(arg.kind, ArgKind::Uint);
        let arg = args.remove(0);
        assert_eq!(arg.name, "fixed_arg");
        assert_eq!(arg.kind, ArgKind::Fixed);
        let arg = args.remove(0);
        assert_eq!(arg.name, "object_arg");
        assert_eq!(arg.kind, ArgKind::Object);
        let arg = args.remove(0);
        assert_eq!(arg.name, "new_id_arg");
        assert_eq!(arg.kind, ArgKind::NewId);
        let arg = args.remove(0);
        assert_eq!(arg.name, "string_arg");
        assert_eq!(arg.kind, ArgKind::String);
        let arg = args.remove(0);
        assert_eq!(arg.name, "array_arg");
        assert_eq!(arg.kind, ArgKind::Array);
        let arg = args.remove(0);
        assert_eq!(arg.name, "fd_arg");
        assert_eq!(arg.kind, ArgKind::Fd);
    }
}
