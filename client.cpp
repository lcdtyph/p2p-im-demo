#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ev.h>
#include <unordered_map>
#include <memory>
#include "utils.h"

enum states{
    CLOSED,
    WATING_FOR_WELCOME,
    CONNECTED,
    WATING_FOR_LIST,
    CHATTING,
    WATING_FOR_BYE
}g_state;

std::unordered_map<uint32_t, std::shared_ptr<peer_ctx>> g_peers;
std::shared_ptr<peer_ctx> server;

void stdin_cb(EV_P_ ev_io *w, int revents);
void peer_cb(EV_P_ ev_io *w, int revents);
void pulse_cb(EV_P_ ev_timer *w, int revents);

int main() {
    struct ev_loop *loop = EV_DEFAULT;
    ev_io stdio;
    ev_io peer_io;

    ev_io_init(&stdio, stdin_cb, 0, EV_READ);
    stdio.data = &peer_io;
    ev_io_start(loop, &stdio);
    
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    setnonblocking(fd);

    ev_io_init(&peer_io, peer_cb, fd, EV_READ);
    ev_io_start(loop, &peer_io);

    ev_run(loop, 0);

    return 0;
}

void chatting(int fd, uint32_t id) {
    char line[1024] = { 0 };
    ssize_t len;
    len = read(0, line, sizeof line);
    if (begin_with(line, ".bye") == 0) {
        g_state = CONNECTED;
        LOG("Chatting end.");
    }

    packet_hdr *phdr = construct_packet(CHAT, id, line, len);
    send_to_peer(fd, &g_peers[id]->peersock, phdr, phdr->len);
    destroy_packet(phdr);
}

void stdin_cb(EV_P_ ev_io *w, int revents) {
    char buf[1024] = "";
    ssize_t buf_size = sizeof buf;
    static uint32_t mate_id;
    ev_io *peer_io = (ev_io *) w->data;

    if (g_state == CHATTING) {
        chatting(peer_io->fd, mate_id);
        return;
    }

    buf_size = read(w->fd, buf, buf_size);

    if (buf_size == -1) {
        LOG("stdin error");
        return;
    }
    
    char *cmd = strtok(buf, " ");
    // LOG("%s", cmd);
    if (begin_with(cmd, "connect") == 0) {
        if (g_state != CLOSED && g_state != WATING_FOR_WELCOME) {
            LOG("already connected");
            return;
        }
        server = std::make_shared<peer_ctx>();
        memset(&server->peersock, 0, sizeof(struct sockaddr_in));
        server->peersock.sin_family = AF_INET;
        char *ip = strtok(NULL, " ");
        if (!ip) {
            LOG("connect <ip> <port>");
            return;
        }
        if (inet_pton(AF_INET, ip, &(server->peersock.sin_addr)) <= 0) LOG("%s", strerror(errno));

        char *str_port = strtok(NULL, " ");
        if (!str_port) {
            LOG("connect <ip> <port>");
            return;
        }
        uint16_t port = atoi(str_port);
        server->peersock.sin_port = htons(port);
        ev_timer_init(&server->timer, pulse_cb, 8., 8.);
        server->timer.data = peer_io;
        server->id = 0;

        LOG("connecting to %s:%d ...", ip, port);
        g_state = WATING_FOR_WELCOME;
        send_op(peer_io->fd, server, HELLO);

    } else if (begin_with(cmd, "disconn") == 0) {
        if (g_state == CLOSED) {
            LOG("haven't connected to a server");
            return;
        }
        g_state = WATING_FOR_BYE;
        send_op(peer_io->fd, server, BYE);
    } else if (begin_with(cmd, "list") == 0) {
        g_state = WATING_FOR_LIST;
        send_op(peer_io->fd, server, LIST_PEERS);
    } else if (begin_with(cmd, "chat") == 0) {
        char *str_id = strtok(NULL, " ");
        if (!str_id) {
            LOG("chat <id>");
            return;
        }
        mate_id = std::stoul(str_id);
        g_state = CHATTING;
        LOG("\nChatting with peer %u\n", mate_id);
    } else if (begin_with(cmd, "exit") == 0) {
        if (g_state != CLOSED) {
            send_op(peer_io->fd, server, BYE);
        }
        ev_break(EV_A_ EVBREAK_ALL);
        return;
    }

}

void handle_list(const char *lst) {
    char *buf = strdup(lst);
    auto pc = std::make_shared<peer_ctx>();
    char ip[30];
    uint16_t port;
    char *line = strtok(buf, "\n");
    while (line) {
        if (begin_with(line, "[*]") != 0) {
            sscanf(line, "Peer %x %[^:]:%hu", &pc->id, ip, &port);
            pc->peersock.sin_port = htons(port);
            g_peers[pc->id] = pc;
        }
        line = strtok(NULL, "\n");
    }
    free(buf);
}

ssize_t recv_udp(int fd, void *dst, ssize_t expected_len, struct sockaddr *peer, socklen_t *len) {
    ssize_t recv_len = 0;
    ssize_t tmp_len = 0;

    while (recv_len < expected_len) {
        tmp_len = recvfrom(fd, dst, expected_len, 0, peer, len);
        if (tmp_len < 0) {
            LOG("error recv for %ld bytes", expected_len);
            LOG("%s", strerror(errno));
            return tmp_len;
        }
        recv_len += tmp_len;
    }
    return recv_len;
}

void peer_cb(EV_P_ ev_io *w, int revents) {
    packet_hdr header;
    char *buf = nullptr;
    ssize_t recv_len = 0;
    struct sockaddr_in peer;
    socklen_t len = sizeof peer;

    memset(&peer, 0, sizeof peer);

    recv_len = recv_udp(w->fd, &header, sizeof header, (struct sockaddr *)&peer, &len);

    if (header.len > recv_len) {
        buf = new char[header.len + 1];
        recv_udp(w->fd, buf, header.len - recv_len, (struct sockaddr *)&peer, &len);
        buf[header.len] = 0;
    }

    if (header.opcode == PONG) {
    //    LOG("PONG received.");
        return;
    }

    if (g_state == CHATTING && header.opcode == CHAT) {
        if (begin_with(buf, ".bye") == 0) {
            g_state = CONNECTED;
            LOG("Chat ends by peer");
            goto __bad__;
        }
        LOG("Peer says: %s\n", buf);
    }

    switch (g_state) {
    case WATING_FOR_WELCOME:
        if (header.opcode == WELCOME) {
            LOG("Connected to server");
            g_state = CONNECTED;
            server->id = header.peerid;
            LOG("get id: %x", header.peerid);
            ev_timer_start(EV_A_ &server->timer);
        }
        break;

    case WATING_FOR_LIST:
        if (header.opcode == LIST_PEERS) {
            LOG("%s", buf);
            handle_list(buf);
            g_state = CONNECTED;
        }
        break;

    case WATING_FOR_BYE:
        if (header.opcode == BYE) {
            LOG("Disconnected to server");
            ev_timer_stop(EV_A_ &server->timer);
            g_state = CLOSED;
        }
        break;

    default:
        break;
    }

__bad__:
    delete []buf;
}

void pulse_cb(EV_P_ ev_timer *w, int revents) {
    peer_ctx *serv = (peer_ctx *)w;
    ev_io *peer_io = (ev_io *)w->data;
    if (server.get() == serv)
        send_op(peer_io->fd, server, PING);
}
