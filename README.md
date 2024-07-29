# CoroIOMultiplex
---
## 利用 C++协程 IOuring Epoll 实现的高性能协程异步io库

## TODO List
- [ ] 通用协程任务框架
  - [x] 通用Promise类
  - [x] 通用Task类
  - [x] 通用调度器
    - [x] Task调度器
    - [x] Timer调度器
  - [x] Awaiter类型萃取
  - [ ] ...
- [ ] Epoll实现协程接口
- [ ] IOuring实现协程接口
- [ ] 线程池调度器
- [ ] 文档
- [ ] 高性能高并发http服务器实现

## 编译环境
- 操作系统: Ubuntu 24.04 LTS
- 编译器: Clang++ 18.1.8
- CMake: CMake 3.28.3
- 内核: linux 6.8.0-35-generic

## 构建流程

```Bash
> mkdir build && cd build
> cmake ..
> cmake -build .
```
[暂]项目生成二进制文件包含一个`build/libCoroIOMultiplex.a`静态库和一个`build/demo`可运行文件