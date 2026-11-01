"""Offline regressions for build dependency verification."""
import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('aster_bootstrap', Path(__file__).resolve().parents[1]/'tools/bootstrap.py')
bootstrap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bootstrap)


class BootstrapTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.target = root/'external'/'fixture.bin'
        self.url = 'https://example.invalid/pinned-dependency'
        self.expected = b'reviewed dependency bytes'
        pins = {'external/fixture.bin': {'path': 'external/fixture.bin', 'url': self.url,
                'sha256': hashlib.sha256(self.expected).hexdigest()}}
        for name, value in [('ROOT', root), ('PINNED', pins)]:
            patcher = patch.object(bootstrap, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_download_is_verified_before_it_is_written(self):
        with patch.object(bootstrap.urllib.request, 'urlopen', return_value=io.BytesIO(b'unexpected bytes')):
            with self.assertRaisesRegex(ValueError, 'hash changed'):
                bootstrap.fetch(self.url, self.target)
        self.assertFalse(self.target.exists())

    def test_valid_download_and_cached_copy_are_verified(self):
        with patch.object(bootstrap.urllib.request, 'urlopen', return_value=io.BytesIO(self.expected)) as download:
            bootstrap.fetch(self.url, self.target)
            self.assertEqual(self.target.read_bytes(), self.expected)
            bootstrap.fetch(self.url, self.target)
            self.assertEqual(download.call_count, 1)

    def test_corrupt_cache_is_preserved_and_rejected_without_a_download(self):
        self.target.parent.mkdir(parents=True)
        self.target.write_bytes(b'corrupt cache')
        with patch.object(bootstrap.urllib.request, 'urlopen') as download:
            with self.assertRaisesRegex(ValueError, 'hash changed'):
                bootstrap.fetch(self.url, self.target)
            download.assert_not_called()
        self.assertEqual(self.target.read_bytes(), b'corrupt cache')

    def test_unpinned_url_cannot_replace_a_reviewed_dependency(self):
        with patch.object(bootstrap.urllib.request, 'urlopen', return_value=io.BytesIO(self.expected)):
            with self.assertRaisesRegex(ValueError, 'not pinned'):
                bootstrap.fetch(self.url+'/other', self.target)
        self.assertFalse(self.target.exists())


if __name__ == '__main__':
    unittest.main()
