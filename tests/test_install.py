import os
from pathlib import Path
import subprocess
import tempfile
import unittest


INSTALL = Path(__file__).resolve().parents[1] / 'scripts/install'


class Installation(unittest.TestCase):
    def run_install(self, source, prefix, **kwargs):
        return subprocess.run([os.sys.executable, str(INSTALL), '--binary', str(source),
            '--prefix', str(prefix)], text=True, capture_output=True, **kwargs)

    def test_install_and_owned_upgrade(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            source.write_text('#!/bin/sh\nexit 0\n')
            source.chmod(0o755)
            self.assertEqual(self.run_install(source, root / 'prefix').returncode, 0)
            source.write_text('#!/bin/sh\nexit 3\n')
            self.assertEqual(self.run_install(source, root / 'prefix').returncode, 0)
            self.assertEqual((root / 'prefix/bin/ec').read_bytes(), source.read_bytes())
            self.assertTrue((root / 'prefix/share/licenses/easycommand/llama.cpp.txt').is_file())

    def test_refuses_foreign_destination_and_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            source.write_text('#!/bin/sh\nexit 0\n')
            source.chmod(0o755)
            target = root / 'prefix/bin/ec'
            target.parent.mkdir(parents=True)
            target.write_text('another tool')
            self.assertNotEqual(self.run_install(source, root / 'prefix').returncode, 0)
            self.assertEqual(target.read_text(), 'another tool')
            target.unlink()
            target.symlink_to(source)
            self.assertNotEqual(self.run_install(source, root / 'prefix').returncode, 0)
            self.assertTrue(target.is_symlink())

    def test_refuses_path_collision(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            foreign = root / 'other/ec'
            foreign.parent.mkdir()
            foreign.write_text('#!/bin/sh\nexit 0\n')
            foreign.chmod(0o755)
            env = dict(os.environ, PATH=str(foreign.parent))
            result = self.run_install(foreign, root / 'prefix', env=env)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((root / 'prefix/bin/ec').exists())

    def test_refuses_modified_owned_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            source.write_text('#!/bin/sh\nexit 0\n')
            source.chmod(0o755)
            self.assertEqual(self.run_install(source, root / 'prefix').returncode, 0)
            destination = root / 'prefix/bin/ec'
            destination.write_text('replaced independently')
            self.assertNotEqual(self.run_install(source, root / 'prefix').returncode, 0)
            self.assertEqual(destination.read_text(), 'replaced independently')
