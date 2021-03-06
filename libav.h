/* MiniDLNA media server
 * Copyright (C) 2013  NETGEAR
 *
 * This file is part of MiniDLNA.
 *
 * MiniDLNA is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MiniDLNA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MiniDLNA. If not, see <http://www.gnu.org/licenses/>.
 */
#if HAVE_FFMPEG_LIBAVUTIL_AVUTIL_H
#include <ffmpeg/libavutil/avutil.h>
#elif HAVE_LIBAV_LIBAVUTIL_AVUTIL_H
#include <libav/libavutil/avutil.h>
#elif HAVE_LIBAVUTIL_AVUTIL_H
#include <libavutil/avutil.h>
#elif HAVE_FFMPEG_AVUTIL_H
#include <ffmpeg/avutil.h>
#elif HAVE_LIBAV_AVUTIL_H
#include <libav/avutil.h>
#elif HAVE_AVUTIL_H
#include <avutil.h>
#endif

#if HAVE_FFMPEG_LIBAVCODEC_AVCODEC_H
#include <ffmpeg/libavcodec/avcodec.h>
#elif HAVE_LIBAV_LIBAVCODEC_AVCODEC_H
#include <libav/libavcodec/avcodec.h>
#elif HAVE_LIBAVCODEC_AVCODEC_H
#include <libavcodec/avcodec.h>
#elif HAVE_FFMPEG_AVCODEC_H
#include <ffmpeg/avcodec.h>
#elif HAVE_LIBAV_AVCODEC_H
#include <libav/avcodec.h>
#elif HAVE_AVCODEC_H
#include <avcodec.h>
#endif

#if HAVE_FFMPEG_LIBAVFORMAT_AVFORMAT_H
#include <ffmpeg/libavformat/avformat.h>
#elif HAVE_LIBAV_LIBAVFORMAT_AVFORMAT_H
#include <libav/libavformat/avformat.h>
#elif HAVE_LIBAVFORMAT_AVFORMAT_H
#include <libavformat/avformat.h>
#elif HAVE_FFMPEG_AVFORMAT_H
#include <ffmpeg/avformat.h>
#elif HAVE_LIBAV_LIBAVFORMAT_H
#include <libav/avformat.h>
#elif HAVE_AVFORMAT_H
#include <avformat.h>
#endif

#include "log.h"

#define USE_CODECPAR LIBAVFORMAT_VERSION_INT >= ((57<<16)+(50<<8)+100)

#ifndef FF_PROFILE_H264_BASELINE
#define FF_PROFILE_H264_BASELINE 66
#endif
#ifndef FF_PROFILE_H264_CONSTRAINED_BASELINE
#define FF_PROFILE_H264_CONSTRAINED_BASELINE 578
#endif
#ifndef FF_PROFILE_H264_MAIN
#define FF_PROFILE_H264_MAIN 77
#endif
#ifndef FF_PROFILE_H264_HIGH
#define FF_PROFILE_H264_HIGH 100
#endif
#ifndef FF_PROFILE_SKIP
#define FF_PROFILE_SKIP -100
#endif

#if LIBAVCODEC_VERSION_MAJOR < 53
#define AVMEDIA_TYPE_AUDIO CODEC_TYPE_AUDIO
#define AVMEDIA_TYPE_VIDEO CODEC_TYPE_VIDEO
#endif

#if LIBAVCODEC_VERSION_INT <= ((51<<16)+(50<<8)+1)
#define CODEC_ID_WMAPRO CODEC_ID_NONE
#endif

#if LIBAVCODEC_VERSION_MAJOR < 55
#define AV_CODEC_ID_AAC CODEC_ID_AAC
#define AV_CODEC_ID_AC3 CODEC_ID_AC3
#define AV_CODEC_ID_ADPCM_IMA_QT CODEC_ID_ADPCM_IMA_QT
#define AV_CODEC_ID_AMR_NB CODEC_ID_AMR_NB
#define AV_CODEC_ID_DTS CODEC_ID_DTS
#define AV_CODEC_ID_H264 CODEC_ID_H264
#define AV_CODEC_ID_MP2 CODEC_ID_MP2
#define AV_CODEC_ID_MP3 CODEC_ID_MP3
#define AV_CODEC_ID_MPEG1VIDEO CODEC_ID_MPEG1VIDEO
#define AV_CODEC_ID_MPEG2VIDEO CODEC_ID_MPEG2VIDEO
#define AV_CODEC_ID_MPEG4 CODEC_ID_MPEG4
#define AV_CODEC_ID_MSMPEG4V3 CODEC_ID_MSMPEG4V3
#define AV_CODEC_ID_PCM_S16LE CODEC_ID_PCM_S16LE
#define AV_CODEC_ID_VC1 CODEC_ID_VC1
#define AV_CODEC_ID_WMAPRO CODEC_ID_WMAPRO
#define AV_CODEC_ID_WMAV1 CODEC_ID_WMAV1
#define AV_CODEC_ID_WMAV2 CODEC_ID_WMAV2
#define AV_CODEC_ID_WMV3 CODEC_ID_WMV3
#endif

#if LIBAVUTIL_VERSION_INT < ((50<<16)+(13<<8)+0)
#define av_strerror(x, y, z) snprintf(y, z, "%d", x)
#endif

#if LIBAVFORMAT_VERSION_INT >= ((52<<16)+(31<<8)+0)
# if LIBAVUTIL_VERSION_INT < ((51<<16)+(5<<8)+0) && !defined(FF_API_OLD_METADATA2)
#define AV_DICT_IGNORE_SUFFIX AV_METADATA_IGNORE_SUFFIX
#define av_dict_get av_metadata_get
typedef AVMetadataTag AVDictionaryEntry;
# endif
#endif

#ifdef PIX_FMT_RGB32_1
#define LAV_PIX_FMT_RGB32_1 PIX_FMT_RGB32_1
#else
#define LAV_PIX_FMT_RGB32_1 AV_PIX_FMT_RGB32_1
#endif

static inline int
lav_open(AVFormatContext **ctx, const char *filename)
{
	int ret;
#if LIBAVFORMAT_VERSION_INT >= ((53<<16)+(17<<8)+0)
	ret = avformat_open_input(ctx, filename, NULL, NULL);
	if (ret == 0)
		avformat_find_stream_info(*ctx, NULL);
#else
	ret = av_open_input_file(ctx, filename, NULL, 0, NULL);
	if (ret == 0)
		av_find_stream_info(*ctx);
#endif
	return ret;
}

static inline void
lav_close(AVFormatContext *ctx)
{
#if LIBAVFORMAT_VERSION_INT >= ((53<<16)+(17<<8)+0)
	avformat_close_input(&ctx);
#else
	av_close_input_file(ctx);
#endif
}

static inline int
lav_get_fps(AVStream *s)
{
#if LIBAVCODEC_VERSION_MAJOR < 54
	if (s->r_frame_rate.den)
		return s->r_frame_rate.num / s->r_frame_rate.den;
#else
	if (s->avg_frame_rate.den)
		return s->avg_frame_rate.num / s->avg_frame_rate.den;
#endif
	return 0;
}

static inline int
lav_get_interlaced(AVStream *s)
{
#if LIBAVCODEC_VERSION_MAJOR >= 57
	return (s->time_base.den ? (s->avg_frame_rate.num / s->time_base.den) : 0);
#elif LIBAVCODEC_VERSION_MAJOR >= 54
	return (s->codec->time_base.den ? (s->avg_frame_rate.num / s->codec->time_base.den) : 0);
#else
	return (s->codec->time_base.den ? (s->r_frame_rate.num / s->codec->time_base.den) : 0);
#endif
}

#if USE_CODECPAR
#define lav_codec_id(s) s->codecpar->codec_id
#define lav_codec_type(s) s->codecpar->codec_type
#define lav_codec_tag(s) s->codecpar->codec_tag
#define lav_sample_rate(s) s->codecpar->sample_rate
#define lav_bit_rate(s) s->codecpar->bit_rate
#define lav_channels(s) s->codecpar->channels
#define lav_width(s) s->codecpar->width
#define lav_height(s) s->codecpar->height
#define lav_profile(s) s->codecpar->profile
#define lav_level(s) s->codecpar->level
#define lav_sample_aspect_ratio(s) s->codecpar->sample_aspect_ratio
#else
#define lav_codec_id(x) x->codec->codec_id
#define lav_codec_type(s) s->codec->codec_type
#define lav_codec_tag(s) s->codec->codec_tag
#define lav_sample_rate(s) s->codec->sample_rate
#define lav_bit_rate(s) s->codec->bit_rate
#define lav_channels(s) s->codec->channels
#define lav_width(s) s->codec->width
#define lav_height(s) s->codec->height
#define lav_profile(s) s->codec->profile
#define lav_level(s) s->codec->level
#define lav_sample_aspect_ratio(s) s->codec->sample_aspect_ratio
#endif

static inline uint8_t *
lav_codec_extradata(AVStream *s)
{
#if USE_CODECPAR
	if (!s->codecpar->extradata_size)
		return NULL;
	return s->codecpar->extradata;
#else
	if (!s->codec->extradata_size)
		return NULL;
	return s->codec->extradata;
#endif
}

static inline int
lav_is_thumbnail_stream(AVStream *s, uint8_t **data, int *size)
{
#if LIBAVFORMAT_VERSION_INT >= ((54<<16)+(25<<8))
	if (s->disposition & AV_DISPOSITION_ATTACHED_PIC &&
	    lav_codec_id(s) == AV_CODEC_ID_MJPEG)
	{
		if (data)
			*data = s->attached_pic.data;
		if (size)
			*size = s->attached_pic.size;
		return 1;
	}
#endif
	return 0;
}

static inline AVFrame*
lav_frame_alloc(void)
{
#if LIBAVCODEC_VERSION_INT >= ((55<<16)+(28<<8)+1)
	return av_frame_alloc();
#else
	return avcodec_alloc_frame();
#endif
}

static inline void
lav_frame_unref(AVFrame *ptr)
{
#if LIBAVCODEC_VERSION_INT >= ((55<<16)+(28<<8)+1)
	return av_frame_unref(ptr);
#else
	return avcodec_get_frame_defaults(ptr);
#endif
}

static inline void
lav_free_packet(AVPacket *pkt)
{
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 8, 0)
	return av_packet_unref(pkt);
#else
	return av_free_packet(pkt);
#endif
}

static inline int
lav_avcodec_open(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options)
{
#if LIBAVCODEC_VERSION_INT >= ((53<<16)+(6<<8)+0)
	return avcodec_open2(avctx, codec, options);
#else
	return avcodec_open(avctx, codec);
#endif
}

static inline int
lav_avcodec_decode_video(AVCodecContext *avctx, AVFrame *picture, int *got_picture_ptr, AVPacket *avpkt)
{
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 36, 100)
	int error = 0;
	if((error = avcodec_send_packet(avctx, avpkt))) return error;
	error = avcodec_receive_frame(avctx, picture);

	// This is a bit tricky, I guess. The new interface does not map very well
	// on the old interface. When we're done with the decoder, we may want to
	// want to drain it (or not), depending on what we want to do with the
	// decoder next. In the old interface this did not seem necessary, or
	// perhaps it happened automatically?
	// In either case, it seems like this function is only used from
	// video_thumb.c::get_video_packet(), which is only interested in decoding
	// a single frame. So for now we don't really need to do this (assuming it
	// happens on close).
	//
	// Note: draining should only be done once a frame has been successfully
	// received, otherwise no frame will ever be received. I.e. if we start
	// emptying the codec before it has had enough data to produce a frame, we
	// will not get any output.
	//if(!error) {
	//	// drain (may fail, don't care)
	//	avcodec_send_packet(avctx, NULL);
	//	AVFrame *dummy = lav_frame_alloc();
	//	//while(avcodec_receive_frame(avctx, dummy) != AVERROR_EOF);
	//	int res = 0;
	//	do {
	//		res = avcodec_receive_frame(avctx, dummy);// XXX unref?
	//		char buf[128];
	//		av_strerror(res, buf, 128);
	//		fprintf(stderr, "lav_avcodec_decode_video: res: %d - %s\n", res, buf);
	//		fflush(stderr);
	//	} while(res != AVERROR_EOF);
	//	lav_frame_unref(dummy);
	//	avcodec_flush_buffers(avctx);
	//}

	if(!error) *got_picture_ptr = 1;
	return error;
#elif LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(52, 23, 0)
	return avcodec_decode_video2(avctx, picture, got_picture_ptr, avpkt);
#else
	return avcodec_decode_video(avctx, picture, got_picture_ptr, avpkt->data, avpkt->size);
#endif
}

static inline int
lav_avpicture_deinterlace(AVFrame *dst, const AVFrame *src, enum AVPixelFormat pix_fmt, int width, int height)
{
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(56, 0, 0)
	// The interlaced_frame flag, which video_thumb.c uses to decide whether or
	// not a frame requires deinterlacing does not seem to mean what one would
	// think, or at least that is what is suggested here:
	// http://libav-users.943685.n4.nabble.com/Libav-user-using-avpicture-deinterlace-td4660459.html
	//
	// I've tried a bunch of videos, and thusfar I've not yet found any video
	// which had this flag set. This is not to say it cannot happen, but rather
	// that at the moment I have no way to test it. If deinterlacing is indeed
	// required, a somewhat complicated setup appears to be required, to set up
	// a filter graph. Until I have a test case this will need to be postponed.
	DPRINTF(E_WARN, L_ARTWORK, "deinterlacing of video frames is not implemented!\n");
	return 0;
#else
	return avpicture_deinterlace((AVPicture*)dst, (AVPicture*)src, pix_fmt, width, height);
#endif
}

// Taken from https://www.ffmpeg.org/doxygen/trunk/libavfilter_2fifo_8c_source.html#l00129
// Did not find a public version (yet).
static inline int
calc_ptr_alignment(AVFrame *frame)
{
    int planes = av_sample_fmt_is_planar(frame->format) ?
                 frame->channels : 1;
    int min_align = 128;
    int p;

    for (p = 0; p < planes; p++) {
        int cur_align = 128;
        while ((intptr_t)frame->extended_data[p] % cur_align)
            cur_align >>= 1;
        if (cur_align < min_align)
            min_align = cur_align;
    }
    return min_align;
}
