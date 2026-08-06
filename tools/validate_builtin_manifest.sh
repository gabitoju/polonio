#!/usr/bin/env bash
set -euo pipefail
manifest=docs/builtins_v1_manifest.tsv
registry=src/polonio/runtime/builtins.cpp
mapfile -t registered < <(sed -n 's/.*env.set_local("\([a-z0-9_]*\)".*/\1/p' "$registry")
mapfile -t listed < <(awk -F '\t' 'NR>1 && $1!="" {print $1}' "$manifest")
for name in "${registered[@]}"; do grep -qx "$name" <(printf '%s\n' "${listed[@]}") || { echo "unclassified builtin: $name"; exit 1; }; done
for name in "${listed[@]}"; do grep -qx "$name" <(printf '%s\n' "${registered[@]}") || { echo "unknown manifest builtin: $name"; exit 1; }; done
duplicates=$(printf '%s\n' "${listed[@]}" | sort | uniq -d)
[[ -z "$duplicates" ]] || { echo "duplicate manifest builtin: $duplicates"; exit 1; }
awk -F '\t' '
  NR > 1 {
    if ($3 !~ /^[1-5]$/) { print "invalid layer: " $1; exit 1 }
    if ($5 ~ /compatibility/ && $1 == $2) { print "self alias: " $1; exit 1 }
    if ($3 == 1 && $4 != "core") { print "core builtin has non-core category: " $1; exit 1 }
    if ($3 == 2 && $4 !~ /^(debug|string|escaping|collection|object|math|predicate|date|url)$/) { print "standard builtin has runtime category: " $1; exit 1 }
    if ($3 == 3 && $4 != "template-output") { print "template builtin has non-template category: " $1; exit 1 }
    if ($3 == 4 && $4 !~ /^(response|request|session|security|upload|response-file|mail)$/) { print "web builtin has non-web category: " $1; exit 1 }
    if ($3 == 5 && $4 !~ /^(storage|sqlite)$/) { print "data builtin has non-data category: " $1; exit 1 }
  }
' "$manifest"
for alias in tostring htmlspecialchars status header; do canonical=$(awk -F '\t' -v n="$alias" '$1==n {print $2}' "$manifest"); grep -qx "$canonical" <(printf '%s\n' "${registered[@]}") || { echo "missing canonical target: $alias"; exit 1; }; done
grep -q 'Layer 4 APIs require their documented Web Runtime context' docs/STANDARD_LIBRARY_V1.md || { echo "missing Web Runtime availability documentation"; exit 1; }
grep -q 'Layer 5 APIs' docs/STANDARD_LIBRARY_V1.md || { echo "missing Data Runtime availability documentation"; exit 1; }
for name in "${listed[@]}"; do
  entry=$(grep -F "\`$name\`" docs/STANDARD_LIBRARY_V1.md || true)
  [[ -n "$entry" ]] || { echo "builtin missing profile documentation: $name"; exit 1; }
  layer=$(awk -F '\t' -v n="$name" '$1==n {print $3}' "$manifest")
  case "$layer" in
    1) [[ "$entry" == *'Language Core; Reference Standard Library; Template Runtime; Web Runtime; Data Runtime; Reference Distribution'* ]] || { echo "core builtin lacks Language Core profile: $name"; exit 1; } ;;
    3) [[ "$entry" == *'Template Runtime; Reference Distribution'* ]] || { echo "template builtin lacks Template Runtime profile: $name"; exit 1; } ;;
    4) [[ "$entry" == *'Web Runtime; Reference Distribution'* ]] || { echo "web builtin lacks Web Runtime profile: $name"; exit 1; } ;;
    5) [[ "$entry" == *'Data Runtime; Reference Distribution'* ]] || { echo "data builtin lacks Data Runtime profile: $name"; exit 1; } ;;
  esac
done
echo "builtin manifest valid: ${#registered[@]} registrations"
