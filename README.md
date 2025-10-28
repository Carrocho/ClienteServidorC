# Cliente

Cliente http feito em C, onde baixa arquivos de uma determinada url (apenas http).

Faz a conexão TCP com o servidor remoto, envia a requisição, lê a resposta e salva o conteúdo (corpo) em uma pasta local chamada "arquivos".


# Servidor
Servidor http feito em C, onde permite acessar diretórios e arquivos via navegador.

Retorna páginas html, imagens, vídeos e outros arquivos comuns, exibe o conteúdo das pastas como uma listagem em html e tenta servir automaticamente um arquivo index.html caso houver.

# Como executar

### Cliente
```py
make # Cria um arquivo chamado "meu_navegador"

make run # Executa o cliente com um site de exemplo "http://example.com/"

./meu_navegador url_http # Substitua "url_http" para o site que você quer acessar
```

### Servidor
```py
make # Cria um arquivo chamado "meu_servidor"

make run # Executa o servidor com uma pasta de exemplo "servidorTeste"

./meu_servidor caminho_pasta # Substitua "caminho_pasta" com o caminho do diretório que deseja hospedar.
```

