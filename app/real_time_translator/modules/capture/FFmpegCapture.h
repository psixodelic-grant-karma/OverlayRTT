#pragma once

#include "../../core/Interfaces.h"
#include <leptonica/allheaders.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <cstring>
#include <iostream>
#include <sstream>
#include <cstdlib>

namespace translator {

/**
 * @brief Захват экрана через FFmpeg (x11grab)
 * 
 * Более надежный метод чем screen_capture_lite на Linux
 */
class FFmpegCapture : public ICapture {
private:
    // FFmpeg контексты
    AVFormatContext* formatCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVFrame* rgbFrame_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    AVPacket* packet_ = nullptr;
    
    int videoStreamIndex_ = -1;
    
    // Состояние
    ModuleState state_ = ModuleState::Idle;
    std::string lastError_;
    std::atomic<bool> running_{false};
    
    // Текущий кадр
    Frame currentFrame_;
    std::mutex frameMutex_;
    
    // Настройки
    int width_ = 0;
    int height_ = 0;
    int fps_ = 30;
    std::string display_ = ":0";
    int x11GrabOffsetX_ = 0;
    int x11GrabOffsetY_ = 0;
    
    // Для отладки
    bool saveDebugImages_ = false;
    std::string debugPath_ = "/tmp/translator";
    int debugCounter_ = 0;
    std::mutex debugMutex_;
    
    // Поток захвата
    std::thread captureThread_;

public:
    FFmpegCapture() {
        avformat_network_init();
        avdevice_register_all();
    }
    
    ~FFmpegCapture() override {
        stop();
        closeCapture();
    }
    
    bool init() override {
        try {
            state_ = ModuleState::Idle;
            lastError_.clear();
            
            // По умолчанию 1920x1080
            width_ = 1920;
            height_ = 1080;
            
            // Читаем DISPLAY из переменной окружения
            const char* displayEnv = std::getenv("DISPLAY");
            if (displayEnv && displayEnv[0] != '\0') {
                display_ = displayEnv;
                std::cout << "[FFmpegCapture] Using DISPLAY=" << display_ << std::endl;
            } else {
                display_ = ":0";
                std::cout << "[FFmpegCapture] Warning: DISPLAY not set, using " << display_ << std::endl;
            }
            
            return true;
        } catch (const std::exception& e) {
            lastError_ = e.what();
            return false;
        }
    }
    
    void start() override {
        if (running_.load()) return;
        
        running_.store(true);
        state_ = ModuleState::Running;
        
        if (!openCapture()) {
            state_ = ModuleState::Error;
            return;
        }
        
        captureThread_ = std::thread(&FFmpegCapture::captureLoop, this);
    }
    
    void stop() override {
        running_.store(false);
        
        if (captureThread_.joinable()) {
            captureThread_.join();
        }
        
        closeCapture();
        state_ = ModuleState::Stopped;
    }
    
    ModuleState getState() const override { return state_; }
    std::string getLastError() const override { return lastError_; }
    
    bool setCaptureSource(const CaptureSource& source) override {
        (void)source;
        return true;
    }
    
    bool setIgnoreRegions(const std::vector<Rect>& regions) override {
        (void)regions;
        return true;
    }
    
    void setCaptureMode(CaptureMode mode) override {
        (void)mode;
    }
    
    Frame getLatestFrame() override {
        std::lock_guard lock(frameMutex_);
        return currentFrame_;
    }
    
    void setOnFrameCallback(std::function<void(const Frame&)> callback) override {
        (void)callback;
    }
    
    void setDebugMode(bool enable, const std::string& path = "/tmp/translator") override {
        saveDebugImages_ = enable;
        debugPath_ = path;
    }

private:
    bool openCapture() {
        // Формат: x11grab:display.screen+x,y или x11grab:display+x,y
        // display_ = ":0" -> URL = "x11grab::0"
        std::string simpleUrl = "x11grab:" + display_;
        
        const AVInputFormat* inputFormat = av_find_input_format("x11grab");
        if (!inputFormat) {
            lastError_ = "Could not find x11grab input format";
            return false;
        }
        
        AVDictionary* options = nullptr;
        
        char videoSize[64];
        snprintf(videoSize, sizeof(videoSize), "%dx%d", width_, height_);
        
        av_dict_set(&options, "framerate", std::to_string(fps_).c_str(), 0);
        av_dict_set(&options, "video_size", videoSize, 0);
        av_dict_set(&options, "capture_cursor", "1", 0);
        av_dict_set(&options, "capture_mouse_clicks", "1", 0);
        
        int ret = avformat_open_input(&formatCtx_, simpleUrl.c_str(), inputFormat, &options);
        if (ret < 0) {
            char errBuf[256];
            av_strerror(ret, errBuf, sizeof(errBuf));
            lastError_ = "Could not open input: " + std::string(errBuf);
            return false;
        }
        
        ret = avformat_find_stream_info(formatCtx_, nullptr);
        if (ret < 0) {
            lastError_ = "Could not find stream info";
            return false;
        }
        
        videoStreamIndex_ = -1;
        for (unsigned int i = 0; i < formatCtx_->nb_streams; i++) {
            if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                videoStreamIndex_ = i;
                break;
            }
        }
        
        if (videoStreamIndex_ == -1) {
            lastError_ = "Could not find video stream";
            return false;
        }
        
        AVCodecParameters* codecParams = formatCtx_->streams[videoStreamIndex_]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
        if (!codec) {
            lastError_ = "Codec not found";
            return false;
        }
        
        codecCtx_ = avcodec_alloc_context3(codec);
        if (!codecCtx_) {
            lastError_ = "Could not allocate codec context";
            return false;
        }
        
        ret = avcodec_parameters_to_context(codecCtx_, codecParams);
        if (ret < 0) {
            lastError_ = "Could not copy codec parameters";
            return false;
        }
        
        ret = avcodec_open2(codecCtx_, codec, nullptr);
        if (ret < 0) {
            lastError_ = "Could not open codec";
            return false;
        }
        
        frame_ = av_frame_alloc();
        rgbFrame_ = av_frame_alloc();
        packet_ = av_packet_alloc();
        
        swsCtx_ = sws_getContext(
            codecCtx_->width, codecCtx_->height, codecCtx_->pix_fmt,
            codecCtx_->width, codecCtx_->height, AV_PIX_FMT_BGRA,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        
        if (!swsCtx_) {
            lastError_ = "Could not create SwsContext";
            return false;
        }
        
        width_ = codecCtx_->width;
        height_ = codecCtx_->height;
        
        return true;
    }
    
    void closeCapture() {
        if (swsCtx_) {
            sws_freeContext(swsCtx_);
            swsCtx_ = nullptr;
        }
        
        if (frame_) {
            av_frame_free(&frame_);
            frame_ = nullptr;
        }
        
        if (rgbFrame_) {
            av_frame_free(&rgbFrame_);
            rgbFrame_ = nullptr;
        }
        
        if (packet_) {
            av_packet_free(&packet_);
            packet_ = nullptr;
        }
        
        if (codecCtx_) {
            avcodec_free_context(&codecCtx_);
            codecCtx_ = nullptr;
        }
        
        if (formatCtx_) {
            avformat_close_input(&formatCtx_);
            formatCtx_ = nullptr;
        }
        
        videoStreamIndex_ = -1;
    }
    
    void captureLoop() {
        std::cout << "[FFmpegCapture] Capture loop started" << std::endl;
        
        while (running_.load()) {
            int ret = av_read_frame(formatCtx_, packet_);
            if (ret < 0) {
                if (running_.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                break;
            }
            
            if (packet_->stream_index == videoStreamIndex_) {
                ret = avcodec_send_packet(codecCtx_, packet_);
                if (ret < 0) {
                    av_packet_unref(packet_);
                    continue;
                }
                
                while (ret >= 0) {
                    ret = avcodec_receive_frame(codecCtx_, frame_);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        break;
                    }
                    
                    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_BGRA, 
                                                           codecCtx_->width, 
                                                           codecCtx_->height, 1);
                    
                    std::vector<uint8_t> buffer(numBytes);
                    
                    av_image_fill_arrays(rgbFrame_->data, rgbFrame_->linesize,
                                        buffer.data(), AV_PIX_FMT_BGRA,
                                        codecCtx_->width, codecCtx_->height, 1);
                    
                    sws_scale(swsCtx_, frame_->data, frame_->linesize,
                             0, codecCtx_->height, rgbFrame_->data, rgbFrame_->linesize);
                    
                    Frame frame;
                    frame.width = codecCtx_->width;
                    frame.height = codecCtx_->height;
                    frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()
                    ).count();
                    frame.sourceRect = {0, 0, frame.width, frame.height};
                    frame.format = PixelFormat::BGRA;
                    
                    size_t dataSize = static_cast<size_t>(frame.width) * frame.height * 4;
                    frame.buffer.resize(dataSize);
                    std::memcpy(frame.buffer.data(), rgbFrame_->data[0], dataSize);
                    
                    {
                        std::lock_guard lock(frameMutex_);
                        currentFrame_ = frame;
                    }
                    
                    if (saveDebugImages_) {
                        saveDebugImage(frame);
                    }
                }
            }
            
            av_packet_unref(packet_);
        }
        
        std::cout << "[FFmpegCapture] Capture loop stopped" << std::endl;
    }
    
    void saveDebugImage(const Frame& frame) {
        if (frame.empty()) return;
        
        int counter;
        {
            std::lock_guard lock(debugMutex_);
            counter = debugCounter_++;
        }
        
        Pix* pix = pixCreate(frame.width, frame.height, 32);
        l_uint32* data = pixGetData(pix);
        int wpl = pixGetWpl(pix);
        
        const uint8_t* src = frame.buffer.data();
        for (int y = 0; y < frame.height; y++) {
            l_uint32* line = data + y * wpl;
            for (int x = 0; x < frame.width; x++) {
                int idx = (y * frame.width + x) * 4;
                l_uint32 pixel = 
                    (static_cast<l_uint32>(src[idx + 2]) << 16) |
                    (static_cast<l_uint32>(src[idx + 1]) << 8) |
                    static_cast<l_uint32>(src[idx + 0]);
                line[x] = pixel;
            }
        }
        
        std::ostringstream oss;
        oss << debugPath_ << "_capture_" << counter << ".png";
        pixWrite(oss.str().c_str(), pix, IFF_PNG);
        pixDestroy(&pix);
    }
};

} // namespace translator
