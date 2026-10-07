import importlib.util
from pathlib import Path
import struct
import unittest
R=Path(__file__).resolve().parents[1]
class PackageTest(unittest.TestCase):
    def test_standard_full_image_container(self):
        p=R/'tools/package_ota.py'
        self.assertTrue(p.exists(), 'OTA packager missing')
        spec=importlib.util.spec_from_file_location('packager',p)
        assert spec is not None and spec.loader is not None
        mod=importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
        payload=b'\xe9'+bytes(99)
        image=mod.package(payload,2)
        self.assertEqual(struct.unpack_from('<IHHHHHIH',image), (0x0beef11e,0x100,56,0,0x1234,1,2,2))
        self.assertEqual(struct.unpack_from('<IHI',image,52), (162,0,100))
        self.assertEqual(image[62:],payload)
if __name__=='__main__': unittest.main()
