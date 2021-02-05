'use strict';

const logger = console;

const hasParent = !!process.send;
if (!hasParent) {
  logger.error(`${process.argv[1]} process can not run independently`);
  process.exit(0);
}

const messageSize = Number(process.argv[2]);
const timeoutSeonds = Number(process.argv[3]);

let message = '';
for (let i = 0; i < messageSize; ++i) {
  message += i % 128;
}

let totalBytesRead = 0;
let totalMessagesRead = 0;
process.on('message', data => {
  totalMessagesRead += 1;
  totalBytesRead += data.length;
  process.send(data);
  // logger.debug(`data: ${data}, length: ${data.length}`);
});
process.send(message);

const runTimeoutMs = timeoutSeonds * 1000;
setTimeout(() => {
  logger.info(`totalBytesRead: ${totalBytesRead}\n` +
    `totalMessagesRead: ${totalMessagesRead}\n` +
    `${totalBytesRead / (timeoutSeonds * 1024 * 1024)} MiB/s throughput`);
  process.exit(0);
}, runTimeoutMs);