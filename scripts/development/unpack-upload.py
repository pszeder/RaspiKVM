#!/usr/bin/env python3
"""Snapshot and validate an authenticated developer's upload before root uses it."""
import hashlib
import io
import json
import os
import pathlib
import re
import shutil
import stat
import sys
import tarfile
import tempfile


def extract(data, destination):
    seen = set()
    total = 0
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        members = archive.getmembers()
        if len(members) > 4096:
            raise ValueError("Too many archive entries")
        for member in members:
            rel = pathlib.PurePosixPath(member.name)
            if rel.is_absolute() or ".." in rel.parts or not (member.isdir() or member.isfile()):
                raise ValueError("Unsafe archive entry")
            target = destination / str(rel)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            if str(rel) in seen:
                raise ValueError("Duplicate archive file")
            seen.add(str(rel))
            total += member.size
            if total > 256 * 1024 * 1024:
                raise ValueError("Archive is too large")
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(member) as source, target.open("xb") as output:
                shutil.copyfileobj(source, output)
    manifest = json.loads((destination / "manifest.json").read_text())
    if manifest.get("schema") != 1 or not re.fullmatch(r"[A-Za-z0-9._-]+", manifest["release"]):
        raise ValueError("Invalid manifest")
    if set(manifest["files"]) != seen - {"manifest.json"}:
        raise ValueError("Manifest does not cover all files")
    for name, expected in manifest["files"].items():
        rel = pathlib.PurePosixPath(name)
        if rel.is_absolute() or ".." in rel.parts or str(rel) != name:
            raise ValueError("Invalid manifest path")
        if hashlib.sha256((destination / name).read_bytes()).hexdigest() != expected:
            raise ValueError("Manifest checksum mismatch: " + name)


def snapshot(upload, checksum, uid, cache):
    if not re.fullmatch(r"[0-9a-f]{64}", checksum):
        raise ValueError("Invalid checksum")
    if upload.name != "bundle.tar.gz" or upload.parent.parent != pathlib.Path("/tmp") or not re.fullmatch(r"raspikey-upload\.[A-Za-z0-9]{8}", upload.parent.name):
        raise ValueError("Unexpected upload location")
    directory_fd = os.open(upload.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        directory = os.fstat(directory_fd)
        if directory.st_uid != uid or directory.st_mode & 0o077:
            raise ValueError("Upload directory must belong privately to the developer")
        file_fd = os.open(upload.name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=directory_fd)
        with os.fdopen(file_fd, "rb") as source:
            info = os.fstat(source.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_uid != uid or info.st_size > 64 * 1024 * 1024:
                raise ValueError("Invalid upload file")
            data = source.read(64 * 1024 * 1024 + 1)
    finally:
        os.close(directory_fd)
    if len(data) > 64 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != checksum:
        raise ValueError("Uploaded archive checksum mismatch")
    stage = pathlib.Path(tempfile.mkdtemp(prefix="upload.", dir=cache))
    try:
        extract(data, stage)
        return stage
    except BaseException:
        shutil.rmtree(stage)
        raise


if __name__ == "__main__":
    print(snapshot(pathlib.Path(sys.argv[1]), sys.argv[2], int(sys.argv[3]), pathlib.Path("/var/cache/raspikey-deploy")))
