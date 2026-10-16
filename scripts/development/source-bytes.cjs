// Native sources have identical bundle bytes on Windows and Linux checkouts.
const path = require('path');
function normalizeSource(file, bytes) {
  if (path.basename(file) === 'CMakeLists.txt' || /\.(?:cpp|c|h|hpp|cmake|py)$/.test(file)) {
    // Latin-1 preserves every byte (including UTF-8/BOM) while replacing CRLF.
    return Buffer.from(bytes.toString('latin1').replace(/\r\n/g, '\n'), 'latin1');
  }
  return bytes;
}
module.exports = {normalizeSource};
