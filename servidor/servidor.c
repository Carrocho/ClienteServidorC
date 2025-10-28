#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>

#define PORT 8080           // Porta padrão do servidor
#define BUF_SIZE 8192       // Tamanho do buffer para leitura/escrita

// Envia uma resposta HTTP simples com um corpo em texto
void send_response(int client, const char *status, const char *content_type, const char *body) {
    char header[1024];
    // Monta o cabeçalho HTTP com status, tipo e comprimento do corpo
    snprintf(header, sizeof(header),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n",
             status, content_type, strlen(body));
    
    // Envia o cabeçalho e o corpo
    send(client, header, strlen(header), 0);
    send(client, body, strlen(body), 0);
}

// Envia um arquivo binário/texto como resposta HTTP (cabeçalho + conteúdo)
void send_file(int client, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        // Se não conseguiu abrir o arquivo, retorna 404
        const char *msg = "<h1>404 Not Found</h1>";
        send_response(client, "404 Not Found", "text/html", msg);
        return;
    }

    // Determina a extensão e escolhe um tipo MIME simples
    const char *ext = strrchr(filepath, '.');
    const char *type = "application/octet-stream";
    
    if (ext) {
        if (strcmp(ext, ".html") == 0) type = "text/html";
        else if (strcmp(ext, ".jpg") == 0) type = "image/jpeg";
        else if (strcmp(ext, ".png") == 0) type = "image/png";
        else if (strcmp(ext, ".gif") == 0) type = "image/gif";
        else if (strcmp(ext, ".webp") == 0) type = "image/webp";
        else if (strcmp(ext, ".txt") == 0) type = "text/plain";
        else if (strcmp(ext, ".mp3") == 0) type = "audio/mpeg";
        else if (strcmp(ext, ".mp4") == 0) type = "video/mp4";
        else if (strcmp(ext, ".webm") == 0) type = "video/webm";
        else if (strcmp(ext, ".pdf") == 0) type = "application/pdf";
        else if (strcmp(ext, ".doc") == 0) type = "application/msword";
        else if (strcmp(ext, ".docx") == 0) type = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    }

    // Obtém o tamanho do arquivo para colocar em Content-Length
    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Monta e envia o cabeçalho HTTP indicando 200 OK
    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Connection: close\r\n"
             "\r\n", type, filesize);
    send(client, header, strlen(header), 0);

    // Envia o conteúdo do arquivo em blocos até o fim
    char buffer[BUF_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, BUF_SIZE, file)) > 0) {
        send(client, buffer, bytes, 0);
    }

    fclose(file); // Fecha o arquivo
}

// Gera uma listagem HTML do diretório passado e envia ao cliente
void list_directory(int client, const char *dirpath, const char *base_url) {
    DIR *dir = opendir(dirpath);
    if (!dir) {
        // Se não pode abrir o diretório (permissão, inexistente), retorna 403
        const char *msg = "<h1>403 Forbidden</h1>";
        send_response(client, "403 Forbidden", "text/html", msg);
        return;
    }

    // Buffer para construir o corpo HTML; cuidado: tamanho fixo pode ser insuficiente
    char body[8192];
    snprintf(body, sizeof(body), "<html><body><h1>Caminho: %s</h1><ul>", base_url);

    struct dirent *entry;
    // Itera sobre cada entrada do diretório
    while ((entry = readdir(dir)) != NULL) {
        // Ignora "." e ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        // Monta a URL que será usada no link
        char full_url[1024];
        if (strcmp(base_url, "/") == 0) {
            // Se estamos na raiz, evita // no caminho
            snprintf(full_url, sizeof(full_url), "/%s", entry->d_name);
        } else {
            snprintf(full_url, sizeof(full_url), "%s/%s", base_url, entry->d_name);
        }
        
        // Monta o caminho no filesystem para verificar se a entrada é diretório
        char full_fs_path[1024];
        snprintf(full_fs_path, sizeof(full_fs_path), "%s/%s", dirpath, entry->d_name);
        
        struct stat st;
        int is_dir = 0;
        if (stat(full_fs_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            is_dir = 1;
            // Adiciona barra à URL para diretórios (boa prática)
            strcat(full_url, "/");
        }
        
        // Adiciona um item <li> com link para o body
        strcat(body, "<li><a href=\"");
        strcat(body, full_url);
        strcat(body, "\">");
        strcat(body, entry->d_name);
        if (is_dir) {
            strcat(body, "/");
        }
        strcat(body, "</a></li>");
    }
    closedir(dir);

    strcat(body, "</ul></body></html>");
    send_response(client, "200 OK", "text/html", body);
}

// Processa uma conexão cliente: lê requisição, decide o que servir e fecha conexão
void handle_client(int client, const char *base_dir) {
    char buffer[BUF_SIZE];
    // Recebe dados do cliente (requisição HTTP)
    int len = recv(client, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        close(client);
        return;
    }
    buffer[len] = '\0';  // Garante terminação da string

    // Extrai método e caminho (ex: "GET /index.html HTTP/1.1")
    char method[8], path[1024];
    sscanf(buffer, "%s %s", method, path);

    // Só aceita GET neste servidor simples
    if (strcmp(method, "GET") != 0) {
        const char *msg = "<h1>405 Method Not Allowed</h1>";
        send_response(client, "405 Method Not Allowed", "text/html", msg);
        close(client);
        return;
    }

    // Constrói o caminho completo (diretório base + caminho da URL)
    char fullpath[2048];
    snprintf(fullpath, sizeof(fullpath), "%s%s", base_dir, path);

    // Limpa a barra final para exibir corretamente na listagem ("/pasta/" -> "/pasta")
    char clean_path[1024];
    strcpy(clean_path, path);
    if (strlen(clean_path) > 1 && clean_path[strlen(clean_path)-1] == '/') {
        clean_path[strlen(clean_path)-1] = '\0';
    }

    // Obtém informações do arquivo/diretório
    struct stat st;
    if (stat(fullpath, &st) == -1) {
        // Não encontrou arquivo/diretório solicitado
        const char *msg = "<h1>404 Not Found</h1>";
        send_response(client, "404 Not Found", "text/html", msg);
    } else if (S_ISDIR(st.st_mode)) {
        // Se for diretório, tenta servir index.html dentro dele
        char indexpath[PATH_MAX];
        snprintf(indexpath, sizeof(indexpath), "%s/index.html", fullpath);
        
        if (stat(indexpath, &st) == 0) {
            // Se index.html existe, serve o arquivo
            send_file(client, indexpath);
        } else {
            // Caso contrário, gera a listagem do diretório
            list_directory(client, fullpath, clean_path);
        }
    } else {
        // Se é arquivo regular, envia o arquivo
        send_file(client, fullpath);
    }

    close(client);  // Fecha a conexão
}

int main(int argc, char *argv[]) {

    // Ignora SIGPIPE para evitar término do processo se escrever em socket fechado
    signal(SIGPIPE, SIG_IGN);

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <diretorio_base>\n", argv[0]);
        exit(1);
    }

    const char *base_dir = argv[1];  // Diretório base para servir arquivos
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Cria socket TCP (IPv4)
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Erro ao criar socket");
        exit(1);
    }

    // Permite reutilizar o endereço rapidamente ao reiniciar o servidor
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Preenche estrutura de endereço do servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // aceita de todas interfaces
    server_addr.sin_port = htons(PORT);

    // Associa (bind) o socket ao endereço/porta
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro no bind");
        close(server_fd);
        exit(1);
    }

    // Escuta conexões (backlog 10)
    if (listen(server_fd, 10) < 0) {
        perror("Erro no listen");
        close(server_fd);
        exit(1);
    }

    printf("Servidor rodando em: http://localhost:%d/\n", PORT);
    printf("\nServindo arquivos da pasta: %s\n", base_dir);

    // Loop principal: aceita conexões e processa uma por uma (modelo single-thread)
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Erro no accept");
            continue;  // Em caso de erro, tenta aceitar próxima conexão
        }
        
        handle_client(client_fd, base_dir);
    }

    // Nunca alcançado neste código, mas bom fechar o socket se sair do loop
    close(server_fd);
    return 0;
}
