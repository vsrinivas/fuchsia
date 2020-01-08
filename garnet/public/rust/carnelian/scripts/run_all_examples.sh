set -e

script_dir=`dirname $0`
carnelian_dir=`cd $script_dir/..; pwd`
fuchsia_dir=`cd ../../../..; pwd`
examples_dir=`cd $carnelian_dir/examples; pwd`
examples=$examples_dir/*.rs
for example in ${examples} ; do
  pkg_name=$(basename "${example}" ".rs")
  ${fuchsia_dir}/scripts/fx shell tiles_ctl add fuchsia-pkg://fuchsia.com/${pkg_name}_rs#meta/${pkg_name}_rs.cmx
done
