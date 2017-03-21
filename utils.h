#ifndef __UTILS_H__
#define __UTILS_H__

#define LOG(format, ...) fprintf(stdout, format"\n", ##__VA_ARGS__)

#include <ev.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <random>
#include <memory>
#include <string.h>

struct peer_ctx {
    ev_timer timer;
    sockaddr_in peersock;
    uint32_t id;

    bool operator==(const struct peer_ctx &rhs) const{
        return id == rhs.id;
    }

    bool update(struct sockaddr_in *addr) {
        bool changed = false;
        if (memcmp(&addr->sin_addr, &peersock.sin_addr, sizeof peersock.sin_addr) || addr->sin_port != peersock.sin_port) {
            memcpy(&peersock, addr, sizeof peersock);
            changed = true;
        }
        return changed;
    }
};

enum operation {
    HELLO,
    WELCOME,
    BYE,
    PING,
    PONG,
    LIST_PEERS,
    CHAT
};

struct packet_hdr {
    enum operation opcode;
    uint32_t peerid;
    size_t len;
    char data[];
};

uint32_t genid() {
    static std::random_device rd;
    return rd();
}

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

void send_to_peer(int fd, const struct sockaddr_in *peersock, const void *buf, ssize_t len) {
    sendto(fd, buf, len, 0, (struct sockaddr *)&peersock, sizeof peersock);
}

packet_hdr *construct_packet(enum operation opcode, uint32_t id, const void *data, size_t data_len) {
    char *pbuf = new char[data_len + sizeof(packet_hdr)];
    packet_hdr *phdr = reinterpret_cast<packet_hdr *>(pbuf);
    phdr->peerid = id;
    phdr->opcode = LIST_PEERS;
    phdr->len = data_len + sizeof(packet_hdr);
    if (data && data_len) memcpy(phdr->data, data, data_len);
    return phdr;
}

void send_op(int fd, std::shared_ptr<peer_ctx> peer, operation op) {
    packet_hdr *phdr = construct_packet(op, peer->id, nullptr, 0);
    send_to_peer(fd, &peer->peersock, phdr, phdr->len);
    delete []phdr;
}

#endif

