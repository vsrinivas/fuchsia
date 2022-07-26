#!/bin/sh
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# list of CMX files we know we can't yet autoconvert
exceptions=(\
    # dart/flutter components
    "./build/dart/tests/meta/dart-component.cmx"
    "./build/flutter/tests/meta/flutter-component-with-flutter-driver.cmx"
    "./build/flutter/tests/meta/flutter-component.cmx"
    "./sdk/dart/fuchsia_inspect/meta/dart-archive-reader-test.cmx"
    "./sdk/dart/fuchsia_inspect/test/inspect_flutter_integration_tester/meta/inspect_dart_integration_test_driver.cmx"
    "./sdk/dart/fuchsia_inspect/test/inspect_flutter_integration_tester/meta/inspect_flutter_integration_tester.cmx"
    "./sdk/dart/fuchsia_inspect/test/validator_puppet/meta/inspect_validator_test_dart.cmx"
    "./sdk/dart/fuchsia_modular_testing/meta/fuchsia-modular-testing-integration-tests.cmx"
    "./sdk/dart/fuchsia_modular/meta/fuchsia-modular-integration-tests.cmx"
    "./sdk/dart/fuchsia_services/meta/fuchsia-services-integration-tests.cmx"
    "./sdk/dart/fuchsia_services/test_support/meta/fuchsia-services-foo-test-server.cmx"
    "./sdk/dart/zircon/meta/zircon_unittests.cmx"
    "./src/experiences/bin/ermine_testserver/meta/ermine_testserver.cmx"
    "./src/experiences/bin/settings/license/meta/license_settings.cmx"
    "./src/experiences/bin/settings/meta/settings.cmx"
    "./src/experiences/examples/localized_flutter/localized_flutter_app/meta/localized_flutter_app.cmx"
    "./src/experiences/examples/spinning_cube/meta/spinning_cube.cmx"
    "./src/flutter/tests/bin/null-safe-enabled-flutter/meta/null-safe-enabled-flutter.cmx"
    "./src/flutter/tests/bin/pingable-flutter-component/meta/pingable-flutter-component-debug-build-cfg.cmx"
    "./src/flutter/tests/bin/pingable-flutter-component/meta/pingable-flutter-component-profile-build-cfg.cmx"
    "./src/flutter/tests/bin/pingable-flutter-component/meta/pingable-flutter-component-release-build-cfg.cmx"
    "./src/lib/diagnostics/inspect/dart/bench/meta/dart-inspect-benchmarks.cmx"
    "./src/lib/fidl/dart/gidl/meta/gidl-library-test.cmx"
    "./src/tests/benchmarks/fidl/dart/meta/dart-fidl-benchmarks.cmx"
    "./src/tests/fidl/compatibility/meta/dart-server.cmx"
    "./src/tests/fidl/dart_bindings_test/meta/conformance.cmx"
    "./src/tests/fidl/dart_bindings_test/meta/dart-bindings-test.cmx"
    "./src/tests/fidl/dart_bindings_test/meta/server.cmx"
    "./src/ui/tests/integration_flutter_tests/embedder/child-view/meta/child-view.cmx"
    "./src/ui/tests/integration_flutter_tests/embedder/parent-view/meta/parent-view-disabled-hittest.cmx"
    "./src/ui/tests/integration_flutter_tests/embedder/parent-view/meta/parent-view-show-overlay.cmx"
    "./src/ui/tests/integration_flutter_tests/embedder/parent-view/meta/parent-view.cmx"

    # these are shards, not for converting
    "./build/dart/meta/aot_product_runtime.cmx"
    "./build/dart/meta/aot_runtime.cmx"
    "./build/dart/meta/jit_product_runtime.cmx"
    "./build/dart/meta/jit_runtime.cmx"
    "./build/flutter/meta/aot_product_runtime.cmx"
    "./build/flutter/meta/aot_runtime.cmx"
    "./build/flutter/meta/jit_product_runtime.cmx"
    "./build/flutter/meta/jit_runtime.cmx"

    # uses shards to have a valid minimal v1 manifest
    "./src/ui/bin/terminal/meta/vsh-terminal.cmx"

    # manually specifies a runner package
    "./src/virtualization/packages/meta/guest_package.cmx"

    # passes args to injected services
    "./src/connectivity/lowpan/drivers/lowpan-spinel-driver/tests/test-components/meta/lowpan-driver-provision-mock.cmx"
    "./src/connectivity/openthread/tests/test-components/meta/ot-radio-ncp-ver-query-mock.cmx"
    "./src/connectivity/openthread/tests/test-components/meta/ot-stack-soft-reset-mock.cmx"
    "./src/connectivity/telephony/tests/meta/tel_fake_at_driver_test.cmx"
    "./src/connectivity/telephony/tests/meta/tel_fake_at_query.cmx"
    "./src/connectivity/telephony/tests/meta/tel_fake_qmi_query.cmx"
    "./src/connectivity/telephony/tests/meta/tel_snooper_multi_clients.cmx"
    "./src/connectivity/telephony/tests/meta/telephony-qmi-usb-integration-test.cmx"
    "./src/performance/trace/tests/meta/detach_tests.cmx"
    "./src/performance/trace/tests/meta/ktrace_integration_tests.cmx"
    "./src/performance/trace/tests/meta/provider_destruction_tests.cmx"
    "./src/performance/trace/tests/meta/return_child_result_tests.cmx"
    "./src/performance/trace/tests/meta/shared_provider_integration_tests.cmx"
    "./src/performance/trace/tests/meta/trace_integration_tests.cmx"
    "./src/factory/fake_factory_store_providers/meta/fake_factory_store_providers_test.cmx"
    "./src/intl/intl_services/tests/meta/intl_services_test.cmx"
    "./src/performance/cpuperf_provider/meta/cpuperf_provider_integration_tests.cmx"
    "./src/sys/sysmgr/integration_tests/meta/service_startup_test.cmx"\

    # does not have a registered v2 child for an injected service replacement
    "./src/chromium/web_runner_tests/meta/web_runner_integration_tests.cmx"
    "./src/chromium/web_runner_tests/meta/web_runner_pixel_tests.cmx"
    "./src/diagnostics/persistence/tests/meta/canonical-diagnostics-persistence-test.cmx"
    "./src/diagnostics/validator/inspect/meta/validator.cmx"
    "./src/factory/fake_factory_items/meta/fake_factory_items_test.cmx"
    "./src/lib/fake-clock/examples/rust/meta/test.cmx"
    "./src/lib/fake-clock/lib/meta/fake_clock_lib_test.cmx"
    "./src/media/codec/examples/use_media_decoder/meta/use_h264_decoder_secure_input_output_test.cmx"
    "./src/media/codec/examples/use_media_decoder/meta/use_vp9_decoder_secure_input_output_test.cmx"
    "./src/recovery/system/meta/system_installer_bin_test.cmx"
    "./src/recovery/system/meta/system_recovery_bin_test.cmx"
    "./src/security/codelab/exploit/meta/security-codelab.cmx"
    "./src/security/codelab/smart_door/meta/smart-door-functional-test.cmx"
    "./src/security/codelab/solution/meta/security-codelab.cmx"
    "./src/security/kms_test_client/meta/kms_test_client.cmx"
    "./src/sys/pkg/lib/pkgfs/meta/pkgfs-lib-test.cmx"
    "./src/sys/pkg/testing/pkgfs-ramdisk/meta/pkgfs-ramdisk-lib-test.cmx"
    "./src/sys/test_runners/legacy_test/test_data/echo_test/meta/echo_test.cmx"

    # requests boot in its sandbox
    "./src/connectivity/openthread/ot-stack/meta/ot-stack.cmx"
    "./src/developer/debug/debug_agent/meta/debug_agent.cmx"
    "./src/developer/sched/meta/sched_tests.cmx"
    "./src/devices/bin/driver_host/meta/driver_host_test.cmx"
    "./src/devices/tests/bind-fail-test/meta/bind-fail-test.cmx"
    "./src/devices/tests/ddk-instance-lifecycle-test/meta/ddk-instance-lifecycle-test.cmx"
    "./src/devices/tests/ddk-metadata-test/meta/ddk-metadata-test.cmx"
    "./src/devices/tests/driver-inspect-test/meta/driver-inspect-test.cmx"
    "./src/lib/process/meta/process_unittests.cmx"
    "./src/ui/bin/terminal/meta/terminal_core.cmx"
    "./src/ui/bin/terminal/meta/terminal.cmx"

    # requests pkgfs in its sandbox
    "./src/connectivity/lowpan/drivers/lowpan-spinel-driver/meta/lowpan-spinel-driver.cmx"
    "./src/connectivity/openthread/tests/test-components/meta/ot-radio-ncp-ver-query.cmx"
    "./src/connectivity/openthread/tests/test-components/meta/ot-stack-ncp-ver-query-mock.cmx"
    "./src/connectivity/openthread/tests/test-components/meta/ot-stack-ncp-ver-query.cmx"
    "./src/connectivity/openthread/tests/test-components/meta/ot-stack-soft-reset.cmx"
    "./src/connectivity/telephony/telephony/meta/telephony_bin_test.cmx"
    "./src/sys/appmgr/integration_tests/lifecycle/meta/appmgr-lifecycle-tests.cmx"
    "./src/sys/appmgr/integration_tests/meta/appmgr_full_integration_tests.cmx"
    "./src/sys/appmgr/integration_tests/meta/failing_appmgr.cmx"
    "./src/sys/appmgr/integration_tests/policy/meta/pkgfs_versions.cmx"
    "./src/sys/appmgr/integration_tests/sandbox/meta/path-traversal-escape-child.cmx"
    "./src/sys/pkg/bin/system-update-checker/meta/system-update-checker.cmx"
    "./src/sys/sysmgr/integration_tests/meta/package_updating_loader_test.cmx"

    # requests system in its sandbox
    "./src/devices/bus/drivers/pci/test/meta/pci-driver-test.cmx"
    "./src/sys/sysmgr/meta/sysmgr.cmx"

    # uses vulkan feature
    "./scripts/sdk/gn/test_project/tests/package/meta/part2.cmx"

    # uses deprecated-ambient-replace-as-executable
    "./scripts/sdk/gn/test_project/chromium_compat/context_provider.cmx"
    "./sdk/dart/fuchsia_inspect/test/integration/meta/dart_inspect_vmo_test.cmx"
    "./src/sys/appmgr/integration_tests/policy/meta/deprecated_ambient_replace_as_exec.cmx"
    "./src/sys/appmgr/integration_tests/sandbox/features/ambient-executable-policy/meta/has_ambient_executable.cmx"
    "./src/sys/component_manager/tests/security_policy/ambient_mark_vmo_exec/meta/cm_for_test.cmx"
    "./src/sys/component_manager/tests/security_policy/capability_allowlist/meta/cm_for_test.cmx"
    "./src/sys/component_manager/tests/security_policy/main_process_critical/meta/cm_for_test.cmx"

    # uses deprecated-shell
    "./src/devices/tests/devfs/meta/devfs-test.cmx"
    "./src/sys/appmgr/integration_tests/components/meta/echo_deprecated_shell.cmx"
    "./src/sys/appmgr/integration_tests/policy/meta/deprecated_shell.cmx"
    "./src/sys/appmgr/integration_tests/sandbox/features/shell/meta/has_deprecated_shell.cmx"
    "./src/sys/run_test_component/test/meta/run_test_component_test.cmx"

    # uses deprecated-global-dev
    "./src/sys/appmgr/integration_tests/sandbox/features/deprecated-global-dev/meta/has_deprecated_global_dev.cmx"
    "./src/virtualization/bin/linux_runner/meta/linux_runner.cmx"

    # uses /dev/zero
    "./sdk/lib/fdio/tests/meta/fdio_test.cmx"

    # uses /dev/null
    "./src/diagnostics/archivist/tests/logs/go/meta/logs_integration_go_tests.cmx"

    # uses device outside /dev/class/*
    "./src/factory/factory_store_providers/meta/factory_store_providers.cmx"
    "./src/graphics/drivers/msd-vsi-vip/tests/integration/meta/msd_vsi_vip_integration_tests.cmx"
    "./src/performance/lib/perfmon/meta/perfmon_tests.cmx"
    "./src/zircon/bin/hwstress/meta/hwstress.cmx"

    # uses device name we can't parse properly
    "./src/graphics/drivers/msd-intel-gen/tests/meta/msd_intel_gen_integration_tests.cmx"
    "./src/graphics/tests/goldfish_test/meta/goldfish_test.cmx"
    "./src/graphics/tests/vkloop/meta/vkloop.cmx"
    "./src/media/drivers/amlogic_decoder/tests/runner/meta/amlogic_decoder_integration_tests.cmx"
    "./src/recovery/system/meta/system_recovery_installer.cmx"
    "./src/recovery/system/meta/system_recovery.cmx"
    "./src/sys/pkg/bin/pkgfs/meta/pmd_pkgfs_test.cmx"
    "./src/sys/pkg/lib/isolated-swd/meta/isolated-swd-tests.cmx"
    "./src/sys/pkg/tests/isolated-ota/meta/isolated-ota-integration-test.cmx"
    "./src/sys/pkg/tests/usb-system-update-checker/meta/usb-system-update-checker-integration-test.cmx"

    # cmx file is intentionally invalid
    "./scripts/sdk/gn/test_project/tests/package/meta/invalid.cmx"
    "./src/modular/tests/meta/module_with_fake_runner.cmx"
    "./src/sys/appmgr/integration_tests/mock_runner/fake_component/meta/fake_component.cmx"
)

# find all the CMXes we think we might be able to convert automatically
output="$FUCHSIA_DIR/out/default/cmxes.list"
find . -type f -name '*.cmx' \
    -not -path "./examples/*" \
    -not -path "./out/*" \
    -not -path "./prebuilt/*" \
    -not -path "./third_party/*" \
    -not -path "./tools/cmx2cml/*" \
    -not -path "./vendor/*" > $output

# remove any files which we know we can't convert
tempfile=$(mktemp)
for exception in ${exceptions[@]}; do
    grep -v "$exception" $output > $tempfile
    mv $tempfile $output
done

sort $output > $tempfile
mv $tempfile $output

# filter out any files which would generate CML that already exist
echo "found $(wc -l < "$output") cmx files to possibly convert..."
echo -n > $tempfile
for f in $(cat $output); do
    cml="${f%.cmx}.cml"
    if ! $(git ls-files --error-unmatch $cml &> /dev/null); then
        echo $f >> $tempfile
    fi
done
mv $tempfile $output

echo "converting $(wc -l < "$output") cmx files which don't already have CML equivalents..."

# run the conversion, assuming the runner since it mostly doesn't affect the output
fx cmx2cml --runner elf-test -f $output

echo "done!"
