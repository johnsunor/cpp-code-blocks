'use strict';

const assert = require('assert');
const crypto = require('crypto');
const dgram = require('dgram');

const logger = console;

function main() {
  if (process.argv.length != 6) {
    logger.error(`Usage: node ${process.argv[1]} <ip> <port> <blockSize> <timeoutSeonds>`);
    process.exit(0);
  }

  const getInt = (v, lv, uv) => {
    let r = parseInt(v);
    assert(r >= lv && r <= uv);
    return r;
  };

  const ip = process.argv[2];
  const port = getInt(process.argv[3], 1024, 65535);
  const blockSize = getInt(process.argv[4], 1, 60 * 1024);
  const timeoutSeonds = getInt(process.argv[5], 1, 5 * 60);
  const message = crypto.randomBytes(blockSize);

  const client = dgram.createSocket({
    type: 'udp4'
  });

  client.connect(port, ip, (error) => {
    if (error) {
      logger.error(`Error: ${error.message}`);
      process.exit(0);
    }
    client.send(message);
  });

  let totalBytesRead = 0;
  let totalBytesWrite = 0;
  let totalMessagesRead = 0;
  client.on('message', (msg, rinfo) => {
    totalBytesRead += rinfo.size;
    totalBytesWrite += rinfo.size;
    ++totalMessagesRead;
    client.send(msg);
  });

  client.on('error', (error) => {
    logger.error(`Error: ${error.message}`);
    process.exit(0);
  });

  const runTimeoutMs = timeoutSeonds * 1000;
  setTimeout(() => {
    logger.info(`${totalBytesRead} total bytes read`);
    logger.info(`${totalBytesWrite} total bytes write`);
    logger.info(`${totalMessagesRead} total messages read`);
    logger.info(`${totalBytesRead / Math.max(totalMessagesRead, 1)} average message size`);
    logger.info(`${totalBytesRead / (timeoutSeonds * 1024 * 1024)} MiB/s throughput`);
    process.exit(0);
   }, runTimeoutMs);
}

main();
