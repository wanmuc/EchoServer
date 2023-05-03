# 1.EchoServer
> 通过在Linux上使用epoll这个IO复用设施，结合多种Reactor并发模型，可以实现经典的回显服务EchoServer，从而加深对高性能并发模型的理解。

学习完本项目后，你将能够得到以下技术能力的良好锻炼，并加深对它们的理解：

- 网络编程应用的实现和调试
- 进程池的实现和使用
- 线程池的实现和使用
- 协议设计与实现（编解码）的能力
- 多种高效的Reactor并发模型的理解和应用
- 基准性能压测工具的实现和使用，以评估系统的性能和稳定性

# 2.目录结构
这个项目的目录结构如下所示。
```
EchoServer
├── BenchMark
├── cmdline.cpp
├── cmdline.h
├── codec.hpp
├── common.hpp
├── conn.hpp
├── epollctl.hpp
├── EpollReactorProcessPool
├── EpollReactorSingleProcess
├── EpollReactorThreadPool
├── EpollReactorThreadPoolHSHA
├── EpollReactorThreadPoolMS
├── mp_account.png
└── README.md
```
- BenchMark是基准性能压测工具的代码目录
- EpollReactorProcessPool是Reactor进程池实现的代码目录
- EpollReactorSingleProcess是Reactor单进程实现的代码目录
- EpollReactorThreadPool是Reactor线程池实现的代码目录
- EpollReactorThreadPoolHSHA是Reactor线程池HSHA实现的代码目录
- EpollReactorThreadPoolMS是Reactor线程池MS实现的代码目录

# 3.微信公众号
欢迎关注微信公众号「Linux后端研发工程实践」，第一时间获取最新文章！扫码即可订阅。也欢迎大家加我个人微信号：wanmuc2018，让我们共同进步。
![img.png](https://github.com/wanmuc/EchoServer/blob/main/mp_account.png#pic_center=660*180)