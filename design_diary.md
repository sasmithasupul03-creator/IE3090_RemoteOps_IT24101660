# Design Diary - IE3090 RemoteOps
Student: K.P.D.S.S.S. Sujeewanta (IT24101660)

My values from reg no IT24101660:
- Port: 9000 + (1660 % 500) = 9000 + 410 = 9410
- Session ID tag: SID:0661 (reverse of 1660)
- Auth token: OPS-1660
- Upload path: agentfiles/IT24101660/
- Max file size: 660 KB (675840 bytes)

---

## Day 1 (2026-10-02)
- Created repo folder on my CentOS VM and connected it to GitHub.
- Wrote Makefile_660 with -Wall -Wextra -pthread so it compiles without warnings.
- Started coding agent_660.c and controller_660.c to test basic socket connection on port 9410.
- Issue: normal recv() was sometimes keeping newline chars or splitting text, so I added a small read_line() loop to read char by char until '\n'.
- Tested connection using 2 SSH terminals on 127.0.0.1:9410. Replies come back with SID:0661 at the end.

## Day 2 (2026-10-03)
- Changed server to support multiple clients at the same time using pthreads (pthread_create and pthread_detach).
- Passed client socket fd and IP/port using a malloc'd struct so threads don't overwrite each other's variables.
- Added log_event() to write into remoteops_IT24101660.log. Used a pthread_mutex_t lock before fopen() so log lines don't get mixed up when 2 clients run commands together.
- Implemented AUTH command checking for OPS-1660. If client didn't run AUTH first, server replies ERR 403.

## Day 3 (2026-10-04)
- Worked on the 3 monitoring commands reading from /proc directly in C:
  1. STATUS -> reads /proc/uptime, /proc/loadavg, and /proc/meminfo (used mem = MemTotal - MemAvailable).
  2. PROC -> uses opendir("/proc") to find number folders (PIDs) and reads process name + state from /proc/[pid]/stat.
  3. INFO -> used uname() struct for kernel/host/arch and read PRETTY_NAME from /etc/os-release.
- Compiler problem: got a format-truncation warning from gcc on line 134 because stat_path[256] was too small for d_name + "/proc//stat". Changed it to stat_path[512] and warning went away.

## Day 4 (2026-10-05)
- Added EXEC command. Put the 5 allowed commands (date, whoami, uname -a, df -h, uptime) in a string array and checked with strcmp(). Anything else (like cat /etc/passwd) returns ERR 403. Used popen() to get command output.
- Added PUT command for file uploads into agentfiles/IT24101660/.
- Protocol flow I used: client sends "PUT filename size", server checks if filename has ".." or "/" and checks if size <= 675840 bytes (660 KB). If ok, server sends "READY SID:0661" and reads the exact byte count.
- Fixed another gcc warning in controller_660.c (line 154) where send_buf[4096] was 1 byte short for input + "\n". Changed it to BUFFER_SIZE + 4.

## Day 5 (2026-10-06)
- Tested all error cases (wrong token, PROC 999, path traversal ../hack.txt, >660KB file) and multi-client connections.
- Checked remoteops_IT24101660.log to make sure all commands and IPs are logged properly.
