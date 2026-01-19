# tcp-atm-semaphore-lab

> **TCP Web ATM（多客戶端並行 + System V Semaphore 防止 Race Condition）**

此專案主要練習多執行緒對於共享資料的存取，以及如何避免Race Condition：
- **Server**：提供一個「只有一個帳戶」的 ATM 服務，多個 client 可以同時連線並執行 **deposit / withdraw**。
- **Client**：送出一次指令（動作 / 金額 / 次數），由 server 反覆執行並在每次操作後印出餘額。
- **重點**：在多執行緒（multi-threading）同時存取共享資料（帳戶餘額）時，使用 **System V semaphore** 保護 critical section，避免 **race condition**。


---

## 目錄

- [功能與特色](#功能與特色)
- [專案結構](#專案結構)
- [通訊協定](#通訊協定)
- [編譯與執行](#編譯與執行)
- [demo.sh 一鍵展示](#demosh-一鍵展示)
- [Linux / OS 觀念整理](#linux--os-觀念整理)
  - [1) Socket 與檔案描述子](#1-socket-與檔案描述子)
  - [2) 連線模型與多執行緒](#2-連線模型與多執行緒)
  - [3) Race Condition 與 Critical Section](#3-race-condition-與-critical-section)
  - [4) System V Semaphore（核心 IPC 物件）](#4-system-v-semaphore核心-ipc-物件)
  - [5) Signal 與資源清理](#5-signal-與資源清理)
- [程式碼導讀](#程式碼導讀)
- [常見問題與除錯](#常見問題與除錯)
- [限制與可改進點](#限制與可改進點)

---

## 功能與特色

- **TCP socket server/client**（BSD sockets API）
- Server 支援 **多客戶端同時連線**：採用「**一連線一執行緒**」模型（thread-per-connection）
- 使用 **System V semaphore**（`semget/semop/semctl`）做 mutual exclusion
- `demo.sh` 使用 **tmux** 同時啟動 server 與多個 client，觀察「存提款交錯」與餘額變化

---

## 專案結構

```
.
├── server.c              # TCP server：accept 後 per-connection thread 處理
├── client.c              # TCP client：送出 deposit/withdraw amount times
├── Makefile              # 編譯 client/server
└── demo.sh               # tmux demo：一次跑 server + 多 client 併發
```

---

## 通訊協定

Client 送給 server 的 payload 是**純文字**（ASCII），格式：

```
<action> <amount> <times>\n
```

- `<action>`：`deposit` 或 `withdraw`
- `<amount>`：每次存/提款金額（整數）
- `<times>`：重複執行次數（整數）

Server 端在每次更新 balance 後輸出：

```
After deposite: <balance>
After withdraw: <balance>
```

> 註：原始程式碼中的字串為 `After deposite`（拼字），如果要更精準可改成 `After deposit`。

---

## 編譯與執行

### 環境需求

- Linux（建議 Ubuntu / Debian 系 VM）
- `gcc`, `make`
- `tmux`（只在跑 `demo.sh` 時需要）

### 編譯

```bash
make
```

會產生：
- `./server`
- `./client`

> **關於 pthread 連結旗標：**
> - 在較新的 glibc（例如 2.34+）環境，pthread 已經合併進 libc，沒有 `-pthread` 也可能編得過。
> - 但為了可攜性（例如較舊的 Linux 發行版），建議在 Makefile 的 server target 加上 `-pthread`。

### 啟動 server

```bash
./server <port>
# e.g.
./server 8888
```

### 執行 client

```bash
./client <ip> <port> <deposit|withdraw> <amount> <times>
# e.g.
./client 127.0.0.1 8888 deposit 1 500
```

---

## demo.sh 一鍵展示

`demo.sh` 會：
1. `make clean && make`
2. 用 tmux 開一個 session
3. 同時跑 1 個 server + 4 個 client（不同 deposit/withdraw 參數）

使用方法：

```bash
chmod +x demo.sh
./demo.sh
```

`tmux` 內看到 server 印出餘額交錯變化，即可展示「多 client 併發 + semaphore 保護」的效果。

---

## Linux / OS 觀念整理

下面用「Linux OS 程式設計師」的角度，把這份程式背後的核心概念說清楚。

### 1) Socket 與檔案描述子

在 Linux 裡，**socket 也是一種 file descriptor (fd)**。
- `socket()` 回傳一個整數 fd
- `connect()/bind()/listen()/accept()` 都是對這個 fd 做操作
- `read()/write()/close()` 也同樣適用

這種「萬物皆檔案（everything is a file descriptor）」的抽象，讓網路 I/O 跟檔案 I/O 可以共用同一套系統呼叫語意。

**server.c 的典型流程：**
1. `socket(AF_INET, SOCK_STREAM, 0)` 建立 TCP socket
2. `bind()` 綁定本機 IP/port
3. `listen()` 進入被動等待
4. `accept()` 接受連線，得到「新的」連線 fd（`CFD`）

**client.c 的流程：**
1. `socket()`
2. `connect()` 對 server 建立 TCP 連線
3. `write()` 把指令送出
4. `shutdown(SHUT_WR)`：關閉「寫入方向」，讓 server 的 `read()` 最終會得到 EOF（回傳 0）

### 2) 連線模型與多執行緒

server 使用的是常見的 **thread-per-connection**：
- main thread 在 `accept()` loop 裡不斷接新連線
- 每接到一個連線就 `pthread_create()` 一條 worker thread
- worker thread 負責該 client 的讀取與處理
- `pthread_detach()`：讓 thread 結束後資源自動回收（不需要 `pthread_join()`）

OS 層面來看：
- 每個 thread 都有自己的 user-space stack、register context
- Linux scheduler 會在不同 thread 間做 time-slicing（可被搶佔）
- 所以「看似」同一段 C 程式中的 `balance += amount`，在多 thread 下其實可能被切成多步驟執行並交錯

### 3) Race Condition 與 Critical Section

`balance` 是全域變數、所有 thread 共用：

```c
int balance = 0;
```

如果兩個 client 同時做 deposit/withdraw，而你沒有鎖住更新區段，可能出現：
- read-modify-write 被交錯
- 最終 balance 結果錯誤（丟失更新 / 覆蓋更新）

這段更新 balance 的區域就是典型 **critical section**。

### 4) System V Semaphore（核心 IPC 物件）

本專案用 **System V semaphore** 做 mutual exclusion。

#### 為什麼叫「核心 IPC 物件」？
- `semget()` 建立的 semaphore 是由 **kernel** 管理的 system-wide object
- 不只是 threads，連不同 processes 只要 key 相同也能取得同一把 semaphore

#### 程式中的 P/V 意義

```c
P(sem);   // lock：sem_op = -1
...       // critical section
V(sem);   // unlock：sem_op = +1
```

`semop()` 在核心中以原子方式完成「檢查 + 修改」：
- 若值為 0，`P()` 會 **block**（睡眠等待），直到別人 `V()`
- 這就是 semaphore 能解決競爭的根本原因

#### 為什麼需要清理？
System V semaphore 不是跟著 process 自動消失（除非你明確移除）：
- 正常 Ctrl+C 時，程式用 `semctl(IPC_RMID)` 移除
- 但如果程式 crash/kill -9，semaphore 可能殘留

你可以用：

```bash
ipcs -s          # 查看目前系統的 semaphore
ipcrm -s <id>    # 手動移除
```

> 這也是作業規格特別要求「結束前要清 semaphore」的原因。

#### 為什麼不用 pthread_mutex？
在「同一個 process 內的 threads」保護共享變數，`pthread_mutex_t` 通常更輕量、語意更貼近。
本作業使用 System V semaphore 主要是在練 OS IPC/同步機制：
- 以熟悉 kernel-managed semaphore 與 `ipcs/ipcrm` 等工具

### 5) Signal 與資源清理

server 註冊了：

```c
signal(SIGINT, cleanSocketSemaphore);
```

當你 Ctrl+C：
- 會收到 SIGINT
- handler 會 `close(listenFd)` 並 `semctl(IPC_RMID)` 移除 semaphore

OS 角度：
- **Signal 是一種非同步事件**，可能在任何時刻打斷你的程式
- 正式產品通常會用更安全的方式處理（例如設旗標、讓主迴圈安全退出），因為 signal handler 能做的事有限（async-signal-safe 限制）
- 但在 Lab 情境下這樣做足以展示「如何確保 IPC 物件被清理」

---

## 程式碼導讀

### server.c

- `socket_Listen(port)`：建立 listen socket、`bind()`、`listen()`
- `accept_Connection()`：`accept()` 成功後動態配置一個 `int* clientFd`，把 fd 指標傳給 thread
- `workerThread(void *arg)`：
  - `read(CFD, recBuf, BUFFER_SIZE-1)` 讀入指令
  - `sscanf(recBuf, "%9s %d %d", action, &amount, &times)` 解析
  - 迴圈 `times` 次：
    - `P(sem)` → 更新 balance → `V(sem)`
    - `usleep(200)` 讓輸出更容易交錯展示
- `cleanSocketSemaphore()`：收到 SIGINT 時清除 semaphore（`IPC_RMID`）

### client.c

- 解析 CLI 參數：`<ip> <port> <deposit/withdraw> <amount> <times>`
- `socketInit()`：`socket()` → `inet_pton()` → `connect()`
- `snprintf(sendBuf, ...)` 組出訊息字串
- `write(sock, sendBuf, BUFFER_SIZE)` 把 buffer 送出
- `shutdown(sock, SHUT_WR)` 送出 EOF

### demo.sh

- 用 tmux 做多視窗展示（server + 多 client 同時操作）
- 方便 Demo「多連線並行」與餘額交錯

### Makefile

- `make`：編譯出 `client` 與 `server`
- 建議改善：對 server 加上 `-pthread`（可攜性）

---

## 常見問題與除錯

### 1) 重新啟動 server 出現 `semget() failed`

因為程式用 `IPC_CREAT | IPC_EXCL`，如果上一次 semaphore 沒清掉，下一次會建立失敗。

解法：
```bash
ipcs -s
ipcrm -s <semid>
```

### 2) 重新啟動 server 出現 `bind() failed: Address already in use`

可能是 port 還在 TIME_WAIT 或還有舊的 process 佔用。

解法：
- 換 port
- 或在 server 加上 `setsockopt(..., SO_REUSEADDR, ...)`
- 或找出佔用者：`ss -ltnp | grep <port>`

### 3) 沒有 tmux

```bash
sudo apt-get install tmux
```

---

## 限制與可改進點

這份程式是針對特定OS觀念作練習，功能刻意簡化，以下是比較「OS/網路程式設計」角度的可改進點：

- **client 寫入長度**：目前用 `write(..., BUFFER_SIZE)` 固定送 256 bytes（大多是 `\0`），可改成 `write(sock, sendBuf, strlen(sendBuf))`。
- **TCP 的 partial read/write**：在真實網路上 `read()` / `write()` 可能只處理部分資料，應該設計 framing（例如以 `\n` 為分隔）並用迴圈收齊。
- **server 端回覆**：目前 server 只印在 stdout，不回傳結果給 client；可以加上 `write(CFD, ...)` 回傳餘額。
- **負餘額/溢位**：沒有檢查 withdraw 造成負值、也沒有檢查 int overflow。
- **同步機制選擇**：如果只是 thread 間保護共享變數，`pthread_mutex` 會更直覺；System V semaphore 則適合跨 process 的同步。
- **signal handler 安全性**：正式產品會避免在 handler 直接呼叫非 async-signal-safe 的函式。

---

## 背景 / 來源

本專案依據嵌入式作業系統課程中實驗之規格完成，目標是練習：
- Linux socket programming
- 多執行緒併發
- semaphore 同步與 race condition 處理

