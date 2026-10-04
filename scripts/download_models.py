#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# Download pretrained INRIA 3D Gaussian Splatting scenes.
#
# The official release is a single ~14.66 GB zip. This script reads its central
# directory over HTTP range requests and extracts only the requested
# `point_cloud.ply` files, so a single ~350-700 MB scene downloads in isolation.
#
# Adapted from the range-extraction helpers in nerfstudio-project/gsplat
# (examples/download_3dgs_paper_scenes.py), Apache-2.0.
#
# Usage:
#   ./scripts/download_models.py --scenes room counter --output-dir assets/models
#   ./scripts/download_models.py --list

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib
from urllib.request import Request, urlopen

MODELS_ZIP_URL = (
    "https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting"
    "/datasets/pretrained/models.zip"
)

MIPNERF360_OUTDOOR = ["bicycle", "flowers", "garden", "stump", "treehill"]
MIPNERF360_INDOOR = ["room", "counter", "kitchen", "bonsai"]
TANKS_AND_TEMPLES = ["truck", "train"]
DEEP_BLENDING = ["drjohnson", "playroom"]
ALL_SCENES = MIPNERF360_OUTDOOR + MIPNERF360_INDOOR + TANKS_AND_TEMPLES + DEEP_BLENDING


def http_read_range(url: str, start: int, length: int) -> bytes:
    req = Request(url)
    req.add_header("Range", f"bytes={start}-{start + length - 1}")
    with urlopen(req, timeout=120) as resp:
        return resp.read()


def http_get_size(url: str) -> int:
    req = Request(url, method="HEAD")
    with urlopen(req, timeout=60) as resp:
        return int(resp.headers["Content-Length"])


def read_central_directory(url: str, file_size: int):
    """Return [(filename, local_header_offset, compressed_size), ...]."""
    eocd_size = min(65536, file_size)
    tail = http_read_range(url, file_size - eocd_size, eocd_size)

    zip64_locator_sig = b"\x50\x4b\x06\x07"
    zip64_eocd_sig = b"\x50\x4b\x06\x06"
    eocd_sig = b"\x50\x4b\x05\x06"

    locator_pos = tail.rfind(zip64_locator_sig)
    if locator_pos != -1:
        zip64_eocd_offset = struct.unpack_from("<Q", tail, locator_pos + 8)[0]
        z64 = http_read_range(url, zip64_eocd_offset, 56)
        if z64[:4] != zip64_eocd_sig:
            raise ValueError("Invalid Zip64 EOCD record")
        cd_size = struct.unpack_from("<Q", z64, 40)[0]
        cd_offset = struct.unpack_from("<Q", z64, 48)[0]
    else:
        eocd_pos = tail.rfind(eocd_sig)
        if eocd_pos == -1:
            raise ValueError("Could not find End of Central Directory record")
        eocd = tail[eocd_pos:]
        cd_size = struct.unpack_from("<I", eocd, 12)[0]
        cd_offset = struct.unpack_from("<I", eocd, 16)[0]

    cd = http_read_range(url, cd_offset, cd_size)

    entries = []
    pos = 0
    cd_sig = b"\x50\x4b\x01\x02"
    while pos < len(cd) - 4:
        if cd[pos : pos + 4] != cd_sig:
            break
        compressed_size = struct.unpack_from("<I", cd, pos + 20)[0]
        fname_len = struct.unpack_from("<H", cd, pos + 28)[0]
        extra_len = struct.unpack_from("<H", cd, pos + 30)[0]
        comment_len = struct.unpack_from("<H", cd, pos + 32)[0]
        local_header_offset = struct.unpack_from("<I", cd, pos + 42)[0]
        fname = cd[pos + 46 : pos + 46 + fname_len].decode("utf-8")

        extra = cd[pos + 46 + fname_len : pos + 46 + fname_len + extra_len]
        if compressed_size == 0xFFFFFFFF or local_header_offset == 0xFFFFFFFF:
            epos = 0
            while epos < len(extra) - 4:
                tag, sz = struct.unpack_from("<HH", extra, epos)
                if tag == 0x0001:
                    z64 = extra[epos + 4 : epos + 4 + sz]
                    off = 0
                    uncomp_orig = struct.unpack_from("<I", cd, pos + 24)[0]
                    if uncomp_orig == 0xFFFFFFFF:
                        off += 8
                    if compressed_size == 0xFFFFFFFF:
                        compressed_size = struct.unpack_from("<Q", z64, off)[0]
                        off += 8
                    if local_header_offset == 0xFFFFFFFF:
                        local_header_offset = struct.unpack_from("<Q", z64, off)[0]
                    break
                epos += 4 + sz

        entries.append((fname, local_header_offset, compressed_size))
        pos += 46 + fname_len + extra_len + comment_len

    return entries


def extract_file(url: str, local_header_offset: int, compressed_size: int) -> bytes:
    header = http_read_range(url, local_header_offset, 30)
    fname_len = struct.unpack_from("<H", header, 26)[0]
    extra_len = struct.unpack_from("<H", header, 28)[0]
    compression = struct.unpack_from("<H", header, 8)[0]

    data_offset = local_header_offset + 30 + fname_len + extra_len
    data = http_read_range(url, data_offset, compressed_size)

    if compression == 0:
        return data
    if compression == 8:
        return zlib.decompress(data, -15)
    raise ValueError(f"Unsupported compression method: {compression}")


def matches_scene(scene: str, fname: str) -> bool:
    parts = fname.strip("/").split("/")
    return (
        scene in parts
        and parts[-1] == "point_cloud.ply"
        and "iteration_30000" in parts
    )


def matches_cameras(scene: str, fname: str) -> bool:
    parts = fname.strip("/").split("/")
    return scene in parts and parts[-1] == "cameras.json"


def main() -> None:
    p = argparse.ArgumentParser(description="Download pretrained INRIA 3DGS scenes.")
    p.add_argument("--scenes", nargs="+")
    p.add_argument("--output-dir", default="assets/models")
    p.add_argument("--list", action="store_true")
    args = p.parse_args()

    if args.list:
        print("Mip-NeRF 360 outdoor:", " ".join(MIPNERF360_OUTDOOR))
        print("Mip-NeRF 360 indoor :", " ".join(MIPNERF360_INDOOR))
        print("Tanks & Temples     :", " ".join(TANKS_AND_TEMPLES))
        print("Deep Blending       :", " ".join(DEEP_BLENDING))
        return

    scenes = args.scenes if args.scenes else ["room"]
    unknown = [s for s in scenes if s not in ALL_SCENES]
    if unknown:
        sys.exit(f"Unknown scene(s) {unknown}. Available: {' '.join(ALL_SCENES)}")

    print(f"Reading central directory of {MODELS_ZIP_URL} ...")
    file_size = http_get_size(MODELS_ZIP_URL)
    entries = read_central_directory(MODELS_ZIP_URL, file_size)
    print(f"  zip size: {file_size / 1e9:.2f} GB, {len(entries)} entries")

    for scene in scenes:
        matches = [e for e in entries if matches_scene(scene, e[0])]
        if not matches:
            print(f"  [skip] {scene}: point_cloud.ply not found", file=sys.stderr)
            continue
        fname, offset, compressed_size = max(matches, key=lambda e: len(e[0]))

        out_dir = os.path.join(args.output_dir, scene)
        os.makedirs(out_dir, exist_ok=True)
        dest = os.path.join(out_dir, "point_cloud.ply")

        print(f"  {scene}: extracting {fname} ({compressed_size / 1e6:.1f} MB compressed) ...")
        data = extract_file(MODELS_ZIP_URL, offset, compressed_size)
        with open(dest, "wb") as f:
            f.write(data)
        print(f"    -> {dest} ({len(data) / 1e6:.1f} MB)")

        # Also grab cameras.json (training poses) for a good initial view.
        cam_matches = [e for e in entries if matches_cameras(scene, e[0])]
        if cam_matches:
            cf, coff, csize = max(cam_matches, key=lambda e: len(e[0]))
            cam_dest = os.path.join(out_dir, "cameras.json")
            with open(cam_dest, "wb") as f:
                f.write(extract_file(MODELS_ZIP_URL, coff, csize))
            print(f"    -> {cam_dest}")


if __name__ == "__main__":
    main()
