#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <set>
#include <arpa/inet.h>
#include <cstring>
#include <string>

int send_string(int s, const char *sString);

void sendFile(char *filePath, int sock);

#define HTTP_VERSION "HTTP/1.0"

int main() {
    int listener;
    struct sockaddr_in addr;
    char buf[1024];
    int bytes_read;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        exit(1);
    }

    std::string LOGIN = "LOG\n", PASSWORD = "PAS\n";

    fcntl(listener, F_SETFL, O_NONBLOCK); // сделать сокет неблокирующим

    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(listener, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(2);
    }
    socklen_t len = sizeof(addr);
    if (getsockname(listener, (sockaddr *) &addr, &len) == -1) {
        perror("getsockname");
    } else {
        printf("Listening port %d...\n", ntohs(addr.sin_port));
    }
    listen(listener, 2);


    std::set<int> clients;
    clients.clear();

    while (true) {
        // Заполняем множество сокетов
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(listener, &readset);

        for (auto it = clients.begin(); it != clients.end(); it++)
            FD_SET(*it, &readset);

        // Задаём таймаут
        timeval timeout;
        timeout.tv_sec = 500;
        timeout.tv_usec = 0;

        // Ждём события в одном из сокетов
        int mx = std::max(listener, *max_element(clients.begin(), clients.end()));
        if (select(mx + 1, &readset, nullptr, nullptr, &timeout) <= 0) {
            perror("select");
            exit(3);
        }

        // Определяем тип события и выполняем соответствующие действия
        if (FD_ISSET(listener, &readset)) {
            // Поступил новый запрос на соединение, используем accept
            sockaddr_in clnt_addr;
            int cl_addrlen = sizeof(clnt_addr);
            int sock = accept(listener, (sockaddr *) &clnt_addr, (socklen_t *) &cl_addrlen);
            if (sock < 0) {
                perror("accept");
                exit(3);
            }
            // Получаем параметры присоединенного сокета NS и
            // информацию о клиенте
//            sockaddr_in * serv_addr;
            int addrlen = sizeof(addr);
            getsockname(sock, (sockaddr *) &addr, (socklen_t *) &cl_addrlen);
            // Функция inet_ntoa возвращает указатель на глобальный буффер,
            // поэтому использовать ее в одном вызове printf не получится
            printf("Accepted connection on %s:%d ",
                   inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
            printf("from client %s:%d\n",
                   inet_ntoa(clnt_addr.sin_addr), ntohs(clnt_addr.sin_port));

            fcntl(sock, F_SETFL, O_NONBLOCK);

            clients.insert(sock);
        }
        char sReceiveBuffer[1024] = {0};

        for (auto it = clients.begin(); it != clients.end(); it++) {
            if (FD_ISSET(*it, &readset)) {
                // Поступили данные от клиента, читаем их
                bytes_read = recv(*it, sReceiveBuffer, 1024, 0);

                if (bytes_read <= 0) {
                    // Соединение разорвано, удаляем сокет из множества
                    close(*it);
                    clients.erase(*it);
                    continue;
                }
                // GET REQUEST ≤HTTP/1.0
                printf("Received data: %s\n", sReceiveBuffer);
                if (strncmp(sReceiveBuffer, "GET ", 4) == 0) {
                    char *cur = sReceiveBuffer + 4;
                    char *nextSpace = strchr(cur, ' ');
                    if (!nextSpace) {
                        send_string(*it, "HTTP/1.0 501 Not Implemented\nServer: http_trynb\n");
                    } else {
                        if (strncmp(nextSpace + 1, HTTP_VERSION, 8) != 0) {
                            send_string(*it, "HTTP/1.0 505 HTTP Version Not Supported\nServer: "
                                             "http_trynb\n");
                        } else if (strchr(nextSpace + 9, ' ')) {
                            send_string(*it, "HTTP/1.0 501 Not Implemented\n"
                                             "Server: http_trynb\n");
                        } else {
                            auto pathLength = nextSpace - cur; // длина пути
                            if (pathLength > 50) {
                                send_string(*it, "HTTP/1.0 414 Request-URI Too Large"
                                                 " Server: http_trynb\n");
                            } else {
                                char pathBuf[1024];
                                strncpy(pathBuf, cur, pathLength);
                                pathBuf[pathLength] = '\0';
                                printf("Client wants to get file %s;\\n", pathBuf);
                                if (pathLength > 7 && strncmp(pathBuf, "secret/", 7) == 0) {
                                    send_string(*it, "HTTP/1.0 401 Unauthorized\nEnter login » ");
                                    char login[1024] = {0}, password[1024] = {0};
                                    int log = -1;
                                    while (log == -1) {
                                        log = recv(*it, login, 1024, 0);
                                    }
                                    send_string(*it, "Enter password » ");
                                    int pass = -1;
                                    while (pass == -1) {
                                        pass = recv(*it, password, 1024, 0);
                                    }
                                    if (strcmp(login, LOGIN.c_str()) != 0 ||
                                        strcmp(password, PASSWORD.c_str()) != 0) {
                                        send_string(*it, "HTTP/1.0 401 Unauthorized\n");
                                        continue;
                                    }
                                }
                                try {
                                    sendFile(pathBuf, *it);
                                } catch (const char *errCode) {
                                    char errResponse[1024];
                                    sprintf(errResponse, "HTTP/1.0 %s\nServer: http_trynb\n\n",
                                            errCode);
                                    send_string(*it, errResponse);
                                }

                            }
                        }
                    }
                } else {
                    send_string(*it, "HTTP/1.0 501 Not Implemented\nServer: http_trynb\n");
                }
            }
        }
    }

    return EXIT_SUCCESS;
}

int send_string(int s, const char *sString) {
    return send(s, sString, strlen(sString), 0);
}

void sendFile(char *filePath, int sock) {
    FILE *file = fopen(filePath, "r");
    if (file == NULL) {
        fputs("File error", stderr);
        throw "404 Not Found";
    }

    // obtain/get size of file:
    fseek(file, 0, SEEK_END);
    long lSize = ftell(file);
    rewind(file);

    // allocate memory to contain the whole file:
    char *buffer = (char *) malloc(sizeof(char) * lSize);
    if (buffer == NULL) {
        fputs("Memory error", stderr);
        fclose(file);
        free(buffer);
        throw "507 Insufficient Storage";
    }

    // copy the file into the buffer:
    size_t result = fread(buffer, 1, lSize, file);
    if (result != lSize) {
        fputs("Reading error", stderr);
        fclose(file);
        free(buffer);
        throw "500 Internal Server Error";
    }

    send_string(sock, "HTTP/1.0 200 OK\n"
                      "Server: http_mult\n\n"
    );
    send_string(sock, buffer);
    send_string(sock, "\n");

    //clear mem
    fclose(file);
    free(buffer);
}