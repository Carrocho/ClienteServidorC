// servidor.c
// Compilar com: gcc servidor.c -o meu_servidor
// Executar: ./meu_servidor /home/usuario/meusite
// Acessar em: http://localhost:8080/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>z

#define PORT 8080
#define BUF_SIZE 4096

void send_response(int client, const char *status, const char *content_type, const char *body) {
    char header[1024];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n",
             status, content_type, strlen(body));
    send(client, header, strlen(header), 0);
    send(client, body, strlen(body), 0);
}

void send_file(int client, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        const char *msg = "<h1>404 Not Found</h1>";
        send_response(client, "404 Not Found", "text/html", msg);
        return;
    }

    // Determina o tipo do arquivo (bem básico)
    const char *ext = strrchr(filepath, '.');
    const char *type = "application/octet-stream";
    if (ext) {
        if (strcmp(ext, ".html") == 0) type = "text/html";
        else if (strcmp(ext, ".jpg") == 0) type = "image/jpeg";
        else if (strcmp(ext, ".png") == 0) type = "image/png";
        else if (strcmp(ext, ".gif") == 0) type = "image/gif";
        else if (strcmp(ext, ".txt") == 0) type = "text/plain";
    }

    // Envia cabeçalho
    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n", type, filesize);
    send(client, header, strlen(header), 0);

    // Envia conteúdo
    char buffer[BUF_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, BUF_SIZE, file)) > 0) {
        send(client, buffer, bytes, 0);
    }

    fclose(file);
}

void list_directory(int client, const char *dirpath) {
    DIR *dir = opendir(dirpath);
    if (!dir) {
        const char *msg = "<h1>403 Forbidden</h1>";
        send_response(client, "403 Forbidden", "text/html", msg);
        return;
    }

    char body[8192];
    snprintf(body, sizeof(body), "<html><body><h1>Índice de %s</h1><ul>", dirpath);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        strcat(body, "<li><a href=\"");
        strcat(body, entry->d_name);
        strcat(body, "\">");
        strcat(body, entry->d_name);
        strcat(body, "</a></li>");
    }
    closedir(dir);

    strcat(body, "</ul></body></html>");
    send_response(client, "200 OK", "text/html", body);
}

void handle_client(int client, const char *base_dir) {
    char buffer[BUF_SIZE];
    int len = recv(client, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        close(client);
        return;
    }
    buffer[len] = '\0';

    // Lê o método e caminho
    char method[8], path[1024];
    sscanf(buffer, "%s %s", method, path);

    if (strcmp(method, "GET") != 0) {
        const char *msg = "<h1>405 Method Not Allowed</h1>";
        send_response(client, "405 Method Not Allowed", "text/html", msg);
        close(client);
        return;
    }

    // Monta caminho absoluto
    char fullpath[2048];
    snprintf(fullpath, sizeof(fullpath), "%s%s", base_dir, path);

    struct stat st;
    if (stat(fullpath, &st) == -1) {
        const char *msg = "<h1>404 Not Found</h1>";
        send_response(client, "404 Not Found", "text/html", msg);
    } else if (S_ISDIR(st.st_mode)) {
        // Se for diretório, tenta index.html
        char indexpath[PATH_MAX];
        snprintf(indexpath, sizeof(indexpath), "%s/index.html", fullpath);
        if (stat(indexpath, &st) == 0) {
            send_file(client, indexpath);
        } else {
            list_directory(client, fullpath);
        }
    } else {
        send_file(client, fullpath);
    }

    close(client);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <diretorio_base>\n", argv[0]);
        exit(1);
    }

    const char *base_dir = argv[1];
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Cria socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Erro ao criar socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro no bind");
        close(server_fd);
        exit(1);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Erro no listen");
        close(server_fd);
        exit(1);
    }

    printf("Servidor rodando em http://localhost:%d/\n", PORT);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Erro no accept");
            continue;
        }
        handle_client(client_fd, base_dir);
    }

    close(server_fd);
    return 0;
}
