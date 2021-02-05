
#include <stdio.h>

#include <algorithm>
#include <sstream>
#include <vector>

#include <muduo/base/Logging.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include <kcp_constants.h>
#include "kcp_callbacks.h"
#include "kcp_client.h"
#include "kcp_session.h"
#include "log_util.h"
#include "urandom.h"

uint32_t g_msg_id = 0;
uint32_t g_limit_count = 0;
std::string g_last_msg;

uint32_t RandBetween(uint32_t a, uint32_t b) {
  ASSERT_EXIT(a <= b);

  uint64_t range = static_cast<uint64_t>(b) - a + 1;

  uint32_t num;
  URandom::GetInstance().RandBytes(&num, sizeof(num));

  uint32_t result = a + static_cast<uint32_t>(num % range);
  return result;
}

std::vector<std::string> SplitInto(const std::string& msg, size_t n) {
  ASSERT_EXIT(msg.size() >= n && n >= 1);

  std::vector<std::string> vs;
  std::vector<size_t> vi;

  vs.reserve(n);
  vi.reserve(msg.size() + 1);

  for (size_t i = 0; i < msg.size(); ++i) {
    vi.push_back(i);
  }
  for (size_t i = 1; i < n; ++i) {
    auto t = RandBetween(static_cast<uint32_t>(i),
                         static_cast<uint32_t>(msg.size() - 1));
    std::swap(vi[i], vi[t]);
  }

  std::sort(vi.begin(), vi.begin() + n);

  vi[n] = msg.size();

  size_t size = 0;
  std::ostringstream os;
  for (size_t i = 0; i < n; ++i) {
    size_t new_size = vi[i + 1] - vi[i];
    size += new_size;
    vs.emplace_back(msg.substr(vi[i], new_size));
    os << "[" << vi[i] << ", " << vi[i] + new_size << ") ";
  }
  os << "[0, " << msg.size() << ")";
  // LOG_WARN << os.str();

  ASSERT_EXIT(size == msg.size());

  return vs;
}

void SendNextMsg(const KCPSessionPtr& session) {
  if (g_msg_id >= g_limit_count) {
    session->CloseAfterMs(50, true);
    return;
  }

  ++g_msg_id;

  // clear
  {
    std::string msg;
    g_last_msg.swap(msg);
  }

  uint32_t msg_size = RandBetween(10, 10 * kMaxPacketSize);

  g_last_msg.reserve(msg_size);
  for (uint32_t i = 0; i < msg_size; ++i) {
    g_last_msg.push_back(static_cast<char>(RandBetween(32, 126)));
  }

  std::vector<std::string> msgs = SplitInto(g_last_msg, 10);
  for (const auto& m : msgs) {
    session->Write(m.data(), m.size());
    // 10ms
    ::usleep(10000);
  }
}

void OnConnection(const KCPSessionPtr& session, bool connected) {
  if (connected) {
    SendNextMsg(session);
  } else {
    session->loop()->quit();
  }
}

void OnMessage(const KCPSessionPtr& session, muduo::net::Buffer* buf) {
  if (buf->readableBytes() < g_last_msg.size()) {
    return;
  }

  if (buf->readableBytes() > g_last_msg.size()) {
    LOG_FATAL << "unexpected msg size: " << buf->readableBytes()
              << ", expected: " << g_last_msg.size();
  }

  std::string received_msg = buf->retrieveAllAsString();
  if (received_msg != g_last_msg) {
    LOG_FATAL << "unexpected msg: " << buf->retrieveAllAsString()
              << ", expected: " << g_last_msg;
  }

  LOG_WARN << "g_msg_id: " << g_msg_id << " msg_size: " << g_last_msg.size()
           << " passed";

  SendNextMsg(session);
}

int main(int argc, char* argv[]) {
  if (argc != 4) {
    fprintf(stderr, "Usage: %s <host_ip> <port> <count>\n", argv[0]);
  } else {
    LOG_INFO << "pid = " << getpid()
             << ", tid = " << muduo::CurrentThread::tid();
    muduo::Logger::setLogLevel(muduo::Logger::WARN);

    const char* ip = argv[1];
    uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
    uint32_t count = static_cast<uint32_t>(atoi(argv[3]));

    ASSERT_EXIT(port >= 1024);
    ASSERT_EXIT(count >= 1);

    g_msg_id = 0;
    g_limit_count = count;

    muduo::net::EventLoop loop;
    muduo::net::InetAddress server_address(ip, port);

    KCPClient client(&loop);
    client.set_connection_callback(OnConnection);
    client.set_message_callback(OnMessage);

    client.ConnectOrDie(server_address);

    loop.loop();

    LOG_WARN << "all passed";
  }
}
