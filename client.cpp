#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ev.h>
#include <list>
#include <map>
#include "utils.h"

enum states{
    CLOSED,
    WATING_FOR_WELCOME,
    CONNECTED,
    WATING_FOR_LIST,
    CHATTING,
    WATING_FOR_BYE
}g_state;

std::map<int, peer_ctx> g_peer;
struct sockaddr_in server;

void stdin_cb(EV_P_ ev_io *w, int revents);
void peer_cb(EV_P_ ev_io *w, int revents);

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

void chatting(int fd, int id) {
    char line[1024] = { 0 };
    ssize_t len;
    len = read(0, line, sizeof line);
    if (begin_with(line, ".bye") == 0) {
        g_state = CONNECTED;
        LOG("Chatting end.");
    }
    send_to_peer(fd, &g_peer[id], line, len);
}

void stdin_cb(EV_P_ ev_io *w, int revents) {
    char buf[1024] = "";
    ssize_t buf_size = sizeof buf;
    static struct peer_ctx pc;
    static int mate_id;
    ev_io *peer_io = (ev_io *) w->data;

    if (g_state == CHATTING) {
    //    LOG("chatting now");
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
        if (g_state == CONNECTED) {
            LOG("already connected");
            return;
        }
        memset(&server, 0, sizeof server);
        server.sin_family = AF_INET;
        char *ip = strtok(NULL, " ");
        if (!ip) {
            LOG("connect <ip> <port>");
            return;
        }
        inet_pton(AF_INET, ip, &server.sin_addr);
        strcpy(pc.ip, ip);

        char *str_port = strtok(NULL, " ");
        if (!str_port) {
            LOG("connect <ip> <port>");
            return;
        }
        uint16_t port = atoi(str_port);
        server.sin_port = htons(port);
        pc.port = port;

        LOG("connecting...");
        g_state = WATING_FOR_WELCOME;
        send_to_peer(peer_io->fd, &pc, "HELLO", 5);

    } else if (begin_with(cmd, "disconn") == 0) {
        if (g_state != CONNECTED) {
            LOG("haven't connected to a server");
            return;
        }
        g_state = WATING_FOR_BYE;
        send_to_peer(peer_io->fd, &pc, "BYE", 3);
    } else if (begin_with(cmd, "list") == 0) {
        g_state = WATING_FOR_LIST;
        send_to_peer(peer_io->fd, &pc, "LIST_PEERS", 10);
    } else if (begin_with(cmd, "chat") == 0) {
        char *str_id = strtok(NULL, " ");
        if (!str_id) {
            LOG("chat <id>");
            return;
        }
        mate_id = atoi(str_id);
        g_state = CHATTING;
        LOG("\nChatting with peer %d\n", mate_id);
    } else if (begin_with(cmd, "exit") == 0) {
        if (g_state != CLOSED) {
            send_to_peer(peer_io->fd, &pc, "BYE", 3);
        }
        ev_break(EV_A_ EVBREAK_ALL);
        return;
    }

}

void handle_list(const char *lst) {
    char *buf = strdup(lst + 5);
    peer_ctx pc;
    char *line = strtok(buf, "\n");
    while (line) {
        if (begin_with(line, "[*]") != 0) {
            sscanf(line, "Peer %d %[^:]:%hu", &pc.id, pc.ip, &pc.port);
            g_peer[pc.id] = pc;
        }
        line = strtok(NULL, "\n");
    }
    free(buf);
}

void peer_cb(EV_P_ ev_io *w, int revents) {
    char buf[1024] = "";
    ssize_t recv_len = sizeof buf;
    struct sockaddr_in peer;
    socklen_t len = sizeof peer;

    memset(&peer, 0, sizeof peer);
    recv_len = recvfrom(w->fd, buf, recv_len, 0, (struct sockaddr *)&peer, &len);

    if (recv_len == -1) {
        LOG("error");
        return;
    }

    if (g_state == CHATTING) {
        if (begin_with(buf, ".bye") == 0) {
            g_state = CONNECTED;
            LOG("Chat ends by peer");
            return;
        }
        LOG("Peer says: %s\n", buf);
    }

    // if (memcmp(&peer, &server, sizeof server)) return;
    switch (g_state) {
    case WATING_FOR_WELCOME:
        if (begin_with(buf, "WELCOME") == 0) {
            LOG("Connected to server");
            g_state = CONNECTED;
        }
        break;
    case WATING_FOR_LIST:
        if (begin_with(buf, "LIST") == 0) {
            LOG("%s", buf);
            handle_list(buf);
            g_state = CONNECTED;
        }
        break;
    case WATING_FOR_BYE:
        if (begin_with(buf, "BYE") == 0) {
            LOG("Disconnected to server");
            g_state = CLOSED;
        }
        break;
    default:
        break;
    }
}

