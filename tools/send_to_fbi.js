// Installs sk1.cia on the 3DS over Wi-Fi through FBI's "Remote Install > Receive URLs over
// the network". Usage (PC and 3DS on the same Wi-Fi):
//   1. On the 3DS open FBI > Remote Install > Receive URLs over the network. It shows its IP.
//   2. On the PC:  node tools/send_to_fbi.js <3ds-ip> [path/to/sk1.cia]
// This serves the CIA over HTTP from this PC and tells FBI where to download it from.
const http = require('http');
const net = require('net');
const os = require('os');
const fs = require('fs');
const path = require('path');

const ip = process.argv[2];
const cia = path.resolve(process.argv[3] || path.join(__dirname, '..', 'app', 'sk1.cia'));
if (!ip || !fs.existsSync(cia)) {
  console.error('usage: node send_to_fbi.js <3ds-ip> [sk1.cia]');
  process.exit(1);
}

// Pick this PC's LAN address (first non-internal IPv4).
const myIp = Object.values(os.networkInterfaces()).flat().find(a => a.family === 'IPv4' && !a.internal).address;
const size = fs.statSync(cia).size;

const server = http.createServer((req, res) => {
  console.log(`3DS is downloading ${path.basename(cia)} (${size} bytes)`);
  res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': size });
  fs.createReadStream(cia).pipe(res).on('finish', () => {
    console.log('done. FBI will now install it; press START to quit this script when it says finished.');
  });
});

server.listen(8080, () => {
  const url = `http://${myIp}:8080/${path.basename(cia)}`;
  console.log(`serving ${url}`);
  // FBI's protocol: 4-byte big-endian length, then the newline-separated URL list.
  const payload = Buffer.from(url + '\n', 'utf8');
  const header = Buffer.alloc(4);
  header.writeUInt32BE(payload.length, 0);
  let sent = false;
  const sock = net.connect(5000, ip, () => {
    sock.write(Buffer.concat([header, payload]));
    sock.end();
    sent = true;
    console.log(`sent the URL to the 3DS at ${ip}. Accept the install on the console.`);
  });
  sock.on('error', err => {
    if (sent) return;   // FBI resets the socket once it has the list; that's normal
    console.error(`could not reach FBI at ${ip}:5000 (${err.message}). Is "Receive URLs over the network" open?`);
    process.exit(1);
  });
});
