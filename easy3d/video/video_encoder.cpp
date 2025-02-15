/********************************************************************
 * Copyright (C) 2015 Liangliang Nan <liangliang.nan@gmail.com>
 * https://3d.bk.tudelft.nl/liangliang/
 *
 * This file is part of Easy3D. If it is useful in your research/work,
 * I would be grateful if you show your appreciation by citing it:
 * ------------------------------------------------------------------
 *      Liangliang Nan.
 *      Easy3D: a lightweight, easy-to-use, and efficient C++ library
 *      for processing and rendering 3D data.
 *      Journal of Open Source Software, 6(64), 3255, 2021.
 * ------------------------------------------------------------------
 *
 * Easy3D is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 3
 * as published by the Free Software Foundation.
 *
 * Easy3D is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 ********************************************************************/

#include <easy3d/video/video_encoder.h>
#include <easy3d/util/logging.h>
#include <easy3d/util/file_system.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}


// This implementation requires ffmpeg v3.4 (Sun, 15 Oct 2017 17:08:45) or above.
// The corresponding libavcodec version is 57.107.100.
#define MIN_REQUIRED_AVCODEC_VERSION    AV_VERSION_INT(57, 107, 100)
#if (LIBAVCODEC_VERSION_INT < MIN_REQUIRED_AVCODEC_VERSION) \
    // this number corresponds to ffmpeg v3.4 (Sun, 15 Oct 2017 17:08:45)
    #pragma message "[WARNING]: Your ffmpeg is too old. Please update it to v3.4 or above"
#endif


namespace internal {

    struct FFmpegStuffEnc {
        AVFormatContext *formatContext;
        AVCodecContext *codecContext;
        AVStream *videoStream;
        AVFrame *frame;
        AVPacket *packet;
        SwsContext *swsContext;

        FFmpegStuffEnc()
                : formatContext(nullptr), codecContext(nullptr), videoStream(nullptr), frame(nullptr), packet(nullptr),
                  swsContext(nullptr) {}
    };


    //! Video encoder based on FFmpeg
    class VideoEncoderImpl {
    public:

        //! Default constructor
        /**
         * \param filename video file name. The video format will be automatically automatically from the extension, e.g.,
         *      - mp4: MPEG-4 Part 14;
         *      - mpeg: MPEG-1 Systems / MPEG program stream;
         *      - avi: Audio Video Interleaved;
         *      - mov: QuickTime / MOV;
         *      Other formats are "h264", "mjpeg", "dvd", "rm", and more. All supported formats can be queried by
         *      calling supported_output_formats(). If it can't be guessed this way then "mp4" is used by default.
         * \param width video width (must be a multiple of 8)
         * \param height video height (must be a multiple of 8)
         * \param fps frame rate
         * \param bitrate bit rate (e.g. 400 000)
         * \param gop keyframe interval
         */
        VideoEncoderImpl(const std::string &filename,
                         int width,
                         int height,
                         int fps = 30,
                         int bitrate = 8000000,
                         int gop = 12);

        virtual ~VideoEncoderImpl();

        /**
         * Creates an (empty) video/animation file
         * \return success
         */
        bool open();

        //! Returns whether the file is opened or not
        inline bool is_open() const { return m_isOpen; }

        /**
         * Adds an image to the stream
         * \param image_data The input image data. It is a 1D array of 'unsigned char' which points to the pixel data.
         *		The pixel data consists of 'height' rows of 'width' pixels, with each pixel has one of the
         *		following structures.
         * \param width video width (must be a multiple of 8)
         * \param height video height (must be a multiple of 8)
         * \param channels the number of channels of the image
         * \param pixel_format pixel format. The correspondences between the image structures and pixel/OpenGL formats are:
         *		    RGB 8:8:8, 24bpp     <--->  PIX_FMT_RGB_888    <--->  GL_RGB
         *          BGR 8:8:8, 24bpp     <--->  PIX_FMT_BGR_888    <--->  GL_BGR
         *          RGBA 8:8:8:8, 32bpp  <--->  PIX_FMT_RGBA_8888  <--->  GL_RGBA
         *          BGRA 8:8:8:8, 32bpp  <--->  PIX_FMT_BGRA_8888  <--->  GL_BGRA
         * \return true on successful.
         **/

         virtual bool encode(const unsigned char *image_data, int width, int height, int channels, AVPixelFormat pixel_format);

        //! Closes the file
        virtual bool close();

        //! Output format
        struct OutputFormat {
            std::string shortName;
            std::string longName;
            std::string extensions;
        };

        //! Returns the list of supported output formats
        static bool supported_output_formats(std::vector<OutputFormat> &formats, bool ignore_if_no_file_extension = true);

    private:
        //! Returns whether the image size is valid
        bool is_size_valid();

        // Alloc/free a frame
        bool init_frame();

        void free_frame();

    private:
        //stream descriptor
        std::string m_filename;
        int m_width;
        int m_height;
        int m_bitrate;
        int m_gop;
        int m_fps;
        bool m_isOpen;

        //! FFmpeg variables
        FFmpegStuffEnc *m_ff;

        friend class easy3d::VideoEncoder;
    };

    static std::string av_error_string(int errnum) {
        char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
        return av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, errnum);
    }

    static int write_frame(FFmpegStuffEnc *ff) {
        if (!ff)
            return 0;

        // rescale output packet timestamp values from codec to stream timebase
        av_packet_rescale_ts(ff->packet, ff->codecContext->time_base, ff->videoStream->time_base);
        ff->packet->stream_index = ff->videoStream->index;

        // write the compressed frame to the media file
        int ret = av_interleaved_write_frame(ff->formatContext, ff->packet);
        /* pkt is now blank (av_interleaved_write_frame() takes ownership of
         * its contents and resets pkt), so that no unreferencing is necessary.
         * This would be different if one used av_write_frame(). */
        if (ret < 0) {
            LOG(ERROR) << "error while writing output packet: " + av_error_string(ret);
            return 0;
        }

        return ret;
    }


    VideoEncoderImpl::VideoEncoderImpl(const std::string &filename, int width, int height, int fps, int bitrate, int gop)
            : m_filename(filename)
            , m_width(width)
            , m_height(height)
            , m_fps(fps)
            , m_bitrate(bitrate)
            , m_gop(gop)
            , m_isOpen(false)
            , m_ff(new FFmpegStuffEnc)
    {
    }

    VideoEncoderImpl::~VideoEncoderImpl() {
        close();
        if (m_ff) {
            delete m_ff;
            m_ff = 0;
        }
    }

    bool VideoEncoderImpl::is_size_valid() {
        return (m_width % 8) == 0 && (m_height % 8) == 0;
    }

    bool VideoEncoderImpl::init_frame() {
        if (!m_ff->codecContext)
            return false;

        if (m_ff->frame) {
            LOG(ERROR) << "frame should not be allocated";
            return false;
        }
        m_ff->frame = av_frame_alloc();
        if (!m_ff->frame) {
            LOG(ERROR) << "could not allocate frame";
            return false;
        }

        m_ff->frame->format = m_ff->codecContext->pix_fmt;
        m_ff->frame->width = m_ff->codecContext->width;
        m_ff->frame->height = m_ff->codecContext->height;
        m_ff->frame->pts = 0;

        // allocate the buffers for the frame data
        int ret = av_frame_get_buffer(m_ff->frame, 0);
        if (ret < 0) {
            LOG(ERROR) << "could not allocate frame data";
            return false;
        }

        return true;
    }

    void VideoEncoderImpl::free_frame() {
        if (m_ff->frame) {
            av_frame_free(&m_ff->frame);
            m_ff->frame = nullptr;
        }
    }

    bool VideoEncoderImpl::supported_output_formats(std::vector<OutputFormat> &formats, bool ignore_if_no_file_extension) {
        try {
            // list of all output formats
            void *ofmt_opaque = nullptr;
            while (true) {
                const AVOutputFormat *format = av_muxer_iterate(&ofmt_opaque);
                if (format) {
                    // potentially skip the output formats without any extension (= test formats mostly)
                    if (format->video_codec != AV_CODEC_ID_NONE
                        && (!ignore_if_no_file_extension || (format->extensions && format->extensions[0] != 0))) {
                        /* find the encoder */
                        auto codec = avcodec_find_encoder(format->video_codec);
                        if (!codec) {
                            // failed to find the codec? Silently skip it
                            continue;
                        }
                        if (codec->type != AVMEDIA_TYPE_VIDEO) {
                            // not a video codec
                            continue;
                        }

                        OutputFormat f;
                        f.shortName = format->name;
                        f.longName = format->long_name;
                        f.extensions = format->extensions;
                        formats.push_back(f);
                    }
                }
                else
                    break;
            }
        }
        catch (const std::bad_alloc &) {
            //not enough memory
            return false;
        }

        return true;
    }

    bool VideoEncoderImpl::open() {
        if (m_isOpen) {
            LOG(ERROR) << "the stream is already opened";
            return false;
        }

        if (!is_size_valid()) {
            LOG(ERROR) << "invalid video size";
            return false;
        }

        // Check if provided file extension is supported
        const std::string extension = easy3d::file_system::extension(m_filename, true); // make sure the extension is lowercase
        std::vector<OutputFormat> formats;
        if (supported_output_formats(formats, true)) {
            bool found = false;
            for (const auto &fmt : formats) {
                if (fmt.extensions.find(extension) != std::string::npos) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                LOG(WARNING) << "output format is not supported: " << extension << ". Popular formats are: mp4, avi, mov, mpeg...";
                return false;
            }
        } else {
            LOG(ERROR) << "could not get the list of supported output formats";
            return false;
        }

        const AVOutputFormat *outputFormat = nullptr;
        if (!extension.empty()) {
            outputFormat = av_guess_format(extension.c_str(), nullptr, nullptr);
            if (!outputFormat)
                LOG(WARNING) << "could not find output format from file extension: " << extension;
        }

#if (LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100))
        // av_register_all() was deprecated since 2018-02-06 - 0694d87024 - lavf 58.9.100 (ffmpeg 4.0) - avformat.h
        // https://github.com/FFmpeg/FFmpeg/blob/70d25268c21cbee5f08304da95be1f647c630c15/doc/APIchanges#L86
        /* Initialize libavcodec, and register all codecs and formats. */
        av_register_all();
#endif

        // find the output format
        avformat_alloc_output_context2(&m_ff->formatContext, const_cast<AVOutputFormat*>(outputFormat), nullptr, outputFormat ? m_filename.c_str() : nullptr);
        if (!m_ff->formatContext) {
            if (!outputFormat) {
                LOG(WARNING) << "could not deduce output format from file extension: using mp4";
                avformat_alloc_output_context2(&m_ff->formatContext, nullptr, "mp4", m_filename.c_str());
                if (!m_ff->formatContext) {
                    LOG(ERROR) << "codec not found (failed to allocate the output media context)";
                    return false;
                }
            } else {
                LOG(ERROR) << "failed to initialize output context with specified output format: " << extension;
                return false;
            }
        }

        if (m_ff->formatContext->oformat->flags & AVFMT_NOFILE) {
            LOG(ERROR) << "codec is not meant to create a video file";
            return false;
        }

        // get the codec
        AVCodecID codec_id = m_ff->formatContext->oformat->video_codec;
        const AVCodec *pCodec = avcodec_find_encoder(codec_id);
        if (!pCodec) {
            LOG(ERROR) << "could not find encoder for " + std::string(avcodec_get_name(codec_id));
            return false;
        }

        // Allocate the AV packet
        m_ff->packet = av_packet_alloc();
        if (!m_ff->packet) {
            LOG(ERROR) << "failed to allocate AVPacket";
            return false;
        }

        // Add the video stream
        m_ff->videoStream = avformat_new_stream(m_ff->formatContext, pCodec);
        if (!m_ff->videoStream) {
            LOG(ERROR) << "failed to allocate output stream";
            return false;
        }
        m_ff->videoStream->id = m_ff->formatContext->nb_streams - 1;

        // Allocate the codec 'context'
        m_ff->codecContext = avcodec_alloc_context3(pCodec);
        if (!m_ff->codecContext) {
            LOG(ERROR) << "failed to allocate encoding context";
            return false;
        }

        m_ff->codecContext->codec_id = codec_id;
        /* put sample parameters */
        m_ff->codecContext->bit_rate = m_bitrate;
        /* resolution must be a multiple of two */
        m_ff->codecContext->width = m_width;
        m_ff->codecContext->height = m_height;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        m_ff->videoStream->time_base = AVRational{1, m_fps};
        /* frames per second */
        m_ff->codecContext->time_base = m_ff->videoStream->time_base;
        /* emit one intra frame every 'gop_size' frames at most */
        m_ff->codecContext->gop_size = m_gop;
        m_ff->codecContext->pix_fmt = AV_PIX_FMT_YUV420P;

        if (codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            /* just for testing, we also add B-frames */
            m_ff->codecContext->max_b_frames = 2;
        } else if (codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            /* Needed to avoid using macroblocks in which some coeffs overflow.
            * This does not happen with normal video, it just happens here as
            * the motion of the chroma plane does not match the luma plane. */
            m_ff->codecContext->mb_decision = FF_MB_DECISION_RD; // rate-distortion optimization for best quality
        }

        // some formats want stream headers to be separate
        if (m_ff->formatContext->oformat->flags & AVFMT_GLOBALHEADER) {
            m_ff->codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        // open the codec
        int ret = avcodec_open2(m_ff->codecContext, pCodec, nullptr);
        if (ret < 0) {
            LOG(ERROR) << "could not open the codec: " << av_error_string(ret);
            return false;
        }

        // allocate and init a re-usable frame
        if (!init_frame()) {
            LOG(ERROR) << "could not init the internal frame";
            return false;
        }

        // copy the stream parameters to the muxer
        if (avcodec_parameters_from_context(m_ff->videoStream->codecpar, m_ff->codecContext) < 0) {
            LOG(ERROR) << "could not copy the stream parameters";
            return false;
        }

        av_dump_format(m_ff->formatContext, 0, m_filename.c_str(), 1);

        // open the output file
        ret = avio_open(&m_ff->formatContext->pb, m_filename.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOG(ERROR) << "could not open file: " << m_filename << ". Error: " << av_error_string(ret);
            return false;
        }

        // write the stream header, if any
        ret = avformat_write_header(m_ff->formatContext, nullptr);
        if (ret < 0) {
            LOG(ERROR) << "could not write header: " << av_error_string(ret);
            return false;
        }

        m_isOpen = true;

        return true;
    }

    bool VideoEncoderImpl::close() {
        if (!m_isOpen) {
            return false;
        }

        // send the frame to the encoder
        int ret = avcodec_send_frame(m_ff->codecContext, nullptr);
        if (ret < 0) {
            LOG(ERROR) << "error sending a frame to the encoder: " << av_error_string(ret);
            return false;
        }

        // delayed frames?
        while (avcodec_receive_packet(m_ff->codecContext, m_ff->packet) >= 0) {
            write_frame(m_ff);
        }

        av_write_trailer(m_ff->formatContext);

        // close the codec
        avcodec_free_context(&m_ff->codecContext);
        m_ff->codecContext = nullptr; // just in case

        // free the streams and other data
        free_frame();
        for (unsigned i = 0; i < m_ff->formatContext->nb_streams; i++)
            av_freep(&m_ff->formatContext->streams[i]);

        av_packet_free(&m_ff->packet);
        m_ff->packet = nullptr; // just in case

        sws_freeContext(m_ff->swsContext);
        m_ff->swsContext = nullptr;

        // close the file
        avio_close(m_ff->formatContext->pb);

        // free the stream
        avformat_free_context(m_ff->formatContext);
        m_ff->formatContext = nullptr;
        m_ff->videoStream = nullptr;

        m_isOpen = false;

        return true;
    }

    bool VideoEncoderImpl::encode(const unsigned char *image_data, int width, int height, int channels, AVPixelFormat pixel_format) {
        // Check if the image matches the size
        if (m_width != width || m_height != height) {
            LOG(ERROR) << "image size (" << width << ", " << height << ") differs from video resolution (" << m_width << ", " << m_height << ")";
            return false;
        }

        if (!is_open()) {
            LOG(ERROR) << "stream is not opened";
            return false;
        }

        /* when we pass a frame to the encoder, it may keep a reference to it
         * internally; make sure we do not overwrite it here */
        if (av_frame_make_writable(m_ff->frame) < 0) {
            LOG(ERROR) << "could not make the frame writable";
            return false;
        }

        /* as we only generate a YUV420P picture, we must convert it
         * to the codec pixel format if needed */
        {
            //Check if context can be reused, otherwise reallocate a new one
            m_ff->swsContext = sws_getCachedContext(m_ff->swsContext,
                                                    m_width,
                                                    m_height,
                                                    pixel_format,
                                                    m_width,
                                                    m_height,
                                                    AV_PIX_FMT_YUV420P,
                                                    SWS_BICUBIC,
                                                    nullptr,
                                                    nullptr,
                                                    nullptr);

            if (m_ff->swsContext == nullptr) {
                LOG(ERROR) << "could not initialize the conversion context";
                return false;
            }

            int num_bytes = av_image_get_buffer_size(pixel_format, m_width, m_height, 1);
            if (num_bytes != width * height * channels) {
                LOG(ERROR) << "number of bytes mismatch";
                return false;
            }

            const uint8_t *srcSlice[3]{image_data, nullptr, nullptr};
            int srcStride[3]{width * channels, 0, 0}; // first element is bytes per line

            sws_scale(m_ff->swsContext,
                      srcSlice,
                      srcStride,
                      0,
                      m_height,
                      m_ff->frame->data,
                      m_ff->frame->linesize);
        }

        // encode the image
        {
            int ret = avcodec_send_frame(m_ff->codecContext, m_ff->frame);
            if (ret < 0) {
                LOG(ERROR) << "error encoding video frame: " << av_error_string(ret);
                return false;
            }

            while (ret >= 0) {
                ret = avcodec_receive_packet(m_ff->codecContext, m_ff->packet);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    break;
                else if (ret < 0) {
                    LOG(ERROR) << "error receiving video frame: " << av_error_string(ret);
                    return false;
                }

                ret = write_frame(m_ff);
                if (ret < 0) {
                    LOG(ERROR) << "error writing video frame: " << av_error_string(ret);
                    return false;
                }
            }
        }

        ++m_ff->frame->pts;
        return true;
    }

}



using namespace internal;

namespace easy3d {

    // Reference: https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/muxing.c
    VideoEncoder::VideoEncoder(const std::string& file_name, int framerate, int bitrate)
            : encoder_(nullptr)
            , file_name_(file_name)
            , framerate_(framerate)
            , bitrate_(bitrate)
    {
        LOG(INFO) << "ffmpeg version: " << av_version_info() << " (avcodec version: " << LIBAVCODEC_VERSION_MAJOR << "."
                  << LIBAVCODEC_VERSION_MINOR << "." << LIBAVCODEC_VERSION_MICRO << ")";

        if (LIBAVCODEC_VERSION_INT < MIN_REQUIRED_AVCODEC_VERSION)
            LOG(WARNING) << "your program was built with too old ffmpeg (" << av_version_info() << "), thus "
                         << "video encoding may not work properly. Contact the author of the program to fix it";
#ifdef NDEBUG
        av_log_set_level(AV_LOG_QUIET);
#else
        av_log_set_level(AV_LOG_DEBUG); // AV_LOG_INFO
#endif

        LOG(INFO) << "output file name: " << file_name_;
        LOG(INFO) << "video framerate: " << framerate_;
        LOG(INFO) << "video bitrate: " << bitrate_ / (1024 * 1024) << " Mbits/sec";
    }


    VideoEncoder::~VideoEncoder() {
        if (encoder_) {
            encoder_->close();
            delete encoder_;
            encoder_ = nullptr;
        }
    }


    bool VideoEncoder::encode(const unsigned char *image_data, int width, int height, PixelFormat pixel_format) {
        if (!is_size_acceptable(width, height)) {
            LOG(ERROR) << "video frame resolution (" << width << ", " << height << ") is not a multiple of 8";
            return false;
        }

        // In this implementation, the creation of the internal encoder can be delayed when the first image is received.
        // This makes it convenient to encode image sequences (because the image size is not know before loading the
        // first image in the sequence).
        if (!encoder_) {
            if (!encoder_) {
                encoder_ = new VideoEncoderImpl(file_name_, width, height, framerate_, bitrate_);
                if (!encoder_) {
                    LOG(ERROR) << "failed to create the internal video encoder";
                    return false;
                }
            }
            if (!encoder_->open()) {
                LOG(ERROR) << "failed to start the internal video encoder";
                return false;
            }
        }

        if (width != encoder_->m_width || height != encoder_->m_height) {
            LOG(ERROR) << "image size differs from the size of the previously created video stream";
            return false;
        }

        int channels = 3;
        AVPixelFormat pix_fmt = AV_PIX_FMT_RGB24;
        switch (pixel_format) {
            case PIX_FMT_RGB_888:
                channels = 3;
                pix_fmt = AV_PIX_FMT_RGB24;
                break;
            case PIX_FMT_BGR_888:
                channels = 3;
                pix_fmt = AV_PIX_FMT_BGR24;
                break;
            case PIX_FMT_RGBA_8888:
                channels = 4;
                pix_fmt = AV_PIX_FMT_RGBA;
                break;
            case PIX_FMT_BGRA_8888:
                channels = 4;
                pix_fmt = AV_PIX_FMT_BGRA;
                break;
        }
        return encoder_->encode(image_data, width, height, channels, pix_fmt);
    }

}
