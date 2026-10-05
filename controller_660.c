#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>

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

void print_help(void) {
    printf("Available RemoteOps Commands (IT24101660):\n");
    printf("  AUTH <token>        - Authenticate session (Token: OPS-1660)\n");
    printf("  STATUS              - View real-time CPU load, memory, and uptime\n");
    printf("  PROC [count]        - List running processes from /proc (1-50)\n");
    printf("  INFO                - View hostname, kernel, arch, and OS details\n");
    printf("  EXEC <cmd>          - Run whitelisted cmd (date, whoami, uname -a, df -h, uptime)\n");
    printf("  PUT <local_file>    - Upload local file to agentfiles/IT24101660/ (Max 660 KB)\n");
    printf("  PUT <name> <size>   - Raw protocol test for path traversal / size limits\n");
    printf("  HELP                - Show this command menu\n");
    printf("  QUIT                - Close session and exit\n");
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <agent_ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    int sockfd;
    struct sockaddr_in server_addr;
    char input[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
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

    printf("[Controller IT24101660] Connected to Agent at %s:%d (Type HELP for commands)\n",
           server_ip, port);

    while (1) {
        printf("RemoteOps> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        input[strcspn(input, "\r\n")] = '\0';

        if (strlen(input) == 0) continue;

        if (strcmp(input, "HELP") == 0 || strcmp(input, "help") == 0) {
            print_help();
            continue;
        }

        /* Handle PUT <local_file> (auto-detect size and stream bytes) */
        if (strncmp(input, "PUT ", 4) == 0) {
            char arg1[256] = {0};
            char arg2[64] = {0};
            int num_args = sscanf(input + 4, "%255s %63s", arg1, arg2);

            /* If user typed 'PUT <local_file>' (1 argument), read local file and send bytes */
            if (num_args == 1) {
                struct stat st;
                if (stat(arg1, &st) != 0) {
                    printf("[Controller] Local file '%s' not found.\n", arg1);
                    continue;
                }
                FILE *fp = fopen(arg1, "rb");
                if (!fp) {
                    perror("fopen local file");
                    continue;
                }

                /* Extract base filename if path was provided */
                const char *base = strrchr(arg1, '/');
                base = base ? (base + 1) : arg1;

                char header[512];
                snprintf(header, sizeof(header), "PUT %s %ld\n", base, (long)st.st_size);
                send(sockfd, header, strlen(header), 0);

                if (read_line(sockfd, response, sizeof(response)) <= 0) {
                    printf("[Controller] Connection closed by Agent.\n");
                    fclose(fp);
                    break;
                }

                if (strncmp(response, "READY", 5) != 0) {
                    printf("%s\n", response);
                    fclose(fp);
                    continue;
                }

                /* Agent is READY: stream raw file bytes */
                char fbuf[BUFFER_SIZE];
                size_t nread;
                while ((nread = fread(fbuf, 1, sizeof(fbuf), fp)) > 0) {
                    send(sockfd, fbuf, nread, 0);
                }
                fclose(fp);

                if (read_line(sockfd, response, sizeof(response)) > 0) {
                    printf("%s\n", response);
                }
                continue;
            }
            /* If user typed 'PUT <filename> <size>' (2 arguments, e.g., security test), send header directly */
        }

        char send_buf[BUFFER_SIZE + 4];
        snprintf(send_buf, sizeof(send_buf), "%s\n", input);
        if (send(sockfd, send_buf, strlen(send_buf), 0) < 0) {
            perror("send failed");
            break;
        }

        ssize_t n = read_line(sockfd, response, sizeof(response));
        if (n <= 0) {
            printf("[Controller] Connection closed by Agent.\n");
            break;
        }

        printf("%s\n", response);

        if (strcmp(input, "QUIT") == 0) {
            break;
        }
    }

    close(sockfd);
    return 0;
}
