#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "../cmdline.h"
#include "../common.hpp"

using namespace std;

typedef struct Stat {
  int sum{0};
  int success{0};
  int failure{0};
  int spendms{0};
} Stat;

std::mutex Mutex;
Stat FinalStat;

bool getConnection(sockaddr_in &addr, int &sockFd) {
  sockFd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockFd < 0) {
    perror("socket failed");
    return false;
  }
  int ret = connect(sockFd, (sockaddr *)&addr, sizeof(addr));
  if (ret < 0) {
    perror("connect failed");
    close(sockFd);
    return false;
  }
  struct linger lin;
  lin.l_onoff = 1;
  lin.l_linger = 0;
  // 设置调用close关闭tcp连接时，直接发送RST包，tcp连接直接复位，进入到closed状态。
  if (0 == setsockopt(sockFd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin))) {
    return true;
  }
  perror("setsockopt failed");
  close(sockFd);
  return false;
}

int64_t getSpendMs(timeval begin, timeval end) {
  end.tv_sec -= begin.tv_sec;
  end.tv_usec -= begin.tv_usec;
  if (end.tv_usec <= 0) {
    end.tv_sec -= 1;
    end.tv_usec += 1000000;
  }
  return end.tv_sec * 1000 + end.tv_usec / 1000;  //计算运行的时间，单位ms
}

void client(int theadId, Stat *curStat, int port, int size, int concurrency) {
  int sum = 0;
  int success = 0;
  int failure = 0;
  int spendms = 0;
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(std::string("127.0.0." + std::to_string(theadId + 1)).c_str());
  std::string message(size - 4, 'a');  // 去掉4个字节的协议头
  concurrency /= 10;                   // 每个线程的并发数
  int *sockFd = new int[concurrency];
  timeval end;
  timeval begin;
  gettimeofday(&begin, NULL);
  for (int i = 0; i < concurrency; i++) {
    if (not getConnection(addr, sockFd[i])) {
      sockFd[i] = 0;
      failure++;
    }
  }
  auto failureDeal = [&sockFd, &failure](int i) {
    close(sockFd[i]);
    sockFd[i] = 0;
    failure++;
  };
  std::cout << "threadId[" << theadId << "] finish connection" << std::endl;
  for (int i = 0; i < concurrency; i++) {
    if (sockFd[i]) {
      if (not EchoServer::SendMsg(sockFd[i], message)) {
        failureDeal(i);
      }
    }
  }
  std::cout << "threadId[" << theadId << "] finish send message" << std::endl;
  for (int i = 0; i < concurrency; i++) {
    if (sockFd[i]) {
      std::string respMessage;
      if (not EchoServer::RecvMsg(sockFd[i], respMessage)) {
        failureDeal(i);
        continue;
      }
      if (respMessage != message) {
        failureDeal(i);
        continue;
      }
      close(sockFd[i]);
      success++;
    }
  }
  delete[] sockFd;
  std::cout << "threadId[" << theadId << "] finish recv message" << std::endl;
  sum = success + failure;
  gettimeofday(&end, NULL);
  spendms = getSpendMs(begin, end);
  std::lock_guard<std::mutex> guard(Mutex);
  curStat->sum += sum;
  curStat->success += success;
  curStat->failure += failure;
  curStat->spendms += spendms;
}

void UpdateFinalStat(Stat stat) {
  FinalStat.sum += stat.sum;
  FinalStat.success += stat.success;
  FinalStat.failure += stat.failure;
  FinalStat.spendms += stat.spendms;
}

void usage() {
  cout << "./BenchMark -port 1688 -size 4 -concurrency 10000 -runtime 60" << endl;
  cout << "options:" << endl;
  cout << "    -h,--help                    print usage" << endl;
  cout << "    -port,--port                 listen port" << endl;
  cout << "    -size,--size                 echo message size, unit is kbyte" << endl;
  cout << "    -concurrency,--concurrency   concurrency" << endl;
  cout << "    -runtime,--runtime           run time, unit is second" << endl;
  cout << endl;
}

int main(int argc, char *argv[]) {
  int64_t port;
  int64_t size;
  int64_t concurrency;
  int64_t runtime;
  CmdLine::Int64OptRequired(&port, "port");
  CmdLine::Int64OptRequired(&size, "size");
  CmdLine::Int64OptRequired(&concurrency, "concurrency");
  CmdLine::Int64OptRequired(&runtime, "runtime");
  CmdLine::SetUsage(usage);
  CmdLine::Parse(argc, argv);

  timeval end;
  timeval runBeginTime;
  gettimeofday(&runBeginTime, NULL);
  int runRoundCount = 0;
  while (true) {
    Stat curStat;
    std::thread threads[10];
    for (int threadId = 0; threadId < 10; threadId++) {
      threads[threadId] = std::thread(client, threadId, &curStat, port, size, concurrency);
    }
    for (int threadId = 0; threadId < 10; threadId++) {
      threads[threadId].join();
    }
    runRoundCount++;
    curStat.spendms /= 10;  // 取平均耗时
    UpdateFinalStat(curStat);
    gettimeofday(&end, NULL);
    std::cout << "round " << runRoundCount << " spend " << curStat.spendms << " ms. " << std::endl;
    if (getSpendMs(runBeginTime, end) >= runtime * 1000) {
      break;
    }
    sleep(2);  // 间隔2秒，再发起下一轮压测，这样压测结果更稳定
  }
  std::cout << "total spend " << FinalStat.spendms << " ms. avg spend " << FinalStat.spendms / runRoundCount
            << " ms. sum[" << FinalStat.sum << "],success[" << FinalStat.success << "],failure[" << FinalStat.failure
            << "]" << std::endl;
  return 0;
}