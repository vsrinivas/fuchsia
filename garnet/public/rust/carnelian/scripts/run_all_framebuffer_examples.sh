set -e

script_dir=`dirname $0`
carnelian_dir=`cd $script_dir/..; pwd`
fuchsia_dir=`cd ../../../..; pwd`
examples_dir=`cd $carnelian_dir/examples; pwd`
examples=$examples_dir/drawing*.rs
for example in ${examples} ; do
  pkg_name=$(basename "${example}" ".rs")
  echo "${fuchsia_dir}/scripts/fx shell run fuchsia-pkg://fuchsia.com/${pkg_name}_rs#meta/${pkg_name}_rs.cmx"
done
