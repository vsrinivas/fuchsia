
cc_import(
    name = "${data.target_arch}_prebuilts",
    % if data.is_static:
    static_library = "${data.prebuilt}",
    % else:
    shared_library = "${data.prebuilt}",
    % endif
)

package_files(
    name = "${data.target_arch}_dist",
    contents = {
        % for path, source in sorted(data.packaged_files.iteritems()):
        "${source}": "${path}",
        % endfor
    },
)
