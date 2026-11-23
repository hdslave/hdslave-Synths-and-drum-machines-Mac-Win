#!/bin/sh
# Move every plug-in submodule to the newest commit on its tracked branch (.gitmodules), then commit the new pins here.
set -eu
cd "$(dirname "$0")/.."

git submodule update --init --remote
paths=$(git config --file .gitmodules --get-regexp '\.path$' | cut -d' ' -f2)

# shellcheck disable=SC2086 # the paths have no spaces
if git diff --quiet -- $paths; then
	echo "Every folder is already at its newest commit."
else
	summary=$(git diff --submodule=log -- $paths)
	printf '%s\n' "$summary"
	git add -- $paths
	git commit -q -m "Update to the newest commits" -m "$summary"
	echo "Committed. Run git push to publish."
fi
git submodule update --init --recursive
