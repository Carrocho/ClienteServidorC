/*
 * meu_navegador.c
 *
 * Compilar:
 *   gcc -std=c11 -O2 -Wall -o meu_navegador meu_navegador.c
 *
 * Uso:
 *   ./meu_navegador http://www.exemplo.com/arquivo.jpg
 *   ./meu_navegador http://localhost:5050/imagem.pdf
 *
 * Limitações:
 * - Só HTTP (porta 80 ou porta indicada). Não faz HTTPS.
 * - Suporta Content-Length e Transfer-Encoding: chunked.
 * - Segue apenas uma única resposta (não segue redirects automaticamente).
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define RECV_BUF 8192

/* Estrutura para URL parseada */
typedef struct {
    char *host;
    char *port;
    char *path;
} URL;

/* Funções utilitárias */
static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static char *strdup_range(const char *s, size_t a, size_t b) {
    size_t len = b - a;
    char *r = malloc(len + 1);
    if (!r) die("malloc");
    memcpy(r, s + a, len);
    r[len] = '\0';
    return r;
}

/* Parse simples de URL. Retorna 0 se ok, -1 se inválida */
int parse_url(const char *url, URL *out) {
    /* Espera algo como: http://host[:port]/path... ou http://host[:port] */
    const char *p = url;
    const char *scheme = "http://";
    size_t scheme_len = strlen(scheme);

    if (strncmp(p, scheme, scheme_len) != 0) return -1;
    p += scheme_len;

    /* host (até ':' ou '/' ou fim) */
    const char *host_start = p;
    while (*p && *p != ':' && *p != '/') p++;
    if (p == host_start) return -1;
    size_t host_end = p - url;

    char *host = strdup_range(url, host_start - url, host_end);
    char *port = NULL;
    char *path = NULL;

    if (*p == ':') {
        p++; /* pula ':' */
        const char *port_start = p;
        while (*p && *p != '/') p++;
        size_t port_end = p - url;
        port = strdup_range(url, port_start - url, port_end);
    } else {
        /* porta padrão 80 */
        port = strdup("80");
    }

    if (*p == '/') {
        path = strdup(p);
    } else {
        path = strdup("/"); /* root */
    }

    out->host = host;
    out->port = port;
    out->path = path;
    return 0;
}

/* extrai filename do path ou do header Content-Disposition */
char *choose_filename(const char *path, const char *content_disposition) {
    /* primeiro tenta Content-Disposition: filename="..." */
    if (content_disposition) {
        const char *p = strstr(content_disposition, "filename=");
        if (p) {
            p += strlen("filename=");
            /* pode vir entre aspas ou sem */
            if (*p == '"' || *p == '\'') {
                char quote = *p++;
                const char *start = p;
                while (*p && *p != quote) p++;
                if (*p == quote) return strdup_range(start, 0, p - start);
            } else {
                const char *start = p;
                while (*p && *p != ';' && *p != ' ' && *p != '\r' && *p != '\n') p++;
                return strdup_range(start, 0, p - start);
            }
        }
    }

    /* se não tiver, pega última parte do path */
    const char *last = strrchr(path, '/');
    if (!last || *(last+1) == '\0') {
        return strdup("index.html");
    } else {
        return strdup(last + 1);
    }
}

/* lê uma linha terminada em \r\n da conexão (buffer interno) */
ssize_t recv_line(int fd, char *buf, size_t maxlen) {
    size_t i = 0;
    char c = 0;
    ssize_t n;
    while (i < maxlen - 1) {
        n = recv(fd, &c, 1, 0);
        if (n <= 0) {
            if (n == 0) break;
            return -1;
        }
        buf[i++] = c;
        if (i >= 2 && buf[i-2] == '\r' && buf[i-1] == '\n') break;
    }
    buf[i] = '\0';
    return i;
}

/* converte hex string para size_t */
size_t hex_to_size(const char *hex) {
    size_t v = 0;
    while (*hex && *hex != '\r' && *hex != '\n') {
        char c = *hex++;
        int val;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'a' && c <= 'f') val = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') val = 10 + (c - 'A');
        else break;
        v = (v << 4) | val;
    }
    return v;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s http://host[:port]/path\n", argv[0]);
        return EXIT_FAILURE;
    }

    URL url;
    if (parse_url(argv[1], &url) != 0) {
        fprintf(stderr, "URL inválida: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    /* Resolver host */
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(url.host, url.port, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai == EAI_SYSTEM ? strerror(errno) : gai_strerror(gai));
        return EXIT_FAILURE;
    }

    int sock = -1;
    struct addrinfo *rp;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) continue;
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);
    if (sock == -1) {
        fprintf(stderr, "falha ao conectar a %s:%s\n", url.host, url.port);
        return EXIT_FAILURE;
    }

    /* Monta e envia requisição */
    char request[4096];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: meu_navegador/1.0\r\n"
             "Accept: */*\r\n"
             "Connection: close\r\n"
             "\r\n",
             url.path, url.host);

    ssize_t sent = send(sock, request, strlen(request), 0);
    if (sent < 0) die("send");

    /* Ler status line */
    char line[RECV_BUF];
    ssize_t n = recv_line(sock, line, sizeof(line));
    if (n <= 0) {
        fprintf(stderr, "Falha ao ler resposta\n");
        close(sock);
        return EXIT_FAILURE;
    }

    /* status line: HTTP/1.1 200 OK\r\n */
    int status = 0;
    if (sscanf(line, "HTTP/%*s %d", &status) != 1) {
        fprintf(stderr, "Resposta inválida: %s\n", line);
        close(sock);
        return EXIT_FAILURE;
    }

    /* Ler cabeçalhos */
    char header_buf[RECV_BUF];
    size_t content_length = (size_t)-1;
    int chunked = 0;
    char *content_disposition = NULL;
    char *content_type = NULL;

    while (1) {
        ssize_t l = recv_line(sock, header_buf, sizeof(header_buf));
        if (l <= 0) {
            fprintf(stderr, "Erro lendo cabeçalhos\n");
            close(sock);
            return EXIT_FAILURE;
        }
        if (strcmp(header_buf, "\r\n") == 0) break; /* fim dos headers */

        /* converte pra minúsculas temporariamente para testar */
        char *lower = strdup(header_buf);
        for (char *p=lower; *p; ++p) *p = tolower((unsigned char)*p);

        if (strstr(lower, "content-length:") == lower) {
            const char *v = header_buf + strlen("Content-Length:");
            while (*v == ' ' || *v == '\t') v++;
            content_length = (size_t)strtoull(v, NULL, 10);
        } else if (strstr(lower, "transfer-encoding:") == lower) {
            if (strstr(lower, "chunked")) chunked = 1;
        } else if (strstr(lower, "content-disposition:") == lower) {
            /* salva linha original */
            free(content_disposition);
            content_disposition = strdup(header_buf + strlen("Content-Disposition:"));
            /* trim */
            while (*content_disposition == ' ' || *content_disposition == '\t') memmove(content_disposition, content_disposition+1, strlen(content_disposition));
        } else if (strstr(lower, "content-type:") == lower) {
            free(content_type);
            content_type = strdup(header_buf + strlen("Content-Type:"));
            while (*content_type == ' ' || *content_type == '\t') memmove(content_type, content_type+1, strlen(content_type));
        }

        free(lower);
    }

    if (status != 200) {
        fprintf(stderr, "Servidor retornou status %d\n", status);
        close(sock);
        free(url.host); free(url.port); free(url.path);
        free(content_disposition); free(content_type);
        return EXIT_FAILURE;
    }

    /* Decide nome do arquivo */
    char *filename = choose_filename(url.path, content_disposition);

    printf("Salvando em: %s\n", filename);
    FILE *out = fopen(filename, "wb");
    if (!out) die("fopen");

    /* Ler o corpo: chunked ou content-length ou leitura até EOF (connection: close) */
    if (chunked) {
        /* ler repetidamente tamanho do chunk em hex (linha), depois ler bytes */
        char chunk_line[256];
        while (1) {
            ssize_t rl = recv_line(sock, chunk_line, sizeof(chunk_line));
            if (rl <= 0) { fprintf(stderr, "Erro lendo chunk size\n"); break; }
            /* chunk_line tem trailing \r\n */
            size_t chunk_size = hex_to_size(chunk_line);
            if (chunk_size == 0) {
                /* consumir possível trailers até linha vazia */
                while (1) {
                    ssize_t t = recv_line(sock, chunk_line, sizeof(chunk_line));
                    if (t <= 0 || strcmp(chunk_line, "\r\n") == 0) break;
                }
                break;
            }
            size_t remaining = chunk_size;
            char buf[RECV_BUF];
            while (remaining > 0) {
                ssize_t toread = remaining > sizeof(buf) ? sizeof(buf) : remaining;
                ssize_t r = recv(sock, buf, toread, 0);
                if (r <= 0) { fprintf(stderr, "Erro lendo chunk body\n"); goto cleanup; }
                fwrite(buf, 1, r, out);
                remaining -= r;
            }
            /* ler o \r\n que segue o chunk */
            char crlf[2];
            ssize_t cr = recv(sock, crlf, 2, 0);
            if (cr != 2) { fprintf(stderr, "Erro lendo CRLF pós chunk\n"); goto cleanup; }
        }
    } else if (content_length != (size_t)-1) {
        size_t remaining = content_length;
        char buf[RECV_BUF];
        while (remaining > 0) {
            ssize_t toread = remaining > sizeof(buf) ? sizeof(buf) : remaining;
            ssize_t r = recv(sock, buf, toread, 0);
            if (r < 0) { perror("recv"); break; }
            if (r == 0) break;
            fwrite(buf, 1, r, out);
            remaining -= r;
        }
    } else {
        /* sem content-length e sem chunked: ler até EOF */
        char buf[RECV_BUF];
        ssize_t r;
        while ((r = recv(sock, buf, sizeof(buf), 0)) > 0) {
            fwrite(buf, 1, r, out);
        }
    }

cleanup:
    fclose(out);
    close(sock);
    free(url.host); free(url.port); free(url.path);
    free(content_disposition);
    free(content_type);

    printf("Concluído.\n");
    return EXIT_SUCCESS;
}
