/*
Prototype video "player" with libav.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>

static yutani_t * yctx;
static yutani_window_t * wina;
static gfx_context_t * ctx;

int decode_frames(int s, AVFormatContext * format_ctx, AVCodecContext * codec_ctx, AVCodec * codec) {

	AVPacket packet;
	AVFrame * frame;
	int framedone;

	struct SwsContext * swctx;

	int linesize[1] = { 4 * frame->width };
	frame = av_frame_alloc();

	if (!frame) {
		fprintf(stderr, "frak, out of memz\n");
	}

	int w = codec_ctx->width;
	int h = codec_ctx->height;

	/* Quick, init swscale */
	fprintf(stderr, "Width = %d, Height = %d, converting from format #%d...\n", frame->width, frame->height, frame->format);

	swctx = sws_getContext(w, h, codec_ctx->pix_fmt, w, h, AV_PIX_FMT_RGB32, 0, 0, 0, 0);

	yctx = yutani_init();
	wina = yutani_window_create(yctx, w, h);
	yutani_window_move(yctx, wina, 300, 300);
	ctx = init_graphics_yutani_double_buffer(wina);

	draw_fill(ctx, rgb(127, 0, 127));
	flip(ctx);
	yutani_flip(yctx, wina);

	uint8_t *dst_data[4];
	int dst_linesize[4];
	av_image_alloc(dst_data, dst_linesize, w, h, AV_PIX_FMT_RGB32, 1);

	fprintf(stderr, "time_base = [%d/%d]\n", format_ctx->streams[s]->time_base.num, format_ctx->streams[s]->time_base.den);


	int i = 0;
	while (1) {
		while (av_read_frame(format_ctx, &packet) >= 0) {
			if (packet.stream_index == s) {
				avcodec_decode_video2(codec_ctx, frame, &framedone, &packet);

				if (framedone) {
					i++;
					printf("\rFrame [%d] pts=%lld", frame->coded_picture_number, frame->pts);
					sws_scale(swctx, (const uint8_t * const *)frame->data, frame->linesize, 0, frame->height, dst_data, dst_linesize);

					memcpy(ctx->backbuffer, dst_data[0], w * h * 4);

					flip(ctx);
					yutani_flip(yctx, wina);

					fflush(stdout);
				}
			}
			av_free_packet(&packet);
		}
		av_seek_frame(format_ctx, s, 0, 0);
	}

	av_free(frame);

	return 0;
}

int main(int argc, char * argv[]) {
	AVFormatContext * format_ctx;
	AVCodecContext * codec_ctx;
	AVCodec * codec;
	int video_stream_index;

	av_register_all();

	format_ctx = avformat_alloc_context();

	if (avformat_open_input(&format_ctx, argv[1], 0, NULL)) {
		return 1;
	}

	if (avformat_find_stream_info(format_ctx, NULL) < 0) {
		return 2;
	}

	av_dump_format(format_ctx, 0, argv[1], 0);

	video_stream_index = -1;
	for (int i = 0; i < format_ctx->nb_streams ; ++i) {
		if (format_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_stream_index = i;
			break;
		}
	}

	if (video_stream_index < 0) {
		return 3;
	}

	codec_ctx = format_ctx->streams[video_stream_index]->codec;
	codec = avcodec_find_decoder(codec_ctx->codec_id);

	int r = avcodec_open2(codec_ctx, codec, NULL);

	if (codec == NULL) {
		return 4;
	}

	/* Frames! */
	decode_frames(video_stream_index, format_ctx, codec_ctx, codec);

	avcodec_close(codec_ctx);
	avformat_close_input(&format_ctx);

	return 0;
}
