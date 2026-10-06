# AI Prompt Log - IE3090 RemoteOps (IT24101660)
Student: K.P.D.S.S.S. Sujeewanta

---

### Prompt 1 - Socket newline reading
- **Prompt I used:** "c socket recv line by line until newline without buffering issues"
- **What I got:** A small loop reading 1 byte at a time with `recv(sockfd, &c, 1, 0)` and stopping at `\n`.
- **What I changed:** Added it as `read_line()` in both `agent_660.c` and `controller_660.c`. Set port to 9410 and added `SID:0661` to the output format.

### Prompt 2 - Pthreads and log file locking
- **Prompt I used:** "how to pass client ip and socket to pthread in c and write to log file safely"
- **What I got:** Example using `typedef struct` with `malloc` before `pthread_create`, and `pthread_mutex_lock` around `fopen("...", "a")`.
- **What I changed:** Changed log filename to `remoteops_IT24101660.log`, added timestamp formatting with `localtime_r()`, and added my `AUTH OPS-1660` check inside `handle_client()`.

### Prompt 3 - Reading /proc files in C
- **Prompt I used:** "how to read MemAvailable from /proc/meminfo and process name from /proc/pid/stat in C"
- **What I got:** Code using `fgets` + `sscanf` for meminfo, and `strchr(line, '(')` / `strrchr(line, ')')` to get the process name from stat.
- **What I changed:** Combined uptime, loadavg, and meminfo into `get_system_status()`. When compiling `get_process_list()`, gcc gave a `-Wformat-truncation` warning on `stat_path[256]`, so I increased the array size to `512` and tested with `PROC 5` and `PROC 999`.

### Prompt 4 - File upload over TCP socket
- **Prompt I used:** "c tcp socket send file after sending filename and size header"
- **What I got:** Idea to send a `READY` message from server after checking header, then loop `recv()` until `remaining == 0`.
- **What I changed:** Added `is_safe_filename()` to block `..` and `/` path traversal, added the 660 KB (`675840` bytes) check for my reg number, and saved files into `agentfiles/IT24101660/`. Fixed `send_buf` size warning in `controller_660.c` by making it `BUFFER_SIZE + 4`.
