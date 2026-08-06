#!/usr/bin/env bash
set -euo pipefail
manifest=docs/builtins_v1_manifest.tsv
registry=src/polonio/runtime/builtins.cpp
mapfile -t registered < <(sed -n 's/.*env.set_local("\([a-z0-9_]*\)".*/\1/p' "$registry")
mapfile -t listed < <(awk -F '\t' 'NR>1 && $1!="" {print $1}' "$manifest")
for name in "${registered[@]}"; do grep -qx "$name" <(printf '%s\n' "${listed[@]}") || { echo "unclassified builtin: $name"; exit 1; }; done
for name in "${listed[@]}"; do grep -qx "$name" <(printf '%s\n' "${registered[@]}") || { echo "unknown manifest builtin: $name"; exit 1; }; done
awk -F '\t' 'NR>1 {if ($3 !~ /^[1-5]$/) {print "invalid layer: " $1; exit 1} if ($5 ~ /compatibility/ && $1==$2) {print "self alias: " $1; exit 1}}' "$manifest"
for alias in tostring htmlspecialchars status header; do canonical=$(awk -F '\t' -v n="$alias" '$1==n {print $2}' "$manifest"); grep -qx "$canonical" <(printf '%s\n' "${registered[@]}") || { echo "missing canonical target: $alias"; exit 1; }; done
echo "builtin manifest valid: ${#registered[@]} registrations"
