#!/usr/bin/env python3
import hashlib
import importlib.util
import io
import json
import os
import pathlib
import shutil
import tarfile
import tempfile
import unittest
import uuid

root = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('unpack', root / 'scripts/development/unpack-upload.py')
unpack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(unpack)


class UploadTests(unittest.TestCase):
    def check(self, kind):
        directory = pathlib.Path('/tmp') / ('raspikey-upload.' + uuid.uuid4().hex[:8])
        directory.mkdir(mode=0o700)
        cache = pathlib.Path(tempfile.mkdtemp(prefix='raspikey-upload-test-'))
        try:
            directory.chmod(0o700)
            archive = directory / 'bundle.tar.gz'
            payload = b'fixture\n'
            manifest = {'schema':1,'release':'fixture-1','files':{'payload':hashlib.sha256(payload).hexdigest()}}
            if kind == 'manifest-checksum': manifest['files']['payload']='0'*64
            if kind == 'uncovered': manifest['files']={}
            with tarfile.open(archive,'w:gz') as output:
                for name, data in [('payload',payload),('manifest.json',json.dumps(manifest).encode())]:
                    info = tarfile.TarInfo(name)
                    if name == 'payload':
                        if kind == 'traversal': info.name='../escaped'
                        if kind == 'absolute': info.name='/tmp/escaped'
                        if kind == 'symlink': info.type=tarfile.SYMTYPE; info.linkname='/etc/passwd'
                        if kind == 'hardlink': info.type=tarfile.LNKTYPE; info.linkname='/etc/passwd'
                    if info.isfile(): info.size=len(data); output.addfile(info,io.BytesIO(data))
                    else: output.addfile(info)
                    if kind == 'duplicate' and name == 'payload': output.addfile(info,io.BytesIO(data))
            checksum=hashlib.sha256(archive.read_bytes()).hexdigest()
            if kind == 'bad-checksum': checksum='0'*64
            if kind == 'directory-mode': directory.chmod(0o755)
            if kind == 'file-symlink':
                archive.rename(directory/'original.tar.gz')
                archive.symlink_to(directory/'original.tar.gz')
            uid=os.getuid()+1 if kind=='wrong-owner' else os.getuid()
            if kind == 'valid':
                stage=unpack.snapshot(archive,checksum,uid,cache)
                self.assertEqual((stage/'payload').read_bytes(),payload)
                archive.write_bytes(b'changed after snapshot')
                self.assertEqual((stage/'payload').read_bytes(),payload)
            else:
                with self.assertRaises((ValueError,OSError,KeyError)):
                    unpack.snapshot(archive,checksum,uid,cache)
                self.assertEqual(list(cache.iterdir()),[])
        finally:
            shutil.rmtree(directory)
            shutil.rmtree(cache)

    def test_snapshot_and_rejections(self):
        for kind in ('valid','traversal','absolute','symlink','hardlink','duplicate',
                     'bad-checksum','manifest-checksum','uncovered','directory-mode','file-symlink','wrong-owner'):
            with self.subTest(kind=kind): self.check(kind)


if __name__ == '__main__': unittest.main()
