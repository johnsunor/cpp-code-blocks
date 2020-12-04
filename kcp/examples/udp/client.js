'use strict';

const assert = require('assert');
const crypto = require('crypto');
const dgram = require('dgram');

function main() {
  if (process.argv.length != 6) {
    console.error(`Usage: node ${process.argv[1]} <address> <port> <blockSize> <timeout>`);
    process.exit(0);
  }

  const getInt = (v, lv, uv) => {
    let r = parseInt(v);
    assert(r >= lv && r <= uv);
    return r;
  };

  const address = process.argv[2];
  const port = getInt(process.argv[3], 1024, 65535);
  const blockSize = getInt(process.argv[4], 1, 60 * 1024);
  const timeout = getInt(process.argv[5], 1, 5 * 60);
  const message = crypto.randomBytes(blockSize);

  const client = dgram.createSocket({
    type: 'udp4'
  });

  client.connect(port, address, (error) => {
    if (error) {
      console.error(`Error: ${error.message}`);
      process.exit(0);
    }
    client.send(message);
  });

  let totalReadBytes = 0;
  let totalWriteBytes = 0;
  let totalMessagesRead = 0;
  client.on('message', (msg, rinfo) => {
    totalReadBytes += rinfo.size;
    totalWriteBytes += rinfo.size;
    ++totalMessagesRead;
    client.send(msg);
  });

  client.on('error', (error) => {
    console.error(`Error: ${error.message}`);
    process.exit(0);
  });

  setTimeout(() => {
    console.log(`${totalReadBytes} total bytes read`);
    console.log(`${totalWriteBytes} total bytes write`);
    console.log(`${totalMessagesRead} total messages read`);
    console.log(`${totalReadBytes / Math.max(totalMessagesRead, 1)} average message size`);
    console.log(`${totalReadBytes / (timeout * 1024 * 1024)} MiB/s throughput`);
    process.exit(0);
   }, timeout * 1000);
}

main();
