
#ifndef KCP_CONSTANTS_H_
#define KCP_CONSTANTS_H_

#pragma GCC diagnostic ignored "-Wunused"

// internal linkage
// https://en.cppreference.com/w/cpp/language/storage_duration#Linkage

const int kMaxPacketSize = 1400;  // <= 1500 - 60(ip 20 + 40) - 8(udp)

const int kDefaultMTUSize = 1400;  // <= kMaxPacketSize

const int kSocketReceiveBuffer = 1000 * kMaxPacketSize;  // ~1MB

const int kSocketSendBuffer = 32 * kMaxPacketSize;  // ~32 KB

const int kNumPacketsPerRead = 16;  // <= 16 * kMaxPacketSize

const int kNumPacketsPerSend = 32;  // <= 32 * kMaxPacketSize

const int kServerMaxSynRetryTimes = 10;

const double kServerSynSentTimeout = 2.0;  // 2s

const double kServerRunPeriodicTaskInterval = 2.0;  // 2s

const int kServerSessionIdleSeconds = 120;  // 120s

const int kServerSessionTimeWaitSeconds = 120;  // 120s

const int kServerMaxWaitSessionClosedTryTimes = 100000;

const int kServerMaxGenSessionIdTryTimes = 20;

const int kClientMaxSynRetryTimes = 30;

const double kClientRunPeriodicTaskInterval = 2.0;  // 2s

const double kClientSynSentTimeout = 2.0;  // 2s

const double kClientSessionIdleSeconds = 120.0;  // 120s

const double kClientMaxReconnectDelay = 3.0;  // 3s

const double kClientPingInterval = 5.0;  // 5s

const int kMaxAncillaryDataLength = 1024;

#pragma GCC diagnostic error "-Wunused"

#endif
