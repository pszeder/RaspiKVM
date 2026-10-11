import importlib.util
import io
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

root = pathlib.Path(__file__).resolve().parents[2] / 'raspikey' / 'tv'
sys.path.insert(0, str(root / 'websocket_client-1.9.0-py3-none-any.whl'))
spec = importlib.util.spec_from_file_location('tv_worker', root / 'worker.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FakeWorker(module.TvWorker):
    def __init__(self, directory):
        super().__init__(directory, module.Inbox(io.StringIO()), lambda _: None)
        self.calls = []
        self.wakes = 0

    def wake(self):
        self.wakes += 1
        self.select_pending = True

    def request(self, uri, payload=None, subscribe=None):
        self.calls.append((uri, payload))
        if 'getForegroundAppInfo' in uri:
            self.status['currentInput'] = self.config['usbInput' if self.host == 'usb' else 'bluetoothInput']
        return {}


class Tests(unittest.TestCase):
    def test_magic_packet(self):
        packet = module.magic_packet('C8:99:B2:A0:3C:B8')
        self.assertEqual(len(packet), 102)
        self.assertEqual(packet[:6], b'\xff' * 6)
        self.assertEqual(packet[6:], bytes.fromhex('c899b2a03cb8') * 16)

    def test_intent_and_absolute_source(self):
        with tempfile.TemporaryDirectory() as directory:
            worker = FakeWorker(directory)
            config = dict(enabled=True, host='192.168.1.192', mac='C8:99:B2:A0:3C:B8',
                          wakeOnInput=True, automaticSwitch=True, usbInput='HDMI_4', bluetoothInput='HDMI_3')
            worker.handle([dict(action='config', settings=config)], False)
            self.assertEqual(worker.wakes, 0)  # startup/configuration never wakes
            worker.handle([dict(action='host', host='usb', explicit=False)], False)
            self.assertEqual(worker.wakes, 0)  # Mac sleep fallback never wakes
            self.assertFalse(worker.allow_screen_wake)
            worker.handle([dict(action='host', host='bluetooth', explicit=True)], False)
            self.assertEqual(worker.wakes, 1)
            worker.status['power'] = 'on'
            worker.status['currentInput'] = 'HDMI_4'
            worker.select_input()
            self.assertIn(('tv/switchInput', {'inputId': 'HDMI_3'}), worker.calls)
            worker.host = 'usb'
            worker.status['currentInput'] = 'HDMI_3'
            worker.select_input()
            self.assertEqual(worker.calls[-2], ('tv/switchInput', {'inputId': 'HDMI_4'}))
            worker.calls.clear()
            worker.status['currentInput'] = 'HDMI_4'
            worker.select_input()
            self.assertEqual(worker.calls, [])  # absolute, idempotent switching
            worker.ws = object()
            worker.handle([], True)
            self.assertEqual(worker.wakes, 1)  # active TV needs no wake request
            self.assertFalse(worker.allow_screen_wake)
            worker.status['power'] = 'off'
            worker.handle([], True)
            self.assertEqual(worker.wakes, 2)
            worker.config['wakeOnInput'] = False
            worker.handle([], True)
            self.assertEqual(worker.wakes, 2)
            worker.handle([dict(action='wake')], False)
            self.assertEqual(worker.wakes, 3)  # explicit UI wake still works

    def test_wake_cooldown_and_packet_destinations(self):
        with tempfile.TemporaryDirectory() as directory:
            worker = module.TvWorker(directory, module.Inbox(io.StringIO()), lambda _: None)
            worker.config = dict(host='192.168.1.192', mac='C8:99:B2:A0:3C:B8', wolDestination='192.168.1.255')
            with mock.patch.object(module.socket, 'socket') as factory:
                udp = factory.return_value.__enter__.return_value
                worker.wake()
                worker.wake()
                self.assertEqual(worker.status['wakeRequests'], 1)
                self.assertEqual(udp.sendto.call_count, 2)
                self.assertEqual({call.args[1] for call in udp.sendto.call_args_list}, {('192.168.1.192', 9), ('192.168.1.255', 9)})
                self.assertTrue(all(len(call.args[0]) == 102 for call in udp.sendto.call_args_list))

    def test_input_coalescing(self):
        inbox = module.Inbox(io.StringIO('{"action":"activity"}\n' * 1000))
        inbox.read()
        commands, activity = inbox.take()
        self.assertEqual(commands, [])
        self.assertTrue(activity)
        self.assertFalse(inbox.take()[1])


if __name__ == '__main__':
    unittest.main()
