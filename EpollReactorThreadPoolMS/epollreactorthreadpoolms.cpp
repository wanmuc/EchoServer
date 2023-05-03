#include <arpa/inet.h>
#include <assert.h>
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
#include <thread>

#include "../cmdline.h"
#include "../epollctl.hpp"

using namespace std;

int *EpollFd;
int EpollInitCnt = 0;
std::mutex Mutex;
std::condition_variable Cond;

void waitSubReactor() {
  std::unique_lock<std::mutex> locker(Mutex);
  Cond.wait(locker, []() -> bool { return EpollInitCnt >= EchoServer::GetNProcs(); });
  return;
}

void subReactorNotifyReady() {
  {
    std::unique_lock<std::mutex> locker(Mutex);
    EpollInitCnt++;
  }
  Cond.notify_all();
}

void addToSubHandler(int &index, int clientFd) {
  index++;
  index %= EchoServer::GetNProcs();
  EchoServer::Conn *conn = new EchoServer::Conn(clientFd, EpollFd[index], true);  // 轮询的方式添加到子Reactor线程中
  EchoServer::AddReadEvent(conn);                                                 // 监听可读事件
}

void mainHandler(const char *ip, int port) {
  waitSubReactor();  // 等待所有的从Reactor线程都启动完毕
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
  int index = 0;
  EchoServer::Conn conn(sockFd, epollFd, true);
  EchoServer::SetNotBlock(sockFd);
  EchoServer::AddReadEvent(&conn);
  while (true) {
    int num = epoll_wait(epollFd, events, 2048, -1);
    if (num < 0) {
      perror("epoll_wait failed");
      continue;
    }
    // 执行到这里就是有客户端连接到来
    EchoServer::LoopAccept(sockFd, 100000, [&index, epollFd](int clientFd) {
      EchoServer::SetNotBlock(clientFd);
      addToSubHandler(index, clientFd);  // 把连接迁移到subHandler线程中管理
    });
  }
}

void subHandler(int threadId) {
  epoll_event events[2048];
  int epollFd = epoll_create(1024);
  if (epollFd < 0) {
    perror("epoll_create failed");
    return;
  }
  EpollFd[threadId] = epollFd;
  subReactorNotifyReady();
  while (true) {
    int num = epoll_wait(epollFd, events, 2048, -1);
    if (num < 0) {
      perror("epoll_wait failed");
      continue;
    }
    for (int i = 0; i < num; i++) {
      EchoServer::Conn *conn = (EchoServer::Conn *)events[i].data.ptr;
      auto releaseConn = [&conn]() {
        EchoServer::ClearEvent(conn);
        delete conn;
      };
      if (events[i].events & EPOLLIN) {  // 可读
        if (not conn->Read()) {          // 执行非阻塞读
          releaseConn();
          continue;
        }
        if (conn->OneMessage()) {             // 判断是否要触发写事件
          EchoServer::ModToWriteEvent(conn);  // 修改成只监控可写事件
        }
      }
      if (events[i].events & EPOLLOUT) {  // 可写
        if (not conn->Write()) {          // 执行非阻塞写
          releaseConn();
          continue;
        }
        if (conn->FinishWrite()) {  // 完成了请求的应答写，则可以释放连接
          releaseConn();
        }
      }
    }
  }
}

void usage() {
  cout << "./EpollReactorThreadPoolMS -ip 0.0.0.0 -port 1688" << endl;
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
  EpollFd = new int[EchoServer::GetNProcs()];
  for (int i = 0; i < EchoServer::GetNProcs(); i++) {
    std::thread(subHandler, i).detach();  // 这里需要调用detach，让创建的线程独立运行
  }
  for (int i = 0; i < EchoServer::GetNProcs(); i++) {
    std::thread(mainHandler, ip.c_str(), port).detach();  // 这里需要调用detach，让创建的线程独立运行
  }
  while (true) sleep(1);  // 主线程陷入死循环
  return 0;
}