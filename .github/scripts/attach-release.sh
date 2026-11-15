#!/bin/bash
# Attach dist/*.zip to the GitHub release of the tag that started this run.
#
# Pushing a tag does not create a release, and a release is where the
# zips hang: GitHub has no separate list of files on a tag. Several jobs
# finish together (two Debian architectures, two Windows bitnesses) and
# each calls this script. The first one creates the release. The others
# find it already there and upload their own zip. --clobber replaces the
# same filename when the workflow is re-run for one tag.

set -euo pipefail

tag="${GITHUB_REF#refs/tags/}"
if [ -z "$tag" ] || [ "$tag" = "$GITHUB_REF" ]; then
	echo "not a tag checkout: ${GITHUB_REF:-<unset>}" >&2
	exit 1
fi

if ! gh release view "$tag" >/dev/null 2>&1; then
	# Another job may create the release between this check and the
	# create. A lost race is not a failed drop: the view below must
	# then succeed, and the upload still runs.
	gh release create "$tag" --title "$tag" --generate-notes || true
fi
gh release view "$tag" >/dev/null
gh release upload "$tag" dist/*.zip --clobber
