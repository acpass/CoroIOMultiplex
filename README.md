# CoroIOMultiplex

---

## 利用 C++协程 IOuring Epoll 实现的高性能协程异步io库

## TODO List

- [x] 通用协程任务框架
  - [x] 通用Promise类
  - [x] 通用Task类
  - [x] Awaiter类型萃取
  - [ ] ...
- [x] Epoll实现协程接口
- [x] IOuring实现协程接口
- [x] 线程池调度器
- [ ] 文档
- [x] 高性能高并发http服务器实现

## 编译环境

- 操作系统: Arch WSL
- 编译器: Clang++ 18.1.8
- CMake: CMake 3.30.3
- 内核: 6.6.36.3-microsoft-standard-WSL2

## demo构建流程

```Bash
> mkdir build && cd build
> cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang ..
> make uringhttp.cpp
```

## Usage

```Bash
> uringhttp [port] [webroot path]
```

## 压力测试

使用wrk进行压力测试达到20000QPS，99%延迟在139ms以下

```Bash
> wrk -t10 -c1000 -d10s --latency -H "Accept: */*" http://127.0.0.1:12312/
Running 10s test @ http://127.0.0.1:12312/
  10 threads and 1000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    47.82ms   22.72ms 761.52ms   94.12%
    Req/Sec     2.16k   174.91     2.86k    74.70%
  Latency Distribution
     50%   43.42ms
     75%   48.18ms
     90%   62.53ms
     99%  139.09ms
  215379 requests in 10.06s, 12.77GB read
Requests/sec:  21409.40
Transfer/sec:      1.27GB
```

进行60s压力测试，并使用strace监控系统调用可以发现，服务器运行期间除了线程同步所需的互斥锁系统调用外，几乎没有产生任何其他系统调用，完全在用户态运行，免去了大量的地址空间切换开销。

```Bash
> sudo strace -c -f -p 49155
strace: Process 49155 attached with 18 threads
^Cstrace: Process 49171 detached
strace: Process 49170 detached
strace: Process 49169 detached
strace: Process 49168 detached
strace: Process 49167 detached
strace: Process 49166 detached
strace: Process 49165 detached
strace: Process 49164 detached
strace: Process 49163 detached
strace: Process 49162 detached
strace: Process 49161 detached
strace: Process 49160 detached
strace: Process 49159 detached
strace: Process 49158 detached
strace: Process 49157 detached
strace: Process 49156 detached
strace: Process 49154 detached
strace: Process 49155 detached
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 94.95 1646.734140        1188   1385612    204684 futex
  3.23   56.102935    56102935         1           clock_nanosleep
  1.81   31.413409    31413409         1           restart_syscall
  0.00    0.017096        5698         3           madvise
  0.00    0.004745        4745         1           munmap
  0.00    0.000163          81         2           close
  0.00    0.000155         155         1           read
  0.00    0.000079          79         1           openat
------ ----------- ----------- --------- --------- ----------------
100.00 1734.272722        1251   1385622    204684 total
```

# 项目结构

## 1. 协程任务框架

### 1.1 Promise类

C++协程的基础类，每个Task对象必须包含其对应的promise对象，用于管理协程的状态和结果。
promise也作为协程内外的通讯channel存在。
本项目实现了一个作为基类的Promise类，用户可以继承该类实现自己的Promise类。
promiseBase类定义了基于其的协程的基本行为：

- initial_suspend()：协程开始时会调用co_await initial_suspend()，用于初始化协程，本项目中所有协程都在初始化后暂停
- final_suspend()：协程结束时会调用co_await final_suspend()，用于清理协程
  - 本项目中所有协程都在结束后暂停
  - 为了实现协程的调度和对称协程的实现，定义了returnPrevAwaiter类作为final_suspend的返回值
    - returnPrevAwaiter类中包含了一个coroutine_handle<>对象，用于记录调用者的协程句柄
    - 在协程为detached状态下时，returnPrevAwaiter会将协程直接暂停，即返回调度器
    - 在协程为attached状态下时，returnPrevAwaiter会将协程返回到其调用者，实现对称协程
- coroutine_handle<> caller：用于记录调用者的协程句柄，用于实现对称协程

在继承promiseBase类后，本项目实现了用于不同协程返回值的模板类promiseType<typename T=void>。
并对promiseType<void>进行了特化，用于实现无返回值的协程。
为了实现返回值或者yield值的传递，promiseType<typename T>内定义了return_value()和yield_value()函数。
并在其内部定义了一个T对象，用于存储返回值或者yield值。
当协程返回或yield时，相应返回值被存储在promiseType<typename T>对象内，等待调用者获取。
当调用者的co_await返回时，调用者awaiter中的await_resume函数会获取promiseType<typename T>对象内的返回值或者yield值,并返回给调用者。

### 1.2 Task类

Task类是协程的基础类，用于管理协程的promise对象和协程句柄。
Task<typename T=void>作为模板类，含有相应的promiseType<typename T>对象。
本项目将Task类作为协程对象的抽象，基于Task类实现协程的调度和管理。

- Task内的detach()函数用于将协程分离，使其在后台运行
- Task定义了其co_await的行为，用于实现co_await TaskCoroutine()的语法
  - taskAwaiter::await_resume()会从promise对象中获取返回值或者yield值并返回

### 1.3 Awaiter类型萃取

TODO

### 1.4 helper

- getSelfAwaiter()：获取当前协程的coroutine_handle
