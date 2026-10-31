#!/usr/bin/env python3
"""Tests for scripts/overlay_libraries.sh, the release scripts' check of a
directory of overlay libraries against the build's bin/overlay_libraries.txt.

    python3 profiles/mhfu/tests/overlay_libraries_tests.py
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "scripts" / "overlay_libraries.sh"


@unittest.skipIf(os.name == "nt" or shutil.which("bash") is None, "needs a POSIX bash")
class CheckOverlayLibrariesTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root)
        self.profile = self.root / "profile"
        self.libraries = self.root / "overlays"
        self.libraries.mkdir()
        self.manifest = self.root / "overlay_libraries.txt"

    def corpora(self, *prefixes):
        for prefix in prefixes:
            corpus = self.profile / "overlays" / prefix
            corpus.mkdir(parents=True)
            (corpus / "meta.txt").write_text("base=0x09D63100\n")

    def check(self):
        command = (
            f'source "{SCRIPT}"; check_overlay_libraries "$1" "$2" "$3" .so; status=$?; '
            'echo "result $status ${overlay_libraries:-} ${overlay_corpora:-}"'
        )
        result = subprocess.run(
            ["bash", "-c", command, "check", str(self.manifest), str(self.libraries), str(self.profile)],
            capture_output=True, text=True, check=True)
        status, *counts = result.stdout.split("result ", 1)[1].split()
        return int(status), counts, result.stderr

    def test_complete_packs_pass_and_count_libraries_and_corpora(self):
        self.corpora("ovlA_one", "ovlA_two", "ovlB_three")
        self.manifest.write_text("ovlpack_A.so ovlA_one\novlpack_A.so ovlA_two\novlpack_B.so ovlB_three\n")
        (self.libraries / "ovlpack_A.so").touch()
        (self.libraries / "ovlpack_B.so").touch()
        self.assertEqual(self.check(), (0, ["2", "3"], ""))

    def test_one_library_per_overlay_passes(self):
        self.corpora("ovlA_one")
        self.manifest.write_text("ovlA_one.so ovlA_one\n")
        (self.libraries / "ovlA_one.so").touch()
        self.assertEqual(self.check()[:2], (0, ["1", "1"]))

    def test_missing_library_fails(self):
        self.corpora("ovlA_one", "ovlB_two")
        self.manifest.write_text("ovlpack_A.so ovlA_one\novlpack_B.so ovlB_two\n")
        (self.libraries / "ovlpack_A.so").touch()
        status, _, error = self.check()
        self.assertEqual(status, 1)
        self.assertIn("ovlpack_B.so missing", error)

    def test_library_the_build_no_longer_makes_fails(self):
        self.corpora("ovlA_one")
        self.manifest.write_text("ovlpack_A.so ovlA_one\n")
        (self.libraries / "ovlpack_A.so").touch()
        (self.libraries / "ovlA_one.so").touch()
        status, _, error = self.check()
        self.assertEqual(status, 1)
        self.assertIn("2 libraries", error)

    def test_corpus_added_after_the_build_fails(self):
        self.corpora("ovlA_one", "ovlA_two")
        self.manifest.write_text("ovlpack_A.so ovlA_one\n")
        (self.libraries / "ovlpack_A.so").touch()
        status, _, error = self.check()
        self.assertEqual(status, 1)
        self.assertIn("lists 1 corpora", error)

    def test_missing_manifest_fails(self):
        self.corpora("ovlA_one")
        status, _, error = self.check()
        self.assertEqual(status, 1)
        self.assertIn("overlay_libraries.txt is missing", error)

    def test_no_corpora_fails(self):
        self.manifest.write_text("")
        status, _, error = self.check()
        self.assertEqual(status, 1)
        self.assertIn("no overlay corpora", error)


if __name__ == "__main__":
    unittest.main()
