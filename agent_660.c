#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define AGENT_PORT 9410
#define SID_TAG "SID:0661"
#define BUFFER_SIZE 4096

/* Structure to pass client details to each worker thread */
typedef struct {
    int sockfd;
    struct sockaddr_in addr;
} client_session_t;

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

    char cmd[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    while (read_line(client_fd, cmd, sizeof(cmd)) > 0) {
        printf("[Agent %s:%d] Received: %s\n", client_ip, client_port, cmd);

        if (strcmp(cmd, "QUIT") == 0) {
            snprintf(response, sizeof(response), "OK BYE %s\n", SID_TAG);
            send(client_fd, response, strlen(response), 0);
            break;
        } else {
            snprintf(response, sizeof(response), "ERR 000 NOT_IMPLEMENTED_YET %s\n", SID_TAG);
            send(client_fd, response, strlen(response), 0);
        }
    }

    printf("[Agent] Controller %s:%d disconnected.\n", client_ip, client_port);
    close(client_fd);
    return NULL;
}

int main(void) {
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int opt = 1;

    /* Ignore SIGPIPE so abrupt client disconnects do not crash the Agent */
    signal(SIGPIPE, SIG_IGN);

    /* 1. Create TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* 2. Bind socket to personalised port 9410 */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(AGENT_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* 3. Listen for incoming Controller connections */
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("[Agent IT24101660] Multi-threaded Server listening on TCP port %d (%s)...\n",
           AGENT_PORT, SID_TAG);

    /* 4. Concurrent accept loop spawning a detached pthread per Controller */
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
