#include <stdio.h>
#include <boost/bind.hpp>

#include <stdlib.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/Atomic.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpClient.h>

#include "dispatcher.h"
#include "codec.h"
#include "ai_2048.pb.h"
#include "ai_2048.h"

using namespace muduo;
using namespace muduo::net;
using namespace aiproto;
using namespace ai_set;

typedef boost::shared_ptr<aiproto::CSAI2048Tile> CSAI2048TilePtr;
typedef boost::shared_ptr<aiproto::SCAI2048Move> SCAI2048MovePtr;

AtomicInt32 g_total_num_winer;

class AI2048Client : boost::noncopyable
{
 public:
  AI2048Client(EventLoop* loop,
              const InetAddress& serverAddr)
  : loop_(loop),
    client_(loop, serverAddr, "AI2048Client"),
    dispatcher_(boost::bind(&AI2048Client::OnUnknownMessage, this, _1, _2, _3)),
    codec_(boost::bind(&ProtobufDispatcher::onProtobufMessage, &dispatcher_, _1, _2, _3))
  {
    dispatcher_.registerMessageCallback<aiproto::CSAI2048Tile>(
        boost::bind(&AI2048Client::OnCSAI2048Tile, this, _1, _2, _3));
    dispatcher_.registerMessageCallback<aiproto::SCAI2048Move>(
        boost::bind(&AI2048Client::OnSCAI2048Move, this, _1, _2, _3));
    client_.setConnectionCallback(
        boost::bind(&AI2048Client::OnConnection, this, _1));
    client_.setMessageCallback(
        boost::bind(&ProtobufCodec::onMessage, &codec_, _1, _2, _3));
    InitDummy();
  }

  void Connect() {
    client_.connect();
  }

  void InitDummy() {
    dummy_.AddRandTile();
    dummy_.AddRandTile();
  }

 private:

  void OnConnection(const TcpConnectionPtr& conn) {
    LOG_INFO << conn->localAddress().toIpPort() << " -> "
        << conn->peerAddress().toIpPort() << " is "
        << (conn->connected() ? "UP" : "DOWN");

    if (conn->connected())
    {
        SendTileDataToServer(conn, false);
    }
    else 
    {
      //loop_->quit();
    }
  }

  void OnUnknownMessage(const TcpConnectionPtr& conn,
                        const MessagePtr& message,
                        Timestamp)
  {
    LOG_INFO << "OnUnknownMessage: " << message->GetTypeName();
  }

  void OnCSAI2048Tile(const muduo::net::TcpConnectionPtr& conn,
                const CSAI2048TilePtr& message,
                muduo::Timestamp)
  {
    LOG_INFO << "OnCSAI2048Tile:\n" << message->GetTypeName() << message->DebugString();
  }

#define DEBUG(msg) \
  do { \
    LOG_INFO << #msg << " :\n" << message->GetTypeName() << message->DebugString(); \
    SendTileDataToServer(conn, false); \
    return; \
  } while (0)

  void OnSCAI2048Move(const muduo::net::TcpConnectionPtr& conn,
               const SCAI2048MovePtr& message,
               muduo::Timestamp)
  {
    int type = (message->type() == aiproto::UP) ? 0 :
               (message->type() == aiproto::RIGHT) ? 1 :
               (message->type() == aiproto::DOWN) ? 2 :
               3;
    bool is_over = !dummy_.TryMove(type);
    dummy_.AddRandTile();
    //DEBUG("msg");
    SendTileDataToServer(conn, is_over);
  }

  void SendTileDataToServer(const TcpConnectionPtr& conn,
                            bool is_over) {
    //LOG_INFO << "MaxValue: " << (1<<dummy_.GetMaxValue());
    if (dummy_.IsWin()) {
       is_over = true; 
       g_total_num_winer.increment();
       LOG_INFO << "[---------- " << g_total_num_winer.get()
                << " ----------]";
    }

    CSAI2048Tile tile;
    int grid[4][4];
    dummy_.GetAllTiles(grid);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            tile.add_data(grid[i][j]);
        }
    }
    if (is_over) {
      LOG_INFO << "\n" << tile.DebugString();
    }
    tile.set_is_over(is_over);
    codec_.send(conn, tile);
  }

  EventLoop* loop_;
  TcpClient client_;
  ProtobufDispatcher dispatcher_;
  ProtobufCodec codec_;
  AI2048 dummy_;
};

int main(int argc, char* argv[]) {
  LOG_INFO << "pid = " << getpid();
  
  EventLoop loop;
  InetAddress serverAddr(9981);
  typedef boost::shared_ptr<AI2048Client> AI2048ClientPtr;

  const int num_clients = 1000;
  std::vector<AI2048ClientPtr> client_array;
  for (int i = 0; i < num_clients; ++i) {
    client_array.push_back(AI2048ClientPtr(new AI2048Client(&loop, serverAddr)));    
    client_array[i]->Connect();
  }

  loop.loop();
  LOG_INFO << "SUCC: " << g_total_num_winer.get();

  return 0;
}

