#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <sys/stat.h>

#define AGENT_PORT 9410
#define SID_TAG "SID:0661"
#define AUTH_TOKEN "OPS-1660"
#define LOG_FILE "remoteops_IT24101660.log"
#define UPLOAD_DIR "agentfiles/IT24101660"
#define MAX_UPLOAD_BYTES (660 * 1024) /* 660 KB = 675,840 bytes */
#define BUFFER_SIZE 4096

/* Mutex to ensure thread-safe logging across concurrent Controller sessions */
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Structure to pass client details to each worker thread */
typedef struct {
    int sockfd;
    struct sockaddr_in addr;
} client_session_t;

/* Thread-safe logging function with timestamp, client IP:port, command, and outcome */
void log_event(const char *client_ip, int client_port, const char *command, const char *outcome) {
    pthread_mutex_lock(&log_mutex);

    FILE *fp = fopen(LOG_FILE, "a");
    if (fp != NULL) {
        time_t now = time(NULL);
        struct tm tm_info;
        localtime_r(&now, &tm_info);
        char time_buf[32];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);

        fprintf(fp, "[%s] [%s:%d] CMD=\"%s\" OUTCOME=\"%s\" (%s)\n",
                time_buf, client_ip, client_port, command, outcome, SID_TAG);
        fclose(fp);
    }

    pthread_mutex_unlock(&log_mutex);
}

/* Helper function to read a single newline-terminated line from TCP socket */
ssize_t read_line(int sockfd, char *buffer, size_t maxlen) {
    size_t n = 0;
    char c;
    while (n < maxlen - 1) {
        ssize_t rc = recv(sockfd, &c, 1, 0);
        if (rc == 1) {
            if (c == '\r') continue;
            if (c == '\n') break;
            buffer[n++] = c;
        } else if (rc == 0) {
            if (n == 0) return 0; /* Client disconnected */
            break;
        } else {
            return -1; /* Error */
        }
    }
    buffer[n] = '\0';
    return (ssize_t)n;
}

/* Read real-time CPU load, memory usage, and uptime from /proc */
int get_system_status(char *out_buf, size_t max_len) {
    double uptime_sec = 0.0;
    double load1 = 0.0, load5 = 0.0, load15 = 0.0;
    long mem_total_kb = 0, mem_avail_kb = 0;

    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) return -1;
    if (fscanf(fp, "%lf", &uptime_sec) != 1) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    fp = fopen("/proc/loadavg", "r");
    if (!fp) return -1;
    if (fscanf(fp, "%lf %lf %lf", &load1, &load5, &load15) != 3) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    fp = fopen("/proc/meminfo", "r");
    if (!fp) return -1;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "MemTotal: %ld kB", &mem_total_kb) == 1) continue;
        if (sscanf(line, "MemAvailable: %ld kB", &mem_avail_kb) == 1) continue;
    }
    fclose(fp);

    long mem_used_kb = mem_total_kb - mem_avail_kb;
    double mem_pct = (mem_total_kb > 0) ? ((double)mem_used_kb * 100.0 / mem_total_kb) : 0.0;

    snprintf(out_buf, max_len,
             "OK STATUS uptime=%.0fs load=%.2f,%.2f,%.2f mem_used_kb=%ld mem_total_kb=%ld mem_pct=%.1f%% %s\n",
             uptime_sec, load1, load5, load15, mem_used_kb, mem_total_kb, mem_pct, SID_TAG);
    return 0;
}

/* Check if a directory name in /proc is purely numeric (a PID) */
int is_pid_dir(const char *name) {
    if (!name || *name == '\0') return 0;
    while (*name) {
        if (!isdigit((unsigned char)*name)) return 0;
        name++;
    }
    return 1;
}

/* Read running processes from /proc/[pid]/stat up to max_count */
int get_process_list(int max_count, char *out_buf, size_t max_len) {
    DIR *dir = opendir("/proc");
    if (!dir) return -1;

    char list_buf[BUFFER_SIZE - 256];
    list_buf[0] = '\0';
    size_t used = 0;
    int count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max_count) {
        if (!is_pid_dir(entry->d_name)) continue;

        char stat_path[512];
        snprintf(stat_path, sizeof(stat_path), "/proc/%s/stat", entry->d_name);

        FILE *fp = fopen(stat_path, "r");
        if (!fp) continue;

        char stat_line[512];
        if (fgets(stat_line, sizeof(stat_line), fp) != NULL) {
            int pid = 0;
            char state = '?';
            char comm[128] = "unknown";

            /* Extract process name inside parentheses (comm) and state */
            char *l_paren = strchr(stat_line, '(');
            char *r_paren = strrchr(stat_line, ')');
            if (l_paren && r_paren && r_paren > l_paren) {
                sscanf(stat_line, "%d", &pid);
                size_t name_len = (size_t)(r_paren - l_paren - 1);
                if (name_len >= sizeof(comm)) name_len = sizeof(comm) - 1;
                memcpy(comm, l_paren + 1, name_len);
                comm[name_len] = '\0';

                if (r_paren[1] == ' ' && r_paren[2] != '\0') {
                    state = r_paren[2];
                }

                char item[192];
                int n = snprintf(item, sizeof(item), "%s%d:%s(%c)",
                                 (count > 0) ? "," : "", pid, comm, state);
                if (n > 0 && used + (size_t)n < sizeof(list_buf) - 1) {
                    memcpy(list_buf + used, item, (size_t)n);
                    used += (size_t)n;
                    list_buf[used] = '\0';
                    count++;
                }
            }
        }
        fclose(fp);
    }
    closedir(dir);

    snprintf(out_buf, max_len, "OK PROC count=%d list=%s %s\n",
             count, (count > 0) ? list_buf : "none", SID_TAG);
    return 0;
}

/* Read hostname, kernel release, architecture, and OS details */
int get_system_info(char *out_buf, size_t max_len) {
    struct utsname uts;
    if (uname(&uts) != 0) return -1;

    char os_name[128] = "Linux";
    FILE *fp = fopen("/etc/os-release", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                char *val = line + 12;
                if (*val == '"') val++;
                size_t len = strlen(val);
                while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r' || val[len - 1] == '"')) {
                    val[--len] = '\0';
                }
                snprintf(os_name, sizeof(os_name), "%s", val);
                break;
            }
        }
        fclose(fp);
    }

    for (char *p = os_name; *p; p++) {
        if (*p == ' ') *p = '_';
    }

    snprintf(out_buf, max_len, "OK INFO host=%s kernel=%s arch=%s os=%s %s\n",
             uts.nodename, uts.release, uts.machine, os_name, SID_TAG);
    return 0;
}

/* Validate against strict whitelist and execute safe system command */
int execute_whitelisted_cmd(const char *subcmd, char *out_buf, size_t max_len) {
    const char *whitelist[] = {
        "date",
        "whoami",
        "uname -a",
        "df -h",
        "uptime",
        NULL
    };

    int allowed = 0;
    for (int i = 0; whitelist[i] != NULL; i++) {
        if (strcmp(subcmd, whitelist[i]) == 0) {
            allowed = 1;
            break;
        }
    }

    if (!allowed) {
        return -2; /* Not whitelisted */
    }

    FILE *pipe = popen(subcmd, "r");
    if (!pipe) return -1;

    char raw_out[BUFFER_SIZE - 256];
    size_t total = 0;
    char line[256];

    raw_out[0] = '\0';
    while (fgets(line, sizeof(line), pipe) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        int n = snprintf(raw_out + total, sizeof(raw_out) - total, "%s%s",
                         (total > 0) ? " | " : "", line);
        if (n < 0 || (size_t)n >= sizeof(raw_out) - total) {
            total = sizeof(raw_out) - 1;
            break;
        }
        total += (size_t)n;
    }
    pclose(pipe);

    snprintf(out_buf, max_len, "OK EXEC cmd=\"%s\" output=\"%s\" %s\n",
             subcmd, (total > 0) ? raw_out : "(empty)", SID_TAG);
    return 0;
}

/* Prevent directory traversal (rejects '/', '\', '..', or illegal chars) */
int is_safe_filename(const char *fname) {
    if (!fname || *fname == '\0' || *fname == '.') return 0;
    if (strstr(fname, "..") != NULL) return 0;
    if (strchr(fname, '/') != NULL || strchr(fname, '\\') != NULL) return 0;

    for (const char *p = fname; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-') {
            return 0;
        }
    }
    return 1;
}

/* Ensure agentfiles/IT24101660 directory exists */
void ensure_upload_dir(void) {
    mkdir("agentfiles", 0755);
    mkdir(UPLOAD_DIR, 0755);
}

/* Thread function to handle an individual Controller session */
void *handle_client(void *arg) {
    client_session_t *session = (client_session_t *)arg;
    int client_fd = session->sockfd;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(session->addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(session->addr.sin_port);
    free(session);

    printf("[Agent] Controller connected from %s:%d (Thread ID: %lu)\n",
           client_ip, client_port, (unsigned long)pthread_self());
    log_event(client_ip, client_port, "CONNECT", "SESSION_OPENED");

    int authenticated = 0;
    char cmd[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    while (read_line(client_fd, cmd, sizeof(cmd)) > 0) {
        printf("[Agent %s:%d] Received: %s\n", client_ip, client_port, cmd);

        /* 1. Always allow QUIT even before authentication */
        if (strcmp(cmd, "QUIT") == 0) {
            snprintf(response, sizeof(response), "OK BYE %s\n", SID_TAG);
            send(client_fd, response, strlen(response), 0);
            log_event(client_ip, client_port, "QUIT", "OK BYE");
            break;
        }

        /* 2. Handle AUTH <token> command */
        if (strncmp(cmd, "AUTH", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\0')) {
            const char *token = cmd + 4;
            while (*token == ' ') token++;

            if (*token == '\0') {
                snprintf(response, sizeof(response), "ERR 400 MISSING_TOKEN %s\n", SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "ERR 400 MISSING_TOKEN");
            } else if (strcmp(token, AUTH_TOKEN) == 0) {
                authenticated = 1;
                snprintf(response, sizeof(response), "OK AUTH %s\n", SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "OK AUTH");
            } else {
                snprintf(response, sizeof(response), "ERR 401 INVALID_TOKEN %s\n", SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "ERR 401 INVALID_TOKEN");
            }
            continue;
        }

        /* 3. Reject all other commands if session is not authenticated */
        if (!authenticated) {
            snprintf(response, sizeof(response), "ERR 403 UNAUTHORIZED_PLEASE_AUTH_FIRST %s\n", SID_TAG);
            send(client_fd, response, strlen(response), 0);
            log_event(client_ip, client_port, cmd, "ERR 403 UNAUTHORIZED");
            continue;
        }

        /* 4. Handle STATUS command */
        if (strcmp(cmd, "STATUS") == 0) {
            if (get_system_status(response, sizeof(response)) == 0) {
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, "STATUS", "OK STATUS");
            } else {
                snprintf(response, sizeof(response), "ERR 500 STATUS_READ_FAILED %s\n", SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, "STATUS", "ERR 500 STATUS_READ_FAILED");
            }
            continue;
        }

        /* 5. Handle PROC [count] command */
        if (strncmp(cmd, "PROC", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\0')) {
            int max_procs = 10;
            const char *arg_str = cmd + 4;
            while (*arg_str == ' ') arg_str++;

            if (*arg_str != '\0') {
                max_procs = atoi(arg_str);
                if (max_procs <= 0 || max_procs > 50) {
                    snprintf(response, sizeof(response), "ERR 400 INVALID_PROC_COUNT_USE_1_TO_50 %s\n", SID_TAG);
                    send(client_fd, response, strlen(response), 0);
                    log_event(client_ip, client_port, cmd, "ERR 400 INVALID_PROC_COUNT");
                    continue;
                }
            }

            if (get_process_list(max_procs, response, sizeof(response)) == 0) {
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "OK PROC");
            } else {
                snprintf(response, sizeof(response), "ERR 500 PROC_READ_FAILED %s\n", SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "ERR 500 PROC_READ_FAILED");
            }
            continue;
        }

        /* 6. Handle INFO command */
        if (strcmp(cmd, "INFO") == 0) {
            if (get_system_info(response, sizeof(response)) == 0) {
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, "INFO", "OK INFO");
            } else {
                snprintf(response, sizeof(response), "ERR 500 INFO_READ_FAILED %s\n", SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, "INFO", "ERR 500 INFO_READ_FAILED");
            }
            continue;
        }

        /* 7. Handle EXEC <cmd> command */
        if (strncmp(cmd, "EXEC", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\0')) {
            const char *subcmd = cmd + 4;
            while (*subcmd == ' ') subcmd++;

            if (*subcmd == '\0') {
                snprintf(response, sizeof(response), "ERR 400 MISSING_EXEC_COMMAND %s\n", SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "ERR 400 MISSING_EXEC_COMMAND");
                continue;
            }

            int rc = execute_whitelisted_cmd(subcmd, response, sizeof(response));
            if (rc == 0) {
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "OK EXEC");
            } else if (rc == -2) {
                snprintf(response, sizeof(response),
                         "ERR 403 COMMAND_NOT_WHITELISTED_ALLOWED(date,whoami,uname -a,df -h,uptime) %s\n",
                         SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "ERR 403 COMMAND_NOT_WHITELISTED");
            } else {
                snprintf(response, sizeof(response), "ERR 500 EXEC_FAILED %s\n", SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "ERR 500 EXEC_FAILED");
            }
            continue;
        }

        /* 8. Handle PUT <filename> <size> command */
        if (strncmp(cmd, "PUT", 3) == 0 && (cmd[3] == ' ' || cmd[3] == '\0')) {
            char fname[256] = {0};
            long fsize = -1;

            if (sscanf(cmd + 3, "%255s %ld", fname, &fsize) != 2) {
                snprintf(response, sizeof(response), "ERR 400 USAGE_PUT_FILENAME_SIZE %s\n", SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "ERR 400 USAGE_PUT_FILENAME_SIZE");
                continue;
            }

            if (!is_safe_filename(fname)) {
                snprintf(response, sizeof(response), "ERR 403 INVALID_FILENAME_PATH_TRAVERSAL_BLOCKED %s\n", SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "ERR 403 PATH_TRAVERSAL_BLOCKED");
                continue;
            }

            if (fsize < 0 || fsize > MAX_UPLOAD_BYTES) {
                snprintf(response, sizeof(response), "ERR 413 FILE_TOO_LARGE_MAX_660KB(%d_BYTES) %s\n",
                         MAX_UPLOAD_BYTES, SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "ERR 413 FILE_TOO_LARGE_MAX_660KB");
                continue;
            }

            ensure_upload_dir();
            char dest_path[512];
            snprintf(dest_path, sizeof(dest_path), "%s/%s", UPLOAD_DIR, fname);

            FILE *out_fp = fopen(dest_path, "wb");
            if (!out_fp) {
                snprintf(response, sizeof(response), "ERR 500 CANNOT_CREATE_FILE %s\n", SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "ERR 500 CANNOT_CREATE_FILE");
                continue;
            }

            /* Send READY handshake to tell Controller to stream raw file bytes */
            snprintf(response, sizeof(response), "READY %s\n", SID_TAG);
            send(client_fd, response, strlen(response), 0);

            long remaining = fsize;
            char file_buf[BUFFER_SIZE];
            int transfer_ok = 1;

            while (remaining > 0) {
                size_t to_read = (remaining < (long)sizeof(file_buf)) ? (size_t)remaining : sizeof(file_buf);
                ssize_t got = recv(client_fd, file_buf, to_read, 0);
                if (got <= 0) {
                    transfer_ok = 0;
                    break;
                }
                fwrite(file_buf, 1, (size_t)got, out_fp);
                remaining -= got;
            }
            fclose(out_fp);

            if (transfer_ok) {
                snprintf(response, sizeof(response), "OK PUT saved=%s bytes=%ld %s\n",
                         dest_path, fsize, SID_TAG);
                send(client_fd, response, strlen(response), 0);
                log_event(client_ip, client_port, cmd, "OK PUT");
            } else {
                log_event(client_ip, client_port, cmd, "ERR 500 UPLOAD_INTERRUPTED");
                break;
            }
            continue;
        }

        /* Unknown command */
        snprintf(response, sizeof(response), "ERR 400 UNKNOWN_COMMAND %s\n", SID_TAG);
        send(client_fd, response, strlen(response), 0);
        log_event(client_ip, client_port, cmd, "ERR 400 UNKNOWN_COMMAND");
    }

    printf("[Agent] Controller %s:%d disconnected.\n", client_ip, client_port);
    log_event(client_ip, client_port, "DISCONNECT", "SESSION_CLOSED");
    close(client_fd);
    return NULL;
}

int main(void) {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int opt = 1;

    signal(SIGPIPE, SIG_IGN);
    ensure_upload_dir();

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(AGENT_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("[Agent IT24101660] Multi-threaded Server listening on TCP port %d (%s)...\n",
           AGENT_PORT, SID_TAG);

    while (1) {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }

        client_session_t *session = malloc(sizeof(client_session_t));
        if (!session) {
            perror("malloc failed");
            close(client_fd);
            continue;
        }
        session->sockfd = client_fd;
        session->addr = client_addr;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, session) != 0) {
            perror("pthread_create failed");
            free(session);
            close(client_fd);
            continue;
        }
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}
