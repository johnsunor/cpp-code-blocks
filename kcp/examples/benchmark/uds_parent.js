'use strict';

const assert = require('assert');
const childProcess = require('child_process');
const fs = require('fs');
const path = require('path');

const logger = console;
const childFile = path.resolve(__dirname, 'uds_child.js');

function run(messageSize, timeoutSeconds) {
  let child = childProcess.fork(childFile,
    [messageSize, timeoutSeconds], {serialization: 'advanced'});
  const runTimeoutMs = (timeoutSeconds + 5) * 1000;
  const runTimeoutId = setTimeout(() => {
    const pid = child.pid;
    child.removeAllListeners();
    child.kill('SIGTERM');
    child = null;
    logger.warn(`child: ${pid} run timeout and will be killed by signal SIGTERM`);
  }, runTimeoutMs);

  child.on('error', err => {
    child.removeAllListeners();
    clearTimeout(runTimeoutId);
    err.name = 'WorkerError';
    err.pid = child.pid;
    logger.error(err);
  });

  child.once('exit', (code, signal) => {
    child.removeAllListeners();
    clearTimeout(runTimeoutId);
    child = null;
    logger.info(`child exit with code: ${code}, signal: ${signal}`);
  });

  child.on('message', data => child.send(data));
}

if (process.argv.length != 4) {
  logger.error(`Useage: node ${process.argv[1]} <messageSize> <timeoutSeconds>`);
  process.exit(0);
}

const messageSize = Number(process.argv[2]);
const timeoutSeconds = Number(process.argv[3]);

assert(messageSize > 0 && messageSize < 32768);
assert(timeoutSeconds > 0 && timeoutSeconds < 3600);
assert(fs.existsSync(childFile));

run(messageSize, timeoutSeconds);
