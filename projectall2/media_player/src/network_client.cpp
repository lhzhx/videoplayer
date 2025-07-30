#include "network_client.h"
#include <iostream>
//可否保证系统通用性？
#ifndef _WIN32
    #include <string.h> // linux库函数调用
    #define closesocket(s) ::close(s)
#endif

CTCPClient::CTCPClient() : m_socket(INVALID_SOCKET), m_connected(false), m_port(0) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
    }
#endif
}

CTCPClient::~CTCPClient() {
    close();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool CTCPClient::connect(const std::string& in_ip, unsigned short in_port) {
    if (m_socket != INVALID_SOCKET) {
        return false; // 已连接
    }
    m_ip = in_ip;
    m_port = in_port;

    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed" << std::endl;
        return false;
    }

    struct hostent* h;
    if ((h = gethostbyname(m_ip.c_str())) == nullptr) {
        std::cerr << "gethostbyname failed for " << m_ip << std::endl;
        close();
        return false;
    }

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(m_port);
    memcpy(&server_addr.sin_addr, h->h_addr, h->h_length);

    if (::connect(m_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to connect to server " << m_ip << ":" << m_port << std::endl;
        close();
        return false;
    }

    m_connected = true;
    std::cout << "Connected to server " << m_ip << ":" << m_port << std::endl;
    return true;
}

void CTCPClient::close() {
    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    m_connected = false;
}

bool CTCPClient::is_connected() const {
    return m_connected;
}

int CTCPClient::recv_all(char* buf, int len) {
    int total = 0;
    while (total < len) {
        int ret = recv(m_socket, buf + total, len - total, 0);
        if (ret <= 0) {
            return ret;
        }
        total += ret;
    }
    return total;
}

bool CTCPClient::receive_packet(std::vector<uint8_t>& payload, uint32_t& data_type, int64_t& pts) {
    if (!m_connected) return false;

    PacketHeader header;
    if (recv_all((char*)&header, sizeof(header)) <= 0) {
        std::cerr << "Server disconnected or error while receiving header." << std::endl;
        close();
        return false;
    }

    if (header.magic != PACKET_MAGIC) {
        std::cerr << "Invalid packet magic!" << std::endl;
        close();
        return false;
    }

    payload.resize(header.dataSize);
    if (header.dataSize > 0) {
        if (recv_all((char*)payload.data(), header.dataSize) <= 0) {
            std::cerr << "Failed to receive packet payload." << std::endl;
            close();
            return false;
        }
    }
    
    data_type = header.dataType;
    pts = header.pts;
    return true;
} 