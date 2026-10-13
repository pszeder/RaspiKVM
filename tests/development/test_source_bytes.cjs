const assert = require('assert');
const {normalizeSource} = require('../../scripts/development/source-bytes.cjs');
const crlf = Buffer.from('\ufeff// café\r\nint main(){}\r\n','utf8');
const lf = Buffer.from('\ufeff// café\nint main(){}\n','utf8');
for(const name of ['file.cpp','file.h','file.hpp','file.c','file.cmake','worker.py','CMakeLists.txt']) {
  assert(normalizeSource(name,crlf).equals(lf));
  assert(normalizeSource(name,lf).equals(lf));
}
const binary = Buffer.from([0,13,10,255,128]);
for(const name of ['vendor.whl','logo.png','font.woff']) assert(normalizeSource(name,binary).equals(binary));
console.log('Source normalization preserves UTF-8/BOM and binary bytes; CRLF/LF builds match');
