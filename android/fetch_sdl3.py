#!/usr/bin/env python3
"""Fetch the SDL3 release the runner pins (snesrecomp/runner/runner.cmake)
into android/SDL3. The APK builds libSDL3.so from it and compiles its
org.libsdl.app Java glue, so the C library and Java side always match."""
import hashlib, io, pathlib, re, shutil, sys, tarfile, urllib.request

here = pathlib.Path(__file__).resolve().parent
runner = (here.parent / "snesrecomp/runner/runner.cmake").read_text(encoding="utf-8")
version = re.search(r'SNESRECOMP_SDL3_FETCH_VERSION "([^"]+)"', runner).group(1)
sha = re.search(r'SNESRECOMP_SDL3_FETCH_SHA256\s*\n\s*"([0-9a-f]{64})"', runner).group(1)
dest = here / "SDL3"
stamp = dest / ".version"
if stamp.is_file() and stamp.read_text().strip() == version:
    print(f"SDL3 {version} already present")
    sys.exit(0)
url = f"https://github.com/libsdl-org/SDL/releases/download/release-{version}/SDL3-{version}.tar.gz"
print(f"Downloading {url}")
data = urllib.request.urlopen(url).read()
if hashlib.sha256(data).hexdigest() != sha:
    sys.exit("SDL3 tarball checksum mismatch")
shutil.rmtree(dest, ignore_errors=True)
with tarfile.open(fileobj=io.BytesIO(data)) as tar:
    prefix = f"SDL3-{version}/"
    for member in tar.getmembers():
        if not member.name.startswith(prefix) or member.issym() or member.islnk():
            continue
        member.name = member.name[len(prefix):]
        if not member.name or member.name.startswith(("/", "..")) or "/../" in member.name:
            continue
        tar.extract(member, dest)
stamp.write_text(version)
print(f"SDL3 {version} ready in {dest}")
