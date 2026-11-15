#!/bin/bash
# Build lrs with MinGW-w64 against msvcrt and pack a binary drop.
#
# GitHub Actions runs this from an MSYS2 shell on a Windows runner
# (see windows.yml). LRS_WIN_TAG is win32 or win64, and the shell is
# already the matching environment: MINGW32 (i686, msvcrt) or MINGW64
# (x86_64, msvcrt). UCRT64 is not used. The Universal CRT is part of
# Windows 10; Windows XP does not have it. msvcrt.dll is on Windows XP.
#
# The calls in this tree exist on Windows 2000. _fseeki64 does not: XP's
# msvcrt.dll has no such export, Vista's does. The msvcrt MinGW runtime
# supplies its own _fseeki64 out of _filelengthi64, fgetpos and fsetpos,
# which XP does have, so the source stays as it is. mingw-w64 itself
# stops at XP, so Windows 2000 is not a target of this drop.
#
# MSYS2 has been retiring the msvcrt environments. The check at the end
# is what keeps a retired toolchain from shipping a newer binary under
# the old name: the exe may import only kernel32.dll and msvcrt.dll,
# none of the symbols XP lacks, and its header may not ask for a Windows
# newer than XP.
#
# libgcc and winpthread are linked in. A second DLL next to lrs.exe
# would have to be unpacked beside it, and the drop is one file.
#
# On success the script leaves one zip under dist/:
#   lrs-<version>-win32-<sha>.zip
#   lrs-<version>-win64-<sha>.zip
# containing lrs.exe, file_id.diz, requirements.txt, both READMEs,
# LICENSE, and lrs.conf.example.

set -euo pipefail

# One banner per phase. The whole script is a single Actions step, so
# without these the configure and compile logs run together.
step() {
	echo
	echo "---- $* ----"
}

case "${LRS_WIN_TAG:-}" in
	win32|win64) ;;
	*)
		echo "LRS_WIN_TAG must be win32 or win64" >&2
		exit 1
		;;
esac

# A leftover cache from another toolchain would keep the old linker
# flags, and the XP check below would be looking at the wrong exe.
rm -rf build

# 0x0501 is Windows XP. The headers then refuse an API that did not
# exist yet. --subsystem,console:5.1 writes the same number into the
# exe so the XP loader agrees to map it. Later Windows load it too:
# the field is a minimum. binutils prints it as MajorOSystemVersion.
# win64 is the same stamp. There is no 64-bit Windows older than
# XP x64, and a 5.1 header still loads there.
#
# Ninja, not MinGW Makefiles. The mingw cmake package already pulls
# ninja in. The gcc package does not ship mingw32-make, and that
# generator then stops before it ever looks for gcc.
step "Configure (${LRS_WIN_TAG})"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_C_FLAGS="-D_WIN32_WINNT=0x0501 -DWINVER=0x0501" \
	-DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -Wl,--subsystem,console:5.1,--major-os-version,5,--minor-os-version,1"
step "Compile"
cmake --build build -j"$(nproc)"

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
# The zip name has no g: lrs-0.1.1-dev-win64-a9d086b.zip.
sha="$(git rev-parse --short=7 HEAD)"
when="$(TZ=UTC git log -1 --format=%cd --date=format-local:'%Y-%m-%d %H:%M')"
name="lrs-${version}-${LRS_WIN_TAG}-${sha}"

stage="dist/${name}"
rm -rf dist
mkdir -p "$stage"

# Stage a copy, then strip that copy. The build tree stays unstripped
# so a failed test is still debuggable in the Actions log.
step "Stage the stripped binary, docs, and the sample config"
install -m 755 build/lrs.exe "$stage/lrs.exe"
strip --strip-unneeded "$stage/lrs.exe"
install -m 644 README.md README_RU.md LICENSE lrs.conf.example "$stage/"

# objdump -p lists "DLL Name: foo.dll", one per imported library, and
# the operating-system version the loader checks before mapping the file.
step "Check that the exe still loads on Windows XP"
objdump -p "$stage/lrs.exe" > "$stage/.pe.txt"
bad=0
while IFS= read -r dll; do
	low="$(printf '%s' "$dll" | tr 'A-Z' 'a-z')"
	case "$low" in
		kernel32.dll|msvcrt.dll) ;;
		*)
			echo "unexpected import: $dll" >&2
			bad=1
			;;
	esac
done < <(sed -n 's/.*DLL Name: //p' "$stage/.pe.txt")
if ! grep -q 'DLL Name:' "$stage/.pe.txt"; then
	echo "objdump listed no DLL imports" >&2
	bad=1
fi
# Symbols XP's kernel32 and msvcrt do not export. A direct import loads
# on Vista and dies on XP with "entry point not found", which the
# version field does not catch. _fseeki64 is the one this tree calls:
# XP has no export, so it must come from the MinGW runtime, not the DLL.
# The *_s and strnlen names are the ones mingw-w64 has mapped onto
# msvcrt by mistake before. The kernel names are Vista.
while IFS= read -r sym; do
	case "$sym" in
		_fseeki64|_ftelli64|strnlen|*_s)
			echo "import $sym is not in Windows XP msvcrt" >&2
			bad=1
			;;
		InitializeCriticalSectionEx|GetTickCount64|InitializeConditionVariable|\
		SleepConditionVariableCS|WakeConditionVariable|WakeAllConditionVariable|\
		SetThreadStackGuarantee|FlsAlloc|FlsFree|FlsGetValue|FlsSetValue|\
		CreateThreadpool|GetFileInformationByHandleEx|WaitOnAddress|\
		WakeByAddressSingle|EventSetInformation|InitializeSRWLock|\
		AcquireSRWLockExclusive|ReleaseSRWLockExclusive|TryAcquireSRWLockExclusive)
			echo "import $sym is not in Windows XP kernel32" >&2
			bad=1
			;;
	esac
done < <(awk 'NF >= 4 && $1 ~ /^[0-9a-fA-F]+$/ && $3 ~ /^[0-9a-fA-F]+$/ && $NF ~ /^[_A-Za-z@?]/ { print $NF }' "$stage/.pe.txt")
# Major/minor pairs. Windows XP is 5.1. A higher pair does not load there,
# whatever the imports say. objdump spells the OS field
# MajorOSystemVersion, not MajorOperatingSystemVersion.
pe_field() {
	sed -n "s/^$1[[:space:]]*//p" "$stage/.pe.txt" | head -n 1
}
# Empty means objdump's wording changed and the check can no longer
# see the field, which is a failed drop.
check_ver() {
	label=$1
	maj=$2
	min=$3
	if [ -z "$maj" ] || [ -z "$min" ]; then
		echo "could not read the $label version from the exe" >&2
		bad=1
		return
	fi
	if [ "$maj" -gt 5 ] || { [ "$maj" -eq 5 ] && [ "$min" -gt 1 ]; }; then
		echo "$label version $maj.$min is newer than Windows XP" >&2
		bad=1
	fi
}
check_ver "OS" "$(pe_field MajorOSystemVersion)" "$(pe_field MinorOSystemVersion)"
check_ver "subsystem" "$(pe_field MajorSubsystemVersion)" "$(pe_field MinorSubsystemVersion)"
if [ "$bad" -ne 0 ]; then
	echo "---- objdump -p ----" >&2
	cat "$stage/.pe.txt" >&2
	exit 1
fi

step "Write requirements.txt"
{
	printf 'Runtime libraries for this build (Windows XP or later, %s).\n' "$LRS_WIN_TAG"
	printf 'Both are part of Windows. Nothing else has to be installed.\n'
	printf '\nlrs.exe:\n'
	sed -n 's/.*DLL Name: /  /p' "$stage/.pe.txt"
	printf '\nlibgcc and winpthread are linked into lrs.exe.\n'
	printf 'This is msvcrt, not the Universal CRT.\n'
} > "$stage/requirements.txt"
rm -f "$stage/.pe.txt"

# The compiler that produced the binary above, as "gcc 15.2.0". The last
# line of -v is "gcc version 15.2.0 (Rev...)" and names the real compiler.
cc_bin="${CC:-gcc}"
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
	printf 'lrs %s, windows xp+ (%s)\n' "$version" "$LRS_WIN_TAG"
	printf '\n'
	printf 'lrs, lastread sync between client and server\n'
	printf 'ftn messagebases\n'
	printf '\n'
	printf 'Binary: lrs.exe, msvcrt\n'
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
# exits 0. A missing DLL fails the job here, on the runner, which is a
# newer Windows and would also start a UCRT build. The import check
# above is what rejects that build.
step "Run lrs.exe --version"
"$stage/lrs.exe" --version
