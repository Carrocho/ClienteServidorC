#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/stat.h>

void create_directory(const char *dirname) {
    struct stat st = {0};
    if (stat(dirname, &st) == -1) {
        mkdir(dirname, 0755);
    }
}

char *get_filename(const char *url_path) {
    const char *slash = strrchr(url_path, '/');
    if (slash && *(slash + 1) != '\0')
        return strdup(slash + 1);
    else
        return strdup("index.html");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <URL>\n", argv[0]);
        return 1;
    }

    char *url = argv[1];
    char host[256], path[1024], port[10] = "80";

    // ---- Parse simples da URL ----
    if (strncmp(url, "http://", 7) == 0)
        url += 7;

    char *slash = strchr(url, '/');
    if (slash) {
        strncpy(host, url, slash - url);
        host[slash - url] = '\0';
        strcpy(path, slash);
    } else {
        strcpy(host, url);
        strcpy(path, "/");
    }

    // Porta (caso tenha, ex: site.com:8080)
    char *portaHost = strchr(host, ':');
    if (portaHost) {
        strcpy(port, portaHost + 1);
        *portaHost = '\0';
    }

    // ---- Resolve DNS ----
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(host, port, &hints, &res);
    if (status != 0) {
        fprintf(stderr, "Erro em getaddrinfo: %s\n", gai_strerror(status));
        return 1;
    }

    // ---- Cria e conecta o socket ----
    int sockfd = -1;
    struct addrinfo *p;
    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) continue;
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }
        break;
    }

    freeaddrinfo(res);

    if (!p) {
        fprintf(stderr, "Falha ao conectar em %s:%s\n", host, port);
        return 1;
    }

    // ---- Envia requisição GET ----
    char request[1024];
    int req_len = snprintf(request, sizeof(request),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "Connection: close\r\n\r\n",
                           path, host);

    if (send(sockfd, request, req_len, 0) == -1) {
        perror("Erro em send()");
        close(sockfd);
        return 1;
    }

    // ---- Cria pasta "Arquivos" ----
    create_directory("arquivos");

    // ---- Recebe resposta ----
    char buffer[4096];
    ssize_t bytes_received;
    int header_skipped = 0;
    FILE *fp = NULL;
    char *body_start = NULL;

    while ((bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';

        if (!header_skipped) {
            body_start = strstr(buffer, "\r\n\r\n");
            if (body_start) {
                *body_start = '\0';
                printf("\n%s\n\n", buffer);

                if (strncmp(buffer, "HTTP/1.1 200", 12) == 0 ||
                    strncmp(buffer, "HTTP/1.0 200", 12) == 0) {
                    char *filename = get_filename(path);
                    char filepath[512];
                    snprintf(filepath, sizeof(filepath), "arquivos/%s", filename);
                    free(filename);

                    fp = fopen(filepath, "wb");
                    if (!fp) {
                        perror("Erro ao criar arquivo");
                        close(sockfd);
                        return 1;
                    }

                    body_start += 4; // pula \r\n\r\n
                    fwrite(body_start, 1, bytes_received - (body_start - buffer), fp);
                    header_skipped = 1;
                } else {
                    printf("Status não é 200 OK, ignorando corpo.\n");
                    break;
                }
            }
        } else {
            fwrite(buffer, 1, bytes_received, fp);
        }
    }

    if (fp) {
        fclose(fp);
        printf("Arquivo salvo na pasta 'arquivos'.\n");
    }

    close(sockfd);
    return 0;
}
