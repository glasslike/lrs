#!/bin/sh
# Build lrs inside a NetBSD virtual machine and pack a binary drop.
#
# GitHub Actions checks the tree out on the Ubuntu runner, then boots a
# real NetBSD under QEMU and runs this script there (see netbsd.yml).
# The self-test is a real run of the binary, so the compiler and the
# test must be the same NetBSD system. Do not cross-compile.
#
# NetBSD's /bin/sh is not bash: this file is POSIX sh, and the build uses
# gmake because the system make is not GNU make. CMake's Unix Makefiles
# generator is pointed at gmake for the same reason.
#
# On success the script leaves one zip under dist/:
#   lrs-<version>-netbsd<major>-<arch>-<sha>.zip
# containing lrs, file_id.diz, requirements.txt, both READMEs, LICENSE,
# and lrs.conf.example. The zip is the Actions artifact and a release asset.

set -eu

# One banner per phase. The whole script is a single Actions step, so
# without these the pkg_add, configure, and compile logs run together.
step() {
	echo
	echo "---- $* ----"
}

step "Install NetBSD build packages"
# pkgin is listed first because the base image may only have pkg_add.
# zip packs the drop. cmake is the pkgsrc package; the base system does
# not ship it.
/usr/sbin/pkg_add -U pkgin git gmake cmake zip

# The host owns the synced tree; git in the VM runs as another user and
# would otherwise refuse it ("dubious ownership"). The -dev suffix in
# lrs --version comes from git during configure, so git must succeed.
step "Trust the synced git checkout"
git config --global --add safe.directory "$(pwd)"

step "Configure"
cmake -S . -B build -G "Unix Makefiles" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_MAKE_PROGRAM="$(command -v gmake)"
step "Compile"
cmake --build build -j"$(sysctl -n hw.ncpu)"

step "Self-test"
ctest --test-dir build --output-on-failure

# set(LRS_VERSION "...") in the top CMakeLists.txt. The quotes are part
# of the CMake string, not of the version.
version="$(sed -n 's/^set(LRS_VERSION "\([^"]*\)").*/\1/p' CMakeLists.txt)"
if [ -z "$version" ]; then
	echo "could not read LRS_VERSION from CMakeLists.txt" >&2
	exit 1
fi

# Seven hex digits, the same abbreviation a -dev version uses after -g.
# The zip name has no g: lrs-0.1.1-dev-netbsd11-amd64-a9d086b.zip.
sha="$(git rev-parse --short=7 HEAD)"
when="$(TZ=UTC git log -1 --format=%cd --date=format-local:'%Y-%m-%d %H:%M')"
# uname -r is "11.0"; the drop is named after the major release only.
# uname -m is the NetBSD architecture name, amd64 on x86_64.
release="$(uname -r | cut -d. -f1)"
arch="$(uname -m)"
name="lrs-${version}-netbsd${release}-${arch}-${sha}"

stage="dist/${name}"
rm -rf dist
mkdir -p "$stage"

# Stage a copy, then strip that copy. The build tree stays unstripped
# so a failed test is still debuggable in the Actions log.
step "Stage the stripped binary, docs, and the sample config"
install -m 755 build/lrs "$stage/lrs"
strip "$stage/lrs"
install -m 644 README.md README_RU.md LICENSE lrs.conf.example "$stage/"

# The dynamic libraries the binary was linked against, read from the ELF
# NEEDED entries rather than from the package list: this is what the
# target host actually has to provide.
step "Write requirements.txt"
{
	printf 'Runtime libraries for this build (NetBSD %s, %s).\n' "$release" "$arch"
	printf 'Install them on the target host before running.\n'
	printf '\nlrs:\n'
	readelf -d "$stage/lrs" | sed -n 's/.*(NEEDED).*\[\(.*\)\].*/  \1/p'
	printf '\nThe binary uses base system libraries only.\n'
} > "$stage/requirements.txt"

# The compiler that produced the binary above, as "gcc 12.4.0". The last
# line of -v is "gcc version 12.4.0 (NetBSD ...)" and names the real
# compiler; --version would say "cc", since cc is the gcc driver here.
cc_bin="${CC:-cc}"
cc_ident="$("$cc_bin" -v 2>&1 | sed -n 's/^\(.*\) version \([0-9][^ ]*\).*/\1 \2/p' | tail -n 1)"
if [ -z "$cc_ident" ]; then
	cc_ident="unknown"
fi

# Classic BBS descriptor: ASCII, at most 10 lines, 45 columns.
# Written at pack time so the version and commit stay in step with the zip.
# Built here with LF so the width check below counts real columns; the
# file itself is converted to CRLF afterwards, as a DIZ is a DOS text file.
step "Write file_id.diz"
diz="$stage/file_id.diz"
diz_lf="$stage/.file_id.diz.lf"
{
	printf 'lrs %s, netbsd %s (%s)\n' "$version" "$release" "$arch"
	printf '\n'
	printf 'lrs, lastread sync between client and server\n'
	printf 'ftn messagebases\n'
	printf '\n'
	printf 'Binary: lrs\n'
	printf 'Source %s %s UTC\n' "$sha" "$when"
	printf 'Compiler: %s\n' "$cc_ident"
} > "$diz_lf"

line_no=0
while IFS= read -r line || [ -n "$line" ]; do
	line_no=$((line_no + 1))
	# ${#line} is characters; the file is ASCII, so that is the column count.
	if [ "${#line}" -gt 45 ]; then
		echo "file_id.diz line $line_no is ${#line} columns (max 45): $line" >&2
		exit 1
	fi
done < "$diz_lf"
if [ "$line_no" -gt 10 ]; then
	echo "file_id.diz has $line_no lines (max 10)" >&2
	exit 1
fi

# awk, not sed: a "\r" in a sed replacement is not portable across seds.
awk '{ printf "%s\r\n", $0 }' "$diz_lf" > "$diz"
rm -f "$diz_lf"

# Paths inside the archive are relative to dist/, so unzip yields one directory.
step "Pack ${name}.zip"
( cd dist && zip -r -9 "${name}.zip" "$name" )

echo "packed dist/${name}.zip"
# The staged binary must actually start. --version prints the ident and
# exits 0. A missing library or a bad link fails the job here.
step "Run lrs --version"
"$stage/lrs" --version
