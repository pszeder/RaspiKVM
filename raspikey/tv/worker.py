"""LG webOS control. This process never receives key values or pointer reports."""
import collections
import hashlib
import ipaddress
import json
import os
import pathlib
import select
import signal
import socket
import ssl
import sys
import threading
import time

import websocket


def magic_packet(mac):
    return b'\xff' * 6 + bytes.fromhex(mac.replace(':', '').replace('-', '')) * 16


def save_private(path, value):
    tmp = path.with_suffix('.next')
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'w') as f:
        json.dump(value, f)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


class Inbox:
    def __init__(self, stream):
        self.lock = threading.Lock()
        self.commands = collections.deque(maxlen=8)
        self.activity = False
        self.closed = False
        self.revision = 0
        self.stream = stream

    def read(self):
        try:
            for line in self.stream:
                if len(line) > 8192:
                    continue
                message = json.loads(line)
                with self.lock:
                    if message.get('action') == 'activity':
                        self.activity = True
                    else:
                        if message.get('action') in ('config', 'forget'):
                            self.revision += 1
                        self.commands.append(message)
        finally:
            self.closed = True

    def take(self):
        with self.lock:
            result = list(self.commands), self.activity
            self.commands.clear()
            self.activity = False
            return result


class TvWorker:
    def __init__(self, data_dir, inbox, emit):
        self.secret_path = pathlib.Path(data_dir) / 'tv-secret.json'
        self.inbox, self.emit = inbox, emit
        self.config = {'enabled': False}
        self.secret = {}
        try:
            self.secret = json.loads(self.secret_path.read_text())
        except (FileNotFoundError, ValueError):
            pass
        self.ws = None
        self.host = 'usb'
        self.select_pending = False
        self.allow_screen_wake = False
        self.pair_requested = False
        self.blocked = False
        self.next_connect = 0
        self.last_wake = -1000
        self.next_poll = 0
        self.request_id = 0
        self.status = {'worker': True, 'connected': False, 'paired': False, 'power': 'unknown',
                       'currentInput': '', 'inputs': [], 'error': '', 'wakeRequests': 0}
        self.last_status = None

    def publish(self):
        self.status['paired'] = bool(self.secret.get('key') and self.secret.get('host') == self.config.get('host'))
        text = json.dumps(self.status, separators=(',', ':'))
        if text != self.last_status:
            self.emit(text)
            self.last_status = text

    def close(self):
        if self.ws:
            try:
                self.ws.close(timeout=0)
            except Exception:
                pass
        self.ws = None
        self.status.update(connected=False, power='unknown', currentInput='')

    def observe(self, message):
        payload = message.get('payload', {})
        if message.get('id') == 'power':
            state = payload.get('state', '')
            self.status['power'] = 'on' if state == 'Active' else 'screen-off' if state == 'Screen Off' else 'off' if state else 'unknown'
        elif message.get('id') == 'app':
            app = payload.get('appId', '')
            prefix = 'com.webos.app.hdmi'
            self.status['currentInput'] = 'HDMI_' + app[len(prefix):] if app.startswith(prefix) else app

    def receive(self, wanted, seconds=3):
        deadline = time.monotonic() + seconds
        revision = self.inbox.revision
        while time.monotonic() < deadline and not self.inbox.closed:
            if revision != self.inbox.revision:
                raise InterruptedError('TV request cancelled by new settings.')
            self.ws.settimeout(min(0.4, max(0.01, deadline - time.monotonic())))
            try:
                message = json.loads(self.ws.recv())
            except websocket.WebSocketTimeoutException:
                continue
            self.observe(message)
            if message.get('id') == wanted:
                if message.get('type') == 'error':
                    raise RuntimeError('TV rejected the requested command; check pairing and permissions.')
                return message
        raise TimeoutError('TV did not answer in time.')

    def request(self, uri, payload=None, subscribe=None):
        self.request_id += 1
        name = subscribe or 'request_' + str(self.request_id)
        self.ws.send(json.dumps({'id': name, 'type': 'subscribe' if subscribe else 'request',
                                 'uri': 'ssap://' + uri, 'payload': payload or {}}))
        reply = self.receive(name)
        value = reply.get('payload', {})
        if value.get('returnValue') is False:
            raise RuntimeError('TV reported that the requested command failed.')
        return value

    def connect(self, pairing=False):
        host = self.config['host']
        if not pairing and not (self.secret.get('host') == host and self.secret.get('key')):
            self.status['error'] = 'Pair the TV before enabling automatic control.'
            return False
        self.ws = websocket.create_connection('wss://' + host + ':3001/', timeout=3,
                    sslopt={'cert_reqs': ssl.CERT_NONE, 'check_hostname': False},
                    http_proxy_host=None, http_no_proxy=[host], enable_multithread=False)
        fingerprint = hashlib.sha256(self.ws.sock.getpeercert(binary_form=True)).hexdigest()
        if not pairing and fingerprint != self.secret.get('fingerprint'):
            raise RuntimeError('TV certificate changed. Use Pair TV to approve it again.')
        self.ws.send(json.dumps({'id': 'hello', 'type': 'hello', 'payload': {}}))
        self.receive('hello')
        # webOS 6+ may require system information before registration.
        try:
            self.request('system/getSystemInfo')
        except RuntimeError:
            pass
        payload = {'forcePairing': False, 'pairingType': 'PROMPT', 'manifest': {
            'manifestVersion': 1, 'appVersion': '1.0',
            'permissions': ['CONTROL_INPUT_TV', 'READ_INPUT_DEVICE_LIST', 'READ_POWER_STATE',
                            'READ_RUNNING_APPS', 'CONTROL_POWER', 'CONTROL_TV_SCREEN']}}
        if self.secret.get('host') == host:
            payload['client-key'] = self.secret.get('key', '')
        self.ws.send(json.dumps({'type': 'register', 'id': 'register', 'payload': payload}))
        reply = self.receive('register')
        if reply.get('type') == 'response':
            if not pairing:
                self.blocked = True
                raise RuntimeError('TV needs approval again. Click Pair TV and accept its prompt.')
            self.status['error'] = 'Accept the RaspiKey connection prompt on the TV.'
            self.publish()
            reply = self.receive('register', 90)
        key = reply.get('payload', {}).get('client-key')
        if reply.get('type') != 'registered' or not key:
            raise RuntimeError('TV pairing did not complete.')
        self.secret = {'host': host, 'key': key, 'fingerprint': fingerprint}
        save_private(self.secret_path, self.secret)
        self.blocked = False
        self.status.update(connected=True, error='')
        inputs = self.request('tv/getExternalInputList').get('devices', [])
        self.status['inputs'] = [{'id': x['id'], 'label': x.get('label', x['id'])} for x in inputs if 'id' in x]
        try:
            self.request('com.webos.service.tvpower/power/getPowerState', subscribe='power')
        except RuntimeError:
            self.status['power'] = 'unknown'
        self.request('com.webos.applicationManager/getForegroundAppInfo', subscribe='app')
        self.next_poll = time.monotonic() + 10
        return True

    def wake(self):
        now = time.monotonic()
        if now - self.last_wake < 10:
            return
        mac = self.config.get('mac', '')
        if not mac:
            self.status['error'] = 'Set the TV network MAC address to enable wake.'
            return
        packet = magic_packet(mac)
        destinations = {self.config['host']}
        if self.config.get('wolDestination'):
            destinations.add(self.config['wolDestination'])
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            udp.settimeout(1)
            for dest in destinations:
                udp.sendto(packet, (dest, 9))
        self.last_wake = now
        self.next_connect = 0
        self.select_pending = True
        self.status['wakeRequests'] += 1
        self.status['error'] = 'Wake sent; waiting for TV. Network routing determines whether it arrives.'

    def select_input(self):
        target = self.config['bluetoothInput' if self.host == 'bluetooth' else 'usbInput']
        if self.status['power'] == 'screen-off':
            self.request('com.webos.service.tvpower/power/turnOnScreen')
        if self.status['currentInput'] != target:
            self.request('tv/switchInput', {'inputId': target})
            # Confirm with the TV, rather than claiming the requested source.
            self.request('com.webos.applicationManager/getForegroundAppInfo', subscribe='app')
        self.select_pending = False
        self.allow_screen_wake = False
        self.status['error'] = ''

    def handle(self, commands, activity):
        for command in commands:
            action = command.get('action')
            if action == 'config':
                self.close()
                self.config = command['settings']
                self.blocked = False
                self.select_pending = self.pair_requested = False
                self.allow_screen_wake = False
                self.next_connect = 0
            elif action == 'host':
                self.host = command['host']
                if self.config.get('enabled') and self.config.get('automaticSwitch'):
                    self.select_pending = True
                    activity |= command.get('explicit', False)
            elif action == 'pair':
                self.close()
                self.pair_requested = True
                self.next_connect = 0
            elif action == 'forget':
                self.close()
                self.secret = {}
                self.secret_path.unlink(missing_ok=True)
                self.select_pending = self.pair_requested = False
            elif action == 'sync':
                self.select_pending = True
                activity = True
            elif action == 'wake':
                activity = True
        if not self.config.get('enabled'):
            self.close()
            self.status['error'] = ''
            return
        if activity and (self.config.get('wakeOnInput') or any(c.get('action') in ('wake', 'sync') or c.get('explicit') for c in commands)):
            self.allow_screen_wake = True
            if self.status['power'] == 'on' and self.ws:
                pass
            elif self.status['power'] == 'screen-off' and self.ws:
                self.select_pending = True
            else:
                self.wake()

    def tick(self):
        if not self.config.get('enabled'):
            return
        now = time.monotonic()
        if not self.ws and not self.blocked and now >= self.next_connect:
            self.next_connect = now + 10
            pairing, self.pair_requested = self.pair_requested, False
            try:
                self.connect(pairing)
            except (OSError, websocket.WebSocketException, ValueError):
                self.close()
                self.status['error'] = 'TV unreachable. Control retries quietly; only input or an explicit switch sends wake.'
                return
        if not self.ws:
            return
        if self.select_pending and (self.status['power'] in ('on', 'unknown') or (self.status['power'] == 'screen-off' and self.allow_screen_wake)):
            self.select_input()
        if now >= self.next_poll:
            self.next_poll = now + 10
            try:
                self.request('com.webos.service.tvpower/power/getPowerState', subscribe='power')
            except RuntimeError:
                pass
        if select.select([self.ws.sock], [], [], 0)[0]:
            self.ws.settimeout(0.1)
            try:
                self.observe(json.loads(self.ws.recv()))
            except websocket.WebSocketTimeoutException:
                pass

    def run(self):
        while not self.inbox.closed:
            try:
                self.handle(*self.inbox.take())
                self.tick()
            except Exception as error:
                self.close()
                self.status['error'] = str(error) if isinstance(error, (RuntimeError, TimeoutError)) else 'TV connection failed; input forwarding continues.'
                self.next_connect = time.monotonic() + 10
            self.publish()
            time.sleep(0.1)
        self.close()


def main():
    os.nice(10)
    inbox = Inbox(sys.stdin)
    signal.signal(signal.SIGTERM, lambda *_: setattr(inbox, 'closed', True))
    threading.Thread(target=inbox.read, daemon=True).start()
    worker = TvWorker(sys.argv[1], inbox, lambda text: print(text, flush=True))
    worker.run()


if __name__ == '__main__':
    main()
