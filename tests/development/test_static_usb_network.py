import configparser
import hashlib
import importlib.util
import pathlib
import tempfile

assets = pathlib.Path(__file__).resolve().parents[2] / 'scripts/development'
spec = importlib.util.spec_from_file_location('setup', assets / 'configure-usb-management.py')
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)
instance = '03368fab-b75b-4537-97d6-fdc8bd891343'
mac = '02:77:e8:46:ad:02'
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    (root / 'etc/systemd/system').mkdir(parents=True)
    (root / 'etc/raspikey-dev-id').write_text(instance + '\n')
    (root / 'etc/systemd/system/raspikey-dev-usb.service').write_bytes((assets / 'raspikey-dev-usb.service').read_bytes())
    resolver = root / 'existing-resolver'
    resolver.write_text('original resolver\n')
    (root / 'etc/resolv.conf').symlink_to(resolver)
    (root / 'untouched-bluetooth').write_text('bond sentinel')
    def snapshot():
        return {str(p.relative_to(root)): (str(p.readlink()) if p.is_symlink() else hashlib.sha256(p.read_bytes()).hexdigest())
                for p in root.rglob('*') if p.is_symlink() or p.is_file()}
    before = snapshot()
    for bad_id, bad_mac in [(instance.replace('0336', '9999'), mac), (instance, '02:77:00:00:00:02')]:
        try:
            setup.install(bad_id, bad_mac, assets, root)
            raise AssertionError('wrong identity accepted')
        except ValueError:
            assert snapshot() == before, 'validation modified files'
    setup.install(instance, mac, assets, root)
    first = snapshot()
    setup.install(instance, mac, assets, root)
    assert snapshot() == first, 'repeat setup changed the result'
    assert resolver.read_text() == 'original resolver\n'
    assert (root / 'untouched-bluetooth').read_text() == 'bond sentinel'
    conf = configparser.ConfigParser()
    conf.read(root / 'etc/NetworkManager/conf.d/90-raspikvm-usb-management.conf')
    assert conf['device-raspikvm-usb']['managed'] == '0'
    assert not list((root / 'etc/systemd/system').glob('raspikvm-*.service')), 'additional service added'
    assert (root / 'etc/systemd/system/rpi-usb-gadget-ics.service').readlink() == pathlib.Path('/dev/null')
    assert 'ExecStartPost=' in (root / 'etc/systemd/system/raspikey-dev-usb.service.d/20-fixed-network.conf').read_text()
print('PASS: instance/MAC refusal before mutation, idempotence, resolver symlink backup, existing-service startup, no new service')
