#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

#include "../cmdline.h"
#include "../epollctl.hpp"

using namespace std;

std::mutex Mutex;
std::condition_variable Cond;
std::queue<EchoServer::Conn *> Queue;

void pushInQueue(EchoServer::Conn *conn) {
  {
    std::unique_lock<std::mutex> locker(Mutex);
    Queue.push(conn);
  }
  Cond.notify_one();
}

EchoServer::Conn *getQueueData() {
  std::unique_lock<std::mutex> locker(Mutex);
  Cond.wait(locker, []() -> bool { return Queue.size() > 0; });
  EchoServer::Conn *conn = Queue.front();
  Queue.pop();
  return conn;
}

void workerHandler() {
  while (true) {
    EchoServer::Conn *conn = getQueueData();  // 从共享的输入队列中获取请求，有锁
    conn->EnCode();
    EchoServer::ModToWriteEvent(conn);  // 修改成监听写事件，应答数据通过epoll的io线程来发送
  }
}

void ioHandler(const char *ip, int port) {
  int sockFd = EchoServer::CreateListenSocket(ip, port, true);
  if (sockFd < 0) {
    return;
  }
  epoll_event events[2048];
  int epollFd = epoll_create(1024);
  if (epollFd < 0) {
    perror("epoll_create failed");
    return;
  }
  EchoServer::Conn conn(sockFd, epollFd, true);
  EchoServer::SetNotBlock(sockFd);
  EchoServer::AddReadEvent(&conn);
  int msec = -1;
  while (true) {
    int num = epoll_wait(epollFd, events, 2048, msec);
    if (num < 0) {
      perror("epoll_wait failed");
      continue;
    }
    for (int i = 0; i < num; i++) {
      EchoServer::Conn *conn = (EchoServer::Conn *)events[i].data.ptr;
      if (conn->Fd() == sockFd) {
        EchoServer::LoopAccept(sockFd, 2048, [epollFd](int clientFd) {
          EchoServer::Conn *conn = new EchoServer::Conn(clientFd, epollFd, true);
          EchoServer::SetNotBlock(clientFd);
          EchoServer::AddReadEvent(conn, false, true);  // 监听可读事件，开启oneshot
        });
        continue;
      }
      auto releaseConn = [&conn]() {
        EchoServer::ClearEvent(conn);
        delete conn;
      };
      if (events[i].events & EPOLLIN) {  // 可读
        if (not conn->Read()) {          // 执行非阻塞read
          releaseConn();
          continue;
        }
        if (conn->OneMessage()) {
          pushInQueue(conn);  // 入共享输入队列，有锁
        } else {
          EchoServer::ReStartReadEvent(conn);  // 还没收到完整的请求，则重新启动可读事件的监听，携带oneshot选项
        }
      }
      if (events[i].events & EPOLLOUT) {  // 可写
        if (not conn->Write(false)) {     // 执行非阻塞write
          releaseConn();
          continue;
        }
        if (conn->FinishWrite()) {  // 完成了请求的应答写，则可以释放连接close
          releaseConn();
        }
      }
    }
  }
}

void usage() {
  cout << "./EpollReactorThreadPoolHSHA -ip 0.0.0.0 -port 1688" << endl;
  cout << "options:" << endl;
  cout << "    -h,--help      print usage" << endl;
  cout << "    -ip,--ip       listen ip" << endl;
  cout << "    -port,--port   listen port" << endl;
  cout << endl;
}

int main(int argc, char *argv[]) {
  string ip;
  int64_t port;
  CmdLine::StrOptRequired(&ip, "ip");
  CmdLine::Int64OptRequired(&port, "port");
  CmdLine::SetUsage(usage);
  CmdLine::Parse(argc, argv);
  for (int i = 0; i < EchoServer::GetNProcs(); i++) {  // 创建worker线程
    std::thread(workerHandler).detach();               // 这里需要调用detach，让创建的线程独立运行
  }
  for (int i = 0; i < EchoServer::GetNProcs(); i++) {   // 创建io线程
    std::thread(ioHandler, ip.c_str(), port).detach();  // 这里需要调用detach，让创建的线程独立运行
  }
  while (true) sleep(1);  // 主线程陷入死循环
  return 0;
}