#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "installer"
FIRMWARE = INSTALLER / "firmware"
EXPECTED = {
    "bootloader.bin": 0x0000,
    "partitions.bin": 0x8000,
    "boot_app0.bin": 0xE000,
    "firmware.bin": 0x10000,
}
EXPECTED_SHA256 = {
    "bootloader.bin": "2a71d69b471e20c2bac7fb469f3c6a807b3ebee780e348e5889db0da849ca363",
    "partitions.bin": "bd0f7954aca2ef7d925ee21aaa1f3dc8822d1d6ce5cbbd26a135e5886bfff6ce",
    "boot_app0.bin": "f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0",
    "firmware.bin": "d38dc4df2224030ea14a250ecc23966ef5c7b0cc108ca572799e2239adb3bb94",
}


def validate_manifests() -> None:
    for name in ("manifest-update.json", "manifest-factory.json"):
        manifest = json.loads((INSTALLER / name).read_text(encoding="utf-8"))
        parts = manifest["builds"][0]["parts"]
        found = {Path(part["path"]).name: int(part["offset"]) for part in parts}
        if found != EXPECTED:
            raise RuntimeError(f"{name}: unexpected image paths or offsets: {found}")
        if manifest["builds"][0]["chipFamily"] != "ESP32-S3":
            raise RuntimeError(f"{name}: wrong chip family")


def main() -> None:
    validate_manifests()
    sums = []
    for name in EXPECTED:
        release_file = FIRMWARE / name
        if not release_file.is_file() or release_file.stat().st_size == 0:
            raise FileNotFoundError(f"Missing release artifact: {release_file}")
        digest = hashlib.sha256(release_file.read_bytes()).hexdigest()
        if digest != EXPECTED_SHA256[name]:
            raise RuntimeError(
                f"{name}: expected hardware-verified SHA-256 {EXPECTED_SHA256[name]}, got {digest}"
            )
        sums.append(f"{digest}  {name}")
    expected_sums = "\n".join(sums) + "\n"
    actual_sums = (FIRMWARE / "SHA256SUMS").read_text(encoding="ascii")
    if actual_sums != expected_sums:
        raise RuntimeError("installer/firmware/SHA256SUMS does not match the verified binaries")

    public_files = list((ROOT / "src").rglob("*")) + list((ROOT / "include").glob("*"))
    public_text = "\n".join(
        path.read_text(encoding="utf-8", errors="ignore")
        for path in public_files
        if path.is_file() and path.name != "local_config.h"
    )
    for forbidden in ("AC:27:6E:A4:1F:00", "192.168.0.199"):
        if forbidden in public_text:
            raise RuntimeError(f"Private value leaked into public source: {forbidden}")

    print("Hardware-verified release files validated")
    for line in sums:
        print(line)


if __name__ == "__main__":
    main()
