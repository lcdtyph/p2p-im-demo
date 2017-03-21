#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ev.h>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <string>
#include "utils.h"

std::unordered_map<uint32_t, std::shared_ptr<struct peer_ctx>> g_peers;

void start_server(const char *host, uint16_t port);
void start_receive(int fd);
void signal_cb(EV_P_ ev_signal *w, int revents);
void peersetup(std::shared_ptr<struct peer_ctx> peer, const struct sockaddr_in *peersock);

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
    LOG("Server start");
    setnonblocking(listen_fd);
    start_receive(listen_fd);
    close(listen_fd);
}

void list_peers(int fd, std::shared_ptr<peer_ctx> peer) {
    std::string buf;
    packet_hdr *phdr;
    char ip[30];
    uint16_t port;

    for (auto &p: g_peers) {
        char line[50];
        inet_ntop(AF_INET, (struct sockaddr *) &p.second->peersock, line, sizeof(struct sockaddr_in));
        port = ntohs(p.second->peersock.sin_port);
        snprintf(line, sizeof line, "Peer %d %s:%d\n", peer->id, ip, port);
        if (p.first == peer->id)
            buf += "[*]";
        buf += line;
    }

    phdr = construct_packet(LIST_PEERS, peer->id, buf.c_str(), buf.size());
    send_to_peer(fd, &peer->peersock, phdr, phdr->len);
    delete []phdr;
}

void accept_cb(EV_P_ ev_io *w, int revents) {
    char recv_buf[1024] = "";
    ssize_t recv_size = sizeof recv_buf;
    struct sockaddr_in peer;
    socklen_t len = sizeof peer;
    char ip[30];
    uint16_t port;
    struct packet_hdr *phdr;
    
    memset(&peer, 0, sizeof peer);
    recv_size = recvfrom(w->fd, recv_buf, recv_size, 0, (struct sockaddr *)&peer, &len);

    if (recv_size == -1) {
        LOG("error");
        return;
    }

    phdr = (struct packet_hdr *)&recv_buf[0];
    inet_ntop(AF_INET, &peer, ip, len);
    port = ntohs(peer.sin_port);
    
    LOG("Received data: %s from %s:%d\n", recv_buf, ip, port);
    auto itr = g_peers.find(phdr->peerid);
    if (itr != g_peers.end() && itr->second->update(&peer))
        LOG("Peer %u updated to %s:%d", itr->first, ip, port);

    switch(phdr->opcode) {
    case HELLO:
        if (itr == g_peers.end()) {
            auto new_peer = std::make_shared<struct peer_ctx>();
            peersetup(new_peer, &peer);
            g_peers[new_peer->id] = new_peer;
            LOG("NEW PEER: %s:%d connected", ip, port);
            itr = g_peers.find(new_peer->id);
            ev_timer_start(EV_A_ &new_peer->timer);
        }
        send_op(w->fd, itr->second, WELCOME);
        break;

    case BYE:
        LOG("PEER: %s:%d disconnected", ip, port);
        send_op(w->fd, itr->second, BYE);
        g_peers.erase(itr);
        break;

    case LIST_PEERS:
        if (itr == g_peers.end()) return;
        LOG("Received [%zd bytes] from client %s:%d", recv_size, ip, port);
        LOG("LIST_PEERS required");
        list_peers(w->fd, itr->second);
        break;

    case PING:
        if (itr == g_peers.end()) return;
        ev_timer_again(EV_A_ &itr->second->timer);
        send_op(w->fd, itr->second, PONG);
        break;

    default:
        break;
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

void pulse_timeout_cb(EV_P_ struct ev_timer *w, int revents) {
    struct peer_ctx *peer = reinterpret_cast<peer_ctx *>(w->data);
    LOG("Peer %u timeout.", peer->id);
    ev_timer_stop(EV_A_ w);
    g_peers.erase(g_peers.find(peer->id));
}

void peersetup(std::shared_ptr<struct peer_ctx> peer, const struct sockaddr_in *peersock) {
    memcpy(&peer->peersock, peersock, sizeof(struct sockaddr_in));
    ev_timer_init(&peer->timer, pulse_timeout_cb, 0., 10.);
    do {
        peer->id = genid();
    } while(g_peers.count(peer->id));
}
