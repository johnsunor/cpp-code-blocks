#include <stdio.h>
#include <boost/bind.hpp>

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>

#include "codec.h"
#include "dispatcher.h"
#include "ai_2048.h"
#include "ai_2048.pb.h"

using namespace muduo;
using namespace muduo::net;
using namespace ai_set;

typedef boost::shared_ptr<aiproto::CSAI2048Tile> CSAI2048TilePtr;
typedef boost::shared_ptr<aiproto::SCAI2048Move> SCAI2048MovePtr;
typedef boost::shared_ptr<ai_set::AI2048> AI2048Ptr;

class AI2048Server : boost::noncopyable {
 public:
  AI2048Server(EventLoop* loop, const InetAddress& listenAddr, int num_threads)
      : server_(loop, listenAddr, "AI2048Server"),
        num_threads_(num_threads),
        dispatcher_(
            boost::bind(&AI2048Server::OnUnknownMessage, this, _1, _2, _3)),
        codec_(boost::bind(&ProtobufDispatcher::onProtobufMessage, &dispatcher_,
                           _1, _2, _3)) {
    dispatcher_.registerMessageCallback<aiproto::CSAI2048Tile>(
        boost::bind(&AI2048Server::OnCSAI2048Tile, this, _1, _2, _3));
    dispatcher_.registerMessageCallback<aiproto::SCAI2048Move>(
        boost::bind(&AI2048Server::OnSCAI2048Move, this, _1, _2, _3));
    server_.setConnectionCallback(
        boost::bind(&AI2048Server::OnConnection, this, _1));
    server_.setMessageCallback(
        boost::bind(&ProtobufCodec::onMessage, &codec_, _1, _2, _3));
    server_.setThreadNum(num_threads);
  }

  void Start() { server_.start(); }

 private:
  void OnConnection(const TcpConnectionPtr& conn) {
    LOG_INFO << conn->localAddress().toIpPort() << " -> "
             << conn->peerAddress().toIpPort() << " is "
             << (conn->connected() ? "UP" : "DOWN");

    MutexLockGuard lock(mutex_);
    if (conn->connected()) {
      AI2048Ptr new_ai(new ai_set::AI2048());
      ai_map_[conn->name().c_str()] = new_ai;
    } else {
      ai_map_.erase(conn->name().c_str());
    }
    LOG_INFO << "NumConnected = " << ai_map_.size();
  }

  void OnUnknownMessage(const TcpConnectionPtr& conn, const MessagePtr& message,
                        Timestamp) {
    LOG_INFO << "onUnknownMessage: " << message->GetTypeName();
    conn->shutdown();
  }

  void OnCSAI2048Tile(const muduo::net::TcpConnectionPtr& conn,
                      const CSAI2048TilePtr& message, muduo::Timestamp) {
    // LOG_INFO << "onCSAI2048Title:\n" << message->GetTypeName() <<
    // message->DebugString();

    AI2048Ptr ai;
    {
      MutexLockGuard lock(mutex_);
      ai = ai_map_[conn->name().c_str()];
    }

    if (message->is_over() || message->data_size() != 16) {
      conn->shutdown();
      LOG_INFO << "GAME OVER ? : " << message->is_over()
               << "  SIZE: " << message->data_size();
      return;
    }

    int grid[4][4];
    for (int i = 0; i < message->data_size(); ++i) {
      grid[i / 4][i % 4] = message->data(i);
    }

    ai->AddComputerMove(grid);
    int best_move = ai->GetPlayerBestMove();

    aiproto::SCAI2048Move move;
    aiproto::MoveType type(aiproto::UP);
    if (ai->TryMove(best_move)) {
      type = (best_move == 0) ? aiproto::UP : (best_move == 1)
                                                  ? aiproto::RIGHT
                                                  : (best_move == 2)
                                                        ? aiproto::DOWN
                                                        : aiproto::LEFT;
    }
    move.set_type(type);
    codec_.send(conn, move);
  }

  void OnSCAI2048Move(const muduo::net::TcpConnectionPtr& conn,
                      const SCAI2048MovePtr& message, muduo::Timestamp) {
    LOG_INFO << "onAnswer: " << message->GetTypeName();
    conn->shutdown();
  }

  typedef std::map<const char*, AI2048Ptr> AIMap;

  TcpServer server_;
  int num_threads_;
  ProtobufDispatcher dispatcher_;
  ProtobufCodec codec_;
  MutexLock mutex_;
  AIMap ai_map_;
};

int main(int argc, char* argv[]) {
  LOG_INFO << "pid = " << getpid() << ", tid = " << CurrentThread::tid();
  int num_threads = 0;
  if (argc > 1) {
    num_threads = atoi(argv[1]);
  }
  srand(static_cast<uint32_t>(time(NULL)));

  EventLoop loop;
  InetAddress listenAddr(9981);
  AI2048Server server(&loop, listenAddr, num_threads);

  server.Start();

  loop.loop();
}
