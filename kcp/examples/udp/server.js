'use strict';

const assert = require('assert');
const dgram = require('dgram');

const logger = console;

function main() {  
  if (process.argv.length != 4) {
    logger.error(`Usage: node ${process.argv[1]} <ip> <port>`);
    process.exit(0);
  }

  const getInt = (v, lv, uv) => {
    let r = parseInt(v);
    assert(r >= lv && r <= uv);
    return r;
  };

  const ip = process.argv[2];
  const port = getInt(process.argv[3], 1024, 65535);

  const server = dgram.createSocket({
    type: 'udp4'
  });

  server.on('error', (error) => {
    logger.info(`Error: ${error.message}`);
    process.exit(0);
  });

  server.on('message', (msg, rinfo) => {
    server.send(msg, rinfo.port, rinfo.address);
  });

  server.bind(port, ip);
}

main()
