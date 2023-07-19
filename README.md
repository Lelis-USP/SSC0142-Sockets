# SSC0142 - Sockets

Vídeo: [Google Drive](https://drive.google.com/file/d/1BXir8GyO-WdFA-DixngbRZ-pYPUC_MZa/view?usp=sharing)

## Membros

- Daniel H. Lelis - 12543822
- Eduardo Souza Rocha - 11218692
- João Francisco CBC de Pinho - 10748500
- Olavo Morais Borges Pereira - 11297792

## Instruções

Os módulos foram implementado de maneira iterativa, mas, por conveniência, estão
separados em diferentes pastas na raiz deste repositório. A versão final e mais
atualizada seria o `Module 3-Extra` que inclui a implementação do módulo 3 junto à
funcionalidade extra de canais somente por convite.

Para compilar o código, é recomendado o uso do CMake, mas também é possível executar
o Makefile na raiz de cada módulo.

### Compilação - CMake

Tendo o CMake instalado (disponível nos repositórios de praticamente toda distribuição),
basta executar os seguintes comandos dentro do módulo desejado:

```bash
# Cria o diretório ondes os arquivos de compilação serão gerados
mkdir build

# Gera os arquivos de compilação
cmake ..

# Compila o código
make

# Executa o programa
./server [porta] [ip]
./client [porta] [ip]
```

### Compilação - Makefile

Tendo o GNU Make instalado (disponível nos repositórios de praticamente toda distribuição),
basta executar os seguintes comandos dentro do módulo desejado:

```bash
# Limpa arquivos de build antigos
make clean

# Compila o client
make client

# Compila o server
make server


# Executa o programa
./server [porta] [ip]
./client [porta] [ip]
```

## Comandos implementados

Module 2:

- `/connect`: Conecta o usuário ao servidor
- `/quit`: Desconecta o usuário do servidor e encerra a aplicação
- `/ping`: O servidor responde `pong` para o cliente

Module 3:

- `/join <channel>`: O usuário tenta entrar no canal `<channel>`
- `/nick <nickname>`: Altera o nickname do usuário
- `/kick <nickname>`: O usuário tenta expulsar o usuário `<nickname>` do canal
- `/mute <nickname>`: O usuário tenta silenciar o usuário `<nickname>` do canal
- `/unmute <nickname>`: O usuário tenta remover o silenciamento do usuário `<nickname>` do canal
- `/whois <nickname>`: O usuário tenta obter o IP do usuário `<nickname>`

Module 3 - Extra:

- `/mode <+|->i`: O usuário tenta alterar o modo do canal entre somente por convite (`+i`) e aberto (`-i`)
- `/invite <nickname>`: O usuário tenta convidar o usuário `<nickname>` para o canal
