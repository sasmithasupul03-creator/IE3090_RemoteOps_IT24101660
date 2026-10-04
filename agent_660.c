#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define AGENT_PORT 9410
#define SID_TAG "SID:0661"
#define AUTH_TOKEN "OPS-1660"
#define LOG_FILE "remoteops_IT24101660.log"
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

    /* 1. Read /proc/uptime */
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) return -1;
    if (fscanf(fp, "%lf", &uptime_sec) != 1) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* 2. Read /proc/loadavg */
    fp = fopen("/proc/loadavg", "r");
    if (!fp) return -1;
    if (fscanf(fp, "%lf %lf %lf", &load1, &load5, &load15) != 3) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* 3. Read /proc/meminfo */
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

        /* Placeholder for remaining authenticated commands */
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
