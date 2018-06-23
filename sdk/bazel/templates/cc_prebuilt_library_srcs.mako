
cc_import(
    name = "${data.target_arch}_prebuilts",
    % if data.is_static:
    static_library = "${data.prebuilt}",
    % else:
    shared_library = "${data.prebuilt}",
    % endif
)
