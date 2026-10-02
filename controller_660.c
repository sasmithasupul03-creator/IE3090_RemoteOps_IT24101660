#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define DEFAULT_PORT 9410
#define BUFFER_SIZE 4096

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
            if (n == 0) return 0;
            break;
        } else {
            return -1;
        }
    }
    buffer[n] = '\0';
    return (ssize_t)n;
}

int main(int argc, char *argv[]) {
    const char *server_ip = (argc >= 2) ? argv[1] : "127.0.0.1";
    int server_port = (argc >= 3) ? atoi(argv[2]) : DEFAULT_PORT;

    int sockfd;
    struct sockaddr_in server_addr;

    /* 1. Create TCP socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    /* 2. Connect to Agent server */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", server_ip);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("[Controller IT24101660] Connected to Agent at %s:%d\n", server_ip, server_port);

    char input[BUFFER_SIZE];
    char reply[BUFFER_SIZE];

    /* 3. Command loop */
    while (1) {
        printf("RemoteOps> ");
        fflush(stdout);
        if (fgets(input, sizeof(input), stdin) == NULL) break;

        /* Ensure newline framing */
        size_t len = strlen(input);
        if (len == 0 || (len == 1 && input[0] == '\n')) continue;

        send(sockfd, input, len, 0);

        if (read_line(sockfd, reply, sizeof(reply)) <= 0) {
            printf("[Controller] Agent closed the connection.\n");
            break;
        }

        printf("%s\n", reply);

        if (strncmp(input, "QUIT", 4) == 0) {
            break;
        }
    }

    close(sockfd);
    return 0;
}
