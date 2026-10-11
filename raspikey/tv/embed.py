"""Embed the isolated TV worker and its pinned pure-Python WebSocket dependency."""
import base64
import hashlib
import pathlib
import sys

root = pathlib.Path(__file__).parent
wheel = (root / 'websocket_client-1.9.0-py3-none-any.whl').read_bytes()
assert hashlib.sha256(wheel).hexdigest() == 'af248a825037ef591efbf6ed20cc5faa03d3b47b9e5a2230a529eeee1c1fc3ef'
bootstrap = '''import base64, os, sys
vendor_fd = os.memfd_create('raspikey-tv-websocket')
os.write(vendor_fd, base64.b64decode(%r))
sys.path.insert(0, '/proc/self/fd/' + str(vendor_fd))
''' % base64.b64encode(wheel).decode('ascii')
source = bootstrap + (root / 'worker.py').read_text(encoding='utf-8')
header = '#pragma once\n#include <cstddef>\nextern const char TvWorkerSource[];\nextern const size_t TvWorkerSourceSize;\n'
cpp = '#include <cstddef>\nextern const char TvWorkerSource[] = R"TVWORKER(' + source + ')TVWORKER";\nextern const size_t TvWorkerSourceSize = sizeof(TvWorkerSource)-1;\n'
assert ')TVWORKER"' not in source
out = pathlib.Path(sys.argv[1])
if not out.exists() or out.read_text(encoding='utf-8') != header:
    out.write_text(header, encoding='utf-8')
cpp_out = out.with_suffix('.cpp')
if not cpp_out.exists() or cpp_out.read_text(encoding='utf-8') != cpp:
    cpp_out.write_text(cpp, encoding='utf-8')
