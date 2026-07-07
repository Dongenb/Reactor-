# 基于 Reactor 模式的高性能 C++ 聊天服务器

这是一个面向 Linux/WSL 的 C++17 聊天服务器项目，核心网络层基于 `epoll` 和非阻塞 Socket 实现单线程 Reactor 事件驱动模型。项目不依赖第三方库，包含服务端、命令行客户端、功能测试脚本和压力测试脚本。

## 项目亮点

- 核心网络层：基于 `epoll + 非阻塞 Socket` 实现 Reactor 事件循环，单线程管理长连接。
- 协议设计：使用 `Length + Type + RequestId + Payload` 自定义二进制协议，能处理 TCP 粘包和半包。
- 服务治理：支持登录、登出、广播消息、点对点私聊、30 秒客户端心跳和 35 秒服务端连接回收。
- 工程实践：使用 RAII 管理 fd 生命周期，显式忽略 `SIGPIPE`，发送端使用 `MSG_NOSIGNAL` 避免异常退出。

## 构建

需要 Linux 或 WSL，安装 `g++` 和 `cmake`：

```bash
cmake -S . -B build
cmake --build build
```

当前项目使用 Linux 专有的 `epoll`，不支持 Windows 原生编译。

## 运行

启动服务端：

```bash
./build/chat_server --host 0.0.0.0 --port 9000
```

启动客户端：

```bash
./build/chat_client --host 127.0.0.1 --port 9000
```

客户端命令：

```text
/login alice
/all hello everyone
/to bob private message
/quit
```

广播消息默认会发给所有已登录用户，包括发送者自己，用作发送确认和聊天室回显。

## 协议格式

每个包由固定 10 字节 header 和 JSON payload 组成，所有整数使用网络字节序：

| 字段 | 大小 | 说明 |
| --- | --- | --- |
| length | 4 字节 | payload 长度，不包含 header |
| type | 2 字节 | 消息类型 |
| request_id | 4 字节 | 请求 ID，用于客户端匹配响应 |
| payload | length 字节 | 简洁 JSON 字符串 |

消息类型：

| 值 | 名称 | 方向 |
| --- | --- | --- |
| 1 | LOGIN | 客户端到服务端 |
| 2 | LOGOUT | 客户端到服务端 |
| 3 | BROADCAST | 双向 |
| 4 | PRIVATE_MSG | 双向 |
| 5 | HEARTBEAT | 客户端到服务端 |
| 6 | ACK | 服务端到客户端 |
| 7 | ERROR | 服务端到客户端 |

payload 示例：

```json
{"username":"alice"}
{"from":"alice","message":"hello"}
{"from":"alice","to":"bob","message":"hi"}
{"code":400,"message":"username already exists"}
```

## 测试

功能测试会启动本地服务端并验证登录、广播、私聊、重复登录和心跳：

```bash
python3 scripts/functional_test.py
```

压力测试默认模拟 3000 个连接和 100000 条消息：

```bash
python3 scripts/stress_test.py --host 127.0.0.1 --port 9000 --connections 3000 --messages 100000
```

如果本机文件描述符限制较低，先调高限制：

```bash
ulimit -n 100000
```

压力测试目标：累计完成 100000+ 条消息收发，服务端无崩溃、无协议解析错误，异常连接平均在 35 秒内回收。

## 目录结构

```text
src/common/   协议编解码、轻量 JSON、Socket RAII 工具
src/server/   Reactor 聊天服务端
src/client/   命令行聊天客户端
scripts/      功能测试和压力测试脚本
```
