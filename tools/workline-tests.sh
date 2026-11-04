#!/usr/bin/env bash
# The project's tests for workline's tester and developer
# (.workline/config.yaml): every native PlatformIO suite of the components
# the given paths fall in, or of every component when none is given, in one
# JUnit report.
#
#   tools/workline-tests.sh <report> [path ...]
#
# Each component is a PlatformIO project of its own, so `pio test` runs once
# per component and this script merges their reports. A test case is named
# <component>/<suite> in its classname, and its file is given from the
# repository's root; compiler errors are rewritten so too, so that a build
# broken by a new test points at the test's file.
set -uo pipefail

report=${1:?usage: tools/workline-tests.sh <report> [path ...]}
shift
root=$(git rev-parse --show-toplevel)
cd "$root" || exit 1
case $report in /*) ;; *) report="$root/$report" ;; esac

# The components holding a native environment.
all=()
for ini in $(git ls-files 'DomoticsCore-*/platformio.ini'); do
  grep -q '^\[env:native\]' "$ini" && all+=("$(dirname "$ini")")
done

# The components the paths fall in; none named, or a path outside every
# component, runs them all.
picked=()
if [ $# -gt 0 ]; then
  for p in "$@"; do
    p=${p#./}
    comp=${p%%/*}
    found=""
    for c in "${all[@]}"; do [ "$c" = "$comp" ] && found=$c; done
    if [ -z "$found" ]; then picked=(); break; fi
    case " ${picked[*]} " in *" $found "*) ;; *) picked+=("$found") ;; esac
  done
fi
[ ${#picked[@]} -gt 0 ] || picked=("${all[@]}")

parts=$(mktemp -d)
trap 'rm -rf "$parts"' EXIT
status=0
for c in "${picked[@]}"; do
  echo "== $c"
  (cd "$c" && pio test -e native --junit-output-path "$parts/$c.xml") 2>&1 |
    sed -E "s#^((test|src|include|lib)/)#$c/\\1#"
  [ "${PIPESTATUS[0]}" -eq 0 ] || status=1
done

python3 - "$report" "$parts" <<'PY'
import os, sys, xml.etree.ElementTree as ET
report, parts = sys.argv[1], sys.argv[2]
out = ET.Element("testsuites")
for name in sorted(os.listdir(parts)):
    comp = name[:-len(".xml")]
    for suite in ET.parse(os.path.join(parts, name)).getroot().iter("testsuite"):
        env, _, sname = suite.get("name", "").partition(":")
        if env != "native":
            continue
        suite.set("name", f"{comp}/{sname}")
        for case in suite.iter("testcase"):
            case.set("classname", f"{comp}/{sname}")
            if case.get("file"):
                case.set("file", f"{comp}/{case.get('file')}")
        out.append(suite)
ET.ElementTree(out).write(report, encoding="utf-8", xml_declaration=True)
PY
exit $status
