import ctypes
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "runner"))

from protocol import decode_message, encode_message
from shm_transport import CAPACITY, SLOT_PAYLOAD, SharedLayout


class ProtocolTests(unittest.TestCase):
    def test_codec_roundtrip(self):
        original = {"type": "SCRIPT_LOG", "message": "中文 = value\nnext", "path": "a/b.py"}
        self.assertEqual(decode_message(encode_message(original)), original)

    def test_shared_memory_layout(self):
        self.assertEqual(CAPACITY, 64)
        self.assertEqual(SLOT_PAYLOAD, 4092)
        self.assertGreater(ctypes.sizeof(SharedLayout), CAPACITY * SLOT_PAYLOAD * 2)


if __name__ == "__main__":
    unittest.main()
