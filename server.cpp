#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ev.h>
#include <list>
#include "utils.h"

std::list<peer_ctx> g_peers;

void start_server(const char *host, uint16_t port);
void start_receive(int fd);
void signal_cb(EV_P_ ev_signal *w, int revents);

int main(int argc, char *argv[]) {
    const char *host = "0.0.0.0";
    int port = 7778;

    start_server(host, port);
    return 0;
}

void start_server(const char *host, uint16_t port) {
    struct sockaddr_in serv_addr;
    int listen_fd;

    memset(&serv_addr, 0, sizeof serv_addr);
    listen_fd = socket(AF_INET, SOCK_DGRAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(host);
    serv_addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof serv_addr) < 0) {
        LOG("error on binding");
        return;
    }
    setnonblocking(listen_fd);
    start_receive(listen_fd);

}

void list_peers(int fd, struct peer_ctx *peer) {
    char buf[1024] = "LIST\n";
    for (auto &p: g_peers) {
        char line[50];
        snprintf(line, sizeof line, "Peer %d %s:%d\n", p.id, p.ip, p.port);
        if (p.id == peer->id)
            strcat(buf, "[*]");
        strcat(buf, line);
    }
    send_to_peer(fd, peer, buf, strlen(buf));
}

void accept_cb(EV_P_ ev_io *w, int revents) {
    char recv_buf[1024] = "";
    ssize_t recv_size = sizeof recv_buf;
    struct sockaddr_in peer;
    socklen_t len = sizeof peer;
    struct peer_ctx pc;
    
    memset(&peer, 0, sizeof peer);
    recv_size = recvfrom(w->fd, recv_buf, recv_size, 0, (struct sockaddr *)&peer, &len);

    if (recv_size == -1) {
        LOG("error");
        return;
    }

    inet_ntop(AF_INET, &peer.sin_addr, pc.ip, len);
    pc.port = ntohs(peer.sin_port);
    pc.id = g_peers.size();
    
    auto itr = std::find(g_peers.begin(), g_peers.end(), pc);
    if (begin_with(recv_buf, "HELLO") == 0) {
        if (itr == g_peers.end())
            g_peers.push_back(pc);
        LOG("PEER: %s:%d connected", pc.ip, pc.port);
        send_to_peer(w->fd, &pc, "WELCOME", 7);
    } else if (begin_with(recv_buf, "BYE") == 0) {
        g_peers.remove(pc);
        LOG("PEER: %s:%d disconnected", pc.ip, pc.port);
        send_to_peer(w->fd, &pc, "BYE", 3);
    } else if (begin_with(recv_buf, "LIST_PEERS") == 0) {
        if (itr == g_peers.end()) return;
        LOG("Received [%zd bytes] from client %s:%d", recv_size, itr->ip, itr->port);
        LOG("LIST_PEER required");
        list_peers(w->fd, &(*itr));
    }
}

void start_receive(int listen_fd) {
    struct ev_loop *loop = EV_DEFAULT;
    ev_io server_io;
    ev_signal sigint;

    ev_io_init(&server_io, accept_cb, listen_fd, EV_READ);
    ev_io_start(loop, &server_io);

    ev_signal_init(&sigint, signal_cb, SIGINT);
    ev_signal_start(loop, &sigint);

    ev_run(loop, 0);
}

void signal_cb(EV_P_ ev_signal *w, int revents) {
    LOG("\nsignal interrupt received");
    LOG("Program terminated.");
    ev_break(EV_A_ EVBREAK_ALL);
}
