"""Known-answer and captured/held-out authentication regression tests."""
import unittest
from pathlib import Path
from danfoss_auth import tag, verify_frame, xtea_encrypt
from danfoss_secret import network_key

ROOT=Path(__file__).resolve().parents[1]
KEY=network_key()

class AuthenticationTests(unittest.TestCase):
    def test_xtea_known_answer(self):
        self.assertEqual(xtea_encrypt(bytes(8),bytes(16)).hex(),'dee9d4d8f7131ed9')

    def test_captured_17_degree_command(self):
        self.assertEqual(tag(bytes.fromhex('100a0e0d0000000006a4'),KEY).hex(),'904fe4b3')

    def test_archived_frames_and_negative_controls(self):
        frames=[bytes.fromhex(l) for l in (ROOT/'data/auth-validated-frames.txt').read_text().splitlines()]
        self.assertGreaterEqual(len(frames),113)
        for frame in frames:
            with self.subTest(frame=frame.hex()):
                self.assertTrue(verify_frame(frame,KEY))
                self.assertFalse(verify_frame(frame,bytes(16)))
                altered=bytearray(frame)
                altered[14]^=1
                altered[-1]^=1  # preserve valid outer RF checksum
                self.assertFalse(verify_frame(bytes(altered),KEY))

if __name__=='__main__':unittest.main()
