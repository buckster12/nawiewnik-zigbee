"""Package a full ESP-IDF application; version MUST match compiled NAW_OTA_VERSION."""
import argparse
from pathlib import Path
import struct

def package(payload: bytes, version: int) -> bytes:
    if not payload or payload[0] != 0xE9 or len(payload) > 0x160000:
        raise ValueError("Not an ESP app or exceeds OTA slot")
    if not 0 < version < 0xFFFFFFFF:
        raise ValueError("Invalid version")
    return struct.pack('<IHHHHHIH32sIHI', 0x0BEEF11E, 0x100, 56, 0,
                       0x1234, 1, version, 2, b'Nawiewnik-H2 full image', len(payload)+62,
                       0, len(payload)) + payload

if __name__ == '__main__':
    p=argparse.ArgumentParser()
    p.add_argument('app',type=Path)
    p.add_argument('output',type=Path)
    p.add_argument('--version',type=lambda s:int(s,0),required=True)
    a=p.parse_args()
    a.output.write_bytes(package(a.app.read_bytes(),a.version))
