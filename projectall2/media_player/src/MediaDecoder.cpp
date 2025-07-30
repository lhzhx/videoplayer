#include "MediaDecoder.h"
#include <iostream>
#include <cstring>

MediaDecoder::MediaDecoder()
    : fmt_(nullptr), vctx_(nullptr), actx_(nullptr), sws_(nullptr), swr_(nullptr),
      vstream_(-1), astream_(-1), w_(0), h_(0), audio_sample_rate_(0), audio_channels_(0),
      video_frame_(nullptr), net_vcodec_(nullptr), net_acodec_(nullptr),
      net_vctx_(nullptr), net_actx_(nullptr), net_pkt_(nullptr), quit_(false), is_network_mode_(false) {}

MediaDecoder::~MediaDecoder() {
    close();
}

bool MediaDecoder::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mtx_);
    close();
    is_network_mode_ = false;
    if (avformat_open_input(&fmt_, path.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Failed to open file: " << path << std::endl;
        return false;
    }
    if (avformat_find_stream_info(fmt_, nullptr) < 0) {
        std::cerr << "Failed to get stream info" << std::endl;
        return false;
    }
    for (unsigned i = 0; i < fmt_->nb_streams; ++i) {
        if (fmt_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vstream_ = i; break;
        }
    }
    for (unsigned i = 0; i < fmt_->nb_streams; ++i) {
        if (fmt_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            astream_ = i; break;
        }
    }
    if (vstream_ == -1) return false;
    AVCodecParameters* vpar = fmt_->streams[vstream_]->codecpar;
    AVCodec* vcodec = avcodec_find_decoder(vpar->codec_id);
    vctx_ = avcodec_alloc_context3(vcodec);
    avcodec_parameters_to_context(vctx_, vpar);
    if (avcodec_open2(vctx_, vcodec, nullptr) < 0) return false;
    if (astream_ != -1) {
        AVCodecParameters* apar = fmt_->streams[astream_]->codecpar;
        AVCodec* acodec = avcodec_find_decoder(apar->codec_id);
        actx_ = avcodec_alloc_context3(acodec);
        avcodec_parameters_to_context(actx_, apar);
        if (avcodec_open2(actx_, acodec, nullptr) < 0) {
            avcodec_free_context(&actx_);
            actx_ = nullptr;
        } else {
            audio_time_base_ = fmt_->streams[astream_]->time_base;
            audio_sample_rate_ = actx_->sample_rate;
            audio_channels_ = actx_->channels;
            swr_ = swr_alloc_set_opts(nullptr,
                AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLT, audio_sample_rate_,
                actx_->channel_layout, actx_->sample_fmt, actx_->sample_rate,
                0, nullptr);
            if (swr_init(swr_) < 0) {
                swr_free(&swr_);
            }
        }
    }
    w_ = vctx_->width;
    h_ = vctx_->height;
    time_base_ = fmt_->streams[vstream_]->time_base;
    sws_ = sws_getContext(w_, h_, vctx_->pix_fmt, w_, h_, AV_PIX_FMT_YUV420P, SWS_BILINEAR, 0, 0, 0);
    return true;
}

bool MediaDecoder::openNetwork(const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(mtx_);
    close();
    is_network_mode_ = true;
    net_client_ = std::make_unique<CTCPClient>();
    if (!net_client_->connect(ip, port)) {
        std::cerr << "Failed to connect to server." << std::endl;
        return false;
    }
    std::vector<uint8_t> info_payload;
    uint32_t info_type;
    int64_t dummy_pts;
    if (!net_client_->receive_packet(info_payload, info_type, dummy_pts)) {
        std::cerr << "Failed to receive stream info packet from server." << std::endl;
        return false;
    }
    size_t expected_size = sizeof(uint32_t) * 5 + sizeof(int32_t) * 2;
    if (info_type != 2 || info_payload.size() != expected_size) {
        std::cerr << "Received invalid stream info packet. Type: " << info_type << ", Size: " << info_payload.size() << std::endl;
        return false;
    }
    const uint8_t* p = info_payload.data();
    memcpy(&w_, p, sizeof(uint32_t)); p += sizeof(uint32_t);
    memcpy(&h_, p, sizeof(uint32_t)); p += sizeof(uint32_t);
    memcpy(&audio_sample_rate_, p, sizeof(uint32_t)); p += sizeof(uint32_t);
    memcpy(&audio_channels_, p, sizeof(uint32_t)); p += sizeof(uint32_t);
    uint32_t audio_format = 0;
    memcpy(&audio_format, p, sizeof(uint32_t)); p += sizeof(uint32_t);
    memcpy(&time_base_.num, p, sizeof(int32_t)); p += sizeof(int32_t);
    memcpy(&time_base_.den, p, sizeof(int32_t));
    net_vcodec_ = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!net_vcodec_) return false;
    net_vctx_ = avcodec_alloc_context3(net_vcodec_);
    if (avcodec_open2(net_vctx_, net_vcodec_, nullptr) < 0) return false;
    net_acodec_ = avcodec_find_decoder(AV_CODEC_ID_AAC);
    if (net_acodec_) {
        net_actx_ = avcodec_alloc_context3(net_acodec_);
        net_actx_->sample_rate = audio_sample_rate_;
        net_actx_->channels = audio_channels_;
        net_actx_->sample_fmt = (AVSampleFormat)audio_format;
        net_actx_->channel_layout = av_get_default_channel_layout(audio_channels_);
        if (avcodec_open2(net_actx_, net_acodec_, nullptr) < 0) {
            avcodec_free_context(&net_actx_);
            net_actx_ = nullptr;
        }
    }
    net_pkt_ = av_packet_alloc();
    sws_ = sws_getContext(w_, h_, AV_PIX_FMT_YUV420P, w_, h_, AV_PIX_FMT_YUV420P, SWS_BILINEAR, 0, 0, 0);
    if (net_actx_) {
        swr_ = swr_alloc_set_opts(nullptr,
            AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLT, audio_sample_rate_,
            net_actx_->channel_layout, net_actx_->sample_fmt, net_actx_->sample_rate,
            0, nullptr);
        if (swr_init(swr_) < 0) {
            swr_free(&swr_);
        }
    }
    quit_ = false;
    net_decode_thread_ = std::thread(&MediaDecoder::networkDecodeLoop, this);
    return true;
}

void MediaDecoder::networkDecodeLoop() {
    AVFrame* frame = av_frame_alloc();
    AVFrame* yuv = av_frame_alloc();
    AVFrame* audio_frame = av_frame_alloc();
    int yuv_bufsize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w_, h_, 1);
    std::vector<uint8_t> yuvbuf(yuv_bufsize);
    av_image_fill_arrays(yuv->data, yuv->linesize, yuvbuf.data(), AV_PIX_FMT_YUV420P, w_, h_, 1);
    while (!quit_ && net_client_ && net_client_->is_connected()) {
        std::vector<uint8_t> packet_payload;
        uint32_t data_type;
        int64_t received_pts;
        if (net_client_->receive_packet(packet_payload, data_type, received_pts)) {
            if (data_type == 0) { // Video packet
                net_pkt_->data = packet_payload.data();
                net_pkt_->size = packet_payload.size();
                int ret = avcodec_send_packet(net_vctx_, net_pkt_);
                if (ret < 0) continue;
                while (ret >= 0) {
                    ret = avcodec_receive_frame(net_vctx_, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    if (ret < 0) break;
                    sws_scale(sws_, frame->data, frame->linesize, 0, h_, yuv->data, yuv->linesize);
                    AVFrame* yuv_copy = av_frame_alloc();
                    av_frame_copy_props(yuv_copy, yuv);
                    yuv_copy->pts = received_pts;
                    yuv_copy->width = w_; yuv_copy->height = h_; yuv_copy->format = AV_PIX_FMT_YUV420P;
                    for (int i = 0; i < 3; ++i) {
                        int plane_h = (i == 0) ? h_ : h_/2;
                        yuv_copy->linesize[i] = yuv->linesize[i];
                        yuv_copy->data[i] = (uint8_t*)av_malloc((size_t)plane_h * yuv_copy->linesize[i]);
                        memcpy(yuv_copy->data[i], yuv->data[i], (size_t)plane_h * yuv_copy->linesize[i]);
                    }
                    std::lock_guard<std::mutex> lk(mtx_);
                    video_queue_.push_back(yuv_copy);
                }
            } else if (data_type == 1 && net_actx_) { // Audio packet
                net_pkt_->data = packet_payload.data();
                net_pkt_->size = packet_payload.size();
                int ret = avcodec_send_packet(net_actx_, net_pkt_);
                if (ret < 0) continue;
                while (ret >= 0) {
                    ret = avcodec_receive_frame(net_actx_, audio_frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    if (ret < 0) break;
                    if (swr_) {
                        int out_samples = av_rescale_rnd(swr_get_delay(swr_, audio_frame->sample_rate) +
                            audio_frame->nb_samples, audio_sample_rate_, audio_frame->sample_rate, AV_ROUND_UP);
                        std::vector<float> audio_buffer(out_samples * audio_channels_);
                        uint8_t* out_data[1] = { (uint8_t*)audio_buffer.data() };
                        int samples_written = swr_convert(swr_, out_data, out_samples,
                            (const uint8_t**)audio_frame->data, audio_frame->nb_samples);
                        if (samples_written > 0) {
                            audio_buffer.resize(samples_written * audio_channels_);
                            AudioFrame aframe(audio_buffer, received_pts, audio_sample_rate_, audio_channels_);
                            std::lock_guard<std::mutex> lk(mtx_);
                            audio_queue_.push_back(aframe);
                        }
                    }
                }
            }
        } else {
            break;
        }
    }
    av_frame_free(&frame);
    av_frame_free(&yuv);
    av_frame_free(&audio_frame);
}

bool MediaDecoder::readFrame() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (is_network_mode_) {
        // 网络模式：只要有缓存帧就返回true
        return !video_queue_.empty() || !audio_queue_.empty();
    } else {
        AVPacket pkt;
        AVFrame* frame = av_frame_alloc();
        AVFrame* yuv = av_frame_alloc();
        AVFrame* audio_frame = av_frame_alloc();
        int yuv_bufsize = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w_, h_, 1);
        std::vector<uint8_t> yuvbuf(yuv_bufsize);
        av_image_fill_arrays(yuv->data, yuv->linesize, yuvbuf.data(), AV_PIX_FMT_YUV420P, w_, h_, 1);
        bool got_frame = false;
        while (av_read_frame(fmt_, &pkt) >= 0) {
            if (pkt.stream_index == vstream_) {
                if (avcodec_send_packet(vctx_, &pkt) == 0) {
                    while (avcodec_receive_frame(vctx_, frame) == 0) {
                        sws_scale(sws_, frame->data, frame->linesize, 0, h_, yuv->data, yuv->linesize);
                        AVFrame* yuv_copy = av_frame_alloc();
                        av_frame_copy_props(yuv_copy, yuv);
                        yuv_copy->pts = frame->pts;
                        yuv_copy->width = w_; yuv_copy->height = h_; yuv_copy->format = AV_PIX_FMT_YUV420P;
                        for (int i = 0; i < 3; ++i) {
                            int plane_h = (i == 0) ? h_ : h_ / 2;
                            int plane_w = (i == 0) ? w_ : w_ / 2;
                            yuv_copy->linesize[i] = yuv->linesize[i];
                            yuv_copy->data[i] = (uint8_t*)av_malloc(plane_h * plane_w);
                            memcpy(yuv_copy->data[i], yuv->data[i], plane_h * plane_w);
                        }
                        if (video_frame_) av_frame_free(&video_frame_);
                        video_frame_ = yuv_copy;
                        got_frame = true;
                    }
                }
            } else if (pkt.stream_index == astream_ && actx_) {
                if (avcodec_send_packet(actx_, &pkt) == 0) {
                    while (avcodec_receive_frame(actx_, audio_frame) == 0) {
                        if (swr_) {
                            int out_samples = av_rescale_rnd(swr_get_delay(swr_, audio_frame->sample_rate) +
                                audio_frame->nb_samples, audio_sample_rate_, audio_frame->sample_rate, AV_ROUND_UP);
                            std::vector<float> audio_buffer(out_samples * audio_channels_);
                            uint8_t* out_data[1] = { (uint8_t*)audio_buffer.data() };
                            int samples_written = swr_convert(swr_, out_data, out_samples,
                                (const uint8_t**)audio_frame->data, audio_frame->nb_samples);
                            if (samples_written > 0) {
                                audio_buffer.resize(samples_written * audio_channels_);
                                int64_t audio_pts = audio_frame->pts;
                                if (audio_pts != AV_NOPTS_VALUE) {
                                    audio_pts = av_rescale_q(audio_pts, audio_time_base_, time_base_);
                                }
                                audio_frame_ = AudioFrame(audio_buffer, audio_pts, audio_sample_rate_, audio_channels_);
                                got_frame = true;
                            }
                        }
                    }
                }
            }
            av_packet_unref(&pkt);
            if (got_frame) break;
        }
        av_frame_free(&frame);
        av_frame_free(&yuv);
        av_frame_free(&audio_frame);
        return got_frame;
    }
}

AVFrame* MediaDecoder::getVideoFrame() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (is_network_mode_) {
        if (!video_queue_.empty()) {
            AVFrame* f = video_queue_.front();
            video_queue_.erase(video_queue_.begin());
            return f;
        }
        return nullptr;
    } else {
        AVFrame* f = video_frame_;
        video_frame_ = nullptr;
        return f;
    }
}

AudioFrame MediaDecoder::getAudioFrame() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (is_network_mode_) {
        if (!audio_queue_.empty()) {
            AudioFrame af = audio_queue_.front();
            audio_queue_.erase(audio_queue_.begin());
            return af;
        }
        return AudioFrame();
    } else {
        AudioFrame af = audio_frame_;
        audio_frame_ = AudioFrame();
        return af;
    }
}

void MediaDecoder::seek(double seconds) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!is_network_mode_ && fmt_ && vstream_ != -1) {
        int64_t target_ts = seconds / av_q2d(fmt_->streams[vstream_]->time_base);
        av_seek_frame(fmt_, vstream_, target_ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(vctx_);
        if (actx_) avcodec_flush_buffers(actx_);
    }
}

void MediaDecoder::flush() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!is_network_mode_) {
        if (vctx_) avcodec_flush_buffers(vctx_);
        if (actx_) avcodec_flush_buffers(actx_);
    }
}

void MediaDecoder::close() {
    quit_ = true;
    if (net_decode_thread_.joinable()) net_decode_thread_.join();
    if (fmt_) avformat_close_input(&fmt_);
    if (vctx_) avcodec_free_context(&vctx_);
    if (actx_) avcodec_free_context(&actx_);
    if (sws_) sws_freeContext(sws_);
    if (swr_) swr_free(&swr_);
    if (net_vctx_) avcodec_free_context(&net_vctx_);
    if (net_actx_) avcodec_free_context(&net_actx_);
    if (net_pkt_) av_packet_free(&net_pkt_);
    if (video_frame_) av_frame_free(&video_frame_);
    video_queue_.clear();
    audio_queue_.clear();
}

int MediaDecoder::width() const { return w_; }
int MediaDecoder::height() const { return h_; }
int MediaDecoder::sampleRate() const { return audio_sample_rate_; }
int MediaDecoder::channels() const { return audio_channels_; }
AVRational MediaDecoder::videoTimeBase() const { return time_base_; }
AVRational MediaDecoder::audioTimeBase() const { return audio_time_base_; }
bool MediaDecoder::isNetworkMode() const { return is_network_mode_; } 