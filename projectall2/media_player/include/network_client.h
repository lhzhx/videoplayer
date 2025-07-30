#ifndef CTCPCLIENT_H
#define CTCPCLIENT_H
#include <string>
#include <vector>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h> // For gethostbyname
typedef int PlatformSocket;
#define INVALID_SOCKET -1

// TCP传输的包头结构
struct PacketHeader {
    uint32_t magic;      // 魔数，用于数据包校验
    uint32_t dataType;   // 数据类型 (0: 视频, 1: 音频, 2: 元数据)
    uint32_t dataSize;   // 负载数据的大小
    int64_t  pts;        // [NEW] 帧的显示时间戳
};
const uint32_t PACKET_MAGIC = 0x12345678;

class CTCPClient {
public:
    CTCPClient();
    ~CTCPClient();

    // 连接到服务器
    bool connect(const std::string& in_ip, unsigned short in_port);

    // 断开连接
    void close();

    // 检查连接状态
    bool is_connected() const;

    // 接收一个完整的数据包
    bool receive_packet(std::vector<uint8_t>& payload, uint32_t& data_type, int64_t& pts);

private:
    PlatformSocket m_socket;
    bool m_connected;
    std::string m_ip;
    unsigned short m_port;
    
    // 辅助函数，确保接收到指定长度的数据
    int recv_all(char* buf, int len);
};

#endif // CTCPCLIENT_H 