#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <vector>
#include <map>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavcodec/bsf.h>
}

// 和客户端完全一致的数据包头
struct PacketHeader {
    uint32_t magic;
    uint32_t dataType; // 0: video, 1: audio, 2: stream_info
    uint32_t dataSize;
    int64_t  pts;
};
const uint32_t PACKET_MAGIC = 0x12345678;

// 保存每个客户端连接的状态
struct ClientState {
    AVFormatContext* fmt_ctx = nullptr;
    int video_stream_index = -1;
    int audio_stream_index = -1; // 新增音频流索引
    AVBSFContext* bsf_ctx = nullptr;
    std::vector<uint8_t> pending_data; // 用于处理非阻塞发送时未发完的数据
    bool header_sent = false; // 标记包头是否已发送
};

// 用于管理所有客户端状态的映射（利用标准库中的map提升工作效率）
std::map<int, ClientState> client_states;

// 函数声明
int initserver(int port);
void set_non_blocking(int sock);
void add_client(int epollfd, int clientsock, const char* video_filename);
void remove_client(int epollfd, int clientsock);
void handle_write(int epollfd, int clientsock);

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("用法: %s <port> <video_file>\n", argv[0]);
        return -1;
    }
    //检查文件是否存在
    const char* video_filename = argv[2];
    FILE* test_file = fopen(video_filename, "rb");
    if (!test_file) {
        perror("Error opening video file");
        return -1;
    }
    fclose(test_file);

    // 初始化并监听socket
    int listensock = initserver(atoi(argv[1]));
    if (listensock < 0) {
        printf("initserver() failed.\n");
        return -1;
    }
    printf("Server listening on port %s, streaming file %s\n", argv[1], video_filename);
    //创建epoll实例
    int epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create1");
        return -1;
    }

    //把监听socket添加到epoll实例，监听EPOLLIN实例
    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listensock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listensock, &ev) == -1) {
        perror("epoll_ctl: listen_sock");
        return -1;
    }

    std::vector<epoll_event> events(64);//用于接收epoll_wait的返回值
    while (true) {
        int nfds = epoll_wait(epollfd, events.data(), events.size(), -1);
        if (nfds == -1) {
            perror("epoll_wait");
            return -1;
        }

        for (int n = 0; n < nfds; ++n) {
            //以下是监听情况，用于观察服务端运行情况
            printf("Epoll event on fd=%d, events=%s%s%s%s\n",
                   events[n].data.fd,
                   (events[n].events & EPOLLIN) ? "EPOLLIN " : "",
                   (events[n].events & EPOLLOUT) ? "EPOLLOUT " : "",
                   (events[n].events & EPOLLERR) ? "EPOLLERR " : "",
                   (events[n].events & EPOLLHUP) ? "EPOLLHUP " : "");

            if (events[n].data.fd == listensock) {
                // 处理新的连接
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int clientsock = accept(listensock, (struct sockaddr*)&client_addr, &client_len);
                if (clientsock == -1) {
                    perror("accept");
                    continue;
                }
                add_client(epollfd, clientsock, video_filename);
            } else {
                // 处理已连接的客户端事件
                int clientsock = events[n].data.fd;
                if (events[n].events & (EPOLLHUP | EPOLLERR)) {
                    // 客户端断开或出错
                    remove_client(epollfd, clientsock);
                    continue;
                }
                if (events[n].events & EPOLLOUT) {
                    // 可以向客户端写数据
                    handle_write(epollfd, clientsock);
                }
                 if (events[n].events & EPOLLIN) {
                    // 接收数据以检测断开
                    char dummy_buf[1];
                    ssize_t n = recv(clientsock, dummy_buf, sizeof(dummy_buf), 0);
                    if (n <= 0) { // 0 or -1 indicates disconnection or error
                        remove_client(epollfd, clientsock);
                    }
                }
            }
        }
    }

    close(listensock);
    return 0;
}
//按流程socket()->connect()->listen()->epoll_create()->epoll_ctl->epoll_wait
int initserver(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket() failed");
        return -1;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    if (bind(sock, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind() failed");
        close(sock);
        return -1;
    }

    if (listen(sock, 5) != 0) {
        perror("listen() failed");
        close(sock);
        return -1;
    }

    return sock;
}
//非阻塞，多进程提高效率
void set_non_blocking(int sock) {
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
}

void add_client(int epollfd, int clientsock, const char* video_filename) {
    set_non_blocking(clientsock);
    
    ClientState state;
    if (avformat_open_input(&state.fmt_ctx, video_filename, nullptr, nullptr) != 0) {
        fprintf(stderr, "Could not open video file %s\n", video_filename);
        close(clientsock);
        return;
    }
    if (avformat_find_stream_info(state.fmt_ctx, nullptr) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        avformat_close_input(&state.fmt_ctx);
        close(clientsock);
        return;
    }
    state.video_stream_index = av_find_best_stream(state.fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    state.audio_stream_index = av_find_best_stream(state.fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0); // 查找音频流
    if (state.video_stream_index < 0) {
        fprintf(stderr, "Could not find video stream in input file\n");
        avformat_close_input(&state.fmt_ctx);
        close(clientsock);
        return;
    }

    // 获取音频参数
    uint32_t audio_sample_rate = 0;
    uint32_t audio_channels = 0;
    uint32_t audio_format = 0;
    if (state.audio_stream_index >= 0) {
        AVCodecParameters* audio_par = state.fmt_ctx->streams[state.audio_stream_index]->codecpar;
        audio_sample_rate = audio_par->sample_rate;
        audio_channels = audio_par->channels;
        audio_format = audio_par->format;
    }

    //存视频元信息，按包处理
    AVStream* stream = state.fmt_ctx->streams[state.video_stream_index];
    AVCodecParameters* codecpar = stream->codecpar;

    PacketHeader info_header;
    info_header.magic = PACKET_MAGIC;
    info_header.dataType = 2; // Using 2 for stream info
    info_header.dataSize = sizeof(uint32_t) * 5 + sizeof(int32_t) * 2; // width, height, audio_sample_rate, audio_channels, audio_format, tb_num, tb_den
    info_header.pts = 0; // Not used for info packet

    uint32_t width = codecpar->width;
    uint32_t height = codecpar->height;
    int32_t tb_num = stream->time_base.num;
    int32_t tb_den = stream->time_base.den;

    state.pending_data.resize(sizeof(info_header) + info_header.dataSize);
    uint8_t* p = state.pending_data.data();
    memcpy(p, &info_header, sizeof(info_header)); p += sizeof(info_header);
    memcpy(p, &width, sizeof(width)); p += sizeof(width);
    memcpy(p, &height, sizeof(height)); p += sizeof(height);
    memcpy(p, &audio_sample_rate, sizeof(audio_sample_rate)); p += sizeof(audio_sample_rate);
    memcpy(p, &audio_channels, sizeof(audio_channels)); p += sizeof(audio_channels);
    memcpy(p, &audio_format, sizeof(audio_format)); p += sizeof(audio_format);
    memcpy(p, &tb_num, sizeof(tb_num)); p += sizeof(tb_num);
    memcpy(p, &tb_den, sizeof(tb_den));
    
    printf("Queued stream info for client %d: %ux%u, time_base: %d/%d, audio: %u Hz, %u ch, fmt=%u\n", clientsock, width, height, tb_num, tb_den, audio_sample_rate, audio_channels, audio_format);
    
    //初始化比特流过滤器（h264模式）一定要统一格式
    const AVBitStreamFilter* bsf = av_bsf_get_by_name("h264_mp4toannexb");
    if (!bsf) {
        fprintf(stderr, "Failed to find h264_mp4toannexb bitstream filter\n");
        avformat_close_input(&state.fmt_ctx);
        close(clientsock);
        return;
    }
    if (av_bsf_alloc(bsf, &state.bsf_ctx) < 0) {
        fprintf(stderr, "Failed to allocate bitstream filter context\n");
        avformat_close_input(&state.fmt_ctx);
        close(clientsock);
        return;
    }
    avcodec_parameters_copy(state.bsf_ctx->par_in, state.fmt_ctx->streams[state.video_stream_index]->codecpar);
    if (av_bsf_init(state.bsf_ctx) < 0) {
        fprintf(stderr, "Failed to init bitstream filter context\n");
        av_bsf_free(&state.bsf_ctx);
        avformat_close_input(&state.fmt_ctx);
        close(clientsock);
        return;
    }

    client_states[clientsock] = std::move(state);

    //把新客户端socket添加到epoll，监听读写，设置边缘触发
    epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET; // 监听读、写和边缘触发
    ev.data.fd = clientsock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, clientsock, &ev) == -1) {
        perror("epoll_ctl: add client");
        client_states.erase(clientsock);
        close(clientsock);
        return;
    }
    
    char client_ip[INET_ADDRSTRLEN];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    getpeername(clientsock, (struct sockaddr*)&client_addr, &client_len);
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    printf("Client %s (socket=%d) connected.\n", client_ip, clientsock);
}

//移除客户端并释放资源
void remove_client(int epollfd, int clientsock) {
    printf("Client (socket=%d) disconnected.\n", clientsock);
    epoll_ctl(epollfd, EPOLL_CTL_DEL, clientsock, nullptr);
    
    auto it = client_states.find(clientsock);
    if (it != client_states.end()) {
        av_bsf_free(&it->second.bsf_ctx);
        avformat_close_input(&it->second.fmt_ctx);
        client_states.erase(it);
    }
    close(clientsock);
}

void handle_write(int epollfd, int clientsock) {
    printf("[LOG] handle_write called for socket %d\n", clientsock);

    auto it = client_states.find(clientsock);
    if (it == client_states.end()) return;
    
    ClientState& state = it->second;

    if (!state.pending_data.empty()) {
        ssize_t n = write(clientsock, state.pending_data.data(), state.pending_data.size());
        if (n >= 0) {
            if (n < state.pending_data.size()) {
                printf("[LOG] Partial write of pending data to socket %d: wrote %zd of %zu bytes.\n", clientsock, n, state.pending_data.size());
                state.pending_data.erase(state.pending_data.begin(), state.pending_data.begin() + n);
                return; //Buffer 满, 等下一个 EPOLLOUT.
            }
            state.pending_data.clear();
             printf("[LOG] Finished sending pending data to socket %d.\n", clientsock);
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("write pending data");
                remove_client(epollfd, clientsock);
            }
            return; //等下一个 EPOLLOUT.
        }
    }

    // 主循环：一直读或写
    while (true) {
        AVPacket* original_pkt = av_packet_alloc();
        if (!original_pkt) return;
        
        int ret = av_read_frame(state.fmt_ctx, original_pkt);
        if (ret < 0) {
            printf("[LOG] End of file on socket %d. Seeking to beginning.\n", clientsock);
            av_seek_frame(state.fmt_ctx, state.video_stream_index, 0, AVSEEK_FLAG_BACKWARD);
            if (state.bsf_ctx) av_bsf_flush(state.bsf_ctx);
            av_packet_free(&original_pkt);
            continue;
        }

        if (original_pkt->stream_index == state.video_stream_index) {
            // 视频包处理
            if (state.bsf_ctx) {
                if (av_bsf_send_packet(state.bsf_ctx, original_pkt) < 0) {
                    av_packet_free(&original_pkt);
                    break;
                }
                av_packet_free(&original_pkt);
                while (true) {
                    AVPacket* filtered_pkt = av_packet_alloc();
                    if (!filtered_pkt) break;
                    int ret2 = av_bsf_receive_packet(state.bsf_ctx, filtered_pkt);
                    if (ret2 == AVERROR(EAGAIN) || ret2 == AVERROR_EOF) {
                        av_packet_free(&filtered_pkt);
                        break;
                    } else if (ret2 < 0) {
                        av_packet_free(&filtered_pkt);
                        remove_client(epollfd, clientsock);
                        return;
                    }
                    PacketHeader header;
                    header.magic = PACKET_MAGIC;
                    header.dataType = 0; //0 ：video
                    header.dataSize = filtered_pkt->size;
                    header.pts = filtered_pkt->pts;
                    std::vector<uint8_t> send_buffer;
                    send_buffer.resize(sizeof(header) + filtered_pkt->size);
                    memcpy(send_buffer.data(), &header, sizeof(header));
                    memcpy(send_buffer.data() + sizeof(header), filtered_pkt->data, filtered_pkt->size);
                    ssize_t n = write(clientsock, send_buffer.data(), send_buffer.size());
                    av_packet_free(&filtered_pkt);
                    if (n < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            perror("write on new packet");
                            remove_client(epollfd, clientsock);
                        } else {
                            printf("[LOG] write would block on socket %d. Storing %zu bytes.\n", clientsock, send_buffer.size());
                            state.pending_data = send_buffer;
                        }
                        return;
                    } else if (n < send_buffer.size()) {
                        printf("[LOG] Partial write to socket %d: wrote %zd of %zu bytes.\n", clientsock, n, send_buffer.size());
                        state.pending_data.assign(send_buffer.begin() + n, send_buffer.end());
                        return;
                    }
                }
            }
        } else if (original_pkt->stream_index == state.audio_stream_index && state.audio_stream_index >= 0) {
            //音频包处理
            PacketHeader header;
            header.magic = PACKET_MAGIC;
            header.dataType = 1; //1 ：audio
            header.dataSize = original_pkt->size;
            header.pts = original_pkt->pts;
            std::vector<uint8_t> send_buffer;
            send_buffer.resize(sizeof(header) + original_pkt->size);
            memcpy(send_buffer.data(), &header, sizeof(header));
            memcpy(send_buffer.data() + sizeof(header), original_pkt->data, original_pkt->size);
            ssize_t n = write(clientsock, send_buffer.data(), send_buffer.size());
            av_packet_free(&original_pkt);
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("write on new audio packet");
                    remove_client(epollfd, clientsock);
                } else {
                    printf("write would block on socket %d. Storing %zu bytes.\n", clientsock, send_buffer.size());
                    state.pending_data = send_buffer;
                }
                return;
            } else if (n < send_buffer.size()) {
                printf("Partial write to socket %d: wrote %zd of %zu bytes.\n", clientsock, n, send_buffer.size());
                state.pending_data.assign(send_buffer.begin() + n, send_buffer.end());
                return;
            }
        } else {
            av_packet_free(&original_pkt);
            continue;
        }
    }
} 