#ifndef __UTILS_H__
#define __UTILS_H__

#define LOG(format, ...) fprintf(stdout, format"\n", ##__VA_ARGS__)

#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

struct peer_ctx {
    char ip[30];
    uint16_t port;
    int id;

    bool operator==(const struct peer_ctx &rhs) const{
        return strcmp(ip, rhs.ip) == 0 && port == rhs.port;
    }
};

int setnonblocking(int sockfd) {
    int flag, s;
    flag = fcntl(sockfd, F_GETFL, 0);
    if (flag == -1) {
        LOG("ERROR: fcntl");
        return -1;
    }
    flag |= O_NONBLOCK;
    s = fcntl(sockfd, F_SETFL, flag);
    return (s == -1 ? s : 0);
}

int begin_with(const char *src, const char * key) {
    return strncmp(src, key, strlen(key));
}

void send_to_peer(int fd, struct peer_ctx *peer, const char *buf, ssize_t len) {
    struct sockaddr_in peersock;

    memset(&peersock, 0, sizeof peersock);

    peersock.sin_family = AF_INET;
    peersock.sin_port = htons(peer->port);
    inet_pton(AF_INET, peer->ip, &peersock.sin_addr);

    sendto(fd, buf, len, 0, (struct sockaddr *)&peersock, sizeof peersock);
}

#endif

